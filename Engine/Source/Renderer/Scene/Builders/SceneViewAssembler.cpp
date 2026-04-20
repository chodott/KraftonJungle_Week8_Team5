#include "Renderer/Scene/Builders/SceneViewAssembler.h"

#include "Renderer/Renderer.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Scene/Builders/SceneCommandBuilder.h"

namespace
{
	void AppendAdditionalMeshBatches(
		FRenderer&                Renderer,
		const TArray<FMeshBatch>& AdditionalMeshBatches,
		FSceneViewData&           InOutSceneViewData)
	{
		for (const FMeshBatch& SourceBatch : AdditionalMeshBatches)
		{
			if (!SourceBatch.Mesh)
			{
				continue;
			}

			FMeshBatch Batch      = SourceBatch;
			Batch.Material        = Batch.Material ? Batch.Material : Renderer.GetDefaultMaterial();
			Batch.SubmissionOrder = InOutSceneViewData.MeshInputs.Batches.size();
			InOutSceneViewData.MeshInputs.Batches.push_back(std::move(Batch));
		}
	}
}

void BuildSceneViewDataFromPacket(
	FRenderer&                  Renderer,
	FSceneCommandBuilder&       CommandBuilder,
	FSceneCommandResourceCache& ResourceCache,
	const FSceneRenderPacket&   Packet,
	const FFrameContext&        Frame,
	const FViewContext&         View,
	UWorld*                     World,
	const TArray<FMeshBatch>&   AdditionalMeshBatches,
	FSceneViewData&             OutSceneViewData)
{
	OutSceneViewData.MeshInputs.Batches.reserve(Packet.MeshPrimitives.size() + AdditionalMeshBatches.size());
	OutSceneViewData.PostProcessInputs.Clear();
	OutSceneViewData.DebugInputs.Clear();

	FSceneCommandBuildContext BuildContext;
	BuildContext.DefaultMaterial  = Renderer.GetDefaultMaterial();
	BuildContext.TextFeature      = Renderer.GetSceneTextFeature();
	BuildContext.SubUVFeature     = Renderer.GetSceneSubUVFeature();
	BuildContext.BillboardFeature = Renderer.GetSceneBillboardFeature();
	BuildContext.ResourceCache    = &ResourceCache;
	BuildContext.TotalTimeSeconds = Frame.TotalTimeSeconds;
	BuildContext.World            = World;

	CommandBuilder.BuildSceneViewData(BuildContext, Packet, Frame, View, OutSceneViewData);
	AppendAdditionalMeshBatches(Renderer, AdditionalMeshBatches, OutSceneViewData);
	OutSceneViewData.DebugInputs.World = World;
}

void ApplyWireframeOverrideToSceneView(FSceneViewData& SceneViewData, FMaterial* WireframeMaterial)
{
	if (!WireframeMaterial)
	{
		return;
	}

	for (FMeshBatch& Batch : SceneViewData.MeshInputs.Batches)
	{
		if (Batch.Domain == EMaterialDomain::EditorPrimitive || Batch.Domain == EMaterialDomain::EditorGrid)
		{
			continue;
		}

		Batch.Material = WireframeMaterial;
	}
}
