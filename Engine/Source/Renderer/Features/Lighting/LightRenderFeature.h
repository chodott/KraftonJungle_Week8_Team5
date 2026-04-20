#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Lighting/LightTypes.h"
#include "Renderer/Common/RenderFrameContext.h"
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

class ENGINE_API FLightRenderFeature {
public:
	~FLightRenderFeature();

	bool Render(
		FRenderer& Renderer,
		const FSceneViewData& SceneViewData,
		const FSceneRenderTargets& Targets);
	void Release();

	void SetLightingModel(ELightingModel Model) { CurrentLightingModel = Model; }
	ELightingModel GetLightingModel() const { return CurrentLightingModel; }

	ID3D11VertexShader* GetCurrentVS() const
	{
		switch (CurrentLightingModel)
		{
		case ELightingModel::Gouraud: return GouraudVS;
		case ELightingModel::Lambert: return LambertVS;
		case ELightingModel::Phong:
		default:                      return PhongVS;
		}
	}
	ID3D11PixelShader* GetCurrentPS() const
	{
		switch (CurrentLightingModel)
		{
		case ELightingModel::Gouraud: return GouraudPS;
		case ELightingModel::Lambert: return LambertPS;
		case ELightingModel::Phong:
		default:                      return PhongPS;
		}
	}
	ID3D11InputLayout* GetInputLayout() const { return LightInputLayout; }

private:
	bool Initialize(FRenderer& Renderer);
	void UpdateLightConstantBuffer(FRenderer& Renderer, const FSceneViewData& SceneViewData);
	bool CompileShaderVariants(FRenderer& Renderer);

private:
	ID3D11Buffer* LightConstantBuffer = nullptr;
	ID3D11BlendState* LightBlendState = nullptr;
	ID3D11DepthStencilState* NoDepthState = nullptr;
	ID3D11RasterizerState* LightRasterizerState = nullptr;
	ID3D11SamplerState* DepthSampler = nullptr;
	ID3D11InputLayout* LightInputLayout = nullptr;

	ID3D11VertexShader* GouraudVS = nullptr;
	ID3D11PixelShader* GouraudPS = nullptr;
	ID3D11VertexShader* LambertVS = nullptr;
	ID3D11PixelShader* LambertPS = nullptr;
	ID3D11VertexShader* PhongVS = nullptr;
	ID3D11PixelShader* PhongPS = nullptr;
	
	ELightingModel CurrentLightingModel = ELightingModel::Gouraud;
};

