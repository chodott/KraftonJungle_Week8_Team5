#include "Renderer/Common/SceneTargetManager.h"

namespace
{
    void ReleaseCOMResource(IUnknown*& Resource)
    {
        if (Resource)
        {
            Resource->Release();
            Resource = nullptr;
        }
    }
}

bool FSceneTargetManager::CreateColorRenderTarget(
    ID3D11Device* Device,
    uint32 Width,
    uint32 Height,
    DXGI_FORMAT Format,
    ID3D11Texture2D** OutTexture,
    ID3D11RenderTargetView** OutRTV,
    ID3D11ShaderResourceView** OutSRV)
{
    if (!Device || !OutTexture || !OutRTV || !OutSRV)
    {
        return false;
    }

    *OutTexture = nullptr;
    *OutRTV = nullptr;
    *OutSRV = nullptr;

    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = Width;
    Desc.Height = Height;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = Format;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(Device->CreateTexture2D(&Desc, nullptr, OutTexture)) || !*OutTexture)
    {
        return false;
    }

    if (FAILED(Device->CreateRenderTargetView(*OutTexture, nullptr, OutRTV)) || !*OutRTV)
    {
        (*OutTexture)->Release();
        *OutTexture = nullptr;
        return false;
    }

    if (FAILED(Device->CreateShaderResourceView(*OutTexture, nullptr, OutSRV)) || !*OutSRV)
    {
        (*OutRTV)->Release();
        (*OutTexture)->Release();
        *OutRTV = nullptr;
        *OutTexture = nullptr;
        return false;
    }

    return true;
}

bool FSceneTargetManager::CreateDepthTarget(
    ID3D11Device* Device,
    uint32 Width,
    uint32 Height,
    ID3D11Texture2D** OutTexture,
    ID3D11DepthStencilView** OutDSV,
    ID3D11ShaderResourceView** OutSRV)
{
    if (!Device || !OutTexture || !OutDSV || !OutSRV)
    {
        return false;
    }

    *OutTexture = nullptr;
    *OutDSV = nullptr;
    *OutSRV = nullptr;

    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = Width;
    Desc.Height = Height;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(Device->CreateTexture2D(&Desc, nullptr, OutTexture)) || !*OutTexture)
    {
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
    DSVDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(Device->CreateDepthStencilView(*OutTexture, &DSVDesc, OutDSV)) || !*OutDSV)
    {
        (*OutTexture)->Release();
        *OutTexture = nullptr;
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MipLevels = 1;
    if (FAILED(Device->CreateShaderResourceView(*OutTexture, &SRVDesc, OutSRV)) || !*OutSRV)
    {
        (*OutDSV)->Release();
        (*OutTexture)->Release();
        *OutDSV = nullptr;
        *OutTexture = nullptr;
        return false;
    }

    return true;
}

void FSceneTargetManager::ReleaseCOM(IUnknown*& Resource)
{
    ReleaseCOMResource(Resource);
}

void FSceneTargetManager::ReleaseSupplementalTargets()
{
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferASRV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferARTV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferATexture));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferBSRV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferBRTV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferBTexture));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferCSRV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferCRTV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferCTexture));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(SceneColorScratchSRV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(SceneColorScratchRTV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(SceneColorScratchTexture));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(OutlineMaskSRV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(OutlineMaskRTV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(OutlineMaskTexture));
    SupplementalTargetCacheWidth = 0;
    SupplementalTargetCacheHeight = 0;
}

bool FSceneTargetManager::EnsureGameSceneTargets(ID3D11Device* Device, uint32 Width, uint32 Height)
{
    if (Width == 0 || Height == 0 || !Device)
    {
        return false;
    }

    if (GameSceneColorRTV && GameSceneDepthDSV
        && GameSceneTargetCacheWidth == Width
        && GameSceneTargetCacheHeight == Height)
    {
        return EnsureSupplementalTargets(Device, Width, Height);
    }

    Release();

    if (!CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, &GameSceneColorTexture, &GameSceneColorRTV, &GameSceneColorSRV))
    {
        Release();
        return false;
    }

    if (!CreateDepthTarget(Device, Width, Height, &GameSceneDepthTexture, &GameSceneDepthDSV, &GameSceneDepthSRV))
    {
        Release();
        return false;
    }

    GameSceneTargetCacheWidth = Width;
    GameSceneTargetCacheHeight = Height;
    return EnsureSupplementalTargets(Device, Width, Height);
}

bool FSceneTargetManager::EnsureSupplementalTargets(ID3D11Device* Device, uint32 Width, uint32 Height)
{
    if (Width == 0 || Height == 0 || !Device)
    {
        return false;
    }

    if (GBufferARTV && GBufferBRTV && GBufferCRTV && SceneColorScratchRTV && OutlineMaskRTV
        && SupplementalTargetCacheWidth == Width
        && SupplementalTargetCacheHeight == Height)
    {
        return true;
    }

    ReleaseSupplementalTargets();

    if (!CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, &GBufferATexture, &GBufferARTV, &GBufferASRV)
        || !CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R16G16B16A16_FLOAT, &GBufferBTexture, &GBufferBRTV, &GBufferBSRV)
        || !CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, &GBufferCTexture, &GBufferCRTV, &GBufferCSRV)
        || !CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, &SceneColorScratchTexture, &SceneColorScratchRTV, &SceneColorScratchSRV)
        || !CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, &OutlineMaskTexture, &OutlineMaskRTV, &OutlineMaskSRV))
    {
        Release();
        return false;
    }

    SupplementalTargetCacheWidth = Width;
    SupplementalTargetCacheHeight = Height;
    return true;
}

bool FSceneTargetManager::AcquireGameSceneTargets(ID3D11Device* Device, const D3D11_VIEWPORT& Viewport, FSceneRenderTargets& OutTargets)
{
    if (!EnsureGameSceneTargets(Device, static_cast<uint32>(Viewport.Width), static_cast<uint32>(Viewport.Height)))
    {
        return false;
    }

    OutTargets = {};
    OutTargets.Width = static_cast<uint32>(Viewport.Width);
    OutTargets.Height = static_cast<uint32>(Viewport.Height);
    OutTargets.SceneColorTexture = GameSceneColorTexture;
    OutTargets.SceneColorRTV = GameSceneColorRTV;
    OutTargets.SceneColorSRV = GameSceneColorSRV;
    OutTargets.SceneColorScratchTexture = SceneColorScratchTexture;
    OutTargets.SceneColorScratchRTV = SceneColorScratchRTV;
    OutTargets.SceneColorScratchSRV = SceneColorScratchSRV;
    OutTargets.SceneDepthTexture = GameSceneDepthTexture;
    OutTargets.SceneDepthDSV = GameSceneDepthDSV;
    OutTargets.SceneDepthSRV = GameSceneDepthSRV;
    OutTargets.GBufferATexture = GBufferATexture;
    OutTargets.GBufferARTV = GBufferARTV;
    OutTargets.GBufferASRV = GBufferASRV;
    OutTargets.GBufferBTexture = GBufferBTexture;
    OutTargets.GBufferBRTV = GBufferBRTV;
    OutTargets.GBufferBSRV = GBufferBSRV;
    OutTargets.GBufferCTexture = GBufferCTexture;
    OutTargets.GBufferCRTV = GBufferCRTV;
    OutTargets.GBufferCSRV = GBufferCSRV;
    OutTargets.OutlineMaskTexture = OutlineMaskTexture;
    OutTargets.OutlineMaskRTV = OutlineMaskRTV;
    OutTargets.OutlineMaskSRV = OutlineMaskSRV;
    return true;
}

bool FSceneTargetManager::WrapExternalSceneTargets(
    ID3D11Device* Device,
    ID3D11RenderTargetView* RenderTargetView,
    ID3D11ShaderResourceView* RenderTargetShaderResourceView,
    ID3D11DepthStencilView* DepthStencilView,
    ID3D11ShaderResourceView* DepthShaderResourceView,
    const D3D11_VIEWPORT& Viewport,
    FSceneRenderTargets& OutTargets)
{
    const uint32 Width = static_cast<uint32>(Viewport.Width);
    const uint32 Height = static_cast<uint32>(Viewport.Height);
    if (!RenderTargetView || !RenderTargetShaderResourceView || !DepthStencilView || !DepthShaderResourceView
        || !EnsureSupplementalTargets(Device, Width, Height))
    {
        return false;
    }

    OutTargets = {};
    OutTargets.Width = Width;
    OutTargets.Height = Height;
    OutTargets.SceneColorRTV = RenderTargetView;
    OutTargets.SceneColorSRV = RenderTargetShaderResourceView;
    OutTargets.SceneColorScratchTexture = SceneColorScratchTexture;
    OutTargets.SceneColorScratchRTV = SceneColorScratchRTV;
    OutTargets.SceneColorScratchSRV = SceneColorScratchSRV;
    OutTargets.SceneDepthDSV = DepthStencilView;
    OutTargets.SceneDepthSRV = DepthShaderResourceView;
    OutTargets.GBufferATexture = GBufferATexture;
    OutTargets.GBufferARTV = GBufferARTV;
    OutTargets.GBufferASRV = GBufferASRV;
    OutTargets.GBufferBTexture = GBufferBTexture;
    OutTargets.GBufferBRTV = GBufferBRTV;
    OutTargets.GBufferBSRV = GBufferBSRV;
    OutTargets.GBufferCTexture = GBufferCTexture;
    OutTargets.GBufferCRTV = GBufferCRTV;
    OutTargets.GBufferCSRV = GBufferCSRV;
    OutTargets.OutlineMaskTexture = OutlineMaskTexture;
    OutTargets.OutlineMaskRTV = OutlineMaskRTV;
    OutTargets.OutlineMaskSRV = OutlineMaskSRV;
    return true;
}

void FSceneTargetManager::Release()
{
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneColorSRV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneColorRTV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneColorTexture));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneDepthSRV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneDepthDSV));
    ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneDepthTexture));
    GameSceneTargetCacheWidth = 0;
    GameSceneTargetCacheHeight = 0;
    ReleaseSupplementalTargets();
}
