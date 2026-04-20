#pragma once

#include "CoreMinimal.h"

class UPrimitiveComponent;
class UTextRenderComponent;
class USubUVComponent;
class UBillboardComponent;
class UDecalComponent;
class UFireBallComponent;
class USpotLightComponent;
class UPointLightComponent;
class UDirectionalLightComponent;

struct ENGINE_API FSceneMeshPrimitive
{
	// 씬 기하로 렌더링할 프리미티브 컴포넌트다.
	UPrimitiveComponent* Component = nullptr;
};

struct ENGINE_API FSceneTextPrimitive
{
	// 텍스트 렌더 기능이 실제 메시로 확장할 텍스트 컴포넌트다.
	UTextRenderComponent* Component = nullptr;
};

struct ENGINE_API FSceneSubUVPrimitive
{
	// 스프라이트 기능이 실제 메시로 확장할 SubUV 컴포넌트다.
	USubUVComponent* Component = nullptr;
};

struct ENGINE_API FSceneBillboardPrimitive
{
	// 빌보드 기능이 실제 메시로 확장할 빌보드 컴포넌트다.
	UBillboardComponent* Component = nullptr;
};

struct ENGINE_API FSceneFogPrimitive
{
	// 후처리 안개 패스가 참조할 글로벌/로컬 fog 프리미티브 컴포넌트다.
	UPrimitiveComponent* Component = nullptr;
};

struct ENGINE_API FSceneDecalPrimitive
{
	// 데칼 패스가 참조할 데칼 컴포넌트다.
	UDecalComponent* Component = nullptr;
};

struct ENGINE_API FSceneFireBallPrimitive
{
	// 파이어볼 패스가 참조할 파이어볼 컴포넌트다.
	UFireBallComponent* Component = nullptr;
};

struct ENGINE_API FScenePointLightPrimitive
{
	UPointLightComponent* Component = nullptr;
};

struct ENGINE_API FSceneSpotLightPrimitive
{
	USpotLightComponent* Component = nullptr;
};

struct ENGINE_API FSceneDirectionalLightPrimitive
{
	UDirectionalLightComponent* Component = nullptr;
};

struct ENGINE_API FSceneRenderPacket
{
	// 이 뷰에서 월드로부터 수집한 메시 프리미티브 목록이다.
	TArray<FSceneMeshPrimitive> MeshPrimitives;
	// 이 뷰에서 월드로부터 수집한 텍스트 프리미티브 목록이다.
	TArray<FSceneTextPrimitive> TextPrimitives;
	// 이 뷰에서 월드로부터 수집한 애니메이션 스프라이트 프리미티브 목록이다.
	TArray<FSceneSubUVPrimitive> SubUVPrimitives;
	// 이 뷰에서 월드로부터 수집한 빌보드 프리미티브 목록이다.
	TArray<FSceneBillboardPrimitive> BillboardPrimitives;
	// 프러스텀과 무관하게 뷰 전체에 적용할 포그 컴포넌트 목록이다.
	TArray<FSceneFogPrimitive> FogPrimitives;
	// 데칼 컴포넌트 목록이다.
	TArray<FSceneDecalPrimitive> DecalPrimitives;
	// 파이어볼 컴포넌트 목록이다.
	TArray<FSceneFireBallPrimitive> FireBallPrimitives;
	// 조명 컴포넌트 목록이다.
	TArray<FSceneSpotLightPrimitive>        SpotLightPrimitives;
	TArray<FScenePointLightPrimitive>       PointLightPrimitives;
	TArray<FSceneDirectionalLightPrimitive> DirectionalLightPrimitives;
	// FXAA 적용 여부이다.
	bool bApplyFXAA = false;


	// 각 프리미티브 버킷에 같은 reserve 힌트를 적용한다.
	void Reserve(size_t PrimitiveCountHint)
	{
		MeshPrimitives.reserve(PrimitiveCountHint);
		TextPrimitives.reserve(PrimitiveCountHint);
		SubUVPrimitives.reserve(PrimitiveCountHint);
		BillboardPrimitives.reserve(PrimitiveCountHint);
		FogPrimitives.reserve(PrimitiveCountHint);
		DecalPrimitives.reserve(PrimitiveCountHint);
		FireBallPrimitives.reserve(PrimitiveCountHint);
		SpotLightPrimitives.reserve(PrimitiveCountHint);
		PointLightPrimitives.reserve(PrimitiveCountHint);
		DirectionalLightPrimitives.reserve(PrimitiveCountHint);
	}

	// 패킷 안의 모든 프리미티브 버킷을 비운다.
	void Clear()
	{
		MeshPrimitives.clear();
		TextPrimitives.clear();
		SubUVPrimitives.clear();
		BillboardPrimitives.clear();
		FogPrimitives.clear();
		DecalPrimitives.clear();
		FireBallPrimitives.clear();
		SpotLightPrimitives.clear();
		PointLightPrimitives.clear();
		DirectionalLightPrimitives.clear();
		bApplyFXAA = false;
	}
};
