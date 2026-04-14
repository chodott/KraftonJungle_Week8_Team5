#include "Renderer/Frame/RenderFrameUtils.h"

#include "Core/Engine.h"

FFrameContext BuildRenderFrameContext(float TotalTimeSeconds)
{
	FFrameContext Frame;
	Frame.TotalTimeSeconds = TotalTimeSeconds;
	Frame.DeltaTimeSeconds = GEngine ? GEngine->GetDeltaTime() : 0.0f;
	return Frame;
}

FViewContext BuildRenderViewContext(const FSceneViewRenderRequest& SceneView, const D3D11_VIEWPORT& Viewport)
{
	FViewContext View;
	View.View = SceneView.ViewMatrix;
	View.Projection = SceneView.ProjectionMatrix;
	View.ViewProjection = SceneView.ViewMatrix * SceneView.ProjectionMatrix;
	View.InverseView = SceneView.ViewMatrix.GetInverse();
	View.InverseProjection = SceneView.ProjectionMatrix.GetInverse();
	View.InverseViewProjection = View.ViewProjection.GetInverse();
	View.CameraPosition = SceneView.CameraPosition;
	View.NearZ = SceneView.NearZ;
	View.FarZ = SceneView.FarZ;
	View.Viewport = Viewport;
	return View;
}
