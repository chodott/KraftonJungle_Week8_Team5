#include "PointLightActor.h"

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
	TComponent* FindPointLightComponentByName(const APointLightActor* Actor, const char* ComponentName)
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

IMPLEMENT_RTTI(APointLightActor, AActor)

void APointLightActor::PostSpawnInitialize()
{
	PointLightComponent = FObjectFactory::ConstructObject<UPointLightComponent>(this, "PointLightComponent");
	AddOwnedComponent(PointLightComponent);

	IconBillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "BillboardComponent");
	if (IconBillboardComponent)
	{
		AddOwnedComponent(IconBillboardComponent);
		IconBillboardComponent->AttachTo(PointLightComponent);
		IconBillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_LightPoint.png").wstring());
		IconBillboardComponent->SetSize(FVector2(0.5f, 0.5f));
		IconBillboardComponent->SetIgnoreParentScaleInRender(true);
		IconBillboardComponent->SetEditorVisualization(true);
		IconBillboardComponent->SetHiddenInGame(true);
	}
	UpdateBillboardTint();

	RadiusGizmoComponent = FObjectFactory::ConstructObject<ULineBatchComponent>(this, "RadiusGizmoComponent");
	if (RadiusGizmoComponent)
	{
		AddOwnedComponent(RadiusGizmoComponent);
		RadiusGizmoComponent->AttachTo(PointLightComponent);
		RadiusGizmoComponent->SetIgnoreParentScaleInRender(true);
		RadiusGizmoComponent->SetEditorVisualization(true);
		RadiusGizmoComponent->SetHiddenInGame(true);
		RadiusGizmoComponent->SetDrawDebugBounds(false);
		UpdateRadiusGizmo();
	}

	AActor::PostSpawnInitialize();
}

void APointLightActor::OnOwnedComponentPropertyChanged(UActorComponent* ChangedComponent)
{
	AActor::OnOwnedComponentPropertyChanged(ChangedComponent);
	if (ChangedComponent == PointLightComponent)
	{
		UpdateBillboardTint();
		UpdateRadiusGizmo();
	}
}

void APointLightActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);

	if (!Ar.IsLoading())
	{
		return;
	}

	PointLightComponent = GetComponentByClass<UPointLightComponent>();
	IconBillboardComponent = FindPointLightComponentByName<UBillboardComponent>(this, "BillboardComponent");
	if (!IconBillboardComponent)
	{
		IconBillboardComponent = FindPointLightComponentByName<UBillboardComponent>(this, "IconBillboardComponent");
	}

	RadiusGizmoComponent = FindPointLightComponentByName<ULineBatchComponent>(this, "RadiusGizmoComponent");
	if (!RadiusGizmoComponent)
	{
		RadiusGizmoComponent = FObjectFactory::ConstructObject<ULineBatchComponent>(this, "RadiusGizmoComponent");
		if (RadiusGizmoComponent)
		{
			AddOwnedComponent(RadiusGizmoComponent);
			if (!RadiusGizmoComponent->IsRegistered())
			{
				RadiusGizmoComponent->OnRegister();
			}
		}
	}

	if (IconBillboardComponent)
	{
		IconBillboardComponent->DetachFromParent();
		if (PointLightComponent)
		{
			IconBillboardComponent->AttachTo(PointLightComponent);
		}
	}

	if (RadiusGizmoComponent)
	{
		RadiusGizmoComponent->DetachFromParent();
		if (PointLightComponent)
		{
			RadiusGizmoComponent->AttachTo(PointLightComponent);
		}
		RadiusGizmoComponent->SetIgnoreParentScaleInRender(true);
		RadiusGizmoComponent->SetEditorVisualization(true);
		RadiusGizmoComponent->SetHiddenInGame(true);
		RadiusGizmoComponent->SetDrawDebugBounds(false);
	}

	UpdateBillboardTint();
	UpdateRadiusGizmo();
}

void APointLightActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	APointLightActor* DuplicatedActor = static_cast<APointLightActor*>(DuplicatedObject);
	DuplicatedActor->PointLightComponent = Context.FindDuplicate(PointLightComponent);
	DuplicatedActor->IconBillboardComponent = Context.FindDuplicate(IconBillboardComponent);
	DuplicatedActor->RadiusGizmoComponent = Context.FindDuplicate(RadiusGizmoComponent);
	DuplicatedActor->bEditorGizmoVisible = bEditorGizmoVisible;
}

void APointLightActor::SetEditorGizmoVisible(bool bVisible)
{
	if (bEditorGizmoVisible == bVisible)
	{
		return;
	}

	bEditorGizmoVisible = bVisible;
	UpdateRadiusGizmo();
}

void APointLightActor::UpdateRadiusGizmo()
{
	if (!PointLightComponent || !RadiusGizmoComponent)
	{
		return;
	}

	RadiusGizmoComponent->Clear();
	if (!bEditorGizmoVisible)
	{
		return;
	}

	const float Radius = (std::max)(PointLightComponent->GetAttenuationRadius(), 0.0f);
	if (Radius <= FMath::SmallNumber)
	{
		return;
	}

	const FVector4 GizmoColor(0.10f, 0.45f, 1.00f, 1.00f);
	RadiusGizmoComponent->DrawWireSphere(FVector::ZeroVector, Radius, GizmoColor);
}

void APointLightActor::UpdateBillboardTint()
{
	if (!PointLightComponent || !IconBillboardComponent)
	{
		return;
	}

	FVector4 Tint = PointLightComponent->GetColor().ToVector4();
	Tint.W = 1.0f;
	IconBillboardComponent->SetBaseColor(Tint);
}
