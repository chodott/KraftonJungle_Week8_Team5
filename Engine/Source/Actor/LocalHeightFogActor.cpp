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

	void ConfigureLocalFogBillboard(UBillboardComponent* BillboardComponent, ULocalHeightFogComponent* LocalHeightFogComponent)
	{
		if (!BillboardComponent)
		{
			return;
		}

		BillboardComponent->DetachFromParent();
		if (LocalHeightFogComponent)
		{
			BillboardComponent->AttachTo(LocalHeightFogComponent);
		}

		BillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_ExpoHeightFog.png").wstring());
		BillboardComponent->SetSize(FVector2(0.5f, 0.5f));
		BillboardComponent->SetIgnoreParentScaleInRender(true);
		BillboardComponent->SetEditorVisualization(true);
		BillboardComponent->SetHiddenInGame(true);
		BillboardComponent->UpdateBounds();
	}
}

void ALocalHeightFogActor::PostSpawnInitialize()
{
	LocalHeightFogComponent = FObjectFactory::ConstructObject<ULocalHeightFogComponent>(this, "LocalHeightFogComponent");
	AddOwnedComponent(LocalHeightFogComponent);

	BillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "BillboardComponent");
	if (BillboardComponent)
	{
		AddOwnedComponent(BillboardComponent);
		ConfigureLocalFogBillboard(BillboardComponent, LocalHeightFogComponent);
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
	BillboardComponent = FindLocalHeightFogComponentByName<UBillboardComponent>(this, "BillboardComponent");

	if (!BillboardComponent)
	{
		BillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "BillboardComponent");
		if (BillboardComponent)
		{
			AddOwnedComponent(BillboardComponent);
			if (!BillboardComponent->IsRegistered())
			{
				BillboardComponent->OnRegister();
			}
		}
	}

	ConfigureLocalFogBillboard(BillboardComponent, LocalHeightFogComponent);
}

void ALocalHeightFogActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	ALocalHeightFogActor* DuplicatedActor = static_cast<ALocalHeightFogActor*>(DuplicatedObject);
	DuplicatedActor->LocalHeightFogComponent = Context.FindDuplicate(LocalHeightFogComponent);
	DuplicatedActor->BillboardComponent = Context.FindDuplicate(BillboardComponent);
}
