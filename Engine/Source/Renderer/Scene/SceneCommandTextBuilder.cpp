#include "Renderer/Scene/SceneCommandTextBuilder.h"

#include "Renderer/Scene/SceneCommandBuilder.h"
#include "Renderer/Scene/SceneCommandBuilderUtils.h"

#include "Component/TextComponent.h"
#include "Component/UUIDBillboardComponent.h"

void FSceneCommandTextBuilder::BuildTextInputs(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket& Packet,
	const FViewContext& View,
	FSceneViewData& OutSceneViewData) const
{
	const FVector& CameraPosition = View.CameraPosition;

	for (const FSceneTextPrimitive& Primitive : Packet.TextPrimitives)
	{
		UTextRenderComponent* TextComponent = Primitive.Component;
		if (!TextComponent)
		{
			continue;
		}

		FRenderMesh* TextMesh = TextComponent->GetRenderMesh();
		if (!TextMesh)
		{
			continue;
		}

		if (TextComponent->IsTextMeshDirty())
		{
			if (BuildContext.TextFeature && BuildContext.TextFeature->BuildMesh(
				TextComponent->GetDisplayText(),
				*TextMesh,
				1.0f,
				TextComponent->GetHorizontalAlignment(),
				TextComponent->GetVerticalAlignment()))
			{
				TextMesh->bIsDirty = true;
				TextComponent->ClearTextMeshDirty();
			}
		}

		if (TextMesh->Vertices.empty())
		{
			continue;
		}

		FMaterial* TextMaterial = BuildContext.ResourceCache
			? BuildContext.ResourceCache->GetOrCreateTextMaterial(BuildContext, TextComponent->GetTextColor())
			: nullptr;
		if (!TextMaterial && BuildContext.TextFeature)
		{
			TextMaterial = BuildContext.TextFeature->GetBaseMaterial();
		}
		if (!TextMaterial)
		{
			continue;
		}

		FMeshBatch Batch;
		Batch.Mesh = TextMesh;
		Batch.Material = TextMaterial;
		Batch.Domain = TextComponent->IsA(UUUIDBillboardComponent::StaticClass()) ? EMaterialDomain::Overlay : EMaterialDomain::Opaque;
		Batch.PassMask = Batch.Domain == EMaterialDomain::Overlay
			? static_cast<uint32>(EMeshPassMask::Overlay)
			: static_cast<uint32>(EMeshPassMask::ForwardOpaque);
		if (Batch.Domain == EMaterialDomain::Overlay)
		{
			Batch.bDisableDepthTest = true;
			Batch.bDisableDepthWrite = true;
		}

		const FVector WorldPosition = TextComponent->GetRenderWorldPosition();
		const FVector WorldScale = TextComponent->GetRenderWorldScale();
		if (TextComponent->IsBillboard())
		{
			Batch.World = FMatrix::MakeScale(WorldScale) * FMatrix::MakeBillboard(WorldPosition, CameraPosition);
		}
		else
		{
			const float TextScale = TextComponent->GetTextScale();
			Batch.World = FMatrix::MakeScale(FVector(TextScale, TextScale, TextScale)) * TextComponent->GetWorldTransform();
		}

		SceneCommandBuilderUtils::AddBatch(BuildContext, OutSceneViewData, std::move(Batch));
	}
}
