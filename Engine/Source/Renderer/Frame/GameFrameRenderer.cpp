#include "Renderer/Frame/GameFrameRenderer.h"

#include "Renderer/Frame/RenderFrameUtils.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene/SceneViewData.h"
#include "Renderer/Scene/SceneRenderer.h"
#include "Renderer/Features/Decal/DecalTextureCache.h"
#include "Renderer/Frame/SceneTargetManager.h"
#include "Renderer/Scene/Builders/DebugSceneBuilder.h"
#include "Renderer/Frame/UI/FramePasses.h"
#include "Renderer/Frame/UI/FramePipeline.h"

bool FGameFrameRenderer::Render(FRenderer& Renderer, const FGameFrameRequest& Request)
{
    FSceneRenderTargets Targets;
    if (!Renderer.SceneTargetManager->AcquireGameSceneTargets(
        Renderer.GetDevice(),
        Renderer.GetRenderDevice().GetViewport(),
        Targets))
    {
        return false;
    }

    const FFrameContext Frame = BuildRenderFrameContext(Request.SceneView.TotalTimeSeconds);
    const FViewContext View = BuildRenderViewContext(Request.SceneView, Renderer.GetRenderDevice().GetViewport());

    FSceneViewData SceneViewData;
    Renderer.GetSceneRenderer().BuildSceneViewData(
        Renderer,
        Request.ScenePacket,
        Frame,
        View,
        Request.AdditionalMeshBatches,
        SceneViewData);
    Renderer.DecalTextureCache->ResolveTextureArray(Renderer.GetDevice(), SceneViewData);
    SceneViewData.ShowFlags = Request.DebugInputs.ShowFlags;
    SceneViewData.bForceWireframe = Request.bForceWireframe;
    BuildDebugLinePassInputs(Request.DebugInputs, SceneViewData.DebugInputs.LinePass);

    if (!Renderer.GetSceneRenderer().RenderSceneView(
        Renderer,
        Targets,
        SceneViewData,
        Request.ClearColor,
        Request.bForceWireframe,
        Request.WireframeMaterial))
    {
        return false;
    }

    FViewportCompositeItem FullscreenItem;
    FullscreenItem.Mode = Request.CompositeMode;
    FullscreenItem.SceneColorSRV = Targets.SceneColorSRV;
    FullscreenItem.SceneDepthSRV = Targets.SceneDepthSRV;
    FullscreenItem.VisualizationParams.NearZ = View.NearZ;
    FullscreenItem.VisualizationParams.FarZ = View.FarZ;
    FullscreenItem.VisualizationParams.bOrthographic = 0u;
    FullscreenItem.Rect.X = 0;
    FullscreenItem.Rect.Y = 0;
    FullscreenItem.Rect.Width = static_cast<int32>(Renderer.GetRenderDevice().GetViewport().Width);
    FullscreenItem.Rect.Height = static_cast<int32>(Renderer.GetRenderDevice().GetViewport().Height);
    FullscreenItem.bVisible = true;

    TArray<FViewportCompositeItem> CompositeItems;
    CompositeItems.push_back(FullscreenItem);
    const FViewportCompositePassInputs CompositeInputs{ &CompositeItems };

    FViewContext FramePassView;
    FramePassView.Viewport = Renderer.GetRenderDevice().GetViewport();
    FFramePassContext FramePassContext{
        Renderer,
        Frame,
        FramePassView,
        Renderer.GetRenderTargetView(),
        nullptr,
        &CompositeInputs,
        nullptr };

    FFrameRenderPipeline FramePipeline;
    FramePipeline.AddPass(std::make_unique<FViewportCompositePass>());
    return FramePipeline.Execute(FramePassContext);
}
