#pragma once

#include "CoreMinimal.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/UI/Screen/ScreenUIRenderer.h"

class FRenderer;

class ENGINE_API FScreenUIBatchRenderer
{
public:
    bool Render(
        FRenderer& Renderer,
        const FFrameContext& Frame,
        const FScreenUIPassInputs& PassInputs,
        ID3D11RenderTargetView* RenderTargetView,
        ID3D11DepthStencilView* DepthStencilView);

private:
    bool DrawBatchCommand(FRenderer& Renderer, const FUIBatchCommand& BatchCommand);
};
