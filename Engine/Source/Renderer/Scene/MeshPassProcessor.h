#pragma once

#include "CoreMinimal.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Mesh/MeshBatch.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/SceneViewData.h"

class FRenderer;

class ENGINE_API FMeshPassProcessor
{
public:
	void UploadMeshBuffers(FRenderer& Renderer, const FSceneViewData& SceneViewData) const;
	void ExecutePass(
		FRenderer& Renderer,
		FSceneRenderTargets& Targets,
		const FSceneViewData& SceneViewData,
		EMeshPassType PassType) const;

private:
	static EMaterialPassType ToMaterialPassType(EMeshPassType PassType);
	static bool ShouldDrawInPass(const FMeshBatch& Batch, EMeshPassType PassType);
	static uint64 MakeBatchSortKey(const FMeshBatch& Batch, EMeshPassType PassType);
};
