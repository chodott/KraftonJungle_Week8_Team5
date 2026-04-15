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
#include "Renderer/Features/FireBall/FireBallRenderFeature.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Mesh/MeshData.h"

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
	int32 CurrentFrame,
	const FVector4& Color)
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
	Material.SetParameterData("BaseColor", &Color, sizeof(FVector4));
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
		Component->GetCurrentFrame(),
		Component->GetColor());

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

	MeshBuilder.BuildMeshInputs(BuildContext, Packet, OutSceneViewData);
	TextBuilder.BuildTextInputs(BuildContext, Packet, View, OutSceneViewData);
	SpriteBuilder.BuildSubUVInputs(BuildContext, Packet, View, OutSceneViewData);
	SpriteBuilder.BuildBillboardInputs(BuildContext, Packet, View, OutSceneViewData);
	PostProcessBuilder.BuildFogInputs(Packet, OutSceneViewData);
	PostProcessBuilder.BuildFireBallInputs(Packet, OutSceneViewData);
	PostProcessBuilder.BuildDecalInputs(Packet, OutSceneViewData);
	OutSceneViewData.PostProcessInputs.bApplyFXAA = Packet.bApplyFXAA;
}
