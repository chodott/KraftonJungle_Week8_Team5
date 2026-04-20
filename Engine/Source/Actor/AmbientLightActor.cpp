#include "AmbientLightActor.h"

#include "Core/Paths.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(AAmbientLightActor, AActor)

void AAmbientLightActor::PostSpawnInitialize()
{
	AmbientLightComponent = FObjectFactory::ConstructObject<UAmbientLightComponent>(this, "AmbientLightComponent");
	AddOwnedComponent(AmbientLightComponent);

	IconBillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "BillboardComponent");
	if (IconBillboardComponent)
	{
		AddOwnedComponent(IconBillboardComponent);
		IconBillboardComponent->AttachTo(AmbientLightComponent);
		IconBillboardComponent->SetTexturePath((FPaths::IconDir() / L"SkyLight.png").wstring());
		IconBillboardComponent->SetSize(FVector2(0.7f, 0.7f));
		IconBillboardComponent->SetIgnoreParentScaleInRender(true);
		IconBillboardComponent->SetEditorVisualization(true);
		IconBillboardComponent->SetHiddenInGame(true);
	}
	UpdateBillboardTint();

	AActor::PostSpawnInitialize();
}

void AAmbientLightActor::OnOwnedComponentPropertyChanged(UActorComponent* ChangedComponent)
{
	AActor::OnOwnedComponentPropertyChanged(ChangedComponent);
	if (ChangedComponent == AmbientLightComponent)
	{
		UpdateBillboardTint();
	}
}

void AAmbientLightActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);

	if (!Ar.IsLoading())
	{
		return;
	}

	AmbientLightComponent = GetComponentByClass<UAmbientLightComponent>();
	IconBillboardComponent = nullptr;
	for (UActorComponent* Component : GetComponents())
	{
		if (!Component || !Component->IsA(UBillboardComponent::StaticClass()))
		{
			continue;
		}

		if (Component->GetName() == "BillboardComponent" || Component->GetName() == "IconBillboardComponent")
		{
			IconBillboardComponent = static_cast<UBillboardComponent*>(Component);
			break;
		}
	}

	if (IconBillboardComponent)
	{
		IconBillboardComponent->DetachFromParent();
		if (AmbientLightComponent)
		{
			IconBillboardComponent->AttachTo(AmbientLightComponent);
		}
	}

	UpdateBillboardTint();
}

void AAmbientLightActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	AAmbientLightActor* DuplicatedActor = static_cast<AAmbientLightActor*>(DuplicatedObject);
	DuplicatedActor->AmbientLightComponent = Context.FindDuplicate(AmbientLightComponent);
	DuplicatedActor->IconBillboardComponent = Context.FindDuplicate(IconBillboardComponent);
}

void AAmbientLightActor::UpdateBillboardTint()
{
	if (!AmbientLightComponent || !IconBillboardComponent)
	{
		return;
	}

	FVector4 Tint = AmbientLightComponent->GetColor().ToVector4();
	Tint.W = 1.0f;
	IconBillboardComponent->SetBaseColor(Tint);
}
