#include "Renderer/Scene/Pipeline/ScenePipelineBuilder.h"

#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/Passes/ScenePasses.h"
#include "Renderer/Scene/Pipeline/RenderPipeline.h"

#include <memory>

void BuildDefaultSceneRenderPipeline(FRenderPipeline& OutPipeline, const FMeshPassProcessor& MeshPassProcessor)
{
    OutPipeline.Reset();
    OutPipeline.AddPass(std::make_unique<FClearSceneTargetsPass>());
    OutPipeline.AddPass(std::make_unique<FUploadMeshBuffersPass>(MeshPassProcessor));
    OutPipeline.AddPass(std::make_unique<FDepthPrepass>(MeshPassProcessor));
    OutPipeline.AddPass(std::make_unique<FGBufferPass>(MeshPassProcessor));
    OutPipeline.AddPass(std::make_unique<FForwardOpaquePass>(MeshPassProcessor));
    OutPipeline.AddPass(std::make_unique<FDecalCompositePass>());
    OutPipeline.AddPass(std::make_unique<FForwardTransparentPass>(MeshPassProcessor));
    OutPipeline.AddPass(std::make_unique<FFogPostPass>());
    OutPipeline.AddPass(std::make_unique<FFireBallPass>());
    OutPipeline.AddPass(std::make_unique<FOutlineMaskPass>());
    OutPipeline.AddPass(std::make_unique<FOutlineCompositePass>());
    OutPipeline.AddPass(std::make_unique<FOverlayPass>(MeshPassProcessor));
    OutPipeline.AddPass(std::make_unique<FDebugLinePass>());
    OutPipeline.AddPass(std::make_unique<FFXAAPass>());
}
