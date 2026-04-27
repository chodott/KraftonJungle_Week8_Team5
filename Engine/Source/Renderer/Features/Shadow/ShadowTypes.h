#pragma once

#include <d3d11.h>

#include "CoreMinimal.h"

namespace ShadowConfig
{
	static constexpr uint32 MaxShadowLights = 16;


	static constexpr uint32 MaxSpotShadowViews		   = 8;
	static constexpr uint32 MaxPointShadowCubes        = 4;
	static constexpr uint32 MaxShadowViews = MaxSpotShadowViews + MaxPointShadowCubes * 6;

	static constexpr uint32 PointShadowSliceOffset = MaxSpotShadowViews;
	static constexpr uint32 DefaultShadowMapResolution = 512;
	static constexpr uint32 MinShadowMapResolution     = 64;
	static constexpr uint32 MaxShadowMapResolution     = 4096;
	static constexpr float  DefaultNearZ               = 0.05f;
}

namespace ShadowSlots
{
	static constexpr uint32 ShadowLightSRV      = 20;
	static constexpr uint32 ShadowViewSRV       = 21;
	static constexpr uint32 ShadowMapSRV        = 22;
	static constexpr uint32 ShadowMomentsSRV    = 23;
	static constexpr uint32 ShadowCubeSRV		= 24;
	static constexpr uint32 ShadowMomentCubeSRV = 25;

	static constexpr uint32 ShadowSampler       = 8;
	static constexpr uint32 ShadowLinearSampler = 9;
}

enum class EShadowFilterMode : uint32
{
	Raw = 0u,
	PCF = 1u,
	VSM = 2u,
};

enum class EShadowLightType : uint32
{
	Directional = 0,
	Spot        = 1,
	Point       = 2,
	Count
};

enum class EShadowProjectionType : uint32
{
	Orthographic = 0,
	Perspective  = 1,
};

struct FShadowLightRenderItem
{
	EShadowLightType LightType        = EShadowLightType::Spot;
	uint32           SourceLightIndex = UINT32_MAX;

	uint32 ShadowIndex = UINT32_MAX;

	uint32 FirstViewIndex = UINT32_MAX;
	uint32 ViewCount      = 0;

	float Bias       = 0.001f;
	float SlopeBias  = 0.001f;
	float NormalBias = 0.0f;
	float Sharpen    = 0.0f;

	uint32 CubeArrayIndex = UINT32_MAX;

	FVector PositionWS  = FVector::ZeroVector;
	FVector DirectionWS = FVector(1.0f, 0.0f, 0.0f);

	FVector4 Params0 = FVector4(0, 0, 0, 0);
	FVector4 Params1 = FVector4(0, 0, 0, 0);
};

struct FShadowViewRenderItem
{
	uint32 ShadowLightIndex = UINT32_MAX;
	uint32 ArraySlice       = UINT32_MAX;

	EShadowProjectionType ProjectionType = EShadowProjectionType::Perspective;

	EShadowLightType LightType = EShadowLightType::Spot;

	FMatrix View           = FMatrix::Identity;
	FMatrix Projection     = FMatrix::Identity;
	FMatrix ViewProjection = FMatrix::Identity;

	FVector PositionWS = FVector::ZeroVector;

	float NearZ = ShadowConfig::DefaultNearZ;
	float FarZ  = 1000.0f;

	uint32            RequestedResolution = 0;
	EShadowFilterMode FilterMode          = EShadowFilterMode::VSM;

	FVector AtlasUV = FVector::ZeroVector;

	D3D11_VIEWPORT Viewport = {};
};


struct FShadowLightGPU
{
	uint32 LightType;
	uint32 FirstViewIndex;
	uint32 ViewCount;
	uint32 Flags;

	FVector4 PositionType;

	FVector4 DirectionBias;

	FVector4 Params0;
};


struct FShadowViewGPU
{
	FMatrix LightViewProjection;

	uint32 ArraySlice;
	uint32 ProjectionType;
	uint32 FilterMode;
	uint32 Pad0;

	FVector4 ViewParams;
	
	FVector AtlasUV; // X,Y: UV offset, Z: UV scale
	float   Pad1;
};
