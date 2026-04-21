#include "Renderer/Scene/Pipeline/RenderPipeline.h"

#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Renderer.h"

void RestoreScenePassDefaults(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	Renderer.SetConstantBuffers();
	Renderer.UpdateFrameConstantBuffer(Frame, View);
	if (Renderer.GetRenderStateManager())
	{
		Renderer.GetRenderStateManager()->RebindState();
	}
}

void BeginPass(
	FRenderer& Renderer,
	uint32 NumRenderTargets,
	ID3D11RenderTargetView* const* RenderTargetViews,
	ID3D11DepthStencilView* DepthStencilView,
	const D3D11_VIEWPORT& Viewport,
	const FFrameContext& Frame,
	const FViewContext& View)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	if (Renderer.GetRenderStateManager())
	{
		Renderer.GetRenderStateManager()->SetRenderTargets(NumRenderTargets, RenderTargetViews, DepthStencilView);
	}
	else
	{
		DeviceContext->OMSetRenderTargets(NumRenderTargets, RenderTargetViews, DepthStencilView);
	}

	DeviceContext->RSSetViewports(1, &Viewport);
	RestoreScenePassDefaults(Renderer, Frame, View);
}

void EndPass(
	FRenderer& Renderer,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView,
	const D3D11_VIEWPORT& Viewport,
	const FFrameContext& Frame,
	const FViewContext& View)
{
	BeginPass(Renderer, RenderTargetView, DepthStencilView, Viewport, Frame, View);
}

void BeginFullscreenPass(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView,
	const D3D11_VIEWPORT& Viewport,
	const FFullscreenPassShaderSet& Shaders,
	const FFullscreenPassPipelineState& PipelineState)
{
	RestoreScenePassDefaults(Renderer, Frame, View);
	::BeginFullscreenPass(
		Renderer.GetDeviceContext(),
		RenderTargetView,
		DepthStencilView,
		Viewport,
		Shaders,
		PipelineState);
}

void EndFullscreenPass(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView,
	const D3D11_VIEWPORT& Viewport)
{
	::EndFullscreenPass(Renderer.GetDeviceContext());
	EndPass(Renderer, RenderTargetView, DepthStencilView, Viewport, Frame, View);
}

ID3D11DeviceContext* GetFullscreenPassDeviceContext(FRenderer& Renderer)
{
	return Renderer.GetDeviceContext();
}

void FRenderPipeline::Reset()
{
	PassSequence.Reset();
}

void FRenderPipeline::AddPass(std::unique_ptr<IRenderPass> Pass)
{
	PassSequence.AddPass(std::move(Pass));
}

bool FRenderPipeline::Execute(FPassContext& Context) const
{
	for (const std::unique_ptr<IRenderPass>& Pass : PassSequence.GetPasses())
	{
		if (!Pass)
		{
			return false;
		}

		Context.Renderer.PreparePassDomain(Pass->GetDomain(), Context.Targets);
		if (!Pass->Execute(Context))
		{
			return false;
		}
	}

	return true;
}
