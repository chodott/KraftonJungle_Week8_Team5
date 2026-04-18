#include "Renderer/Scene/Passes/ScenePasses.h"

#include "Renderer/Renderer.h"
#include "Renderer/Scene/Passes/ScenePassExecutionUtils.h"

bool FClearSceneTargetsPass::Execute(FPassContext& Context)
{
	ID3D11DeviceContext* DeviceContext = Context.Renderer.GetDeviceContext();
	if (!DeviceContext || !Context.Targets.IsValid())
	{
		return false;
	}

	const float ClearColor[4] =
	{
		Context.ClearColor.X,
		Context.ClearColor.Y,
		Context.ClearColor.Z,
		Context.ClearColor.W
	};
	constexpr float ZeroColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	DeviceContext->ClearRenderTargetView(Context.Targets.SceneColorRTV, ClearColor);
	if (Context.Targets.SceneColorScratchRTV) DeviceContext->ClearRenderTargetView(Context.Targets.SceneColorScratchRTV, ClearColor);
	DeviceContext->ClearDepthStencilView(Context.Targets.SceneDepthDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	if (Context.Targets.GBufferARTV) DeviceContext->ClearRenderTargetView(Context.Targets.GBufferARTV, ZeroColor);
	if (Context.Targets.GBufferBRTV) DeviceContext->ClearRenderTargetView(Context.Targets.GBufferBRTV, ZeroColor);
	if (Context.Targets.GBufferCRTV) DeviceContext->ClearRenderTargetView(Context.Targets.GBufferCRTV, ZeroColor);
	if (Context.Targets.OverlayColorRTV) DeviceContext->ClearRenderTargetView(Context.Targets.OverlayColorRTV, ZeroColor);
	if (Context.Targets.OutlineMaskRTV) DeviceContext->ClearRenderTargetView(Context.Targets.OutlineMaskRTV, ZeroColor);

	BeginPass(
		Context.Renderer,
		Context.Targets.SceneColorRTV,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	return true;
}

bool FUploadMeshBuffersPass::Execute(FPassContext& Context)
{
	Processor.UploadMeshBuffers(Context.Renderer, Context.SceneViewData);
	return true;
}

bool FDepthPrepass::Execute(FPassContext& Context)
{
	ID3D11DeviceContext* DeviceContext = Context.Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	BeginPass(
		Context.Renderer,
		0,
		nullptr,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	Processor.ExecutePass(Context.Renderer, Context.Targets, Context.SceneViewData, EMeshPassType::DepthPrepass);
	EndPass(
		Context.Renderer,
		Context.Targets.SceneColorRTV,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	return true;
}

bool FGBufferPass::Execute(FPassContext& Context)
{
	if (!Context.Targets.GBufferARTV || !Context.Targets.GBufferBRTV || !Context.Targets.GBufferCRTV)
	{
		return true;
	}

	ID3D11DeviceContext* DeviceContext = Context.Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	ID3D11RenderTargetView* MRTs[3] =
	{
		Context.Targets.GBufferARTV,
		Context.Targets.GBufferBRTV,
		Context.Targets.GBufferCRTV,
	};

	BeginPass(
		Context.Renderer,
		3,
		MRTs,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	Processor.ExecutePass(Context.Renderer, Context.Targets, Context.SceneViewData, EMeshPassType::GBuffer);
	EndPass(
		Context.Renderer,
		Context.Targets.SceneColorRTV,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	return true;
}

bool FForwardOpaquePass::Execute(FPassContext& Context)
{
	ID3D11DeviceContext* DeviceContext = Context.Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	BeginPass(
		Context.Renderer,
		Context.Targets.SceneColorRTV,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	Processor.ExecutePass(Context.Renderer, Context.Targets, Context.SceneViewData, EMeshPassType::ForwardOpaque);
	EndPass(
		Context.Renderer,
		Context.Targets.SceneColorRTV,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	return true;
}

bool FForwardTransparentPass::Execute(FPassContext& Context)
{
	return ExecuteMeshScenePass(
		Context.Renderer,
		Context.Targets,
		Context.SceneViewData,
		Processor,
		EMeshPassType::ForwardTransparent);
}
