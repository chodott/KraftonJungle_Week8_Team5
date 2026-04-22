#pragma once

#include "CoreMinimal.h"
#include "Actor/Actor.h"
#include "Component/LocalHeightFogComponent.h"

class UBillboardComponent;
class FArchive;

class ENGINE_API ALocalHeightFogActor : public AActor
{
public:
	DECLARE_RTTI(ALocalHeightFogActor, AActor)

	void PostSpawnInitialize() override;
	void Serialize(FArchive& Ar) override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;

private:
	ULocalHeightFogComponent* LocalHeightFogComponent = nullptr;
	UBillboardComponent* IconBillboardComponent = nullptr;
};
