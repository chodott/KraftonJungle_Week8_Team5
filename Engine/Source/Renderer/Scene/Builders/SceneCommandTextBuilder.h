#pragma once

#include "CoreMinimal.h"

struct FSceneCommandBuildContext;
struct FSceneRenderPacket;
struct FViewContext;
struct FSceneViewData;

class ENGINE_API FSceneCommandTextBuilder
{
public:
	void BuildTextInputs(
		const FSceneCommandBuildContext& BuildContext,
		const FSceneRenderPacket& Packet,
		const FViewContext& View,
		FSceneViewData& OutSceneViewData) const;
};
