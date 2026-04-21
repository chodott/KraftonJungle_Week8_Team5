#pragma once

#include "CoreMinimal.h"
#include "Math/LinearColor.h"

#include <d3d11.h>

struct ENGINE_API FFogRenderItem
{
	FVector FogOrigin = FVector::ZeroVector;
	FVector FogExtents = FVector::ZeroVector;
	bool bLocalFogVolume = false;
	float FogDensity = 0.0f;
	float FogHeightFalloff = 0.0f;
	float StartDistance = 0.0f;
	float FogCutoffDistance = 0.0f;
	float FogMaxOpacity = 1.0f;
	float AllowBackground = 1.0f;
	FLinearColor FogInscatteringColor = FLinearColor::White;
	FMatrix FogVolumeWorld = FMatrix::Identity;
	FMatrix WorldToFogVolume = FMatrix::Identity;

	bool IsLocalFogVolume() const
	{
		return bLocalFogVolume;
	}
};

struct ENGINE_API FFogRenderRequest
{
	TArray<FFogRenderItem> Items;
	ID3D11ShaderResourceView* DepthTextureSRV = nullptr;
	FMatrix InverseViewProjection = FMatrix::Identity;
	FVector CameraPosition = FVector::ZeroVector;
	bool bEnabled = true;

	bool IsEmpty() const
	{
		return !bEnabled || DepthTextureSRV == nullptr || Items.empty();
	}
};
