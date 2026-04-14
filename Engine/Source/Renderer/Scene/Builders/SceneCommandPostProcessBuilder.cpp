#include "Renderer/Scene/Builders/SceneCommandPostProcessBuilder.h"

#include "Renderer/Scene/Builders/SceneCommandBuilder.h"
#include "Renderer/Scene/Builders/SceneCommandBuilderUtils.h"

#include <cmath>

#include "Component/DecalComponent.h"
#include "Component/FireBallComponent.h"
#include "Component/HeightFogComponent.h"

void FSceneCommandPostProcessBuilder::BuildFogInputs(
	const FSceneRenderPacket& Packet,
	FSceneViewData& OutSceneViewData) const
{
	OutSceneViewData.PostProcessInputs.FogItems.reserve(Packet.FogPrimitives.size());
	for (const FSceneFogPrimitive& Primitive : Packet.FogPrimitives)
	{
		const UHeightFogComponent* FogComponent = Primitive.Component;
		if (!FogComponent)
		{
			continue;
		}

		FFogRenderItem& Item = OutSceneViewData.PostProcessInputs.FogItems.emplace_back();
		Item.FogOrigin = FogComponent->GetWorldLocation();
		Item.FogDensity = FogComponent->FogDensity;
		Item.FogHeightFalloff = FogComponent->FogHeightFalloff;
		Item.StartDistance = FogComponent->StartDistance;
		Item.FogCutoffDistance = FogComponent->FogCutoffDistance;
		Item.FogMaxOpacity = FogComponent->FogMaxOpacity;
		Item.FogInscatteringColor = FogComponent->FogInscatteringColor;
		Item.AllowBackground = FogComponent->AllowBackground;
		Item.FogExtents = FogComponent->FogExtents;
		Item.FogVolumeWorld = FTransform(FogComponent->GetWorldTransform()).ToMatrixWithScale();
		Item.WorldToFogVolume = Item.FogVolumeWorld.GetInverse();
	}
}

void FSceneCommandPostProcessBuilder::BuildFireBallInputs(
	const FSceneRenderPacket& Packet,
	FSceneViewData& OutSceneViewData) const
{
	OutSceneViewData.PostProcessInputs.FireBallItems.reserve(Packet.FireBallPrimitives.size());
	for (const FSceneFireBallPrimitive& Primitive : Packet.FireBallPrimitives)
	{
		const UFireBallComponent* FireballComponent = Primitive.Component;
		if (!FireballComponent)
		{
			continue;
		}

		FFireBallRenderItem& Item = OutSceneViewData.PostProcessInputs.FireBallItems.emplace_back();
		Item.Color = FireballComponent->GetColor();
		Item.FireballOrigin = FireballComponent->GetWorldLocation();
		Item.Intensity = FireballComponent->GetIntensity();
		Item.Radius = FireballComponent->GetRadius();
		Item.RadiusFallOff = FireballComponent->GetRadiusFallOff();
	}
}

void FSceneCommandPostProcessBuilder::BuildDecalInputs(
	const FSceneRenderPacket& Packet,
	FSceneViewData& OutSceneViewData) const
{
	OutSceneViewData.PostProcessInputs.DecalItems.reserve(Packet.DecalPrimitives.size());
	for (const FSceneDecalPrimitive& Primitive : Packet.DecalPrimitives)
	{
		const UDecalComponent* DecalComponent = Primitive.Component;
		if (!DecalComponent || !DecalComponent->IsEnabled())
		{
			continue;
		}

		FDecalRenderItem& Item = OutSceneViewData.PostProcessInputs.DecalItems.emplace_back();
		Item.AtlasScaleBias = DecalComponent->GetAtlasScaleBias();
		Item.BaseColorTint = DecalComponent->GetBaseColorTint();
		Item.DecalWorld = DecalComponent->GetWorldTransform();
		Item.EdgeFade = DecalComponent->GetEdgeFade();
		Item.EmissiveBlend = DecalComponent->GetEmissiveBlend();
		Item.Extents = DecalComponent->GetExtents();
		Item.Flags = DecalComponent->GetRenderFlags();
		Item.NormalBlend = DecalComponent->GetNormalBlend();
		Item.Priority = DecalComponent->GetPriority();
		Item.ReceiverLayerMask = DecalComponent->GetReceiverLayerMask();
		Item.RoughnessBlend = DecalComponent->GetRoughnessBlend();
		Item.TexturePath = DecalComponent->GetTexturePath();
		Item.TextureIndex = 0;
		Item.WorldToDecal = Item.DecalWorld.GetInverse();
		Item.bIsFading = DecalComponent->GetFadeState() != EDecalFadeState::None;
		const float AngleRad = DecalComponent->GetAllowAngle() * (3.14159265f / 180.0f);
		Item.AllowAngle = std::cos(AngleRad);
	}
}
