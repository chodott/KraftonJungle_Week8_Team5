#pragma once

#include "Actor/Actor.h"

class UBillboardComponent;
class UDecalComponent;
class USceneComponent;
class FArchive;

class ENGINE_API ASpotLightFakeActor : public AActor
{
public:
	DECLARE_RTTI(ASpotLightFakeActor, AActor)

	void PostSpawnInitialize() override;
	void Serialize(FArchive& Ar) override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;

	UBillboardComponent* GetBillboardComponent() const { return BillboardComponent; }
	UDecalComponent* GetDecalComponent() const { return DecalComponent; }

	void SetBillboardTexturePath(const std::wstring& InPath);
	const std::wstring& GetBillboardTexturePath() const;

	void SetBillboardSize(const FVector2& InSize);
	const FVector2& GetBillboardSize() const;

	void SetDecalTexturePath(const std::wstring& InPath);
	const std::wstring& GetDecalTexturePath() const;

	void SetDecalExtent(const FVector& InExtent);
	FVector GetDecalExtent() const;

	void SetDecalFadeEnabled(bool bInEnabled);
	bool IsDecalFadeEnabled() const;

	void SetDecalFadeRadius(float InRadius);
	float GetDecalFadeRadius() const;

private:
	void UpdateBillboardPlacement();

	USceneComponent* RootSceneComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
	UDecalComponent* DecalComponent = nullptr;

	bool bDecalFadeEnabled = true;
	float DecalFadeRadius = 0.8f;
};
