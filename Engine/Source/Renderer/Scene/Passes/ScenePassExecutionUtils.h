#pragma once

#include "Renderer/Scene/Passes/PassContext.h"
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
