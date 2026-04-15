#include "Renderer/Features/Decal/DecalRenderFeature.h"

#include "Core/Paths.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/Shader/ShaderResource.h"
#include <algorithm>
#include <filesystem>

#include "Renderer/Mesh/Vertex.h"
#include "Renderer/Resources/Shader/ShaderMap.h"

namespace
{
	using FDecalClock = std::chrono::high_resolution_clock;

	static double ToMilliseconds(FDecalClock::duration Duration)
	{
		return std::chrono::duration<double, std::milli>(Duration).count();
	}

	static constexpr UINT DECAL_CLUSTER_CB_SLOT = 3;
	static constexpr UINT DECAL_CLUSTER_HEADERS_SRV_SLOT = 10;
	static constexpr UINT DECAL_CLUSTER_INDICES_SRV_SLOT = 11;
	static constexpr UINT DECAL_DATA_SRV_SLOT = 12;
	static constexpr UINT DECAL_BASECOLOR_TEX_SRV_SLOT = 13;
	static constexpr UINT DECAL_SCENECOLOR_SRV_SLOT = 0;
	static constexpr UINT DECAL_DEPTH_SRV_SLOT = 1;
	static constexpr UINT DECAL_COMPOSITE_CB_SLOT = 4;
	static constexpr UINT DECAL_SCENECOLOR_SAMPLER_SLOT = 0;
	static constexpr UINT DECAL_DEPTH_SAMPLER_SLOT = 1;
	static constexpr UINT DECAL_TEXTURE_SAMPLER_SLOT = 2;

	static uint32 Align16(uint32 Size)
	{
		return (Size + 15u) & ~15u;
	}

	struct FClusterGlobalsCB
	{
		uint32 ClusterCountX = 0;
		uint32 ClusterCountY = 0;
		uint32 ClusterCountZ = 0;
		uint32 MaxClusterItems = 0;

		float ViewportWidth = 0.0f;
		float ViewportHeight = 0.0f;
		float NearZ = 0.0f;
		float FarZ = 0.0f;

		float LogZScale = 0.0f;
		float LogZBias = 0.0f;
		float TileWidth = 0.0f;
		float TileHeight = 0.0f;
	};

	struct FCompositeCB
	{
		FMatrix View = FMatrix::Identity;
		FMatrix InverseViewProjection = FMatrix::Identity;
	};

	static uint32 FlattenClusterIndex(uint32 X, uint32 Y, uint32 Z, const FDecalRenderRequest& Request)
	{
		return X
			+ Y * Request.ClusterCountX
			+ Z * Request.ClusterCountX * Request.ClusterCountY;
	}

	static uint32 ComputeTileX(float ScreenX, const FDecalRenderRequest& Request)
	{
		const float TileWidth = static_cast<float>(Request.ViewportWidth) / static_cast<float>(Request.ClusterCountX);
		int32 X = static_cast<int32>(std::floor(ScreenX / TileWidth));
		X = std::clamp(X, 0, static_cast<int32>(Request.ClusterCountX) - 1);
		return static_cast<uint32>(X);
	}

	static uint32 ComputeTileY(float ScreenY, const FDecalRenderRequest& Request)
	{
		const float TileHeight = static_cast<float>(Request.ViewportHeight) / static_cast<float>(Request.ClusterCountY);
		int32 Y = static_cast<int32>(std::floor(ScreenY / TileHeight));
		Y = std::clamp(Y, 0, static_cast<int32>(Request.ClusterCountY) - 1);
		return static_cast<uint32>(Y);
	}

	static uint32 ComputeZSlice(float ViewDepth, const FDecalRenderRequest& Request)
	{
		const float Depth = std::clamp(ViewDepth, Request.NearZ, Request.FarZ);

		const float LogScale = static_cast<float>(Request.ClusterCountZ) / std::log(Request.FarZ / Request.NearZ);
		const float LogBias = -std::log(Request.NearZ) * LogScale;

		int32 Slice = static_cast<int32>(std::floor(std::log(Depth) * LogScale + LogBias));
		Slice = std::clamp(Slice, 0, static_cast<int32>(Request.ClusterCountZ) - 1);
		return static_cast<uint32>(Slice);
	}

	static uint64 HashBytes(uint64 Seed, const void* Data, size_t Size)
	{
		const uint8* Bytes = reinterpret_cast<const uint8*>(Data);
		uint64 Hash = Seed;
		for (size_t Index = 0; Index < Size; ++Index)
		{
			Hash ^= static_cast<uint64>(Bytes[Index]);
			Hash *= 1099511628211ull;
		}
		return Hash;
	}

	template <typename TValue>
	static uint64 HashValue(uint64 Seed, const TValue& Value)
	{
		return HashBytes(Seed, &Value, sizeof(TValue));
	}

	static uint64 HashWideString(uint64 Seed, const std::wstring& Value)
	{
		if (Value.empty())
		{
			static const wchar_t EmptyMarker = L'\0';
			return HashBytes(Seed, &EmptyMarker, sizeof(EmptyMarker));
		}

		const std::wstring NormalizedPath = std::filesystem::path(Value).lexically_normal().wstring();
		return HashBytes(Seed, NormalizedPath.data(), NormalizedPath.size() * sizeof(wchar_t));
	}

	static uint64 ComputeDecalRequestSignature(const FDecalRenderRequest& Request)
	{
		uint64 Hash = 1469598103934665603ull;

		Hash = HashValue(Hash, Request.ViewProjection);
		Hash = HashValue(Hash, Request.ViewportWidth);
		Hash = HashValue(Hash, Request.ViewportHeight);
		Hash = HashValue(Hash, Request.NearZ);
		Hash = HashValue(Hash, Request.FarZ);
		Hash = HashValue(Hash, Request.ClusterCountX);
		Hash = HashValue(Hash, Request.ClusterCountY);
		Hash = HashValue(Hash, Request.ClusterCountZ);
		Hash = HashValue(Hash, Request.ReceiverLayerMask);
		Hash = HashValue(Hash, Request.MaxClusterItems);
		Hash = HashValue(Hash, static_cast<uint32>(Request.bEnabled ? 1u : 0u));
		Hash = HashValue(Hash, static_cast<uint32>(Request.bSortByPriority ? 1u : 0u));
		Hash = HashValue(Hash, static_cast<uint32>(Request.bClampClusterItemCount ? 1u : 0u));
		Hash = HashValue(Hash, static_cast<uint32>(Request.Items.size()));

		for (const FDecalRenderItem& Item : Request.Items)
		{
			Hash = HashValue(Hash, Item.DecalWorld);
			Hash = HashValue(Hash, Item.WorldToDecal);
			Hash = HashValue(Hash, Item.Extents);
			Hash = HashValue(Hash, Item.TextureIndex);
			Hash = HashWideString(Hash, Item.TexturePath);
			Hash = HashValue(Hash, Item.Flags);
			Hash = HashValue(Hash, Item.Priority);
			Hash = HashValue(Hash, Item.ReceiverLayerMask);
			Hash = HashValue(Hash, Item.AtlasScaleBias);
			Hash = HashValue(Hash, Item.BaseColorTint);
			Hash = HashValue(Hash, Item.NormalBlend);
			Hash = HashValue(Hash, Item.RoughnessBlend);
			Hash = HashValue(Hash, Item.EmissiveBlend);
			Hash = HashValue(Hash, Item.EdgeFade);
			Hash = HashValue(Hash, static_cast<uint32>(Item.bIsFading ? 1u : 0u));
			Hash = HashValue(Hash, Item.AllowAngle);
		}

		return Hash;
	}

	static bool ComputeDecalClusterRange(
		const FDecalRenderRequest& Request,
		const FDecalRenderItem& Item,
		FClusterRange& OutRange)
	{
		OutRange = {};

		static const FVector CornerSigns[8] =
		{
			FVector(-1, -1, -1),
			FVector(1, -1, -1),
			FVector(-1,  1, -1),
			FVector(1,  1, -1),
			FVector(-1, -1,  1),
			FVector(1, -1,  1),
			FVector(-1,  1,  1),
			FVector(1,  1,  1),
		};

		float MinViewDepth = FLT_MAX;
		float MaxViewDepth = -FLT_MAX;
		float MinScreenX = FLT_MAX;
		float MaxScreenX = -FLT_MAX;
		float MinScreenY = FLT_MAX;
		float MaxScreenY = -FLT_MAX;

		bool bTouchesNearPlane = false;
		bool bHasProjectedPoint = false;

		const DirectX::XMMATRIX ViewProjXM = Request.ViewProjection.ToXMMatrix();

		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const FVector LocalCorner(
				CornerSigns[CornerIndex].X * Item.Extents.X,
				CornerSigns[CornerIndex].Y * Item.Extents.Y,
				CornerSigns[CornerIndex].Z * Item.Extents.Z);

			const FVector WorldCorner = Item.DecalWorld.TransformPosition(LocalCorner);
			const FVector ViewCorner = Request.View.TransformPosition(WorldCorner);

			const float ViewDepth = ViewCorner.X;

			MinViewDepth = std::min(MinViewDepth, ViewDepth);
			MaxViewDepth = std::max(MaxViewDepth, ViewDepth);

			if (ViewDepth <= Request.NearZ)
			{
				bTouchesNearPlane = true;
			}

			const DirectX::XMVECTOR WorldCorner4 =
				DirectX::XMVectorSet(WorldCorner.X, WorldCorner.Y, WorldCorner.Z, 1.0f);

			const DirectX::XMVECTOR ClipCornerXM =
				DirectX::XMVector4Transform(WorldCorner4, ViewProjXM);

			const float ClipX = DirectX::XMVectorGetX(ClipCornerXM);
			const float ClipY = DirectX::XMVectorGetY(ClipCornerXM);
			const float ClipW = DirectX::XMVectorGetW(ClipCornerXM);

			if (std::fabs(ClipW) <= 1e-4f)
			{
				continue;
			}

			const float InvW = 1.0f / ClipW;
			const float NdcX = ClipX * InvW;
			const float NdcY = ClipY * InvW;

			const float ScreenX =
				(NdcX * 0.5f + 0.5f) * static_cast<float>(Request.ViewportWidth);

			const float ScreenY =
				(-NdcY * 0.5f + 0.5f) * static_cast<float>(Request.ViewportHeight);

			MinScreenX = std::min(MinScreenX, ScreenX);
			MaxScreenX = std::max(MaxScreenX, ScreenX);
			MinScreenY = std::min(MinScreenY, ScreenY);
			MaxScreenY = std::max(MaxScreenY, ScreenY);

			bHasProjectedPoint = true;
		}

		if (MaxViewDepth < Request.NearZ || MinViewDepth > Request.FarZ)
		{
			return false;
		}

		MinViewDepth = std::clamp(MinViewDepth, Request.NearZ, Request.FarZ);
		MaxViewDepth = std::clamp(MaxViewDepth, Request.NearZ, Request.FarZ);

		OutRange.MinSliceZ = ComputeZSlice(MinViewDepth, Request);
		OutRange.MaxSliceZ = ComputeZSlice(MaxViewDepth, Request);

		if (bTouchesNearPlane || !bHasProjectedPoint)
		{
			OutRange.MinTileX = 0;
			OutRange.MaxTileX = Request.ClusterCountX - 1;
			OutRange.MinTileY = 0;
			OutRange.MaxTileY = Request.ClusterCountY - 1;
			OutRange.bValid = true;
			return true;
		}
		if (MaxScreenX < 0.0f || MaxScreenY < 0.0f ||
			MinScreenX >= static_cast<float>(Request.ViewportWidth) ||
			MinScreenY >= static_cast<float>(Request.ViewportHeight))
		{
			return false;
		}

		MinScreenX = std::clamp(MinScreenX, 0.0f, static_cast<float>(Request.ViewportWidth - 1));
		MaxScreenX = std::clamp(MaxScreenX, 0.0f, static_cast<float>(Request.ViewportWidth - 1));
		MinScreenY = std::clamp(MinScreenY, 0.0f, static_cast<float>(Request.ViewportHeight - 1));
		MaxScreenY = std::clamp(MaxScreenY, 0.0f, static_cast<float>(Request.ViewportHeight - 1));

		OutRange.MinTileX = ComputeTileX(MinScreenX, Request);
		OutRange.MaxTileX = ComputeTileX(MaxScreenX, Request);
		OutRange.MinTileY = ComputeTileY(MinScreenY, Request);
		OutRange.MaxTileY = ComputeTileY(MaxScreenY, Request);
		OutRange.bValid = true;

		return true;
	}
	
	struct FDebugBoxMaterialCB
	{
		FLinearColor BaseColorTint = FLinearColor::White;
		FVector4 AtlasScaleBias = FVector4(1, 1, 0, 0);
		FVector DecalExtents = FVector(50, 50, 50);
		float DecalEdgeFade = 2.0f;
	};
}



FDecalRenderFeature::~FDecalRenderFeature()
{
	Release();
}

bool FDecalRenderFeature::UpdateCompositeConstantBuffer(FRenderer& Renderer, const FViewContext& View)
{
	if (!CompositeConstantBuffer)
	{
		return false;
	}

	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
	if (!Context)
	{
		return false;
	}

	FCompositeCB CBData = {};
	CBData.View = View.View.GetTransposed();
	CBData.InverseViewProjection = View.InverseViewProjection.GetTransposed();

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (FAILED(Context->Map(CompositeConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}

	std::memcpy(Mapped.pData, &CBData, sizeof(FCompositeCB));
	Context->Unmap(CompositeConstantBuffer, 0);
	return true;
}

bool FDecalRenderFeature::Render(
	FRenderer& Renderer,
	const FDecalRenderRequest& Request,
	const FSceneRenderTargets& Targets)
{
	const FDecalClock::time_point PrepareStartTime = FDecalClock::now();
	if (!Initialize(Renderer))
	{
		return false;
	}

	FDecalFrameStats FrameStats;
	FrameStats.InputItemCount = static_cast<uint32>(Request.Items.size());

	if (Request.IsEmpty())
	{
		LastBuildStats = {};
		LastFrameStats = FrameStats;
		return true;
	}

	if (!Targets.SceneColorRTV || !Targets.SceneDepthSRV || !Targets.SceneColorScratchTexture || !Targets.SceneColorScratchSRV)
	{
		return false;
	}

	// BaseColorTextureArraySRV는 매 프레임 갱신 (포인터가 바뀔 수 있음)
	CachedPreparedData.BaseColorTextureArraySRV = Request.BaseColorTextureArraySRV;

	if (!BuildFrameData(Request, CachedPreparedData, FrameStats))
	{
		return false;
	}

	FDecalPreparedViewData& PreparedData = CachedPreparedData;

	if (PreparedData.DecalGPUItems.empty() || PreparedData.ClusterIndexList.empty())
	{
		FrameStats.PrepareTimeMs = ToMilliseconds(FDecalClock::now() - PrepareStartTime);
		LastBuildStats = PreparedData.BuildStats;
		LastFrameStats = FrameStats;
		return true;
	}

	const FDecalClock::time_point ConstantBufferStartTime = FDecalClock::now();
	if (!UpdateClusterGlobalsConstantBuffer(Renderer, Request, PreparedData))
	{
		return false;
	}
	FrameStats.ConstantBufferUpdateTimeMs = ToMilliseconds(FDecalClock::now() - ConstantBufferStartTime);

	const FDecalClock::time_point UploadDecalStartTime = FDecalClock::now();
	if (!UploadDecalStructuredBuffer(Renderer, PreparedData))
	{
		return false;
	}
	FrameStats.UploadDecalBufferTimeMs = ToMilliseconds(FDecalClock::now() - UploadDecalStartTime);
	FrameStats.UploadedDecalCount = static_cast<uint32>(PreparedData.DecalGPUItems.size());

	const FDecalClock::time_point UploadClusterHeaderStartTime = FDecalClock::now();
	if (!UploadClusterHeaderStructuredBuffer(Renderer, PreparedData))
	{
		return false;
	}
	FrameStats.UploadClusterHeaderBufferTimeMs = ToMilliseconds(FDecalClock::now() - UploadClusterHeaderStartTime);
	FrameStats.UploadedClusterHeaderCount = static_cast<uint32>(PreparedData.ClusterHeaders.size());

	const FDecalClock::time_point UploadClusterIndexStartTime = FDecalClock::now();
	if (!UploadClusterIndexStructuredBuffer(Renderer, PreparedData))
	{
		return false;
	}
	FrameStats.UploadClusterIndexBufferTimeMs = ToMilliseconds(FDecalClock::now() - UploadClusterIndexStartTime);
	FrameStats.UploadedClusterIndexCount = static_cast<uint32>(PreparedData.ClusterIndexList.size());
	FrameStats.PrepareTimeMs = ToMilliseconds(FDecalClock::now() - PrepareStartTime);

	FViewContext View;
	View.View = Request.View;
	View.Projection = Request.Projection;
	View.ViewProjection = Request.ViewProjection;
	View.InverseViewProjection = Request.InverseViewProjection;
	View.CameraPosition = Request.CameraPosition;
	View.NearZ = Request.NearZ;
	View.FarZ = Request.FarZ;
	View.Viewport.Width = static_cast<float>(Request.ViewportWidth);
	View.Viewport.Height = static_cast<float>(Request.ViewportHeight);
	View.Viewport.MinDepth = 0.0f;
	View.Viewport.MaxDepth = 1.0f;
	const FFrameContext Frame = {};

	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
	if (!Context || !UpdateCompositeConstantBuffer(Renderer, View))
	{
		return false;
	}

	ID3D11Resource* SourceResource = nullptr;
	bool bReleaseSourceResource = false;
	if (Targets.SceneColorTexture)
	{
		SourceResource = Targets.SceneColorTexture;
	}
	else if (Targets.SceneColorRTV)
	{
		Targets.SceneColorRTV->GetResource(&SourceResource);
		bReleaseSourceResource = true;
	}

	if (!SourceResource)
	{
		return false;
	}

	Context->OMSetRenderTargets(0, nullptr, nullptr);
	Context->CopyResource(Targets.SceneColorScratchTexture, SourceResource);

	if (bReleaseSourceResource)
	{
		SourceResource->Release();
	}

	constexpr float BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	const FFullscreenPassConstantBufferBinding ConstantBuffers[] =
	{
		{ DECAL_CLUSTER_CB_SLOT, ClusterGlobalsConstantBuffer },
		{ DECAL_COMPOSITE_CB_SLOT, CompositeConstantBuffer },
	};
	const FFullscreenPassShaderResourceBinding ShaderResources[] =
	{
		{ DECAL_SCENECOLOR_SRV_SLOT, Targets.SceneColorScratchSRV },
		{ DECAL_DEPTH_SRV_SLOT, Targets.SceneDepthSRV },
		{ DECAL_CLUSTER_HEADERS_SRV_SLOT, ClusterHeaderStructuredBufferSRV },
		{ DECAL_CLUSTER_INDICES_SRV_SLOT, ClusterIndexStructuredBufferSRV },
		{ DECAL_DATA_SRV_SLOT, DecalStructuredBufferSRV },
		{ DECAL_BASECOLOR_TEX_SRV_SLOT, PreparedData.BaseColorTextureArraySRV },
	};
	const FFullscreenPassSamplerBinding Samplers[] =
	{
		{ DECAL_SCENECOLOR_SAMPLER_SLOT, LinearSampler },
		{ DECAL_DEPTH_SAMPLER_SLOT, PointSampler },
		{ DECAL_TEXTURE_SAMPLER_SLOT, LinearSampler },
	};
	const FFullscreenPassBindings Bindings
	{
		ConstantBuffers,
		static_cast<uint32>(sizeof(ConstantBuffers) / sizeof(ConstantBuffers[0])),
		ShaderResources,
		static_cast<uint32>(sizeof(ShaderResources) / sizeof(ShaderResources[0])),
		Samplers,
		static_cast<uint32>(sizeof(Samplers) / sizeof(Samplers[0]))
	};

	FFullscreenPassPipelineState PipelineState;
	PipelineState.BlendState = CompositeBlendState;
	PipelineState.BlendFactor = BlendFactor;
	PipelineState.DepthStencilState = CompositeDepthState;
	PipelineState.RasterizerState = CompositeRasterizerState;
	const FDecalClock::time_point ShadingPassStartTime = FDecalClock::now();
	const bool bRendered = ExecuteFullscreenPass(
		Renderer,
		Frame,
		View,
		Targets.SceneColorRTV,
		nullptr,
		View.Viewport,
		{ CompositeVS, CompositePS },
		PipelineState,
		Bindings,
		[](ID3D11DeviceContext& DrawContext)
		{
			DrawContext.Draw(3, 0);
		});

	FrameStats.ShadingPassTimeMs = ToMilliseconds(FDecalClock::now() - ShadingPassStartTime);
	FrameStats.TotalDecalTimeMs = ToMilliseconds(FDecalClock::now() - PrepareStartTime);
	LastBuildStats = PreparedData.BuildStats;
	LastFrameStats = FrameStats;
	
	{
  	    ID3D11ShaderResourceView* NullSRVs[14] = {};
  	    Context->PSSetShaderResources(0, 14, NullSRVs);
  	}
	
  	if (Request.bDebugDraw
  	    && DebugBoxVS && DebugBoxPS
  	    && DebugBoxVertexBuffer && DebugBoxIndexBuffer
  	    && DebugBoxConstantBuffer && DebugBoxDepthState)
  	{
  	    Renderer.SetConstantBuffers();   // View / Projection CB 세팅
	
  	    Context->OMSetRenderTargets(1, &Targets.SceneColorRTV, Targets.SceneDepthDSV);
  	    Context->OMSetDepthStencilState(DebugBoxDepthState, 0);
  	    Context->OMSetBlendState(DebugBoxBlendState, nullptr, 0xFFFFFFFFu);
  	    Context->RSSetState(DebugBoxRasterizerState);
  	    DebugBoxVS->Bind(Context);
  	    DebugBoxPS->Bind(Context);
	
  	    UINT Stride = sizeof(FVertex), Offset = 0;
  	    Context->IASetVertexBuffers(0, 1, &DebugBoxVertexBuffer, &Stride, &Offset);
  	    Context->IASetIndexBuffer(DebugBoxIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
  	    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
  	    for (const FDecalRenderItem& Item : Request.Items)
  	    {
  	        if (!Item.IsValid()) continue;
	
  	        Renderer.UpdateObjectConstantBuffer(Item.DecalWorld);

  	        FDebugBoxMaterialCB MatCB;
  	        MatCB.BaseColorTint = FLinearColor(1.0f, 0.6f, 0.1f, 1.0f);
  	        MatCB.DecalExtents = Item.Extents;   
  	                                                                                                    
  	        D3D11_MAPPED_SUBRESOURCE Mapped = {};
  	        if (SUCCEEDED(Context->Map(DebugBoxConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
  	        {                                             
  	            memcpy(Mapped.pData, &MatCB, sizeof(MatCB));
  	            Context->Unmap(DebugBoxConstantBuffer, 0);
  	        }                                                 
  	                                                 
  	        ID3D11Buffer* CBs[1] = { DebugBoxConstantBuffer };
  	        Context->VSSetConstantBuffers(2, 1, CBs);      
  	     
  	        Context->DrawIndexed(DebugBoxIndexCount, 0, 0);
  	    }                                                               
  	 
  	    Context->OMSetRenderTargets(1, &Targets.SceneColorRTV, nullptr);
  	}                
	
	return bRendered;
}

FClusteredLookupDecalStats FDecalRenderFeature::GetClusteredStats() const
{
	FClusteredLookupDecalStats Stats;
	Stats.ClustersBuilt = LastFrameStats.ClusterCount;
	Stats.DecalCellRegistrations = LastFrameStats.TotalClusterIndices;
	Stats.MaxDecalsPerCell = LastFrameStats.MaxItemsPerCluster;
	Stats.AvgDecalsPerCell = LastFrameStats.ClusterCount > 0
		? static_cast<double>(LastFrameStats.TotalClusterIndices) / static_cast<double>(LastFrameStats.ClusterCount)
		: 0.0;
	return Stats;
}

bool FDecalRenderFeature::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();

	if (!Device) return false;
	
	if (!ClusterGlobalsConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.MiscFlags = 0;
		Desc.StructureByteStride = 0;
		Desc.ByteWidth = Align16(static_cast<uint32>(sizeof(FClusterGlobalsCB)));

		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &ClusterGlobalsConstantBuffer)))
		{
			return false;
		}
	}

	if (!CompositeConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth = Align16(static_cast<uint32>(sizeof(FCompositeCB)));

		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &CompositeConstantBuffer)))
		{
			return false;
		}
	}

	if (!CompositeBlendState)
	{
		D3D11_BLEND_DESC BlendDesc = {};
		BlendDesc.RenderTarget[0].BlendEnable = FALSE;
		BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(Device->CreateBlendState(&BlendDesc, &CompositeBlendState)))
		{
			return false;
		}
	}

	if (!CompositeDepthState)
	{
		D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
		DepthDesc.DepthEnable = FALSE;
		DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		DepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		if (FAILED(Device->CreateDepthStencilState(&DepthDesc, &CompositeDepthState)))
		{
			return false;
		}
	}

	if (!CompositeRasterizerState)
	{
		D3D11_RASTERIZER_DESC RasterDesc = {};
		RasterDesc.FillMode = D3D11_FILL_SOLID;
		RasterDesc.CullMode = D3D11_CULL_NONE;
		RasterDesc.DepthClipEnable = TRUE;
		if (FAILED(Device->CreateRasterizerState(&RasterDesc, &CompositeRasterizerState)))
		{
			return false;
		}
	}

	if (!LinearSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		SamplerDesc.MinLOD = 0.0f;
		SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(Device->CreateSamplerState(&SamplerDesc, &LinearSampler)))
		{
			return false;
		}
	}

	if (!PointSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		SamplerDesc.MinLOD = 0.0f;
		SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(Device->CreateSamplerState(&SamplerDesc, &PointSampler)))
		{
			return false;
		}
	}

	const std::wstring ShaderDir = FPaths::ShaderDir();
	if (!CompositeVS)
	{
		auto Resource = FShaderResource::GetOrCompile((ShaderDir + L"BlitVertexShader.hlsl").c_str(), "main", "vs_5_0");
		if (!Resource || FAILED(Device->CreateVertexShader(Resource->GetBufferPointer(), Resource->GetBufferSize(), nullptr, &CompositeVS)))
		{
			return false;
		}
	}

	if (!CompositePS)
	{
		auto Resource = FShaderResource::GetOrCompile((ShaderDir + L"DecalCompositePixelShader.hlsl").c_str(), "main", "ps_5_0");
		if (!Resource || FAILED(Device->CreatePixelShader(Resource->GetBufferPointer(), Resource->GetBufferSize(), nullptr, &CompositePS)))
		{
			return false;
		}
	}
	
	if (!DebugBoxPS || !DebugBoxVS)
	{
		const std::wstring ShaderDir = FPaths::ShaderDir();
		DebugBoxVS = FShaderMap::Get().GetOrCreateVertexShader(Device, (ShaderDir + L"DecalVertexShader.hlsl").c_str());
		DebugBoxPS = FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"DecalDebugPixelShader.hlsl").c_str());
		if (!DebugBoxPS || !DebugBoxVS) return false;
	}
	
  	if (!DebugBoxVertexBuffer)
  	{
  	    const FVector4 White(1,1,1,1);
  	    const FVector  NX(1,0,0);
  	    const FVertex Verts[] =
  	    {
  	        { FVector(-1,-1,-1), White, NX, FVector2(0,0) },
  	        { FVector( 1,-1,-1), White, NX, FVector2(1,0) },
  	        { FVector( 1, 1,-1), White, NX, FVector2(1,1) },
  	        { FVector(-1, 1,-1), White, NX, FVector2(0,1) },
  	        { FVector(-1,-1, 1), White, NX, FVector2(0,0) },
  	        { FVector( 1,-1, 1), White, NX, FVector2(1,0) },
  	        { FVector( 1, 1, 1), White, NX, FVector2(1,1) },
  	        { FVector(-1, 1, 1), White, NX, FVector2(0,1) },
  	    };
  	    const uint32 Idx[] = { 0,2,1, 0,3,2, 4,5,6, 4,6,7,
  	                           0,1,5, 0,5,4, 1,2,6, 1,6,5,
  	                           2,3,7, 2,7,6, 3,0,4, 3,4,7 };
	
  	    D3D11_BUFFER_DESC VBDesc = {};
  	    VBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  	    VBDesc.ByteWidth = sizeof(Verts);
  	    VBDesc.Usage     = D3D11_USAGE_IMMUTABLE;
  	    D3D11_SUBRESOURCE_DATA VBData = { Verts };
  	    if (FAILED(Device->CreateBuffer(&VBDesc, &VBData, &DebugBoxVertexBuffer))) return false;
	
  	    D3D11_BUFFER_DESC IBDesc = {};
  	    IBDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  	    IBDesc.ByteWidth = sizeof(Idx);
  	    IBDesc.Usage     = D3D11_USAGE_IMMUTABLE;
  	    D3D11_SUBRESOURCE_DATA IBData = { Idx };
  	    if (FAILED(Device->CreateBuffer(&IBDesc, &IBData, &DebugBoxIndexBuffer))) return false;
	
  	    DebugBoxIndexCount = static_cast<UINT>(sizeof(Idx) / sizeof(Idx[0]));
  	}
	
  	if (!DebugBoxConstantBuffer)
  	{
  	    D3D11_BUFFER_DESC Desc = {};
  	    Desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
  	    Desc.ByteWidth      = Align16(static_cast<uint32>(sizeof(FDebugBoxMaterialCB)));
  	    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  	    Desc.Usage          = D3D11_USAGE_DYNAMIC;
  	    if (FAILED(Device->CreateBuffer(&Desc, nullptr, &DebugBoxConstantBuffer))) return false;
  	}
	
  	if (!DebugBoxDepthState)
  	{
  	    D3D11_DEPTH_STENCIL_DESC D = {};
  	    D.DepthEnable    = TRUE;
  	    D.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  	    D.DepthFunc      = D3D11_COMPARISON_LESS;
  	    if (FAILED(Device->CreateDepthStencilState(&D, &DebugBoxDepthState))) return false;
  	}
	
  	if (!DebugBoxBlendState)
  	{
  	    D3D11_BLEND_DESC B = {};
  	    B.RenderTarget[0].BlendEnable           = TRUE;
  	    B.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
  	    B.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
  	    B.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
  	    B.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
  	    B.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
  	    B.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
  	    B.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  	    if (FAILED(Device->CreateBlendState(&B, &DebugBoxBlendState))) return false;
  	}
	
  	if (!DebugBoxRasterizerState)
  	{
  	    D3D11_RASTERIZER_DESC R = {};
  	    R.FillMode        = D3D11_FILL_SOLID;
  	    R.CullMode        = D3D11_CULL_NONE;
  	    R.DepthClipEnable = TRUE;
  	    if (FAILED(Device->CreateRasterizerState(&R, &DebugBoxRasterizerState))) return false;
  	}	
	bInitialized = true;
	return true;
}

bool FDecalRenderFeature::BuildFrameData(
	const FDecalRenderRequest& Request,
	FDecalPreparedViewData& OutPreparedData,
	FDecalFrameStats& OutFrameStats)
{
	const uint64 RequestSignature = ComputeDecalRequestSignature(Request);
	if (bClusterDataValid && RequestSignature == CachedRequestSignature)
	{
		return true;
	}

	CachedRequestSignature = RequestSignature;
	bClusterDataValid = true;

	OutPreparedData.DecalGPUItems.clear();
	OutPreparedData.ClusterHeaders.clear();
	OutPreparedData.ClusterIndexList.clear();
	OutPreparedData.VisibleSourceItems.clear();
	OutPreparedData.BuildStats = {};

	const FDecalClock::time_point VisibleBuildStartTime = FDecalClock::now();
	if (!BuildVisibleDecalData(Request, OutPreparedData)) return false;
	OutFrameStats.VisibleBuildTimeMs = ToMilliseconds(FDecalClock::now() - VisibleBuildStartTime);

	const FDecalClock::time_point ClusterBuildStartTime = FDecalClock::now();
	if (!BuildClusterLists(Request, OutPreparedData)) return false;
	OutFrameStats.ClusterBuildTimeMs = ToMilliseconds(FDecalClock::now() - ClusterBuildStartTime);

	OutFrameStats.InputItemCount = OutPreparedData.BuildStats.InputItemCount;
	OutFrameStats.VisibleItemCount = OutPreparedData.BuildStats.ValidItemCount;
	OutFrameStats.ClusterCount = OutPreparedData.BuildStats.ClusterCount;
	OutFrameStats.TotalClusterIndices = OutPreparedData.BuildStats.TotalClusterIndices;
	OutFrameStats.MaxItemsPerCluster = OutPreparedData.BuildStats.MaxItemsPerCluster;
	return true;
}

bool FDecalRenderFeature::BuildVisibleDecalData(
	const FDecalRenderRequest& Request,
	FDecalPreparedViewData& InOutPreparedData)
{
	InOutPreparedData.VisibleSourceItems.clear();
	InOutPreparedData.DecalGPUItems.clear();

	InOutPreparedData.BuildStats.InputItemCount = static_cast<uint32>(Request.Items.size());
	InOutPreparedData.BuildStats.ValidItemCount = 0;

	TArray<const FDecalRenderItem*> FilteredItems;
	FilteredItems.reserve(Request.Items.size());

	for (const FDecalRenderItem& Item : Request.Items)
	{
		if (!Item.IsValid())
		{
			continue;
		}

		if ((Item.ReceiverLayerMask & Request.ReceiverLayerMask) == 0)
		{
			continue;
		}

		FilteredItems.push_back(&Item);
	}

	if (Request.bSortByPriority)
	{
		std::sort(
			FilteredItems.begin(),
			FilteredItems.end(),
			[](const FDecalRenderItem* A, const FDecalRenderItem* B)
			{
				if (A->Priority != B->Priority)
				{
					return A->Priority > B->Priority;
				}
				return A->TextureIndex < B->TextureIndex;
			});
	}

	InOutPreparedData.VisibleSourceItems.reserve(FilteredItems.size());
	InOutPreparedData.DecalGPUItems.reserve(FilteredItems.size());

	for (const FDecalRenderItem* Item : FilteredItems)
	{
		InOutPreparedData.VisibleSourceItems.push_back(Item);

		FDecalGPUData GPUItem{};
		GPUItem.WorldToDecal = Item->WorldToDecal.GetTransposed();
		GPUItem.AtlasScaleBias = Item->AtlasScaleBias;
		GPUItem.BaseColorTint = Item->BaseColorTint;
		GPUItem.Extents = Item->Extents;
		GPUItem.TextureIndex = Item->TextureIndex;
		GPUItem.Flags = Item->Flags;
		GPUItem.NormalBlend = Item->NormalBlend;
		GPUItem.RoughnessBlend = Item->RoughnessBlend;
		GPUItem.EmissiveBlend = Item->EmissiveBlend;
		GPUItem.EdgeFade = Item->EdgeFade;
		GPUItem.AllowAngle = Item->AllowAngle;

		GPUItem.AxisXWS = FVector(
			Item->DecalWorld.M[0][0],
			Item->DecalWorld.M[0][1],
			Item->DecalWorld.M[0][2]).GetSafeNormal();

		GPUItem.AxisYWS = FVector(
			Item->DecalWorld.M[1][0],
			Item->DecalWorld.M[1][1],
			Item->DecalWorld.M[1][2]).GetSafeNormal();

		GPUItem.AxisZWS = FVector(
			Item->DecalWorld.M[2][0],
			Item->DecalWorld.M[2][1],
			Item->DecalWorld.M[2][2]).GetSafeNormal();

		InOutPreparedData.DecalGPUItems.push_back(GPUItem);
	}

	InOutPreparedData.BuildStats.ValidItemCount = static_cast<uint32>(InOutPreparedData.DecalGPUItems.size());
	return true;
}

bool FDecalRenderFeature::BuildClusterLists(
	const FDecalRenderRequest& Request,
	FDecalPreparedViewData& InOutPreparedData)
{
	InOutPreparedData.ClusterHeaders.clear();
	InOutPreparedData.ClusterIndexList.clear();

	const uint32 ClusterCount =
		Request.ClusterCountX * Request.ClusterCountY * Request.ClusterCountZ;

	InOutPreparedData.BuildStats.ClusterCount = ClusterCount;
	InOutPreparedData.BuildStats.TotalClusterIndices = 0;
	InOutPreparedData.BuildStats.MaxItemsPerCluster = 0;

	if (ClusterCount == 0 || InOutPreparedData.DecalGPUItems.empty() || InOutPreparedData.VisibleSourceItems.empty())
	{
		return true;
	}

	if (InOutPreparedData.DecalGPUItems.size() != InOutPreparedData.VisibleSourceItems.size())
	{
		return false;
	}

	InOutPreparedData.ClusterHeaders.resize(ClusterCount);

	CachedClusterRanges.resize(InOutPreparedData.VisibleSourceItems.size());
	CachedCount.assign(ClusterCount, 0);
	CachedWriteCursor.resize(ClusterCount);

	for (int32 DecalIndex = 0; DecalIndex < static_cast<int32>(InOutPreparedData.VisibleSourceItems.size()); ++DecalIndex)
	{
		FClusterRange Range;
		if (!ComputeDecalClusterRange(Request, *InOutPreparedData.VisibleSourceItems[DecalIndex], Range))
		{
			continue;
		}

		if (!Range.bValid)
		{
			continue;
		}

		CachedClusterRanges[DecalIndex] = Range;

		for (uint32 Z = Range.MinSliceZ; Z <= Range.MaxSliceZ; ++Z)
		{
			for (uint32 Y = Range.MinTileY; Y <= Range.MaxTileY; ++Y)
			{
				for (uint32 X = Range.MinTileX; X <= Range.MaxTileX; ++X)
				{
					const uint32 ClusterId = FlattenClusterIndex(X, Y, Z, Request);
					CachedCount[ClusterId]++;
				}
			}
		}
	}

	uint32 RunningOffset = 0;
	for (uint32 ClusterId = 0; ClusterId < ClusterCount; ++ClusterId)
	{
		const uint32 Count = CachedCount[ClusterId];

		InOutPreparedData.ClusterHeaders[ClusterId].Offset = RunningOffset;
		InOutPreparedData.ClusterHeaders[ClusterId].Count = Count;
		InOutPreparedData.ClusterHeaders[ClusterId].Pad0 = 0;
		InOutPreparedData.ClusterHeaders[ClusterId].Pad1 = 0;

		if (Count > InOutPreparedData.BuildStats.MaxItemsPerCluster)
		{
			InOutPreparedData.BuildStats.MaxItemsPerCluster = Count;
		}

		RunningOffset += Count;
	}

	InOutPreparedData.ClusterIndexList.resize(RunningOffset);
	InOutPreparedData.BuildStats.TotalClusterIndices = RunningOffset;

	if (RunningOffset == 0)
	{
		return true;
	}

	for (uint32 ClusterId = 0; ClusterId < ClusterCount; ++ClusterId)
	{
		CachedWriteCursor[ClusterId] = InOutPreparedData.ClusterHeaders[ClusterId].Offset;
	}

	for (int32 DecalIndex = 0; DecalIndex < static_cast<int32>(InOutPreparedData.VisibleSourceItems.size()); ++DecalIndex)
	{
		const FClusterRange& Range = CachedClusterRanges[DecalIndex];
		if (!Range.bValid)
		{
			continue;
		}

		for (uint32 Z = Range.MinSliceZ; Z <= Range.MaxSliceZ; ++Z)
		{
			for (uint32 Y = Range.MinTileY; Y <= Range.MaxTileY; ++Y)
			{
				for (uint32 X = Range.MinTileX; X <= Range.MaxTileX; ++X)
				{
					const uint32 ClusterId = FlattenClusterIndex(X, Y, Z, Request);

					if (Request.bClampClusterItemCount)
					{
						const uint32 LocalWritten =
							CachedWriteCursor[ClusterId] - InOutPreparedData.ClusterHeaders[ClusterId].Offset;

						if (LocalWritten >= Request.MaxClusterItems)
						{
							continue;
						}
					}

					const uint32 WriteIndex = CachedWriteCursor[ClusterId]++;
					InOutPreparedData.ClusterIndexList[WriteIndex] = static_cast<uint32>(DecalIndex);
				}
			}
		}
	}

	if (Request.bClampClusterItemCount)
	{
		for (uint32 ClusterId = 0; ClusterId < ClusterCount; ++ClusterId)
		{
			const uint32 WrittenCount = CachedWriteCursor[ClusterId] - InOutPreparedData.ClusterHeaders[ClusterId].Offset;
			InOutPreparedData.ClusterHeaders[ClusterId].Count = WrittenCount;
		}

		uint32 ValidTotalIndexCount = 0;
		for (uint32 ClusterId = 0; ClusterId < ClusterCount; ++ClusterId)
		{
			ValidTotalIndexCount += InOutPreparedData.ClusterHeaders[ClusterId].Count;
		}

		InOutPreparedData.BuildStats.TotalClusterIndices = ValidTotalIndexCount;
	}

	return true;
}

void FDecalRenderFeature::Release()
{
	// 슬롯에 남아 있을 수 있는 프레임 준비 상태 해제
	bInitialized = false;
	LastBuildStats = {};
	LastFrameStats = {};

	if (ClusterIndexStructuredBufferSRV)
	{
		ClusterIndexStructuredBufferSRV->Release();
		ClusterIndexStructuredBufferSRV = nullptr;
	}

	if (ClusterIndexStructuredBuffer)
	{
		ClusterIndexStructuredBuffer->Release();
		ClusterIndexStructuredBuffer = nullptr;
	}

	if (ClusterHeaderStructuredBufferSRV)
	{
		ClusterHeaderStructuredBufferSRV->Release();
		ClusterHeaderStructuredBufferSRV = nullptr;
	}

	if (ClusterHeaderStructuredBuffer)
	{
		ClusterHeaderStructuredBuffer->Release();
		ClusterHeaderStructuredBuffer = nullptr;
	}

	if (DecalStructuredBufferSRV)
	{
		DecalStructuredBufferSRV->Release();
		DecalStructuredBufferSRV = nullptr;
	}

	if (DecalStructuredBuffer)
	{
		DecalStructuredBuffer->Release();
		DecalStructuredBuffer = nullptr;
	}

	if (ClusterGlobalsConstantBuffer)
	{
		ClusterGlobalsConstantBuffer->Release();
		ClusterGlobalsConstantBuffer = nullptr;
	}

	if (CompositeConstantBuffer)
	{
		CompositeConstantBuffer->Release();
		CompositeConstantBuffer = nullptr;
	}

	if (CompositeBlendState)
	{
		CompositeBlendState->Release();
		CompositeBlendState = nullptr;
	}

	if (CompositeDepthState)
	{
		CompositeDepthState->Release();
		CompositeDepthState = nullptr;
	}

	if (CompositeRasterizerState)
	{
		CompositeRasterizerState->Release();
		CompositeRasterizerState = nullptr;
	}

	if (LinearSampler)
	{
		LinearSampler->Release();
		LinearSampler = nullptr;
	}

	if (PointSampler)
	{
		PointSampler->Release();
		PointSampler = nullptr;
	}

	if (CompositeVS)
	{
		CompositeVS->Release();
		CompositeVS = nullptr;
	}

	if (CompositePS)
	{
		CompositePS->Release();
		CompositePS = nullptr;
	}
	
	DebugBoxVS.reset();
	DebugBoxPS.reset();
	if (DebugBoxVertexBuffer)
	{
		DebugBoxVertexBuffer->Release();
		DebugBoxVertexBuffer = nullptr;
	}
	if (DebugBoxIndexBuffer)
	{
		DebugBoxIndexBuffer->Release();
		DebugBoxIndexBuffer = nullptr;
		DebugBoxIndexCount = 0;
	}
	if (DebugBoxConstantBuffer)
	{
		DebugBoxConstantBuffer->Release();
		DebugBoxConstantBuffer = nullptr;
	}
	if (DebugBoxBlendState)
	{
		DebugBoxBlendState->Release();
		DebugBoxBlendState = nullptr;
	}
	if (DebugBoxDepthState)
	{
		DebugBoxDepthState->Release();
		DebugBoxDepthState = nullptr;
	}
	if (DebugBoxRasterizerState)
	{
		DebugBoxRasterizerState->Release();
		DebugBoxRasterizerState = nullptr;
	}
}

bool FDecalRenderFeature::UpdateClusterGlobalsConstantBuffer(
	FRenderer& Renderer,
	const FDecalRenderRequest& Request,
	const FDecalPreparedViewData& PreparedData)
{
	if (!ClusterGlobalsConstantBuffer)
	{
		return false;
	}

	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
	if (!Context)
	{
		return false;
	}

	if (Request.ClusterCountX == 0 ||
		Request.ClusterCountY == 0 ||
		Request.ClusterCountZ == 0)
	{
		return false;
	}

	if (Request.ViewportWidth == 0 || Request.ViewportHeight == 0)
	{
		return false;
	}

	if (Request.NearZ <= 0.0f || Request.FarZ <= Request.NearZ)
	{
		return false;
	}

	FClusterGlobalsCB ClusterGlobalsCB{};
	ClusterGlobalsCB.ClusterCountX = Request.ClusterCountX;
	ClusterGlobalsCB.ClusterCountY = Request.ClusterCountY;
	ClusterGlobalsCB.ClusterCountZ = Request.ClusterCountZ;
	ClusterGlobalsCB.ViewportHeight = static_cast<float>(Request.ViewportHeight);
	ClusterGlobalsCB.ViewportWidth = static_cast<float>(Request.ViewportWidth);
	ClusterGlobalsCB.FarZ = Request.FarZ;
	ClusterGlobalsCB.NearZ = Request.NearZ;
	ClusterGlobalsCB.TileWidth = static_cast<float>(Request.ViewportWidth) / static_cast<float>(Request.ClusterCountX);
	ClusterGlobalsCB.TileHeight = static_cast<float>(Request.ViewportHeight) / static_cast<float>(Request.ClusterCountY);
	ClusterGlobalsCB.LogZScale = static_cast<float>(Request.ClusterCountZ) / std::log(Request.FarZ / Request.NearZ);
	ClusterGlobalsCB.LogZBias = -std::log(Request.NearZ) * ClusterGlobalsCB.LogZScale;
	ClusterGlobalsCB.MaxClusterItems = Request.bClampClusterItemCount ? Request.MaxClusterItems : PreparedData.BuildStats.MaxItemsPerCluster;

	D3D11_MAPPED_SUBRESOURCE Mapped{};
	if (FAILED(Context->Map(ClusterGlobalsConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}

	std::memcpy(Mapped.pData, &ClusterGlobalsCB, sizeof(FClusterGlobalsCB));
	Context->Unmap(ClusterGlobalsConstantBuffer, 0);

	return true;
}

bool FDecalRenderFeature::UploadDecalStructuredBuffer(FRenderer& Renderer, const FDecalPreparedViewData& PreparedData)
{
	ID3D11Device* Device = Renderer.GetDevice();
	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();

	if (!Device || !Context) return false;

	if (PreparedData.DecalGPUItems.empty()) return false;

	const uint32 ElementCount = static_cast<uint32>(PreparedData.DecalGPUItems.size());
	const uint32 ElementSize = static_cast<uint32>(sizeof(FDecalGPUData));
	const uint32 RequiredByteWidth = ElementCount * ElementSize;

	bool bNeedRecreate = false;

	if (!DecalStructuredBuffer || !DecalStructuredBufferSRV) bNeedRecreate = true;
	else
	{
		D3D11_BUFFER_DESC ExistingDesc{};
		DecalStructuredBuffer->GetDesc(&ExistingDesc);

		if (ExistingDesc.ByteWidth < RequiredByteWidth ||
			ExistingDesc.StructureByteStride != ElementSize ||
			ExistingDesc.Usage != D3D11_USAGE_DYNAMIC ||
			ExistingDesc.BindFlags != D3D11_BIND_SHADER_RESOURCE ||
			ExistingDesc.CPUAccessFlags != D3D11_CPU_ACCESS_WRITE ||
			(ExistingDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) == 0)
		{
			bNeedRecreate = true;
		}
	}

	if (bNeedRecreate)
	{
		if (DecalStructuredBufferSRV)
		{
			DecalStructuredBufferSRV->Release();
			DecalStructuredBufferSRV = nullptr;
		}

		if (DecalStructuredBuffer)
		{
			DecalStructuredBuffer->Release();
			DecalStructuredBuffer = nullptr;
		}

		D3D11_BUFFER_DESC BufferDesc{};
		BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		BufferDesc.StructureByteStride = ElementSize;
		BufferDesc.ByteWidth = RequiredByteWidth;

		if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, &DecalStructuredBuffer)))
		{
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = ElementCount;

		if (FAILED(Device->CreateShaderResourceView(DecalStructuredBuffer, &SRVDesc, &DecalStructuredBufferSRV)))
		{
			DecalStructuredBuffer->Release();
			DecalStructuredBuffer = nullptr;
			return false;
		}
	}

	D3D11_MAPPED_SUBRESOURCE Mapped{};
	if (FAILED(Context->Map(DecalStructuredBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}

	std::memcpy(Mapped.pData, PreparedData.DecalGPUItems.data(), RequiredByteWidth);
	Context->Unmap(DecalStructuredBuffer, 0);

	return true;
}

bool FDecalRenderFeature::UploadClusterHeaderStructuredBuffer(FRenderer& Renderer, const FDecalPreparedViewData& PreparedData)
{
	ID3D11Device* Device = Renderer.GetDevice();
	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();

	if (!Device || !Context)
	{
		return false;
	}

	if (PreparedData.ClusterHeaders.empty())
	{
		return false;
	}

	const uint32 ElementCount = static_cast<uint32>(PreparedData.ClusterHeaders.size());
	const uint32 ElementSize = static_cast<uint32>(sizeof(FDecalClusterHeaderGPU));
	const uint32 RequiredByteWidth = ElementCount * ElementSize;

	bool bNeedRecreate = false;

	if (!ClusterHeaderStructuredBuffer || !ClusterHeaderStructuredBufferSRV)
	{
		bNeedRecreate = true;
	}
	else
	{
		D3D11_BUFFER_DESC ExistingDesc{};
		ClusterHeaderStructuredBuffer->GetDesc(&ExistingDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC ExistingSRVDesc{};
		ClusterHeaderStructuredBufferSRV->GetDesc(&ExistingSRVDesc);

		if (ExistingDesc.ByteWidth < RequiredByteWidth ||
			ExistingDesc.StructureByteStride != ElementSize ||
			ExistingDesc.Usage != D3D11_USAGE_DYNAMIC ||
			ExistingDesc.BindFlags != D3D11_BIND_SHADER_RESOURCE ||
			ExistingDesc.CPUAccessFlags != D3D11_CPU_ACCESS_WRITE ||
			(ExistingDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) == 0 ||
			ExistingSRVDesc.ViewDimension != D3D11_SRV_DIMENSION_BUFFER ||
			ExistingSRVDesc.Buffer.NumElements < ElementCount)
		{
			bNeedRecreate = true;
		}
	}

	if (bNeedRecreate)
	{
		if (ClusterHeaderStructuredBufferSRV)
		{
			ClusterHeaderStructuredBufferSRV->Release();
			ClusterHeaderStructuredBufferSRV = nullptr;
		}

		if (ClusterHeaderStructuredBuffer)
		{
			ClusterHeaderStructuredBuffer->Release();
			ClusterHeaderStructuredBuffer = nullptr;
		}

		D3D11_BUFFER_DESC BufferDesc{};
		BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		BufferDesc.StructureByteStride = ElementSize;
		BufferDesc.ByteWidth = RequiredByteWidth;

		if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, &ClusterHeaderStructuredBuffer)))
		{
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = ElementCount;

		if (FAILED(Device->CreateShaderResourceView(
			ClusterHeaderStructuredBuffer,
			&SRVDesc,
			&ClusterHeaderStructuredBufferSRV)))
		{
			ClusterHeaderStructuredBuffer->Release();
			ClusterHeaderStructuredBuffer = nullptr;
			return false;
		}
	}

	D3D11_MAPPED_SUBRESOURCE Mapped{};
	if (FAILED(Context->Map(ClusterHeaderStructuredBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}

	std::memcpy(Mapped.pData, PreparedData.ClusterHeaders.data(), RequiredByteWidth);
	Context->Unmap(ClusterHeaderStructuredBuffer, 0);

	return true;
}

bool FDecalRenderFeature::UploadClusterIndexStructuredBuffer(FRenderer& Renderer, const FDecalPreparedViewData& PreparedData)
{
	ID3D11Device* Device = Renderer.GetDevice();
	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();

	if (!Device || !Context)
	{
		return false;
	}

	if (PreparedData.ClusterIndexList.empty())
	{
		return false;
	}

	const uint32 ElementCount = static_cast<uint32>(PreparedData.ClusterIndexList.size());
	const uint32 ElementSize = static_cast<uint32>(sizeof(uint32));
	const uint32 RequiredByteWidth = ElementCount * ElementSize;

	bool bNeedRecreate = false;

	if (!ClusterIndexStructuredBuffer || !ClusterIndexStructuredBufferSRV)
	{
		bNeedRecreate = true;
	}
	else
	{
		D3D11_BUFFER_DESC ExistingDesc{};
		ClusterIndexStructuredBuffer->GetDesc(&ExistingDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC ExistingSRVDesc{};
		ClusterIndexStructuredBufferSRV->GetDesc(&ExistingSRVDesc);

		if (ExistingDesc.ByteWidth < RequiredByteWidth ||
			ExistingDesc.StructureByteStride != ElementSize ||
			ExistingDesc.Usage != D3D11_USAGE_DYNAMIC ||
			ExistingDesc.BindFlags != D3D11_BIND_SHADER_RESOURCE ||
			ExistingDesc.CPUAccessFlags != D3D11_CPU_ACCESS_WRITE ||
			(ExistingDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) == 0 ||
			ExistingSRVDesc.ViewDimension != D3D11_SRV_DIMENSION_BUFFER ||
			ExistingSRVDesc.Buffer.NumElements < ElementCount)
		{
			bNeedRecreate = true;
		}
	}

	if (bNeedRecreate)
	{
		if (ClusterIndexStructuredBufferSRV)
		{
			ClusterIndexStructuredBufferSRV->Release();
			ClusterIndexStructuredBufferSRV = nullptr;
		}

		if (ClusterIndexStructuredBuffer)
		{
			ClusterIndexStructuredBuffer->Release();
			ClusterIndexStructuredBuffer = nullptr;
		}

		D3D11_BUFFER_DESC BufferDesc{};
		BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		BufferDesc.StructureByteStride = ElementSize;
		BufferDesc.ByteWidth = RequiredByteWidth;

		if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, &ClusterIndexStructuredBuffer)))
		{
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = ElementCount;

		if (FAILED(Device->CreateShaderResourceView(
			ClusterIndexStructuredBuffer,
			&SRVDesc,
			&ClusterIndexStructuredBufferSRV)))
		{
			ClusterIndexStructuredBuffer->Release();
			ClusterIndexStructuredBuffer = nullptr;
			return false;
		}
	}

	D3D11_MAPPED_SUBRESOURCE Mapped{};
	if (FAILED(Context->Map(ClusterIndexStructuredBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}

	std::memcpy(Mapped.pData, PreparedData.ClusterIndexList.data(), RequiredByteWidth);
	Context->Unmap(ClusterIndexStructuredBuffer, 0);
	return true;
}
