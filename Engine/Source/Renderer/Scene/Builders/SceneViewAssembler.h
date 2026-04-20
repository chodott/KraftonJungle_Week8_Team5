#pragma once

#include "CoreMinimal.h"
#include "Level/SceneRenderPacket.h"
#include "Renderer/Mesh/MeshBatch.h"
#include "Renderer/Scene/SceneViewData.h"

class FRenderer;
class FMaterial;
class FSceneCommandBuilder;
class FSceneCommandResourceCache;
class UWorld;
struct FSceneCommandBuildContext;

ENGINE_API void BuildSceneViewDataFromPacket(
	FRenderer&                  Renderer,
	FSceneCommandBuilder&       CommandBuilder,
	FSceneCommandResourceCache& ResourceCache,
	const FSceneRenderPacket&   Packet,
	const FFrameContext&        Frame,
	const FViewContext&         View,
	UWorld*                     World,
	const TArray<FMeshBatch>&   AdditionalMeshBatches,
	FSceneViewData&             OutSceneViewData);

ENGINE_API void ApplyWireframeOverrideToSceneView(FSceneViewData& SceneViewData, FMaterial* WireframeMaterial);
