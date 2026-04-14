#pragma once

#include "CoreMinimal.h"
#include "Renderer/Frame/FrameRequests.h"
#include "Renderer/Features/Debug/DebugTypes.h"

struct FDebugPrimitiveList;

ENGINE_API void BuildDebugLinePassInputs(const FDebugPrimitiveList& Primitives, FDebugLinePassInputs& OutPassInputs);
ENGINE_API void BuildDebugLinePassInputs(const FDebugSceneBuildInputs& Inputs, FDebugLinePassInputs& OutPassInputs);
