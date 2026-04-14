#pragma once

#include "CoreMinimal.h"
#include "Level/SceneRenderPacket.h"
#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/RenderPipeline.h"
#include "Renderer/Scene/SceneCommandBuilder.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/ScenePasses.h"
#include "Renderer/Scene/SceneViewData.h"

class FRenderer;
class FMaterial;

class ENGINE_API FSceneRenderer
{
public:
	void BeginFrame();
	size_t GetPrevCommandCount() const;

	void BuildSceneViewData(
		FRenderer& Renderer,
		const FSceneRenderPacket& Packet,
		const FFrameContext& Frame,
		const FViewContext& View,
		const TArray<FMeshBatch>& AdditionalMeshBatches,
		FSceneViewData& OutSceneViewData);

	bool RenderSceneView(
		FRenderer& Renderer,
		FSceneRenderTargets& Targets,
		FSceneViewData& SceneViewData,
		const float ClearColor[4],
		bool bForceWireframe,
		FMaterial* WireframeMaterial);

private:
	void AppendAdditionalMeshBatches(FRenderer& Renderer, const TArray<FMeshBatch>& AdditionalMeshBatches, FSceneViewData& InOutSceneViewData);
	void BuildRenderPipeline(FRenderPipeline& OutPipeline) const;

	static void ApplyWireframeOverride(FSceneViewData& SceneViewData, FMaterial* WireframeMaterial);

private:
	FSceneCommandBuilder SceneCommandBuilder;
	FSceneCommandResourceCache SceneCommandResourceCache;
	FMeshPassProcessor MeshPassProcessor;
	size_t PrevCommandCount = 0;
	size_t CurrentFramePeakCommandCount = 0;
};
