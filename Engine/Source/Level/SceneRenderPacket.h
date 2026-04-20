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
class UMeshDecalComponent;

struct ENGINE_API FSceneMeshPrimitive
{
	UPrimitiveComponent* Component = nullptr;
};

struct ENGINE_API FSceneTextPrimitive
{
	UTextRenderComponent* Component = nullptr;
};

struct ENGINE_API FSceneSubUVPrimitive
{
	USubUVComponent* Component = nullptr;
};

struct ENGINE_API FSceneBillboardPrimitive
{
	UBillboardComponent* Component = nullptr;
};

struct ENGINE_API FSceneFogPrimitive
{
	UPrimitiveComponent* Component = nullptr;
};

struct ENGINE_API FSceneDecalPrimitive
{
	UDecalComponent* Component = nullptr;
};

struct ENGINE_API FSceneMeshDecalPrimitive
{
	UMeshDecalComponent* Component = nullptr;
};

struct ENGINE_API FSceneFireBallPrimitive
{
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
	TArray<FSceneMeshPrimitive> MeshPrimitives;
	TArray<FSceneTextPrimitive> TextPrimitives;
	TArray<FSceneSubUVPrimitive> SubUVPrimitives;
	TArray<FSceneBillboardPrimitive> BillboardPrimitives;
	TArray<FSceneFogPrimitive> FogPrimitives;
	TArray<FSceneDecalPrimitive> DecalPrimitives;
	TArray<FSceneMeshDecalPrimitive> MeshDecalPrimitives;
	TArray<FSceneFireBallPrimitive> FireBallPrimitives;
	TArray<FSceneSpotLightPrimitive> SpotLightPrimitives;
	TArray<FScenePointLightPrimitive> PointLightPrimitives;
	TArray<FSceneDirectionalLightPrimitive> DirectionalLightPrimitives;
	bool bApplyFXAA = false;

	void Reserve(size_t PrimitiveCountHint)
	{
		MeshPrimitives.reserve(PrimitiveCountHint);
		TextPrimitives.reserve(PrimitiveCountHint);
		SubUVPrimitives.reserve(PrimitiveCountHint);
		BillboardPrimitives.reserve(PrimitiveCountHint);
		FogPrimitives.reserve(PrimitiveCountHint);
		DecalPrimitives.reserve(PrimitiveCountHint);
		MeshDecalPrimitives.reserve(PrimitiveCountHint);
		FireBallPrimitives.reserve(PrimitiveCountHint);
		SpotLightPrimitives.reserve(PrimitiveCountHint);
		PointLightPrimitives.reserve(PrimitiveCountHint);
		DirectionalLightPrimitives.reserve(PrimitiveCountHint);
	}

	void Clear()
	{
		MeshPrimitives.clear();
		TextPrimitives.clear();
		SubUVPrimitives.clear();
		BillboardPrimitives.clear();
		FogPrimitives.clear();
		DecalPrimitives.clear();
		MeshDecalPrimitives.clear();
		FireBallPrimitives.clear();
		SpotLightPrimitives.clear();
		PointLightPrimitives.clear();
		DirectionalLightPrimitives.clear();
		bApplyFXAA = false;
	}
};
