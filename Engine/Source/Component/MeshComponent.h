#pragma once
#include "Component/PrimitiveComponent.h"

class FMaterial;

class ENGINE_API UMeshComponent : public UPrimitiveComponent
{
public:
	DECLARE_RTTI(UMeshComponent, UPrimitiveComponent)

	void SetMaterial(int32 Index, const std::shared_ptr<FMaterial>& InMaterial);
	std::shared_ptr<FMaterial> GetMaterial(int32 Index) const;
	int32 GetNumMaterials() const { return static_cast<int32>(Materials.size()); }
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;

	// 섹션별 Normal Texture 오버라이드. 값이 비어있지 않으면 Material 교체 시 새 Material에도 자동 재적용됨.
	void SetNormalTextureOverride(int32 Index, const FString& InTexturePath);
	void ClearNormalTextureOverride(int32 Index);
	const FString& GetNormalTextureOverride(int32 Index) const;
	
	void Serialize(FArchive& Ar) override;

protected:
	void DuplicateMaterialsTo(UMeshComponent* DuplicatedComponent) const;
	TArray<std::shared_ptr<FMaterial>> Materials;
	TArray<FString> NormalTextureOverrides; 
};
