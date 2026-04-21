#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Debug/DebugTypes.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Common/SceneRenderTargets.h"

class FRenderer;

class ENGINE_API FDebugLineRenderFeature
{
public:
	~FDebugLineRenderFeature();

	static void AppendLine(FEditorLinePassInputs& PassInputs, const FVector& Start, const FVector& End, const FVector4& Color);
	static void AppendCube(FEditorLinePassInputs& PassInputs, const FVector& Center, const FVector& BoxExtent, const FVector4& Color);

	bool Render(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		const FSceneRenderTargets& Targets,
		FEditorLinePassInputs& PassInputs);
	void Release();

private:
	bool EnsureDebugDepthState(ID3D11Device* Device);
	ID3D11DepthStencilState* DebugDepthOffState = nullptr;
};
