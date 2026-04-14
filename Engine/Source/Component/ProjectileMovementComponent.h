#pragma once
#include "Component/MovementComponent.h"
#include "Math/Vector.h"

class FArchive;
class UStaticMeshComponent;

class ENGINE_API UProjectileMovementComponent : public UMovementComponent
{
public:
	DECLARE_RTTI(UProjectileMovementComponent, UMovementComponent)

	void PostConstruct() override;
	void OnRegister() override;
	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	void LaunchWithVelocity(const FVector& InVelocity);
	void StartSimulation();
	void StopSimulation();

	void SetVelocity(const FVector& InVelocity);
	const FVector& GetVelocity() const { return Velocity; }
	UStaticMeshComponent* GetVelocityArrowComponent() const { return VelocityArrowComponent; }

	void SetGravityScale(float InScale) { GravityScale = InScale; }
	float GetGravityScale() const { return GravityScale; }

	void SetMaxSpeed(float InMaxSpeed) { MaxSpeed = InMaxSpeed; }
	float GetMaxSpeed() const { return MaxSpeed; }

	void SetAutoStartSimulation(bool bInAutoStartSimulation);
	bool IsAutoStartSimulationEnabled() const { return bAutoStartSimulation; }
	bool IsSimulationEnabled() const { return bSimulationEnabled; }

	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;
	void Serialize(FArchive& Ar) override;

private:
	void EnsureVelocityArrowComponent();
	void UpdateVelocityArrow();

	FVector Velocity{ FVector::ZeroVector };
	float GravityScale = 1.0f;
	float MaxSpeed = 0.0f;
	bool bAutoStartSimulation = true;
	bool bSimulationEnabled = false;

	TObjectPtr<UStaticMeshComponent> VelocityArrowComponent;

	static constexpr float GravityZ = -980.0f;
};
