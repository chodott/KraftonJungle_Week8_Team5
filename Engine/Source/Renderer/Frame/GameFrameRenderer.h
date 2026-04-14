#pragma once

#include "CoreMinimal.h"
#include "Renderer/Frame/FrameRequests.h"

class FRenderer;

class ENGINE_API FGameFrameRenderer
{
public:
    static bool Render(FRenderer& Renderer, const FGameFrameRequest& Request);
};
