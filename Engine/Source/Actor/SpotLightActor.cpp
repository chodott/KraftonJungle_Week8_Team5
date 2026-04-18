#include "SpotLightActor.h"

#include <algorithm>

#include "Component/LineBatchComponent.h"
#include "Core/Paths.h"
#include "Math/MathUtility.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"
#include "Serializer/Archive.h"

namespace
{
	template <typename TComponent>
	TComponent* FindSpotLightComponentByName(const ASpotLightActor* Actor, const char* ComponentName)
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
}

IMPLEMENT_RTTI(ASpotLightActor, AActor)

void ASpotLightActor::PostSpawnInitialize()
{
	SpotLightComponent = FObjectFactory::ConstructObject<USpotLightComponent>(this, "SpotLightComponent");
	AddOwnedComponent(SpotLightComponent);
	SpotLightComponent->SetRelativeTransform(FTransform(FRotator(90.f, 0.0f, 0.0), FVector::ZeroVector, FVector::OneVector));

	IconBillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "BillboardComponent");
	if (IconBillboardComponent)
	{
		AddOwnedComponent(IconBillboardComponent);
		IconBillboardComponent->AttachTo(SpotLightComponent);
		IconBillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_LightSpot.png").wstring());
		IconBillboardComponent->SetSize(FVector2(0.5f, 0.5f));
		IconBillboardComponent->SetIgnoreParentScaleInRender(true);
		IconBillboardComponent->SetEditorVisualization(true);
		IconBillboardComponent->SetHiddenInGame(true);
	}
	UpdateBillboardTint();

	ConeGizmoComponent = FObjectFactory::ConstructObject<ULineBatchComponent>(this, "ConeGizmoComponent");
	if (ConeGizmoComponent)
	{
		AddOwnedComponent(ConeGizmoComponent);
		ConeGizmoComponent->AttachTo(SpotLightComponent);
		ConeGizmoComponent->SetIgnoreParentScaleInRender(true);
		ConeGizmoComponent->SetEditorVisualization(true);
		ConeGizmoComponent->SetHiddenInGame(true);
		ConeGizmoComponent->SetDrawDebugBounds(false);
		UpdateConeGizmo();
	}

	AActor::PostSpawnInitialize();
}

void ASpotLightActor::OnOwnedComponentPropertyChanged(UActorComponent* ChangedComponent)
{
	AActor::OnOwnedComponentPropertyChanged(ChangedComponent);
	if (ChangedComponent == SpotLightComponent)
	{
		UpdateBillboardTint();
		UpdateConeGizmo();
	}
}

void ASpotLightActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);

	if (!Ar.IsLoading())
	{
		return;
	}

	SpotLightComponent = GetComponentByClass<USpotLightComponent>();
	IconBillboardComponent = FindSpotLightComponentByName<UBillboardComponent>(this, "BillboardComponent");
	if (!IconBillboardComponent)
	{
		IconBillboardComponent = FindSpotLightComponentByName<UBillboardComponent>(this, "IconBillboardComponent");
	}

	ConeGizmoComponent = FindSpotLightComponentByName<ULineBatchComponent>(this, "ConeGizmoComponent");
	if (!ConeGizmoComponent)
	{
		ConeGizmoComponent = FObjectFactory::ConstructObject<ULineBatchComponent>(this, "ConeGizmoComponent");
		if (ConeGizmoComponent)
		{
			AddOwnedComponent(ConeGizmoComponent);
			if (!ConeGizmoComponent->IsRegistered())
			{
				ConeGizmoComponent->OnRegister();
			}
		}
	}

	if (IconBillboardComponent)
	{
		IconBillboardComponent->DetachFromParent();
		if (SpotLightComponent)
		{
			IconBillboardComponent->AttachTo(SpotLightComponent);
		}
	}

	if (ConeGizmoComponent)
	{
		ConeGizmoComponent->DetachFromParent();
		if (SpotLightComponent)
		{
			ConeGizmoComponent->AttachTo(SpotLightComponent);
		}
		ConeGizmoComponent->SetIgnoreParentScaleInRender(true);
		ConeGizmoComponent->SetEditorVisualization(true);
		ConeGizmoComponent->SetHiddenInGame(true);
		ConeGizmoComponent->SetDrawDebugBounds(false);
	}

	UpdateBillboardTint();
	UpdateConeGizmo();
}

void ASpotLightActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	ASpotLightActor* DuplicatedActor = static_cast<ASpotLightActor*>(DuplicatedObject);
	DuplicatedActor->SpotLightComponent = Context.FindDuplicate(SpotLightComponent);
	DuplicatedActor->IconBillboardComponent = Context.FindDuplicate(IconBillboardComponent);
	DuplicatedActor->ConeGizmoComponent = Context.FindDuplicate(ConeGizmoComponent);
	DuplicatedActor->bEditorGizmoVisible = bEditorGizmoVisible;
}

void ASpotLightActor::SetEditorGizmoVisible(bool bVisible)
{
	if (bEditorGizmoVisible == bVisible)
	{
		return;
	}

	bEditorGizmoVisible = bVisible;
	UpdateConeGizmo();
}

void ASpotLightActor::UpdateConeGizmo()
{
	if (!SpotLightComponent || !ConeGizmoComponent)
	{
		return;
	}

	ConeGizmoComponent->Clear();
	if (!bEditorGizmoVisible)
	{
		return;
	}

	const float Length = (std::max)(SpotLightComponent->GetAttenuationRadius(), 0.0f);
	if (Length <= FMath::SmallNumber)
	{
		return;
	}

	const float OuterConeAngle = FMath::Clamp(SpotLightComponent->GetOuterConeAngle(), 0.0f, 80.0f);
	const float InnerConeAngle = FMath::Clamp(SpotLightComponent->GetInnerConeAngle(), 0.0f, OuterConeAngle);

	const FVector4 OuterColor(0.20f, 0.75f, 1.00f, 1.00f);
	const FVector4 InnerColor(0.05f, 0.35f, 0.85f, 1.00f);

	ConeGizmoComponent->DrawWireCone(
		FVector::ZeroVector,
		FVector::ForwardVector,
		Length,
		OuterConeAngle,
		OuterColor,
		16,
		24,
		true);

	if (InnerConeAngle > FMath::KindaSmallNumber)
	{
		ConeGizmoComponent->DrawWireCone(
			FVector::ZeroVector,
			FVector::ForwardVector,
			Length,
			InnerConeAngle,
			InnerColor,
			16,
			16,
			true);
	}
}

void ASpotLightActor::UpdateBillboardTint()
{
	if (!SpotLightComponent || !IconBillboardComponent)
	{
		return;
	}

	FVector4 Tint = SpotLightComponent->GetColor().ToVector4();
	Tint.W = 1.0f;
	IconBillboardComponent->SetBaseColor(Tint);
}
