#pragma once
#include "MovementComponent.h"
#include "Math/Rotator.h"

class FArchive;

class ENGINE_API URotatingMovementComponent : public UMovementComponent
{
public:
	DECLARE_RTTI(URotatingMovementComponent, UMovementComponent)

	void Tick(float DeltaTime) override;

	void SetRotationRate(const FRotator& InRate) { RotationRate = InRate; }
	const FRotator& GetRotationRate() const { return RotationRate; }

	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;
	void Serialize(FArchive& Ar) override;

	void SetPivotTranslation(const FVector& InPivotTranslation) { PivotTranslation = InPivotTranslation; }
	const FVector& GetPivotTranslation() const { return PivotTranslation; }

private:
	/** 초당 회전 속도 (degrees/sec), 기본값: Yaw 90°/s */
	FRotator RotationRate{ 0.0f, 90.0f, 0.0f };
	FVector PivotTranslation{ FVector::ZeroVector };
};