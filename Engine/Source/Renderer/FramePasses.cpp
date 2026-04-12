#include "Renderer/FramePasses.h"

#include "Renderer/FullscreenPass.h"
#include "Renderer/Renderer.h"

bool FViewportCompositePass::Execute(FFramePassContext& Context)
{
	if (!Context.CompositeInputs || Context.CompositeInputs->IsEmpty())
	{
		BeginPass(
			Context.Renderer,
			Context.RenderTargetView,
			Context.DepthStencilView,
			Context.View.Viewport,
			Context.Frame,
			Context.View);
		return true;
	}

	return Context.Renderer.ComposeViewports(
		*Context.CompositeInputs,
		Context.Frame,
		Context.View,
		Context.RenderTargetView,
		Context.DepthStencilView);
}

bool FScreenUIPass::Execute(FFramePassContext& Context)
{
	if (!Context.ScreenUIInputs)
	{
		return true;
	}

	return Context.Renderer.RenderScreenUIPass(
		*Context.ScreenUIInputs,
		Context.Frame,
		Context.RenderTargetView,
		Context.DepthStencilView);
}
