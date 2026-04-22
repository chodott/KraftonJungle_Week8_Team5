#pragma once

#include "CoreMinimal.h"
#include "Level/ScenePacketBuilder.h"

struct FViewContext;
struct FSceneViewData;
struct FSceneCommandBuildContext;

class ENGINE_API FSceneCommandLightingBuilder
{
public:
	void BuildLightingInputs(
		const FSceneCommandBuildContext& BuildContext,
		const FSceneRenderPacket&        Packet,
		const FViewContext&              View,
		FSceneViewData&                  OutSceneViewData) const;
};
