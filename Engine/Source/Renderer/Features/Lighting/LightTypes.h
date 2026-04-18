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
	FVector Padding;
};

struct FSpotLightInfo
{
	FVector4 Color;
	FVector Position; // World Space
	float Intensity;
	FVector Direction;
	float Range;
	float InnerCutoff; // cos(내부 각도)
	float OuterCutoff; // cos(외부 각도)
	FVector2 Padding;
};

struct FLightConstantBuffer
{
	FAmbientLightInfo     Ambient;
	FDirectionalLightInfo Directional;
	FPointLightInfo       PointLights[4];
	FSpotLightInfo        SpotLights[4];
};