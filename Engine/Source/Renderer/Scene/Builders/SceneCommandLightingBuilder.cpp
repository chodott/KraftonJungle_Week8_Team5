#include "SceneCommandLightingBuilder.h"

#include "Renderer/Scene/Builders/SceneCommandBuilder.h"
#include "Renderer/Scene/SceneViewData.h"

#include "Actor/Actor.h"
#include "Component/AmbientLightComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/PointLightComponent.h"
#include "Component/SpotLightComponent.h"
#include "Math/MathUtility.h"
#include "World/World.h"

#include "Renderer/Features/Shadow/ShadowAtlasAllocator.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "Math/Cascade.h"


namespace
{
	FVector ToLightRGB(const FLinearColor& Color)
	{
		return FVector(Color.R, Color.G, Color.B);
	}

	FLocalLightRenderItem BuildPointLight(const UPointLightComponent* C)
	{
		FLocalLightRenderItem L;
		L.LightClass = ELightClass::Point;
		L.CullShape  = ECullShapeType::Sphere;

		L.PositionWS      = C->GetWorldLocation();
		L.Range           = C->GetAttenuationRadius();
		L.Color           = ToLightRGB(C->GetColor());
		L.Intensity       = C->GetEffectiveIntensity();
		L.FalloffExponent = C->GetLightFalloffExponent();
		L.Flags           = 0;

		L.CullCenterWS = L.PositionWS;
		L.CullRadius   = L.Range;

		return L;
	}

	FLocalLightRenderItem BuildSpotLight(const USpotLightComponent* C)
	{
		FLocalLightRenderItem L;
		L.LightClass = ELightClass::Spot;
		L.CullShape  = ECullShapeType::Cone;

		L.PositionWS      = C->GetWorldLocation();
		L.DirectionWS     = C->GetEmissionDirectionWS().GetSafeNormal();
		L.Range           = C->GetAttenuationRadius();
		L.FalloffExponent = C->GetLightFalloffExponent();
		L.Color           = ToLightRGB(C->GetColor());
		L.Intensity       = C->GetEffectiveIntensity();
		L.Flags           = 0;

		const float InnerAngleRad =
				FMath::DegreesToRadians(
					FMath::Clamp(C->GetInnerConeAngle(), 0.0f, 89.0f));

		const float OuterAngleRad =
				FMath::DegreesToRadians(
					FMath::Clamp(C->GetOuterConeAngle(), 0.0f, 89.0f));

		L.InnerAngleCos = std::cos(InnerAngleRad);
		L.OuterAngleCos = std::cos(OuterAngleRad);

		if (L.InnerAngleCos < L.OuterAngleCos)
		{
			std::swap(L.InnerAngleCos, L.OuterAngleCos);
		}

		L.CullCenterWS = L.PositionWS + L.DirectionWS * (L.Range * 0.5f);
		L.CullRadius   = L.Range;

		return L;
	}

	FDirectionalLightRenderItem BuildDirectionalLight(const UDirectionalLightComponent* C)
	{
		FDirectionalLightRenderItem L;
		L.DirectionWS = C->GetEmissionDirectionWS().GetSafeNormal();
		L.Color       = ToLightRGB(C->GetColor());
		L.Intensity   = C->GetEffectiveIntensity();
		L.Flags       = 0;
		return L;
	}

	uint32 AllocalteShadowLight(
		FSceneLightingInputs& Inputs,
		EShadowLightType      LightType,
		uint32                SourceLightIndex)
	{
		if (Inputs.ShadowLights.size() >= ShadowConfig::MaxShadowLights)
		{
			return UINT32_MAX;
		}

		FShadowLightRenderItem Item = {};
		Item.LightType              = LightType;
		Item.SourceLightIndex       = SourceLightIndex;
		Item.ShadowIndex            = static_cast<uint32>(Inputs.ShadowLights.size());

		Inputs.ShadowLights.push_back(Item);

		return Item.ShadowIndex;
	}

	uint32 AddShadowView(
		FSceneLightingInputs&        Inputs,
		uint32                       ShadowLightIndex,
		const FShadowViewRenderItem& InView)
	{
		if (Inputs.ShadowViews.size() >= ShadowConfig::MaxShadowViews)
		{
			return UINT32_MAX;
		}

		FShadowViewRenderItem View = InView;
		View.ShadowLightIndex      = ShadowLightIndex;
		View.ArraySlice            = static_cast<uint32>(Inputs.ShadowViews.size());

		const uint32 ViewIndex =
				static_cast<uint32>(Inputs.ShadowViews.size());

		Inputs.ShadowViews.push_back(std::move(View));

		FShadowLightRenderItem& ShadowLight =
				Inputs.ShadowLights[ShadowLightIndex];

		if (ShadowLight.ViewCount == 0)
		{
			ShadowLight.FirstViewIndex = ViewIndex;
		}

		++ShadowLight.ViewCount;

		return ViewIndex;
	}

	float ComputeSpotScreenCoverage(
		const FVector& CameraPosition,
		const FVector& LightPosition,
		const FVector& LightDirection,
		float          Range,
		float          OuterConeAngleDeg)
	{
		const float SafeRange = (std::max)(Range, 0.0f);
		const float HalfRange = SafeRange * 0.5f;

		const float OuterAngleRad =
				FMath::DegreesToRadians(
					FMath::Clamp(OuterConeAngleDeg, 1.0f, 80.0f));

		const float ConeEndRadius =
				std::tan(OuterAngleRad) * SafeRange;

		const FVector BoundsCenter =
				LightPosition + LightDirection.GetSafeNormal() * HalfRange;

		const float BoundsRadius =
				std::sqrt(HalfRange * HalfRange + ConeEndRadius * ConeEndRadius);

		const float Distance =
				(CameraPosition - BoundsCenter).Size();

		const float SafeDistance =
				(std::max)(Distance - BoundsRadius, 1.0f);

		const float ProjectedRadius =
				BoundsRadius / SafeDistance;

		return FMath::Clamp(
			ProjectedRadius * ProjectedRadius,
			0.0f,
			1.0f);
	}

	uint32 QuantizeShadowResolution(uint32 Resolution)
	{
		if (Resolution >= 2048)
		{
			return 2048;
		}
		if (Resolution >= 1024)
		{
			return 1024;
		}
		if (Resolution >= 512)
		{
			return 512;
		}
		if (Resolution >= 256)
		{
			return 256;
		}
		if (Resolution >= 128)
		{
			return 128;
		}
		return 64;
	}

	uint32 AddPointShadowView(
		FSceneLightingInputs&        Inputs,
		uint32                       ShadowLightIndex,
		uint32                       ExplicitArraySlice,
		const FShadowViewRenderItem& InView)
	{
		FShadowViewRenderItem View = InView;
		View.ShadowLightIndex      = ShadowLightIndex;
		View.ArraySlice            = ExplicitArraySlice;

		const uint32 ViewIndex =
				static_cast<uint32>(Inputs.ShadowViews.size());

		Inputs.ShadowViews.push_back(std::move(View));

		FShadowLightRenderItem& Light =
				Inputs.ShadowLights[ShadowLightIndex];

		if (Light.ViewCount == 0)
		{
			Light.FirstViewIndex = ViewIndex;
		}

		++Light.ViewCount;
		Light.LightType = EShadowLightType::Point;

		return ViewIndex;
	}

	uint32 AllocateDirShadowLight(
		FSceneLightingInputs& Inputs,
		EShadowLightType      LightType,
		uint32                SourceLightIndex)
	{
		if (Inputs.DirShadowLights.size() >= ShadowConfig::MaxShadowLights)
		{
			return UINT32_MAX;
		}

		FShadowLightRenderItem Item = {};
		Item.LightType              = LightType;
		Item.SourceLightIndex       = SourceLightIndex;
		Item.ShadowIndex            = static_cast<uint32>(Inputs.DirShadowLights.size());

		Inputs.DirShadowLights.push_back(Item);

		return Item.ShadowIndex;
	}

	uint32 AddDirShadowView(
		FSceneLightingInputs&        Inputs,
		uint32                       ShadowLightIndex,
		const FShadowViewRenderItem& InView)
	{
		if (Inputs.DirShadowViews.size() >= ShadowConfig::MaxDirCascade)
		{
			return UINT32_MAX;
		}

		FShadowViewRenderItem View = InView;
		View.ShadowLightIndex      = ShadowLightIndex;
		View.ArraySlice            = static_cast<uint32>(Inputs.DirShadowViews.size());

		const uint32 ViewIndex =
				static_cast<uint32>(Inputs.DirShadowViews.size());

		Inputs.DirShadowViews.push_back(std::move(View));

		FShadowLightRenderItem& ShadowLight =
				Inputs.DirShadowLights[ShadowLightIndex];

		if (ShadowLight.ViewCount == 0)
		{
			ShadowLight.FirstViewIndex = ViewIndex;
		}

		++ShadowLight.ViewCount;

		return ViewIndex;
	}

	bool IsFinite(float Value)
	{
		return std::isfinite(Value);
	}

	bool IsFinite(const FVector& Value)
	{
		return
				IsFinite(Value.X) &&
				IsFinite(Value.Y) &&
				IsFinite(Value.Z);
	}

	bool IsFinite(const FVector4& Value)
	{
		return
				IsFinite(Value.X) &&
				IsFinite(Value.Y) &&
				IsFinite(Value.Z) &&
				IsFinite(Value.W);
	}

	bool IsFinite(const FMatrix& Matrix)
	{
		for (int Row = 0; Row < 4; ++Row)
		{
			for (int Col = 0; Col < 4; ++Col)
			{
				if (!IsFinite(Matrix[Row][Col]))
				{
					return false;
				}
			}
		}

		return true;
	}

	bool IsUsableProjectionMatrix(const FMatrix& Matrix)
	{
		return IsFinite(Matrix) && Matrix.IsInvertible(1.0e-7f);
	}

	struct FPSMBoundsWS
	{
		FVector Min    = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
		FVector Max    = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		bool    bValid = false;

		void Expand(const FVector& Point)
		{
			if (!IsFinite(Point))
			{
				return;
			}

			Min.X = (std::min)(Min.X, Point.X);
			Min.Y = (std::min)(Min.Y, Point.Y);
			Min.Z = (std::min)(Min.Z, Point.Z);

			Max.X = (std::max)(Max.X, Point.X);
			Max.Y = (std::max)(Max.Y, Point.Y);
			Max.Z = (std::max)(Max.Z, Point.Z);

			bValid = true;
		}

		void Expand(const FPSMBoundsWS& Other)
		{
			if (!Other.bValid)
			{
				return;
			}

			Expand(Other.Min);
			Expand(Other.Max);
		}

		FVector GetCorner(int Index) const
		{
			return FVector(
				(Index & 1) ? Max.X : Min.X,
				(Index & 2) ? Max.Y : Min.Y,
				(Index & 4) ? Max.Z : Min.Z);
		}
	};

	FPSMBoundsWS BuildBoundsFromCornersWS(const FVector CornersWS[8])
	{
		FPSMBoundsWS Bounds;

		for (int i = 0; i < 8; ++i)
		{
			Bounds.Expand(CornersWS[i]);
		}

		return Bounds;
	}

	FPSMBoundsWS BuildPotentialCasterBoundsWS(
		const FPSMBoundsWS& ReceiverBoundsWS,
		const FVector&      LightSourceDirectionWS,
		float               MaxCasterDistance)
	{
		FPSMBoundsWS Bounds = ReceiverBoundsWS;

		if (!ReceiverBoundsWS.bValid)
		{
			return Bounds;
		}

		const FVector SafeLightSourceDirectionWS =
				LightSourceDirectionWS.GetSafeNormal();

		if (SafeLightSourceDirectionWS.IsNearlyZero())
		{
			return Bounds;
		}

		const FVector Offset =
				SafeLightSourceDirectionWS * MaxCasterDistance;

		for (int i = 0; i < 8; ++i)
		{
			Bounds.Expand(ReceiverBoundsWS.GetCorner(i) + Offset);
		}

		return Bounds;
	}

	bool ComputeBoundsDepthRangeX(
		const FPSMBoundsWS& Bounds,
		const FVector&      CameraPositionWS,
		const FVector&      CameraForwardWS,
		float&              OutMinDepthX,
		float&              OutMaxDepthX)
	{
		if (!Bounds.bValid)
		{
			return false;
		}

		const FVector SafeForwardWS =
				CameraForwardWS.GetSafeNormal();

		if (SafeForwardWS.IsNearlyZero())
		{
			return false;
		}

		OutMinDepthX = FLT_MAX;
		OutMaxDepthX = -FLT_MAX;

		for (int i = 0; i < 8; ++i)
		{
			const FVector ToCorner =
					Bounds.GetCorner(i) - CameraPositionWS;

			const float DepthX =
					FVector::DotProduct(ToCorner, SafeForwardWS);

			OutMinDepthX = (std::min)(OutMinDepthX, DepthX);
			OutMaxDepthX = (std::max)(OutMaxDepthX, DepthX);
		}

		return IsFinite(OutMinDepthX) && IsFinite(OutMaxDepthX);
	}

	float ComputeReceiverFitNearX(
		const FPSMBoundsWS& ReceiverBoundsWS,
		const FViewContext& View,
		const FVector&      CameraForwardWS,
		float               NearSplit,
		float               FarSplit)
	{
		float ReceiverMinDepthX = 0.0f;
		float ReceiverMaxDepthX = 0.0f;

		if (!ComputeBoundsDepthRangeX(
			ReceiverBoundsWS,
			View.CameraPosition,
			CameraForwardWS,
			ReceiverMinDepthX,
			ReceiverMaxDepthX))
		{
			return NearSplit;
		}

		if (ReceiverMaxDepthX < NearSplit || ReceiverMinDepthX > FarSplit)
		{
			return NearSplit;
		}

		constexpr float SafetyMargin = 2.0f;

		const float FitNearX =
				FMath::Clamp(
					ReceiverMinDepthX - SafetyMargin,
					NearSplit,
					FarSplit - 1.0f);

		return (std::max)(NearSplit, FitNearX);
	}

	bool ExtractPerspectiveSettings(
		const FMatrix& Projection,
		float&         OutFovY,
		float&         OutAspectRatio)
	{
		const float XScale = Projection[1][0];
		const float YScale = Projection[2][1];

		if (!IsFinite(XScale) || !IsFinite(YScale) ||
			std::fabs(XScale) <= 1.0e-6f ||
			std::fabs(YScale) <= 1.0e-6f)
		{
			return false;
		}

		OutFovY        = 2.0f * std::atan(1.0f / YScale);
		OutAspectRatio = YScale / XScale;

		return
				IsFinite(OutFovY) &&
				IsFinite(OutAspectRatio) &&
				OutFovY > FMath::DegreesToRadians(1.0f) &&
				OutFovY < FMath::DegreesToRadians(179.0f) &&
				OutAspectRatio > 1.0e-4f;
	}

	FVector ChooseStableUpVector(const FVector& LookDirection)
	{
		const FVector SafeLook =
				LookDirection.GetSafeNormal();

		FVector UpVector = FVector::ZAxisVector;

		if (std::fabs(FVector::DotProduct(UpVector, SafeLook)) > 0.99f)
		{
			UpVector = FVector::YAxisVector;
		}

		return UpVector;
	}

	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float ComputeVirtualSlideBack(
		float          NearSplit,
		float          FarSplit,
		float          VirtualNearWorldDepthX,
		const FVector& CameraForwardWS,
		const FVector& LightSourceDirectionWS)
	{
		const FVector SafeCameraForwardWS =
				CameraForwardWS.GetSafeNormal();

		const FVector SafeLightSourceDirectionWS =
				LightSourceDirectionWS.GetSafeNormal();

		if (SafeCameraForwardWS.IsNearlyZero() ||
			SafeLightSourceDirectionWS.IsNearlyZero())
		{
			return 0.0f;
		}

		const float ParallelAmount =
				std::fabs(
					FVector::DotProduct(
						SafeCameraForwardWS,
						SafeLightSourceDirectionWS));

		const float SlideWeight =
				SmoothStep01((ParallelAmount - 0.55f) / 0.45f);

		const float CascadeDepth =
				(std::max)(FarSplit - NearSplit, 0.0f);

		const float HeuristicSlide =
				FMath::Clamp(CascadeDepth * 0.5f, 10.0f, 100.0f) *
				SlideWeight;

		const float RequiredSlideForPositiveNear =
				(std::max)(0.0f, ShadowConfig::DefaultNearZ - VirtualNearWorldDepthX);

		return (std::max)(HeuristicSlide, RequiredSlideForPositiveNear);
	}

	bool BuildVirtualCameraForCascade(
		const FViewContext& View,
		const FPSMBoundsWS& ReceiverBoundsWS,
		const FPSMBoundsWS& PotentialCasterBoundsWS,
		const FVector&      LightSourceDirectionWS,
		float               NearSplit,
		float               FarSplit,
		FMatrix&            OutVirtualView,
		FMatrix&            OutVirtualProjection,
		FVector&            OutVirtualCameraPositionWS,
		float&              OutVirtualNear,
		float&              OutVirtualFar)
	{
		float FovY        = 0.0f;
		float AspectRatio = 1.0f;

		if (!ExtractPerspectiveSettings(View.Projection, FovY, AspectRatio))
		{
			return false;
		}

		const FVector CameraForwardWS =
				View.InverseView.TransformVector(FVector::ForwardVector).GetSafeNormal();

		const FVector CameraUpWS =
				View.InverseView.TransformVector(FVector::ZAxisVector).GetSafeNormal();

		if (CameraForwardWS.IsNearlyZero() || CameraUpWS.IsNearlyZero())
		{
			return false;
		}

		const float ReceiverFitNearX =
				ComputeReceiverFitNearX(
					ReceiverBoundsWS,
					View,
					CameraForwardWS,
					NearSplit,
					FarSplit);

		float CasterMinDepthX = ReceiverFitNearX;
		float CasterMaxDepthX = FarSplit;

		float PotentialMinDepthX = 0.0f;
		float PotentialMaxDepthX = 0.0f;

		if (ComputeBoundsDepthRangeX(
			PotentialCasterBoundsWS,
			View.CameraPosition,
			CameraForwardWS,
			PotentialMinDepthX,
			PotentialMaxDepthX))
		{
			CasterMinDepthX = (std::min)(CasterMinDepthX, PotentialMinDepthX);
			CasterMaxDepthX = (std::max)(CasterMaxDepthX, PotentialMaxDepthX);
		}

		const float VirtualNearWorldDepthX = CasterMinDepthX;

		const float SlideBack =
				ComputeVirtualSlideBack(
					NearSplit,
					FarSplit,
					VirtualNearWorldDepthX,
					CameraForwardWS,
					LightSourceDirectionWS);

		OutVirtualCameraPositionWS =
				View.CameraPosition - CameraForwardWS * SlideBack;

		OutVirtualNear =
				VirtualNearWorldDepthX + SlideBack;

		OutVirtualFar =
				(std::max)(FarSplit, CasterMaxDepthX) + SlideBack;

		OutVirtualNear =
				(std::max)(OutVirtualNear, ShadowConfig::DefaultNearZ);

		if (OutVirtualFar <= OutVirtualNear + 1.0e-3f)
		{
			return false;
		}

		OutVirtualView =
				FMatrix::MakeViewLookAtLH(
					OutVirtualCameraPositionWS,
					OutVirtualCameraPositionWS + CameraForwardWS,
					CameraUpWS);

		OutVirtualProjection =
				FMatrix::MakePerspectiveFovLH(
					FovY,
					AspectRatio,
					OutVirtualNear,
					OutVirtualFar);

		return
				IsUsableProjectionMatrix(OutVirtualView) &&
				IsUsableProjectionMatrix(OutVirtualProjection);
	}

	struct FPostPerspectiveBuildResult
	{
		FMatrix ViewPP              = FMatrix::Identity;
		FMatrix ProjectionPP        = FMatrix::Identity;
		bool    bInversePerspective = false;
		bool    bOrthographicPP     = false;
	};

	bool BuildPostPerspectiveInfo(
		const FMatrix&               VirtualView,
		const FMatrix&               VirtualProjection,
		const FVector&               LightSourceDirectionWS,
		FPostPerspectiveBuildResult& OutResult)
	{
		const FVector CubeCenterPP(0.0f, 0.0f, 0.5f);
		const float   CubeRadiusPP = 1.5f;
		const float   MinDepthSpan = 0.1f;

		const FVector SafeLightSourceDirectionWS =
				LightSourceDirectionWS.GetSafeNormal();

		if (SafeLightSourceDirectionWS.IsNearlyZero())
		{
			return false;
		}

		const FVector EyeLightDirection =
				VirtualView.TransformVector(SafeLightSourceDirectionWS);

		const FVector4 LightPP =
				FVector4(
					EyeLightDirection.X,
					EyeLightDirection.Y,
					EyeLightDirection.Z,
					0.0f) *
				VirtualProjection;

		if (!IsFinite(LightPP))
		{
			return false;
		}

		const bool bLightIsBehindEye  = LightPP.W < 0.0f;
		const bool bUseOrthographicPP = std::fabs(LightPP.W) <= 1.0e-3f;

		OutResult.bInversePerspective =
				bLightIsBehindEye && !bUseOrthographicPP;

		OutResult.bOrthographicPP =
				bUseOrthographicPP;

		if (bUseOrthographicPP)
		{
			FVector LightDirectionPP(
				LightPP.X,
				LightPP.Y,
				LightPP.Z);

			LightDirectionPP =
					LightDirectionPP.GetSafeNormal();

			if (LightDirectionPP.IsNearlyZero())
			{
				return false;
			}

			const FVector LightPositionPP =
					CubeCenterPP + LightDirectionPP * (2.0f * CubeRadiusPP);

			const FVector LookDirectionPP =
					CubeCenterPP - LightPositionPP;

			const float DistToCenter =
					LookDirectionPP.Size();

			if (!IsFinite(DistToCenter) || DistToCenter <= CubeRadiusPP)
			{
				return false;
			}

			const float NearPP =
					(std::max)(MinDepthSpan, DistToCenter - CubeRadiusPP);

			const float FarPP =
					(std::max)(NearPP + MinDepthSpan, DistToCenter + CubeRadiusPP);

			OutResult.ViewPP =
					FMatrix::MakeViewLookAtLH(
						LightPositionPP,
						CubeCenterPP,
						ChooseStableUpVector(LookDirectionPP));

			OutResult.ProjectionPP =
					FMatrix::MakeOrthographicLH(
						CubeRadiusPP * 2.0f,
						CubeRadiusPP * 2.0f,
						NearPP,
						FarPP);

			return
					IsUsableProjectionMatrix(OutResult.ViewPP) &&
					IsUsableProjectionMatrix(OutResult.ProjectionPP);
		}

		const float InvW = 1.0f / LightPP.W;

		const FVector LightPositionPP(
			LightPP.X * InvW,
			LightPP.Y * InvW,
			LightPP.Z * InvW);

		if (!IsFinite(LightPositionPP))
		{
			return false;
		}

		FVector LookAtCubePP =
				CubeCenterPP - LightPositionPP;

		const float DistLookAtCubePP =
				LookAtCubePP.Size();

		if (!IsFinite(DistLookAtCubePP) || DistLookAtCubePP <= 1.0e-4f)
		{
			return false;
		}

		LookAtCubePP /= DistLookAtCubePP;

		float FovPP =
				2.0f * std::atan(CubeRadiusPP / DistLookAtCubePP);

		FovPP =
				FMath::Clamp(
					FovPP,
					FMath::DegreesToRadians(1.0f),
					FMath::DegreesToRadians(175.0f));

		float NearPP = 0.0f;
		float FarPP  = 0.0f;

		if (bLightIsBehindEye)
		{
			NearPP = (std::max)(MinDepthSpan, DistLookAtCubePP - CubeRadiusPP);
			FarPP  = NearPP;
			NearPP = -NearPP;
		}
		else
		{
			NearPP = (std::max)(MinDepthSpan, DistLookAtCubePP - CubeRadiusPP);
			FarPP  = (std::max)(NearPP + MinDepthSpan, DistLookAtCubePP + CubeRadiusPP);
		}

		OutResult.ViewPP =
				FMatrix::MakeViewLookAtLH(
					LightPositionPP,
					CubeCenterPP,
					ChooseStableUpVector(LookAtCubePP));

		OutResult.ProjectionPP =
				FMatrix::MakePerspectiveFovLH(
					FovPP,
					1.0f,
					NearPP,
					FarPP);

		return
				IsUsableProjectionMatrix(OutResult.ViewPP) &&
				IsUsableProjectionMatrix(OutResult.ProjectionPP);
	}

	bool ValidateShadowProjectionForCascade(
		const FMatrix& ViewProjection,
		const FVector  FrustumCornersWS[8],
		bool           bInversePerspective)
	{
		float MinX     = FLT_MAX;
		float MaxX     = -FLT_MAX;
		float MinY     = FLT_MAX;
		float MaxY     = -FLT_MAX;
		float MinDepth = FLT_MAX;
		float MaxDepth = -FLT_MAX;

		for (int CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const FVector& Corner = FrustumCornersWS[CornerIndex];

			if (!IsFinite(Corner))
			{
				return false;
			}

			const FVector4 Clip =
					FVector4(Corner.X, Corner.Y, Corner.Z, 1.0f) *
					ViewProjection;

			if (!IsFinite(Clip) || std::fabs(Clip.W) <= 1.0e-5f)
			{
				return false;
			}

			const float InvW = 1.0f / Clip.W;

			const FVector NDC(
				Clip.X * InvW,
				Clip.Y * InvW,
				Clip.Z * InvW);

			if (!IsFinite(NDC))
			{
				return false;
			}

			const float XYMargin =
					bInversePerspective ? 1.08f : 1.02f;

			if (NDC.X < -XYMargin || NDC.X > XYMargin ||
				NDC.Y < -XYMargin || NDC.Y > XYMargin)
			{
				return false;
			}

			if (!bInversePerspective)
			{
				if (NDC.Z < -0.02f || NDC.Z > 1.02f)
				{
					return false;
				}
			}
			else
			{
				if (NDC.Z < -16.0f || NDC.Z > 16.0f)
				{
					return false;
				}
			}

			MinX     = (std::min)(MinX, NDC.X);
			MaxX     = (std::max)(MaxX, NDC.X);
			MinY     = (std::min)(MinY, NDC.Y);
			MaxY     = (std::max)(MaxY, NDC.Y);
			MinDepth = (std::min)(MinDepth, NDC.Z);
			MaxDepth = (std::max)(MaxDepth, NDC.Z);
		}

		const float MinXYSpan    = 1.0e-2f;
		const float MinDepthSpan =
				bInversePerspective ? 1.0e-6f : 1.0e-4f;

		return
				IsFinite(MinX) &&
				IsFinite(MaxX) &&
				IsFinite(MinY) &&
				IsFinite(MaxY) &&
				IsFinite(MinDepth) &&
				IsFinite(MaxDepth) &&
				(MaxX - MinX) > MinXYSpan &&
				(MaxY - MinY) > MinXYSpan &&
				std::fabs(MaxDepth - MinDepth) > MinDepthSpan;
	}

	bool TryBuildCascadePSMView(
		const FViewContext&          View,
		const FPSMBoundsWS&          ReceiverBoundsWS,
		const FPSMBoundsWS&          PotentialCasterBoundsWS,
		const FVector&               LightSourceDirectionWS,
		float                        NearSplit,
		float                        FarSplit,
		const FVector                FrustumCornersWS[8],
		const FShadowViewRenderItem& StableCascadeView,
		FShadowViewRenderItem&       OutView)
	{
		if (View.bOrthographic || FarSplit <= NearSplit + 1.0e-3f)
		{
			return false;
		}

		FMatrix VirtualView             = FMatrix::Identity;
		FMatrix VirtualProjection       = FMatrix::Identity;
		FVector VirtualCameraPositionWS = View.CameraPosition;
		float   VirtualNear             = NearSplit;
		float   VirtualFar              = FarSplit;

		if (!BuildVirtualCameraForCascade(
			View,
			ReceiverBoundsWS,
			PotentialCasterBoundsWS,
			LightSourceDirectionWS,
			NearSplit,
			FarSplit,
			VirtualView,
			VirtualProjection,
			VirtualCameraPositionWS,
			VirtualNear,
			VirtualFar))
		{
			return false;
		}

		FPostPerspectiveBuildResult PPResult;

		if (!BuildPostPerspectiveInfo(
			VirtualView,
			VirtualProjection,
			LightSourceDirectionWS,
			PPResult))
		{
			return false;
		}

		const FMatrix PSMProjection =
				VirtualProjection * PPResult.ViewPP * PPResult.ProjectionPP;

		const FMatrix PSMViewProjection =
				VirtualView * PSMProjection;

		if (!IsUsableProjectionMatrix(PSMProjection) ||
			!IsUsableProjectionMatrix(PSMViewProjection))
		{
			return false;
		}

		if (!ValidateShadowProjectionForCascade(
			PSMViewProjection,
			FrustumCornersWS,
			PPResult.bInversePerspective))
		{
			return false;
		}

		OutView                = StableCascadeView;
		OutView.ProjectionType = EShadowProjectionType::Perspective;
		OutView.PositionWS     = VirtualCameraPositionWS;
		OutView.NearZ          = VirtualNear;
		OutView.FarZ           = VirtualFar;
		OutView.View           = VirtualView;
		OutView.Projection     = PSMProjection;
		OutView.ViewProjection = PSMViewProjection;

		OutView.BiasParams.W =
				PPResult.bInversePerspective ? 2.0f : 1.0f;

		return true;
	}

	void BuildSpotShadowViews(
		FSceneLightingInputs&        Inputs,
		const USpotLightComponent*   Spot,
		const FLocalLightRenderItem& LightItem,
		uint32                       LocalLightIndex,
		uint32                       ShadowLightIndex,
		const FVector&               CameraPosition)
	{
		FShadowLightRenderItem& ShadowLight =
				Inputs.ShadowLights[ShadowLightIndex];

		ShadowLight.LightType   = EShadowLightType::Spot;
		ShadowLight.PositionWS  = LightItem.PositionWS;
		ShadowLight.DirectionWS = LightItem.DirectionWS;
		ShadowLight.Mobility    = Spot->GetMobility();
		ShadowLight.bCacheDirty = Spot->IsCacheDirty();
		Spot->ResetShadowCacheDirty();
		ShadowLight.Bias       = Spot->GetShadowBias();
		ShadowLight.SlopeBias  = Spot->GetShadowSlopeBias();
		ShadowLight.NormalBias = 0.0f;
		ShadowLight.Sharpen    = Spot->GetShadowSharpen();

		const FVector DirectionWS =
				LightItem.DirectionWS.GetSafeNormal();

		FVector UpWS = FVector::UpVector;
		if (std::abs(FVector::DotProduct(DirectionWS, UpWS)) > 0.98f)
		{
			UpWS = FVector::RightVector;
		}

		const float NearZ = ShadowConfig::DefaultNearZ;
		const float FarZ  =
				(std::max)(LightItem.Range, NearZ + 0.001f);

		const float OuterHalfAngleDeg =
				FMath::Clamp(Spot->GetOuterConeAngle(), 1.0f, 80.0f);

		const float FullFovRad =
				FMath::DegreesToRadians(OuterHalfAngleDeg * 2.0f);

		const float Coverage =
				ComputeSpotScreenCoverage(
					CameraPosition,
					LightItem.PositionWS,
					LightItem.DirectionWS,
					LightItem.Range,
					Spot->GetOuterConeAngle());

		const float ResolutionScale =
				Spot->GetShadowResolutionScale();

		const float ResolutionFactor =
				std::sqrt(Coverage) * ResolutionScale;

		const uint32 RequestedResolution =
				QuantizeShadowResolution(
					static_cast<uint32>(
						ShadowConfig::DefaultShadowMapResolution *
						ResolutionFactor));

		FShadowViewRenderItem View;
		View.ProjectionType = EShadowProjectionType::Perspective;
		View.PositionWS     = LightItem.PositionWS;
		View.NearZ          = NearZ;
		View.FarZ           = FarZ;
		View.SourceActor    = Spot->GetOwner();

		View.View =
				FMatrix::MakeViewLookAtLH(
					LightItem.PositionWS,
					LightItem.PositionWS + DirectionWS,
					UpWS);

		View.Projection =
				FMatrix::MakePerspectiveFovLH(
					FullFovRad,
					1.0f,
					NearZ,
					FarZ);

		View.ViewProjection =
				View.View * View.Projection;

		View.LightType           = EShadowLightType::Spot;
		View.Viewport            = {};
		View.RequestedResolution = RequestedResolution;

		AddShadowView(Inputs, ShadowLightIndex, View);
	}

	void BuildDirectionalShadowViews(
		FSceneLightingInputs&             Inputs,
		const UDirectionalLightComponent* DirLight,
		FDirectionalLightRenderItem&      LightItem,
		uint32                            ShadowLightIndex,
		const FViewContext&               View)
	{
		FShadowLightRenderItem& ShadowLight =
				Inputs.DirShadowLights[ShadowLightIndex];

		ShadowLight.LightType   = EShadowLightType::Directional;
		ShadowLight.PositionWS  = FVector::ZeroVector;
		ShadowLight.DirectionWS = LightItem.DirectionWS;
		ShadowLight.Bias        = DirLight->GetShadowBias();
		ShadowLight.SlopeBias   = DirLight->GetShadowSlopeBias();
		ShadowLight.NormalBias  = 0.0f;
		ShadowLight.Sharpen     = DirLight->GetShadowSharpen();

		uint32 CascadeCount = DirLight->GetCascadeCount();
		CascadeCount        =
				(std::min)(CascadeCount, ShadowConfig::MaxDirCascade);

		TArray<float> FrustumSplits =
				FCasCade::CalculateCascadeSplits(
					CascadeCount,
					View.NearZ,
					View.FarZ,
					0.9f);

		if (FrustumSplits.size() < 2)
		{
			return;
		}

		LightItem.CascadeSplits = FVector4(
			FrustumSplits.size() > 1 ? FrustumSplits[1] : 0.0f,
			FrustumSplits.size() > 2 ? FrustumSplits[2] : 0.0f,
			FrustumSplits.size() > 3 ? FrustumSplits[3] : 0.0f,
			FrustumSplits.size() > 4 ? FrustumSplits[4] : 0.0f
		);

		const FVector LightRayDirectionWS =
				LightItem.DirectionWS.GetSafeNormal();

		const FVector LightSourceDirectionWS =
				-LightRayDirectionWS;

		FVector UpVector =
				(std::abs(LightRayDirectionWS.Z) > 0.999f)
					? FVector::YAxisVector
					: FVector::ZAxisVector;

		const FMatrix CameraInvView = View.InverseView;
		const FMatrix CameraInvProj = View.InverseProjection;

		FVector4 NDC_Corners[4] = {
			FVector4(-1.0f, 1.0f, 1.0f, 1.0f),
			FVector4(1.0f, 1.0f, 1.0f, 1.0f),
			FVector4(1.0f, -1.0f, 1.0f, 1.0f),
			FVector4(-1.0f, -1.0f, 1.0f, 1.0f)
		};

		FVector ViewRays[4];
		bool    bValidViewRays = true;

		for (int j = 0; j < 4; ++j)
		{
			FVector4 ViewCorner =
					NDC_Corners[j] * CameraInvProj;

			if (std::fabs(ViewCorner.W) <= 1.0e-6f)
			{
				bValidViewRays = false;
				break;
			}

			const float InvW = 1.0f / ViewCorner.W;

			FVector Ray(
				ViewCorner.X * InvW,
				ViewCorner.Y * InvW,
				ViewCorner.Z * InvW);

			if (std::fabs(Ray.X) <= 1.0e-6f)
			{
				bValidViewRays = false;
				break;
			}

			ViewRays[j] = Ray * (1.0f / Ray.X);
		}

		if (!bValidViewRays)
		{
			return;
		}

		const uint32 MaxPSMCascadeCount = 2;

		for (uint32 i = 0; i < CascadeCount; ++i)
		{
			const float NearSplit = FrustumSplits[i];
			const float FarSplit  = FrustumSplits[i + 1];

			FVector FrustumCornersWS[8];

			for (int j = 0; j < 4; ++j)
			{
				const FVector NearVS =
						ViewRays[j] * NearSplit;

				const FVector FarVS =
						ViewRays[j] * FarSplit;

				FrustumCornersWS[j] =
						CameraInvView.TransformPosition(NearVS);

				FrustumCornersWS[j + 4] =
						CameraInvView.TransformPosition(FarVS);
			}

			FVector FrustumCenter = FVector::ZeroVector;
			for (int j = 0; j < 8; ++j)
			{
				FrustumCenter += FrustumCornersWS[j];
			}
			FrustumCenter /= 8.0f;

			float SphereRadius = 0.0f;
			for (int j = 0; j < 8; ++j)
			{
				SphereRadius =
						(std::max)(
							SphereRadius,
							FVector::Dist(FrustumCenter, FrustumCornersWS[j]));
			}

			SphereRadius =
					std::ceil(SphereRadius * 16.0f) / 16.0f;

			const float BoxWidth  = SphereRadius * 2.0f;
			const float BoxHeight = SphereRadius * 2.0f;

			const float WorldUnitsPerTexel =
					BoxWidth / ShadowConfig::DirShadowDepthResolution;

			FMatrix TempShadowView =
					FMatrix::MakeViewLookAtLH(
						FVector::ZeroVector,
						LightRayDirectionWS,
						UpVector);

			FVector CenterLS =
					TempShadowView.TransformPosition(FrustumCenter);

			CenterLS.Y =
					std::floor(CenterLS.Y / WorldUnitsPerTexel) *
					WorldUnitsPerTexel;

			CenterLS.Z =
					std::floor(CenterLS.Z / WorldUnitsPerTexel) *
					WorldUnitsPerTexel;

			const FVector SnappedCenterWS =
					TempShadowView.GetInverse().TransformPosition(CenterLS);

			const FVector LightPosition =
					SnappedCenterWS;

			const float BoxNear = -SphereRadius;
			const float BoxFar  = SphereRadius;

			FShadowViewRenderItem ViewItem;
			ViewItem.ProjectionType      = EShadowProjectionType::Orthographic;
			ViewItem.PositionWS          = FrustumCenter;
			ViewItem.NearZ               = BoxNear;
			ViewItem.FarZ                = BoxFar;
			ViewItem.RequestedResolution =
					ShadowConfig::DirShadowDepthResolution;

			ViewItem.BiasParams = {
				DirLight->GetShadowBias(),
				DirLight->GetShadowSlopeBias(),
				0.0f,
				0.0f
			};

			ViewItem.View =
					FMatrix::MakeViewLookAtLH(
						LightPosition,
						LightPosition + LightRayDirectionWS,
						UpVector);

			ViewItem.Projection =
					FMatrix::MakeOrthographicLH(
						BoxWidth,
						BoxHeight,
						BoxNear,
						BoxFar);

			ViewItem.ViewProjection =
					ViewItem.View * ViewItem.Projection;

			ViewItem.Viewport = {};

			const FPSMBoundsWS CascadeReceiverBoundsWS =
					BuildBoundsFromCornersWS(FrustumCornersWS);

			const float CasterSearchDistance =
					FMath::Clamp(
						(FarSplit - NearSplit) * 2.0f,
						100.0f,
						2000.0f);

			const FPSMBoundsWS PotentialCasterBoundsWS =
					BuildPotentialCasterBoundsWS(
						CascadeReceiverBoundsWS,
						LightSourceDirectionWS,
						CasterSearchDistance);

			const bool bTryPSMForThisCascade =
					i < MaxPSMCascadeCount;

			FShadowViewRenderItem PSMViewItem;

			if (bTryPSMForThisCascade &&
				TryBuildCascadePSMView(
					View,
					CascadeReceiverBoundsWS,
					PotentialCasterBoundsWS,
					LightSourceDirectionWS,
					NearSplit,
					FarSplit,
					FrustumCornersWS,
					ViewItem,
					PSMViewItem))
			{
				ViewItem = PSMViewItem;
			}

			AddDirShadowView(
				Inputs,
				ShadowLightIndex,
				ViewItem);
		}
	}

	void BuildPointShadowViews(
		FSceneLightingInputs&        Inputs,
		const UPointLightComponent*  Point,
		const FLocalLightRenderItem& LightItem,
		uint32                       ShadowLightIndex,
		uint32                       CubeArrayIndex)
	{
		static const FVector CubeFaceLook[6] = {
			{ 1, 0, 0 },
			{ -1, 0, 0 },
			{ 0, 1, 0 },
			{ 0, -1, 0 },
			{ 0, 0, 1 },
			{ 0, 0, -1 },
		};

		static const FVector CubeFaceUp[6] = {
			{ 0, 1, 0 },
			{ 0, 1, 0 },
			{ 0, 0, -1 },
			{ 0, 0, 1 },
			{ 0, 1, 0 },
			{ 0, 1, 0 },
		};

		FShadowLightRenderItem& ShadowLight =
				Inputs.ShadowLights[ShadowLightIndex];

		ShadowLight.PositionWS  = LightItem.PositionWS;
		ShadowLight.Mobility    = Point->GetMobility();
		ShadowLight.bCacheDirty = Point->IsCacheDirty();
		Point->ResetShadowCacheDirty();
		ShadowLight.Bias           = Point->GetShadowBias();
		ShadowLight.SlopeBias      = Point->GetShadowSlopeBias();
		ShadowLight.Sharpen        = Point->GetShadowSharpen();
		ShadowLight.CubeArrayIndex = CubeArrayIndex;

		const float NearZ = ShadowConfig::DefaultNearZ;
		const float FarZ  =
				(std::max)(LightItem.Range, NearZ + 0.001f);

		const uint32 BaseSlice =
				ShadowConfig::PointShadowSliceOffset + CubeArrayIndex * 6;

		for (uint32 F = 0; F < 6; ++F)
		{
			FShadowViewRenderItem View;
			View.ProjectionType      = EShadowProjectionType::Perspective;
			View.PositionWS          = LightItem.PositionWS;
			View.NearZ               = NearZ;
			View.FarZ                = FarZ;
			View.RequestedResolution =
					ShadowConfig::DefaultShadowMapResolution;

			View.View =
					FMatrix::MakeViewLookAtLH(
						LightItem.PositionWS,
						LightItem.PositionWS + CubeFaceLook[F],
						CubeFaceUp[F]);

			View.Projection =
					FMatrix::MakePerspectiveFovLH(
						FMath::DegreesToRadians(90.0f),
						1.0f,
						NearZ,
						FarZ);

			View.ViewProjection =
					View.View * View.Projection;

			View.FilterMode = EShadowFilterMode::Raw;
			View.LightType  = EShadowLightType::Point;

			AddPointShadowView(
				Inputs,
				ShadowLightIndex,
				BaseSlice + F,
				View);
		}
	}

	float ComputeShadowPriority(
		const FVector& LightPos,
		const FVector& CameraPos)
	{
		return (LightPos - CameraPos).SizeSquared();
	}
}


void FSceneCommandLightingBuilder::BuildLightingInputs(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket&        Packet,
	const FViewContext&              View,
	FSceneViewData&                  OutSceneViewData) const
{
	(void)Packet;
	(void)View;
	FSceneLightingInputs& LightingInputs = OutSceneViewData.LightingInputs;
	LightingInputs.Clear();

	LightingInputs.Ambient.Color     = FVector::OneVector;
	LightingInputs.Ambient.Intensity = 0.0f;

	if (!BuildContext.World)
	{
		return;
	}


	//temp Condidtate colletion vector
	struct FPointShadowCandidate
	{
		const UPointLightComponent* PointLightl = nullptr;
		FLocalLightRenderItem       LightItem;
		uint32                      LocalLightIndex = 0;
		float                       SortKey         = 0.0f;
	};
	std::vector<FPointShadowCandidate> ShadowCandidates;
	ShadowCandidates.reserve(16);

	FVector                     AmbientRadiance      = FVector::ZeroVector;
	float                       AmbientIntensitySum  = 0.0f;
	bool                        bHasAmbientLight     = false;
	bool                        bHasDirectionalLight = false;
	float                       StrongestDirectional = -1.0f;
	FDirectionalLightRenderItem DirectionalLightItem;

	const TArray<AActor*> Actors = BuildContext.World->GetAllActors();
	LightingInputs.LocalLights.reserve(Actors.size());

	for (AActor* Actor : Actors)
	{
		if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible())
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component || Component->IsPendingKill() || !Component->IsRegistered())
			{
				continue;
			}

			if (Component->IsA(UAmbientLightComponent::StaticClass()))
			{
				const UAmbientLightComponent* Ambient = static_cast<UAmbientLightComponent*>(Component);
				if (!Ambient->GetVisible())
				{
					continue;
				}

				const float EffectiveIntensity = Ambient->GetEffectiveIntensity();
				if (EffectiveIntensity <= 0.0f)
				{
					continue;
				}

				AmbientRadiance     += ToLightRGB(Ambient->GetColor()) * EffectiveIntensity;
				AmbientIntensitySum += EffectiveIntensity;
				bHasAmbientLight    = true;
				continue;
			}

			if (Component->IsA(UDirectionalLightComponent::StaticClass()))
			{
				const UDirectionalLightComponent* Directional = static_cast<UDirectionalLightComponent*>(Component);
				if (!Directional->GetVisible() || Directional->GetEffectiveIntensity() <= 0.0f)
				{
					continue;
				}

				const FDirectionalLightRenderItem Candidate = BuildDirectionalLight(Directional);
				if (!bHasDirectionalLight || Candidate.Intensity > StrongestDirectional)
				{
					DirectionalLightItem = Candidate;
					StrongestDirectional = Candidate.Intensity;
					bHasDirectionalLight = true;

					LightingInputs.DirShadowLights.clear();
					LightingInputs.DirShadowViews.clear();

					if (Directional->IsCastingShadows())
					{
						const uint32 ShadowLightIndex = AllocateDirShadowLight(LightingInputs, EShadowLightType::Directional, 0);
						if (ShadowLightIndex != UINT32_MAX)
						{
							BuildDirectionalShadowViews(LightingInputs, Directional, DirectionalLightItem, ShadowLightIndex, View);
						}
					}
				}
				continue;
			}

			if (Component->IsA(USpotLightComponent::StaticClass()))
			{
				const USpotLightComponent* Spot = static_cast<USpotLightComponent*>(Component);
				if (!Spot->GetVisible() || Spot->GetEffectiveIntensity() <= 0.0f || Spot->GetAttenuationRadius() <= 0.0f)
				{
					continue;
				}

				FLocalLightRenderItem LightItem       = BuildSpotLight(Spot);
				const uint32          LocalLightIndex = static_cast<uint32>(LightingInputs.LocalLights.size());
				if (Spot->IsCastingShadows())
				{
					const uint32 ShadowLightIndex = AllocalteShadowLight(LightingInputs, EShadowLightType::Spot, LocalLightIndex);

					if (ShadowLightIndex != UINT32_MAX)
					{
						BuildSpotShadowViews(LightingInputs, Spot, LightItem, LocalLightIndex, ShadowLightIndex, View.CameraPosition);
						if (LightingInputs.ShadowLights[ShadowLightIndex].ViewCount > 0)
						{
							LightItem.ShadowIndex = ShadowLightIndex;
						}
					}
				}

				LightingInputs.LocalLights.push_back(LightItem);
				continue;
			}

			if (Component->IsA(UPointLightComponent::StaticClass()))
			{
				const UPointLightComponent* Point = static_cast<UPointLightComponent*>(Component);
				if (!Point->GetVisible() || Point->GetEffectiveIntensity() <= 0.0f
					|| Point->GetAttenuationRadius() <= 0.0f)
				{
					continue;
				}

				FLocalLightRenderItem LightItem       = BuildPointLight(Point);
				const uint32          LocalLightIndex = static_cast<uint32>(LightingInputs.LocalLights.size());

				if (Point->IsCastingShadows())
				{
					// 후보로만 수집 등록은 정렬 후 2차 패스에서
					FPointShadowCandidate Candidate;
					Candidate.PointLightl     = Point;
					Candidate.LightItem       = LightItem;
					Candidate.LocalLightIndex = LocalLightIndex;
					Candidate.SortKey         = ComputeShadowPriority(LightItem.PositionWS, OutSceneViewData.View.CameraPosition);
					ShadowCandidates.push_back(Candidate);
				}
				else
				{
					LightingInputs.LocalLights.push_back(LightItem);
				}
				continue;
			}
		}
	}
	std::sort(ShadowCandidates.begin(), ShadowCandidates.end(),
	          [](const FPointShadowCandidate& A, const FPointShadowCandidate& B)
	          {
		          ;
		          return A.SortKey < B.SortKey;
	          });

	uint32 PointCubeCounter = 0;

	for (FPointShadowCandidate& Candidate : ShadowCandidates)
	{
		FLocalLightRenderItem LightItem = Candidate.LightItem;

		if (PointCubeCounter < ShadowConfig::MaxPointShadowCubes)
		{
			const uint32 ShadowLightIndex = AllocalteShadowLight(
				LightingInputs, EShadowLightType::Point, Candidate.LocalLightIndex);

			if (ShadowLightIndex != UINT32_MAX)
			{
				BuildPointShadowViews(LightingInputs, Candidate.PointLightl, LightItem,
				                      ShadowLightIndex, PointCubeCounter);

				if (LightingInputs.ShadowLights[ShadowLightIndex].ViewCount == 6)
				{
					LightItem.ShadowIndex = ShadowLightIndex;
					++PointCubeCounter;
				}
			}
		}

		LightingInputs.LocalLights.push_back(LightItem);
	}
	if (bHasAmbientLight && AmbientIntensitySum > 0.0f)
	{
		LightingInputs.Ambient.Color     = AmbientRadiance / AmbientIntensitySum;
		LightingInputs.Ambient.Intensity = AmbientIntensitySum;
	}

	if (bHasDirectionalLight)
	{
		LightingInputs.DirectionalLights.push_back(DirectionalLightItem);
	}
}
