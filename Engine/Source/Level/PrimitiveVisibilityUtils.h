#pragma once

#include "Core/ShowFlags.h"

#include "Actor/DecalActor.h"
#include "Actor/Actor.h"
#include "Component/PrimitiveComponent.h"
#include "Component/ProjectileMovementComponent.h"
#include "Component/StaticMeshComponent.h"
#include "World/World.h"

inline bool IsArrowVisualizationPrimitive(UPrimitiveComponent* Primitive)
{
	if (!Primitive || Primitive->IsPendingKill())
	{
		return false;
	}

	if (AActor* Owner = Primitive->GetOwner())
	{
		if (Owner->IsA(ADecalActor::StaticClass()))
		{
			ADecalActor* DecalActor = static_cast<ADecalActor*>(Owner);
			if (Primitive == DecalActor->GetArrowComponent())
			{
				return true;
			}
		}

		for (UActorComponent* Component : Owner->GetComponents())
		{
			if (!Component || !Component->IsA(UProjectileMovementComponent::StaticClass()))
			{
				continue;
			}

			UProjectileMovementComponent* ProjectileMovementComponent = static_cast<UProjectileMovementComponent*>(Component);
			if (Primitive == ProjectileMovementComponent->GetVelocityArrowComponent())
			{
				return true;
			}
		}
	}

	return false;
}

inline bool IsHiddenByArrowVisualizationShowFlags(UPrimitiveComponent* Primitive, const FShowFlags& ShowFlags)
{
	if (!Primitive || Primitive->IsPendingKill())
	{
		return true;
	}

	if (!IsArrowVisualizationPrimitive(Primitive))
	{
		return false;
	}

	if (AActor* Owner = Primitive->GetOwner())
	{
		if (Owner->IsA(ADecalActor::StaticClass()))
		{
			return !ShowFlags.HasFlag(EEngineShowFlags::SF_DecalArrow);
		}

		return !ShowFlags.HasFlag(EEngineShowFlags::SF_ProjectileArrow);
	}

	return false;
}
