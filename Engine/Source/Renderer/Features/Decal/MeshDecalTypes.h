#pragma once

#include "CoreMinimal.h"
#include "Math/LinearColor.h"
#include "Renderer/Features/Decal/DecalTypes.h"

#include <cstdint>

class UPrimitiveComponent;

struct ENGINE_API FMeshDecalRenderItem
{
	FMatrix DecalWorld = FMatrix::Identity;
	FMatrix WorldToDecal = FMatrix::Identity;
	FVector Extents = FVector(50.0f, 50.0f, 50.0f);

	std::wstring TexturePath;
	uint32 TextureIndex = 0;
	uint32 Flags = DECAL_RENDER_FLAG_BaseColor;
	FVector4 AtlasScaleBias = FVector4(1, 1, 0, 0);
	FLinearColor BaseColorTint = FLinearColor::White;

	uint32 Priority = 0;
	uint32 ReceiverLayerMask = 0xFFFFFFFFu;
	float NormalBlend = 1.0f;
	float RoughnessBlend = 1.0f;
	float EmissiveBlend = 1.0f;
	float EdgeFade = 2.0f;
	float AllowAngle = 0.0f;
	bool bIsFading = false;
	float SurfaceOffset = 0.002f;

	uint64 SourceComponentId = 0;
	uint32 VisibleRevision = 1;
	uint32 ClusterRevision = 1;

	bool IsValid() const
	{
		return Extents.X > 0.0f && Extents.Y > 0.0f && Extents.Z > 0.0f;
	}
};

struct ENGINE_API FMeshDecalReceiverCandidate
{
	UPrimitiveComponent* Component = nullptr;
	FMatrix World = FMatrix::Identity;
	FBoxSphereBounds WorldBounds;
	uint64 SourceComponentId = 0;
	uint32 ReceiverLayerMask = 0xFFFFFFFFu;
};

struct ENGINE_API FMeshDecalBuildStats
{
	uint32 InputDecalCount = 0;
	uint32 ReceiverCandidateCount = 0;
	uint32 CoarseIntersectPairCount = 0;
	uint32 ClippedTriangleCount = 0;
};
