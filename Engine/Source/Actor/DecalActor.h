#pragma once

#include "Actor/Actor.h"

class UDecalComponent;
class UBillboardComponent;
class UStaticMeshComponent;

class ENGINE_API ADecalActor : public AActor
{
public:
	DECLARE_RTTI(ADecalActor, AActor)

	void PostSpawnInitialize() override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;

	UDecalComponent* GetDecalComponent() const { return DecalComponent; }

private:
	UDecalComponent* DecalComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
	UStaticMeshComponent* ArrowComponent = nullptr;

};
