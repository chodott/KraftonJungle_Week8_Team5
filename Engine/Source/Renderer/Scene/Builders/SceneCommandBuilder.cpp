#include "Renderer/Scene/Builders/SceneCommandBuilder.h"

#include <algorithm>

#include "Renderer/Resources/Material/Material.h"
#include "Component/BillboardComponent.h"
#include "Component/DecalComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextComponent.h"
#include "Component/FireBallComponent.h"
#include "Component/UUIDBillboardComponent.h"
#include "Debug/EngineLog.h"
#include "Renderer/Features/FireBall/FireBallRenderFeature.h"
#include "Renderer/Mesh/MeshData.h"

namespace
{
	uint8 ToColorChannel(float Value)
	{
		const float Clamped = (std::max)(0.0f, (std::min)(1.0f, Value));
		return static_cast<uint8>(Clamped * 255.0f + 0.5f);
	}

	bool IntersectBoundsWithCullSphere(
		const FBoxSphereBounds& Bounds,
		const FVector&          SphereCenter,
		float                   SphereRadius
	)
	{
		const float CombinedRadius = Bounds.Radius + SphereRadius;
		return FVector::DistSquared(Bounds.Center, SphereCenter) <= CombinedRadius * CombinedRadius;
	}

	void BuildObjectLightLists(FSceneViewData& SceneViewData)
	{
		auto& Batches = SceneViewData.MeshInputs.Batches;
		auto& Lights  = SceneViewData.LightingInputs.LocalLights;
		auto& Words   = SceneViewData.LightingInputs.ObjectLightMaskWords;

		Words.clear();
		Words.resize(Batches.size() * LightMaskConfig::MaskWordCount, 0u);

		if (Lights.size() > LightMaskConfig::MaxLocalLights)
		{
			UE_LOG("[Lighting] Local light count (%zu) exceeds MaxLocalLights (%u). Extra lights will be ignored.",
			       Lights.size(),
			       LightMaskConfig::MaxLocalLights);
		}

		for (SIZE_T BatchIndex = 0; BatchIndex < Batches.size(); ++BatchIndex)
		{
			FMeshBatch& Batch          = Batches[BatchIndex];
			Batch.LocalLightMaskOffset = static_cast<uint32>(BatchIndex * LightMaskConfig::MaskWordCount);

			for (uint32 LightIndex = 0; LightIndex < static_cast<uint32>(Lights.size()) && LightIndex < LightMaskConfig::MaxLocalLights; ++LightIndex)
			{
				const FLocalLightRenderItem& L = Lights[LightIndex];
				if (IntersectBoundsWithCullSphere(Batch.WorldBounds, L.CullCenterWS, L.CullRadius))
				{
					const uint32 WordIndex = Batch.LocalLightMaskOffset + (LightIndex >> 5);
					const uint32 BitIndex  = LightIndex & 31u;
					Words[WordIndex]       |= (1u << BitIndex);
				}
			}
		}
	}
}

FMaterial* FSceneCommandResourceCache::GetOrCreateTextMaterial(const FSceneCommandBuildContext& BuildContext, const FVector4& TextColor)
{
	if (!BuildContext.TextFeature)
	{
		return nullptr;
	}

	const uint32 ColorKey = ToColorKey(TextColor);
	const auto   Found    = TextMaterialsByColor.find(ColorKey);
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

	FDynamicMaterial* RawMaterial  = Material.get();
	TextMaterialsByColor[ColorKey] = std::move(Material);
	return RawMaterial;
}

FMaterial* FSceneCommandResourceCache::GetOrCreateSubUVMaterial(
	const FSceneCommandBuildContext& BuildContext,
	const USubUVComponent*           Component)
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

uint32 FSceneCommandResourceCache::ToColorKey(const FVector4& Color)
{
	const uint32 A = ToColorChannel(Color.W);
	const uint32 R = ToColorChannel(Color.X);
	const uint32 G = ToColorChannel(Color.Y);
	const uint32 B = ToColorChannel(Color.Z);
	return (A << 24) | (R << 16) | (G << 8) | B;
}

void FSceneCommandResourceCache::UpdateSubUVMaterialParams(
	FMaterial& Material,
	int32      Columns,
	int32      Rows,
	int32      CurrentFrame)
{
	if (Columns <= 0 || Rows <= 0)
	{
		return;
	}

	const int32 SafeColumns    = (std::max)(1, Columns);
	const int32 SafeRows       = (std::max)(1, Rows);
	const int32 MaxFrameIndex  = SafeColumns * SafeRows - 1;
	const int32 SafeFrameIndex = (std::max)(0, (std::min)(CurrentFrame, MaxFrameIndex));

	const int32 Col = SafeFrameIndex % SafeColumns;
	const int32 Row = SafeFrameIndex / SafeColumns;

	const FVector2 CellSize(1.0f / static_cast<float>(SafeColumns), 1.0f / static_cast<float>(SafeRows));
	const FVector2 UVOffset(static_cast<float>(Col) * CellSize.X, static_cast<float>(Row) * CellSize.Y);

	Material.SetParameterData("CellSize", &CellSize, sizeof(FVector2));
	Material.SetParameterData("UVOffset", &UVOffset, sizeof(FVector2));
}

void FSceneCommandBuilder::BuildSceneViewData(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket&        Packet,
	const FFrameContext&             Frame,
	const FViewContext&              View,
	FSceneViewData&                  OutSceneViewData)
{
	OutSceneViewData.Frame = Frame;
	OutSceneViewData.View  = View;
	OutSceneViewData.MeshInputs.Clear();
	OutSceneViewData.LightingInputs.Clear();
	OutSceneViewData.PostProcessInputs.Clear();
	OutSceneViewData.DebugInputs.Clear();

	MeshBuilder.BuildMeshInputs(BuildContext, Packet, OutSceneViewData);
	LightingBuilder.BuildLightingInputs(BuildContext, Packet, View, OutSceneViewData);
	TextBuilder.BuildTextInputs(BuildContext, Packet, View, OutSceneViewData);
	SpriteBuilder.BuildSubUVInputs(BuildContext, Packet, View, OutSceneViewData);
	SpriteBuilder.BuildBillboardInputs(BuildContext, Packet, View, OutSceneViewData);
	PostProcessBuilder.BuildFogInputs(Packet, OutSceneViewData);
	PostProcessBuilder.BuildFireBallInputs(Packet, OutSceneViewData);
	PostProcessBuilder.BuildDecalInputs(Packet, OutSceneViewData);

	BuildObjectLightLists(OutSceneViewData);

	OutSceneViewData.PostProcessInputs.bApplyFXAA = Packet.bApplyFXAA;
}
