#pragma once

#include "CoreMinimal.h"
#include "Actor/Actor.h"
#include "Component/LocalHeightFogComponent.h"

class ENGINE_API ALocalHeightFogActor : public AActor
{
	DECLARE_RTTI(ALocalHeightFogActor, AActor)

	virtual void PostSpawnInitialize() override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;

private:
	ULocalHeightFogComponent* LocalHeightFogComponent = nullptr;
};
