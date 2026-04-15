#pragma once

#include "Renderer/Scene/Passes/PassContext.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Features/Decal/DecalRenderFeature.h"
#include "Renderer/Features/Outline/OutlineRenderFeature.h"

inline FDecalRenderRequest BuildDecalPassRequest(const FSceneViewData& SceneViewData)
{
	FDecalRenderRequest Request;
	Request.Items = SceneViewData.PostProcessInputs.DecalItems;
	Request.View = SceneViewData.View.View;
	Request.Projection = SceneViewData.View.Projection;
	Request.ViewProjection = SceneViewData.View.ViewProjection;
	Request.InverseViewProjection = SceneViewData.View.InverseViewProjection;
	Request.CameraPosition = SceneViewData.View.CameraPosition;
	Request.ViewportWidth = static_cast<uint32>(SceneViewData.View.Viewport.Width);
	Request.ViewportHeight = static_cast<uint32>(SceneViewData.View.Viewport.Height);
	Request.NearZ = SceneViewData.View.NearZ;
	Request.FarZ = SceneViewData.View.FarZ;
	Request.ClusterCountX = 16;
	Request.ClusterCountY = 9;
	Request.ClusterCountZ = 24;
	Request.ReceiverLayerMask = 0xFFFFFFFFu;
	Request.BaseColorTextureArraySRV = SceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV;
	Request.CandidateReceiverObjectCount = static_cast<uint32>(SceneViewData.MeshInputs.Batches.size());
	Request.bDebugDraw = SceneViewData.ShowFlags.HasFlag(EEngineShowFlags::SF_DebugVolume)
	                  && SceneViewData.ShowFlags.HasFlag(EEngineShowFlags::SF_DecalDebug)
	                  && !SceneViewData.bForceWireframe;
	return Request;
}

inline FDecalRenderRequest BuildLocalFogDebugPassRequest(const FSceneViewData& SceneViewData)
{
	FDecalRenderRequest Request;
	for (const FFogRenderItem& FogItem : SceneViewData.PostProcessInputs.FogItems)
	{
		if (!FogItem.IsLocalFogVolume())
		{
			continue;
		}

		FDecalRenderItem& Item = Request.Items.emplace_back();
		Item.DecalWorld = FogItem.FogVolumeWorld;
		Item.WorldToDecal = FogItem.WorldToFogVolume;
		Item.Extents = FVector(0.5f, 0.5f, 0.5f);
		Item.Flags = DECAL_RENDER_FLAG_BaseColor;
		Item.Priority = 0u;
		Item.ReceiverLayerMask = 0xFFFFFFFFu;
		Item.AtlasScaleBias = FVector4(1.0f, 1.0f, 0.0f, 0.0f);
		Item.BaseColorTint = FogItem.FogInscatteringColor;
		Item.EdgeFade = 1.0f;
		Item.AllowAngle = 0.0f;
	}

	Request.View = SceneViewData.View.View;
	Request.Projection = SceneViewData.View.Projection;
	Request.ViewProjection = SceneViewData.View.ViewProjection;
	Request.InverseViewProjection = SceneViewData.View.InverseViewProjection;
	Request.CameraPosition = SceneViewData.View.CameraPosition;
	Request.ViewportWidth = static_cast<uint32>(SceneViewData.View.Viewport.Width);
	Request.ViewportHeight = static_cast<uint32>(SceneViewData.View.Viewport.Height);
	Request.NearZ = SceneViewData.View.NearZ;
	Request.FarZ = SceneViewData.View.FarZ;
	Request.ClusterCountX = 16;
	Request.ClusterCountY = 9;
	Request.ClusterCountZ = 24;
	Request.ReceiverLayerMask = 0xFFFFFFFFu;
	Request.CandidateReceiverObjectCount = static_cast<uint32>(SceneViewData.MeshInputs.Batches.size());
	Request.bDebugDraw = SceneViewData.ShowFlags.HasFlag(EEngineShowFlags::SF_DebugVolume)
	                  && SceneViewData.ShowFlags.HasFlag(EEngineShowFlags::SF_LocalFogDebug)
	                  && !SceneViewData.bForceWireframe;
	return Request;
}

inline FOutlineRenderRequest BuildOutlinePassRequest(const FSceneViewData& SceneViewData)
{
	FOutlineRenderRequest Request;
	Request.bEnabled = SceneViewData.PostProcessInputs.bOutlineEnabled;
	Request.Items = SceneViewData.PostProcessInputs.OutlineItems;
	return Request;
}

inline bool ExecuteMeshScenePass(
	FRenderer& Renderer,
	FSceneRenderTargets& Targets,
	FSceneViewData& SceneViewData,
	const FMeshPassProcessor& Processor,
	EMeshPassType PassType)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	BeginPass(
		Renderer,
		Targets.SceneColorRTV,
		Targets.SceneDepthDSV,
		SceneViewData.View.Viewport,
		SceneViewData.Frame,
		SceneViewData.View);
	Processor.ExecutePass(Renderer, Targets, SceneViewData, PassType);
	EndPass(
		Renderer,
		Targets.SceneColorRTV,
		Targets.SceneDepthDSV,
		SceneViewData.View.Viewport,
		SceneViewData.Frame,
		SceneViewData.View);
	return true;
}
