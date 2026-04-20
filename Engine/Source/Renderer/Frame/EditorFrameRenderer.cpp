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
    TArray<FViewportCompositeItem> CompositeItems = Request.CompositeItems;

    struct FOverlaySourceBinding
    {
        ID3D11ShaderResourceView* SceneColorSRV = nullptr;
        ID3D11ShaderResourceView* SceneDepthSRV = nullptr;
        ID3D11ShaderResourceView* OverlayColorSRV = nullptr;
    };

    TArray<FOverlaySourceBinding> OverlayBindings;
    OverlayBindings.reserve(Request.ScenePasses.size());

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
		SceneViewData.RenderMode = ScenePass.RenderMode;
        SceneViewData.bForceWireframe = ScenePass.bForceWireframe;
        SceneViewData.PostProcessInputs.OutlineItems = ScenePass.OutlineRequest.Items;
        SceneViewData.PostProcessInputs.bOutlineEnabled = ScenePass.OutlineRequest.bEnabled;
        BuildEditorLinePassInputs(ScenePass.DebugInputs, SceneViewData.DebugInputs.LinePass);
        SceneViewData.DebugInputs.World = ScenePass.DebugInputs.World;

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

        OverlayBindings.push_back({
            ScenePass.RenderTargetShaderResourceView,
            ScenePass.DepthShaderResourceView,
            Targets.OverlayColorSRV
        });
    }

    for (FViewportCompositeItem& CompositeItem : CompositeItems)
    {
        for (const FOverlaySourceBinding& Binding : OverlayBindings)
        {
            if (CompositeItem.SceneColorSRV == Binding.SceneColorSRV
                && CompositeItem.SceneDepthSRV == Binding.SceneDepthSRV)
            {
                CompositeItem.OverlayColorSRV = Binding.OverlayColorSRV;
                break;
            }
        }
    }

    const float FrameTimeSeconds = !Request.ScenePasses.empty()
        ? Request.ScenePasses.front().SceneView.TotalTimeSeconds
        : 0.0f;
    const FFrameContext Frame = BuildRenderFrameContext(FrameTimeSeconds);

    FViewContext FramePassView;
    FramePassView.Viewport = Renderer.GetRenderDevice().GetViewport();
    const FViewportCompositePassInputs CompositeInputs{ &CompositeItems };

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
