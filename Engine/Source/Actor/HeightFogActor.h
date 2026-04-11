

#include "CoreMinimal.h"
#include "Actor/Actor.h"
#include "Component/HeightFogComponent.h"

class ENGINE_API AHeightFogActor : public AActor
{
	DECLARE_RTTI(AHeightFogActor, AActor)

	virtual void PostSpawnInitialize() override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;

private:
	UHeightFogComponent* HeightFogComponent = nullptr;
};
