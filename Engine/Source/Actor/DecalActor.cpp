#include "Actor/DecalActor.h"

#include "Component/BillboardComponent.h"
#include "Component/DecalComponent.h"
#include "Component/SceneComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Core/Paths.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"
#include "Primitive/PrimitiveGizmo.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Mesh/MeshData.h"
#include "Serializer/Archive.h"

namespace
{
	UStaticMesh* GetDecalArrowMesh()
	{
		static TObjectPtr<UStaticMesh> ArrowMesh;
		if (ArrowMesh)
		{
			return ArrowMesh;
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

		ArrowMesh = FObjectFactory::ConstructObject<UStaticMesh>(nullptr, "DecalArrowMesh");
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

IMPLEMENT_RTTI(ADecalActor, AActor)

void ADecalActor::PostSpawnInitialize()
{
	bTickInEditor = true;

	DecalComponent = FObjectFactory::ConstructObject<UDecalComponent>(this, "DecalComponent");
	AddOwnedComponent(DecalComponent);

	BillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "BillboardComponent");
	if (BillboardComponent)
	{
		AddOwnedComponent(BillboardComponent);
		BillboardComponent->AttachTo(DecalComponent);
		BillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_DecalActorIcon.png").wstring());
		BillboardComponent->SetSize(FVector2(0.5f, 0.5f));
		BillboardComponent->SetIgnoreParentScaleInRender(true);
		BillboardComponent->SetEditorVisualization(true);
		BillboardComponent->SetHiddenInGame(true);
	}

	ArrowComponent = FObjectFactory::ConstructObject<UStaticMeshComponent>(this, "ArrowComponent");
	if (ArrowComponent)
	{
		AddOwnedComponent(ArrowComponent);
		ArrowComponent->AttachTo(BillboardComponent);
		ArrowComponent->SetRelativeTransform(FTransform(
			FQuat::Identity,
			FVector(0.0f, 0.0f, 0.0f),
			FVector(0.02f, 0.02f, 0.02f)));
		ArrowComponent->SetStaticMesh(GetDecalArrowMesh());
		ArrowComponent->SetIgnoreParentScaleInRender(true);
		ArrowComponent->SetEditorVisualization(true);
	}

	AActor::PostSpawnInitialize();
}

void ADecalActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);

	if (!Ar.IsLoading())
	{
		return;
	}

	DecalComponent = GetComponentByClass<UDecalComponent>();

	BillboardComponent = nullptr;
	ArrowComponent = nullptr;
	for (UActorComponent* Component : GetComponents())
	{
		if (!Component)
		{
			continue;
		}

		if (!BillboardComponent && Component->IsA(UBillboardComponent::StaticClass()) && Component->GetName() == "BillboardComponent")
		{
			BillboardComponent = static_cast<UBillboardComponent*>(Component);
			continue;
		}

		if (!ArrowComponent && Component->IsA(UStaticMeshComponent::StaticClass()) && Component->GetName() == "ArrowComponent")
		{
			ArrowComponent = static_cast<UStaticMeshComponent*>(Component);
		}
	}

	if (BillboardComponent)
	{
		BillboardComponent->DetachFromParent();
		if (DecalComponent)
		{
			BillboardComponent->AttachTo(DecalComponent);
		}
	}

	if (ArrowComponent)
	{
		ArrowComponent->DetachFromParent();
		if (BillboardComponent)
		{
			ArrowComponent->AttachTo(BillboardComponent);
		}

		// 런타임 생성 메쉬는 파일 경로가 없어 Serialize로 복원되지 않으므로 직접 재적용한다.
		if (!ArrowComponent->GetStaticMesh())
		{
			ArrowComponent->SetStaticMesh(GetDecalArrowMesh());
		}
	}
}

void ADecalActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	ADecalActor* DuplicatedActor = static_cast<ADecalActor*>(DuplicatedObject);
	DuplicatedActor->DecalComponent = Context.FindDuplicate(DecalComponent);
	DuplicatedActor->BillboardComponent = Context.FindDuplicate(BillboardComponent);
	DuplicatedActor->ArrowComponent = Context.FindDuplicate(ArrowComponent);

}
