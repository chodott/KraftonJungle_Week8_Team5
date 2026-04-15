#pragma once

#include "CoreMinimal.h"
#include "Math/LinearColor.h"

#include <d3d11.h>

struct ENGINE_API FFireBallRenderItem
{
	FVector FireballOrigin = FVector::ZeroVector;
	float Intensity = 1.0f;
	float Radius = 1.0f;
	float RadiusFallOff = 2.0f;
	FLinearColor Color = FLinearColor::White;
};

struct ENGINE_API FFireBallRenderRequest
{
	TArray<FFireBallRenderItem> Items;
	ID3D11ShaderResourceView* DepthTextureSRV = nullptr;
	FMatrix InverseViewProjection = FMatrix::Identity;
	bool bEnabled = true;

	bool IsEmpty() const
	{
		return !bEnabled || DepthTextureSRV == nullptr || Items.empty();
	}
};
