#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/FireBall/FireBallTypes.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Common/SceneRenderTargets.h"

#include <d3d11.h>

class FRenderer;
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
