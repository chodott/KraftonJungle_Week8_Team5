#pragma once

#include "CoreMinimal.h"
#include "Math/LinearColor.h"

#include <cstdint>
#include <d3d11.h>

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
	FMatrix DecalWorld = FMatrix::Identity;
	FMatrix WorldToDecal = FMatrix::Identity;
	FVector Extents = FVector(50.0f, 50.0f, 50.0f);
	uint32 TextureIndex = 0;
	std::wstring TexturePath;
	uint32 Flags = DECAL_RENDER_FLAG_BaseColor;
	uint32 Priority = 0;
	uint32 ReceiverLayerMask = 0xFFFFFFFF;
	FVector4 AtlasScaleBias = FVector4(1, 1, 0, 0);
	FLinearColor BaseColorTint = FLinearColor::White;
	float NormalBlend = 1.0f;
	float RoughnessBlend = 1.0f;
	float EmissiveBlend = 1.0f;
	float EdgeFade = 2.0f;
	bool bIsFading = false;
	float AllowAngle = 0.0f;
	uint64 SourceComponentId = 0;
	uint32 VisibleRevision = 1;
	uint32 ClusterRevision = 1;

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
	float AllowAngle = 0.0f;
};

enum class EDecalDirtyFlags : uint32
{
	None = 0,
	VisibleData = 1u << 0,
	ClusterData = 1u << 1,
	ForceRebuild = 1u << 2,
};

inline EDecalDirtyFlags operator|(EDecalDirtyFlags A, EDecalDirtyFlags B)
{
	return static_cast<EDecalDirtyFlags>(static_cast<uint32>(A) | static_cast<uint32>(B));
}

inline EDecalDirtyFlags operator&(EDecalDirtyFlags A, EDecalDirtyFlags B)
{
	return static_cast<EDecalDirtyFlags>(static_cast<uint32>(A) & static_cast<uint32>(B));
}

inline EDecalDirtyFlags& operator|=(EDecalDirtyFlags& A, EDecalDirtyFlags B)
{
	A = A | B;
	return A;
}

inline bool HasDecalDirtyFlag(EDecalDirtyFlags Value, EDecalDirtyFlags Flag)
{
	return (static_cast<uint32>(Value) & static_cast<uint32>(Flag)) != 0;
}

struct ENGINE_API FDecalRenderRequest
{
	TArray<FDecalRenderItem> Items;
	FMatrix View = FMatrix::Identity;
	FMatrix Projection = FMatrix::Identity;
	FMatrix ViewProjection = FMatrix::Identity;
	FMatrix InverseViewProjection = FMatrix::Identity;
	FVector CameraPosition = FVector::ZeroVector;
	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;
	float NearZ = 0.1f;
	float FarZ = 1000.0f;
	uint32 ClusterCountX = 16;
	uint32 ClusterCountY = 9;
	uint32 ClusterCountZ = 24;
	uint32 ReceiverLayerMask = 0xFFFFFFFF;
	ID3D11ShaderResourceView* BaseColorTextureArraySRV = nullptr;
	uint32 CandidateReceiverObjectCount = 0;
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
	
	bool bDebugDraw = false;
	EDecalDirtyFlags DirtyFlags = EDecalDirtyFlags::VisibleData | EDecalDirtyFlags::ClusterData;
	uint64 VisibleSignature = 0;
	uint64 ClusterSignature = 0;
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
	uint32 FadeInOutCount = 0;

	double PrepareTimeMs = 0.0;
	double VisibleBuildTimeMs = 0.0;
	double ClusterBuildTimeMs = 0.0;
	double ConstantBufferUpdateTimeMs = 0.0;
	double UploadDecalBufferTimeMs = 0.0;
	double UploadClusterHeaderBufferTimeMs = 0.0;
	double UploadClusterIndexBufferTimeMs = 0.0;
	double ShadingPassTimeMs = 0.0;
	double TotalDecalTimeMs = 0.0;
};

struct ENGINE_API FDecalPreparedViewData
{
	TArray<FDecalGPUData> DecalGPUItems;
	TArray<FDecalClusterHeaderGPU> ClusterHeaders;
	TArray<uint32> ClusterIndexList;
	TArray<FDecalRenderItem> VisibleSourceItems;

	FDecalClusterBuildStats BuildStats;

	ID3D11ShaderResourceView* BaseColorTextureArraySRV = nullptr;
};
