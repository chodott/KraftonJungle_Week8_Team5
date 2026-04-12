#include "Renderer/SceneRenderer.h"

#include "Renderer/Material.h"
#include "Renderer/Renderer.h"

void FSceneRenderer::BeginFrame()
{
	PrevCommandCount = (std::max)(PrevCommandCount, CurrentFramePeakCommandCount);
	CurrentFramePeakCommandCount = 0;
}

size_t FSceneRenderer::GetPrevCommandCount() const
{
	return (std::max)(PrevCommandCount, CurrentFramePeakCommandCount);
}

void FSceneRenderer::BuildSceneViewData(
	FRenderer& Renderer,
	const FSceneRenderPacket& Packet,
	const FFrameContext& Frame,
	const FViewContext& View,
	const TArray<FMeshBatch>& AdditionalMeshBatches,
	FSceneViewData& OutSceneViewData)
{
	OutSceneViewData.MeshInputs.Batches.reserve(Packet.MeshPrimitives.size() + AdditionalMeshBatches.size());
	OutSceneViewData.PostProcessInputs.Clear();
	OutSceneViewData.DebugInputs.Clear();

	FSceneCommandBuildContext BuildContext;
	BuildContext.DefaultMaterial = Renderer.GetDefaultMaterial();
	BuildContext.TextFeature = Renderer.GetSceneTextFeature();
	BuildContext.SubUVFeature = Renderer.GetSceneSubUVFeature();
	BuildContext.BillboardFeature = Renderer.GetSceneBillboardFeature();
	BuildContext.ResourceCache = &SceneCommandResourceCache;
	BuildContext.TotalTimeSeconds = Frame.TotalTimeSeconds;

	SceneCommandBuilder.BuildSceneViewData(BuildContext, Packet, Frame, View, OutSceneViewData);
	AppendAdditionalMeshBatches(Renderer, AdditionalMeshBatches, OutSceneViewData);

	CurrentFramePeakCommandCount = (std::max)(CurrentFramePeakCommandCount, OutSceneViewData.MeshInputs.Batches.size());
}

bool FSceneRenderer::RenderSceneView(
	FRenderer& Renderer,
	FSceneRenderTargets& Targets,
	FSceneViewData& SceneViewData,
	const float ClearColor[4],
	bool bForceWireframe,
	FMaterial* WireframeMaterial)
{
	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
	if (!Context || !Targets.IsValid())
	{
		return false;
	}

	if (bForceWireframe)
	{
		ApplyWireframeOverride(SceneViewData, WireframeMaterial);
	}

	FPassContext PassContext
	{
		Renderer,
		Targets,
		SceneViewData,
		FVector4(ClearColor[0], ClearColor[1], ClearColor[2], ClearColor[3])
	};

	FRenderPipeline Pipeline;
	BuildRenderPipeline(Pipeline);
	return Pipeline.Execute(PassContext);
}

void FSceneRenderer::AppendAdditionalMeshBatches(
	FRenderer& Renderer,
	const TArray<FMeshBatch>& AdditionalMeshBatches,
	FSceneViewData& InOutSceneViewData)
{
	for (const FMeshBatch& SourceBatch : AdditionalMeshBatches)
	{
		if (!SourceBatch.Mesh)
		{
			continue;
		}

		FMeshBatch Batch = SourceBatch;
		Batch.Material = Batch.Material ? Batch.Material : Renderer.GetDefaultMaterial();
		Batch.SubmissionOrder = static_cast<uint64>(InOutSceneViewData.MeshInputs.Batches.size());
		InOutSceneViewData.MeshInputs.Batches.push_back(std::move(Batch));
	}
}

void FSceneRenderer::BuildRenderPipeline(FRenderPipeline& OutPipeline) const
{
	OutPipeline.Reset();
	OutPipeline.AddPass(std::make_unique<FClearSceneTargetsPass>());
	OutPipeline.AddPass(std::make_unique<FUploadMeshBuffersPass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FDepthPrepass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FGBufferPass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FForwardOpaquePass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FDecalCompositePass>());
	OutPipeline.AddPass(std::make_unique<FForwardTransparentPass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FFogPostPass>());
	OutPipeline.AddPass(std::make_unique<FOutlineMaskPass>());
	OutPipeline.AddPass(std::make_unique<FOutlineCompositePass>());
	OutPipeline.AddPass(std::make_unique<FOverlayPass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FDebugLinePass>());
}

void FSceneRenderer::ApplyWireframeOverride(FSceneViewData& SceneViewData, FMaterial* WireframeMaterial)
{
	if (!WireframeMaterial)
	{
		return;
	}

	for (FMeshBatch& Batch : SceneViewData.MeshInputs.Batches)
	{
		if (Batch.Domain == EMaterialDomain::Overlay)
		{
			continue;
		}

		Batch.Material = WireframeMaterial;
	}
}
