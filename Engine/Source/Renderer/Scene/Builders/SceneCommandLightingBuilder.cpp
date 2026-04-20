#include "SceneCommandLightingBuilder.h"

#include "Renderer/Scene/SceneViewData.h"
#include "Renderer/Features/Lighting/LightTypes.h"

#include "Component/PointLightComponent.h"
#include "Component/SpotLightComponent.h"
#include "Component/DirectionalLightComponent.h"

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

		L.PositionWS = C->GetWorldLocation();
		L.Range      = C->GetAttenuationRadius();
		L.Color      = ToLightRGB(C->GetLightColor());
		L.Intensity  = C->GetIntensity();
		L.Flags      = C->BuildLightFlags();

		L.CullCenterWS = L.PositionWS;
		L.CullRadius   = L.Range;

		return L;
	}

	FLocalLightRenderItem BuildSpotLight(const USpotLightComponent* C)
	{
		FLocalLightRenderItem L;
		L.LightClass = ELightClass::Spot;
		L.CullShape  = ECullShapeType::Cone;

		L.PositionWS    = C->GetWorldLocation();
		L.DirectionWS   = C->GetWorldDirection();
		L.Range         = C->GetAttenuationRadius();
		L.InnerAngleCos = C->GetInnerConeCos();
		L.OuterAngleCos = C->GetOuterConeCos();
		L.Color         = ToLightRGB(C->GetLightColor());
		L.Intensity     = C->GetIntensity();
		L.Flags         = C->BuildLightFlags();

		// conservative sphere
		L.CullCenterWS = L.PositionWS + L.DirectionWS * (L.Range * 0.5f);
		L.CullRadius   = L.Range;

		return L;
	}

	FDirectionalLightRenderItem BuildDirectionalLight(const UDirectionalLightComponent* C)
	{
		FDirectionalLightRenderItem L;
		L.DirectionWS = C->GetWorldDirection();
		L.Color       = ToLightRGB(C->GetLightColor());
		L.Intensity   = C->GetIntensity();
		L.Flags       = C->BuildLightFlags();
		return L;
	}
}

void FSceneCommandLightingBuilder::BuildLightingInputs(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket&        Packet,
	const FViewContext&              View,
	FSceneViewData&                  OutSceneViewData) const
{
	FSceneLightingInputs& LightingInputs = OutSceneViewData.LightingInputs;
	LightingInputs.Clear();

	LightingInputs.Ambient.Color     = FVector::OneVector;
	LightingInputs.Ambient.Intensity = 0.08f;

	LightingInputs.LocalLights.reserve(
		Packet.PointLightPrimitives.size() + Packet.SpotLightPrimitives.size());
	LightingInputs.DirectionalLights.reserve(Packet.DirectionalLightPrimitives.size());

	for (const FScenePointLightPrimitive& Primitive : Packet.PointLightPrimitives)
	{
		if (Primitive.Component && Primitive.Component->IsEnabled())
		{
			LightingInputs.LocalLights.push_back(BuildPointLight(Primitive.Component));
		}
	}

	for (const FSceneSpotLightPrimitive& Primitive : Packet.SpotLightPrimitives)
	{
		if (Primitive.Component && Primitive.Component->IsEnabled())
		{
			LightingInputs.LocalLights.push_back(BuildSpotLight(Primitive.Component));
		}
	}

	for (const FSceneDirectionalLightPrimitive& Primitive : Packet.DirectionalLightPrimitives)
	{
		if (Primitive.Component && Primitive.Component->IsEnabled())
		{
			LightingInputs.DirectionalLights.push_back(BuildDirectionalLight(Primitive.Component));
		}
	}
}
