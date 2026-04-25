#include "Renderer/Frame/EditorFrameRenderer.h"

#include "Renderer/Frame/RenderFrameUtils.h"
#include "Renderer/Renderer.h"
#include "Renderer/UI/Screen/ScreenUIRenderer.h"
#include "Renderer/Scene/SceneViewData.h"
#include "Renderer/Scene/SceneRenderer.h"
#include "Renderer/Features/Decal/DecalTextureCache.h"
#include "Renderer/Frame/SceneTargetManager.h"
#include "Renderer/Scene/Builders/DebugSceneBuilder.h"
#include "Renderer/Frame/UI/FramePasses.h"
#include "Renderer/Frame/UI/FramePipeline.h"
#include "Renderer/Features/Shadow/ShadowRenderFeature.h"

bool FEditorFrameRenderer::Render(FRenderer& Renderer, const FEditorFrameRequest& Request)
{
	TArray<FViewportCompositeItem> CompositeItems = Request.CompositeItems;

	struct FOverlaySourceBinding
	{
		ID3D11ShaderResourceView* SceneColorSRV   = nullptr;
		ID3D11ShaderResourceView* SceneDepthSRV   = nullptr;
		ID3D11ShaderResourceView* OverlayColorSRV = nullptr;
	};

	TArray<FOverlaySourceBinding> OverlayBindings;
	OverlayBindings.reserve(Request.ScenePasses.size());

	for (const FViewportScenePassRequest& ScenePass : Request.ScenePasses)
	{
		FSceneRenderTargets Targets;
		if (!Renderer.SceneTargetManager->WrapExternalSceneTargets(
			Renderer.GetDevice(),
			ScenePass.RenderTargetView,
			ScenePass.RenderTargetShaderResourceView,
			ScenePass.DepthStencilView,
			ScenePass.DepthShaderResourceView,
			ScenePass.Viewport,
			Targets))
		{
			continue;
		}

		const FFrameContext Frame = BuildRenderFrameContext(ScenePass.SceneView.TotalTimeSeconds);
		const FViewContext  View  = BuildRenderViewContext(ScenePass.SceneView, ScenePass.Viewport);

		FSceneViewData SceneViewData;
		SceneViewData.RenderMode = ScenePass.RenderMode;
		Renderer.GetSceneRenderer().BuildSceneViewData(
			Renderer,
			ScenePass.ScenePacket,
			Frame,
			View,
			ScenePass.DebugInputs.World,
			ScenePass.AdditionalMeshBatches,
			SceneViewData);
		Renderer.DecalTextureCache->ResolveTextureArray(Renderer.GetDevice(), SceneViewData);
		SceneViewData.ShowFlags                         = ScenePass.DebugInputs.ShowFlags;
		SceneViewData.bForceWireframe                   = ScenePass.bForceWireframe;
		SceneViewData.PostProcessInputs.OutlineItems    = ScenePass.OutlineRequest.Items;
		SceneViewData.PostProcessInputs.bOutlineEnabled = ScenePass.OutlineRequest.bEnabled;
		BuildEditorLinePassInputs(ScenePass.DebugInputs, SceneViewData.DebugInputs.LinePass);
		SceneViewData.DebugInputs.World = ScenePass.DebugInputs.World;

		if (!Renderer.GetSceneRenderer().RenderSceneView(
			Renderer,
			Targets,
			SceneViewData,
			ScenePass.ClearColor,
			ScenePass.bForceWireframe,
			ScenePass.WireframeMaterial))
		{
			continue;
		}

		bool bDisableFinalOverlayForThisScenePass = false;

		if (FShadowRenderFeature* ShadowFeature = Renderer.GetShadowFeature())
		{
			ID3D11ShaderResourceView* ShadowDebugSRV = ShadowFeature->GetShadowDebugPreviewSRV();

			if (ShadowDebugSRV &&
				ShadowFeature->GetDebugViewMode() != EShadowDebugViewMode::None &&
				ShadowFeature->IsDebugViewportOverlayEnabled())
			{
				FViewportCompositeItem ShadowDebugItem;
				ShadowDebugItem.Mode            = EViewportCompositeMode::SceneColor;
				ShadowDebugItem.SceneColorSRV   = ShadowDebugSRV;
				ShadowDebugItem.SceneDepthSRV   = nullptr;
				ShadowDebugItem.OverlayColorSRV = nullptr;
				ShadowDebugItem.Rect.X          = 0;
				ShadowDebugItem.Rect.Y          = 0;
				ShadowDebugItem.Rect.Width      = static_cast<int32>(ScenePass.Viewport.Width);
				ShadowDebugItem.Rect.Height     = static_cast<int32>(ScenePass.Viewport.Height);
				ShadowDebugItem.bVisible        = true;

				TArray<FViewportCompositeItem> ShadowDebugItems;
				ShadowDebugItems.push_back(ShadowDebugItem);

				const FViewportCompositePassInputs ShadowDebugCompositeInputs { &ShadowDebugItems };

				FViewContext ShadowDebugView      = View;
				ShadowDebugView.Viewport.TopLeftX = 0.0f;
				ShadowDebugView.Viewport.TopLeftY = 0.0f;
				ShadowDebugView.Viewport.Width    = ScenePass.Viewport.Width;
				ShadowDebugView.Viewport.Height   = ScenePass.Viewport.Height;
				ShadowDebugView.Viewport.MinDepth = 0.0f;
				ShadowDebugView.Viewport.MaxDepth = 1.0f;

				const bool bCompositedShadowDebug = Renderer.ComposeViewports(
					ShadowDebugCompositeInputs,
					Frame,
					ShadowDebugView,
					Targets.FinalSceneColor ? Targets.FinalSceneColor->RTV : Targets.SceneColorRTV,
					nullptr);

				if (bCompositedShadowDebug)
				{
					bDisableFinalOverlayForThisScenePass = true;
				}
			}
		}

		OverlayBindings.push_back({
			ScenePass.RenderTargetShaderResourceView,
			ScenePass.DepthShaderResourceView,
			bDisableFinalOverlayForThisScenePass ? nullptr : Targets.OverlayColorSRV
		});
	}

	for (FViewportCompositeItem& CompositeItem : CompositeItems)
	{
		for (const FOverlaySourceBinding& Binding : OverlayBindings)
		{
			if (CompositeItem.SceneColorSRV == Binding.SceneColorSRV
				&& CompositeItem.SceneDepthSRV == Binding.SceneDepthSRV)
			{
				CompositeItem.OverlayColorSRV = Binding.OverlayColorSRV;
				break;
			}
		}
	}

	const float FrameTimeSeconds = !Request.ScenePasses.empty()
		                               ? Request.ScenePasses.front().SceneView.TotalTimeSeconds
		                               : 0.0f;
	const FFrameContext Frame = BuildRenderFrameContext(FrameTimeSeconds);

	FViewContext FramePassView;
	FramePassView.Viewport = Renderer.GetRenderDevice().GetViewport();
	const FViewportCompositePassInputs CompositeInputs { &CompositeItems };

	FScreenUIPassInputs ScreenUIInputs;
	if (!Renderer.GetScreenUIRenderer().BuildPassInputs(
		Renderer,
		Request.ScreenDrawList,
		Renderer.GetRenderDevice().GetViewport(),
		ScreenUIInputs))
	{
		return false;
	}

	FFramePassContext FramePassContext {
		Renderer,
		Frame,
		FramePassView,
		Renderer.GetRenderTargetView(),
		nullptr,
		&CompositeInputs,
		&ScreenUIInputs
	};

	FFrameRenderPipeline FramePipeline;
	FramePipeline.AddPass(std::make_unique<FViewportCompositePass>());
	FramePipeline.AddPass(std::make_unique<FScreenUIPass>());
	return FramePipeline.Execute(FramePassContext);
}
