#include "Renderer/Scene/Builders/SceneCommandMeshBuilder.h"

#include "Renderer/Scene/Builders/SceneCommandBuilder.h"
#include "Renderer/Scene/Builders/SceneCommandBuilderUtils.h"

#include "Component/StaticMeshComponent.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/Resources/Material/Material.h"

#include <algorithm>
#include <cmath>

void FSceneCommandMeshBuilder::BuildMeshInputs(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket& Packet,
	FSceneViewData& OutSceneViewData) const
{
	for (const FSceneMeshPrimitive& Primitive : Packet.MeshPrimitives)
	{
		UStaticMeshComponent* MeshComponent = Primitive.Component;
		if (!MeshComponent)
		{
			continue;
		}

		const FMatrix WorldTransform = MeshComponent->GetRenderWorldTransform();
		const FBoxSphereBounds WorldBounds = MeshComponent->GetWorldBounds();
		FRenderMeshSelectionContext SelectionContext;
		SelectionContext.Distance = FVector::Dist(
			OutSceneViewData.View.CameraPosition,
			WorldBounds.Center);
		FRenderMesh* TargetMesh = MeshComponent->GetRenderMesh(SelectionContext);
		if (!TargetMesh)
		{
			continue;
		}

		const int32 SectionCount = TargetMesh->GetNumSection();
		if (SectionCount <= 0)
		{
			FMeshBatch Batch;
			Batch.Mesh = TargetMesh;
			Batch.World = WorldTransform;
			std::shared_ptr<FMaterial> Material = MeshComponent->GetMaterial(0);
			Batch.Material = Material ? Material.get() : BuildContext.DefaultMaterial;
			if (MeshComponent->IsEditorVisualization())
			{
				Batch.Domain = EMaterialDomain::EditorPrimitive;
				Batch.PassMask = static_cast<uint32>(EMeshPassMask::EditorPrimitive);
				Batch.bDisableDepthWrite = true;
			}
			else
			{
				Batch.Domain = EMaterialDomain::Opaque;
				Batch.PassMask =
					static_cast<uint32>(EMeshPassMask::DepthPrepass) |
					static_cast<uint32>(EMeshPassMask::GBuffer) |
					static_cast<uint32>(EMeshPassMask::ForwardOpaque);
			}
			SceneCommandBuilderUtils::AddBatch(BuildContext, OutSceneViewData, std::move(Batch));
			continue;
		}

		for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
		{
			const FMeshSection& Section = TargetMesh->Sections[SectionIndex];

			FMeshBatch Batch;
			Batch.Mesh = TargetMesh;
			Batch.World = WorldTransform;
			Batch.SectionIndex = static_cast<uint32>(SectionIndex);
			Batch.IndexStart = Section.StartIndex;
			Batch.IndexCount = Section.IndexCount;

			std::shared_ptr<FMaterial> Material = MeshComponent->GetMaterial(SectionIndex);
			Batch.Material = Material ? Material.get() : BuildContext.DefaultMaterial;
			if (MeshComponent->IsEditorVisualization())
			{
				Batch.Domain = EMaterialDomain::EditorPrimitive;
				Batch.PassMask = static_cast<uint32>(EMeshPassMask::EditorPrimitive);
				Batch.bDisableDepthWrite = true;
			}
			else
			{
				Batch.Domain = EMaterialDomain::Opaque;
				Batch.PassMask =
					static_cast<uint32>(EMeshPassMask::DepthPrepass) |
					static_cast<uint32>(EMeshPassMask::GBuffer) |
					static_cast<uint32>(EMeshPassMask::ForwardOpaque);
			}
			SceneCommandBuilderUtils::AddBatch(BuildContext, OutSceneViewData, std::move(Batch));
		}
	}
}
