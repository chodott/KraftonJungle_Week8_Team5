#include "SceneCommandLightingBuilder.h"

#include "Renderer/Scene/Builders/SceneCommandBuilder.h"
#include "Renderer/Scene/SceneViewData.h"
#include "Level/SceneRenderPacket.h"

#include "Actor/Actor.h"
#include "Component/AmbientLightComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/PointLightComponent.h"
#include "Component/SpotLightComponent.h"
#include "Component/SkyComponent.h"
#include "Math/MathUtility.h"
#include "World/World.h"

#include "Renderer/Features/Shadow/ShadowAtlasAllocator.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

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

		const float InnerAngleRad = FMath::DegreesToRadians(
			FMath::Clamp(C->GetInnerConeAngle(), 0.0f, 89.0f));

		const float OuterAngleRad = FMath::DegreesToRadians(
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
		EShadowLightType LightType,
		uint32 SourceLightIndex)
	{
		if (Inputs.ShadowLights.size() >= ShadowConfig::MaxShadowLights)
		{
			return UINT32_MAX;
		}

		FShadowLightRenderItem Item = {};
		Item.LightType        = LightType;
		Item.SourceLightIndex = SourceLightIndex;
		Item.ShadowIndex      = static_cast<uint32>(Inputs.ShadowLights.size());

		Inputs.ShadowLights.push_back(Item);
		return Item.ShadowIndex;
	}

	uint32 AddShadowView(
		FSceneLightingInputs& Inputs,
		uint32 ShadowLightIndex,
		const FShadowViewRenderItem& InView)
	{
		if (Inputs.ShadowViews.size() >= ShadowConfig::MaxShadowViews)
		{
			return UINT32_MAX;
		}

		FShadowViewRenderItem View = InView;
		View.ShadowLightIndex = ShadowLightIndex;
		View.ArraySlice       = static_cast<uint32>(Inputs.ShadowViews.size());

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
			{
				return;
			}

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
		{
			return 0.0f;
		}

		MinX = FMath::Clamp(MinX, -1.0f, 1.0f);
		MinY = FMath::Clamp(MinY, -1.0f, 1.0f);
		MaxX = FMath::Clamp(MaxX, -1.0f, 1.0f);
		MaxY = FMath::Clamp(MaxY, -1.0f, 1.0f);

		const float Width  = std::max(0.0f, MaxX - MinX);
		const float Height = std::max(0.0f, MaxY - MinY);
		const float Coverage = (Width * Height) * 0.25f;

		return FMath::Clamp(Coverage, 0.0f, 1.0f);
	}

	float ComputePointScreenCoverage(
		const FVector& LightPosition,
		float Range,
		const FMatrix& ViewProj)
	{
		const float SafeRange = (std::max)(Range, 0.0f);
		if (SafeRange <= 0.0f)
		{
			return 0.0f;
		}

		float MinX = FLT_MAX;
		float MinY = FLT_MAX;
		float MaxX = -FLT_MAX;
		float MaxY = -FLT_MAX;

		int32 ProjectedCount = 0;

		auto AddProjectedPoint = [&](const FVector& WorldPos)
		{
			const FVector4 Clip = ViewProj.TransformVector4(FVector4(WorldPos, 1.0f));

			if (Clip.W <= 1.0e-4f)
			{
				return;
			}

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

		static const FVector SampleDirections[] =
		{
			FVector(1.0f, 0.0f, 0.0f),
			FVector(-1.0f, 0.0f, 0.0f),
			FVector(0.0f, 1.0f, 0.0f),
			FVector(0.0f, -1.0f, 0.0f),
			FVector(0.0f, 0.0f, 1.0f),
			FVector(0.0f, 0.0f, -1.0f),
			FVector(1.0f, 1.0f, 1.0f).GetSafeNormal(),
			FVector(1.0f, 1.0f, -1.0f).GetSafeNormal(),
			FVector(1.0f, -1.0f, 1.0f).GetSafeNormal(),
			FVector(1.0f, -1.0f, -1.0f).GetSafeNormal(),
			FVector(-1.0f, 1.0f, 1.0f).GetSafeNormal(),
			FVector(-1.0f, 1.0f, -1.0f).GetSafeNormal(),
			FVector(-1.0f, -1.0f, 1.0f).GetSafeNormal(),
			FVector(-1.0f, -1.0f, -1.0f).GetSafeNormal(),
		};

		for (const FVector& Direction : SampleDirections)
		{
			AddProjectedPoint(LightPosition + Direction * SafeRange);
		}

		if (ProjectedCount == 0)
		{
			return 0.0f;
		}

		MinX = FMath::Clamp(MinX, -1.0f, 1.0f);
		MinY = FMath::Clamp(MinY, -1.0f, 1.0f);
		MaxX = FMath::Clamp(MaxX, -1.0f, 1.0f);
		MaxY = FMath::Clamp(MaxY, -1.0f, 1.0f);

		const float Width  = std::max(0.0f, MaxX - MinX);
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

	uint32 AddPointShadowView(
		FSceneLightingInputs& Inputs,
		uint32 ShadowLightIndex,
		uint32 ExplicitArraySlice,
		const FShadowViewRenderItem& InView)
	{
		FShadowViewRenderItem View = InView;
		View.ShadowLightIndex = ShadowLightIndex;
		View.ArraySlice = ExplicitArraySlice;

		const uint32 ViewIndex = static_cast<uint32>(Inputs.ShadowViews.size());
		Inputs.ShadowViews.push_back(std::move(View));

		FShadowLightRenderItem& Light = Inputs.ShadowLights[ShadowLightIndex];

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
		EShadowLightType LightType,
		uint32 SourceLightIndex)
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

	uint32 AddDirShadowView(
		FSceneLightingInputs& Inputs,
		uint32 ShadowLightIndex,
		const FShadowViewRenderItem& InView)
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
		FSceneLightingInputs& Inputs,
		const USpotLightComponent* Spot,
		const FLocalLightRenderItem& LightItem,
		uint32 LocalLightIndex,
		uint32 ShadowLightIndex,
		const FMatrix& ViewProjMatrix)
	{
		(void)LocalLightIndex;

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
		const float FarZ = (std::max)(LightItem.Range, NearZ + 0.001f);

		const float OuterHalfAngleDeg = FMath::Clamp(Spot->GetOuterConeAngle(), 1.0f, 80.0f);
		const float FullFovRad = FMath::DegreesToRadians(OuterHalfAngleDeg * 2.0f);

		const float Coverage = ComputeSpotScreenCoverage(
			LightItem.PositionWS,
			LightItem.DirectionWS,
			LightItem.Range,
			Spot->GetOuterConeAngle(),
			ViewProjMatrix);

		const float SafeCoverage = FMath::Clamp(Coverage, 0.01f, 1.0f);
		const float ResolutionFactor = std::sqrt(SafeCoverage) * Spot->GetShadowResolutionScale();
		const uint32 RequestedResolution = QuantizeShadowResolution(
			static_cast<uint32>(ShadowConfig::DefaultShadowMapResolution * ResolutionFactor));

		FShadowViewRenderItem View;
		View.ProjectionType = EShadowProjectionType::Perspective;
		View.PositionWS = LightItem.PositionWS;
		View.NearZ = NearZ;
		View.FarZ = FarZ;
		View.SourceActor = Spot->GetOwner();
		View.SourceComponent = Spot;

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
		View.RequestedResolution = RequestedResolution;
		View.ESMExponent = ShadowLight.ESMExponent;

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
		FMatrix View = FMatrix::Identity;
		FMatrix Projection = FMatrix::Identity;
		bool bOrthographic = false;
		bool bInversePerspective = false;
	};

	struct FDirectionalShadowProjectionResult
	{
		float SplitNear = 0.0f;
		float SplitFar = 0.0f;

		FMatrix View = FMatrix::Identity;
		FMatrix Projection = FMatrix::Identity;
		FMatrix ViewProjection = FMatrix::Identity;

		FVector PositionWS = FVector::ZeroVector;
		float NearZ = ShadowConfig::DefaultNearZ;
		float FarZ = 1000.0f;
		EShadowProjectionType ProjectionType = EShadowProjectionType::Orthographic;
		FVector4 BiasParams = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	};

	struct FPSMSceneBounds
	{
		bool bValid = false;
		FVector MinWS = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
		FVector MaxWS = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);

		void MergePoint(const FVector& PointWS)
		{
			MinWS.X = (std::min)(MinWS.X, PointWS.X);
			MinWS.Y = (std::min)(MinWS.Y, PointWS.Y);
			MinWS.Z = (std::min)(MinWS.Z, PointWS.Z);

			MaxWS.X = (std::max)(MaxWS.X, PointWS.X);
			MaxWS.Y = (std::max)(MaxWS.Y, PointWS.Y);
			MaxWS.Z = (std::max)(MaxWS.Z, PointWS.Z);

			bValid = true;
		}

		void MergeBounds(const FBoxSphereBounds& Bounds)
		{
			const FVector Extent(
				(std::max)(0.0f, Bounds.BoxExtent.X),
				(std::max)(0.0f, Bounds.BoxExtent.Y),
				(std::max)(0.0f, Bounds.BoxExtent.Z));

			MergePoint(Bounds.Center - Extent);
			MergePoint(Bounds.Center + Extent);
		}
	};

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

	float QuantizePSMDepth(float Value)
	{
		const float Step = 1.0f / 16.0f;
		return std::floor(Value / Step + 0.5f) * Step;
	}

	float QuantizePSMDepthCeil(float Value)
	{
		const float Step = 1.0f / 16.0f;
		return std::ceil(Value / Step) * Step;
	}

	float QuantizePSMValue(float Value, float Step)
	{
		if (Step <= 0.0f)
		{
			return Value;
		}

		return std::floor(Value / Step + 0.5f) * Step;
	}

	float QuantizeToStep(float Value, float Step)
	{
		if (Step <= 0.0f)
		{
			return Value;
		}

		return std::floor(Value / Step + 0.5f) * Step;
	}

	void BuildAABBCorners(const FVector& MinWS, const FVector& MaxWS, FVector OutCorners[8])
	{
		OutCorners[0] = FVector(MinWS.X, MinWS.Y, MinWS.Z);
		OutCorners[1] = FVector(MaxWS.X, MinWS.Y, MinWS.Z);
		OutCorners[2] = FVector(MinWS.X, MaxWS.Y, MinWS.Z);
		OutCorners[3] = FVector(MaxWS.X, MaxWS.Y, MinWS.Z);

		OutCorners[4] = FVector(MinWS.X, MinWS.Y, MaxWS.Z);
		OutCorners[5] = FVector(MaxWS.X, MinWS.Y, MaxWS.Z);
		OutCorners[6] = FVector(MinWS.X, MaxWS.Y, MaxWS.Z);
		OutCorners[7] = FVector(MaxWS.X, MaxWS.Y, MaxWS.Z);
	}

	bool IsFiniteVector(const FVector& V)
	{
		return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
	}

	bool ShouldUsePrimitiveForPSMBounds(const UPrimitiveComponent* Primitive)
	{
		if (!Primitive || Primitive->IsPendingKill() || !Primitive->IsRegistered())
		{
			return false;
		}

		if (Primitive->IsHiddenInGame() || Primitive->IsEditorVisualization())
		{
			return false;
		}

		if (Primitive->IsA(USkyComponent::StaticClass()))
		{
			return false;
		}

		const AActor* Owner = Primitive->GetOwner();
		if (!Owner || Owner->IsPendingDestroy() || !Owner->IsVisible())
		{
			return false;
		}

		return Primitive->GetRenderMesh() != nullptr;
	}

	void MergePrimitiveIntoPSMBounds(FPSMSceneBounds& Bounds, const UPrimitiveComponent* Primitive)
	{
		if (!ShouldUsePrimitiveForPSMBounds(Primitive))
		{
			return;
		}

		FBoxSphereBounds WorldBounds = Primitive->GetWorldBounds();

		if (!IsFiniteVector(WorldBounds.Center)
			|| !IsFiniteVector(WorldBounds.BoxExtent)
			|| !std::isfinite(WorldBounds.Radius)
			|| WorldBounds.Radius <= 0.0f
			|| WorldBounds.Radius > 1.0e7f)
		{
			return;
		}

		const float Expand = FMath::Clamp(WorldBounds.Radius * 0.03f, 5.0f, 50.0f);
		WorldBounds.BoxExtent += FVector(Expand, Expand, Expand);

		Bounds.MergeBounds(WorldBounds);
	}

	FPSMSceneBounds BuildPSMSceneBoundsFromPacket(const FSceneRenderPacket& Packet)
	{
		FPSMSceneBounds Bounds;

		for (const FSceneMeshPrimitive& Primitive : Packet.ShadowCasterPrimitives)
		{
			MergePrimitiveIntoPSMBounds(Bounds, Primitive.Component);
		}

		return Bounds;
	}

	FPSMSceneBounds BuildPSMSceneBoundsFromWorld(const FSceneCommandBuildContext& BuildContext)
	{
		FPSMSceneBounds Bounds;

		if (!BuildContext.World)
		{
			return Bounds;
		}

		const TArray<AActor*> Actors = BuildContext.World->GetAllActors();

		for (AActor* Actor : Actors)
		{
			if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible())
			{
				continue;
			}

			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (!Component || !Component->IsA(UPrimitiveComponent::StaticClass()))
				{
					continue;
				}

				MergePrimitiveIntoPSMBounds(
					Bounds,
					static_cast<const UPrimitiveComponent*>(Component));
			}
		}

		return Bounds;
	}

	FPSMSceneBounds BuildPSMSceneBounds(
		const FSceneCommandBuildContext& BuildContext,
		const FSceneRenderPacket& Packet)
	{
		FPSMSceneBounds Bounds = BuildPSMSceneBoundsFromPacket(Packet);

		if (!Bounds.bValid)
		{
			Bounds = BuildPSMSceneBoundsFromWorld(BuildContext);
		}

		return Bounds;
	}

	bool ComputePSMFitRangeFromBounds(
		const FPSMSceneBounds& Bounds,
		const FVector& CameraPositionWS,
		const FVector& CameraForwardWS,
		float VirtualSlideBack,
		float BaseNear,
		float BaseFar,
		float& OutNear,
		float& OutFar)
	{
		if (!Bounds.bValid)
		{
			return false;
		}

		const FVector ForwardWS = CameraForwardWS.GetSafeNormal();
		if (ForwardWS.IsNearlyZero())
		{
			return false;
		}

		FVector Corners[8];
		BuildAABBCorners(Bounds.MinWS, Bounds.MaxWS, Corners);

		float MinPositiveDepth = FLT_MAX;
		float MaxPositiveDepth = -FLT_MAX;
		uint32 PositiveCount = 0;

		for (uint32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const float DepthFromCamera =
				FVector::DotProduct(Corners[CornerIndex] - CameraPositionWS, ForwardWS);

			if (DepthFromCamera <= 0.0f)
			{
				continue;
			}

			MinPositiveDepth = (std::min)(MinPositiveDepth, DepthFromCamera);
			MaxPositiveDepth = (std::max)(MaxPositiveDepth, DepthFromCamera);
			++PositiveCount;
		}

		if (PositiveCount == 0 || MaxPositiveDepth <= 0.0f)
		{
			return false;
		}

		const float BaseRange = (std::max)(1.0f, BaseFar - BaseNear);

		const float NearSafetyMargin = FMath::Clamp(
			BaseRange * 0.03f,
			0.50f,
			8.0f);

		const float FarSafetyMargin = FMath::Clamp(
			BaseRange * 0.10f,
			5.0f,
			80.0f);

		const float CandidateNear =
			MinPositiveDepth + VirtualSlideBack - NearSafetyMargin;

		const float CandidateFar =
			MaxPositiveDepth + VirtualSlideBack + FarSafetyMargin;

		constexpr float PSMMinNearZ = 0.50f;

		const float MinPSMRange = FMath::Clamp(
			BaseRange * 0.25f,
			3.0f,
			(std::max)(3.0f, BaseRange * 0.65f));

		float FitNear = (std::max)(BaseNear, (std::max)(PSMMinNearZ, CandidateNear));
		float FitFar = (std::max)(CandidateFar, FitNear + MinPSMRange);

		FitFar = (std::min)(FitFar, BaseFar);
		FitNear = (std::min)(FitNear, FitFar - MinPSMRange);
		FitNear = (std::max)(PSMMinNearZ, FitNear);
		FitFar = (std::max)(FitFar, FitNear + 1.0f);

		OutNear = (std::max)(PSMMinNearZ, QuantizePSMDepth(FitNear));
		OutFar = QuantizePSMDepthCeil(FitFar);

		if (OutFar > BaseFar)
		{
			OutFar = BaseFar;
		}

		return OutFar > OutNear + 1.0f;
	}

	FVector TransformDirectionRow(const FMatrix& Matrix, const FVector& Direction)
	{
		const FVector4 V = FVector4(Direction, 0.0f) * Matrix;
		return FVector(V.X, V.Y, V.Z);
	}

	float ComputePSMFrontFacingT(float Dot)
	{
		return SmoothStep01(0.05f, 0.45f, Dot);
	}

	float ComputePSMSideToAlignedT(float Dot)
	{
		return FMath::Clamp((std::abs(Dot) - 0.18f) / 0.37f, 0.0f, 1.0f);
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

	void BuildCameraFrustumSliceCornersWS(
		const FViewContext& View,
		float SplitNear,
		float SplitFar,
		FVector OutCornersWS[8])
	{
		const FPSMFrustumBasis Basis = ExtractCameraFrustumBasis(View.InverseProjection);

		for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
		{
			OutCornersWS[CornerIndex] = View.InverseView.TransformPosition(
				Basis.UnitDepthRays[CornerIndex] * SplitNear);

			OutCornersWS[CornerIndex + 4] = View.InverseView.TransformPosition(
				Basis.UnitDepthRays[CornerIndex] * SplitFar);
		}
	}

	FVector ComputeCornersCenter(const FVector CornersWS[8])
	{
		FVector Center = FVector::ZeroVector;

		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			Center += CornersWS[CornerIndex];
		}

		return Center / 8.0f;
	}

	float ComputeCornersRadius(const FVector CornersWS[8], const FVector& CenterWS)
	{
		float Radius = 0.0f;

		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			Radius = (std::max)(Radius, VectorLength(CornersWS[CornerIndex] - CenterWS));
		}

		return (std::max)(Radius, 1.0f);
	}

	void BuildLightBasis(
		const FVector& LightDirectionWS,
		FVector& OutForwardWS,
		FVector& OutRightWS,
		FVector& OutUpWS)
	{
		OutForwardWS = LightDirectionWS.GetSafeNormal();

		if (OutForwardWS.IsNearlyZero())
		{
			OutForwardWS = FVector::ForwardVector;
		}

		OutUpWS = FVector::ZAxisVector;

		if (std::abs(FVector::DotProduct(OutForwardWS, OutUpWS)) > 0.98f)
		{
			OutUpWS = FVector::YAxisVector;
		}

		OutRightWS = FVector::CrossProduct(OutUpWS, OutForwardWS).GetSafeNormal();
		OutUpWS = FVector::CrossProduct(OutForwardWS, OutRightWS).GetSafeNormal();
	}

	void ComputeViewSpaceBounds(
		const FVector CornersWS[8],
		const FMatrix& ViewMatrix,
		FVector& OutMin,
		FVector& OutMax)
	{
		OutMin = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
		OutMax = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);

		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const FVector CornerVS = ViewMatrix.TransformPosition(CornersWS[CornerIndex]);

			OutMin.X = (std::min)(OutMin.X, CornerVS.X);
			OutMin.Y = (std::min)(OutMin.Y, CornerVS.Y);
			OutMin.Z = (std::min)(OutMin.Z, CornerVS.Z);

			OutMax.X = (std::max)(OutMax.X, CornerVS.X);
			OutMax.Y = (std::max)(OutMax.Y, CornerVS.Y);
			OutMax.Z = (std::max)(OutMax.Z, CornerVS.Z);
		}
	}

	FDirectionalShadowProjectionResult BuildCSMShadowProjection(
		const FViewContext& View,
		const FVector& LightDirectionWS,
		float SplitNear,
		float SplitFar,
		uint32 CascadeIndex,
		uint32 RequestedResolution,
		const UDirectionalLightComponent* DirLight)
	{
		FDirectionalShadowProjectionResult Result;
		Result.SplitNear = SplitNear;
		Result.SplitFar = SplitFar;
		Result.ProjectionType = EShadowProjectionType::Orthographic;

		FVector CornersWS[8];
		BuildCameraFrustumSliceCornersWS(View, SplitNear, SplitFar, CornersWS);

		const FVector CenterWS = ComputeCornersCenter(CornersWS);
		const float Radius = ComputeCornersRadius(CornersWS, CenterWS);

		FVector LightForwardWS, LightRightWS, LightUpWS;
		BuildLightBasis(LightDirectionWS, LightForwardWS, LightRightWS, LightUpWS);

		const float Diameter = Radius * 2.0f;
		float ViewWidth = Diameter;
		float ViewHeight = Diameter;

		const float ShadowCasterPullback = 100.0f;
		FVector LightPositionWS = CenterWS - LightForwardWS * (Radius + ShadowCasterPullback);

		FMatrix LightView = FMatrix::MakeViewLookAtLH(LightPositionWS, CenterWS, LightUpWS);

		const float SafeResolution = static_cast<float>((std::max)(1u, RequestedResolution));
		const float TexelSize = Diameter / SafeResolution;

		FVector CenterVS = LightView.TransformPosition(CenterWS);
		CenterVS.Y = std::floor(CenterVS.Y / TexelSize) * TexelSize;
		CenterVS.Z = std::floor(CenterVS.Z / TexelSize) * TexelSize;

		LightPositionWS = CenterWS
			+ LightRightWS * (CenterVS.Y - LightView.TransformPosition(CenterWS).Y)
			+ LightUpWS * (CenterVS.Z - LightView.TransformPosition(CenterWS).Z)
			- LightForwardWS * (Radius + ShadowCasterPullback);

		LightView = FMatrix::MakeViewLookAtLH(LightPositionWS, LightPositionWS + LightForwardWS, LightUpWS);

		FVector MinVS, MaxVS;
		ComputeViewSpaceBounds(CornersWS, LightView, MinVS, MaxVS);

		const float NearZ = 0.01f;
		const float FarZ = MaxVS.X + 10.0f;

		Result.PositionWS = LightPositionWS;
		Result.NearZ = NearZ;
		Result.FarZ = FarZ;
		Result.View = LightView;
		Result.Projection = FMatrix::MakeOrthographicLH(ViewWidth, ViewHeight, NearZ, FarZ);
		Result.ViewProjection = Result.View * Result.Projection;

		Result.BiasParams = FVector4(
			DirLight->GetCascadeBias(CascadeIndex),
			DirLight->GetCascadeSlopeBias(CascadeIndex),
			0.0f,
			0.0f);

		return Result;
	}

	float ComputeVirtualSlideBackForPSM(
		const FVector& CameraForwardWS,
		const FVector& LightDirectionWS,
		float SplitNear,
		float SplitFar)
	{
		const float Dot = FVector::DotProduct(
			CameraForwardWS.GetSafeNormal(),
			LightDirectionWS.GetSafeNormal());

		const float SideToAlignedT = ComputePSMSideToAlignedT(Dot);
		const float FrontT = ComputePSMFrontFacingT(Dot);
		const float Range = (std::max)(1.0f, SplitFar - SplitNear);

		const float MaxAbsoluteSlide = LerpFloat(40.0f, 757.0f, FrontT);
		const float RangeScale = LerpFloat(0.075f, 0.18f, FrontT);

		float SlideBack =
			(std::min)(MaxAbsoluteSlide, Range * RangeScale)
			* FrontT
			* SideToAlignedT;

		const float TargetScale = LerpFloat(0.44f, 0.58f, SideToAlignedT);
		const float TargetMin = LerpFloat(8.0f, 16.0f, SideToAlignedT);
		const float TargetFarScale = LerpFloat(0.58f, 0.76f, SideToAlignedT);

		const float TargetDepthAtSplitNear = FMath::Clamp(
			Range * TargetScale,
			TargetMin,
			(std::max)(TargetMin, SplitFar * TargetFarScale));

		const float RequiredSlide = (std::max)(0.0f, TargetDepthAtSplitNear - SplitNear);
		const float MaxSlideAbs = LerpFloat(100.0f, 150.0f, SideToAlignedT);
		const float MaxSlideRange = LerpFloat(0.85f, 1.10f, SideToAlignedT);
		const float MaxSlide = (std::min)(MaxSlideAbs, Range * MaxSlideRange);

		SlideBack = FMath::Clamp(
			(std::max)(SlideBack, RequiredSlide),
			0.0f,
			MaxSlide);

		return QuantizePSMValue(SlideBack, 0.25f);
	}

	void ComputePSMVirtualCameraRange(
		const FVector& CameraForwardWS,
		const FVector& LightDirectionWS,
		float SplitNear,
		float SplitFar,
		float VirtualSlideBack,
		float& OutNear,
		float& OutFar)
	{
		const float Range = (std::max)(1.0f, SplitFar - SplitNear);

		const float Dot = FVector::DotProduct(
			CameraForwardWS.GetSafeNormal(),
			LightDirectionWS.GetSafeNormal());

		const float SideToAlignedT = ComputePSMSideToAlignedT(Dot);
		const float FrontT = ComputePSMFrontFacingT(Dot);

		const float SideNearMargin = FMath::Clamp(Range * 0.010f, 0.10f, 0.90f);
		const float AlignedNearMargin = FMath::Clamp(Range * 0.015f, 0.15f, 1.25f);
		const float SideFarMargin = FMath::Clamp(Range * 0.012f, 0.15f, 1.00f);
		const float AlignedFarMargin = FMath::Clamp(Range * 0.018f, 0.20f, 1.50f);

		float NearMargin = LerpFloat(SideNearMargin, AlignedNearMargin, SideToAlignedT);
		float FarMargin = LerpFloat(SideFarMargin, AlignedFarMargin, SideToAlignedT);

		const float FrontNearMargin = FMath::Clamp(
			Range * LerpFloat(0.015f, 0.045f, FrontT),
			0.15f,
			2.0f);

		NearMargin = (std::max)(NearMargin, FrontNearMargin * FrontT);

		constexpr float PSMMinNearZ = 0.50f;

		const float RawNear = SplitNear + VirtualSlideBack - NearMargin;
		const float RawFar = SplitFar + VirtualSlideBack + FarMargin;

		OutNear = (std::max)(PSMMinNearZ, RawNear);
		OutFar = (std::max)(OutNear + 1.0f, RawFar);

		OutNear = (std::max)(PSMMinNearZ, QuantizePSMDepth(OutNear));
		OutFar = (std::max)(OutNear + 1.0f, QuantizePSMDepthCeil(OutFar));
	}

	FPostPerspectiveShadowCamera BuildPostPerspectiveShadowCamera(
		const FMatrix& VCView,
		const FMatrix& VCProjection,
		const FVector& LightDirectionWS,
		const FPSMSceneBounds& PSMSceneBounds)
	{
		(void)PSMSceneBounds;

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

			Result.View = FMatrix::MakeViewLookAtLH(
				LightPositionPP,
				PPCenter,
				UpPP);

			Result.Projection = FMatrix::MakeOrthographicLH(
				PPRadius * 2.0f,
				PPRadius * 2.0f,
				NearPP,
				FarPP);

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

		FVector LightPositionPP(
			LightPPH.X * InvW,
			LightPPH.Y * InvW,
			LightPPH.Z * InvW);

		if (!IsFiniteVector(LightPositionPP))
		{
			LightPositionPP = PPCenter + FVector(1.0f, 0.0f, 0.0f) * (PPRadius * 2.0f);
		}

		FVector ToCenter = PPCenter - LightPositionPP;
		float DistToCenter = VectorLength(ToCenter);

		if (DistToCenter <= 1.0e-4f)
		{
			LightPositionPP = PPCenter + FVector(1.0f, 0.0f, 0.0f) * (PPRadius * 2.0f);
			ToCenter = PPCenter - LightPositionPP;
			DistToCenter = VectorLength(ToCenter);
		}

		const FVector ForwardPP = ToCenter * (1.0f / (std::max)(DistToCenter, 1.0e-4f));

		FVector UpPP = FVector::ZAxisVector;
		if (std::abs(FVector::DotProduct(ForwardPP, UpPP)) > 0.99f)
		{
			UpPP = FVector::YAxisVector;
		}

		const float FovPP = FMath::Clamp(
			2.0f * std::atan(PPRadius / (std::max)(DistToCenter, 1.0e-4f)),
			FMath::DegreesToRadians(2.0f),
			FMath::DegreesToRadians(155.0f));

		const float AspectPP = 1.0f;

		Result.View = FMatrix::MakeViewLookAtLH(
			LightPositionPP,
			PPCenter,
			UpPP);

		if (LightPPH.W < 0.0f)
		{
			const float Plane = (std::max)(0.24f, DistToCenter - PPRadius);

			Result.Projection = FMatrix::MakePerspectiveFovLH(
				FovPP,
				AspectPP,
				-Plane,
				Plane);

			Result.bInversePerspective = true;
		}
		else
		{
			const float NearPP = (std::max)(0.1f, DistToCenter - PPRadius);
			const float FarPP = (std::max)(NearPP + 0.001f, DistToCenter + PPRadius);

			Result.Projection = FMatrix::MakePerspectiveFovLH(
				FovPP,
				AspectPP,
				NearPP,
				FarPP);

			Result.bInversePerspective = false;
		}

		Result.bOrthographic = false;
		return Result;
	}

	void ComputePSMBias(
		const FVector& CameraForwardWS,
		const FVector& LightDirectionWS,
		const UDirectionalLightComponent* DirLight,
		float& OutConstantBias,
		float& OutSlopeBias,
		float& OutNormalBias)
	{
		const float Dot = FVector::DotProduct(
			CameraForwardWS.GetSafeNormal(),
			LightDirectionWS.GetSafeNormal());

		const float AlignT = FMath::Clamp((std::abs(Dot) - 0.15f) / 0.85f, 0.0f, 1.0f);
		const float SideToAlignedT = ComputePSMSideToAlignedT(Dot);
		const float FrontBiasT = SmoothStep01(-0.05f, 0.10f, Dot);
		const float FrontPeterT = FMath::Clamp((Dot - 0.20f) / 0.80f, 0.0f, 1.0f);

		const float UserCascadeBias = DirLight->GetCascadeBias(0);
		const float UserCascadeSlopeBias = DirLight->GetCascadeSlopeBias(0);

		const float AutoConstantBias = LerpFloat(0.00000025f, 0.0000020f, AlignT);
		const float AutoSlopeBias = LerpFloat(0.00028f, 0.00100f, AlignT);

		OutConstantBias = (std::min)(
			(std::max)(UserCascadeBias, AutoConstantBias),
			0.0000075f);

		OutSlopeBias = (std::min)(
			(std::max)(UserCascadeSlopeBias, AutoSlopeBias),
			0.0030f);

		OutConstantBias *= LerpFloat(1.0f, 0.75f, FrontBiasT);
		OutSlopeBias *= LerpFloat(1.0f, 0.90f, FrontBiasT);

		if (SideToAlignedT < 1.0f)
		{
			const float BiasReleaseT = SideToAlignedT * SideToAlignedT * SideToAlignedT;

			const float ConstantCap = LerpFloat(0.00000025f, 0.0000075f, BiasReleaseT);
			const float SlopeCap = LerpFloat(0.00035f, 0.0030f, BiasReleaseT);

			OutConstantBias = (std::min)(OutConstantBias, ConstantCap);
			OutSlopeBias = (std::min)(OutSlopeBias, SlopeCap);
		}

		if (FrontPeterT > 0.0f)
		{
			OutConstantBias = (std::min)(OutConstantBias, 0.00000020f);
			OutSlopeBias = (std::min)(OutSlopeBias, 0.00045f);
		}

		OutNormalBias = 0.0f;
	}

	FDirectionalShadowProjectionResult BuildPSMShadowProjection(
		const FViewContext& View,
		const FVector& LightDirectionWS,
		float SplitNear,
		float SplitFar,
		const UDirectionalLightComponent* DirLight,
		const FPSMSceneBounds& PSMSceneBounds)
	{
		FDirectionalShadowProjectionResult Result;
		Result.SplitNear = SplitNear;
		Result.SplitFar = SplitFar;

		const FPSMFrustumBasis Basis = ExtractCameraFrustumBasis(View.InverseProjection);
		const FVector CameraPositionWS = GetCameraPositionWS(View);
		const FVector CameraForwardWS = GetCameraForwardWS(View);
		const FVector CameraUpWS = GetCameraUpWS(View);

		const float VirtualSlideBack = ComputeVirtualSlideBackForPSM(
			CameraForwardWS,
			LightDirectionWS,
			SplitNear,
			SplitFar);

		float VCNear = 0.0f;
		float VCFar = 0.0f;

		ComputePSMVirtualCameraRange(
			CameraForwardWS,
			LightDirectionWS,
			SplitNear,
			SplitFar,
			VirtualSlideBack,
			VCNear,
			VCFar);

		float FitNear = 0.0f;
		float FitFar = 0.0f;

		if (ComputePSMFitRangeFromBounds(
			PSMSceneBounds,
			CameraPositionWS,
			CameraForwardWS,
			VirtualSlideBack,
			VCNear,
			VCFar,
			FitNear,
			FitFar))
		{
			VCNear = FitNear;
			VCFar = FitFar;
		}

		const FVector VCPositionWS = CameraPositionWS - CameraForwardWS * VirtualSlideBack;
		const FVector VCTargetWS = VCPositionWS + CameraForwardWS;

		const FMatrix VCView = FMatrix::MakeViewLookAtLH(
			VCPositionWS,
			VCTargetWS,
			CameraUpWS);

		const FMatrix VCProjection = FMatrix::MakePerspectiveFovLH(
			Basis.FovYRad,
			Basis.Aspect,
			VCNear,
			VCFar);

		const FPostPerspectiveShadowCamera PPCamera = BuildPostPerspectiveShadowCamera(
			VCView,
			VCProjection,
			LightDirectionWS,
			PSMSceneBounds);

		Result.PositionWS = VCPositionWS;
		Result.NearZ = VCNear;
		Result.FarZ = VCFar;
		Result.View = VCView;
		Result.Projection = VCProjection * PPCamera.View * PPCamera.Projection;
		Result.ViewProjection = Result.View * Result.Projection;
		Result.ProjectionType = PPCamera.bOrthographic
			? EShadowProjectionType::Orthographic
			: EShadowProjectionType::Perspective;

		float ConstantBias = 0.0f;
		float SlopeBias = 0.0f;
		float NormalBias = 0.0f;

		ComputePSMBias(
			CameraForwardWS,
			LightDirectionWS,
			DirLight,
			ConstantBias,
			SlopeBias,
			NormalBias);

		Result.BiasParams = FVector4(
			ConstantBias,
			SlopeBias,
			NormalBias,
			PPCamera.bInversePerspective ? 1.0f : 0.0f);

		return Result;
	}

	float ComputePSMAdaptiveShadowFarZ(
		const FPSMSceneBounds& Bounds,
		const FViewContext& View,
		float UserShadowFarZ)
	{
		(void)UserShadowFarZ;

		constexpr float PSMQualityFarCap = 120.0f;
		constexpr float PSMMinFarZ = 20.0f;

		const float MaxAllowedFar = (std::max)(View.NearZ + 1.0f, PSMQualityFarCap);

		auto ClampPSMFar = [&](float CandidateFar)
		{
			const float MinAllowedFar = (std::min)(
				MaxAllowedFar,
				(std::max)(View.NearZ + 10.0f, PSMMinFarZ));

			return FMath::Clamp(CandidateFar, MinAllowedFar, MaxAllowedFar);
		};

		if (!Bounds.bValid)
		{
			return ClampPSMFar(PSMQualityFarCap);
		}

		const FVector CameraPositionWS = GetCameraPositionWS(View);
		const FVector CameraForwardWS = GetCameraForwardWS(View);

		if (CameraForwardWS.IsNearlyZero())
		{
			return ClampPSMFar(PSMQualityFarCap);
		}

		FVector Corners[8];
		BuildAABBCorners(Bounds.MinWS, Bounds.MaxWS, Corners);

		float MaxPositiveDepth = -FLT_MAX;
		uint32 PositiveCount = 0;

		for (uint32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const float Depth =
				FVector::DotProduct(Corners[CornerIndex] - CameraPositionWS, CameraForwardWS);

			if (Depth <= 0.0f)
			{
				continue;
			}

			MaxPositiveDepth = (std::max)(MaxPositiveDepth, Depth);
			++PositiveCount;
		}

		if (PositiveCount == 0 || MaxPositiveDepth <= 0.0f)
		{
			return ClampPSMFar(PSMQualityFarCap);
		}

		const float FarMargin = FMath::Clamp(
			MaxPositiveDepth * 0.15f,
			10.0f,
			60.0f);

		return ClampPSMFar(MaxPositiveDepth + FarMargin);
	}

	void BuildDirectionalShadowViews(
		FSceneLightingInputs& Inputs,
		const UDirectionalLightComponent* DirLight,
		FDirectionalLightRenderItem& LightItem,
		uint32 ShadowLightIndex,
		const FViewContext& View,
		const FPSMSceneBounds& PSMSceneBounds)
	{
		FShadowLightRenderItem& ShadowLight = Inputs.DirShadowLights[ShadowLightIndex];

		ShadowLight.LightType = EShadowLightType::Directional;
		ShadowLight.PositionWS = FVector::ZeroVector;
		ShadowLight.DirectionWS = LightItem.DirectionWS;
		ShadowLight.Bias = 0.0f;
		ShadowLight.SlopeBias = 0.0f;
		ShadowLight.NormalBias = 0.0f;
		ShadowLight.Sharpen = DirLight->GetShadowSharpen();
		ShadowLight.ESMExponent = DirLight->GetShadowESMExponent();

		const bool bUsePSM =
			DirLight->GetShadowProjectionMode() == EDirectionalShadowProjectionMode::PSM;

		uint32 CascadeCount = bUsePSM
			? 1u
			: static_cast<uint32>(DirLight->GetCascadeCount());

		CascadeCount = (std::min)(CascadeCount, ShadowConfig::MaxDirCascade);

		const float ShadowFarZ = bUsePSM
			? ComputePSMAdaptiveShadowFarZ(PSMSceneBounds, View, 0.0f)
			: (std::max)(DirLight->GetShadowFarZ(), View.NearZ + 1.0f);

		TArray<float> FrustumSplits = FCasCade::CalculateCascadeSplits(
			CascadeCount,
			View.NearZ,
			ShadowFarZ,
			DirLight->GetSplitLambda());

		if (FrustumSplits.size() < 2)
		{
			return;
		}

		LightItem.CascadeSplits = FVector4(
			FrustumSplits.size() > 1 ? FrustumSplits[1] : 0.0f,
			FrustumSplits.size() > 2 ? FrustumSplits[2] : 0.0f,
			FrustumSplits.size() > 3 ? FrustumSplits[3] : 0.0f,
			FrustumSplits.size() > 4 ? FrustumSplits[4] : 0.0f);

		float ResolutionScale = DirLight->GetShadowResolutionScale();

		uint32 RequestedResolution = QuantizeDiraShadowResolution(
			static_cast<uint32>(ShadowConfig::DefaultShadowMapResolution * ResolutionScale));

		if (bUsePSM)
		{
			RequestedResolution = QuantizeDiraShadowResolution(
				(std::max)(RequestedResolution, 4096u));
		}

		constexpr float MaxDirectionalESMExponent = 512.0f;
		constexpr float MinESMDepthRange = 0.001f;
		float ReferenceESMDepthRange = 0.0f;

		for (uint32 CascadeIndex = 0; CascadeIndex < CascadeCount; ++CascadeIndex)
		{
			const float SplitNear = FrustumSplits[CascadeIndex];
			const float SplitFar = FrustumSplits[CascadeIndex + 1];

			const FDirectionalShadowProjectionResult ShadowProjection = bUsePSM
				? BuildPSMShadowProjection(
					View,
					LightItem.DirectionWS,
					SplitNear,
					SplitFar,
					DirLight,
					PSMSceneBounds)
				: BuildCSMShadowProjection(
					View,
					LightItem.DirectionWS,
					SplitNear,
					SplitFar,
					CascadeIndex,
					RequestedResolution,
					DirLight);

			const float ESMDepthRange = (std::max)(ShadowProjection.FarZ - ShadowProjection.NearZ, MinESMDepthRange);
			if (CascadeIndex == 0)
			{
				ReferenceESMDepthRange = ESMDepthRange;
			}

			FShadowViewRenderItem ViewItem;
			ViewItem.LightType = EShadowLightType::Directional;
			ViewItem.ProjectionType = ShadowProjection.ProjectionType;

			ViewItem.PositionWS = ShadowProjection.PositionWS;
			ViewItem.NearZ = ShadowProjection.NearZ;
			ViewItem.FarZ = ShadowProjection.FarZ;

			ViewItem.View = ShadowProjection.View;
			ViewItem.Projection = ShadowProjection.Projection;
			ViewItem.ViewProjection = ShadowProjection.ViewProjection;

			ViewItem.RequestedResolution = RequestedResolution;
			ViewItem.BiasParams = ShadowProjection.BiasParams;
			const float ESMRangeScale = bUsePSM
				? 1.0f
				: ESMDepthRange / (std::max)(ReferenceESMDepthRange, MinESMDepthRange);
			ViewItem.ESMExponent = FMath::Clamp(
				DirLight->GetShadowESMExponent() * ESMRangeScale,
				0.0f,
				MaxDirectionalESMExponent);
			ViewItem.Viewport = {};
			ViewItem.SourceActor = DirLight->GetOwner();
			ViewItem.SourceComponent = DirLight;

			AddDirShadowView(Inputs, ShadowLightIndex, ViewItem);
		}
	}

	void BuildPointShadowViews(
		FSceneLightingInputs& Inputs,
		const UPointLightComponent* Point,
		const FLocalLightRenderItem& LightItem,
		uint32 ShadowLightIndex,
		uint32 CubeArrayIndex,
		const FMatrix& ViewProjMatrix)
	{
		static const FVector CubeFaceLook[6] =
		{
			FVector( 1,  0,  0),
			FVector(-1,  0,  0),
			FVector( 0,  1,  0),
			FVector( 0, -1,  0),
			FVector( 0,  0,  1),
			FVector( 0,  0, -1),
		};

		static const FVector CubeFaceUp[6] =
		{
			FVector(0, 1,  0),
			FVector(0, 1,  0),
			FVector(0, 0, -1),
			FVector(0, 0,  1),
			FVector(0, 1,  0),
			FVector(0, 1,  0),
		};

		FShadowLightRenderItem& ShadowLight = Inputs.ShadowLights[ShadowLightIndex];

		ShadowLight.PositionWS = LightItem.PositionWS;
		ShadowLight.Mobility = Point->GetMobility();
		ShadowLight.bCacheDirty = Point->IsCacheDirty();

		Point->ResetShadowCacheDirty();

		ShadowLight.Bias = Point->GetShadowBias();
		ShadowLight.SlopeBias = Point->GetShadowSlopeBias();
		ShadowLight.Sharpen = Point->GetShadowSharpen();
		ShadowLight.CubeArrayIndex = CubeArrayIndex;
		ShadowLight.ESMExponent = Point->GetShadowESMExponent();

		const float NearZ = ShadowConfig::DefaultNearZ;
		const float FarZ = (std::max)(LightItem.Range, NearZ + 0.001f);

		const float Coverage = ComputePointScreenCoverage(
			LightItem.PositionWS,
			LightItem.Range,
			ViewProjMatrix);

		const float SafeCoverage = FMath::Clamp(Coverage, 0.01f, 1.0f);
		const float ResolutionFactor = std::sqrt(SafeCoverage) * Point->GetShadowResolutionScale();
		const uint32 RequestedResolution = QuantizeShadowResolution(
			static_cast<uint32>(ShadowConfig::DefaultShadowMapResolution * ResolutionFactor));

		const uint32 BaseSlice = ShadowConfig::PointShadowSliceOffset + CubeArrayIndex * 6;

		for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
		{
			FShadowViewRenderItem View;
			View.ProjectionType = EShadowProjectionType::Perspective;
			View.PositionWS = LightItem.PositionWS;
			View.NearZ = NearZ;
			View.FarZ = FarZ;
			View.SourceActor = Point->GetOwner();
			View.SourceComponent = Point;
			View.RequestedResolution = RequestedResolution;

			View.View = FMatrix::MakeViewLookAtLH(
				LightItem.PositionWS,
				LightItem.PositionWS + CubeFaceLook[FaceIndex],
				CubeFaceUp[FaceIndex]);

			View.Projection = FMatrix::MakePerspectiveFovLH(
				FMath::DegreesToRadians(90.0f),
				1.0f,
				NearZ,
				FarZ);

			View.ViewProjection = View.View * View.Projection;
			View.FilterMode = EShadowFilterMode::Raw;
			View.LightType = EShadowLightType::Point;
			View.ESMExponent = ShadowLight.ESMExponent;

			AddPointShadowView(
				Inputs,
				ShadowLightIndex,
				BaseSlice + FaceIndex,
				View);
		}
	}

	float ComputeShadowPriority(const FVector& LightPos, const FVector& CameraPos)
	{
		return (LightPos - CameraPos).SizeSquared();
	}
}

void FSceneCommandLightingBuilder::BuildLightingInputs(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket& Packet,
	const FViewContext& View,
	FSceneViewData& OutSceneViewData) const
{
	const FPSMSceneBounds PSMSceneBounds = BuildPSMSceneBounds(BuildContext, Packet);

	FSceneLightingInputs& LightingInputs = OutSceneViewData.LightingInputs;
	LightingInputs.Clear();

	LightingInputs.Ambient.Color = FVector::OneVector;
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

	FVector AmbientRadiance = FVector::ZeroVector;
	float AmbientIntensitySum = 0.0f;

	bool bHasAmbientLight = false;
	bool bHasDirectionalLight = false;

	float StrongestDirectional = -1.0f;
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
				const UAmbientLightComponent* Ambient =
					static_cast<UAmbientLightComponent*>(Component);

				if (!Ambient->GetVisible())
				{
					continue;
				}

				const float EffectiveIntensity = Ambient->GetEffectiveIntensity();

				if (EffectiveIntensity <= 0.0f)
				{
					continue;
				}

				AmbientRadiance += ToLightRGB(Ambient->GetColor()) * EffectiveIntensity;
				AmbientIntensitySum += EffectiveIntensity;
				bHasAmbientLight = true;

				continue;
			}

			if (Component->IsA(UDirectionalLightComponent::StaticClass()))
			{
				const UDirectionalLightComponent* Directional =
					static_cast<UDirectionalLightComponent*>(Component);

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
						const uint32 ShadowLightIndex = AllocateDirShadowLight(
							LightingInputs,
							EShadowLightType::Directional,
							0);

						if (ShadowLightIndex != UINT32_MAX)
						{
							BuildDirectionalShadowViews(
								LightingInputs,
								Directional,
								DirectionalLightItem,
								ShadowLightIndex,
								View,
								PSMSceneBounds);
						}
					}
				}

				continue;
			}

			if (Component->IsA(USpotLightComponent::StaticClass()))
			{
				const USpotLightComponent* Spot =
					static_cast<USpotLightComponent*>(Component);

				if (!Spot->GetVisible()
					|| Spot->GetEffectiveIntensity() <= 0.0f
					|| Spot->GetAttenuationRadius() <= 0.0f)
				{
					continue;
				}

				FLocalLightRenderItem LightItem = BuildSpotLight(Spot);
				const uint32 LocalLightIndex = static_cast<uint32>(LightingInputs.LocalLights.size());

				if (Spot->IsCastingShadows())
				{
					const uint32 ShadowLightIndex = AllocalteShadowLight(
						LightingInputs,
						EShadowLightType::Spot,
						LocalLightIndex);

					if (ShadowLightIndex != UINT32_MAX)
					{
						BuildSpotShadowViews(
							LightingInputs,
							Spot,
							LightItem,
							LocalLightIndex,
							ShadowLightIndex,
							View.ViewProjection);

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
				const UPointLightComponent* Point =
					static_cast<UPointLightComponent*>(Component);

				if (!Point->GetVisible()
					|| Point->GetEffectiveIntensity() <= 0.0f
					|| Point->GetAttenuationRadius() <= 0.0f)
				{
					continue;
				}

				FLocalLightRenderItem LightItem = BuildPointLight(Point);
				const uint32 LocalLightIndex = static_cast<uint32>(LightingInputs.LocalLights.size());

				if (Point->IsCastingShadows())
				{
					FPointShadowCandidate Candidate;
					Candidate.PointLightl = Point;
					Candidate.LightItem = LightItem;
					Candidate.LocalLightIndex = LocalLightIndex;
					Candidate.SortKey = ComputeShadowPriority(
						LightItem.PositionWS,
						OutSceneViewData.View.CameraPosition);

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

	std::sort(
		ShadowCandidates.begin(),
		ShadowCandidates.end(),
		[](const FPointShadowCandidate& A, const FPointShadowCandidate& B)
		{
			return A.SortKey < B.SortKey;
		});

	uint32 PointCubeCounter = 0;

	for (FPointShadowCandidate& Candidate : ShadowCandidates)
	{
		FLocalLightRenderItem LightItem = Candidate.LightItem;

		if (PointCubeCounter < ShadowConfig::MaxPointShadowCubes)
		{
			const uint32 ShadowLightIndex = AllocalteShadowLight(
				LightingInputs,
				EShadowLightType::Point,
				Candidate.LocalLightIndex);

			if (ShadowLightIndex != UINT32_MAX)
			{
				BuildPointShadowViews(
					LightingInputs,
					Candidate.PointLightl,
					LightItem,
					ShadowLightIndex,
					PointCubeCounter,
					View.ViewProjection);

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
		LightingInputs.Ambient.Color = AmbientRadiance / AmbientIntensitySum;
		LightingInputs.Ambient.Intensity = AmbientIntensitySum;
	}

	if (bHasDirectionalLight)
	{
		LightingInputs.DirectionalLights.push_back(DirectionalLightItem);
	}
}
