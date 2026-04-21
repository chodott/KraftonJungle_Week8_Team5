#include "Renderer/UI/Screen/ScreenUIRenderer.h"

#include "Renderer/UI/Screen/ScreenUIBatchRenderer.h"
#include "Renderer/UI/Screen/ScreenUIPassBuilder.h"

FScreenUIRenderer::FScreenUIRenderer()
    : PassBuilder(std::make_unique<FScreenUIPassBuilder>())
    , BatchRenderer(std::make_unique<FScreenUIBatchRenderer>())
{
}

FScreenUIRenderer::~FScreenUIRenderer() = default;

bool FScreenUIRenderer::BuildPassInputs(
    FRenderer& Renderer,
    const FUIDrawList& DrawList,
    const D3D11_VIEWPORT& OutputViewport,
    FScreenUIPassInputs& OutPassInputs)
{
    return PassBuilder
        ? PassBuilder->BuildPassInputs(Renderer, DrawList, OutputViewport, OutPassInputs)
        : false;
}

bool FScreenUIRenderer::Render(
    FRenderer& Renderer,
    const FFrameContext& Frame,
    const FScreenUIPassInputs& PassInputs,
    ID3D11RenderTargetView* RenderTargetView,
    ID3D11DepthStencilView* DepthStencilView)
{
    return BatchRenderer
        ? BatchRenderer->Render(Renderer, Frame, PassInputs, RenderTargetView, DepthStencilView)
        : false;
}
