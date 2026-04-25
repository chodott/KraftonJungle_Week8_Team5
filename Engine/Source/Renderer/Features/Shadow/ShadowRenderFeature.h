#pragma once
#include "CoreMinimal.h"
#include "Renderer/Features/Shadow/ShadowTypes.h"
#include "Renderer/Features/Shadow/ShadowStats.h"
#include "Renderer/Scene/SceneViewData.h"
#include <d3d11.h>
#include <vector>
class FVertexShaderHandle;
class FRenderer;;
class ENGINE_API FShadowRenderFeature
{
public:
	~FShadowRenderFeature();
	bool Initialize(FRenderer& Renderer);
	void Release();

	void PrepareShadowViews(const FSceneViewData& SceneViewData);
	bool RenderDepthPass(FRenderer& Renderer, const FSceneViewData& SceneViewData);
	ID3D11ShaderResourceView* GetShadowSRV() const { return ShadowSRV; }
	ID3D11ShaderResourceView* GetShadowMatricesSRV() const { return ShadowMatricesSRV; }
	ID3D11ShaderResourceView* GetPointShadowSRV() const { return PointShadowSRV; }
	ID3D11ShaderResourceView* GetPointShadowMatricesSRV() const { return PointShadowMatricesSRV; }
	FShadowStats GetStats() const { return Stats; }
private:
	bool EnsureShadowMapResources(FRenderer& Renderer, uint32 Resolution);
	bool EnsurePointShadowMapResources(FRenderer& Renderer, uint32 Resolution);
	void BuildSpotShadowItem(const FLocalLightRenderItem& InLight);
	void BuildPointShadowItems(const FLocalLightRenderItem& InLight);
	void UploadSpotShadowMatrices();
	void UploadPointShadowMatrices();
private:



	ID3D11ShaderResourceView* ShadowSRV = nullptr;
	ID3D11RasterizerState* ShadowRasterizerState = nullptr;
	std::shared_ptr<FVertexShaderHandle> ShadowDepthVS = nullptr;
	FShadowStats Stats;


	static constexpr uint32 MaxPointShadows = 4;
	ID3D11Texture2D* PointShadowCubeArray = nullptr;
	TArray<ID3D11DepthStencilView*> PointShadowDSVs;
	ID3D11ShaderResourceView* PointShadowSRV = nullptr;
	TArray<FShadowRenderItem> PointShadowItems;
	ID3D11Buffer* ShadowPassCB = nullptr;


	static constexpr uint32 MaxShadowedLights = 8;
	ID3D11Texture2D* ShadowDepthTexture = nullptr;
	ID3D11Buffer* ShadowMatricesBuffer = nullptr;

	ID3D11Buffer* PointShadowMatricesBuffer = nullptr;
	TArray<ID3D11DepthStencilView*> ShadowDSVs;

	ID3D11ShaderResourceView* ShadowMatricesSRV = nullptr;
	ID3D11ShaderResourceView* PointShadowMatricesSRV = nullptr;
	TArray<FShadowRenderItem> ShadowItems;
};