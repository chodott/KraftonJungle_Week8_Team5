#pragma once

#include "CoreMinimal.h"
#include "Renderer/LinearColor.h"

#include <d3d11.h>
#include <chrono>

class FRenderer;

enum ENGINE_API EDecalRenderFlags : uint32
{
	DECAL_RENDER_FLAG_None = 0,
	DECAL_RENDER_FLAG_BaseColor = 1u << 0,
	DECAL_RENDER_FLAG_Normal = 1u << 1,
	DECAL_RENDER_FLAG_Roughness = 1u << 2,
	DECAL_RENDER_FLAG_Emissive = 1u << 3,
};

struct ENGINE_API FDecalRenderItem
{
	// Local -> World
	FMatrix DecalWorld = FMatrix::Identity;

	// World -> Local
	FMatrix WorldToDecal = FMatrix::Identity;

	// OBB half extents in decal local space
	FVector Extents = FVector(50.0f, 50.0f, 50.0f);

	// Texture array / atlas index
	uint32 TextureIndex = 0;

	// EDecalRenderFlags bitmask
	uint32 Flags = DECAL_RENDER_FLAG_BaseColor;

	// Optional filtering / sorting
	uint32 Priority = 0;
	uint32 ReceiverLayerMask = 0xFFFFFFFF;

	// Atlas UV transform: uv * AtlasScaleBias.xy + AtlasScaleBias.zw
	FVector4 AtlasScaleBias = FVector4(1, 1, 0, 0);

	// Multiplier / tint
	FLinearColor BaseColorTint = FLinearColor::White;

	float NormalBlend = 1.0f;
	float RoughnessBlend = 1.0f;
	float EmissiveBlend = 1.0f;
	float EdgeFade = 2.0f;

	bool IsValid() const
	{
		return Extents.X > 0.0f && Extents.Y > 0.0f && Extents.Z > 0.0f;
	}
};

struct ENGINE_API FDecalClusterHeaderGPU
{
	uint32 Offset = 0;
	uint32 Count = 0;
	uint32 Pad0 = 0;
	uint32 Pad1 = 0;
};

struct ENGINE_API FDecalGPUData
{
	FMatrix WorldToDecal = FMatrix::Identity;

	FVector4 AtlasScaleBias = FVector4(1, 1, 0, 0);
	FLinearColor BaseColorTint = FLinearColor::White;

	FVector Extents = FVector(50.0f, 50.0f, 50.0f);
	uint32 TextureIndex = 0;

	FVector AxisXWS = FVector(1, 0, 0);
	uint32 Flags = DECAL_RENDER_FLAG_BaseColor;

	FVector AxisYWS = FVector(0, 1, 0);
	float NormalBlend = 1.0f;

	FVector AxisZWS = FVector(0, 0, 1);
	float RoughnessBlend = 1.0f;

	float EmissiveBlend = 1.0f;
	float EdgeFade = 2.0f;
	uint32 PadA = 0;
	uint32 PadB = 0;
};

struct ENGINE_API FDecalRenderRequest
{
	// Candidate decals for this frame.
	// Broad phase visibility filtering can be done before filling this array.
	TArray<FDecalRenderItem> Items;

	// Camera / view
	FMatrix View = FMatrix::Identity;
	FMatrix Projection = FMatrix::Identity;
	FMatrix ViewProjection = FMatrix::Identity;
	FMatrix InverseViewProjection = FMatrix::Identity;

	FVector CameraPosition = FVector::ZeroVector;

	// Viewport
	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;

	// Projection range
	float NearZ = 1.0f;
	float FarZ = 10000.0f;

	// Cluster resolution
	uint32 ClusterCountX = 16;
	uint32 ClusterCountY = 9;
	uint32 ClusterCountZ = 24;

	// Optional filtering for the current receiver pass
	uint32 ReceiverLayerMask = 0xFFFFFFFF;

	// Shared decal resources for the frame
	ID3D11ShaderResourceView* BaseColorTextureArraySRV = nullptr;
	ID3D11ShaderResourceView* NormalTextureArraySRV = nullptr;
	ID3D11ShaderResourceView* ORMTextureArraySRV = nullptr;

	bool bEnabled = true;
	bool bSortByPriority = true;
	bool bClampClusterItemCount = true;
	uint32 MaxClusterItems = 64;

	bool IsEmpty() const
	{
		return !bEnabled
			|| ViewportWidth == 0
			|| ViewportHeight == 0
			|| Items.empty();
	}
};

struct ENGINE_API FDecalClusterBuildStats
{
	uint32 InputItemCount = 0;
	uint32 ValidItemCount = 0;
	uint32 ClusterCount = 0;
	uint32 TotalClusterIndices = 0;
	uint32 MaxItemsPerCluster = 0;
};

struct ENGINE_API FDecalFrameStats
{
	uint32 InputItemCount = 0;
	uint32 VisibleItemCount = 0;
	uint32 ClusterCount = 0;
	uint32 TotalClusterIndices = 0;
	uint32 MaxItemsPerCluster = 0;
	uint32 UploadedDecalCount = 0;
	uint32 UploadedClusterHeaderCount = 0;
	uint32 UploadedClusterIndexCount = 0;

	double PrepareTimeMs = 0.0;
	double VisibleBuildTimeMs = 0.0;
	double ClusterBuildTimeMs = 0.0;
	double ConstantBufferUpdateTimeMs = 0.0;
	double UploadDecalBufferTimeMs = 0.0;
	double UploadClusterHeaderBufferTimeMs = 0.0;
	double UploadClusterIndexBufferTimeMs = 0.0;
};

class ENGINE_API FDecalRenderFeature
{
public:
	~FDecalRenderFeature();

	// Build per-frame clustered decal data and upload GPU buffers.
	bool Prepare(FRenderer& Renderer, const FDecalRenderRequest& Request);

	// Bind SRVs / constant buffer for the forward opaque pass.
	void BindForForwardPass(FRenderer& Renderer);

	// Optional explicit unbind for safety when another pass reuses the same slots.
	void Unbind(FRenderer& Renderer);

	void Release();

	const FDecalClusterBuildStats& GetBuildStats() const
	{
		return BuildStats;
	}

	const FDecalFrameStats& GetFrameStats() const
	{
		return FrameStats;
	}

	bool IsReady() const
	{
		return bInitialized && bFramePrepared;
	}

	bool Initialize(FRenderer& Renderer);
private:

	bool BuildFrameData(const FDecalRenderRequest& Request);
	bool BuildVisibleDecalData(const FDecalRenderRequest& Request);
	bool BuildClusterLists(const FDecalRenderRequest& Request);

	bool UpdateClusterGlobalsConstantBuffer(FRenderer& Renderer, const FDecalRenderRequest& Request);
	bool UploadDecalStructuredBuffer(FRenderer& Renderer);
	bool UploadClusterHeaderStructuredBuffer(FRenderer& Renderer);
	bool UploadClusterIndexStructuredBuffer(FRenderer& Renderer);

	void ResetFrameData();

private:
	bool bInitialized = false;
	bool bFramePrepared = false;

	// CPU-side per-frame data
	TArray<FDecalGPUData> DecalGPUItems;
	TArray<FDecalClusterHeaderGPU> ClusterHeaders;
	TArray<uint32> ClusterIndexList;
	TArray<const FDecalRenderItem*> VisibleSourceItems;

	FDecalClusterBuildStats BuildStats;
	FDecalFrameStats FrameStats;

	// Cached frame request data needed for binding
	ID3D11ShaderResourceView* BaseColorTextureArraySRV = nullptr;
	ID3D11ShaderResourceView* NormalTextureArraySRV = nullptr;
	ID3D11ShaderResourceView* ORMTextureArraySRV = nullptr;

	// GPU resources
	ID3D11Buffer* ClusterGlobalsConstantBuffer = nullptr;

	ID3D11Buffer* DecalStructuredBuffer = nullptr;
	ID3D11ShaderResourceView* DecalStructuredBufferSRV = nullptr;

	ID3D11Buffer* ClusterHeaderStructuredBuffer = nullptr;
	ID3D11ShaderResourceView* ClusterHeaderStructuredBufferSRV = nullptr;

	ID3D11Buffer* ClusterIndexStructuredBuffer = nullptr;
	ID3D11ShaderResourceView* ClusterIndexStructuredBufferSRV = nullptr;
};
