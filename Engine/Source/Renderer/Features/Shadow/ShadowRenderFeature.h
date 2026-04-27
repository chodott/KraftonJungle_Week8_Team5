#pragma once

#include "CoreMinimal.h"
#include "ShadowTypes.h"
#include <d3d11.h>

#include "Renderer/Resources/Shader/ShaderHandles.h"

struct FSceneViewData;
struct FSceneRenderTargets;
class FMeshPassProcessor;
class FRenderer;

enum class EShadowDebugViewMode : uint32
{
	None        = 0,
	Depth       = 1,
	VSMMean     = 2,
	VSMVariance = 3
};

class FShadowRenderFeature
{
public:
	~FShadowRenderFeature();

	void SetDefaultShadowMapResolution(uint32 Resolution);

	uint32 GetDefaultShadowMapResolution() const
	{
		return DefaultShadowMapResolution;
	}

	void SetGlobalFilterMode(EShadowFilterMode InMode)
	{
		GlobalFilterMode = InMode;
	}

	EShadowFilterMode GetGlobalFilterMode() const
	{
		return GlobalFilterMode;
	}

	void BindShadowResources(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	void UnbindShadowResources(FRenderer& Renderer);

	void Release();

	ID3D11ShaderResourceView* GetShadowDepthArraySRV() const
	{
		return ShadowDepthArraySRV;
	}

	bool RenderShadows(FRenderer& Renderer, const FMeshPassProcessor& Processor, FSceneRenderTargets& Targets, FSceneViewData& SceneViewData);

	void SetDebugViewMode(EShadowDebugViewMode InMode)
	{
		DebugViewMode = InMode;
	}

	void SetDebugViewSlice(uint32 InSlice)
	{
		DebugViewSlice = InSlice;
	}

	EShadowDebugViewMode GetDebugViewMode() const
	{
		return DebugViewMode;
	}

	uint32 GetDebugViewSlice() const
	{
		return DebugViewSlice;
	}

	ID3D11ShaderResourceView* GetShadowDebugPreviewSRV() const
	{
		return ShadowDebugPreviewSRV;
	}

	const TArray<uint32>& GetDebugAvailableSlices() const
	{
		return DebugAvailableSlices;
	}

	void SetDebugViewportOverlayEnabled(bool bEnabled)
	{
		bDebugViewportOverlayEnabled = bEnabled;
	}

	bool IsDebugViewportOverlayEnabled() const
	{
		return bDebugViewportOverlayEnabled;
	}

private:
	bool EnsureLinearSampler(const FRenderer& Renderer);
	bool EnsureMomentsArray(const FRenderer& Renderer, uint32 RequiredResolution);
	bool EnsureResources(FRenderer& Renderer, const FSceneViewData& SceneViewData);
	bool EnsureShadowDepthArray(FRenderer& Renderer, uint32 RequiredResolution);
	bool EnsureShadowBuffers(FRenderer& Renderer, uint32 ShadowLightCount, uint32 ShadowViewCount);

	bool EnsureDirMomentsArray(const FRenderer& Renderer, uint32 RequiredResolution);
	bool EnsureDirShadowDepthArray(FRenderer& Renderer, uint32 RequiredResolution);
	bool EnsureDirShadowBuffers(FRenderer& Renderer, uint32 ShadowLightCount, uint32 ShadowViewCount);

	bool EnsureDynamicStructuredBufferSRV(
		FRenderer&                 Renderer,
		uint32                     ElementStride,
		uint32                     ElementCount,
		ID3D11Buffer*&             Buffer,
		ID3D11ShaderResourceView*& SRV);

	bool EnsureComparisonSampler(FRenderer& Renderer);

	void UploadShadowBuffers(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	void RenderShadowViews(FRenderer& Renderer, const FMeshPassProcessor& Processor, FSceneRenderTargets& Targets, FSceneViewData& SceneViewData);
	void RenderDirectionalShadows(FRenderer& Renderer, const FMeshPassProcessor& Processor, FSceneRenderTargets& Targets, FSceneViewData& SceneViewData);

	uint32         ResolveShadowViewResolution(uint32 RequestedResolution) const;
	uint32         ComputeRequiredShadowDepthArrayResolution(const FSceneViewData& SceneViewData) const;
	D3D11_VIEWPORT BuildShadowViewport(uint32 RequestedResolution) const;
	float          GetShadowViewportScale(uint32 RequestedResolution) const;
	bool           EnsureDebugPreviewResources(FRenderer& Renderer);
	bool           RenderDebugPreview(FRenderer& Renderer, FSceneRenderTargets& Targets, const FSceneViewData& SceneViewData);

	ID3D11Texture2D*          ShadowDebugPreviewTexture = nullptr;
	ID3D11RenderTargetView*   ShadowDebugPreviewRTV     = nullptr;
	ID3D11ShaderResourceView* ShadowDebugPreviewSRV     = nullptr;

	ID3D11SamplerState* ShadowDebugSampler = nullptr;
	ID3D11Buffer*       ShadowDebugCB      = nullptr;

	std::shared_ptr<FVertexShaderHandle> ShadowDebugVS = nullptr;
	std::shared_ptr<FPixelShaderHandle>  ShadowDebugPS = nullptr;;

	ID3D11Texture2D*          ShadowDepthArray                             = nullptr;
	ID3D11ShaderResourceView* ShadowDepthArraySRV                          = nullptr;
	ID3D11DepthStencilView*   ShadowViewDSVs[ShadowConfig::MaxShadowViews] = {};

	ID3D11Texture2D*          ShadowMomentsArray                             = nullptr;
	ID3D11RenderTargetView*   ShadowMomentsRTV[ShadowConfig::MaxShadowViews] = {};
	ID3D11ShaderResourceView* ShadowMomentsArraySRV                          = nullptr;

	ID3D11Texture2D*          ShadowMomentsBlur                                  = nullptr;
	ID3D11RenderTargetView*   ShadowMomentsBlurRTV[ShadowConfig::MaxShadowViews] = {};
	ID3D11ShaderResourceView* ShadowMomentsBlurSRV                               = nullptr;

	ID3D11Buffer*             ShadowLightBuffer    = nullptr;
	ID3D11ShaderResourceView* ShadowLightBufferSRV = nullptr;

	ID3D11Buffer*             ShadowViewBuffer    = nullptr;
	ID3D11ShaderResourceView* ShadowViewBufferSRV = nullptr;

	ID3D11Texture2D*			DirShadowDepthArray		= nullptr;
	ID3D11ShaderResourceView*	DirShadowDepthArraySRV	= nullptr;
	ID3D11DepthStencilView*		DirShadowViewDSVs[ShadowConfig::MaxDirCascade] = {};

	ID3D11Texture2D*			DirShadowMomentsArray	= nullptr;
	ID3D11RenderTargetView*		DirShadowMomentsRTV[ShadowConfig::MaxDirCascade] = {};
	ID3D11ShaderResourceView*	DirShadowMomentsArraySRV = nullptr;

	ID3D11Texture2D*			DirShadowMomentsBlur	= nullptr;
	ID3D11RenderTargetView*		DirShadowMomentsBlurRTV[ShadowConfig::MaxDirCascade] = {};
	ID3D11ShaderResourceView*	DirShadowMomentsBlurSRV = nullptr;

	ID3D11Buffer*				DirShadowLightBuffer	= nullptr;
	ID3D11ShaderResourceView*	DirShadowLightBufferSRV	= nullptr;

	ID3D11Buffer*				DirShadowViewBuffer		= nullptr;
	ID3D11ShaderResourceView*	DirShadowViewBufferSRV = nullptr;


	ID3D11SamplerState*  ShadowComparisonSampler    = nullptr;
	ID3D11SamplerState*  ShadowLinearSampler        = nullptr;
	uint32               DefaultShadowMapResolution = ShadowConfig::DefaultShadowMapResolution;
	uint32               ShadowDepthArrayResolution = ShadowConfig::DefaultShadowMapResolution;
	bool                 bShadowDepthArrayDirty = true;
	bool                 bDirShadowDepthArrayDirty     = true;
	bool                 bMomentsBlurValid          = false;
	EShadowFilterMode    GlobalFilterMode           = EShadowFilterMode::VSM;
	EShadowDebugViewMode DebugViewMode              = EShadowDebugViewMode::None;
	uint32               DebugViewSlice             = 0;
	float                DebugVarianceExposure      = 5000.0f;;
	TArray<uint32>       DebugAvailableSlices;
	bool                 bDebugViewportOverlayEnabled = false;
};
