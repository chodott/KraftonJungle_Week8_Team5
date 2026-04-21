#pragma once

#include "CoreMinimal.h"

struct FSceneRenderPacket;
struct FSceneViewData;

class ENGINE_API FSceneCommandPostProcessBuilder
{
public:
	void BuildFogInputs(
		const FSceneRenderPacket& Packet,
		FSceneViewData& OutSceneViewData) const;

	void BuildFireBallInputs(
		const FSceneRenderPacket& Packet,
		FSceneViewData& OutSceneViewData) const;

	void BuildDecalInputs(
		const FSceneRenderPacket& Packet,
		FSceneViewData& OutSceneViewData) const;

	void BuildMeshDecalInputs(
		const struct FSceneCommandBuildContext& BuildContext,
		const FSceneRenderPacket& Packet,
		FSceneViewData& OutSceneViewData) const;
};
