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
		const FVector& CameraPosition,
		const FVector& LightPosition,
		const FVector& LightDirection,
		float Range,
		float OuterConeAngleDeg)
	{
		const float SafeRange = (std::max)(Range, 0.0f);
		const float HalfRange = SafeRange * 0.5f;

		const float OuterAngleRad = FMath::DegreesToRadians(
			FMath::Clamp(OuterConeAngleDeg, 1.0f, 80.0f));

		const float ConeEndRadius = std::tan(OuterAngleRad) * SafeRange;

		const FVector BoundsCenter = LightPosition + LightDirection.GetSafeNormal() * HalfRange;
		const float BoundsRadius = std::sqrt(HalfRange * HalfRange + ConeEndRadius * ConeEndRadius);

		const float Distance = (CameraPosition - BoundsCenter).Size();
		const float SafeDistance = (std::max)(Distance - BoundsRadius, 1.0f);

		const float ProjectedRadius = BoundsRadius / SafeDistance;

		return FMath::Clamp(ProjectedRadius * ProjectedRadius, 0.0f, 1.0f);
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
		const FVector&				 CameraPosition)
	{
		FShadowLightRenderItem& ShadowLight = Inputs.ShadowLights[ShadowLightIndex];

		ShadowLight.LightType   = EShadowLightType::Spot;
		ShadowLight.PositionWS  = LightItem.PositionWS;
		ShadowLight.DirectionWS = LightItem.DirectionWS;
		ShadowLight.Bias        = Spot->GetShadowBias();
		ShadowLight.SlopeBias   = Spot->GetShadowSlopeBias();
		ShadowLight.NormalBias  = 0.0f;
		ShadowLight.Sharpen     = Spot->GetShadowSharpen();

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

		const float Coverage = ComputeSpotScreenCoverage(CameraPosition, LightItem.PositionWS, LightItem.DirectionWS,
			LightItem.Range, Spot->GetOuterConeAngle());
		float ResolutionScale = Spot->GetShadowResolutionScale();

		float ResolutionFactor = std::sqrt(Coverage) * ResolutionScale;
		uint32 RequestedResolution = QuantizeShadowResolution(static_cast<uint32>(ShadowConfig::DefaultShadowMapResolution * ResolutionFactor));

		FShadowViewRenderItem View;
		View.ProjectionType      = EShadowProjectionType::Perspective;
		View.PositionWS          = LightItem.PositionWS;
		View.NearZ               = NearZ;
		View.FarZ                = FarZ;

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
		ShadowLight.Bias = 0.000001f;
		ShadowLight.SlopeBias = 0.001f;
		ShadowLight.NormalBias = 0.0f;
		ShadowLight.Sharpen = 0.0f;

		uint32 CascadeCount = DirLight->GetCascadeCount();
		CascadeCount = (std::min)(CascadeCount, ShadowConfig::MaxDirCascade);

		TArray<float> FrustumSplits = FCasCade::CalculateCascadeSplits(CascadeCount, View.NearZ, View.FarZ, 0.9f);
		
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

		FVector UpVector = (std::abs(LightItem.DirectionWS.Z) > 0.999f) ? FVector::YAxisVector : FVector::ZAxisVector;

		FMatrix InvView = View.InverseView;
		FMatrix InvProj = View.InverseProjection;

		// NDC좌표상 각 꼭짓점
		FVector4 NDC_Corners[4] = {
			FVector4(-1.0f,  1.0f, 1.0f, 1.0f), FVector4(1.0f,  1.0f, 1.0f, 1.0f),
			FVector4(1.0f, -1.0f, 1.0f, 1.0f), FVector4(-1.0f, -1.0f, 1.0f, 1.0f)
		};

		// 절두체 4개의 변
		FVector ViewRays[4];
		for (int j = 0; j < 4; j++)
		{
			FVector4 ViewCorner = NDC_Corners[j] * InvProj;
			float InvW = 1.0f / ViewCorner.W;
			FVector Ray = FVector(ViewCorner.X * InvW, ViewCorner.Y * InvW, ViewCorner.Z * InvW);

			ViewRays[j] = Ray * (1.0f / Ray.X);
		}

		// Cascade개수만큼
		for (uint32 i = 0; i < CascadeCount; i++)
		{
			float NearSplit = FrustumSplits[i];
			float FarSplit = FrustumSplits[i + 1];

			// 절두체의 각 꼭짓점
			FVector FrustumCornersWS[8];
			for (int j = 0; j < 4; j++)
			{
				FVector NearVS = ViewRays[j] * NearSplit;
				FVector FarVS = ViewRays[j] * FarSplit;

				FrustumCornersWS[j] = InvView.TransformPosition(NearVS);
				FrustumCornersWS[j + 4] = InvView.TransformPosition(FarVS);
			}

			// 절두체 8개의 꼭짓점 기반 중심점 계산
			FVector FrustumCenter = FVector::ZeroVector;
			for (int j = 0; j < 8; j++) { FrustumCenter += FrustumCornersWS[j]; }
			FrustumCenter /= 8.0f;

			// 자른 절두체 기준 view 행렬 생성
			FVector LightPosition = FrustumCenter - (LightItem.DirectionWS * 1000.0f);
			FMatrix TempShadowView = FMatrix::MakeViewLookAtLH(LightPosition, FrustumCenter, UpVector);

			// 여기서부터 각 절두체의 Light위치 기준 AABB 계산
			float MinX = FLT_MAX; float MaxX = -FLT_MAX;
			float MinY = FLT_MAX; float MaxY = -FLT_MAX;
			float MinZ = FLT_MAX; float MaxZ = -FLT_MAX;

			// Light위치 기준 Min, Max구해주는 중
			for (int j = 0; j < 8; j++)
			{
				FVector CornerLS = TempShadowView.TransformPosition(FrustumCornersWS[j]);
				MinX = (std::min)(MinX, CornerLS.X); MaxX = (std::max)(MaxX, CornerLS.X);
				MinY = (std::min)(MinY, CornerLS.Y); MaxY = (std::max)(MaxY, CornerLS.Y);
				MinZ = (std::min)(MinZ, CornerLS.Z); MaxZ = (std::max)(MaxZ, CornerLS.Z);
			}

			float MaxRadiusY = (std::max)(std::abs(MinY), std::abs(MaxY));
			float MaxRadiusZ = (std::max)(std::abs(MinZ), std::abs(MaxZ));

			float BoxWidth = MaxRadiusY * 2.0f;
			float BoxHeight = MaxRadiusZ * 2.0f;

			float BoxNear = MinX - 2000.0f;
			float BoxFar = MaxX + 5.0f;

			FShadowViewRenderItem ViewItem;
			ViewItem.ProjectionType = EShadowProjectionType::Orthographic;
			ViewItem.PositionWS = FrustumCenter;
			ViewItem.NearZ = BoxNear;
			ViewItem.FarZ = BoxFar;
			ViewItem.RequestedResolution = ShadowConfig::DirShadowDepthArrayResolution;

			ViewItem.BiasParams = { DirLight->GetShadowBias(), DirLight->GetShadowSlopeBias(), 0.0f, 0.0f };
			ViewItem.View = TempShadowView;
			ViewItem.Projection = FMatrix::MakeOrthographicLH(BoxWidth, BoxHeight, BoxNear, BoxFar);
			ViewItem.ViewProjection = ViewItem.View * ViewItem.Projection;
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
		ShadowLight.Bias       = Point->GetShadowBias();
		ShadowLight.SlopeBias  = Point->GetShadowSlopeBias();
		ShadowLight.Sharpen    = Point->GetShadowSharpen();
		ShadowLight.CubeArrayIndex = CubeArrayIndex;

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
			View.FilterMode = EShadowFilterMode::VSM;
			View.LightType = EShadowLightType::Point;

			AddPointShadowView(Inputs, ShadowLightIndex, BaseSlice + F, View);
		}

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
	uint32 PointCubeCounter = 0;

	if (!BuildContext.World)
	{
		return;
	}

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
					const uint32 ShadowLightIndex = AllocateDirShadowLight(LightingInputs, EShadowLightType::Directional, 0);
					if (ShadowLightIndex != UINT32_MAX)
					{
						BuildDirectionalShadowViews(LightingInputs, Directional, DirectionalLightItem, ShadowLightIndex, View);
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
					continue;

				FLocalLightRenderItem LightItem = BuildPointLight(Point);
				const uint32 LocalLightIndex = static_cast<uint32>(LightingInputs.LocalLights.size());

				if (Point->IsCastingShadows() && PointCubeCounter < ShadowConfig::MaxPointShadowCubes)
				{
					const uint32 ShadowLightIndex = AllocalteShadowLight(
						LightingInputs, EShadowLightType::Point, LocalLightIndex);

					if (ShadowLightIndex != UINT32_MAX)
					{
						BuildPointShadowViews(LightingInputs, Point, LightItem,
							ShadowLightIndex, PointCubeCounter);

						if (LightingInputs.ShadowLights[ShadowLightIndex].ViewCount == 6)
						{
							LightItem.ShadowIndex = ShadowLightIndex;
							++PointCubeCounter;
						}
					}
				}

				LightingInputs.LocalLights.push_back(LightItem);
				continue;
			}
		}
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
