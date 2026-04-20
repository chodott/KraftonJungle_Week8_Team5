#pragma once
#include "Math/Matrix.h"

// b0: 프레임당 1회 업데이트 (카메라)
struct FFrameConstantBuffer
{
	FMatrix  View;
	FMatrix  Projection;
	FVector4 CameraPosition;

	float Time;
	float DeltaTime;
	float Padding[2] = { 0.0f, 0.0f };
};

// b1: 오브젝트당 업데이트
struct FObjectConstantBuffer
{
	FMatrix World;
	FMatrix WorldInvTranspose;

	uint32 LocalLightListOffset = 0;
	uint32 LocalLightListCount  = 0;
	uint32 ObjectFlags          = 0;
	uint32 Pad0                 = 0;
};
