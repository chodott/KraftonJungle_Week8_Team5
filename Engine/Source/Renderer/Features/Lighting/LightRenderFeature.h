#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Lighting/LightTypes.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Common/SceneRenderTargets.h"

#include <d3d11.h>

class FRenderer;

class ENGINE_API FLightRenderFeature {
public:
	~FLightRenderFeature();

	bool Render(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		const FSceneRenderTargets& Targets);
	void Release();

private:
	bool Initialize(FRenderer& Renderer);
	void UpdateLightConstantBuffer(FRenderer& Renderer, const FViewContext& View);

private:
	ID3D11Buffer* LightConstantBuffer = nullptr;
	ID3D11BlendState* LightBlendState = nullptr;
	ID3D11DepthStencilState* NoDepthState = nullptr;
	ID3D11RasterizerState* LightRasterizerState = nullptr;
	ID3D11SamplerState* DepthSampler = nullptr;
	ID3D11InputLayout* LightInputLayout = nullptr;
	ID3D11VertexShader* LightVS = nullptr;
	ID3D11PixelShader* LightPS = nullptr;
};

