#include "Component/ProjectileMovementComponent.h"
#include "Actor/Actor.h"
#include "World/World.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"
#include "Component/StaticMeshComponent.h"
#include "Object/ObjectFactory.h"
#include "Primitive/PrimitiveGizmo.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Mesh/MeshData.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include <cmath>

IMPLEMENT_RTTI(UProjectileMovementComponent, UMovementComponent)

namespace
{
	UStaticMesh* GetVelocityArrowMesh()
	{
		static TObjectPtr<UStaticMesh> ArrowMesh;
		if (ArrowMesh)
		{
			return ArrowMesh;
		}

		std::shared_ptr<FDynamicMesh> SourceMesh =
			FPrimitiveGizmo::CreateTranslationAxisMesh(EAxis::X, FVector4(0.f, 1.0f, 1.0f, 1.0f));
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

		ArrowMesh = FObjectFactory::ConstructObject<UStaticMesh>(nullptr, "VelocityArrowMesh");
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

void UProjectileMovementComponent::PostConstruct()
{
	UMovementComponent::PostConstruct();
	SetAutoStartSimulation(bAutoStartSimulation);
	EnsureVelocityArrowComponent();
	UpdateVelocityArrow();
}

void UProjectileMovementComponent::OnRegister()
{
	UMovementComponent::OnRegister();
	EnsureVelocityArrowComponent();
	UpdateVelocityArrow();
}

void UProjectileMovementComponent::BeginPlay()
{
	UMovementComponent::BeginPlay();
	EnsureVelocityArrowComponent();
	UpdateVelocityArrow();

	bSimulationEnabled = bAutoStartSimulation && IsComponentTickEnabled() && !Velocity.IsNearlyZero();
}

void UProjectileMovementComponent::OnPostLoad()
{
	UMovementComponent::OnPostLoad();
	SetAutoStartSimulation(bAutoStartSimulation);
	bSimulationEnabled = false;
	EnsureVelocityArrowComponent();
	UpdateVelocityArrow();
}

void UProjectileMovementComponent::SetVelocity(const FVector& InVelocity)
{
	Velocity = InVelocity;
	UpdateVelocityArrow();
}

void UProjectileMovementComponent::LaunchWithVelocity(const FVector& InVelocity)
{
	SetVelocity(InVelocity);
	StartSimulation();
}

void UProjectileMovementComponent::StartSimulation()
{
	SetComponentTickEnabled(true);
	bSimulationEnabled = !Velocity.IsNearlyZero();
}

void UProjectileMovementComponent::StopSimulation()
{
	bSimulationEnabled = false;
}

void UProjectileMovementComponent::SetAutoStartSimulation(bool bInAutoStartSimulation)
{
	bAutoStartSimulation = bInAutoStartSimulation;
	SetTickInEditor(bAutoStartSimulation);
	if (AActor* OwnerActor = GetOwner(); OwnerActor && bAutoStartSimulation)
	{
		OwnerActor->SetTickInEditor(true);
	}

	UpdateVelocityArrow();
}

void UProjectileMovementComponent::Tick(float DeltaTime)
{
	if (!bSimulationEnabled && bAutoStartSimulation && !Velocity.IsNearlyZero())
	{
		UWorld* World = GetOwner() ? GetOwner()->GetWorld() : nullptr;
		if (World && World->GetWorldType() == EWorldType::Editor)
		{
			bSimulationEnabled = IsComponentTickEnabled();
		}
	}

	if (!bSimulationEnabled)
	{
		return;
	}

	if (ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	Velocity.Z += GravityZ * GravityScale * DeltaTime;

	if (MaxSpeed > 0.0f)
	{
		const float SpeedSq = Velocity.X * Velocity.X + Velocity.Y * Velocity.Y + Velocity.Z * Velocity.Z;
		if (SpeedSq > MaxSpeed * MaxSpeed)
		{
			const float Scale = MaxSpeed / std::sqrt(SpeedSq);
			Velocity.X *= Scale;
			Velocity.Y *= Scale;
			Velocity.Z *= Scale;
		}
	}

	UpdateVelocityArrow();
	MoveUpdatedComponent(Velocity * DeltaTime);
}

void UProjectileMovementComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UMovementComponent::DuplicateShallow(DuplicatedObject, Context);

	UProjectileMovementComponent* Duplicated = static_cast<UProjectileMovementComponent*>(DuplicatedObject);
	Duplicated->Velocity = Velocity;
	Duplicated->GravityScale = GravityScale;
	Duplicated->MaxSpeed = MaxSpeed;
	Duplicated->bAutoStartSimulation = bAutoStartSimulation;
	Duplicated->bSimulationEnabled = false;
	Duplicated->VelocityArrowComponent = nullptr;
	Duplicated->SetAutoStartSimulation(Duplicated->bAutoStartSimulation);
}

void UProjectileMovementComponent::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	UMovementComponent::FixupDuplicatedReferences(DuplicatedObject, Context);

	UProjectileMovementComponent* Duplicated = static_cast<UProjectileMovementComponent*>(DuplicatedObject);
	Duplicated->VelocityArrowComponent = Context.FindDuplicate(VelocityArrowComponent.Get());
}

void UProjectileMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);

	Ar.Serialize("VelocityX", Velocity.X);
	Ar.Serialize("VelocityY", Velocity.Y);
	Ar.Serialize("VelocityZ", Velocity.Z);
	Ar.Serialize("GravityScale", GravityScale);
	Ar.Serialize("MaxSpeed", MaxSpeed);
	if (Ar.IsSaving())
	{
		Ar.Serialize("AutoStartSimulation", bAutoStartSimulation);
	}
	else if (Ar.Contains("AutoStartSimulation"))
	{
		Ar.Serialize("AutoStartSimulation", bAutoStartSimulation);
	}
}

void UProjectileMovementComponent::EnsureVelocityArrowComponent()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	if (VelocityArrowComponent == nullptr)
	{
		VelocityArrowComponent = FObjectFactory::ConstructObject<UStaticMeshComponent>(OwnerActor, "VelocityArrowComponent");
		if (!VelocityArrowComponent)
		{
			return;
		}

		OwnerActor->AddOwnedComponent(VelocityArrowComponent);
		VelocityArrowComponent->SetStaticMesh(GetVelocityArrowMesh());
		VelocityArrowComponent->SetIgnoreParentScaleInRender(true);
		VelocityArrowComponent->SetEditorVisualization(true);
		VelocityArrowComponent->SetInstanceComponent(IsInstanceComponent());
	}

	if (!EnsureUpdatedComponent() || VelocityArrowComponent == nullptr)
	{
		return;
	}

	if (VelocityArrowComponent->GetAttachParent() != UpdatedComponent)
	{
		VelocityArrowComponent->AttachTo(UpdatedComponent);
	}

	if (!VelocityArrowComponent->IsRegistered())
	{
		VelocityArrowComponent->OnRegister();
	}
}

void UProjectileMovementComponent::UpdateVelocityArrow()
{
	EnsureVelocityArrowComponent();
	if (VelocityArrowComponent == nullptr)
	{
		return;
	}

	FQuat LocalRotation = FQuat::Identity;
	if (!Velocity.IsNearlyZero())
	{
		FVector LocalDirection = Velocity.GetSafeNormal();
		if (USceneComponent* AttachParent = VelocityArrowComponent->GetAttachParent())
		{
			LocalDirection = FQuat(AttachParent->GetWorldTransform()).Inverse().RotateVector(LocalDirection);
		}

		LocalRotation = FQuat(FMatrix::MakeFromX(LocalDirection));
	}

	VelocityArrowComponent->SetRelativeTransform(FTransform(
		LocalRotation,
		FVector::ZeroVector,
		FVector(0.04f, 0.04f, 0.04f)));
}
