#include "Renderer/Scene/Builders/SceneCommandMeshBuilder.h"

#include "Renderer/Scene/Builders/SceneCommandBuilder.h"
#include "Renderer/Scene/Builders/SceneCommandBuilderUtils.h"

#include "Actor/Actor.h"
#include "Component/MeshComponent.h"
#include "Component/LineBatchComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"

#include <algorithm>
#include <cmath>

namespace
{
	void ApplyLineBatchGizmoMaterialOverrides(FMaterial* Material)
	{
		if (!Material)
		{
			return;
		}

		const float White[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		Material->SetParameterData("BaseColor", White, sizeof(White));
	}
}

void FSceneCommandMeshBuilder::BuildMeshInputs(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket&        Packet,
	FSceneViewData&                  OutSceneViewData) const
{
	for (const FSceneMeshPrimitive& Primitive : Packet.MeshPrimitives)
	{
		UPrimitiveComponent* PrimitiveComponent = Primitive.Component;
		if (!PrimitiveComponent)
		{
			continue;
		}

		UMeshComponent* MeshComponent =
				PrimitiveComponent->IsA(UMeshComponent::StaticClass())
					? static_cast<UMeshComponent*>(PrimitiveComponent)
					: nullptr;

		const FMatrix               WorldTransform = PrimitiveComponent->GetRenderWorldTransform();
		const FBoxSphereBounds      WorldBounds    = PrimitiveComponent->GetWorldBounds();
		FRenderMeshSelectionContext SelectionContext;
		SelectionContext.Distance = FVector::Dist(
			OutSceneViewData.View.CameraPosition,
			WorldBounds.Center);
		FRenderMesh* TargetMesh = PrimitiveComponent->GetRenderMesh(SelectionContext);
		if (!TargetMesh)
		{
			continue;
		}

		const int32 SectionCount = TargetMesh->GetNumSection();
		if (SectionCount <= 0)
		{
			FMeshBatch Batch;
			Batch.Mesh               = TargetMesh;
			Batch.World              = WorldTransform;
			Batch.WorldBounds        = WorldBounds;
			Batch.SourceComponent    = PrimitiveComponent;
			Batch.DistanceSqToCamera = (WorldBounds.Center - OutSceneViewData.View.CameraPosition).SizeSquared();
			if (MeshComponent)
			{
				std::shared_ptr<FMaterial> Material = MeshComponent->GetMaterial(0);
				Batch.Material                      = Material ? Material.get() : BuildContext.DefaultMaterial;
			}
			else
			{
				Batch.Material = BuildContext.DefaultMaterial;
				if (PrimitiveComponent->IsEditorVisualization())
				{
					if (std::shared_ptr<FMaterial> GizmoMaterial = FMaterialManager::Get().FindByName("M_Gizmos"))
					{
						if (PrimitiveComponent->IsA(ULineBatchComponent::StaticClass()))
						{
							ApplyLineBatchGizmoMaterialOverrides(GizmoMaterial.get());
						}
						Batch.Material = GizmoMaterial.get();
					}
				}
			}
			if (PrimitiveComponent->IsEditorVisualization())
			{
				Batch.Domain   = EMaterialDomain::EditorPrimitive;
				Batch.PassMask = static_cast<uint32>(EMeshPassMask::EditorPrimitive);
				if (PrimitiveComponent->IsPickable())
				{
					Batch.PassMask |= static_cast<uint32>(EMeshPassMask::EditorPicking);
				}
				Batch.bDisableDepthWrite = true;
				Batch.bDisableDepthTest  = PrimitiveComponent->IsA(ULineBatchComponent::StaticClass());
			}
			else
			{
				const bool bCanPick  = PrimitiveComponent->IsPickable();
				FVector4   BaseColor = Batch.Material
					                     ? Batch.Material->GetVectorParameter("BaseColor")
					                     : FVector4(1.0f, 1.0f, 1.0f, 1.0f);
				const bool bIsTransparent = BaseColor.W < 1.0f;

				if (bIsTransparent)
				{
					Batch.Domain   = EMaterialDomain::Transparent;
					Batch.PassMask = static_cast<uint32>(EMeshPassMask::ForwardTransparent);
					if (bCanPick)
					{
						Batch.PassMask |= static_cast<uint32>(EMeshPassMask::EditorPicking);
					}
					Batch.bDisableDepthWrite = true;
				}
				else
				{
					Batch.Domain   = EMaterialDomain::Opaque;
					Batch.PassMask =
							static_cast<uint32>(EMeshPassMask::DepthPrepass) |
							static_cast<uint32>(EMeshPassMask::GBuffer) |
							static_cast<uint32>(EMeshPassMask::ForwardOpaque) |
							static_cast<uint32>(EMeshPassMask::ShadowVSM);
					if (bCanPick)
					{
						Batch.PassMask |= static_cast<uint32>(EMeshPassMask::EditorPicking);
					}
				}
			}
			SceneCommandBuilderUtils::AddBatch(BuildContext, OutSceneViewData, std::move(Batch));
			continue;
		}

		for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
		{
			const FMeshSection& Section = TargetMesh->Sections[SectionIndex];

			FMeshBatch Batch;
			Batch.Mesh               = TargetMesh;
			Batch.World              = WorldTransform;
			Batch.WorldBounds        = WorldBounds;
			Batch.SourceComponent    = PrimitiveComponent;
			Batch.DistanceSqToCamera = (WorldBounds.Center - OutSceneViewData.View.CameraPosition).SizeSquared();
			Batch.SectionIndex       = static_cast<uint32>(SectionIndex);
			Batch.IndexStart         = Section.StartIndex;
			Batch.IndexCount         = Section.IndexCount;

			if (MeshComponent)
			{
				std::shared_ptr<FMaterial> Material = MeshComponent->GetMaterial(SectionIndex);
				Batch.Material                      = Material ? Material.get() : BuildContext.DefaultMaterial;
			}
			else
			{
				Batch.Material = BuildContext.DefaultMaterial;
				if (PrimitiveComponent->IsEditorVisualization())
				{
					if (std::shared_ptr<FMaterial> GizmoMaterial = FMaterialManager::Get().FindByName("M_Gizmos"))
					{
						if (PrimitiveComponent->IsA(ULineBatchComponent::StaticClass()))
						{
							ApplyLineBatchGizmoMaterialOverrides(GizmoMaterial.get());
						}
						Batch.Material = GizmoMaterial.get();
					}
				}
			}
			if (PrimitiveComponent->IsEditorVisualization())
			{
				Batch.Domain   = EMaterialDomain::EditorPrimitive;
				Batch.PassMask = static_cast<uint32>(EMeshPassMask::EditorPrimitive);
				if (PrimitiveComponent->IsPickable())
				{
					Batch.PassMask |= static_cast<uint32>(EMeshPassMask::EditorPicking);
				}
				Batch.bDisableDepthWrite = true;
				Batch.bDisableDepthTest  = PrimitiveComponent->IsA(ULineBatchComponent::StaticClass());
			}
			else
			{
				const bool bCanPick  = PrimitiveComponent->IsPickable();
				FVector4   BaseColor = Batch.Material
					                     ? Batch.Material->GetVectorParameter("BaseColor")
					                     : FVector4(1.0f, 1.0f, 1.0f, 1.0f);
				const bool bIsTransparent = BaseColor.W < 1.0f;

				if (bIsTransparent)
				{
					Batch.Domain   = EMaterialDomain::Transparent;
					Batch.PassMask = static_cast<uint32>(EMeshPassMask::ForwardTransparent);
					if (bCanPick)
					{
						Batch.PassMask |= static_cast<uint32>(EMeshPassMask::EditorPicking);
					}
					Batch.bDisableDepthWrite = true;
				}
				else
				{
					Batch.Domain   = EMaterialDomain::Opaque;
					Batch.PassMask =
							static_cast<uint32>(EMeshPassMask::DepthPrepass) |
							static_cast<uint32>(EMeshPassMask::GBuffer) |
							static_cast<uint32>(EMeshPassMask::ForwardOpaque) |
							static_cast<uint32>(EMeshPassMask::ShadowVSM);
					if (bCanPick)
					{
						Batch.PassMask |= static_cast<uint32>(EMeshPassMask::EditorPicking);
					}
				}
			}
			SceneCommandBuilderUtils::AddBatch(BuildContext, OutSceneViewData, std::move(Batch));
		}
	}
}
