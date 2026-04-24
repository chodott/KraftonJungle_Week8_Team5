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

bool FShadowMapPass::Execute(FPassContext& Context)
{
	ID3D11DeviceContext* DeviceContext = Context.Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	if (Context.Targets.ShadowMapTexture)
	{
		Context.Renderer.GetRenderStateManager()->UnbindResourceEverywhere(Context.Targets.ShadowMapTexture);
	}

	if (Context.SceneViewData.LightingInputs.LocalLights.empty())
	{
		return true;
	}

	FViewContext OriginView = Context.SceneViewData.View;
	D3D11_VIEWPORT ShadowViewPort = { 0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f };
	
	for (const FLocalLightRenderItem& Light : Context.SceneViewData.LightingInputs.LocalLights)
	{
		if (Light.ShadowIndex == UINT32_MAX || Light.ShadowIndex >= Context.Targets.ShadowMapDSVs.size())
		{
			continue;
		}

		ID3D11DepthStencilView* CurrentDSV = Context.Targets.ShadowMapDSVs[Light.ShadowIndex];

		DeviceContext->ClearDepthStencilView(CurrentDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

		Context.SceneViewData.View.CameraPosition = Light.PositionWS;
		Context.SceneViewData.View.View = Light.ShadowView;
		Context.SceneViewData.View.Projection = Light.ShadowProj;
		Context.SceneViewData.View.InverseView = Light.ShadowView.GetInverse();
		Context.SceneViewData.View.InverseProjection = Light.ShadowProj.GetInverse();

		float SafeRange = std::max(Light.Range, 1.0f);
		Context.SceneViewData.View.NearZ = std::max(0.1f, SafeRange * 0.02f);
		Context.SceneViewData.View.FarZ = std::max(SafeRange, Context.SceneViewData.View.NearZ + 0.1f);
		Context.SceneViewData.View.Viewport = ShadowViewPort;

		BeginPass(
			Context.Renderer,
			0,
			nullptr,
			CurrentDSV,
			ShadowViewPort,
			Context.SceneViewData.Frame,
			Context.SceneViewData.View);

		Processor.ExecutePass(Context.Renderer, Context.Targets, Context.SceneViewData, EMeshPassType::ShadowDepthPrepass);
	}

	Context.SceneViewData.View = OriginView;

	EndPass(
		Context.Renderer,
		Context.Targets.SceneColorRTV,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);

	return true;
}