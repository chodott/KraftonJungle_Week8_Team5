#pragma once

#include "CoreMinimal.h"
#include "Renderer/Common/RenderFrameContext.h"
#include <d3d11.h>

class FRenderer;

enum class EViewportCompositeMode : uint8
{
	SceneColor,
	DepthView,
	NormalView,
	GBufferAView,
	GBufferBView,
	GBufferCView,
	OutlineMaskView,
};

struct ENGINE_API FViewportCompositeRect
{
	int32 X = 0;
	int32 Y = 0;
	int32 Width = 0;
	int32 Height = 0;

	// 실제로 그릴 수 있는 크기면 true를 반환한다.
	bool IsValid() const { return Width > 0 && Height > 0; }
};

struct ENGINE_API FViewportVisualizationParams
{
	float NearZ = 0.1f;
	float FarZ = 1000.0f;
	uint32 bOrthographic = 0;
	float Padding = 0.0f;
};

struct ENGINE_API FViewportCompositeItem
{
	EViewportCompositeMode Mode = EViewportCompositeMode::SceneColor;

	ID3D11ShaderResourceView* SceneColorSRV = nullptr;
	ID3D11ShaderResourceView* SceneDepthSRV = nullptr;
	ID3D11ShaderResourceView* OverlayColorSRV = nullptr;

	FViewportVisualizationParams VisualizationParams = {};
	// 장면 텍스처를 백버퍼 어디에 놓을지 나타내는 사각형이다.
	FViewportCompositeRect Rect = {};
	// 리스트를 다시 만들지 않고 합성만 건너뛸 수 있게 한다.
	bool bVisible = true;
};

struct ENGINE_API FViewportCompositePassInputs
{
	const TArray<FViewportCompositeItem>* Items = nullptr;

	bool IsEmpty() const
	{
		return Items == nullptr || Items->empty();
	}
};

class ENGINE_API FViewportCompositor
{
public:
	FViewportCompositor() = default;
	~FViewportCompositor();

	FViewportCompositor(const FViewportCompositor&) = delete;
	FViewportCompositor& operator=(const FViewportCompositor&) = delete;

	// 뷰포트 합성에 필요한 셰이더와 고정 기능 상태를 생성한다.
	bool Initialize(ID3D11Device* Device);
	// 뷰포트 합성에 쓰는 자원을 해제한다.
	void Release();
	// 전달받은 뷰포트 장면 텍스처를 현재 백버퍼에 배치해 그린다.
	bool Compose(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		ID3D11RenderTargetView* RenderTargetView,
		ID3D11DepthStencilView* DepthStencilView,
		const FViewportCompositePassInputs& Inputs) const;

private:
	ID3D11PixelShader* ResolvePixelShader(const FViewportCompositeItem& Item) const;
	ID3D11ShaderResourceView* ResolveSourceSRV(const FViewportCompositeItem& Item) const;
private:
	ID3D11VertexShader* BlitVertexShader = nullptr;
	ID3D11PixelShader* BlitPixelShader = nullptr;
	ID3D11PixelShader* DepthViewPixelShader = nullptr;
	ID3D11SamplerState* PointSampler = nullptr;
	ID3D11BlendState* AlphaBlendState = nullptr;
	ID3D11DepthStencilState* NoDepthState = nullptr;
	ID3D11RasterizerState* ScissorRasterizerState = nullptr;
	ID3D11Buffer* VisualizationConstantBuffer = nullptr;
	bool bInitialized = false;
};
