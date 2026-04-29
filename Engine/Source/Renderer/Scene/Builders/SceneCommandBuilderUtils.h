#pragma once

#include "Renderer/Scene/Builders/SceneCommandBuilder.h"

namespace SceneCommandBuilderUtils
{
	inline void FinalizeBatchMaterial(const FSceneCommandBuildContext& BuildContext, FMeshBatch& Batch)
	{
		if (!Batch.Material)
		{
			Batch.Material = BuildContext.DefaultMaterial;
		}
	}

	inline bool AddBatch(const FSceneCommandBuildContext& BuildContext, TArray<FMeshBatch>& OutBatches, FMeshBatch&& Batch)
	{
		FinalizeBatchMaterial(BuildContext, Batch);
		if (!Batch.Mesh || !Batch.Material)
		{
			return false;
		}

		Batch.SubmissionOrder = static_cast<uint64>(OutBatches.size());
		OutBatches.push_back(std::move(Batch));
		return true;
	}

	inline bool AddBatch(const FSceneCommandBuildContext& BuildContext, FSceneViewData& OutSceneViewData, FMeshBatch&& Batch)
	{
		return AddBatch(BuildContext, OutSceneViewData.MeshInputs.Batches, std::move(Batch));
	}
}
