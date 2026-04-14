#include "Actor/DecalActor.h"

#include "Component/BillboardComponent.h"
#include "Component/DecalComponent.h"
#include "Core/Paths.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_RTTI(ADecalActor, AActor)

void ADecalActor::PostSpawnInitialize()
{
	bTickInEditor = true;

	DecalComponent = FObjectFactory::ConstructObject<UDecalComponent>(this, "DecalComponent");
	AddOwnedComponent(DecalComponent);

	BillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "BillboardComponent");
	if (BillboardComponent)
	{
		AddOwnedComponent(BillboardComponent);
		BillboardComponent->AttachTo(DecalComponent);
		BillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_DecalActorIcon.png").wstring());
		BillboardComponent->SetSize(FVector2(0.5f, 0.5f));
		BillboardComponent->SetHiddenInGame(true);
	}

	AActor::PostSpawnInitialize();
}

void ADecalActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	ADecalActor* DuplicatedActor = static_cast<ADecalActor*>(DuplicatedObject);
	DuplicatedActor->DecalComponent = Context.FindDuplicate(DecalComponent);
	DuplicatedActor->BillboardComponent = Context.FindDuplicate(BillboardComponent);
}