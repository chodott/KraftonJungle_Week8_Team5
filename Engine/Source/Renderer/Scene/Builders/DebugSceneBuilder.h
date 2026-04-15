#pragma once

#include "CoreMinimal.h"
#include "Renderer/Frame/FrameRequests.h"
#include "Renderer/Features/Debug/DebugTypes.h"

struct FDebugPrimitiveList;

ENGINE_API void BuildEditorLinePassInputs(const FDebugPrimitiveList& Primitives, FEditorLinePassInputs& OutPassInputs);
ENGINE_API void BuildEditorLinePassInputs(const FDebugSceneBuildInputs& Inputs, FEditorLinePassInputs& OutPassInputs);
