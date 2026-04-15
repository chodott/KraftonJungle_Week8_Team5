#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Outline/OutlineTypes.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Common/SceneRenderTargets.h"

#include <d3d11.h>

class FRenderer;

class ENGINE_API FOutlineRenderFeature
{
public:
	~FOutlineRenderFeature();

	bool Render(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		FSceneRenderTargets& Targets,
		const FOutlineRenderRequest& Request);
	bool RenderMaskPass(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		FSceneRenderTargets& Targets,
		const FOutlineRenderRequest& Request);
	bool RenderCompositePass(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		FSceneRenderTargets& Targets,
		const FOutlineRenderRequest& Request);
	void Release();

private:
	bool Initialize(FRenderer& Renderer);
	void UpdateOutlinePostConstantBuffer(FRenderer& Renderer, const FVector4& OutlineColor, float OutlineThickness, float OutlineThreshold);

private:
	ID3D11Buffer* OutlinePostConstantBuffer = nullptr;
	ID3D11DepthStencilState* StencilWriteState = nullptr;
	ID3D11DepthStencilState* StencilEqualState = nullptr;
	ID3D11DepthStencilState* StencilNotEqualState = nullptr;
	ID3D11BlendState* OutlineBlendState = nullptr;
	ID3D11RasterizerState* OutlineRasterizerState = nullptr;
	ID3D11SamplerState* OutlineSampler = nullptr;
	ID3D11VertexShader* OutlinePostVS = nullptr;
	ID3D11PixelShader* OutlineMaskPS = nullptr;
	ID3D11PixelShader* OutlineSobelPS = nullptr;
};
