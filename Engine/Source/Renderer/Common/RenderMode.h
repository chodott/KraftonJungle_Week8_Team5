#pragma once

#include "CoreMinimal.h"

enum class ERenderMode : uint8
{
    Lighting = 0,
    NoLighting,
    Wireframe,
    SceneDepth,
};
