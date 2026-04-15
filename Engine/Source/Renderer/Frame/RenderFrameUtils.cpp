#include "Renderer/Frame/RenderFrameUtils.h"

#include "Core/Engine.h"

#include <cmath>

namespace
{
	bool IsOrthographicProjection(const FMatrix& Projection)
	{
		return std::fabs(Projection.M[0][3]) <= 1.0e-6f
			&& std::fabs(Projection.M[3][3] - 1.0f) <= 1.0e-6f;
	}
}

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
	View.bOrthographic = IsOrthographicProjection(SceneView.ProjectionMatrix);
	View.Viewport = Viewport;
	return View;
}
