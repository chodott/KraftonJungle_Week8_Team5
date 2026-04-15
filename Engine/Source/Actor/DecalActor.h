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
	void Tick(float DeltaTime) override;
	void Serialize(FArchive& Ar) override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;

	UDecalComponent* GetDecalComponent() const { return DecalComponent; }
	UStaticMeshComponent* GetArrowComponent() const { return ArrowComponent; }

private:
	void UpdateArrowVisualization();

	UDecalComponent* DecalComponent = nullptr;
	UBillboardComponent* IconBillboardComponent = nullptr;
	UStaticMeshComponent* ArrowComponent = nullptr;

};
