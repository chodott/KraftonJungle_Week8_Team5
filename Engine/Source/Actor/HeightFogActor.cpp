#include "Actor/HeightFogActor.h"
#include "Component/BillboardComponent.h"
#include "Core/Paths.h"
#include "Object/ObjectFactory.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(AHeightFogActor, AActor)

void AHeightFogActor::PostSpawnInitialize()
{
	HeightFogComponent = FObjectFactory::ConstructObject<UHeightFogComponent>(this, "HeightFogComponent");
	AddOwnedComponent(HeightFogComponent);

	BillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "BillboardComponent");
	if (BillboardComponent)
	{
		AddOwnedComponent(BillboardComponent);
		BillboardComponent->AttachTo(HeightFogComponent);
		BillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_DecalActorIcon.png").wstring());
		BillboardComponent->SetSize(FVector2(0.5f, 0.5f));
		BillboardComponent->SetIgnoreParentScaleInRender(true);
		BillboardComponent->SetEditorVisualization(true);
		BillboardComponent->SetHiddenInGame(true);
	}

	AActor::PostSpawnInitialize();
}

void AHeightFogActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	static_cast<AHeightFogActor*>(DuplicatedObject)->HeightFogComponent = Context.FindDuplicate(HeightFogComponent);
}