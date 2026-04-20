#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Lighting/LightTypes.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/SceneViewData.h"

#include <d3d11.h>

class FRenderer;

enum class ELightingModel : uint8
{
	Gouraud,
	Lambert,
	Phong
};

class ENGINE_API FLightRenderFeature
{
public:
	~FLightRenderFeature();

	bool PrepareClusteredLightResources(
		FRenderer&                 Renderer,
		const FSceneViewData&      SceneViewData,
		const FSceneRenderTargets& Targets);

	bool Render(
		FRenderer&                 Renderer,
		const FSceneViewData&      SceneViewData,
		const FSceneRenderTargets& Targets);

	void Release();

	void SetLightingModel(ELightingModel Model)
	{
		CurrentLightingModel = Model;
	}

	ELightingModel GetLightingModel() const
	{
		return CurrentLightingModel;
	}

	ID3D11VertexShader* GetCurrentVS() const;
	ID3D11PixelShader*  GetCurrentPS() const;

	ID3D11InputLayout* GetInputLayout() const
	{
		return LightInputLayout;
	}

private:
	bool Initialize(FRenderer& Renderer);
	bool CompileShaderVariants(FRenderer& Renderer);

	void UpdateGlobalLightConstantBuffer(FRenderer& Renderer, const FSceneViewData& SceneViewData);
	void UpdateClusterGlobalConstantBuffer(FRenderer& Renderer, const FSceneViewData& SceneViewData);
	void UploadLocalLightBuffers(FRenderer& Renderer, const FSceneViewData& SceneViewData);

	bool EnsureDynamicStructuredBufferSRV(
		FRenderer&                 Renderer,
		uint32                     ElementStride,
		uint32                     ElementCount,
		ID3D11Buffer*&             Buffer,
		ID3D11ShaderResourceView*& SRV);

	bool EnsureDefaultStructuredBufferSRVUAV(
		FRenderer&                  Renderer,
		uint32                      ElementStride,
		uint32                      ElementCount,
		ID3D11Buffer*&              Buffer,
		ID3D11ShaderResourceView*&  SRV,
		ID3D11UnorderedAccessView*& UAV);

	ID3D11Buffer* GlobalLightConstantBuffer   = nullptr;
	ID3D11Buffer* ClusterGlobalConstantBuffer = nullptr;

	ID3D11Buffer*             LocalLightBuffer = nullptr;
	ID3D11ShaderResourceView* LocalLightSRV    = nullptr;

	ID3D11Buffer*             LightCullProxyBuffer = nullptr;
	ID3D11ShaderResourceView* LightCullProxySRV    = nullptr;

	ID3D11Buffer*             ObjectLightIndexBuffer = nullptr;
	ID3D11ShaderResourceView* ObjectLightIndexSRV    = nullptr;

	ID3D11Buffer*             ClusterLightHeaderBuffer = nullptr;
	ID3D11ShaderResourceView* ClusterLightHeaderSRV    = nullptr;
	ID3D11UnorderedAccessView* ClusterLightHeaderUAV   = nullptr;

	ID3D11Buffer*             ClusterLightIndexBuffer = nullptr;
	ID3D11ShaderResourceView* ClusterLightIndexSRV    = nullptr;
	ID3D11UnorderedAccessView* ClusterLightIndexUAV   = nullptr;

	ID3D11ComputeShader* LightCullingCS = nullptr;

	ID3D11InputLayout*  LightInputLayout = nullptr;
	ID3D11SamplerState* DepthSampler     = nullptr;

	ID3D11VertexShader* GouraudVS = nullptr;
	ID3D11PixelShader*  GouraudPS = nullptr;
	ID3D11VertexShader* LambertVS = nullptr;
	ID3D11PixelShader*  LambertPS = nullptr;
	ID3D11VertexShader* PhongVS   = nullptr;
	ID3D11PixelShader*  PhongPS   = nullptr;

	ELightingModel CurrentLightingModel = ELightingModel::Phong;
};
