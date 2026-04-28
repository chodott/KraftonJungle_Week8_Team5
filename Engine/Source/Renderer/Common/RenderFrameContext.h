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
	bool bOrthographic = false;

	// 그림자 캐시 시스템용 메시 필터링 플래그
	// bShadowStaticOnly: 캐시 갱신 패스 — 정적 메시만 그림
	// bShadowDynamicOnly: Stationary 라이트의 메인 패스 — 동적 메시만 그림
	bool bShadowStaticOnly = false;
	bool bShadowDynamicOnly = false;

	D3D11_VIEWPORT Viewport = {};
};
