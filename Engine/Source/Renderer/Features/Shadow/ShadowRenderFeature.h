#pragma once

#include "CoreMinimal.h"
#include "ShadowTypes.h"
#include <d3d11.h>

#include "Renderer/Resources/Shader/ShaderHandles.h"

struct FSceneViewData;
struct FSceneRenderTargets;
class FMeshPassProcessor;
class FRenderer;
class FShadowAtlasAllocator;

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

	ID3D11ShaderResourceView* GetLocalShadowDepthAtlasSRV() const
	{
		return LocalShadowDepthAtlasSRV;
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
	bool EnsureMomentsAtlas(const FRenderer& Renderer, uint32 RequiredResolution);
	bool EnsureResources(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	bool EnsureShadowDepthAtlas(FRenderer& Renderer, uint32 RequiredResolution);

	bool EnsureShadowBuffers(FRenderer& Renderer, uint32 ShadowLightCount, uint32 ShadowViewCount);

	bool EnsureDynamicStructuredBufferSRV(
		FRenderer&                 Renderer,
		uint32                     ElementStride,
		uint32                     ElementCount,
		ID3D11Buffer*&             Buffer,
		ID3D11ShaderResourceView*& SRV);

	bool EnsureComparisonSampler(FRenderer& Renderer);

	void UploadShadowBuffers(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	void RenderShadowViews(FRenderer& Renderer, const FMeshPassProcessor& Processor, FSceneRenderTargets& Targets, FSceneViewData& SceneViewData);

	uint32         ResolveShadowViewResolution(uint32 RequestedResolution) const;
	uint32         ComputeRequiredShadowDepthArrayResolution(const FSceneViewData& SceneViewData) const;
	D3D11_VIEWPORT BuildShadowViewport(int X, int Y, int Size) const;
	bool           EnsureDebugPreviewResources(FRenderer& Renderer);
	bool           RenderDebugPreview(FRenderer& Renderer, FSceneRenderTargets& Targets, const FSceneViewData& SceneViewData);

	ID3D11Texture2D*          ShadowDebugPreviewTexture = nullptr;
	ID3D11RenderTargetView*   ShadowDebugPreviewRTV     = nullptr;
	ID3D11ShaderResourceView* ShadowDebugPreviewSRV     = nullptr;

	ID3D11SamplerState* ShadowDebugSampler = nullptr;
	ID3D11Buffer*       ShadowDebugCB      = nullptr;

	std::shared_ptr<FVertexShaderHandle> ShadowDebugVS = nullptr;
	std::shared_ptr<FPixelShaderHandle>  ShadowDebugPS = nullptr;;

	//Spot
	ID3D11Texture2D*		  LocalShadowDepthAtlas						   = nullptr;
	ID3D11DepthStencilView*   LocalShadowDepthAtlasDSV					   = nullptr;
	ID3D11ShaderResourceView* LocalShadowDepthAtlasSRV					   = nullptr;

	ID3D11Texture2D* LocalShadowMomentsAtlas = nullptr;
	ID3D11RenderTargetView* LocalShadowMomentsAtlasRTV = nullptr;
	ID3D11ShaderResourceView* LocalShadowMomentsAtlasSRV = nullptr;


	ID3D11Texture2D* ShadowCacheDepthCube = nullptr;
	ID3D11Texture2D* ShadowCacheMomentsCube = nullptr;
	ID3D11ShaderResourceView* ShadowCacheDepthCubeSRV = nullptr;
	ID3D11ShaderResourceView* ShadowCacheMomentsCubeSRV = nullptr;
	ID3D11DepthStencilView*   ShadowCacheDepthCubeDSVs[ShadowConfig::MaxShadowViews]   = {};
	ID3D11RenderTargetView*   ShadowCacheMomentsCubeRTVs[ShadowConfig::MaxShadowViews] = {};
	//PointCubeMap
	ID3D11Texture2D*          ShadowDepthCubeArray                             = nullptr;
	ID3D11ShaderResourceView* ShadowDepthCubeArraySRV					   = nullptr;
	ID3D11DepthStencilView*   ShadowDepthCubeDSVs[ShadowConfig::MaxShadowViews] = {};

	ID3D11Texture2D* ShadowMomentsCubeArray = nullptr;
	ID3D11ShaderResourceView* ShadowMomentsCubeArraySRV = nullptr;
	ID3D11RenderTargetView* ShadowMomentsCubeRTVs[ShadowConfig::MaxShadowViews] = {};

	ID3D11Buffer*             ShadowLightBuffer    = nullptr;
	ID3D11ShaderResourceView* ShadowLightBufferSRV = nullptr;

	ID3D11Buffer*             ShadowViewBuffer    = nullptr;
	ID3D11ShaderResourceView* ShadowViewBufferSRV = nullptr;

	ID3D11SamplerState*  ShadowComparisonSampler    = nullptr;
	ID3D11SamplerState*  ShadowLinearSampler        = nullptr;
	uint32               DefaultShadowMapResolution = ShadowConfig::DefaultShadowMapResolution;
	uint32               ShadowDepthArrayResolution = ShadowConfig::DefaultShadowMapResolution;
	bool                 bShadowDepthArrayDirty     = true;
	bool                 bMomentsBlurValid          = false;
	EShadowFilterMode    GlobalFilterMode           = EShadowFilterMode::VSM;
	EShadowDebugViewMode DebugViewMode              = EShadowDebugViewMode::None;
	uint32               DebugViewSlice             = 0;
	float                DebugVarianceExposure      = 5000.0f;;
	TArray<uint32>       DebugAvailableSlices;
	bool                 bDebugViewportOverlayEnabled = false;

	FShadowAtlasAllocator* ShadowAtlasAllocator;
};
