#pragma once

#include "CoreMinimal.h"
#include "Renderer/LinearColor.h"

#include <d3d11.h>

class FRenderer;

struct ENGINE_API FFogRenderItem
{
	FVector FogOrigin = FVector::ZeroVector;
	float FogDensity = 0.0f;
	float FogHeightFalloff = 0.0f;
	float StartDistance = 0.0f;
	float FogCutoffDistance = 0.0f;
	float FogMaxOpacity = 1.0f;
	float AllowBackground = 1.0f;
	FLinearColor FogInscatteringColor = FLinearColor::White;
};

struct ENGINE_API FFogRenderRequest
{
	TArray<FFogRenderItem> Items;
	ID3D11ShaderResourceView* DepthTextureSRV = nullptr;
	FMatrix InverseViewProjection = FMatrix::Identity;
	FVector CameraPosition = FVector::ZeroVector;
	bool bEnabled = true;

	bool IsEmpty() const
	{
		return !bEnabled || DepthTextureSRV == nullptr || Items.empty();
	}
};

class ENGINE_API FFogRenderFeature
{
public:
	~FFogRenderFeature();

	bool Render(FRenderer& Renderer, const FFogRenderRequest& Request);
	void Release();

private:
	bool Initialize(FRenderer& Renderer);
	void UpdateFogConstantBuffer(FRenderer& Renderer, const FFogRenderRequest& Request, const FFogRenderItem& Item);

private:
	ID3D11Buffer* FogConstantBuffer = nullptr;
	ID3D11BlendState* FogBlendState = nullptr;
	ID3D11DepthStencilState* NoDepthState = nullptr;
	ID3D11RasterizerState* FogRasterizerState = nullptr;
	ID3D11SamplerState* DepthSampler = nullptr;
	ID3D11VertexShader* FogPostVS = nullptr;
	ID3D11PixelShader* FogPostPS = nullptr;
};
