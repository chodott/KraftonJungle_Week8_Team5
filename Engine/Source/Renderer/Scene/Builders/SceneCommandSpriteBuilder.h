#pragma once

#include "CoreMinimal.h"

struct FSceneCommandBuildContext;
struct FSceneRenderPacket;
struct FViewContext;
struct FSceneViewData;

class ENGINE_API FSceneCommandSpriteBuilder
{
public:
	void BuildSubUVInputs(
		const FSceneCommandBuildContext& BuildContext,
		const FSceneRenderPacket& Packet,
		const FViewContext& View,
		FSceneViewData& OutSceneViewData) const;

	void BuildBillboardInputs(
		const FSceneCommandBuildContext& BuildContext,
		const FSceneRenderPacket& Packet,
		const FViewContext& View,
		FSceneViewData& OutSceneViewData) const;
};
