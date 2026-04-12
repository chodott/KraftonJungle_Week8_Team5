#pragma once

#include "CoreMinimal.h"
#include "Level/SceneRenderPacket.h"
#include "Renderer/RenderFeatureInterfaces.h"
#include "Renderer/SceneViewData.h"

#include <memory>

class FDynamicMaterial;
class FMaterial;
class UBillboardComponent;
class USubUVComponent;
struct FSceneCommandBuildContext;

class ENGINE_API FSceneCommandResourceCache
{
public:
	FMaterial* GetOrCreateTextMaterial(const FSceneCommandBuildContext& BuildContext, const FVector4& TextColor);
	FMaterial* GetOrCreateSubUVMaterial(const FSceneCommandBuildContext& BuildContext, const USubUVComponent* Component);
	void PruneStaleSubUVMaterials(const TArray<const USubUVComponent*>& ActiveComponents);

private:
	static uint32 ToColorKey(const FVector4& Color);
	static void UpdateSubUVMaterialParams(
		FMaterial& Material,
		int32 Columns,
		int32 Rows,
		int32 CurrentFrame);

private:
	TMap<uint32, std::shared_ptr<FDynamicMaterial>> TextMaterialsByColor;
	TMap<const USubUVComponent*, std::shared_ptr<FDynamicMaterial>> SubUVMaterialsByComponent;
};

struct ENGINE_API FSceneCommandBuildContext
{
	FMaterial* DefaultMaterial = nullptr;
	ISceneTextFeature* TextFeature = nullptr;
	ISceneSubUVFeature* SubUVFeature = nullptr;
	ISceneBillboardFeature* BillboardFeature = nullptr;
	FSceneCommandResourceCache* ResourceCache = nullptr;
	float TotalTimeSeconds = 0.0f;
};

class ENGINE_API FSceneCommandBuilder
{
public:
	void BuildSceneViewData(
		const FSceneCommandBuildContext& BuildContext,
		const FSceneRenderPacket& Packet,
		const FFrameContext& Frame,
		const FViewContext& View,
		FSceneViewData& OutSceneViewData);

private:
	static void FinalizeBatchMaterial(const FSceneCommandBuildContext& BuildContext, FMeshBatch& Batch);
};
