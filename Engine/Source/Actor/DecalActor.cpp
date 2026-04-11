#include "Actor/DecalActor.h"

#include "Component/DecalComponent.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_RTTI(ADecalActor, AActor)

void ADecalActor::PostSpawnInitialize()
{
	DecalComponent = FObjectFactory::ConstructObject<UDecalComponent>(this, "DecalComponent");
	AddOwnedComponent(DecalComponent);

	AActor::PostSpawnInitialize();
}

void ADecalActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	static_cast<ADecalActor*>(DuplicatedObject)->DecalComponent = Context.FindDuplicate(DecalComponent);
}
