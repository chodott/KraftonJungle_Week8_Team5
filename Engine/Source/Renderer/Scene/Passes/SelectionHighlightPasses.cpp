#include "Renderer/Scene/Passes/ScenePasses.h"

#include "Renderer/Features/Outline/OutlineRenderFeature.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene/Passes/ScenePassExecutionUtils.h"

bool FOutlineMaskPass::Execute(FPassContext& Context)
{
	FOutlineRenderFeature* OutlineFeature = Context.Renderer.GetOutlineFeature();
	if (!OutlineFeature
		|| !Context.SceneViewData.PostProcessInputs.bOutlineEnabled
		|| Context.SceneViewData.PostProcessInputs.OutlineItems.empty())
	{
		return true;
	}

	const FOutlineRenderRequest Request = BuildOutlinePassRequest(Context.SceneViewData);
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

	const FOutlineRenderRequest Request = BuildOutlinePassRequest(Context.SceneViewData);
	return OutlineFeature->RenderCompositePass(
		Context.Renderer,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View,
		Context.Targets,
		Request);
}
