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

	IconBillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "IconBillboardComponent");
	if (IconBillboardComponent)
	{
		AddOwnedComponent(IconBillboardComponent);
		IconBillboardComponent->AttachTo(HeightFogComponent);
		IconBillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_ExpoHeightFog.png").wstring());
		IconBillboardComponent->SetSize(FVector2(0.5f, 0.5f));
		IconBillboardComponent->SetIgnoreParentScaleInRender(true);
		IconBillboardComponent->SetEditorVisualization(true);
		IconBillboardComponent->SetHiddenInGame(true);
	}

	AActor::PostSpawnInitialize();
}

void AHeightFogActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	AHeightFogActor* DuplicatedActor = static_cast<AHeightFogActor*>(DuplicatedObject);
	DuplicatedActor->HeightFogComponent = Context.FindDuplicate(HeightFogComponent);
	DuplicatedActor->IconBillboardComponent = Context.FindDuplicate(IconBillboardComponent);
}