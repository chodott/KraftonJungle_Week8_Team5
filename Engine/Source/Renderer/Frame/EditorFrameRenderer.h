#pragma once

#include "CoreMinimal.h"
#include "Renderer/Frame/FrameRequests.h"

class FRenderer;

class ENGINE_API FEditorFrameRenderer
{
public:
    static bool Render(FRenderer& Renderer, const FEditorFrameRequest& Request);
};
