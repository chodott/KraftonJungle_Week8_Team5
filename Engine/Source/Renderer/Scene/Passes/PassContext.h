#pragma once

#include "CoreMinimal.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/SceneViewData.h"

class FRenderer;

struct ENGINE_API FPassContext
{
	FRenderer& Renderer;
	FSceneRenderTargets& Targets;
	FSceneViewData& SceneViewData;
	FVector4 ClearColor = FVector4(0.1f, 0.1f, 0.1f, 1.0f);
};

class ENGINE_API IRenderPass
{
public:
	virtual ~IRenderPass() = default;
	virtual bool Execute(FPassContext& Context) = 0;
};
