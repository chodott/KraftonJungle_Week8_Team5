#pragma once

#include "CoreMinimal.h"
#include "ShadowTypes.h"
#include <d3d11.h>

struct FSceneViewData;
struct FSceneRenderTargets;
class FMeshPassProcessor;
class FRenderer;

class FShadowRenderFeature
{
public:
	~FShadowRenderFeature();

	void SetDefaultShadowMapResolution(uint32 Resolution);

	uint32 GetDefaultShadowMapResolution() const
	{
		return DefaultShadowMapResolution;
	}

	void BindShadowResources(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	void UnbindShadowResources(FRenderer& Renderer);

	void Release();

	ID3D11ShaderResourceView* GetShadowDepthArraySRV() const
	{
		return ShadowDepthArraySRV;
	}

	bool RenderShadows(FRenderer& Renderer, const FMeshPassProcessor& Processor, FSceneRenderTargets& Targets, FSceneViewData& SceneViewData);

private:
	bool EnsureLinearSampler(const FRenderer& Renderer);
	bool EnsureMomentsArray(const FRenderer& Renderer, uint32 RequiredResolution);
	bool EnsureResources(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	bool EnsureShadowDepthArray(FRenderer& Renderer, uint32 RequiredResolution);

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
	D3D11_VIEWPORT BuildShadowViewport(uint32 RequestedResolution) const;
	float          GetShadowViewportScale(uint32 RequestedResolution) const;

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

	ID3D11SamplerState* ShadowComparisonSampler    = nullptr;
	ID3D11SamplerState* ShadowLinearSampler        = nullptr;
	uint32              DefaultShadowMapResolution = ShadowConfig::DefaultShadowMapResolution;
	uint32              ShadowDepthArrayResolution = ShadowConfig::DefaultShadowMapResolution;
	bool                bShadowDepthArrayDirty     = true;
	bool                bMomentsBlurValid          = false;
};
