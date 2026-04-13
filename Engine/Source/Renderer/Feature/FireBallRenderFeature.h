#pragma once

#include "CoreMinimal.h"
#include "Renderer/LinearColor.h"
#include "Renderer/RenderFrameContext.h"
#include "Renderer/SceneRenderTargets.h"

#include <d3d11.h>

class FRenderer;

struct ENGINE_API FFireBallRenderItem
{
	FVector FireballOrigin = FVector::ZeroVector;
	float Intensity = 1.0f;
	float Radius = 1.0f;
	float RadiusFallOff = 2.0f;
	FLinearColor Color = FLinearColor::White;
};

struct ENGINE_API FFireBallRenderRequest
{
	TArray<FFireBallRenderItem> Items;
	ID3D11ShaderResourceView* DepthTextureSRV = nullptr;
	FMatrix InverseViewProjection = FMatrix::Identity;
	bool bEnabled = true;

	bool IsEmpty() const
	{
		return !bEnabled || DepthTextureSRV == nullptr || Items.empty();
	}
};
class ENGINE_API FFireBallRenderFeature
{
public:
	~FFireBallRenderFeature();

	bool Render(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		const FSceneRenderTargets& Targets,
		const TArray<FFireBallRenderItem>& Items);
	void Release();

private:
	bool Initialize(FRenderer& Renderer);
	void UpdateFireBallConstantBuffer(FRenderer& Renderer, const FViewContext& View, const FFireBallRenderItem& Item);

private:
	ID3D11Buffer* FireBallConstantBuffer = nullptr;
	ID3D11BlendState* FireBallBlendState = nullptr;
	ID3D11DepthStencilState* NoDepthState = nullptr;
	ID3D11RasterizerState* FireBallRasterizerState = nullptr;
	ID3D11SamplerState* DepthSampler = nullptr;
	ID3D11VertexShader* FireBallPostVS = nullptr;
	ID3D11PixelShader* FireBallPostPS = nullptr;
};
