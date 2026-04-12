#pragma once

#include "CoreMinimal.h"

#include <d3d11.h>

struct ENGINE_API FFrameContext
{
	float TotalTimeSeconds = 0.0f;
	float DeltaTimeSeconds = 0.0f;
};

struct ENGINE_API FViewContext
{
	FMatrix View = FMatrix::Identity;
	FMatrix Projection = FMatrix::Identity;
	FMatrix ViewProjection = FMatrix::Identity;
	FMatrix InverseView = FMatrix::Identity;
	FMatrix InverseProjection = FMatrix::Identity;
	FMatrix InverseViewProjection = FMatrix::Identity;

	FVector CameraPosition = FVector::ZeroVector;
	float NearZ = 0.1f;
	float FarZ = 1000.0f;

	D3D11_VIEWPORT Viewport = {};
};
