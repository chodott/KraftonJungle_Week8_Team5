#include "Renderer/Scene/SceneCommandSpriteBuilder.h"

#include "Renderer/Scene/SceneCommandBuilder.h"
#include "Renderer/Scene/SceneCommandBuilderUtils.h"

#include "Component/BillboardComponent.h"
#include "Component/SubUVComponent.h"

void FSceneCommandSpriteBuilder::BuildSubUVInputs(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket& Packet,
	const FViewContext& View,
	FSceneViewData& OutSceneViewData) const
{
	const FVector& CameraPosition = View.CameraPosition;

	TArray<const USubUVComponent*> ActiveSubUVComponents;
	ActiveSubUVComponents.reserve(Packet.SubUVPrimitives.size());

	for (const FSceneSubUVPrimitive& Primitive : Packet.SubUVPrimitives)
	{
		USubUVComponent* SubUVComponent = Primitive.Component;
		if (!SubUVComponent)
		{
			continue;
		}

		FRenderMesh* SubUVMesh = SubUVComponent->GetSubUVMesh();
		if (!SubUVMesh)
		{
			continue;
		}

		if (SubUVComponent->IsSubUVMeshDirty())
		{
			if (!BuildContext.SubUVFeature || !BuildContext.SubUVFeature->BuildMesh(SubUVComponent->GetSize(), *SubUVMesh))
			{
				continue;
			}

			SubUVMesh->bIsDirty = true;
			SubUVComponent->ClearSubUVMeshDirty();
		}

		FMaterial* SubUVMaterial = BuildContext.ResourceCache
			? BuildContext.ResourceCache->GetOrCreateSubUVMaterial(BuildContext, SubUVComponent)
			: nullptr;
		if (!SubUVMaterial && BuildContext.SubUVFeature)
		{
			SubUVMaterial = BuildContext.SubUVFeature->GetBaseMaterial();
		}
		if (!SubUVMaterial)
		{
			continue;
		}

		FMeshBatch Batch;
		Batch.Mesh = SubUVMesh;
		Batch.Material = SubUVMaterial;
		Batch.Domain = EMaterialDomain::Transparent;
		Batch.PassMask = static_cast<uint32>(EMeshPassMask::ForwardTransparent);
		Batch.bDisableDepthWrite = true;
		Batch.World = SubUVComponent->GetWorldTransform();

		if (SubUVComponent->IsBillboard())
		{
			const FVector WorldPosition = Batch.World.GetTranslation();
			const FVector Scale = Batch.World.GetScaleVector();
			Batch.World = FMatrix::MakeScale(Scale) * FMatrix::MakeBillboard(WorldPosition, CameraPosition);
		}

		const FVector WorldPosition = Batch.World.GetTranslation();
		Batch.DistanceSqToCamera = (WorldPosition - CameraPosition).SizeSquared();

		if (SceneCommandBuilderUtils::AddBatch(BuildContext, OutSceneViewData, std::move(Batch)))
		{
			ActiveSubUVComponents.push_back(SubUVComponent);
		}
	}

	if (BuildContext.ResourceCache)
	{
		BuildContext.ResourceCache->PruneStaleSubUVMaterials(ActiveSubUVComponents);
	}
}

void FSceneCommandSpriteBuilder::BuildBillboardInputs(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket& Packet,
	const FViewContext& View,
	FSceneViewData& OutSceneViewData) const
{
	const FVector& CameraPosition = View.CameraPosition;

	TArray<const UBillboardComponent*> ActiveBillboardComponents;
	ActiveBillboardComponents.reserve(Packet.BillboardPrimitives.size());

	for (const FSceneBillboardPrimitive& Primitive : Packet.BillboardPrimitives)
	{
		UBillboardComponent* BillboardComponent = Primitive.Component;
		if (!BillboardComponent)
		{
			continue;
		}

		FRenderMesh* BillboardMesh = BillboardComponent->GetBillboardMesh();
		if (!BillboardMesh || !BuildContext.BillboardFeature)
		{
			continue;
		}

		if (BillboardComponent->IsBillboardMeshDirty())
		{
			if (!BuildContext.BillboardFeature->BuildMesh(BillboardComponent->GetSize(), *BillboardMesh))
			{
				continue;
			}

			BillboardMesh->bIsDirty = true;
			BillboardComponent->ClearBillboardMeshDirty();
		}

		FMaterial* BillboardMaterial = BuildContext.BillboardFeature->GetOrCreateMaterial(*BillboardComponent);
		if (!BillboardMaterial)
		{
			continue;
		}

		FMeshBatch Batch;
		Batch.Mesh = BillboardMesh;
		Batch.Material = BillboardMaterial;
		Batch.Domain = BillboardComponent->IsEditorVisualization() ? EMaterialDomain::Overlay : EMaterialDomain::Transparent;
		Batch.PassMask = BillboardComponent->IsEditorVisualization()
			? static_cast<uint32>(EMeshPassMask::Overlay)
			: static_cast<uint32>(EMeshPassMask::ForwardTransparent);
		Batch.bDisableDepthWrite = true;

		const FVector WorldPosition = BillboardComponent->GetWorldTransform().GetTranslation();
		const FVector Scale = BillboardComponent->GetRenderWorldScale();
		Batch.World = FMatrix::MakeScale(Scale) * FMatrix::MakeBillboard(WorldPosition, CameraPosition);
		Batch.DistanceSqToCamera = (WorldPosition - CameraPosition).SizeSquared();

		if (SceneCommandBuilderUtils::AddBatch(BuildContext, OutSceneViewData, std::move(Batch)))
		{
			ActiveBillboardComponents.push_back(BillboardComponent);
		}
	}

	if (BuildContext.BillboardFeature)
	{
		BuildContext.BillboardFeature->PruneMaterials(ActiveBillboardComponents);
	}
}
