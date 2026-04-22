#pragma once

#include "CoreMinimal.h"
#include "Level/SceneRenderPacket.h"
#include "Math/LinearColor.h"
#include "Renderer/Common/RenderFeatureInterfaces.h"
#include "Renderer/Scene/SceneViewData.h"
#include "Renderer/Scene/Builders/SceneCommandMeshBuilder.h"
#include "Renderer/Scene/Builders/SceneCommandPostProcessBuilder.h"
#include "Renderer/Scene/Builders/SceneCommandSpriteBuilder.h"
#include "Renderer/Scene/Builders/SceneCommandTextBuilder.h"
#include "Renderer/Scene/Builders/SceneCommandLightingBuilder.h"

#include <memory>

class FDynamicMaterial;
class FMaterial;
struct FMaterialTexture;
class UBillboardComponent;
class USubUVComponent;
class UWorld;
struct FSceneCommandBuildContext;

class ENGINE_API FSceneCommandResourceCache
{
public:
	FMaterial* GetOrCreateTextMaterial(const FSceneCommandBuildContext& BuildContext, const FLinearColor& TextColor);
	FMaterial* GetOrCreateSubUVMaterial(const FSceneCommandBuildContext& BuildContext, const USubUVComponent* Component);
	FMaterial* GetOrCreateMeshDecalMaterial(const FSceneCommandBuildContext& BuildContext, const class UMeshDecalComponent* Component);
	void       PruneStaleSubUVMaterials(const TArray<const USubUVComponent*>& ActiveComponents);

private:
	static uint32 ToColorKey(const FLinearColor& Color);
	static void   UpdateSubUVMaterialParams(
		FMaterial&      Material,
		int32           Columns,
		int32           Rows,
		int32           CurrentFrame,
		const FLinearColor& Color);

	TMap<uint32, std::shared_ptr<FDynamicMaterial>>                 TextMaterialsByColor;
	TMap<const USubUVComponent*, std::shared_ptr<FDynamicMaterial>> SubUVMaterialsByComponent;
	TMap<const class UMeshDecalComponent*, std::shared_ptr<FDynamicMaterial>> MeshDecalMaterialsByComponent;
	TMap<std::wstring, std::shared_ptr<FMaterialTexture>> MeshDecalTextureByPath;
};

struct ENGINE_API FSceneCommandBuildContext
{
	FMaterial*                  DefaultMaterial        = nullptr;
	FMaterial*                  DefaultTextureMaterial = nullptr;
	ISceneTextFeature*          TextFeature            = nullptr;
	ISceneSubUVFeature*         SubUVFeature           = nullptr;
	ISceneBillboardFeature*     BillboardFeature       = nullptr;
	FSceneCommandResourceCache* ResourceCache          = nullptr;
	float                       TotalTimeSeconds       = 0.0f;
	UWorld*                     World                  = nullptr;
};

class ENGINE_API FSceneCommandBuilder
{
public:
	void BuildSceneViewData(
		const FSceneCommandBuildContext& BuildContext,
		const FSceneRenderPacket&        Packet,
		const FFrameContext&             Frame,
		const FViewContext&              View,
		FSceneViewData&                  OutSceneViewData);

private:
	FSceneCommandMeshBuilder        MeshBuilder;
	FSceneCommandTextBuilder        TextBuilder;
	FSceneCommandSpriteBuilder      SpriteBuilder;
	FSceneCommandPostProcessBuilder PostProcessBuilder;
	FSceneCommandLightingBuilder    LightingBuilder;
};
