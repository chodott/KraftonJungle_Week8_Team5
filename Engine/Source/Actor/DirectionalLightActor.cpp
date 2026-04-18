#include "DirectionalLightActor.h"

#include "Component/StaticMeshComponent.h"
#include "Core/Paths.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"
#include "Primitive/PrimitiveGizmo.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Serializer/Archive.h"

namespace
{
	UStaticMesh* GetDirectionalLightArrowMesh()
	{
		static TObjectPtr<UStaticMesh> ArrowMesh;
		if (ArrowMesh)
		{
			return ArrowMesh;
		}

		std::shared_ptr<FDynamicMesh> SourceMesh =
			FPrimitiveGizmo::CreateTranslationAxisMesh(EAxis::X, FVector4(1.f, 0.95f, 0.2f, 1.0f));
		if (!SourceMesh)
		{
			return nullptr;
		}

		auto StaticRenderMesh = std::make_unique<FStaticMesh>();
		StaticRenderMesh->Topology = SourceMesh->Topology;
		StaticRenderMesh->Vertices = SourceMesh->Vertices;
		StaticRenderMesh->Indices = SourceMesh->Indices;
		StaticRenderMesh->Sections.push_back({ 0, 0, static_cast<uint32>(StaticRenderMesh->Indices.size()) });
		StaticRenderMesh->UpdateLocalBound();

		ArrowMesh = FObjectFactory::ConstructObject<UStaticMesh>(nullptr, "DirectionalLightArrowMesh");
		if (!ArrowMesh)
		{
			return nullptr;
		}

		ArrowMesh->SetStaticMeshAsset(StaticRenderMesh.release());
		ArrowMesh->LocalBounds.Radius = ArrowMesh->GetRenderData()->GetLocalBoundRadius();
		ArrowMesh->LocalBounds.Center = ArrowMesh->GetRenderData()->GetCenterCoord();
		ArrowMesh->LocalBounds.BoxExtent =
			(ArrowMesh->GetRenderData()->GetMaxCoord() - ArrowMesh->GetRenderData()->GetMinCoord()) * 0.5f;

		if (std::shared_ptr<FMaterial> GizmoMaterial = FMaterialManager::Get().FindByName("M_Gizmos"))
		{
			ArrowMesh->AddDefaultMaterial(GizmoMaterial);
		}

		return ArrowMesh;
	}
}

IMPLEMENT_RTTI(ADirectionalLightActor, AActor)

void ADirectionalLightActor::PostSpawnInitialize()
{
	DirectionalLightComponent = FObjectFactory::ConstructObject<UDirectionalLightComponent>(this, "DirectionalLightComponent");
	AddOwnedComponent(DirectionalLightComponent);
	DirectionalLightComponent->SetRelativeTransform(FTransform(FRotator(90.0f, 0.0f, 0.0f), FVector::ZeroVector, FVector::OneVector));

	IconBillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "BillboardComponent");
	if (IconBillboardComponent)
	{
		AddOwnedComponent(IconBillboardComponent);
		IconBillboardComponent->AttachTo(DirectionalLightComponent);
		IconBillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_LightDirectional.png").wstring());
		IconBillboardComponent->SetSize(FVector2(0.7f, 0.7f));
		IconBillboardComponent->SetIgnoreParentScaleInRender(true);
		IconBillboardComponent->SetEditorVisualization(true);
		IconBillboardComponent->SetHiddenInGame(true);
	}
	UpdateBillboardTint();

	ArrowComponent = FObjectFactory::ConstructObject<UStaticMeshComponent>(this, "ArrowComponent");
	if (ArrowComponent)
	{
		AddOwnedComponent(ArrowComponent);
		ArrowComponent->AttachTo(IconBillboardComponent ? static_cast<USceneComponent*>(IconBillboardComponent) : static_cast<USceneComponent*>(DirectionalLightComponent));
		ArrowComponent->SetRelativeTransform(FTransform(
			FQuat::Identity,
			FVector(0.0f, 0.0f, 0.0f),
			FVector(0.02f, 0.02f, 0.02f)));
		ArrowComponent->SetStaticMesh(GetDirectionalLightArrowMesh());
		ArrowComponent->SetIgnoreParentScaleInRender(true);
		ArrowComponent->SetEditorVisualization(true);
		ArrowComponent->SetDrawDebugBounds(false);
	}

	AActor::PostSpawnInitialize();
}

void ADirectionalLightActor::OnOwnedComponentPropertyChanged(UActorComponent* ChangedComponent)
{
	AActor::OnOwnedComponentPropertyChanged(ChangedComponent);
	if (ChangedComponent == DirectionalLightComponent)
	{
		UpdateBillboardTint();
	}
}

void ADirectionalLightActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);

	if (!Ar.IsLoading())
	{
		return;
	}

	DirectionalLightComponent = GetComponentByClass<UDirectionalLightComponent>();
	IconBillboardComponent = nullptr;
	ArrowComponent = nullptr;
	for (UActorComponent* Component : GetComponents())
	{
		if (!Component)
		{
			continue;
		}

		if (!IconBillboardComponent && Component->IsA(UBillboardComponent::StaticClass()) &&
			(Component->GetName() == "BillboardComponent" || Component->GetName() == "IconBillboardComponent"))
		{
			IconBillboardComponent = static_cast<UBillboardComponent*>(Component);
			continue;
		}

		if (!ArrowComponent && Component->IsA(UStaticMeshComponent::StaticClass()) && Component->GetName() == "ArrowComponent")
		{
			ArrowComponent = static_cast<UStaticMeshComponent*>(Component);
		}
	}

	if (IconBillboardComponent)
	{
		IconBillboardComponent->DetachFromParent();
		if (DirectionalLightComponent)
		{
			IconBillboardComponent->AttachTo(DirectionalLightComponent);
		}
	}

	if (ArrowComponent)
	{
		ArrowComponent->DetachFromParent();
		if (IconBillboardComponent)
		{
			ArrowComponent->AttachTo(IconBillboardComponent);
		}
		else if (DirectionalLightComponent)
		{
			ArrowComponent->AttachTo(DirectionalLightComponent);
		}

		if (!ArrowComponent->GetStaticMesh())
		{
			ArrowComponent->SetStaticMesh(GetDirectionalLightArrowMesh());
		}
		ArrowComponent->SetDrawDebugBounds(false);
	}

	UpdateBillboardTint();
}

void ADirectionalLightActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	ADirectionalLightActor* DuplicatedActor = static_cast<ADirectionalLightActor*>(DuplicatedObject);
	DuplicatedActor->DirectionalLightComponent = Context.FindDuplicate(DirectionalLightComponent);
	DuplicatedActor->IconBillboardComponent = Context.FindDuplicate(IconBillboardComponent);
	DuplicatedActor->ArrowComponent = Context.FindDuplicate(ArrowComponent);
}

void ADirectionalLightActor::UpdateBillboardTint()
{
	if (!DirectionalLightComponent || !IconBillboardComponent)
	{
		return;
	}

	FVector4 Tint = DirectionalLightComponent->GetColor().ToVector4();
	Tint.W = 1.0f;
	IconBillboardComponent->SetBaseColor(Tint);
}
