#include "Actor/HeightFogActor.h"

#include "Object/ObjectFactory.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(AHeightFogActor, AActor)

void AHeightFogActor::PostSpawnInitialize()
{
	HeightFogComponent = FObjectFactory::ConstructObject<UHeightFogComponent>(this, "HeightFogComponent");
	AddOwnedComponent(HeightFogComponent);

	AActor::PostSpawnInitialize();
}

void AHeightFogActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	static_cast<AHeightFogActor*>(DuplicatedObject)->HeightFogComponent = Context.FindDuplicate(HeightFogComponent);
}