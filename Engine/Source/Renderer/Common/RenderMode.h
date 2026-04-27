#pragma once

#include "CoreMinimal.h"

enum class ERenderMode : uint8
{
	Lit_Gouraud = 0,
	Lit_Lambert,
	Lit_Phong,
	Unlit,
    Wireframe,
	SceneDepth,
	WorldNormal,
    LightCullingHeatmap,
	CSMCascadeDebug,
};
