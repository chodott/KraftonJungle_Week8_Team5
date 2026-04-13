#include "Renderer/ScenePasses.h"

#include "Feature/FireballRenderFeature.h"
#include "Renderer/FullscreenPass.h"
#include "Renderer/Feature/DebugLineRenderFeature.h"
#include "Renderer/Feature/DecalRenderFeature.h"
#include "Renderer/Feature/FogRenderFeature.h"
#include "Renderer/Feature/OutlineRenderFeature.h"
#include "Renderer/Renderer.h"

namespace
{
	FDecalRenderRequest BuildDecalPassRequest(const FSceneViewData& SceneViewData)
	{
		FDecalRenderRequest Request;
		Request.Items = SceneViewData.PostProcessInputs.DecalItems;
		Request.View = SceneViewData.View.View;
		Request.Projection = SceneViewData.View.Projection;
		Request.ViewProjection = SceneViewData.View.ViewProjection;
		Request.InverseViewProjection = SceneViewData.View.InverseViewProjection;
		Request.CameraPosition = SceneViewData.View.CameraPosition;
		Request.ViewportWidth = static_cast<uint32>(SceneViewData.View.Viewport.Width);
		Request.ViewportHeight = static_cast<uint32>(SceneViewData.View.Viewport.Height);
		Request.NearZ = SceneViewData.View.NearZ;
		Request.FarZ = SceneViewData.View.FarZ;
		Request.ClusterCountX = 16;
		Request.ClusterCountY = 9;
		Request.ClusterCountZ = 24;
		Request.ReceiverLayerMask = 0xFFFFFFFFu;
		Request.BaseColorTextureArraySRV = SceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV;
		return Request;
	}
}

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
	DeviceContext->ClearDepthStencilView(Context.Targets.SceneDepthDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	if (Context.Targets.GBufferARTV) DeviceContext->ClearRenderTargetView(Context.Targets.GBufferARTV, ZeroColor);
	if (Context.Targets.GBufferBRTV) DeviceContext->ClearRenderTargetView(Context.Targets.GBufferBRTV, ZeroColor);
	if (Context.Targets.GBufferCRTV) DeviceContext->ClearRenderTargetView(Context.Targets.GBufferCRTV, ZeroColor);
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

bool FDecalCompositePass::Execute(FPassContext& Context)
{
	FDecalRenderFeature* DecalFeature = Context.Renderer.GetDecalFeature();
	if (!DecalFeature || Context.SceneViewData.PostProcessInputs.DecalItems.empty())
	{
		return true;
	}

	const FDecalRenderRequest Request = BuildDecalPassRequest(Context.SceneViewData);
	return DecalFeature->Render(
		Context.Renderer,
		Request,
		Context.Targets);
}

bool FForwardTransparentPass::Execute(FPassContext& Context)
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
	Processor.ExecutePass(Context.Renderer, Context.Targets, Context.SceneViewData, EMeshPassType::ForwardTransparent);
	EndPass(
		Context.Renderer,
		Context.Targets.SceneColorRTV,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	return true;
}

bool FFogPostPass::Execute(FPassContext& Context)
{
	FFogRenderFeature* FogFeature = Context.Renderer.GetFogFeature();
	if (!FogFeature || Context.SceneViewData.PostProcessInputs.FogItems.empty())
	{
		return true;
	}

	return FogFeature->Render(
		Context.Renderer,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View,
		Context.Targets,
		Context.SceneViewData.PostProcessInputs.FogItems);
}

bool FOutlineMaskPass::Execute(FPassContext& Context)
{
	FOutlineRenderFeature* OutlineFeature = Context.Renderer.GetOutlineFeature();
	if (!OutlineFeature
		|| !Context.SceneViewData.PostProcessInputs.bOutlineEnabled
		|| Context.SceneViewData.PostProcessInputs.OutlineItems.empty())
	{
		return true;
	}

	FOutlineRenderRequest Request;
	Request.bEnabled = Context.SceneViewData.PostProcessInputs.bOutlineEnabled;
	Request.Items = Context.SceneViewData.PostProcessInputs.OutlineItems;
	return OutlineFeature->RenderMaskPass(
		Context.Renderer,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View,
		Context.Targets,
		Request);
}

bool FOutlineCompositePass::Execute(FPassContext& Context)
{
	FOutlineRenderFeature* OutlineFeature = Context.Renderer.GetOutlineFeature();
	if (!OutlineFeature
		|| !Context.SceneViewData.PostProcessInputs.bOutlineEnabled
		|| Context.SceneViewData.PostProcessInputs.OutlineItems.empty())
	{
		return true;
	}

	FOutlineRenderRequest Request;
	Request.bEnabled = Context.SceneViewData.PostProcessInputs.bOutlineEnabled;
	Request.Items = Context.SceneViewData.PostProcessInputs.OutlineItems;
	return OutlineFeature->RenderCompositePass(
		Context.Renderer,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View,
		Context.Targets,
		Request);
}

bool FOverlayPass::Execute(FPassContext& Context)
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
	Processor.ExecutePass(Context.Renderer, Context.Targets, Context.SceneViewData, EMeshPassType::Overlay);
	EndPass(
		Context.Renderer,
		Context.Targets.SceneColorRTV,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	return true;
}

bool FDebugLinePass::Execute(FPassContext& Context)
{
	FDebugLineRenderFeature* DebugLineFeature = Context.Renderer.GetDebugLineFeature();
	if (!DebugLineFeature || Context.SceneViewData.DebugInputs.LinePass.IsEmpty())
	{
		return true;
	}

	return DebugLineFeature->Render(
		Context.Renderer,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View,
		Context.Targets,
		Context.SceneViewData.DebugInputs.LinePass);
}

bool FFireBallPass::Execute(FPassContext& Context)
{
	FFireBallRenderFeature* Feature = Context.Renderer.GetFireBallFeature();
	if (!Feature || Context.SceneViewData.PostProcessInputs.FireBallItems.empty())
		return true;
	return Feature->Render(
		Context.Renderer,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View,
		Context.Targets,
		Context.SceneViewData.PostProcessInputs.FireBallItems	
	);
}
