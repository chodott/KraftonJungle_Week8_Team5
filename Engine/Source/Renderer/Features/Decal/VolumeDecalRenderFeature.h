#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Decal/DecalStats.h"
#include "Renderer/Features/Decal/DecalTypes.h"
#include "Renderer/Common/SceneRenderTargets.h"

#include <d3d11.h>
#include <memory>

class FRenderer;
class FVertexShader;
class FPixelShader;

class ENGINE_API FVolumeDecalRenderFeature
{
public:
    ~FVolumeDecalRenderFeature();

    bool Initialize(FRenderer& Renderer);
    void Release();

    bool Render(
        FRenderer& Renderer,
        const FDecalRenderRequest& Request,
        const FSceneRenderTargets& Targets);

    const FVolumeDecalStats& GetStats() const { return LastStats; }
    double GetShadingPassTimeMs() const { return LastShadingPassTimeMs; }
    double GetTotalTimeMs() const { return LastTotalTimeMs; }
    uint32 GetTotalDecalCount() const { return LastTotalDecalCount; }
    uint32 GetFadeInOutCount() const { return LastFadeInOutCount; }

private:
    struct FVolumeDecalConstants
    {
        FMatrix InverseViewProjection = FMatrix::Identity;
        FMatrix WorldToDecal = FMatrix::Identity;
        FVector4 AtlasScaleBias = FVector4(1, 1, 0, 0);
        FLinearColor BaseColorTint = FLinearColor::White;
        FVector4 DecalExtentsAndEdgeFade = FVector4(50.0f, 50.0f, 50.0f, 2.0f);
        FVector4 InvViewportSizeAndAllowAngleAndTextureIndex = FVector4(1.0f, 1.0f, 0.0f, 0.0f);
        FVector4 DecalForwardWSAndPad = FVector4(1.0f, 0.0f, 0.0f, 0.0f);
    };

    bool CreateVolumeMesh(FRenderer& Renderer);
    bool CreatePerDecalConstantBuffer(FRenderer& Renderer);
    bool CreateStates(FRenderer& Renderer);
    bool CreateSamplers(FRenderer& Renderer);
    bool CreateShaders(FRenderer& Renderer);

    bool UpdatePerDecalConstants(
        FRenderer& Renderer,
        const FDecalRenderRequest& Request,
        const FDecalRenderItem& Item);

private:
    bool bInitialized = false;
    FVolumeDecalStats LastStats;
    double LastShadingPassTimeMs = 0.0;
    double LastTotalTimeMs = 0.0;
    uint32 LastTotalDecalCount = 0;
    uint32 LastFadeInOutCount = 0;

    std::shared_ptr<FVertexShader> VolumeVS;
    std::shared_ptr<FPixelShader> VolumePS;

    ID3D11Buffer* VolumeVertexBuffer = nullptr;
    ID3D11Buffer* VolumeIndexBuffer = nullptr;
    UINT VolumeIndexCount = 0;

    ID3D11Buffer* PerDecalConstantBuffer = nullptr;

    ID3D11SamplerState* DepthPointSampler = nullptr;
    ID3D11SamplerState* DecalLinearSampler = nullptr;

    ID3D11BlendState* VolumeBlendState = nullptr;
    ID3D11DepthStencilState* VolumeDepthState = nullptr;
    ID3D11RasterizerState* VolumeRasterizerState = nullptr;
	
	std::shared_ptr<FPixelShader> DebugPS = nullptr;
	ID3D11DepthStencilState* DebugDepthState = nullptr;
};
