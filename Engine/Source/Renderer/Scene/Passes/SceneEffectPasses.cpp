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


bool FDecalCompositePass::Execute(FPassContext& Context)
{
	if (Context.SceneViewData.PostProcessInputs.DecalItems.empty())
	{
		return true;
	}

	const FDecalRenderRequest Request = BuildDecalPassRequest(Context.SceneViewData);
	if (Context.Renderer.GetDecalProjectionMode() == EDecalProjectionMode::VolumeDraw)
	{
		FVolumeDecalRenderFeature* VolumeDecalFeature = Context.Renderer.GetVolumeDecalFeature();
		return VolumeDecalFeature
			? VolumeDecalFeature->Render(Context.Renderer, Request, Context.Targets)
			: true;
	}

	FDecalRenderFeature* DecalFeature = Context.Renderer.GetDecalFeature();
	return DecalFeature
		? DecalFeature->Render(Context.Renderer, Request, Context.Targets)
		: true;
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
