#pragma once

#include "CoreMinimal.h"

struct FAmbientLightInfo
{
	FVector4 Color;
	float Intensity;
	FVector Padding;
};

struct FDirectionalLightInfo
{
	FVector4 Color;
	FVector Direction;
	float Intensity;
};

struct FPointLightInfo
{
	FVector4 Color;
	FVector Position; // World Space
	float Intensity;
	float Range;
	float FalloffExponent;
	FVector2 Padding;
};

struct FSpotLightInfo
{
	FVector4 Color;
	FVector Position; // World Space
	float Intensity;
	FVector Direction;
	float Range;
	float InnerCutoff; // cos(inner angle)
	float OuterCutoff; // cos(outer angle)
	float FalloffExponent;
	float Padding;
};

struct FLightConstantBuffer
{
	FAmbientLightInfo Ambient;
	FDirectionalLightInfo Directional;
	FPointLightInfo PointLights[4];
	FSpotLightInfo SpotLights[4];
};
