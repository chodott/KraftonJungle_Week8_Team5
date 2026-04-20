#pragma once

#include "CoreMinimal.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Mesh/MeshBatch.h"

#include <memory>

class FRenderer;
class FMaterial;
class FSceneCommandBuilder;
class FSceneCommandResourceCache;
class FMeshPassProcessor;
class UWorld;
struct FMeshPassFrameStats;
struct FSceneRenderPacket;
struct FSceneViewData;
struct FSceneRenderTargets;

class ENGINE_API FSceneRenderer
{
public:
	FSceneRenderer();
	~FSceneRenderer();

	FSceneRenderer(const FSceneRenderer&)            = delete;
	FSceneRenderer& operator=(const FSceneRenderer&) = delete;

	void                       BeginFrame();
	size_t                     GetPrevCommandCount() const;
	const FMeshPassFrameStats& GetMeshPassFrameStats() const;

	void BuildSceneViewData(
		FRenderer&                Renderer,
		const FSceneRenderPacket& Packet,
		const FFrameContext&      Frame,
		const FViewContext&       View,
		UWorld*                   World,
		const TArray<FMeshBatch>& AdditionalMeshBatches,
		FSceneViewData&           OutSceneViewData);

	bool RenderSceneView(
		FRenderer&           Renderer,
		FSceneRenderTargets& Targets,
		FSceneViewData&      SceneViewData,
		const float          ClearColor[4],
		bool                 bForceWireframe,
		FMaterial*           WireframeMaterial);

private:
	std::unique_ptr<FSceneCommandBuilder>       SceneCommandBuilder;
	std::unique_ptr<FSceneCommandResourceCache> SceneCommandResourceCache;
	std::unique_ptr<FMeshPassProcessor>         MeshPassProcessor;
	size_t                                      PrevCommandCount             = 0;
	size_t                                      CurrentFramePeakCommandCount = 0;
};
