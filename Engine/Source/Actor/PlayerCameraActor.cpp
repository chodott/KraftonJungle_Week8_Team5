#include "Actor/PlayerCameraActor.h"

#include "Camera/Camera.h"
#include "Component/CameraComponent.h"
#include "Component/SceneComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Math/Quat.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"
#include "Primitive/PrimitiveGizmo.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Mesh/MeshData.h"
#include "Serializer/Archive.h"

namespace
{
	UStaticMesh* GetPlayerCameraVisualizerMesh()
	{
		static TObjectPtr<UStaticMesh> VisualizerMesh;
		if (VisualizerMesh)
		{
			return VisualizerMesh;
		}

		std::shared_ptr<FDynamicMesh> SourceMesh =
			FPrimitiveGizmo::CreateTranslationAxisMesh(EAxis::X, FVector4(0.f, 1.f, 1.f, 1.0f));
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

		VisualizerMesh = FObjectFactory::ConstructObject<UStaticMesh>(nullptr, "PlayerCameraVisualizerMesh");
		if (!VisualizerMesh)
		{
			return nullptr;
		}

		VisualizerMesh->SetStaticMeshAsset(StaticRenderMesh.release());
		VisualizerMesh->LocalBounds.Radius = VisualizerMesh->GetRenderData()->GetLocalBoundRadius();
		VisualizerMesh->LocalBounds.Center = VisualizerMesh->GetRenderData()->GetCenterCoord();
		VisualizerMesh->LocalBounds.BoxExtent =
			(VisualizerMesh->GetRenderData()->GetMaxCoord() - VisualizerMesh->GetRenderData()->GetMinCoord()) * 0.5f;

		if (std::shared_ptr<FMaterial> GizmoMaterial = FMaterialManager::Get().FindByName("M_Gizmos"))
		{
			VisualizerMesh->AddDefaultMaterial(GizmoMaterial);
		}

		return VisualizerMesh;
	}
}

IMPLEMENT_RTTI(APlayerCameraActor, AActor)

namespace
{
	template <typename TComponent>
	TComponent* FindCameraActorComponentByName(const APlayerCameraActor* Actor, const char* ComponentName, TArray<UActorComponent*>* OutDuplicates = nullptr)
	{
		if (!Actor)
		{
			return nullptr;
		}

		TComponent* FoundComponent = nullptr;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component || !Component->IsA(TComponent::StaticClass()) || Component->GetName() != ComponentName)
			{
				continue;
			}

			if (!FoundComponent)
			{
				FoundComponent = static_cast<TComponent*>(Component);
				continue;
			}

			if (OutDuplicates)
			{
				OutDuplicates->push_back(Component);
			}
		}

		return FoundComponent;
	}
}

void APlayerCameraActor::PostSpawnInitialize()
{
	CameraComponent = FObjectFactory::ConstructObject<UCameraComponent>(this, "PlayerCameraComponent");
	AddOwnedComponent(CameraComponent);

	VisualizerComponent = FObjectFactory::ConstructObject<UStaticMeshComponent>(this, "PlayerCameraVisualizer");
	if (VisualizerComponent)
	{
		AddOwnedComponent(VisualizerComponent);
		VisualizerComponent->AttachTo(CameraComponent);
		VisualizerComponent->SetRelativeTransform(FTransform(
			FQuat::Identity,
			FVector(0.0f, 0.0f, 0.0f),
			FVector(0.05f, 0.05f, 0.05f)));
		VisualizerComponent->SetStaticMesh(GetPlayerCameraVisualizerMesh());
		VisualizerComponent->SetIgnoreParentScaleInRender(true);
		VisualizerComponent->SetEditorVisualization(true);
	}

	SyncCameraComponentState();

	AActor::PostSpawnInitialize();
}

void APlayerCameraActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);

	if (!Ar.IsLoading())
	{
		return;
	}

	TArray<UActorComponent*> DuplicateComponents;
	CameraComponent = FindCameraActorComponentByName<UCameraComponent>(this, "PlayerCameraComponent", &DuplicateComponents);
	VisualizerComponent = FindCameraActorComponentByName<UStaticMeshComponent>(this, "PlayerCameraVisualizer", &DuplicateComponents);

	for (UActorComponent* DuplicateComponent : DuplicateComponents)
	{
		if (!DuplicateComponent)
		{
			continue;
		}

		if (DuplicateComponent->IsA(USceneComponent::StaticClass()))
		{
			static_cast<USceneComponent*>(DuplicateComponent)->DetachFromParent();
		}

		RemoveOwnedComponent(DuplicateComponent);
	}

	if (VisualizerComponent)
	{
		VisualizerComponent->DetachFromParent();
		if (CameraComponent)
		{
			VisualizerComponent->AttachTo(CameraComponent);
		}
	}

	SyncCameraComponentState();
}

void APlayerCameraActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	APlayerCameraActor* DuplicatedActor = static_cast<APlayerCameraActor*>(DuplicatedObject);
	DuplicatedActor->CameraComponent = Context.FindDuplicate(CameraComponent);
	DuplicatedActor->VisualizerComponent = Context.FindDuplicate(VisualizerComponent);
}

void APlayerCameraActor::SyncCameraComponentState() const
{
	if (CameraComponent == nullptr)
	{
		return;
	}

	FCamera* Camera = CameraComponent->GetCamera();
	if (Camera == nullptr)
	{
		return;
	}

	const FTransform& ActorTransform = GetActorTransform();
	const FRotator ActorRotation = ActorTransform.Rotator();
	Camera->SetPosition(ActorTransform.GetLocation());
	Camera->SetRotation(ActorRotation.Yaw, ActorRotation.Pitch);
}
