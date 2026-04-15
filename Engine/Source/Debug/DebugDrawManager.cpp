#include "DebugDrawManager.h"

#include "Actor/Actor.h"
#include "Component/PrimitiveComponent.h"
#include "Component/DecalComponent.h"
#include "Component/LocalHeightFogComponent.h"
#include "Core/ShowFlags.h"
#include "Level/PrimitiveVisibilityUtils.h"
#include "Object/Class.h"
#include "World/World.h"

FWorldDebugDrawBucket* FDebugDrawManager::FindOrAddBucket(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	return &WorldBuckets[World];
}

const FWorldDebugDrawBucket* FDebugDrawManager::FindBucket(UWorld* World) const
{
	if (!World)
	{
		return nullptr;
	}

	const auto Found = WorldBuckets.find(World);
	return (Found != WorldBuckets.end()) ? &Found->second : nullptr;
}


void FDebugDrawManager::DrawLine(UWorld* World, const FVector& Start, const FVector& End, const FVector4& Color)
{
	if (FWorldDebugDrawBucket* Bucket = FindOrAddBucket(World))
	{
		Bucket->Lines.push_back({ Start, End, Color });
	}
}

void FDebugDrawManager::DrawCube(UWorld* World, const FVector& Center, const FVector& Extent, const FVector4& Color)
{
	if (FWorldDebugDrawBucket* Bucket = FindOrAddBucket(World))
	{
		Bucket->Cubes.push_back({ Center, Extent, Color });
	}
}

void FDebugDrawManager::BuildPrimitiveList(
	const FShowFlags& ShowFlags,
	UWorld* World,
	FDebugPrimitiveList& OutPrimitives) const
{
	OutPrimitives.Clear();
	if (!ShowFlags.HasFlag(EEngineShowFlags::SF_DebugDraw))
	{
		return;
	}

	if (ShowFlags.HasFlag(EEngineShowFlags::SF_Collision) && World)
	{
		DrawAllCollisionBounds(ShowFlags, World, OutPrimitives);
	}

	if (const FWorldDebugDrawBucket* Bucket = FindBucket(World))
	{
		OutPrimitives.Cubes.insert(OutPrimitives.Cubes.end(), Bucket->Cubes.begin(), Bucket->Cubes.end());
		OutPrimitives.Lines.insert(OutPrimitives.Lines.end(), Bucket->Lines.begin(), Bucket->Lines.end());
	}

}

void FDebugDrawManager::ReleaseWorld(UWorld* World)
{
	if (!World)
	{
		return;
	}

	WorldBuckets.erase(World);
}

void FDebugDrawManager::Clear()
{
	WorldBuckets.clear();
}

void FDebugDrawManager::DrawAllCollisionBounds(const FShowFlags& ShowFlags, UWorld* World, FDebugPrimitiveList& OutPrimitives) const
{
	// 충돌 디버그 가시화는 월드의 프리미티브 컴포넌트를 순회하며 바운드를 선분으로 바꾼다.
	TArray<AActor*> AllActors = World->GetAllActors();
	for (AActor* Actor : AllActors)
	{
		if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible())
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component || !Component->IsA(UPrimitiveComponent::StaticClass()))
			{
				continue;
			}

			UPrimitiveComponent* PrimitiveComponent = static_cast<UPrimitiveComponent*>(Component);
			const bool bIsLocalFogComponent = PrimitiveComponent->IsA(ULocalHeightFogComponent::StaticClass());
			if (!bIsLocalFogComponent && !PrimitiveComponent->ShouldDrawDebugBounds())
			{
				continue;
			}

			if (IsArrowVisualizationPrimitive(PrimitiveComponent) || IsHiddenByArrowVisualizationShowFlags(PrimitiveComponent, ShowFlags))
			{
				continue;
			}

			if (PrimitiveComponent->IsA(ULocalHeightFogComponent::StaticClass()))
			{
				const ULocalHeightFogComponent* LocalFogComponent = static_cast<const ULocalHeightFogComponent*>(PrimitiveComponent);
				const FTransform FogTransform = FTransform(LocalFogComponent->GetWorldTransform());
				const FVector Extent = LocalFogComponent->FogExtents;
				const FVector Signs[8] =
				{
					FVector(-Extent.X, -Extent.Y, -Extent.Z), FVector(Extent.X, -Extent.Y, -Extent.Z),
					FVector(-Extent.X, Extent.Y, -Extent.Z),  FVector(Extent.X, Extent.Y, -Extent.Z),
					FVector(-Extent.X, -Extent.Y, Extent.Z),  FVector(Extent.X, -Extent.Y, Extent.Z),
					FVector(-Extent.X, Extent.Y, Extent.Z),   FVector(Extent.X, Extent.Y, Extent.Z)
				};
				const int Edges[12][2] =
				{
					{0,1},{1,3},{3,2},{2,0},
					{4,5},{5,7},{7,6},{6,4},
					{0,4},{1,5},{2,6},{3,7}
				};
				FVector WorldCorners[8];
				for (int i = 0; i < 8; ++i)
				{
					WorldCorners[i] = FogTransform.TransformPosition(Signs[i]);
				}
				for (const auto& Edge : Edges)
				{
					OutPrimitives.Lines.push_back({ WorldCorners[Edge[0]], WorldCorners[Edge[1]], FVector4(1.0f, 0.2f, 1.0f, 1.0f) });
				}
				continue;
			}

			const FBoxSphereBounds Bounds = PrimitiveComponent->GetWorldBounds();
			if (Bounds.BoxExtent.SizeSquared() > 0.0f)
			{
				const FVector4 Color = FVector4(1.0f, 0.2f, 1.0f, 1.0f); // Magenta: Picking Bounds / generic collision bounds
				OutPrimitives.Cubes.push_back({ Bounds.Center, Bounds.BoxExtent, Color });
			}
		}
	}
}
