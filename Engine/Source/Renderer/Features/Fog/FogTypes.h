#pragma once

#include "CoreMinimal.h"
#include "Math/LinearColor.h"

#include <d3d11.h>

struct ENGINE_API FFogRenderItem
{
	FVector FogOrigin = FVector::ZeroVector;
	float FogDensity = 0.0f;
	float FogHeightFalloff = 0.0f;
	float StartDistance = 0.0f;
	float FogCutoffDistance = 0.0f;
	float FogMaxOpacity = 1.0f;
	float AllowBackground = 1.0f;
	FLinearColor FogInscatteringColor = FLinearColor::White;
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
