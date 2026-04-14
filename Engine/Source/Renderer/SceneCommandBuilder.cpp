#include "Renderer/SceneCommandBuilder.h"

#include <algorithm>

#include "Component/BillboardComponent.h"
#include "Component/DecalComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextComponent.h"
#include "Component/FireBallComponent.h"
#include "Component/UUIDBillboardComponent.h"
#include "Feature/FireballRenderFeature.h"
#include "Renderer/Material.h"
#include "Renderer/MeshData.h"

namespace
{
	static uint8 ToColorChannel(float Value)
	{
		const float Clamped = (std::max)(0.0f, (std::min)(1.0f, Value));
		return static_cast<uint8>(Clamped * 255.0f + 0.5f);
	}
}

uint32 FSceneCommandResourceCache::ToColorKey(const FVector4& Color)
{
	const uint32 A = static_cast<uint32>(ToColorChannel(Color.W));
	const uint32 R = static_cast<uint32>(ToColorChannel(Color.X));
	const uint32 G = static_cast<uint32>(ToColorChannel(Color.Y));
	const uint32 B = static_cast<uint32>(ToColorChannel(Color.Z));
	return (A << 24) | (R << 16) | (G << 8) | B;
}

void FSceneCommandResourceCache::UpdateSubUVMaterialParams(
	FMaterial& Material,
	int32 Columns,
	int32 Rows,
	int32 CurrentFrame)
{
	if (Columns <= 0 || Rows <= 0)
	{
		return;
	}

	const int32 SafeColumns = (std::max)(1, Columns);
	const int32 SafeRows = (std::max)(1, Rows);
	const int32 MaxFrameIndex = SafeColumns * SafeRows - 1;
	const int32 SafeFrameIndex = (std::max)(0, (std::min)(CurrentFrame, MaxFrameIndex));

	const int32 Col = SafeFrameIndex % SafeColumns;
	const int32 Row = SafeFrameIndex / SafeColumns;

	const FVector2 CellSize(1.0f / static_cast<float>(SafeColumns), 1.0f / static_cast<float>(SafeRows));
	const FVector2 UVOffset(static_cast<float>(Col) * CellSize.X, static_cast<float>(Row) * CellSize.Y);

	Material.SetParameterData("CellSize", &CellSize, sizeof(FVector2));
	Material.SetParameterData("UVOffset", &UVOffset, sizeof(FVector2));
}

FMaterial* FSceneCommandResourceCache::GetOrCreateTextMaterial(const FSceneCommandBuildContext& BuildContext, const FVector4& TextColor)
{
	if (!BuildContext.TextFeature)
	{
		return nullptr;
	}

	const uint32 ColorKey = ToColorKey(TextColor);
	const auto Found = TextMaterialsByColor.find(ColorKey);
	if (Found != TextMaterialsByColor.end())
	{
		return Found->second.get();
	}

	FMaterial* BaseFontMaterial = BuildContext.TextFeature->GetBaseMaterial();
	if (!BaseFontMaterial)
	{
		return nullptr;
	}

	std::unique_ptr<FDynamicMaterial> OwnedMaterial = BaseFontMaterial->CreateDynamicMaterial();
	if (!OwnedMaterial)
	{
		return BaseFontMaterial;
	}

	std::shared_ptr<FDynamicMaterial> Material(OwnedMaterial.release());
	Material->SetVectorParameter("TextColor", TextColor);

	FDynamicMaterial* RawMaterial = Material.get();
	TextMaterialsByColor[ColorKey] = std::move(Material);
	return RawMaterial;
}

FMaterial* FSceneCommandResourceCache::GetOrCreateSubUVMaterial(
	const FSceneCommandBuildContext& BuildContext,
	const USubUVComponent* Component)
{
	if (!Component || !BuildContext.SubUVFeature)
	{
		return nullptr;
	}

	FMaterial* BaseSubUVMaterial = BuildContext.SubUVFeature->GetBaseMaterial();
	if (!BaseSubUVMaterial)
	{
		return nullptr;
	}

	auto Found = SubUVMaterialsByComponent.find(Component);
	if (Found == SubUVMaterialsByComponent.end())
	{
		std::unique_ptr<FDynamicMaterial> OwnedMaterial = BaseSubUVMaterial->CreateDynamicMaterial();
		if (!OwnedMaterial)
		{
			return BaseSubUVMaterial;
		}

		std::shared_ptr<FDynamicMaterial> Material(OwnedMaterial.release());
		Found = SubUVMaterialsByComponent.emplace(Component, std::move(Material)).first;
	}

	FDynamicMaterial* Material = Found->second.get();
	if (!Material)
	{
		return BaseSubUVMaterial;
	}

	UpdateSubUVMaterialParams(
		*Material,
		Component->GetColumns(),
		Component->GetRows(),
		Component->GetCurrentFrame());

	return Material;
}

void FSceneCommandResourceCache::PruneStaleSubUVMaterials(const TArray<const USubUVComponent*>& ActiveComponents)
{
	for (auto It = SubUVMaterialsByComponent.begin(); It != SubUVMaterialsByComponent.end();)
	{
		if (std::find(ActiveComponents.begin(), ActiveComponents.end(), It->first) == ActiveComponents.end())
		{
			It = SubUVMaterialsByComponent.erase(It);
			continue;
		}

		++It;
	}
}

void FSceneCommandBuilder::FinalizeBatchMaterial(const FSceneCommandBuildContext& BuildContext, FMeshBatch& Batch)
{
	if (!Batch.Material)
	{
		Batch.Material = BuildContext.DefaultMaterial;
	}
}

void FSceneCommandBuilder::BuildSceneViewData(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket& Packet,
	const FFrameContext& Frame,
	const FViewContext& View,
	FSceneViewData& OutSceneViewData)
{
	OutSceneViewData.Frame = Frame;
	OutSceneViewData.View = View;
	OutSceneViewData.MeshInputs.Clear();
	OutSceneViewData.PostProcessInputs.Clear();
	OutSceneViewData.DebugInputs.Clear();

	const FVector& CameraPosition = View.CameraPosition;

	const auto AddBatch = [&](FMeshBatch&& Batch)
	{
		FinalizeBatchMaterial(BuildContext, Batch);
		if (!Batch.Mesh || !Batch.Material)
		{
			return;
		}

		Batch.SubmissionOrder = static_cast<uint64>(OutSceneViewData.MeshInputs.Batches.size());
		OutSceneViewData.MeshInputs.Batches.push_back(std::move(Batch));
	};

	for (const FSceneMeshPrimitive& Primitive : Packet.MeshPrimitives)
	{
		UStaticMeshComponent* MeshComponent = Primitive.Component;
		if (!MeshComponent)
		{
			continue;
		}

		FRenderMesh* TargetMesh = MeshComponent->GetRenderMesh();
		if (!TargetMesh)
		{
			continue;
		}

		const int32 SectionCount = TargetMesh->GetNumSection();
		if (SectionCount <= 0)
		{
			FMeshBatch Batch;
			Batch.Mesh = TargetMesh;
			Batch.World = MeshComponent->GetRenderWorldTransform();
			std::shared_ptr<FMaterial> Material = MeshComponent->GetMaterial(0);
			Batch.Material = Material ? Material.get() : BuildContext.DefaultMaterial;
			Batch.Domain = EMaterialDomain::Opaque;
			Batch.PassMask =
				static_cast<uint32>(EMeshPassMask::DepthPrepass) |
				static_cast<uint32>(EMeshPassMask::GBuffer) |
				static_cast<uint32>(EMeshPassMask::ForwardOpaque);
			AddBatch(std::move(Batch));
			continue;
		}

		for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
		{
			const FMeshSection& Section = TargetMesh->Sections[SectionIndex];

			FMeshBatch Batch;
			Batch.Mesh = TargetMesh;
			Batch.World = MeshComponent->GetRenderWorldTransform();
			Batch.SectionIndex = static_cast<uint32>(SectionIndex);
			Batch.IndexStart = Section.StartIndex;
			Batch.IndexCount = Section.IndexCount;

			std::shared_ptr<FMaterial> Material = MeshComponent->GetMaterial(SectionIndex);
			Batch.Material = Material ? Material.get() : BuildContext.DefaultMaterial;
			Batch.Domain = EMaterialDomain::Opaque;
			Batch.PassMask =
				static_cast<uint32>(EMeshPassMask::DepthPrepass) |
				static_cast<uint32>(EMeshPassMask::GBuffer) |
				static_cast<uint32>(EMeshPassMask::ForwardOpaque);
			AddBatch(std::move(Batch));
		}
	}

	for (const FSceneTextPrimitive& Primitive : Packet.TextPrimitives)
	{
		UTextRenderComponent* TextComponent = Primitive.Component;
		if (!TextComponent)
		{
			continue;
		}

		FRenderMesh* TextMesh = TextComponent->GetRenderMesh();
		if (!TextMesh)
		{
			continue;
		}

		if (TextComponent->IsTextMeshDirty())
		{
			if (BuildContext.TextFeature && BuildContext.TextFeature->BuildMesh(
				TextComponent->GetDisplayText(),
				*TextMesh,
				1.0f,
				TextComponent->GetHorizontalAlignment(),
				TextComponent->GetVerticalAlignment()))
			{
				TextMesh->bIsDirty = true;
				TextComponent->ClearTextMeshDirty();
			}
		}

		if (TextMesh->Vertices.empty())
		{
			continue;
		}

		FMaterial* TextMaterial = BuildContext.ResourceCache
			? BuildContext.ResourceCache->GetOrCreateTextMaterial(BuildContext, TextComponent->GetTextColor())
			: nullptr;
		if (!TextMaterial && BuildContext.TextFeature)
		{
			TextMaterial = BuildContext.TextFeature->GetBaseMaterial();
		}
		if (!TextMaterial)
		{
			continue;
		}

		FMeshBatch Batch;
		Batch.Mesh = TextMesh;
		Batch.Material = TextMaterial;
		Batch.Domain = TextComponent->IsA(UUUIDBillboardComponent::StaticClass()) ? EMaterialDomain::Overlay : EMaterialDomain::Opaque;
		Batch.PassMask = Batch.Domain == EMaterialDomain::Overlay
			? static_cast<uint32>(EMeshPassMask::Overlay)
			: static_cast<uint32>(EMeshPassMask::ForwardOpaque);
		if (Batch.Domain == EMaterialDomain::Overlay)
		{
			Batch.bDisableDepthTest = true;
			Batch.bDisableDepthWrite = true;
		}

		const FVector WorldPosition = TextComponent->GetRenderWorldPosition();
		const FVector WorldScale = TextComponent->GetRenderWorldScale();
		if (TextComponent->IsBillboard())
		{
			Batch.World = FMatrix::MakeScale(WorldScale) * FMatrix::MakeBillboard(WorldPosition, CameraPosition);
		}
		else
		{
			const float TextScale = TextComponent->GetTextScale();
			Batch.World = FMatrix::MakeScale(FVector(TextScale, TextScale, TextScale)) * TextComponent->GetWorldTransform();
		}

		AddBatch(std::move(Batch));
	}

	TArray<const USubUVComponent*> ActiveSubUVComponents;
	ActiveSubUVComponents.reserve(Packet.SubUVPrimitives.size());

	for (const FSceneSubUVPrimitive& Primitive : Packet.SubUVPrimitives)
	{
		USubUVComponent* SubUVComponent = Primitive.Component;
		if (!SubUVComponent)
		{
			continue;
		}

		FRenderMesh* SubUVMesh = SubUVComponent->GetSubUVMesh();
		if (!SubUVMesh)
		{
			continue;
		}

		if (SubUVComponent->IsSubUVMeshDirty())
		{
			if (!BuildContext.SubUVFeature || !BuildContext.SubUVFeature->BuildMesh(SubUVComponent->GetSize(), *SubUVMesh))
			{
				continue;
			}

			SubUVMesh->bIsDirty = true;
			SubUVComponent->ClearSubUVMeshDirty();
		}

		FMaterial* SubUVMaterial = BuildContext.ResourceCache
			? BuildContext.ResourceCache->GetOrCreateSubUVMaterial(BuildContext, SubUVComponent)
			: nullptr;
		if (!SubUVMaterial && BuildContext.SubUVFeature)
		{
			SubUVMaterial = BuildContext.SubUVFeature->GetBaseMaterial();
		}
		if (!SubUVMaterial)
		{
			continue;
		}

		FMeshBatch Batch;
		Batch.Mesh = SubUVMesh;
		Batch.Material = SubUVMaterial;
		Batch.Domain = EMaterialDomain::Transparent;
		Batch.PassMask = static_cast<uint32>(EMeshPassMask::ForwardTransparent);
		Batch.bDisableDepthWrite = true;
		Batch.World = SubUVComponent->GetWorldTransform();

		if (SubUVComponent->IsBillboard())
		{
			const FVector WorldPosition = Batch.World.GetTranslation();
			const FVector Scale = Batch.World.GetScaleVector();
			Batch.World = FMatrix::MakeScale(Scale) * FMatrix::MakeBillboard(WorldPosition, CameraPosition);
		}

		const FVector WorldPosition = Batch.World.GetTranslation();
		Batch.DistanceSqToCamera = (WorldPosition - CameraPosition).SizeSquared();

		AddBatch(std::move(Batch));
		ActiveSubUVComponents.push_back(SubUVComponent);
	}

	if (BuildContext.ResourceCache)
	{
		BuildContext.ResourceCache->PruneStaleSubUVMaterials(ActiveSubUVComponents);
	}

	TArray<const UBillboardComponent*> ActiveBillboardComponents;
	ActiveBillboardComponents.reserve(Packet.BillboardPrimitives.size());

	for (const FSceneBillboardPrimitive& Primitive : Packet.BillboardPrimitives)
	{
		UBillboardComponent* BillboardComponent = Primitive.Component;
		if (!BillboardComponent)
		{
			continue;
		}

		FRenderMesh* BillboardMesh = BillboardComponent->GetBillboardMesh();
		if (!BillboardMesh || !BuildContext.BillboardFeature)
		{
			continue;
		}

		if (BillboardComponent->IsBillboardMeshDirty())
		{
			if (!BuildContext.BillboardFeature->BuildMesh(BillboardComponent->GetSize(), *BillboardMesh))
			{
				continue;
			}

			BillboardMesh->bIsDirty = true;
			BillboardComponent->ClearBillboardMeshDirty();
		}

		FMaterial* BillboardMaterial = BuildContext.BillboardFeature->GetOrCreateMaterial(*BillboardComponent);
		if (!BillboardMaterial)
		{
			continue;
		}

		FMeshBatch Batch;
		Batch.Mesh = BillboardMesh;
		Batch.Material = BillboardMaterial;
		Batch.Domain = EMaterialDomain::Transparent;
		Batch.PassMask = static_cast<uint32>(EMeshPassMask::ForwardTransparent);
		Batch.bDisableDepthWrite = true;

		const FVector WorldPosition = BillboardComponent->GetWorldTransform().GetTranslation();
		const FVector Scale = BillboardComponent->GetRenderWorldScale();
		Batch.World = FMatrix::MakeScale(Scale) * FMatrix::MakeBillboard(WorldPosition, CameraPosition);
		Batch.DistanceSqToCamera = (WorldPosition - CameraPosition).SizeSquared();

		AddBatch(std::move(Batch));
		ActiveBillboardComponents.push_back(BillboardComponent);
	}

	if (BuildContext.BillboardFeature)
	{
		BuildContext.BillboardFeature->PruneMaterials(ActiveBillboardComponents);
	}

	OutSceneViewData.PostProcessInputs.FogItems.reserve(Packet.FogPrimitives.size());
	for (const FSceneFogPrimitive& Primitive : Packet.FogPrimitives)
	{
		const UHeightFogComponent* FogComponent = Primitive.Component;
		if (!FogComponent)
		{
			continue;
		}

		FFogRenderItem& Item = OutSceneViewData.PostProcessInputs.FogItems.emplace_back();
		Item.FogOrigin = FogComponent->GetWorldLocation();
		Item.FogDensity = FogComponent->FogDensity;
		Item.FogHeightFalloff = FogComponent->FogHeightFalloff;
		Item.StartDistance = FogComponent->StartDistance;
		Item.FogCutoffDistance = FogComponent->FogCutoffDistance;
		Item.FogMaxOpacity = FogComponent->FogMaxOpacity;
		Item.FogInscatteringColor = FogComponent->FogInscatteringColor;
		Item.AllowBackground = FogComponent->AllowBackground;
	}
	
	OutSceneViewData.PostProcessInputs.FireBallItems.reserve(Packet.FireBAllPrimitives.size());
	for (const FSceneFireBallPrimitive& Primitive : Packet.FireBAllPrimitives)
	{
		const UFireBallComponent* FireballComponent = Primitive.Component;
		if (!FireballComponent)
		{
			continue;
		}
		
		FFireBallRenderItem& Item = OutSceneViewData.PostProcessInputs.FireBallItems.emplace_back();
		Item.Color = FireballComponent->GetColor();
		Item.FireballOrigin = FireballComponent->GetWorldLocation();
		Item.Intensity = FireballComponent->GetIntensity();
		Item.Radius = FireballComponent->GetRadius();
		Item.RadiusFallOff = FireballComponent->GetRadiusFallOff();
	}

	OutSceneViewData.PostProcessInputs.DecalItems.reserve(Packet.DecalPrimitives.size());
	for (const FSceneDecalPrimitive& Primitive : Packet.DecalPrimitives)
	{
		const UDecalComponent* DecalComponent = Primitive.Component;
		if (!DecalComponent || !DecalComponent->IsEnabled())
		{
			continue;
		}

		FDecalRenderItem& Item = OutSceneViewData.PostProcessInputs.DecalItems.emplace_back();
		Item.AtlasScaleBias = DecalComponent->GetAtlasScaleBias();
		Item.BaseColorTint = DecalComponent->GetBaseColorTint();
		Item.DecalWorld = DecalComponent->GetWorldTransform();
		Item.EdgeFade = DecalComponent->GetEdgeFade();
		Item.EmissiveBlend = DecalComponent->GetEmissiveBlend();
		Item.Extents = DecalComponent->GetExtents();
		Item.Flags = DecalComponent->GetRenderFlags();
		Item.NormalBlend = DecalComponent->GetNormalBlend();
		Item.Priority = DecalComponent->GetPriority();
		Item.ReceiverLayerMask = DecalComponent->GetReceiverLayerMask();
		Item.RoughnessBlend = DecalComponent->GetRoughnessBlend();
		Item.TexturePath = DecalComponent->GetTexturePath();
		Item.TextureIndex = 0;
		Item.WorldToDecal = Item.DecalWorld.GetInverse();
		Item.bIsFading = DecalComponent->GetFadeState() != EDecalFadeState::None;
		const float AngleRad = DecalComponent->GetAllowAngle() * (3.14159265f / 180.0f);
		Item.AllowAngle = std::cos(AngleRad);
	}
	OutSceneViewData.PostProcessInputs.bApplyFXAA = Packet.bApplyFXAA;
}
