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

#include <algorithm>

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
			FPrimitiveGizmo::CreateTranslationAxisMesh(EAxis::X, FVector4(1.f, 0.f, 0.f, 1.0f));
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

	IconBillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "IconBillboardComponent");
	if (IconBillboardComponent)
	{
		AddOwnedComponent(IconBillboardComponent);
		IconBillboardComponent->AttachTo(DecalComponent);
		IconBillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_DecalActorIcon.png").wstring());
		IconBillboardComponent->SetSize(FVector2(0.5f, 0.5f));
		IconBillboardComponent->SetIgnoreParentScaleInRender(true);
		IconBillboardComponent->SetEditorVisualization(true);
		IconBillboardComponent->SetHiddenInGame(true);
	}

	ArrowComponent = FObjectFactory::ConstructObject<UStaticMeshComponent>(this, "ArrowComponent");
	if (ArrowComponent)
	{
		AddOwnedComponent(ArrowComponent);
		ArrowComponent->AttachTo(IconBillboardComponent);
		ArrowComponent->SetRelativeTransform(FTransform(
			FQuat::Identity,
			FVector(0.0f, 0.0f, 0.0f),
			FVector(0.02f, 0.02f, 0.02f)));
		ArrowComponent->SetStaticMesh(GetDecalArrowMesh());
		ArrowComponent->SetIgnoreParentScaleInRender(true);
		ArrowComponent->SetEditorVisualization(true);
		ArrowComponent->SetDrawDebugBounds(false);
	}

	//UpdateArrowVisualization();
	AActor::PostSpawnInitialize();
}

//void ADecalActor::Tick(float DeltaTime)
//{
//	AActor::Tick(DeltaTime);
//	UpdateArrowVisualization();
//}

void ADecalActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);

	if (!Ar.IsLoading())
	{
		return;
	}

	DecalComponent = GetComponentByClass<UDecalComponent>();

	IconBillboardComponent = nullptr;
	ArrowComponent = nullptr;
	for (UActorComponent* Component : GetComponents())
	{
		if (!Component)
		{
			continue;
		}

		if (!IconBillboardComponent && Component->IsA(UBillboardComponent::StaticClass()) && (Component->GetName() == "IconBillboardComponent" || Component->GetName() == "BillboardComponent"))
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
		if (DecalComponent)
		{
			IconBillboardComponent->AttachTo(DecalComponent);
		}
	}

	if (ArrowComponent)
	{
		ArrowComponent->DetachFromParent();
		if (IconBillboardComponent)
		{
			ArrowComponent->AttachTo(IconBillboardComponent);
		}

		// 런타임 생성 메쉬는 파일 경로가 없어 Serialize로 복원되지 않으므로 직접 재적용한다.
		if (!ArrowComponent->GetStaticMesh())
		{
			ArrowComponent->SetStaticMesh(GetDecalArrowMesh());
		}
		ArrowComponent->SetDrawDebugBounds(false);
	}

	// UpdateArrowVisualization();
}

void ADecalActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	ADecalActor* DuplicatedActor = static_cast<ADecalActor*>(DuplicatedObject);
	DuplicatedActor->DecalComponent = Context.FindDuplicate(DecalComponent);
	DuplicatedActor->IconBillboardComponent = Context.FindDuplicate(IconBillboardComponent);
	DuplicatedActor->ArrowComponent = Context.FindDuplicate(ArrowComponent);
	// DuplicatedActor->UpdateArrowVisualization();
}

//void ADecalActor::UpdateArrowVisualization()
//{
//	if (!DecalComponent || !ArrowComponent)
//	{
//		return;
//	}
//
//	UStaticMesh* ArrowMesh = ArrowComponent->GetStaticMesh();
//	if (!ArrowMesh)
//	{
//		ArrowMesh = GetDecalArrowMesh();
//		ArrowComponent->SetStaticMesh(ArrowMesh);
//	}
//
//	const float BaseArrowLength =
//		(ArrowMesh && ArrowMesh->LocalBounds.BoxExtent.X > 0.0f)
//		? (ArrowMesh->LocalBounds.BoxExtent.X * 2.0f)
//		: 1.0f;
//	const float BaseArrowRadius =
//		(ArrowMesh && ArrowMesh->LocalBounds.BoxExtent.Y > 0.0f && ArrowMesh->LocalBounds.BoxExtent.Z > 0.0f)
//		? (std::max)(ArrowMesh->LocalBounds.BoxExtent.Y, ArrowMesh->LocalBounds.BoxExtent.Z)
//		: 1.0f;
//
//	const FVector DecalExtents = DecalComponent->GetExtents();
//	const float ProjectionDepth = (std::max)(DecalComponent->GetProjectionDepth(), 1.0f);
//	const float ArrowLengthScale = ProjectionDepth / BaseArrowLength;
//
//	float DesiredArrowRadius = ProjectionDepth * 0.05f;
//	const float CrossSectionRadius = (std::max)(DecalExtents.Y, DecalExtents.Z);
//	if (CrossSectionRadius > 0.0f)
//	{
//		const float MinRadius = CrossSectionRadius * 0.12f;
//		const float MaxRadius = CrossSectionRadius * 0.35f;
//		DesiredArrowRadius = std::clamp(DesiredArrowRadius, MinRadius, (std::max)(MinRadius, MaxRadius));
//	}
//
//	const float ArrowRadiusScale = DesiredArrowRadius / BaseArrowRadius;
//
//	ArrowComponent->SetRelativeTransform(FTransform(
//		FQuat::Identity,
//		FVector(0.0f, 0.0f, 0.0f),
//		FVector(ArrowLengthScale, ArrowRadiusScale, ArrowRadiusScale)));
//}
