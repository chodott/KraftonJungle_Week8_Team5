#pragma once

#include "EngineAPI.h"

class FMeshPassProcessor;
class FRenderPipeline;

ENGINE_API void BuildDefaultSceneRenderPipeline(FRenderPipeline& OutPipeline, const FMeshPassProcessor& MeshPassProcessor);
