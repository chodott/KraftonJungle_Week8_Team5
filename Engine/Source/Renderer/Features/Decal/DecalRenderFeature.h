#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Decal/DecalStats.h"
#include "Renderer/Features/Decal/DecalTypes.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Common/SceneRenderTargets.h"

#include <d3d11.h>
#include <chrono>

class FRenderer;

class ENGINE_API FDecalRenderFeature
{
public:
	~FDecalRenderFeature();

	bool Render(
		FRenderer& Renderer,
		const FDecalRenderRequest& Request,
		const FSceneRenderTargets& Targets);

	void Release();

	const FDecalClusterBuildStats& GetBuildStats() const
	{
		return LastBuildStats;
	}

	const FDecalFrameStats& GetFrameStats() const
	{
		return LastFrameStats;
	}

	FClusteredLookupDecalStats GetClusteredStats() const;

	bool Initialize(FRenderer& Renderer);
private:

	bool BuildFrameData(
		const FDecalRenderRequest& Request,
		FDecalPreparedViewData& OutPreparedData,
		FDecalFrameStats& OutFrameStats);
	bool BuildVisibleDecalData(
		const FDecalRenderRequest& Request,
		FDecalPreparedViewData& InOutPreparedData);
	bool BuildClusterLists(
		const FDecalRenderRequest& Request,
		FDecalPreparedViewData& InOutPreparedData);

	bool UpdateClusterGlobalsConstantBuffer(
		FRenderer& Renderer,
		const FDecalRenderRequest& Request,
		const FDecalPreparedViewData& PreparedData);
	bool UpdateCompositeConstantBuffer(FRenderer& Renderer, const FViewContext& View);
	bool UploadDecalStructuredBuffer(FRenderer& Renderer, const FDecalPreparedViewData& PreparedData);
	bool UploadClusterHeaderStructuredBuffer(FRenderer& Renderer, const FDecalPreparedViewData& PreparedData);
	bool UploadClusterIndexStructuredBuffer(FRenderer& Renderer, const FDecalPreparedViewData& PreparedData);

private:
	bool bInitialized = false;
	FDecalClusterBuildStats LastBuildStats;
	FDecalFrameStats LastFrameStats;

	// GPU resources
	ID3D11Buffer* ClusterGlobalsConstantBuffer = nullptr;
	ID3D11Buffer* CompositeConstantBuffer = nullptr;

	ID3D11Buffer* DecalStructuredBuffer = nullptr;
	ID3D11ShaderResourceView* DecalStructuredBufferSRV = nullptr;

	ID3D11Buffer* ClusterHeaderStructuredBuffer = nullptr;
	ID3D11ShaderResourceView* ClusterHeaderStructuredBufferSRV = nullptr;

	ID3D11Buffer* ClusterIndexStructuredBuffer = nullptr;
	ID3D11ShaderResourceView* ClusterIndexStructuredBufferSRV = nullptr;

	ID3D11BlendState* CompositeBlendState = nullptr;
	ID3D11DepthStencilState* CompositeDepthState = nullptr;
	ID3D11RasterizerState* CompositeRasterizerState = nullptr;
	ID3D11SamplerState* LinearSampler = nullptr;
	ID3D11SamplerState* PointSampler = nullptr;
	ID3D11VertexShader* CompositeVS = nullptr;
	ID3D11PixelShader* CompositePS = nullptr;
};
