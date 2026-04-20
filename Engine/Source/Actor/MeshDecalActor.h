#pragma once

#include "Actor/Actor.h"

class UBillboardComponent;
class UMeshDecalComponent;

class ENGINE_API AMeshDecalActor : public AActor
{
public:
	DECLARE_RTTI(AMeshDecalActor, AActor)

	void PostSpawnInitialize() override;
	void Serialize(FArchive& Ar) override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;

	UMeshDecalComponent* GetMeshDecalComponent() const { return MeshDecalComponent; }

private:
	UMeshDecalComponent* MeshDecalComponent = nullptr;
	UBillboardComponent* IconBillboardComponent = nullptr;
};
