#pragma once

#include "CoreMinimal.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Mesh/MeshBatch.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/SceneViewData.h"

class FRenderer;

struct ENGINE_API FMeshPassFrameStats
{
	uint32 TotalDrawCalls = 0;
	double TotalTimeMs = 0.0;
};

class ENGINE_API FMeshPassProcessor
{
public:
	void BeginFrame();
	void UploadMeshBuffers(FRenderer& Renderer, const FSceneViewData& SceneViewData) const;
	void ExecutePass(
		FRenderer& Renderer,
		FSceneRenderTargets& Targets,
		const FSceneViewData& SceneViewData,
		EMeshPassType PassType) const;
	const FMeshPassFrameStats& GetFrameStats() const { return FrameStats; }

private:
	static EMaterialPassType ToMaterialPassType(EMeshPassType PassType);
	static bool ShouldDrawInPass(const FMeshBatch& Batch, EMeshPassType PassType);
	static uint64 MakeBatchSortKey(const FMeshBatch& Batch, EMeshPassType PassType);

private:
	mutable FMeshPassFrameStats FrameStats;
};
