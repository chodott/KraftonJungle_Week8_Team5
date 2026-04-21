#pragma once

#include "Component/SceneComponent.h"

class ENGINE_API USpringArmComponent : public USceneComponent
{
public:
	DECLARE_RTTI(USpringArmComponent, USceneComponent)

	float GetTargetArmLength() const { return TargetArmLength; }
	void SetTargetArmLength(float InTargetArmLength);

	const FVector& GetSocketOffset() const { return SocketOffset; }
	void SetSocketOffset(const FVector& InSocketOffset);

	FVector GetSocketWorldLocation() const;
	FRotator GetSocketWorldRotation() const;

	void Serialize(FArchive& Ar) override;
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;

private:
	float TargetArmLength = 300.0f;
	FVector SocketOffset = FVector(0.0f, 0.0f, 50.0f);
};

