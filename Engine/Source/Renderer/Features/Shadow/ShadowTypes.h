#pragma once
#include "CoreMinimal.h"

enum class EShadowType : uint8
{
	HardShadow,
	PCFSoftShadow,
	VSM,
	CSM
};


struct FShadowRenderItem
{

	FMatrix ViewMatrix = FMatrix::Identity;
	FMatrix ProjectionMatrix = FMatrix::Identity;
	FMatrix ViewProjectionMatrix = FMatrix::Identity;


	FVector LightPositionWS = FVector::ZeroVector;
	FVector LightDirectionWS = FVector::ForwardVector;
	FVector4 ShadowColor = FVector4(0.0f, 0.0f, 0.0f, 1.0f);


	float DepthBias = 0.0f;
	float SlopeScaleDepthBias = 0.0f;
	float FarZ = 0.0f;
	float CullRadius = 0.0f;


	uint32 LightIndex = 0;
	uint32 ShadowMapIndex = 0;

	uint32 Resolution = 0;
	uint32 AtlasViewportX = 0;
	uint32 AtlasViewportY = 0;


	uint32 FaceIndex = 0;
	uint32 CascadeIndex = 0;

	uint8 bIsDirectionalLight : 1 = 0;
	uint8 bIsPointLight : 1 = 0;
	uint8 bIsSpotLight : 1 = 0;
	uint8 bUseAtlas : 1 = 0;
};

struct FShadowPassConstantsGPU
{
	FMatrix LightViewProj;
	FVector4 ShadowParams; // X: DepthBias, Y: SlopeBias, Z: FarZ, W: 예약
};

struct FShadowDataGPU
{
	FMatrix ViewProj;
	float DepthBias;
	float Pad0;
	float Pad1;
	float Pad2;
};