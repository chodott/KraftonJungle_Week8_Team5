#include "Renderer/Scene/Pipeline/ScenePipelineBuilder.h"

#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/Passes/ScenePasses.h"
#include "Renderer/Scene/Pipeline/RenderPipeline.h"

#include <memory>

void BuildDefaultSceneRenderPipeline(FRenderPipeline& OutPipeline, const FMeshPassProcessor& MeshPassProcessor)
{
	OutPipeline.Reset();

	// -----------------------------------------------------------------------
	// [16-bit HDR] R16G16B16A16_FLOAT — 선형 공간 (Linear)
	// SceneColorRead/Write, GBufferA/B/C 모두 float16 포맷으로 동작
	// -----------------------------------------------------------------------

	// Scene Geometry
	OutPipeline.AddPass(std::make_unique<FClearSceneTargetsPass>());
	OutPipeline.AddPass(std::make_unique<FUploadMeshBuffersPass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FDepthPrepass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FLightCullingComputePass>());
	// Inactive for the current forward-focused renderer. Keep the pass code and shaders, but do not execute it.
	// OutPipeline.AddPass(std::make_unique<FGBufferPass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FShadowMapPass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FForwardOpaquePass>(MeshPassProcessor));

	// Scene Effects
	OutPipeline.AddPass(std::make_unique<FMeshDecalPass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FDecalCompositePass>());
	OutPipeline.AddPass(std::make_unique<FFogPostPass>());
	OutPipeline.AddPass(std::make_unique<FFireBallPass>());
	OutPipeline.AddPass(std::make_unique<FForwardTransparentPass>(MeshPassProcessor));
	OutPipeline.AddPass(std::make_unique<FBloomPass>());

	// Editor World Overlay
	OutPipeline.AddPass(std::make_unique<FEditorGridPass>(MeshPassProcessor));

	// Selection Highlight
	OutPipeline.AddPass(std::make_unique<FOutlineMaskPass>());
	OutPipeline.AddPass(std::make_unique<FOutlineCompositePass>());

	// Editor Screen Overlay
	OutPipeline.AddPass(std::make_unique<FEditorLinePass>());
	OutPipeline.AddPass(std::make_unique<FEditorPrimitivePass>(MeshPassProcessor));

	// -----------------------------------------------------------------------
	// 이후 ResolveSceneColorTargets에서:
	//   ACES 톤매핑 + LinearToSRGB → SceneColorWrite (R16G16B16A16_FLOAT)
	//   FXAA (옵션, sRGB 공간에서 동작)
	//   최종 Blit → [8-bit LDR] R8G8B8A8_UNORM 백버퍼
	// -----------------------------------------------------------------------
}
