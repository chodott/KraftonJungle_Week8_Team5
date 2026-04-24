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
	FShadowStats GetStats() const { return Stats; }
private:
	bool EnsureShadowMapResources(FRenderer& Renderer, uint32 Resolution);

private:

	ID3D11Texture2D* ShadowDepthTexture = nullptr;
	ID3D11DepthStencilView* ShadowDSV = nullptr;
	ID3D11ShaderResourceView* ShadowSRV = nullptr;
	ID3D11Buffer* ShadowPassCB = nullptr;
	std::shared_ptr<FVertexShaderHandle> ShadowDepthVS = nullptr;

	ID3D11RasterizerState* ShadowRasterizerState = nullptr;
	ID3D11Buffer* ShadowMatricesBuffer = nullptr;
	ID3D11ShaderResourceView* ShadowMatricesSRV = nullptr;
	TArray<FShadowRenderItem> ShadowItems;
	FShadowStats Stats;
};