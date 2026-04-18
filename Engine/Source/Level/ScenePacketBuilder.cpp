#include "Level/ScenePacketBuilder.h"
#include "Level/PrimitiveVisibilityUtils.h"
#include "Actor/DecalActor.h"
#include "Component/BillboardComponent.h"
#include "Component/DecalComponent.h"
#include "Component/ProjectileMovementComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextComponent.h"
#include "Component/UUIDBillboardComponent.h"

bool FScenePacketBuilder::ShouldIncludePrimitive(UPrimitiveComponent* Primitive, const FShowFlags& ShowFlags) const
{
	if (!Primitive || Primitive->IsPendingKill())
	{
		return false;
	}

	if (AActor* Owner = Primitive->GetOwner())
	{
		if (!Owner->IsVisible())
		{
			return false;
		}

		if (UWorld* World = Owner->GetWorld())
		{
			const EWorldType WorldType = World->GetWorldType();
			const bool bIsPlayWorld = (WorldType == EWorldType::Game || WorldType == EWorldType::PIE);
			if (bIsPlayWorld)
			{
				if (Primitive->IsHiddenInGame())
				{
					return false;
				}
			}
		}

		if (Owner->IsA(ADecalActor::StaticClass()))
		{
			ADecalActor* DecalActor = static_cast<ADecalActor*>(Owner);
			if (Primitive == DecalActor->GetArrowComponent() && !ShowFlags.HasFlag(EEngineShowFlags::SF_DecalArrow))
			{
				return false;
			}

			if (UWorld* World = DecalActor->GetWorld())
			{
				const EWorldType WorldType = World->GetWorldType();
				const bool bIsPlayWorld = (WorldType == EWorldType::Game || WorldType == EWorldType::PIE);
				if (bIsPlayWorld && Primitive != DecalActor->GetDecalComponent())
				{
					return false;
				}
			}
		}

		if (IsArrowVisualizationPrimitive(Primitive))
		{
			return ShowFlags.HasFlag(EEngineShowFlags::SF_ProjectileArrow);
		}
	}

	const bool bIsUUID = Primitive->IsA(UUUIDBillboardComponent::StaticClass());
	const bool bIsSubUV = Primitive->IsA(USubUVComponent::StaticClass());
	const bool bIsText = Primitive->IsA(UTextRenderComponent::StaticClass());
	const bool bIsBillboard = Primitive->IsA(UBillboardComponent::StaticClass());
	const bool bIsDecal = Primitive->IsA(UDecalComponent::StaticClass());

	if (bIsUUID)
	{
		return ShowFlags.HasFlag(EEngineShowFlags::SF_UUID);
	}

	if (bIsSubUV || bIsBillboard)
	{
		return ShowFlags.HasFlag(EEngineShowFlags::SF_Billboard);
	}

	if (bIsText)
	{
		return ShowFlags.HasFlag(EEngineShowFlags::SF_Text);
	}

	if (bIsDecal)
	{
		return ShowFlags.HasFlag(EEngineShowFlags::SF_Decal);
	}

	if (!ShowFlags.HasFlag(EEngineShowFlags::SF_Primitives))
	{
		return false;
	}

	const bool bHasRenderMesh = (Primitive->GetRenderMesh() != nullptr);
	return bHasRenderMesh;
}

void FScenePacketBuilder::BuildScenePacket(
	const TArray<UPrimitiveComponent*>& VisiblePrimitives,
	const FShowFlags& ShowFlags,
	FSceneRenderPacket& OutPacket)
{
	OutPacket.Clear();
	OutPacket.Reserve(VisiblePrimitives.size());

	for (UPrimitiveComponent* Primitive : VisiblePrimitives)
	{
		if (!ShouldIncludePrimitive(Primitive, ShowFlags))
		{
			continue;
		}

		if (Primitive->IsA(UStaticMeshComponent::StaticClass()))
		{
			OutPacket.MeshPrimitives.push_back({ Primitive });
			continue;
		}

		if (Primitive->IsA(UTextRenderComponent::StaticClass()))
		{
			OutPacket.TextPrimitives.push_back({ static_cast<UTextRenderComponent*>(Primitive) });
			continue;
		}

		if (Primitive->IsA(USubUVComponent::StaticClass()))
		{
			OutPacket.SubUVPrimitives.push_back({ static_cast<USubUVComponent*>(Primitive) });
			continue;
		}

		if (Primitive->IsA(UBillboardComponent::StaticClass()))
		{
			OutPacket.BillboardPrimitives.push_back({ static_cast<UBillboardComponent*>(Primitive) });
			continue;
		}

		if (Primitive->IsA(UDecalComponent::StaticClass()))
		{
			OutPacket.DecalPrimitives.push_back({ static_cast<UDecalComponent*>(Primitive) });
			continue;
		}

		// Generic primitive path: includes editor visualization primitives.
		if (Primitive->GetRenderMesh() != nullptr)
		{
			OutPacket.MeshPrimitives.push_back({ Primitive });
		}
	}
}
