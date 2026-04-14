#pragma once

#include "EngineAPI.h"
#include "Renderer/Common/RenderFrameContext.h"

#include <d3d11.h>

class FRenderer;
struct FScreenUIPassInputs;
struct FViewportCompositePassInputs;

struct ENGINE_API FFramePassContext
{
	FRenderer& Renderer;
	FFrameContext Frame;
	FViewContext View;
	ID3D11RenderTargetView* RenderTargetView = nullptr;
	ID3D11DepthStencilView* DepthStencilView = nullptr;
	const FViewportCompositePassInputs* CompositeInputs = nullptr;
	const FScreenUIPassInputs* ScreenUIInputs = nullptr;
};

class ENGINE_API IFrameRenderPass
{
public:
	virtual ~IFrameRenderPass() = default;
	virtual bool Execute(FFramePassContext& Context) = 0;
};
