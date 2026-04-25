#pragma once
#include "CoreMinimal.h"

struct ENGINE_API FShadowStats
{

	uint32 TotalShadowCastingLights = 0;
	uint32 ActiveSpotShadows = 0;
	
	uint32 ActivePointShadowFaces = 0;
	uint32 ActiveCSMCascades = 0;

	uint64 TotalShadowMapMemoryBytes = 0;
	uint32 AtlasAllocatedNodes = 0;
	uint32 AtlasFailedAllocations = 0;

	double ShadowSetupTimeMs = 0.0;
	double DepthPassRenderTimeMs = 0.0;
	double ShadowFilterTimeMs = 0.0;
};
