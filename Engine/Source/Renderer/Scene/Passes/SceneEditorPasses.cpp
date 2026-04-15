#include "Renderer/Scene/Passes/ScenePasses.h"

#include "Renderer/Features/FireBall/FireBallRenderFeature.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Features/Debug/DebugLineRenderFeature.h"
#include "Renderer/Features/Decal/DecalRenderFeature.h"
#include "Renderer/Features/Decal/VolumeDecalRenderFeature.h"
#include "Renderer/Features/Fog/FogRenderFeature.h"
#include "Renderer/Features/Outline/OutlineRenderFeature.h"
#include "Renderer/Features/PostProcess/FXAARenderFeature.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene/Passes/ScenePassExecutionUtils.h"


bool FEditorLinePass::Execute(FPassContext& Context)
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
