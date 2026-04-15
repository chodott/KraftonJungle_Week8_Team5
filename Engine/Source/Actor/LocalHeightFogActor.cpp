#include "Actor/LocalHeightFogActor.h"

#include "Object/ObjectFactory.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(ALocalHeightFogActor, AActor)

void ALocalHeightFogActor::PostSpawnInitialize()
{
	LocalHeightFogComponent = FObjectFactory::ConstructObject<ULocalHeightFogComponent>(this, "LocalHeightFogComponent");
	AddOwnedComponent(LocalHeightFogComponent);

	AActor::PostSpawnInitialize();
}

void ALocalHeightFogActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	static_cast<ALocalHeightFogActor*>(DuplicatedObject)->LocalHeightFogComponent = Context.FindDuplicate(LocalHeightFogComponent);
}
