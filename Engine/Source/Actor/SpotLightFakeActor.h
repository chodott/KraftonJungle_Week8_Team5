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

	UBillboardComponent* GetLightBillboardComponent() const { return LightBillboardComponent; }
	UBillboardComponent* GetIconBillboardComponent() const { return IconBillboardComponent; }
	UDecalComponent* GetDecalComponent() const { return DecalComponent; }

	void SetLightBillboardTexturePath(const std::wstring& InPath);
	const std::wstring& GetLightBillboardTexturePath() const;

	void SetLightBillboardSize(const FVector2& InSize);
	const FVector2& GetLightBillboardSize() const;

	void SetIconBillboardTexturePath(const std::wstring& InPath);
	const std::wstring& GetIconBillboardTexturePath() const;

	void SetIconBillboardSize(const FVector2& InSize);
	const FVector2& GetIconBillboardSize() const;

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
	UBillboardComponent* LightBillboardComponent = nullptr;
	UBillboardComponent* IconBillboardComponent = nullptr;
	UDecalComponent* DecalComponent = nullptr;

	bool bDecalFadeEnabled = true;
	float DecalFadeRadius = 0.8f;
};
