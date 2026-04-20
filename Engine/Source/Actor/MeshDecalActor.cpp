#include "Actor/MeshDecalActor.h"

#include "Component/BillboardComponent.h"
#include "Component/MeshDecalComponent.h"
#include "Core/Paths.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(AMeshDecalActor, AActor)

void AMeshDecalActor::PostSpawnInitialize()
{
	MeshDecalComponent = FObjectFactory::ConstructObject<UMeshDecalComponent>(this, "MeshDecalComponent");
	AddOwnedComponent(MeshDecalComponent);

	IconBillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "IconBillboardComponent");
	if (IconBillboardComponent)
	{
		AddOwnedComponent(IconBillboardComponent);
		IconBillboardComponent->AttachTo(MeshDecalComponent);
		IconBillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_DecalActorIcon.png").wstring());
		IconBillboardComponent->SetSize(FVector2(0.5f, 0.5f));
		IconBillboardComponent->SetIgnoreParentScaleInRender(true);
		IconBillboardComponent->SetEditorVisualization(true);
		IconBillboardComponent->SetHiddenInGame(true);
	}

	AActor::PostSpawnInitialize();
}

void AMeshDecalActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);

	if (!Ar.IsLoading())
	{
		return;
	}

	MeshDecalComponent = GetComponentByClass<UMeshDecalComponent>();
	IconBillboardComponent = nullptr;

	for (UActorComponent* Component : GetComponents())
	{
		if (!Component)
		{
			continue;
		}

		if (!IconBillboardComponent && Component->IsA(UBillboardComponent::StaticClass()) && Component->GetName() == "IconBillboardComponent")
		{
			IconBillboardComponent = static_cast<UBillboardComponent*>(Component);
		}
	}

	if (IconBillboardComponent)
	{
		IconBillboardComponent->DetachFromParent();
		if (MeshDecalComponent)
		{
			IconBillboardComponent->AttachTo(MeshDecalComponent);
		}
	}
}

void AMeshDecalActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	AMeshDecalActor* DuplicatedActor = static_cast<AMeshDecalActor*>(DuplicatedObject);
	DuplicatedActor->MeshDecalComponent = Context.FindDuplicate(MeshDecalComponent);
	DuplicatedActor->IconBillboardComponent = Context.FindDuplicate(IconBillboardComponent);
}
