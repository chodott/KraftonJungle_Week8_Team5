#include "Actor/LocalHeightFogActor.h"

#include "Component/BillboardComponent.h"
#include "Core/Paths.h"
#include "Object/ObjectFactory.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(ALocalHeightFogActor, AActor)

namespace
{
	template <typename TComponent>
	TComponent* FindLocalHeightFogComponentByName(const ALocalHeightFogActor* Actor, const char* ComponentName)
	{
		if (!Actor)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component || !Component->IsA(TComponent::StaticClass()) || Component->GetName() != ComponentName)
			{
				continue;
			}

			return static_cast<TComponent*>(Component);
		}

		return nullptr;
	}

	void ConfigureLocalFogBillboard(UBillboardComponent* IconBillboardComponent, ULocalHeightFogComponent* LocalHeightFogComponent)
	{
		if (!IconBillboardComponent)
		{
			return;
		}

		IconBillboardComponent->DetachFromParent();
		if (LocalHeightFogComponent)
		{
			IconBillboardComponent->AttachTo(LocalHeightFogComponent);
		}

		IconBillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_ExpoHeightFog.png").wstring());
		IconBillboardComponent->SetSize(FVector2(0.5f, 0.5f));
		IconBillboardComponent->SetIgnoreParentScaleInRender(true);
		IconBillboardComponent->SetEditorVisualization(true);
		IconBillboardComponent->SetHiddenInGame(true);
		IconBillboardComponent->UpdateBounds();
	}
}

void ALocalHeightFogActor::PostSpawnInitialize()
{
	LocalHeightFogComponent = FObjectFactory::ConstructObject<ULocalHeightFogComponent>(this, "LocalHeightFogComponent");
	AddOwnedComponent(LocalHeightFogComponent);

	IconBillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "IconBillboardComponent");
	if (IconBillboardComponent)
	{
		AddOwnedComponent(IconBillboardComponent);
		ConfigureLocalFogBillboard(IconBillboardComponent, LocalHeightFogComponent);
	}

	AActor::PostSpawnInitialize();
}

void ALocalHeightFogActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);

	if (!Ar.IsLoading())
	{
		return;
	}

	LocalHeightFogComponent = GetComponentByClass<ULocalHeightFogComponent>();
	IconBillboardComponent = FindLocalHeightFogComponentByName<UBillboardComponent>(this, "IconBillboardComponent");
	if (!IconBillboardComponent)
	{
		IconBillboardComponent = FindLocalHeightFogComponentByName<UBillboardComponent>(this, "BillboardComponent");
	}

	if (!IconBillboardComponent)
	{
		IconBillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "IconBillboardComponent");
		if (IconBillboardComponent)
		{
			AddOwnedComponent(IconBillboardComponent);
			if (!IconBillboardComponent->IsRegistered())
			{
				IconBillboardComponent->OnRegister();
			}
		}
	}

	ConfigureLocalFogBillboard(IconBillboardComponent, LocalHeightFogComponent);
}

void ALocalHeightFogActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	ALocalHeightFogActor* DuplicatedActor = static_cast<ALocalHeightFogActor*>(DuplicatedObject);
	DuplicatedActor->LocalHeightFogComponent = Context.FindDuplicate(LocalHeightFogComponent);
	DuplicatedActor->IconBillboardComponent = Context.FindDuplicate(IconBillboardComponent);
}
