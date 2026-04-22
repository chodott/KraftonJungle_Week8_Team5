#include "SceneLightingPasses.h"

#include "Renderer/Renderer.h"
#include "Renderer/Features/Lighting/LightRenderFeature.h"

bool FLightCullingComputePass::Execute(FPassContext& Context)
{
	FLightRenderFeature* Feature = Context.Renderer.GetLightFeature();
	if (!Feature)
	{
		return true;
	}

	return Feature->PrepareClusteredLightResources(
		Context.Renderer,
		Context.SceneViewData,
		Context.Targets);
}
