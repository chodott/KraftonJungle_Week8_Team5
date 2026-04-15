#include "Renderer/Scene/Passes/ScenePasses.h"

#include "Renderer/Features/PostProcess/FXAARenderFeature.h"
#include "Renderer/Renderer.h"

bool FFXAAPass::Execute(FPassContext& Context)
{
	if (!Context.SceneViewData.PostProcessInputs.bApplyFXAA)
	{
		return true;
	}

	FFXAARenderFeature* Feature = Context.Renderer.GetFXAAFeature();
	if (!Feature)
	{
		return true;
	}

	return Feature->Render(
		Context.Renderer,
		Context.SceneViewData.Frame,
		Context.SceneViewData.View,
		Context.Targets);
}
