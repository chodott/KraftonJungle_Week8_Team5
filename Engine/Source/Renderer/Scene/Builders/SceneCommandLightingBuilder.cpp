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

#include <algorithm>
#include <cmath>

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

		L.CullCenterWS = L.PositionWS + L.DirectionWS * (L.Range * 0.5f);
		L.CullRadius   = L.Range;

		FVector UpVector = FVector(0.0f, 1.0f, 0.0f);
		if (std::abs(L.DirectionWS.Y) > 0.999f)
		{
			UpVector = FVector(0.0f, 0.0f, 1.0f);
		}

		L.ShadowView = FMatrix::MakeViewLookAtLH(L.PositionWS, L.PositionWS + L.DirectionWS, UpVector);

		const float FovAngle = OuterAngleRad * 2.0f;
		const float AspectRatio = 1.0f;

		float SafeRange = std::max(L.Range, 1.0f);
		const float NearZ = std::max(0.1f, SafeRange * 0.02f);

		const float FarZ = std::max(SafeRange, NearZ + 0.1f);

		L.ShadowProj = FMatrix::MakePerspectiveFovLH(FovAngle, AspectRatio, NearZ, FarZ);
		L.ShadowViewProj = L.ShadowView * L.ShadowProj;


		return L;
	}

	FDirectionalLightRenderItem BuildDirectionalLight(const UDirectionalLightComponent* C, FViewContext View)
	{
		FDirectionalLightRenderItem L;
		L.DirectionWS = C->GetEmissionDirectionWS().GetSafeNormal();
		L.Color       = ToLightRGB(C->GetColor());
		L.Intensity   = C->GetEffectiveIntensity();
		L.Flags       = 0;
		L.CasCadeCount = C->GetCascadeCount();

		TArray<float> FrustumSplits = FCasCade::CalculateCascadeSplits(C->GetCascadeCount(), View.NearZ, View.FarZ);
		FVector UpVector = (std::abs(L.DirectionWS.Z) > 0.999f) ? FVector::YAxisVector : FVector::ZAxisVector;

		FMatrix InvView = View.InverseView;
		FMatrix InvProj = View.InverseProjection;

		// NDC좌표상 각 꼭짓점
		FVector4 NDC_Corners[4] = {
			FVector4(-1.0f, 1.0f, 1.0f, 1.0f), FVector4(1.0f, 1.0f, 1.0f, 1.0f),
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
		for (uint32 i = 0; i < L.CasCadeCount; i++)
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
			FVector LightPosition = FrustumCenter - (L.DirectionWS * 1000.0f);
			FMatrix TempShadowView = FMatrix::MakeViewLookAtLH(LightPosition, FrustumCenter, UpVector);

			// 여기서부터 각 절두체의 Light위치 기준 AABB 계산
			float MinX = FLT_MAX; float MaxX = -FLT_MAX;
			float MinY = FLT_MAX; float MaxY = -FLT_MAX;
			float MinZ = FLT_MAX; float MaxZ = -FLT_MAX;

			// Light위치 기준 Min, Max구해주는 중
			for (int j = 0; j < 8; j++)
			{
				FVector CornerLS = TempShadowView.TransformPosition(FrustumCornersWS[j]);

				MinX = std::min(MinX, CornerLS.X); MaxX = std::max(MaxX, CornerLS.X);
				MinY = std::min(MinY, CornerLS.Y); MaxY = std::max(MaxY, CornerLS.Y);
				MinZ = std::min(MinZ, CornerLS.Z); MaxZ = std::max(MaxZ, CornerLS.Z);
			}

			float MaxRadiusY = std::max(std::abs(MinY), std::abs(MaxY));
			float MaxRadiusZ = std::max(std::abs(MinZ), std::abs(MaxZ));

			float BoxWidth = MaxRadiusY * 2.0f;
			float BoxHeight = MaxRadiusZ * 2.0f;

			float BoxNear = MinX - 2000.0f;
			float BoxFar = MaxX + 500.0f;

			L.CascadeMatrices[i].ViewMatrix = TempShadowView;
			L.CascadeMatrices[i].ProjectionMatrix = FMatrix::MakeOrthographicLH(BoxWidth, BoxHeight, BoxNear, BoxFar);
			L.CascadeMatrices[i].ViewProjMatrix = L.CascadeMatrices[i].ViewMatrix * L.CascadeMatrices[i].ProjectionMatrix;

			L.CascadeSplits[i] = FarSplit;
		}

		return L;
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

	FVector AmbientRadiance        = FVector::ZeroVector;
	float   AmbientIntensitySum    = 0.0f;
	bool    bHasAmbientLight       = false;
	bool    bHasDirectionalLight   = false;
	float   StrongestDirectional   = -1.0f;
	FDirectionalLightRenderItem DirectionalLightItem;

	const TArray<AActor*> Actors = BuildContext.World->GetAllActors();
	LightingInputs.LocalLights.reserve(Actors.size());

	uint32 AllocatedShadowCount = 0;

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

				AmbientRadiance += ToLightRGB(Ambient->GetColor()) * EffectiveIntensity;
				AmbientIntensitySum += EffectiveIntensity;
				bHasAmbientLight = true;
				continue;
			}

			if (Component->IsA(UDirectionalLightComponent::StaticClass()))
			{
				const UDirectionalLightComponent* Directional = static_cast<UDirectionalLightComponent*>(Component);
				if (!Directional->GetVisible() || Directional->GetEffectiveIntensity() <= 0.0f)
				{
					continue;
				}

				const FDirectionalLightRenderItem Candidate = BuildDirectionalLight(Directional, View);
				if (!bHasDirectionalLight || Candidate.Intensity > StrongestDirectional)
				{
					DirectionalLightItem = Candidate;
					StrongestDirectional = Candidate.Intensity;
					bHasDirectionalLight = true;
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

				FLocalLightRenderItem SpotLightItem = BuildSpotLight(Spot);
				if (AllocatedShadowCount < LightListConfig::MaxShadowCastingLights)
				{
					SpotLightItem.ShadowIndex = AllocatedShadowCount;
					AllocatedShadowCount++;
				}

				LightingInputs.LocalLights.push_back(SpotLightItem);
				continue;
			}

			if (Component->IsA(UPointLightComponent::StaticClass()))
			{
				const UPointLightComponent* Point = static_cast<UPointLightComponent*>(Component);
				if (!Point->GetVisible() || Point->GetEffectiveIntensity() <= 0.0f || Point->GetAttenuationRadius() <= 0.0f)
				{
					continue;
				}

				LightingInputs.LocalLights.push_back(BuildPointLight(Point));
			}
		}
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
