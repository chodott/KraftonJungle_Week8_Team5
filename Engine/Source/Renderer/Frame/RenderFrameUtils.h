#pragma once

#include "CoreMinimal.h"
#include "Renderer/Frame/FrameRequests.h"
#include "Renderer/Common/RenderFrameContext.h"

ENGINE_API FFrameContext BuildRenderFrameContext(float TotalTimeSeconds);
ENGINE_API FViewContext BuildRenderViewContext(const FSceneViewRenderRequest& SceneView, const D3D11_VIEWPORT& Viewport);
