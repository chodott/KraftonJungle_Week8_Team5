#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/FireBall/FireBallTypes.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Resources/Shader/ShaderHandles.h"

#include <d3d11.h>

class FRenderer;

class FBloomRenderFeature
{
public:
	~FBloomRenderFeature();

	bool Render(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		FSceneRenderTargets& Targets);
	void Release();

private:
	ID3D11Texture2D* BloomBrightnessTexture = nullptr;
	ID3D11RenderTargetView* BloomBrightnessRTV = nullptr;
	ID3D11ShaderResourceView* BloomBrightnessSRV = nullptr;
	ID3D11UnorderedAccessView* BloomBrightnessUAV = nullptr;

	// Bloom Scratch (Blur 출력)
	ID3D11Texture2D* BloomScratchTexture = nullptr;
	ID3D11ShaderResourceView* BloomScratchSRV = nullptr;
	ID3D11UnorderedAccessView* BloomScratchUAV = nullptr;

	// Constant Buffers
	ID3D11Buffer* ThresholdConstantBuffer = nullptr;
	ID3D11Buffer* BlurConstantBuffer = nullptr;
	ID3D11Buffer* CompositeConstantBuffer = nullptr;

	// Shaders (기존 FullscreenVS, BloomPostPS 대체)
	std::shared_ptr<FComputeShaderHandle> ThresholdCS;
	std::shared_ptr<FComputeShaderHandle> BlurCS;
	std::shared_ptr<FComputeShaderHandle> CompositeCS;

	bool Initialize(FRenderer& Renderer, const FSceneRenderTargets& Targets);
	void UpdateThresholdConstantBuffer(FRenderer& Renderer);
	void UpdateBlurConstantBuffer(FRenderer& Renderer, UINT Width, UINT Height);
	void UpdateCompositeConstantBuffer(FRenderer& Renderer);
};

