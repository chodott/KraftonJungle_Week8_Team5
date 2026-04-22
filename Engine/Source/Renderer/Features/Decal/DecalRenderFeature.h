#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Decal/DecalStats.h"
#include "Renderer/Features/Decal/DecalTypes.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Common/SceneRenderTargets.h"

#include <d3d11.h>
#include <chrono>

#include "Renderer/Resources/Shader/ShaderHandles.h"

class FRenderer;
struct FClusterRange
{
	bool bValid = false;

	uint32 MinTileX = 0;
	uint32 MaxTileX = 0;

	uint32 MinTileY = 0;
	uint32 MaxTileY = 0;

	uint32 MinSliceZ = 0;
	uint32 MaxSliceZ = 0;
};

class ENGINE_API FDecalRenderFeature
{
public:
	~FDecalRenderFeature();

	bool Render(
		FRenderer& Renderer,
		const FDecalRenderRequest& Request,
		FSceneRenderTargets& Targets);
	bool RenderDebugOverlay(
		FRenderer& Renderer,
		const FDecalRenderRequest& Request,
		const FSceneRenderTargets& Targets,
		ID3D11RenderTargetView* RenderTargetView,
		const FLinearColor& DebugColor);

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

	TArray<FClusterRange>	CachedClusterRanges;
	TArray<uint32>			CachedCount;
	TArray<uint32>			CachedWriteCursor;
	bool					bVisibleDataValid = false;
	bool					bClusterDataValid = false;
	uint64				LastVisibleSignature = 0;
	uint64				LastClusterSignature = 0;
	FDecalPreparedViewData	CachedPreparedData;
	
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
	std::shared_ptr<FVertexShaderHandle> CompositeVS = nullptr;
	std::shared_ptr<FPixelShaderHandle> CompositePS = nullptr;
	
	std::shared_ptr<FVertexShaderHandle> DebugBoxVS = nullptr;
	std::shared_ptr<FPixelShaderHandle> DebugBoxPS = nullptr;
	
	
	ID3D11Buffer*             DebugBoxVertexBuffer  = nullptr;
	ID3D11Buffer*             DebugBoxIndexBuffer   = nullptr;
	UINT                      DebugBoxIndexCount    = 0;
	ID3D11Buffer*             DebugBoxConstantBuffer = nullptr;
	ID3D11DepthStencilState*  DebugBoxDepthState    = nullptr;
	ID3D11BlendState*         DebugBoxBlendState    = nullptr;
	ID3D11RasterizerState*    DebugBoxRasterizerState = nullptr;
};
