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

	void BindShadowResources(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	void UnbindShadowResources(FRenderer& Renderer);

	void Release();

	ID3D11ShaderResourceView* GetShadowDepthArraySRV() const
	{
		return ShadowDepthArraySRV;
	}

	bool RenderShadows(FRenderer& Renderer, const FMeshPassProcessor& Processor, FSceneRenderTargets& Targets, FSceneViewData& SceneViewData);

private:
	bool EnsureResources(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	bool EnsureShadowDepthArray(FRenderer& Renderer);

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

	ID3D11Texture2D*          ShadowDepthArray                             = nullptr;
	ID3D11ShaderResourceView* ShadowDepthArraySRV                          = nullptr;
	ID3D11DepthStencilView*   ShadowViewDSVs[ShadowConfig::MaxShadowViews] = {};

	ID3D11Buffer*             ShadowLightBuffer    = nullptr;
	ID3D11ShaderResourceView* ShadowLightBufferSRV = nullptr;

	ID3D11Buffer*             ShadowViewBuffer    = nullptr;
	ID3D11ShaderResourceView* ShadowViewBufferSRV = nullptr;

	ID3D11SamplerState* ShadowComparisonSampler = nullptr;
};
