#pragma once

#include "Component/PrimitiveComponent.h"
#include "Renderer/Features/Decal/DecalTypes.h"
#include "Math/LinearColor.h"

#include <algorithm>

class FArchive;

enum class EDecalFadeState : uint8
{
	None,
	FadeIn,
	FadeOut,
};

class ENGINE_API UDecalComponent : public UPrimitiveComponent
{
public:
	DECLARE_RTTI(UDecalComponent, UPrimitiveComponent)

	void PostConstruct() override;
	FBoxSphereBounds GetLocalBounds() const override;
	void Serialize(FArchive& Ar) override;
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;
	void Tick(float DeltaTime) override;

	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }
	bool IsEnabled() const { return bEnabled; }
	virtual bool IsPickable() const { return false; }

	// Fade In/Out
	void FadeIn(float Duration);
	void FadeOut(float Duration, bool bDisableOnComplete = true);
	EDecalFadeState GetFadeState() const { return FadeState; }

	void SetFadeInDuration(float Duration) { FadeInDuration = (std::max)(0.05f, Duration); }
	float GetFadeInDuration() const { return FadeInDuration; }

	void SetFadeOutDuration(float Duration) { FadeOutDuration = (std::max)(0.05f, Duration); }
	float GetFadeOutDuration() const { return FadeOutDuration; }

	void SetSize(const FVector2& InSize)
	{
		const FVector2 Sanitized(
			(std::max)(0.0f, InSize.X),
			(std::max)(0.0f, InSize.Y));
		if (Size != Sanitized)
		{
			Size = Sanitized;
			UpdateBounds();
		}
	}
	const FVector2& GetSize() const { return Size; }

	void SetProjectionDepth(float InProjectionDepth)
	{
		const float Sanitized = (std::max)(0.0f, InProjectionDepth);
		if (ProjectionDepth != Sanitized)
		{
			ProjectionDepth = Sanitized;
			UpdateBounds();
		}
	}
	float GetProjectionDepth() const { return ProjectionDepth; }

	void SetExtents(const FVector& InExtents)
	{
		const FVector Sanitized(
			(std::max)(0.0f, InExtents.X),
			(std::max)(0.0f, InExtents.Y),
			(std::max)(0.0f, InExtents.Z));

		const float NewProjectionDepth = Sanitized.X * 2.0f;
		const FVector2 NewSize(Sanitized.Y * 2.0f, Sanitized.Z * 2.0f);
		if (Size != NewSize || ProjectionDepth != NewProjectionDepth)
		{
			Size = NewSize;
			ProjectionDepth = NewProjectionDepth;
			UpdateBounds();
		}
	}
	FVector GetExtents() const
	{
		return FVector(ProjectionDepth * 0.5f, Size.X * 0.5f, Size.Y * 0.5f);
	}

	void SetUVMin(const FVector2& InUVMin) { UVMin = InUVMin; }
	const FVector2& GetUVMin() const { return UVMin; }

	void SetUVMax(const FVector2& InUVMax) { UVMax = InUVMax; }
	const FVector2& GetUVMax() const { return UVMax; }

	FVector4 GetAtlasScaleBias() const
	{
		return FVector4(
			UVMax.X - UVMin.X,
			UVMax.Y - UVMin.Y,
			UVMin.X,
			UVMin.Y);
	}

	void SetTexturePath(const std::wstring& InPath) { TexturePath = InPath; }
	const std::wstring& GetTexturePath() const { return TexturePath; }

	void SetTextureIndex(uint32 InTextureIndex) { TextureIndex = InTextureIndex; }
	uint32 GetTextureIndex() const { return TextureIndex; }

	void SetRenderFlags(uint32 InRenderFlags) { RenderFlags = InRenderFlags; }
	uint32 GetRenderFlags() const { return RenderFlags; }

	void SetPriority(uint32 InPriority) { Priority = InPriority; }
	uint32 GetPriority() const { return Priority; }

	void SetReceiverLayerMask(uint32 InReceiverLayerMask) { ReceiverLayerMask = InReceiverLayerMask; }
	uint32 GetReceiverLayerMask() const { return ReceiverLayerMask; }

	void SetAllowAngle(float InDegrees) { AllowAngle = std::clamp(InDegrees, 0.0f, 180.0f); }
	float GetAllowAngle() const { return AllowAngle; }

	void SetBaseColorTint(const FLinearColor& InBaseColorTint) { BaseColorTint = InBaseColorTint; }
	const FLinearColor& GetBaseColorTint() const { return BaseColorTint; }

	void SetNormalBlend(float InNormalBlend) { NormalBlend = std::clamp(InNormalBlend, 0.0f, 1.0f); }
	float GetNormalBlend() const { return NormalBlend; }

	void SetRoughnessBlend(float InRoughnessBlend) { RoughnessBlend = std::clamp(InRoughnessBlend, 0.0f, 1.0f); }
	float GetRoughnessBlend() const { return RoughnessBlend; }

	void SetEmissiveBlend(float InEmissiveBlend) { EmissiveBlend = std::clamp(InEmissiveBlend, 0.0f, 1.0f); }
	float GetEmissiveBlend() const { return EmissiveBlend; }

	void SetEdgeFade(float InEdgeFade) { EdgeFade = (std::max)(0.0f, InEdgeFade); }
	float GetEdgeFade() const { return EdgeFade; }

private:
	bool bEnabled = true;

	EDecalFadeState FadeState = EDecalFadeState::None;
	float FadeInDuration = 1.0f;
	float FadeOutDuration = 1.0f;
	float FadeDuration = 1.0f;
	float FadeElapsed = 0.0f;
	bool bDisableOnFadeOutComplete = true;

	FVector2 Size = FVector2(10.0f, 10.0f);
	float ProjectionDepth = 10.0f;

	FVector2 UVMin = FVector2(0.f, 0.f);
	FVector2 UVMax = FVector2(1.f, 1.f);

	std::wstring TexturePath;
	uint32 TextureIndex = 0;
	uint32 RenderFlags = DECAL_RENDER_FLAG_BaseColor;
	uint32 Priority = 0;
	uint32 ReceiverLayerMask = 0xFFFFFFFFu;
	float AllowAngle = 90.0f;

	FLinearColor BaseColorTint = FLinearColor::White;
	float NormalBlend = 1.0f;
	float RoughnessBlend = 1.0f;
	float EmissiveBlend = 1.0f;
	float EdgeFade = 2.0f;
};
