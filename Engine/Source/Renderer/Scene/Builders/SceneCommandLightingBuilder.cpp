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

		const float InnerAngleRad = FMath::DegreesToRadians(FMath::Clamp(C->GetInnerConeAngle(), 0.0f, 89.0f));
		const float OuterAngleRad = FMath::DegreesToRadians(FMath::Clamp(C->GetOuterConeAngle(), 0.0f, 89.0f));

		L.InnerAngleCos = std::cos(InnerAngleRad);
		L.OuterAngleCos = std::cos(OuterAngleRad);

		if (L.InnerAngleCos < L.OuterAngleCos)
		{
			std::swap(L.InnerAngleCos, L.OuterAngleCos);
		}

		// conservative sphere
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

	uint32 AllocalteShadowLight(FSceneLightingInputs& Inputs, EShadowLightType LightType, uint32 SourceLightIndex)
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

	uint32 AddShadowView(FSceneLightingInputs& Inputs, uint32 ShadowLightIndex, const FShadowViewRenderItem& InView)
	{
		if (Inputs.ShadowViews.size() >= ShadowConfig::MaxShadowViews)
			return UINT32_MAX;

		FShadowViewRenderItem View = InView;
		View.ShadowLightIndex = ShadowLightIndex;
		View.ArraySlice = static_cast<uint32>(Inputs.ShadowViews.size());

		const uint32 ViewIndex = static_cast<uint32>(Inputs.ShadowViews.size());
		Inputs.ShadowViews.push_back(std::move(View));

		FShadowLightRenderItem& ShadowLight = Inputs.ShadowLights[ShadowLightIndex];

		if (ShadowLight.ViewCount == 0)
		{
			ShadowLight.FirstViewIndex = ViewIndex;
		}

		++ShadowLight.ViewCount;

		return ViewIndex;
	}
	float ComputeSpotScreenCoverage(
		const FVector& LightPosition,
		const FVector& LightDirection,
		float Range,
		float OuterConeAngleDeg,
		const FMatrix& ViewProj)
	{
		const float SafeRange = (std::max)(Range, 0.0f);
		const FVector Direction = LightDirection.GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return 0.0f;
		}

		const float OuterAngleRad = FMath::DegreesToRadians(
			FMath::Clamp(OuterConeAngleDeg, 1.0f, 80.0f));

		const float ConeEndRadius = std::tan(OuterAngleRad) * SafeRange;
		const FVector EndCenter = LightPosition + Direction * SafeRange;

		FVector Up = FVector(0.0f, 0.0f, 1.0f);
		if (std::abs(FVector::DotProduct(Direction, Up)) > 0.95f)
		{
			Up = FVector(0.0f, 1.0f, 0.0f);
		}

		const FVector Right = FVector::CrossProduct(Up, Direction).GetSafeNormal();
		const FVector ConeUp = FVector::CrossProduct(Direction, Right).GetSafeNormal();

		float MinX = FLT_MAX;
		float MinY = FLT_MAX;
		float MaxX = -FLT_MAX;
		float MaxY = -FLT_MAX;

		int32 ProjectedCount = 0;

		auto AddProjectedPoint = [&](const FVector& WorldPos)
			{
				const FVector4 Clip = ViewProj.TransformVector4(FVector4(WorldPos, 1.0f));

				if (Clip.W <= 1.0e-4f)
					return;

				const float InvW = 1.0f / Clip.W;
				const float NdcX = Clip.X * InvW;
				const float NdcY = Clip.Y * InvW;

				MinX = std::min(MinX, NdcX);
				MinY = std::min(MinY, NdcY);
				MaxX = std::max(MaxX, NdcX);
				MaxY = std::max(MaxY, NdcY);

				++ProjectedCount;
			};

		AddProjectedPoint(LightPosition);
		AddProjectedPoint(EndCenter);

		constexpr int32 SegmentCount = 12;

		for (int32 i = 0; i < SegmentCount; ++i)
		{
			const float T = (2.0f * FMath::PI * i) / SegmentCount;

			const FVector P =
				EndCenter
				+ Right * std::cos(T) * ConeEndRadius
				+ ConeUp * std::sin(T) * ConeEndRadius;

			AddProjectedPoint(P);
		}

		if (ProjectedCount == 0)
			return 0.0f;

		MinX = FMath::Clamp(MinX, -1.0f, 1.0f);
		MinY = FMath::Clamp(MinY, -1.0f, 1.0f);
		MaxX = FMath::Clamp(MaxX, -1.0f, 1.0f);
		MaxY = FMath::Clamp(MaxY, -1.0f, 1.0f);

		const float Width = std::max(0.0f, MaxX - MinX);
		const float Height = std::max(0.0f, MaxY - MinY);

		const float Coverage = (Width * Height) * 0.25f;

		return FMath::Clamp(Coverage, 0.0f, 1.0f);
	}

	uint32 QuantizeShadowResolution(uint32 Resolution)
	{
		if (Resolution >= 2048) return 2048;
		if (Resolution >= 1024) return 1024;
		if (Resolution >= 512)  return 512;
		if (Resolution >= 256)  return 256;
		if (Resolution >= 128)  return 128;
		return 64;
	}
	uint32 QuantizeDiraShadowResolution(uint32 Resolution)
	{
		if (Resolution >= 4096) return 4096;
		return QuantizeShadowResolution(Resolution);
	}

	uint32 AddPointShadowView(FSceneLightingInputs& Inputs, uint32 ShadowLightIndex, uint32 ExplicitArraySlice, const FShadowViewRenderItem& InView) 
	{
		FShadowViewRenderItem View = InView;
		View.ShadowLightIndex = ShadowLightIndex;
		View.ArraySlice = ExplicitArraySlice;

		uint32 ViewIndex = static_cast<uint32>(Inputs.ShadowViews.size());
		Inputs.ShadowViews.push_back(std::move(View));

		FShadowLightRenderItem& Light = Inputs.ShadowLights[ShadowLightIndex];
		if (Light.ViewCount == 0)
			Light.FirstViewIndex = ViewIndex;
		Light.ViewCount++;
		Light.LightType = EShadowLightType::Point;

		return ViewIndex;
	}

	uint32 AllocateDirShadowLight(FSceneLightingInputs& Inputs, EShadowLightType LightType, uint32 SourceLightIndex)
	{
		if (Inputs.DirShadowLights.size() >= ShadowConfig::MaxShadowLights)
		{
			return UINT32_MAX;
		}

		FShadowLightRenderItem Item = {};
		Item.LightType = LightType;
		Item.SourceLightIndex = SourceLightIndex;
		Item.ShadowIndex = static_cast<uint32>(Inputs.DirShadowLights.size());
		Inputs.DirShadowLights.push_back(Item);

		return Item.ShadowIndex;
	}

	uint32 AddDirShadowView(FSceneLightingInputs& Inputs, uint32 ShadowLightIndex, const FShadowViewRenderItem& InView)
	{
		if (Inputs.DirShadowViews.size() >= ShadowConfig::MaxDirCascade)
		{
			return UINT32_MAX;
		}

		FShadowViewRenderItem View = InView;
		View.ShadowLightIndex = ShadowLightIndex;
		View.ArraySlice = static_cast<uint32>(Inputs.DirShadowViews.size());

		const uint32 ViewIndex = static_cast<uint32>(Inputs.DirShadowViews.size());
		Inputs.DirShadowViews.push_back(std::move(View));

		FShadowLightRenderItem& ShadowLight = Inputs.DirShadowLights[ShadowLightIndex];
		if (ShadowLight.ViewCount == 0)
		{
			ShadowLight.FirstViewIndex = ViewIndex;
		}
		++ShadowLight.ViewCount;

		return ViewIndex;
	}


	void BuildSpotShadowViews(
		FSceneLightingInputs&        Inputs,
		const USpotLightComponent*   Spot,
		const FLocalLightRenderItem& LightItem,
		uint32                       LocalLightIndex,
		uint32                       ShadowLightIndex,
		const FMatrix&				 ViewProjMatrix)
	{
		FShadowLightRenderItem& ShadowLight = Inputs.ShadowLights[ShadowLightIndex];

		ShadowLight.LightType   = EShadowLightType::Spot;
		ShadowLight.PositionWS  = LightItem.PositionWS;
		ShadowLight.DirectionWS = LightItem.DirectionWS;
		ShadowLight.Mobility    = Spot->GetMobility();
		ShadowLight.bCacheDirty = Spot->IsCacheDirty();
		Spot->ResetShadowCacheDirty();
		ShadowLight.Bias        = Spot->GetShadowBias();
		ShadowLight.SlopeBias   = Spot->GetShadowSlopeBias();
		ShadowLight.NormalBias  = 0.0f;
		ShadowLight.Sharpen     = Spot->GetShadowSharpen();
		ShadowLight.ESMExponent = Spot->GetShadowESMExponent();

		const FVector DirectionWS = LightItem.DirectionWS.GetSafeNormal();

		FVector UpWS = FVector::UpVector;
		if (std::abs(FVector::DotProduct(DirectionWS, UpWS)) > 0.98f)
		{
			UpWS = FVector::RightVector;
		}

		const float NearZ = ShadowConfig::DefaultNearZ;
		const float FarZ  = (std::max)(LightItem.Range, NearZ + 0.001f);

		const float OuterHalfAngleDeg = FMath::Clamp(Spot->GetOuterConeAngle(), 1.0f, 80.0f);
		const float FullFovRad        = FMath::DegreesToRadians(OuterHalfAngleDeg * 2.0f);

		const float Coverage = ComputeSpotScreenCoverage(LightItem.PositionWS, LightItem.DirectionWS,LightItem.Range, Spot->GetOuterConeAngle(), ViewProjMatrix);
		if(Coverage <= 0.0f)
		{
			return;
		}

		float ResolutionFactor = std::sqrt(Coverage) * Spot->GetShadowResolutionScale();
		uint32 RequestedResolution = QuantizeShadowResolution(static_cast<uint32>(ShadowConfig::DefaultShadowMapResolution * ResolutionFactor));

		FShadowViewRenderItem View;
		View.ProjectionType      = EShadowProjectionType::Perspective;
		View.PositionWS          = LightItem.PositionWS;
		View.NearZ               = NearZ;
		View.FarZ                = FarZ;
		View.SourceActor         = Spot->GetOwner();

		View.View = FMatrix::MakeViewLookAtLH(
			LightItem.PositionWS,
			LightItem.PositionWS + DirectionWS,
			UpWS);

		View.Projection = FMatrix::MakePerspectiveFovLH(
			FullFovRad,
			1.0f,
			NearZ,
			FarZ);

		View.ViewProjection = View.View * View.Projection;
		View.LightType = EShadowLightType::Spot;

		View.Viewport = {};
		View.RequestedResolution = QuantizeShadowResolution(RequestedResolution);

		AddShadowView(Inputs, ShadowLightIndex, View);
	}

	struct FPSMFrustumBasis
	{
		FVector UnitDepthRays[4];
		float FovYRad = FMath::DegreesToRadians(60.0f);
		float Aspect = 1.0f;
	};

	struct FPostPerspectiveShadowCamera
	{
		FMatrix View;
		FMatrix Projection;
		bool bOrthographic = false;
		bool bInversePerspective = false;
	};

	struct FCascadePSMResult
	{
		float SplitNear = 0.0f;
		float SplitFar = 0.0f;
		float VCNear = 0.0f;
		float VCFar = 0.0f;
		float VirtualSlideBack = 0.0f;

		FMatrix VCView;
		FMatrix VCProjection;
		FMatrix PPView;
		FMatrix PPProjection;
		FMatrix PSM;

		bool bPPOrthographic = false;
		bool bInversePP = false;

		float ConstantBias = 0.00001f;
		float SlopeBias = 0.001f;
		float NormalBias = 0.0f;
	};

	FVector TransformDirectionRow(const FMatrix& Matrix, const FVector& Direction)
	{
		const FVector4 V = FVector4(Direction, 0.0f) * Matrix;
		return FVector(V.X, V.Y, V.Z);
	}

	float VectorLength(const FVector& V)
	{
		return std::sqrt((std::max)(0.0f, V.SizeSquared()));
	}

	float LerpFloat(float A, float B, float T)
	{
		return A + (B - A) * T;
	}

	float SmoothStep01(float Edge0, float Edge1, float X)
	{
		if (std::abs(Edge1 - Edge0) <= 1.0e-6f)
		{
			return (X >= Edge1) ? 1.0f : 0.0f;
		}

		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float ComputePSMFrontFacingT(float Dot)
	{
		// Smoothly replaces the old Dot > 0.10 / Dot > 0.35 branches.
		return SmoothStep01(0.05f, 0.45f, Dot);
	}

	float ComputePSMSideToAlignedT(float Dot)
	{
		return FMath::Clamp((std::abs(Dot) - 0.18f) / 0.37f, 0.0f, 1.0f);
	}

	float QuantizePSMDepth(float Value)
	{
		const float Step = 1.0f / 16.0f;
		return std::floor(Value / Step) * Step;
	}

	float QuantizePSMValue(float Value, float Step)
	{
		if (Step <= 0.0f)
		{
			return Value;
		}
		return std::floor(Value / Step + 0.5f) * Step;
	}

	FVector GetCameraPositionWS(const FViewContext& View)
	{
		return View.InverseView.TransformPosition(FVector::ZeroVector);
	}

	FVector GetCameraForwardWS(const FViewContext& View)
	{
		return TransformDirectionRow(View.InverseView, FVector(1.0f, 0.0f, 0.0f)).GetSafeNormal();
	}

	FVector GetCameraUpWS(const FViewContext& View)
	{
		return TransformDirectionRow(View.InverseView, FVector(0.0f, 0.0f, 1.0f)).GetSafeNormal();
	}

	FPSMFrustumBasis ExtractCameraFrustumBasis(const FMatrix& InverseProjection)
	{
		FPSMFrustumBasis Out;

		const FVector4 NdcFarCorners[4] =
		{
			FVector4(-1.0f,  1.0f, 1.0f, 1.0f),
			FVector4( 1.0f,  1.0f, 1.0f, 1.0f),
			FVector4( 1.0f, -1.0f, 1.0f, 1.0f),
			FVector4(-1.0f, -1.0f, 1.0f, 1.0f)
		};

		float MaxAbsY = 0.0f;
		float MaxAbsZ = 0.0f;

		for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
		{
			FVector4 ViewCorner = NdcFarCorners[CornerIndex] * InverseProjection;
			const float InvW = 1.0f / ViewCorner.W;

			FVector Ray(
				ViewCorner.X * InvW,
				ViewCorner.Y * InvW,
				ViewCorner.Z * InvW);

			if (std::abs(Ray.X) < 1.0e-6f)
			{
				Ray.X = (Ray.X < 0.0f) ? -1.0e-6f : 1.0e-6f;
			}

			Ray = Ray * (1.0f / Ray.X);
			Out.UnitDepthRays[CornerIndex] = Ray;

			MaxAbsY = (std::max)(MaxAbsY, std::abs(Ray.Y));
			MaxAbsZ = (std::max)(MaxAbsZ, std::abs(Ray.Z));
		}

		MaxAbsZ = (std::max)(MaxAbsZ, 1.0e-6f);
		Out.FovYRad = 2.0f * std::atan(MaxAbsZ);
		Out.Aspect = (std::max)(0.001f, MaxAbsY / MaxAbsZ);
		return Out;
	}

	float ComputeVirtualSlideBackForCascade(
		const FVector& CameraForwardWS,
		const FVector& LightDirectionWS,
		float SplitNear,
		float SplitFar,
		uint32 CascadeIndex)
	{
		const float Dot = FVector::DotProduct(CameraForwardWS.GetSafeNormal(), LightDirectionWS.GetSafeNormal());
		const float AbsDot = std::abs(Dot);
		const float SideToAlignedT = ComputePSMSideToAlignedT(Dot);
		const float FrontT = ComputePSMFrontFacingT(Dot);
		const float Range = (std::max)(1.0f, SplitFar - SplitNear);

		const float MaxAbsoluteSlide = LerpFloat(40.0f, 757.0f, FrontT);
		const float RangeScale = LerpFloat(0.075f, 0.18f, FrontT);

		float SlideBack =
			(std::min)(MaxAbsoluteSlide, Range * RangeScale)
			* FrontT
			* SideToAlignedT;

		if (CascadeIndex == 0)
		{
			const float TargetScale = LerpFloat(0.44f, 0.58f, SideToAlignedT);
			const float TargetMin = LerpFloat(2.75f, 3.75f, SideToAlignedT);
			const float TargetFarScale = LerpFloat(0.58f, 0.76f, SideToAlignedT);

			const float TargetDepthAtSplitNear = FMath::Clamp(
				Range * TargetScale,
				TargetMin,
				(std::max)(TargetMin, SplitFar * TargetFarScale));

			const float RequiredSlide = (std::max)(0.0f, TargetDepthAtSplitNear - SplitNear);
			const float MaxSlideAbs = LerpFloat(55.0f, 75.0f, SideToAlignedT);
			const float MaxSlideRange = LerpFloat(0.85f, 1.10f, SideToAlignedT);
			const float MaxSlide = (std::min)(MaxSlideAbs, Range * MaxSlideRange);

			SlideBack = FMath::Clamp(
				(std::max)(SlideBack, RequiredSlide),
				0.0f,
				MaxSlide);

			SlideBack = QuantizePSMValue(SlideBack, 0.25f);
		}
		else
		{
			const float SideFade = SmoothStep01(0.10f, 0.22f, AbsDot);
			SlideBack *= SideFade;
		}

		return SlideBack;
	}

	void ComputeCascadeVirtualCameraRange(
		const FVector& CameraForwardWS,
		const FVector& LightDirectionWS,
		float SplitNear,
		float SplitFar,
		float VirtualSlideBack,
		uint32 CascadeIndex,
		float& OutNear,
		float& OutFar)
	{
		const float Range = (std::max)(1.0f, SplitFar - SplitNear);
		const float Dot = FVector::DotProduct(CameraForwardWS.GetSafeNormal(), LightDirectionWS.GetSafeNormal());
		const float SideToAlignedT = ComputePSMSideToAlignedT(Dot);
		const float FrontT = ComputePSMFrontFacingT(Dot);

		float NearMargin = (std::max)(1.0f, Range * 0.02f);
		float FarMargin = NearMargin;

		if (CascadeIndex == 0)
		{
			const float SideNearMargin = FMath::Clamp(Range * 0.010f, 0.10f, 0.90f);
			const float AlignedNearMargin = FMath::Clamp(Range * 0.015f, 0.15f, 1.25f);
			const float SideFarMargin = FMath::Clamp(Range * 0.012f, 0.15f, 1.00f);
			const float AlignedFarMargin = FMath::Clamp(Range * 0.018f, 0.20f, 1.50f);

			NearMargin = LerpFloat(SideNearMargin, AlignedNearMargin, SideToAlignedT);
			FarMargin = LerpFloat(SideFarMargin, AlignedFarMargin, SideToAlignedT);

			const float FrontNearMargin = FMath::Clamp(
				Range * LerpFloat(0.015f, 0.045f, FrontT),
				0.15f,
				2.0f);
			NearMargin = (std::max)(NearMargin, FrontNearMargin * FrontT);
		}
		else
		{
			const float BaseNearMargin = (std::max)(0.35f, Range * 0.015f);
			const float BaseFarMargin = (std::max)(0.35f, Range * 0.015f);
			NearMargin = LerpFloat(BaseNearMargin, NearMargin, SideToAlignedT);
			FarMargin = LerpFloat(BaseFarMargin, FarMargin, SideToAlignedT);

			const float ExtraNearScale = LerpFloat(0.015f, 0.12f, FrontT);
			NearMargin = (std::max)(NearMargin, Range * ExtraNearScale * FrontT);
		}

		OutNear = (std::max)(0.01f, SplitNear + VirtualSlideBack - NearMargin);
		OutFar = (std::max)(OutNear + 1.0f, SplitFar + VirtualSlideBack + FarMargin);

		OutNear = (std::max)(0.01f, QuantizePSMDepth(OutNear));
		OutFar = (std::max)(OutNear + 1.0f, QuantizePSMDepth(OutFar));
	}

	FPostPerspectiveShadowCamera BuildPostPerspectiveShadowCamera(
		const FMatrix& VCView,
		const FMatrix& VCProjection,
		const FVector& LightDirectionWS,
		uint32 CascadeIndex)
	{
		FPostPerspectiveShadowCamera Result;

		const FVector PPCenter(0.0f, 0.0f, 0.5f);
		const float PPRadius = 1.5f;

		const FVector LightToSourceDirWS = -LightDirectionWS.GetSafeNormal();

		const FVector4 EyeLightDir = FVector4(LightToSourceDirWS, 0.0f) * VCView;
		const FVector4 LightPPH = EyeLightDir * VCProjection;

		const float WEpsilon = 1.5e-3f;
		const bool bOrthoLike = std::abs(LightPPH.W) <= WEpsilon;

		if (bOrthoLike)
		{
			FVector LightDirPP(LightPPH.X, LightPPH.Y, LightPPH.Z);
			if (LightDirPP.IsNearlyZero())
			{
				LightDirPP = FVector(1.0f, 0.0f, 0.0f);
			}
			LightDirPP = LightDirPP.GetSafeNormal();

			const FVector LightPositionPP = PPCenter + LightDirPP * (PPRadius * 2.0f);
			const FVector ForwardPP = (PPCenter - LightPositionPP).GetSafeNormal();

			FVector UpPP = FVector::ZAxisVector;
			if (std::abs(FVector::DotProduct(ForwardPP, UpPP)) > 0.99f)
			{
				UpPP = FVector::YAxisVector;
			}

			const float DistToCenter = VectorLength(PPCenter - LightPositionPP);
			const float NearPP = (std::max)(0.001f, DistToCenter - PPRadius);
			const float FarPP = (std::max)(NearPP + 0.001f, DistToCenter + PPRadius);

			Result.View = FMatrix::MakeViewLookAtLH(LightPositionPP, PPCenter, UpPP);
			Result.Projection = FMatrix::MakeOrthographicLH(PPRadius * 2.0f, PPRadius * 2.0f, NearPP, FarPP);
			Result.bOrthographic = true;
			Result.bInversePerspective = false;
			return Result;
		}

		const float AbsW = std::abs(LightPPH.W);
		const float WSign = (LightPPH.W < 0.0f) ? -1.0f : 1.0f;
		const float WSoft = 0.002f;
		const float SafeAbsW = std::sqrt(AbsW * AbsW + WSoft * WSoft);
		const float SafeW = WSign * SafeAbsW;
		const float InvW = 1.0f / SafeW;

		const FVector LightPositionPP(
			LightPPH.X * InvW,
			LightPPH.Y * InvW,
			LightPPH.Z * InvW);

		const FVector ToCenter = PPCenter - LightPositionPP;
		const float DistToCenter = (std::max)(0.001f, VectorLength(ToCenter));
		const FVector ForwardPP = ToCenter * (1.0f / DistToCenter);

		FVector UpPP = FVector::ZAxisVector;
		if (std::abs(FVector::DotProduct(ForwardPP, UpPP)) > 0.99f)
		{
			UpPP = FVector::YAxisVector;
		}

		const float FovPP = 2.0f * std::atan(PPRadius / DistToCenter);
		const float AspectPP = 1.0f;

		Result.View = FMatrix::MakeViewLookAtLH(LightPositionPP, PPCenter, UpPP);
		Result.bOrthographic = false;

		if (LightPPH.W < 0.0f)
		{
			const float MinPlane = (CascadeIndex == 0) ? 0.24f : 0.16f;
			const float Plane = (std::max)(MinPlane, DistToCenter - PPRadius);
			Result.Projection = FMatrix::MakePerspectiveFovLH(FovPP, AspectPP, -Plane, Plane);
			Result.bInversePerspective = true;
		}
		else
		{
			const float NearPP = (std::max)(0.1f, DistToCenter - PPRadius);
			const float FarPP = (std::max)(NearPP + 0.001f, DistToCenter + PPRadius);
			Result.Projection = FMatrix::MakePerspectiveFovLH(FovPP, AspectPP, NearPP, FarPP);
			Result.bInversePerspective = false;
		}

		return Result;
	}

	void ComputeCascadePSMBias(
		const FVector& CameraForwardWS,
		const FVector& LightDirectionWS,
		uint32 CascadeIndex,
		float SplitNear,
		float SplitFar,
		bool bInversePP,
		const UDirectionalLightComponent* DirLight,
		float& OutConstantBias,
		float& OutSlopeBias,
		float& OutNormalBias)
	{
		const float Dot = FVector::DotProduct(CameraForwardWS.GetSafeNormal(), LightDirectionWS.GetSafeNormal());
		const float AlignT = FMath::Clamp((std::abs(Dot) - 0.15f) / 0.85f, 0.0f, 1.0f);
		const float SideToAlignedT = ComputePSMSideToAlignedT(Dot);
		const float FrontBiasT = SmoothStep01(-0.05f, 0.10f, Dot);
		const float FrontPeterT = FMath::Clamp((Dot - 0.20f) / 0.80f, 0.0f, 1.0f);

		const float UserCascadeBias = DirLight->GetCascadeBias(CascadeIndex);
		const float UserCascadeSlopeBias = DirLight->GetCascadeSlopeBias(CascadeIndex);

		float AutoConstantBias = LerpFloat(0.0000010f, 0.000010f, AlignT);
		float AutoSlopeBias = LerpFloat(0.00030f, 0.0012f, AlignT);

		if (CascadeIndex == 0)
		{
			AutoConstantBias = LerpFloat(0.00000025f, 0.0000020f, AlignT);
			AutoSlopeBias = LerpFloat(0.00028f, 0.00100f, AlignT);

			OutConstantBias = (std::min)(
				(std::max)(UserCascadeBias, AutoConstantBias),
				0.0000075f);

			OutSlopeBias = (std::min)(
				(std::max)(UserCascadeSlopeBias, AutoSlopeBias),
				0.0030f);
		}
		else
		{
			const float CascadeScale = 1.0f + 0.20f * static_cast<float>(CascadeIndex);

			OutConstantBias = (std::min)(
				(std::max)(UserCascadeBias, AutoConstantBias) * CascadeScale,
				0.000060f);

			OutSlopeBias = (std::min)(
				(std::max)(UserCascadeSlopeBias, AutoSlopeBias) * CascadeScale,
				0.0060f);
		}

		OutConstantBias *= LerpFloat(1.0f, 0.75f, FrontBiasT);
		OutSlopeBias *= LerpFloat(
			1.0f,
			(CascadeIndex == 0) ? 0.90f : 0.85f,
			FrontBiasT);

		(void)bInversePP;

		if (SideToAlignedT < 1.0f)
		{
			const float BiasReleaseT = SideToAlignedT * SideToAlignedT * SideToAlignedT;

			const float SideConstantCap = (CascadeIndex == 0)
				? 0.00000025f
				: 0.00000070f * (1.0f + 0.04f * static_cast<float>(CascadeIndex));

			const float AlignedConstantCap = (CascadeIndex == 0)
				? 0.0000075f
				: 0.000060f;

			const float ConstantCap = LerpFloat(SideConstantCap, AlignedConstantCap, BiasReleaseT);
			OutConstantBias = (std::min)(OutConstantBias, ConstantCap);

			const float SideSlopeCap = (CascadeIndex == 0)
				? 0.00035f
				: 0.00048f * (1.0f + 0.06f * static_cast<float>(CascadeIndex));

			const float AlignedSlopeCap = (CascadeIndex == 0)
				? 0.0030f
				: 0.0060f;

			const float SlopeCap = LerpFloat(SideSlopeCap, AlignedSlopeCap, BiasReleaseT);
			OutSlopeBias = (std::min)(OutSlopeBias, SlopeCap);
		}

		if (FrontPeterT > 0.0f)
		{
			const float FrontConstantCap = (CascadeIndex == 0)
				? 0.00000020f
				: 0.00000055f * (1.0f + 0.04f * static_cast<float>(CascadeIndex));

			const float FrontSlopeCap = (CascadeIndex == 0)
				? 0.00045f
				: 0.00058f * (1.0f + 0.05f * static_cast<float>(CascadeIndex));

			OutConstantBias = (std::min)(OutConstantBias, FrontConstantCap);
			OutSlopeBias = (std::min)(OutSlopeBias, FrontSlopeCap);
		}

		const float ContactConstantCap = (CascadeIndex == 0)
			? 0.00000001f
			: 0.00000006f * (1.0f + 0.03f * static_cast<float>(CascadeIndex));

		const float ContactSlopeCap = (CascadeIndex == 0)
			? 0.000012f
			: 0.000016f * (1.0f + 0.04f * static_cast<float>(CascadeIndex));

		OutConstantBias = (std::min)(OutConstantBias, ContactConstantCap);
		OutSlopeBias = (std::min)(OutSlopeBias, ContactSlopeCap);

		// Keep normal bias disabled for directional PSM until the shader applies it as a true world-position offset.
		(void)SplitNear;
		(void)SplitFar;
		OutNormalBias = 0.0f;
	}

	FCascadePSMResult BuildCascadePSMMatrix(
		const FViewContext& View,
		const FVector& LightDirectionWS,
		float SplitNear,
		float SplitFar,
		uint32 CascadeIndex,
		const UDirectionalLightComponent* DirLight)
	{
		FCascadePSMResult Result;
		Result.SplitNear = SplitNear;
		Result.SplitFar = SplitFar;

		const FPSMFrustumBasis Basis = ExtractCameraFrustumBasis(View.InverseProjection);
		const FVector CameraPositionWS = GetCameraPositionWS(View);
		const FVector CameraForwardWS = GetCameraForwardWS(View);
		const FVector CameraUpWS = GetCameraUpWS(View);

		Result.VirtualSlideBack = ComputeVirtualSlideBackForCascade(
			CameraForwardWS,
			LightDirectionWS,
			SplitNear,
			SplitFar,
			CascadeIndex);

		ComputeCascadeVirtualCameraRange(
			CameraForwardWS,
			LightDirectionWS,
			SplitNear,
			SplitFar,
			Result.VirtualSlideBack,
			CascadeIndex,
			Result.VCNear,
			Result.VCFar);

		const FVector VCPositionWS = CameraPositionWS - CameraForwardWS * Result.VirtualSlideBack;
		const FVector VCTargetWS = CameraPositionWS + CameraForwardWS;

		Result.VCView = FMatrix::MakeViewLookAtLH(VCPositionWS, VCTargetWS, CameraUpWS);
		Result.VCProjection = FMatrix::MakePerspectiveFovLH(Basis.FovYRad, Basis.Aspect, Result.VCNear, Result.VCFar);

		const FPostPerspectiveShadowCamera PPCamera = BuildPostPerspectiveShadowCamera(
			Result.VCView,
			Result.VCProjection,
			LightDirectionWS,
			CascadeIndex);

		Result.PPView = PPCamera.View;
		Result.PPProjection = PPCamera.Projection;
		Result.bPPOrthographic = PPCamera.bOrthographic;
		Result.bInversePP = PPCamera.bInversePerspective;

		Result.PSM = Result.VCView * Result.VCProjection * Result.PPView * Result.PPProjection;

		ComputeCascadePSMBias(
			CameraForwardWS,
			LightDirectionWS,
			CascadeIndex,
			SplitNear,
			SplitFar,
			Result.bInversePP,
			DirLight,
			Result.ConstantBias,
			Result.SlopeBias,
			Result.NormalBias);

		return Result;
	}


	void BuildDirectionalShadowViews(
		FSceneLightingInputs& Inputs,
		const UDirectionalLightComponent* DirLight,
		FDirectionalLightRenderItem& LightItem,
		uint32 ShadowLightIndex,
		const FViewContext& View)
	{
		FShadowLightRenderItem& ShadowLight = Inputs.DirShadowLights[ShadowLightIndex];
		ShadowLight.LightType = EShadowLightType::Directional;
		ShadowLight.PositionWS = FVector::ZeroVector;
		ShadowLight.DirectionWS = LightItem.DirectionWS;
		ShadowLight.Bias = 0.0f;
		ShadowLight.SlopeBias = 0.0f;
		ShadowLight.NormalBias = 0.0f;
		ShadowLight.Sharpen = 0.0f;
		ShadowLight.ESMExponent = DirLight->GetShadowESMExponent();

		uint32 CascadeCount = DirLight->GetCascadeCount();
		CascadeCount = (std::min)(CascadeCount, ShadowConfig::MaxDirCascade);

		TArray<float> FrustumSplits = FCasCade::CalculateCascadeSplits(
			CascadeCount,
			View.NearZ,
			DirLight->GetShadowFarZ(),
			DirLight->GetSplitLambda());

		if (FrustumSplits.size() < 2)
		{
			return;
		}

		TArray<float> SelectionSplits = FrustumSplits;
		if (CascadeCount > 1 && FrustumSplits.size() > 2)
		{
			const float Split0 = FrustumSplits[1];
			const float Split1 = FrustumSplits[2];
			const float PushRange = (std::max)(0.0f, Split1 - Split0);
			const float Cascade0HoldRatio = 0.18f;
			SelectionSplits[1] = (std::min)(
				Split1 - 0.25f,
				Split0 + PushRange * Cascade0HoldRatio);
		}

		LightItem.CascadeSplits = FVector4(
			SelectionSplits.size() > 1 ? SelectionSplits[1] : 0.0f,
			SelectionSplits.size() > 2 ? SelectionSplits[2] : 0.0f,
			SelectionSplits.size() > 3 ? SelectionSplits[3] : 0.0f,
			SelectionSplits.size() > 4 ? SelectionSplits[4] : 0.0f
		);

		const float ResolutionScale = DirLight->GetShadowResolutionScale();
		const uint32 RequestedResolution = QuantizeDiraShadowResolution(
			static_cast<uint32>(ShadowConfig::DefaultShadowMapResolution * ResolutionScale));

		for (uint32 CascadeIndex = 0; CascadeIndex < CascadeCount; ++CascadeIndex)
		{
			const float SplitNear = FrustumSplits[CascadeIndex];
			const float SplitFar = FrustumSplits[CascadeIndex + 1];

			float BuildSplitNear = SplitNear;
			float BuildSplitFar = SplitFar;
			if (CascadeIndex == 0 && SelectionSplits.size() > 1)
			{
				// Keep cascade 0 valid up to the delayed public boundary.
				BuildSplitFar = (std::max)(BuildSplitFar, SelectionSplits[1]);
			}
			
			const FCascadePSMResult PSM = BuildCascadePSMMatrix(
				View,
				LightItem.DirectionWS,
				BuildSplitNear,
				BuildSplitFar,
				CascadeIndex,
				DirLight);

			FShadowViewRenderItem ViewItem;
			ViewItem.LightType = EShadowLightType::Directional;
			ViewItem.ProjectionType = PSM.bPPOrthographic
				? EShadowProjectionType::Orthographic
				: EShadowProjectionType::Perspective;

			ViewItem.PositionWS = GetCameraPositionWS(View);
			ViewItem.NearZ = PSM.VCNear;
			ViewItem.FarZ = PSM.VCFar;

			ViewItem.View = PSM.VCView;
			ViewItem.Projection = PSM.VCProjection * PSM.PPView * PSM.PPProjection;
			ViewItem.ViewProjection = PSM.PSM;

			ViewItem.RequestedResolution = RequestedResolution;
			ViewItem.BiasParams = FVector4(
				PSM.ConstantBias,
				PSM.SlopeBias,
				PSM.NormalBias,
				PSM.bInversePP ? 1.0f : 0.0f);

			ViewItem.Viewport = {};

			AddDirShadowView(Inputs, ShadowLightIndex, ViewItem);
		}
	}

	void BuildPointShadowViews(FSceneLightingInputs& Inputs,const UPointLightComponent* Point, const FLocalLightRenderItem& LightItem,uint32 ShadowLightIndex, uint32 CubeArrayIndex)
	{
		static const FVector CubeFaceLook[6] = {{ 1, 0, 0 }, { -1, 0, 0 },	{ 0, 1, 0 }, { 0,-1, 0 },{0, 0, 1 },{ 0, 0, -1 },};
		static const FVector CubeFaceUp[6] = {{ 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 },{ 0, 1, 0 }, { 0, 1, 0 },	};
		FShadowLightRenderItem& ShadowLight = Inputs.ShadowLights[ShadowLightIndex];
		ShadowLight.PositionWS = LightItem.PositionWS;
		ShadowLight.Mobility = Point->GetMobility();
		ShadowLight.bCacheDirty = Point->IsCacheDirty();
		Point->ResetShadowCacheDirty();
		ShadowLight.Bias       = Point->GetShadowBias();
		ShadowLight.SlopeBias  = Point->GetShadowSlopeBias();
		ShadowLight.Sharpen    = Point->GetShadowSharpen();
		ShadowLight.CubeArrayIndex = CubeArrayIndex;
		ShadowLight.ESMExponent = Point->GetShadowESMExponent();

		const float NearZ = ShadowConfig::DefaultNearZ;
		const float FarZ = (std::max)(LightItem.Range, NearZ + 0.001f);

		const uint32 BaseSlice = ShadowConfig::PointShadowSliceOffset + CubeArrayIndex * 6;

		for (uint32 F = 0; F < 6; ++F)
		{
			FShadowViewRenderItem View;
			View.ProjectionType = EShadowProjectionType::Perspective;
			View.PositionWS = LightItem.PositionWS;
			View.NearZ = NearZ;
			View.FarZ = FarZ;
			View.RequestedResolution = ShadowConfig::DefaultShadowMapResolution;

			View.View = FMatrix::MakeViewLookAtLH(
				LightItem.PositionWS,
				LightItem.PositionWS + CubeFaceLook[F],
				CubeFaceUp[F]);	

			View.Projection = FMatrix::MakePerspectiveFovLH(
				FMath::DegreesToRadians(90.0f), 1.0f, NearZ, FarZ);

			View.ViewProjection = View.View * View.Projection;
			View.FilterMode = EShadowFilterMode::Raw; 
			View.LightType = EShadowLightType::Point;

			AddPointShadowView(Inputs, ShadowLightIndex, BaseSlice + F, View);
		}

	}
	float ComputeShadowPriority(const FVector& LightPos, const FVector& CameraPos)
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

	struct FPointShadowCandidate
	{
		const UPointLightComponent* PointLightl = nullptr;
		FLocalLightRenderItem LightItem;
		uint32 LocalLightIndex = 0;
		float SortKey = 0.0f;
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
						BuildSpotShadowViews(LightingInputs, Spot, LightItem, LocalLightIndex, ShadowLightIndex, View.ViewProjection);
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
					continue;

				FLocalLightRenderItem LightItem = BuildPointLight(Point);
				const uint32 LocalLightIndex = static_cast<uint32>(LightingInputs.LocalLights.size());

				if (Point->IsCastingShadows())
				{
					// 후보로만 수집 등록은 정렬 후 2차 패스에서
					FPointShadowCandidate Candidate;
					Candidate.PointLightl = Point;
					Candidate.LightItem = LightItem;
					Candidate.LocalLightIndex = LocalLightIndex;
					Candidate.SortKey = ComputeShadowPriority(LightItem.PositionWS, OutSceneViewData.View.CameraPosition);
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
	{;
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
