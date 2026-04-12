#pragma once

#include "CoreMinimal.h"
#include "Renderer/RenderFrameContext.h"
#include "Renderer/SceneRenderTargets.h"

#include <d3d11.h>

class FRenderer;
struct FRenderMesh;
class FMaterial;

struct ENGINE_API FOutlineRenderItem
{
	FRenderMesh* Mesh = nullptr;
	FMaterial* Material = nullptr;
	FMatrix WorldMatrix = FMatrix::Identity;
	uint32 IndexStart = 0;
	uint32 IndexCount = 0;
	bool bDisableCulling = false;
};

struct ENGINE_API FOutlineRenderRequest
{
	TArray<FOutlineRenderItem> Items;
	bool bEnabled = true;
};

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
