#pragma once

#include "CoreMinimal.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Common/SceneRenderTargets.h"

#include <d3d11.h>

class FRenderer;

class ENGINE_API FFXAARenderFeature
{
public:
	~FFXAARenderFeature();

	bool Render(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		FSceneRenderTargets& Targets);
	void Release();

private:
	bool Initialize(FRenderer& Renderer);
	void UpdateConstantBuffer(FRenderer& Renderer, const FViewContext& View);

private:
	ID3D11Buffer*            FXAAConstantBuffer  = nullptr;
	ID3D11RasterizerState*   RasterizerState     = nullptr;
	ID3D11DepthStencilState* NoDepthState        = nullptr;
	ID3D11SamplerState*      LinearSampler       = nullptr;
	ID3D11VertexShader*      FullscreenVS        = nullptr;
	ID3D11PixelShader*       FXAAPS              = nullptr;
};
