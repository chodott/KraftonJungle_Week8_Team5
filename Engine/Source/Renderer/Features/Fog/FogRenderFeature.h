#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Fog/FogTypes.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Common/SceneRenderTargets.h"

#include <d3d11.h>

class FRenderer;

class ENGINE_API FFogRenderFeature
{
public:
	~FFogRenderFeature();

	bool Render(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		const FSceneRenderTargets& Targets,
		const TArray<FFogRenderItem>& Items);
	void Release();

private:
	bool Initialize(FRenderer& Renderer);
	void UpdateFogConstantBuffer(FRenderer& Renderer, const FViewContext& View, const FFogRenderItem& Item);

private:
	ID3D11Buffer* FogConstantBuffer = nullptr;
	ID3D11BlendState* FogBlendState = nullptr;
	ID3D11DepthStencilState* NoDepthState = nullptr;
	ID3D11RasterizerState* FogRasterizerState = nullptr;
	ID3D11SamplerState* DepthSampler = nullptr;
	ID3D11VertexShader* FogPostVS = nullptr;
	ID3D11PixelShader* FogPostPS = nullptr;
};
