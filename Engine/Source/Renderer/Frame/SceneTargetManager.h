#pragma once

#include "CoreMinimal.h"
#include "Renderer/Common/SceneRenderTargets.h"

#include <d3d11.h>
#include <unordered_map>

class ENGINE_API FSceneTargetManager
{
public:
    bool EnsureGameSceneTargets(ID3D11Device* Device, uint32 Width, uint32 Height);
    bool EnsureSupplementalTargets(ID3D11Device* Device, uint32 Width, uint32 Height);
    bool AcquireGameSceneTargets(ID3D11Device* Device, const D3D11_VIEWPORT& Viewport, FSceneRenderTargets& OutTargets);
    bool WrapExternalSceneTargets(
        ID3D11Device* Device,
        ID3D11RenderTargetView* RenderTargetView,
        ID3D11ShaderResourceView* RenderTargetShaderResourceView,
        ID3D11DepthStencilView* DepthStencilView,
        ID3D11ShaderResourceView* DepthShaderResourceView,
        const D3D11_VIEWPORT& Viewport,
        FSceneRenderTargets& OutTargets);
    void Release();

private:
    static bool CreateColorRenderTarget(
        ID3D11Device* Device,
        uint32 Width,
        uint32 Height,
        DXGI_FORMAT Format,
        ID3D11Texture2D** OutTexture,
        ID3D11RenderTargetView** OutRTV,
        ID3D11ShaderResourceView** OutSRV);
    static bool CreateDepthTarget(
        ID3D11Device* Device,
        uint32 Width,
        uint32 Height,
        ID3D11Texture2D** OutTexture,
        ID3D11DepthStencilView** OutDSV,
        ID3D11ShaderResourceView** OutSRV);
    static void ReleaseCOM(IUnknown*& Resource);
    static uint64 MakeExternalOverlayKey(
        ID3D11RenderTargetView* RenderTargetView,
        ID3D11DepthStencilView* DepthStencilView);
    struct FExternalOverlayTargets
    {
        uint32 Width = 0;
        uint32 Height = 0;
        ID3D11Texture2D* OverlayColorTexture = nullptr;
        ID3D11RenderTargetView* OverlayColorRTV = nullptr;
        ID3D11ShaderResourceView* OverlayColorSRV = nullptr;
    };
    bool EnsureExternalOverlayTargets(
        ID3D11Device* Device,
        ID3D11RenderTargetView* RenderTargetView,
        ID3D11DepthStencilView* DepthStencilView,
        uint32 Width,
        uint32 Height,
        FExternalOverlayTargets*& OutTargets);
    static void ReleaseExternalOverlayTargets(FExternalOverlayTargets& Targets);
    void ReleaseSupplementalTargets();
    void ReleaseAllExternalOverlayTargets();

private:
    ID3D11Texture2D* GameSceneColorTexture = nullptr;
    ID3D11RenderTargetView* GameSceneColorRTV = nullptr;
    ID3D11ShaderResourceView* GameSceneColorSRV = nullptr;
    ID3D11Texture2D* GameSceneDepthTexture = nullptr;
    ID3D11DepthStencilView* GameSceneDepthDSV = nullptr;
    ID3D11ShaderResourceView* GameSceneDepthSRV = nullptr;

    ID3D11Texture2D* GBufferATexture = nullptr;
    ID3D11RenderTargetView* GBufferARTV = nullptr;
    ID3D11ShaderResourceView* GBufferASRV = nullptr;
    ID3D11Texture2D* GBufferBTexture = nullptr;
    ID3D11RenderTargetView* GBufferBRTV = nullptr;
    ID3D11ShaderResourceView* GBufferBSRV = nullptr;
    ID3D11Texture2D* GBufferCTexture = nullptr;
    ID3D11RenderTargetView* GBufferCRTV = nullptr;
    ID3D11ShaderResourceView* GBufferCSRV = nullptr;
    ID3D11Texture2D* SceneColorScratchTexture = nullptr;
    ID3D11RenderTargetView* SceneColorScratchRTV = nullptr;
    ID3D11ShaderResourceView* SceneColorScratchSRV = nullptr;
    ID3D11Texture2D* OverlayColorTexture = nullptr;
    ID3D11RenderTargetView* OverlayColorRTV = nullptr;
    ID3D11ShaderResourceView* OverlayColorSRV = nullptr;
    ID3D11Texture2D* OutlineMaskTexture = nullptr;
    ID3D11RenderTargetView* OutlineMaskRTV = nullptr;
    ID3D11ShaderResourceView* OutlineMaskSRV = nullptr;

    uint32 GameSceneTargetCacheWidth = 0;
    uint32 GameSceneTargetCacheHeight = 0;
    uint32 SupplementalTargetCacheWidth = 0;
    uint32 SupplementalTargetCacheHeight = 0;

    std::unordered_map<uint64, FExternalOverlayTargets> ExternalOverlayTargetMap;
};
