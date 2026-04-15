#pragma once

#include "CoreMinimal.h"

struct FSceneCommandBuildContext;
struct FSceneRenderPacket;
struct FSceneViewData;

class ENGINE_API FSceneCommandMeshBuilder
{
public:
	void BuildMeshInputs(
		const FSceneCommandBuildContext& BuildContext,
		const FSceneRenderPacket& Packet,
		FSceneViewData& OutSceneViewData) const;
};
