#pragma once

#include "CoreMinimal.h"

namespace LightCullingConfig
{
	static constexpr uint32 TileSizeX     = 16;
	static constexpr uint32 TileSizeY     = 16;
	static constexpr uint32 ClusterCountZ = 24;
}

namespace LightListConfig
{
	static constexpr uint32 MaxLocalLights          = 1024;
	static constexpr uint32 MaxLightsPerCluster     = 1024;
	static constexpr uint32 HeatmapVisualizationMax = 16;

	static constexpr uint32 MaxShadowCastingLights	= 8;
}

namespace LightClusterSlots
{
	static constexpr uint32 GlobalLightCB   = 4;
	static constexpr uint32 ClusterGlobalCB = 8;

	static constexpr uint32 ClusterLightHeaderSRV = 10;
	static constexpr uint32 ClusterLightIndexSRV  = 11;
	static constexpr uint32 LocalLightSRV         = 12;
	static constexpr uint32 ObjectLightIndexSRV   = 13;
}

enum class ELightClass : uint32
{
	Point  = 0,
	Spot   = 1,
	Rect   = 2,
	Tube   = 3,
	Custom = 4,
};

enum class ECullShapeType : uint32
{
	Sphere  = 0,
	Cone    = 1,
	OBB     = 2,
	Capsule = 3,
	Custom  = 4,
};

struct FAmbientLightInfo
{
	FVector4 ColorIntensity; // xyz=color, w=intensity
};

struct FDirectionalLightInfo
{
	FVector4 ColorIntensity; // xyz=color, w=intensity
	FVector4 DirectionEtc;   // xyz=direction, w=reserved
};

struct FGlobalLightConstantBuffer
{
	FAmbientLightInfo     Ambient;
	FDirectionalLightInfo Directional;

	uint32 AmbientEnabled        = 1;
	uint32 DirectionalLightCount = 0;
	uint32 Pad0                  = 0;
	uint32 Pad1                  = 0;
};

struct FLocalLightGPU
{
	FVector4 ColorIntensity; // xyz=color, w=intensity
	FVector4 PositionRange;  // xyz=positionWS, w=range
	FVector4 DirectionType;  // xyz=directionWS, w=ELightClass
	FVector4 AngleParams;    // x=innerCos, y=outerCos, z=param0, w=param1

	FVector4 Axis0Extent; // xyz=axis0, w=extent0
	FVector4 Axis1Extent; // xyz=axis1, w=extent1
	FVector4 Axis2Extent; // xyz=axis2, w=extent2

	uint32 Flags       = 0;
	uint32 ShadowIndex = UINT32_MAX;
	uint32 CookieIndex = UINT32_MAX;
	uint32 IESIndex    = UINT32_MAX;

	FMatrix ShadowViewProj = FMatrix::Identity;
};

struct FLightCullProxyGPU
{
	FVector4 CullCenterRadius; // xyz=centerWS, w=radius

	FVector4 PositionRange;
	FVector4 DirectionType;
	FVector4 AngleParams;

	FVector4 Axis0Extent;
	FVector4 Axis1Extent;
	FVector4 Axis2Extent;

	uint32 Flags         = 0;
	uint32 LightIndex    = 0;
	uint32 CullShapeType = 0; // ECullShapeType
	uint32 Reserved      = 0;
};

struct FLightClusterHeaderGPU
{
	uint32 Offset = 0;
	uint32 Count  = 0;
	uint32 Pad0   = 0; // raw unclamped count
	uint32 Pad1   = 0;
};

struct FTileDepthBoundsGPU
{
	float  MinViewZ     = 0.0f;
	float  MaxViewZ     = 0.0f;
	uint32 TileMinSlice = 0;
	uint32 TileMaxSlice = 0;
	uint32 HasGeometry  = 0;
	uint32 Pad0         = 0;
	uint32 Pad1         = 0;
	uint32 Pad2         = 0;
};

struct FLightClusterGlobalCB
{
	FMatrix View              = FMatrix::Identity;
	FMatrix Projection        = FMatrix::Identity;
	FMatrix InverseProjection = FMatrix::Identity;
	FMatrix InverseView       = FMatrix::Identity;

	FVector4 ClusterCameraPosition = FVector4(FVector::ZeroVector, 1.0f);
	FVector4 ScreenParams          = FVector4(0, 0, 0, 0); // width, height, 1/w, 1/h

	uint32 ClusterCountX   = 0;
	uint32 ClusterCountY   = 0;
	uint32 ClusterCountZ   = 0;
	uint32 LocalLightCount = 0;

	uint32 bOrthographic         = 0;
	uint32 MaxLightsPerCluster   = LightListConfig::MaxLightsPerCluster;
	uint32 LightingEnabled       = 1;
	uint32 VisualizationMode     = 0;

	float NearZ     = 0.1f;
	float FarZ      = 1000.0f;
	float LogZScale = 0.0f;
	float LogZBias  = 0.0f;
};
