#include "Level/Level.h"

#include "Core/Paths.h"
#include "Actor/Actor.h"
#include "Actor/AmbientLightActor.h"
#include "Actor/DirectionalLightActor.h"
#include "Camera/Camera.h"
#include "Component/BillboardComponent.h"
#include "Component/CameraComponent.h"
#include "Object/ObjectFactory.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Component/UUIDBillboardComponent.h"
#include "Level/PrimitiveVisibilityUtils.h"
#include "Object/Class.h"

#include "Serializer/SceneSerializer.h"
#include "World/World.h"
#include <algorithm>



#include "Component/LineBatchComponent.h"

IMPLEMENT_RTTI(ULevel, UObject)

namespace
{
	constexpr const char* PlayerStartNamePrefix = "PlayerStart";
	constexpr const char* PlayerStartRootComponentName = "PlayerStartRootComponent";
	constexpr const char* PlayerStartBillboardComponentName = "PlayerStartBillboardComponent";

	bool IsPlayerStartActor(const AActor* Actor)
	{
		if (!Actor || Actor->IsPendingDestroy())
		{
			return false;
		}

		const FString& ActorName = Actor->GetName();
		if (!ActorName.empty() && ActorName.rfind(PlayerStartNamePrefix, 0) == 0)
		{
			return true;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component || !Component->IsA(UBillboardComponent::StaticClass()))
			{
				continue;
			}

			if (Component->GetName() == PlayerStartBillboardComponentName)
			{
				return true;
			}
		}

		return false;
	}
}

ULevel::~ULevel()
{
	for (AActor* Actor : Actors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	Actors.clear();
	SpatialBVH.Reset();
	DebugSpatialBVH.Reset();
	bSpatialDirty = true;
}


FCamera* ULevel::GetCamera() const
{
	UWorld* World = GetTypedOuter<UWorld>();
	return World ? World->GetCamera() : nullptr;
}

EWorldType ULevel::GetWorldType() const
{
	UWorld* World = GetTypedOuter<UWorld>();
	return World ? World->GetWorldType() : EWorldType::Game;
}

bool ULevel::IsEditorScene() const
{
	return GetWorldType() == EWorldType::Editor;
}

bool ULevel::IsGameScene() const
{
	const EWorldType WorldType = GetWorldType();
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

const FLevelGameplaySettings& ULevel::GetGameplaySettings() const
{
	return GameplaySettings;
}

void ULevel::SetGameplaySettings(const FLevelGameplaySettings& InSettings)
{
	GameplaySettings = InSettings;
}

void ULevel::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	ULevel* DuplicatedLevel = static_cast<ULevel*>(DuplicatedObject);
	DuplicatedLevel->Actors.clear();
	DuplicatedLevel->GameplaySettings = GameplaySettings;
	DuplicatedLevel->SpatialBVH.Reset();
	DuplicatedLevel->DebugSpatialBVH.Reset();
	DuplicatedLevel->bSpatialDirty = true;
}

void ULevel::DuplicateSubObjects(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	ULevel* DuplicatedLevel = static_cast<ULevel*>(DuplicatedObject);

	for (AActor* Actor : Actors)
	{
		if (!Actor || Actor->IsPendingKill() || Actor->IsPendingDestroy())
		{
			continue;
		}

		AActor* DuplicatedActor = static_cast<AActor*>(Actor->Duplicate(DuplicatedLevel, Actor->GetName(), Context));
		if (!DuplicatedActor)
		{
			continue;
		}

		DuplicatedLevel->RegisterActor(DuplicatedActor);
	}
}

void ULevel::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	for (AActor* Actor : Actors)
	{
		if (!Actor || Actor->IsPendingKill() || Actor->IsPendingDestroy())
		{
			continue;
		}

		if (AActor* DuplicatedActor = Context.FindDuplicate(Actor))
		{
			Actor->FixupDuplicatedReferences(DuplicatedActor, Context);
		}
	}
}

void ULevel::PostDuplicate(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	ULevel* DuplicatedLevel = static_cast<ULevel*>(DuplicatedObject);

	for (AActor* Actor : Actors)
	{
		if (!Actor || Actor->IsPendingKill() || Actor->IsPendingDestroy())
		{
			continue;
		}

		if (AActor* DuplicatedActor = Context.FindDuplicate(Actor))
		{
			Actor->PostDuplicate(DuplicatedActor, Context);
		}
	}

	DuplicatedLevel->MarkSpatialDirty();
}



void ULevel::ClearActors()
{
	for (AActor* Actor : Actors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	CleanupDestroyedActors();

	MarkSpatialDirty();
}

AActor* ULevel::FindPlayerStartActor() const
{
	for (AActor* Actor : Actors)
	{
		if (IsPlayerStartActor(Actor))
		{
			return Actor;
		}
	}

	return nullptr;
}

int32 ULevel::GetPlayerStartActorCount() const
{
	int32 Count = 0;
	for (AActor* Actor : Actors)
	{
		if (IsPlayerStartActor(Actor))
		{
			++Count;
		}
	}

	return Count;
}

AActor* ULevel::EnsurePlayerStartActor()
{
	AActor* PrimaryPlayerStart = nullptr;
	TArray<AActor*> RedundantPlayerStarts;
	for (AActor* Actor : Actors)
	{
		if (!IsPlayerStartActor(Actor))
		{
			continue;
		}

		if (!PrimaryPlayerStart)
		{
			PrimaryPlayerStart = Actor;
		}
		else
		{
			RedundantPlayerStarts.push_back(Actor);
		}
	}

	for (AActor* RedundantActor : RedundantPlayerStarts)
	{
		DestroyActor(RedundantActor);
	}

	if (PrimaryPlayerStart)
	{
		return PrimaryPlayerStart;
	}

	PrimaryPlayerStart = SpawnActor<AActor>(PlayerStartNamePrefix);
	if (!PrimaryPlayerStart)
	{
		return nullptr;
	}

	USceneComponent* PlayerStartRootComponent = FObjectFactory::ConstructObject<USceneComponent>(
		PrimaryPlayerStart, PlayerStartRootComponentName);
	if (PlayerStartRootComponent)
	{
		PrimaryPlayerStart->AddOwnedComponent(PlayerStartRootComponent);
		PrimaryPlayerStart->SetRootComponent(PlayerStartRootComponent);
		if (!PlayerStartRootComponent->IsRegistered())
		{
			PlayerStartRootComponent->OnRegister();
		}
	}

	UBillboardComponent* PlayerStartBillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(
		PrimaryPlayerStart, PlayerStartBillboardComponentName);
	if (PlayerStartBillboardComponent)
	{
		PrimaryPlayerStart->AddOwnedComponent(PlayerStartBillboardComponent);
		if (PlayerStartRootComponent)
		{
			PlayerStartBillboardComponent->AttachTo(PlayerStartRootComponent);
		}

		PlayerStartBillboardComponent->SetTexturePath((FPaths::IconDir() / L"S_Player.PNG").wstring());
		PlayerStartBillboardComponent->SetSize(FVector2(0.8f, 0.8f));
		PlayerStartBillboardComponent->SetIgnoreParentScaleInRender(true);
		PlayerStartBillboardComponent->SetEditorVisualization(true);
		PlayerStartBillboardComponent->SetHiddenInGame(true);
		if (!PlayerStartBillboardComponent->IsRegistered())
		{
			PlayerStartBillboardComponent->OnRegister();
		}
		PlayerStartBillboardComponent->UpdateBounds();
	}

	PrimaryPlayerStart->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
	MarkSpatialDirty();
	return PrimaryPlayerStart;
}

void ULevel::EnsureEssentialActors()
{
	bool bHasDirectionalLight = false;
	bool bHasAmbientLight = false;

	for (AActor* Actor : Actors)
	{
		if (!Actor || Actor->IsPendingDestroy())
		{
			continue;
		}

		if (!bHasDirectionalLight && Actor->IsA(ADirectionalLightActor::StaticClass()))
		{
			bHasDirectionalLight = true;
		}

		if (!bHasAmbientLight && Actor->IsA(AAmbientLightActor::StaticClass()))
		{
			bHasAmbientLight = true;
		}
	}

	if (!bHasDirectionalLight)
	{
		SpawnActor<ADirectionalLightActor>("DirectionalLight");
	}

	if (!bHasAmbientLight)
	{
		SpawnActor<AAmbientLightActor>("AmbientLight");
	}

	if (GameplaySettings.bAutoSpawnPlayerStart)
	{
		EnsurePlayerStartActor();
	}
}

void ULevel::RegisterActor(AActor* InActor)
{
	if (!InActor)
	{
		return;
	}

	const auto It = std::find(Actors.begin(), Actors.end(), InActor);
	if (It != Actors.end())
	{
		return;
	}

	Actors.push_back(InActor);
	InActor->SetLevel(this);
	MarkSpatialDirty();
}

void ULevel::DestroyActor(AActor* InActor)
{
	if (!InActor)
	{
		return;
	}
	InActor->Destroy();
	CleanupDestroyedActors();
	MarkSpatialDirty();
}

void ULevel::CleanupDestroyedActors()
{
	const auto NewEnd = std::ranges::remove_if(Actors,
		[](const AActor* Actor)
		{
			return Actor == nullptr || Actor->IsPendingDestroy();
		}).begin();

	const bool bRemovedAny = (NewEnd != Actors.end());
	Actors.erase(NewEnd, Actors.end());
	if (bRemovedAny)
	{
		MarkSpatialDirty();
	}
}

void ULevel::MarkSpatialDirty()
{
	bSpatialDirty = true;
}

void ULevel::GatherPrimitiveComponents(TArray<UPrimitiveComponent*>& OutPrimitives, bool bExcludeUUIDBillboards, bool bExcludeArrows) const
{
	for (AActor* Actor : Actors)
	{
		if (!Actor || Actor->IsPendingDestroy())
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
			if (PrimitiveComponent->IsPendingKill())
			{
				continue;
			}

			if (bExcludeUUIDBillboards && PrimitiveComponent->IsA(UUUIDBillboardComponent::StaticClass()))
			{
				continue;
			}

			if (bExcludeArrows && IsArrowVisualizationPrimitive(PrimitiveComponent))
			{
				continue;
			}

			OutPrimitives.push_back(PrimitiveComponent);
		}
	}
}

void ULevel::RebuildSpatialIfNeeded() const
{
	if (!bSpatialDirty)
	{
		return;
	}

	TArray<UPrimitiveComponent*> SpatialPrimitives;
	GatherPrimitiveComponents(SpatialPrimitives, false);
	SpatialBVH.Build(SpatialPrimitives);

	TArray<UPrimitiveComponent*> DebugPrimitives;
	GatherPrimitiveComponents(DebugPrimitives,true, true);
	DebugSpatialBVH.Build(DebugPrimitives);

	bSpatialDirty = false;
}

void ULevel::QueryPrimitivesByFrustum(const FFrustum& Frustum, TArray<UPrimitiveComponent*>& OutPrimitives) const
{
	RebuildSpatialIfNeeded();
	SpatialBVH.QueryFrustum(Frustum, OutPrimitives);
}

void ULevel::QueryPrimitivesByRay(const FVector& RayOrigin, const FVector& RayDirection, float MaxDistance, TArray<UPrimitiveComponent*>& OutPrimitives) const
{
	RebuildSpatialIfNeeded();

	if (RayDirection.IsZero())
	{
		return;
	}

	const Ray SceneRay(RayOrigin, RayDirection.GetSafeNormal());
	SpatialBVH.QueryRay(SceneRay, MaxDistance, OutPrimitives);
}

void ULevel::VisitPrimitivesByRay(const FVector& RayOrigin, const FVector& RayDirection, float& InOutMaxDistance, const BVH::FRayHitVisitor& Visitor) const
{
	RebuildSpatialIfNeeded();

	if (RayDirection.IsZero())
	{
		return;
	}

	const Ray SceneRay(RayOrigin, RayDirection.GetSafeNormal());
	SpatialBVH.VisitRay(SceneRay, InOutMaxDistance, Visitor);
}

void ULevel::VisitBVHNodes(const FBVHNodeVisitor& Visitor) const
{
	RebuildSpatialIfNeeded();
	SpatialBVH.VisitNodes(Visitor);
}

void ULevel::VisitBVHNodesForPrimitive(UPrimitiveComponent* Target, const FBVHNodeVisitor& Visitor) const
{
	RebuildSpatialIfNeeded();
	SpatialBVH.VisitNodesForPrimitive(Target, Visitor);
}

void ULevel::VisitDebugBVHNodes(const FBVHNodeVisitor& Visitor) const
{
	RebuildSpatialIfNeeded();
	DebugSpatialBVH.VisitNodes(Visitor);
}

void ULevel::VisitDebugBVHNodesForPrimitive(UPrimitiveComponent* Target, const FBVHNodeVisitor& Visitor) const
{
	RebuildSpatialIfNeeded();
	DebugSpatialBVH.VisitNodesForPrimitive(Target, Visitor);
}
