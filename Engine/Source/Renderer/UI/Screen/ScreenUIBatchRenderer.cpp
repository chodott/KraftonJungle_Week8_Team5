#include "Renderer/UI/Screen/ScreenUIBatchRenderer.h"

#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/GraphicsCore/RenderStateManager.h"
#include "Renderer/Renderer.h"

bool FScreenUIBatchRenderer::DrawBatchCommand(FRenderer& Renderer, const FUIBatchCommand& BatchCommand)
{
    if (!BatchCommand.Mesh || !BatchCommand.Material)
    {
        return true;
    }

    ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
    if (!DeviceContext)
    {
        return false;
    }

    if (!BatchCommand.Mesh->UpdateVertexAndIndexBuffer(Renderer.GetDevice(), DeviceContext))
    {
        return false;
    }

    BatchCommand.Material->Bind(DeviceContext);

    FRenderStateManager& RenderStateManager = *Renderer.GetRenderStateManager();
    RenderStateManager.BindState(BatchCommand.Material->GetRasterizerState());
    RenderStateManager.BindState(BatchCommand.Material->GetDepthStencilState());
    RenderStateManager.BindState(BatchCommand.Material->GetBlendState());

    if (!BatchCommand.Material->HasPixelTextureBinding())
    {
        ID3D11SamplerState* DefaultSampler = Renderer.GetDefaultSampler();
        DeviceContext->PSSetSamplers(0, 1, &DefaultSampler);
    }

    BatchCommand.Mesh->Bind(DeviceContext);
    Renderer.UpdateObjectConstantBuffer(FMatrix::Identity);

    if (!BatchCommand.Mesh->Indices.empty())
    {
        DeviceContext->DrawIndexed(static_cast<UINT>(BatchCommand.Mesh->Indices.size()), 0, 0);
    }
    else
    {
        DeviceContext->Draw(static_cast<UINT>(BatchCommand.Mesh->Vertices.size()), 0);
    }

    return true;
}

bool FScreenUIBatchRenderer::Render(
    FRenderer& Renderer,
    const FFrameContext& Frame,
    const FScreenUIPassInputs& PassInputs,
    ID3D11RenderTargetView* RenderTargetView,
    ID3D11DepthStencilView* DepthStencilView)
{
    if (PassInputs.Batch.Commands.empty())
    {
        return true;
    }

    ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
    if (!DeviceContext || !Renderer.GetRenderStateManager())
    {
        return false;
    }

    BeginPass(Renderer, RenderTargetView, DepthStencilView, PassInputs.View.Viewport, Frame, PassInputs.View);

    for (const FUIBatchCommand& BatchCommand : PassInputs.Batch.Commands)
    {
        if (!DrawBatchCommand(Renderer, BatchCommand))
        {
            return false;
        }
    }

    EndPass(Renderer, RenderTargetView, DepthStencilView, PassInputs.View.Viewport, Frame, PassInputs.View);
    return true;
}
