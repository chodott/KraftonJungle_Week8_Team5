#pragma once
#include "Component/ActorComponent.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

class USceneComponent;
class FArchive;

class ENGINE_API UMovementComponent : public UActorComponent
{
public:
	DECLARE_RTTI(UMovementComponent, UActorComponent)

	void PostConstruct() override;
	void BeginPlay() override;
	void Serialize(FArchive& Ar) override;
	void OnPostLoad() override;

	void SetUpdatedComponent(USceneComponent* InComponent);
	USceneComponent* GetUpdatedComponent() const { return UpdatedComponent; }

	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;

protected:
	bool EnsureUpdatedComponent();
	bool ShouldSkipUpdate(float DeltaTime);
	void MoveUpdatedComponent(const FVector& DeltaLocation, const FRotator& DeltaRotation = FRotator::ZeroRotator);
	void ResolveUpdatedComponent();

	TObjectPtr<USceneComponent> UpdatedComponent;
	uint32 PendingUpdatedComponentUUID = 0;
};
