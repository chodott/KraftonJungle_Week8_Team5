#include "Renderer/Frame/EditorFrameRenderer.h"

#include "Renderer/Frame/RenderFrameUtils.h"
#include "Renderer/Renderer.h"
#include "Renderer/UI/Screen/ScreenUIRenderer.h"
#include "Renderer/Scene/SceneViewData.h"
#include "Renderer/Scene/SceneRenderer.h"
#include "Renderer/Features/Decal/DecalTextureCache.h"
#include "Renderer/Frame/SceneTargetManager.h"
#include "Renderer/Scene/Builders/DebugSceneBuilder.h"
#include "Renderer/Frame/UI/FramePasses.h"
#include "Renderer/Frame/UI/FramePipeline.h"

bool FEditorFrameRenderer::Render(FRenderer& Renderer, const FEditorFrameRequest& Request)
{
    for (const FViewportScenePassRequest& ScenePass : Request.ScenePasses)
    {
        FSceneRenderTargets Targets;
        if (!Renderer.SceneTargetManager->WrapExternalSceneTargets(
            Renderer.GetDevice(),
            ScenePass.RenderTargetView,
            ScenePass.RenderTargetShaderResourceView,
            ScenePass.DepthStencilView,
            ScenePass.DepthShaderResourceView,
            ScenePass.Viewport,
            Targets))
        {
            continue;
        }

        const FFrameContext Frame = BuildRenderFrameContext(ScenePass.SceneView.TotalTimeSeconds);
        const FViewContext View = BuildRenderViewContext(ScenePass.SceneView, ScenePass.Viewport);

        FSceneViewData SceneViewData;
        Renderer.GetSceneRenderer().BuildSceneViewData(
            Renderer,
            ScenePass.ScenePacket,
            Frame,
            View,
            ScenePass.AdditionalMeshBatches,
            SceneViewData);
        Renderer.DecalTextureCache->ResolveTextureArray(Renderer.GetDevice(), SceneViewData);
        SceneViewData.ShowFlags = ScenePass.DebugInputs.ShowFlags;
        SceneViewData.PostProcessInputs.OutlineItems = ScenePass.OutlineRequest.Items;
        SceneViewData.PostProcessInputs.bOutlineEnabled = ScenePass.OutlineRequest.bEnabled;
        BuildDebugLinePassInputs(ScenePass.DebugInputs, SceneViewData.DebugInputs.LinePass);

        if (!Renderer.GetSceneRenderer().RenderSceneView(
            Renderer,
            Targets,
            SceneViewData,
            ScenePass.ClearColor,
            ScenePass.bForceWireframe,
            ScenePass.WireframeMaterial))
        {
            continue;
        }
    }

    const float FrameTimeSeconds = !Request.ScenePasses.empty()
        ? Request.ScenePasses.front().SceneView.TotalTimeSeconds
        : 0.0f;
    const FFrameContext Frame = BuildRenderFrameContext(FrameTimeSeconds);

    FViewContext FramePassView;
    FramePassView.Viewport = Renderer.GetRenderDevice().GetViewport();
    const FViewportCompositePassInputs CompositeInputs{ &Request.CompositeItems };

    FScreenUIPassInputs ScreenUIInputs;
    if (!Renderer.GetScreenUIRenderer().BuildPassInputs(
        Renderer,
        Request.ScreenDrawList,
        Renderer.GetRenderDevice().GetViewport(),
        ScreenUIInputs))
    {
        return false;
    }

    FFramePassContext FramePassContext{
        Renderer,
        Frame,
        FramePassView,
        Renderer.GetRenderTargetView(),
        nullptr,
        &CompositeInputs,
        &ScreenUIInputs };

    FFrameRenderPipeline FramePipeline;
    FramePipeline.AddPass(std::make_unique<FViewportCompositePass>());
    FramePipeline.AddPass(std::make_unique<FScreenUIPass>());
    return FramePipeline.Execute(FramePassContext);
}
