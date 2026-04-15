#include "Renderer/Scene/Passes/ScenePasses.h"

#include "Renderer/Renderer.h"
#include "Renderer/Scene/Passes/ScenePassExecutionUtils.h"

bool FEditorGridPass::Execute(FPassContext& Context)
{
	ID3D11RenderTargetView* OverlayRenderTarget = Context.Targets.OverlayColorRTV
		? Context.Targets.OverlayColorRTV
		: Context.Targets.SceneColorRTV;

	if (!OverlayRenderTarget)
	{
		return true;
	}

	BeginPass(
		Context.Renderer,
		OverlayRenderTarget,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	Processor.ExecutePass(Context.Renderer, Context.Targets, Context.SceneViewData, EMeshPassType::EditorGrid);
	EndPass(
		Context.Renderer,
		Context.Targets.SceneColorRTV,
		Context.Targets.SceneDepthDSV,
		Context.SceneViewData.View.Viewport,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View);
	return true;
}
