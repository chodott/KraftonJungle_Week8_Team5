#include "Renderer/Scene/SceneCommandMeshBuilder.h"

#include "Renderer/Scene/SceneCommandBuilder.h"
#include "Renderer/Scene/SceneCommandBuilderUtils.h"

#include "Component/StaticMeshComponent.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/Resources/Material/Material.h"

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

		FRenderMesh* TargetMesh = MeshComponent->GetRenderMesh();
		if (!TargetMesh)
		{
			continue;
		}

		const int32 SectionCount = TargetMesh->GetNumSection();
		if (SectionCount <= 0)
		{
			FMeshBatch Batch;
			Batch.Mesh = TargetMesh;
			Batch.World = MeshComponent->GetRenderWorldTransform();
			std::shared_ptr<FMaterial> Material = MeshComponent->GetMaterial(0);
			Batch.Material = Material ? Material.get() : BuildContext.DefaultMaterial;
			if (MeshComponent->IsEditorVisualization())
			{
				Batch.Domain = EMaterialDomain::Overlay;
				Batch.PassMask = static_cast<uint32>(EMeshPassMask::Overlay);
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
			Batch.World = MeshComponent->GetRenderWorldTransform();
			Batch.SectionIndex = static_cast<uint32>(SectionIndex);
			Batch.IndexStart = Section.StartIndex;
			Batch.IndexCount = Section.IndexCount;

			std::shared_ptr<FMaterial> Material = MeshComponent->GetMaterial(SectionIndex);
			Batch.Material = Material ? Material.get() : BuildContext.DefaultMaterial;
			if (MeshComponent->IsEditorVisualization())
			{
				Batch.Domain = EMaterialDomain::Overlay;
				Batch.PassMask = static_cast<uint32>(EMeshPassMask::Overlay);
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
