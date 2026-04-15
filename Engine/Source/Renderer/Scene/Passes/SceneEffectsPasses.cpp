#include "Renderer/Scene/Passes/ScenePasses.h"

#include "Renderer/Features/Decal/DecalRenderFeature.h"
#include "Renderer/Features/Decal/VolumeDecalRenderFeature.h"
#include "Renderer/Features/FireBall/FireBallRenderFeature.h"
#include "Renderer/Features/Fog/FogRenderFeature.h"
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
