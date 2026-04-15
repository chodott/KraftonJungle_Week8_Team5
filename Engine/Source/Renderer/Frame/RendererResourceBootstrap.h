#pragma once

#include "CoreMinimal.h"

class FRenderer;

class ENGINE_API FRendererResourceBootstrap
{
public:
	static bool Initialize(FRenderer& Renderer);
	static void Release(FRenderer& Renderer);
};
