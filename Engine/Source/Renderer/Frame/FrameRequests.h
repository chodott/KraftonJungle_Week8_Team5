#pragma once

#include "CoreMinimal.h"
#include "Core/ShowFlags.h"
#include "Level/SceneRenderPacket.h"
#include "Renderer/Features/Outline/OutlineTypes.h"
#include "Renderer/Mesh/MeshBatch.h"
#include "Renderer/UI/Screen/UIDrawList.h"
#include "Renderer/Frame/Viewport/ViewportCompositor.h"
#include "Renderer/Common/RenderMode.h"

#include <d3d11.h>

class FMaterial;
class UWorld;
class AActor;
class FDebugDrawManager;

struct FSceneViewRenderRequest
{
	FMatrix ViewMatrix = FMatrix::Identity;
	FMatrix ProjectionMatrix = FMatrix::Identity;
	FVector CameraPosition = FVector::ZeroVector;
	float TotalTimeSeconds = 0.0f;
	float NearZ = 0.1f;
	float FarZ = 1000.0f;
};

struct FDebugSceneBuildInputs
{
	const FDebugDrawManager* DrawManager = nullptr;
	UWorld* World = nullptr;
	AActor* BoundsActor = nullptr;
	FShowFlags ShowFlags = {};
};

struct FGameFrameRequest
{
	FSceneRenderPacket ScenePacket;
	FSceneViewRenderRequest SceneView;
	TArray<FMeshBatch> AdditionalMeshBatches;
	FDebugSceneBuildInputs DebugInputs;
    ERenderMode RenderMode = ERenderMode::Lighting;
	EViewportCompositeMode CompositeMode = EViewportCompositeMode::SceneColor;
	bool bForceWireframe = false;
	FMaterial* WireframeMaterial = nullptr;
	float ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
};

struct FViewportScenePassRequest
{
	ID3D11RenderTargetView* RenderTargetView = nullptr;
	ID3D11ShaderResourceView* RenderTargetShaderResourceView = nullptr;
	ID3D11DepthStencilView* DepthStencilView = nullptr;
	ID3D11ShaderResourceView* DepthShaderResourceView = nullptr;
	D3D11_VIEWPORT Viewport = {};
	FSceneRenderPacket ScenePacket;
	FSceneViewRenderRequest SceneView;
	ERenderMode RenderMode = ERenderMode::Lighting;
	TArray<FMeshBatch> AdditionalMeshBatches;
	FOutlineRenderRequest OutlineRequest;
	FDebugSceneBuildInputs DebugInputs;
	bool bForceWireframe = false;
	FMaterial* WireframeMaterial = nullptr;
	float ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };

	bool IsValid() const
	{
		return RenderTargetView != nullptr
			&& DepthStencilView != nullptr
			&& RenderTargetShaderResourceView != nullptr
			&& DepthShaderResourceView != nullptr
			&& Viewport.Width > 0.0f
			&& Viewport.Height > 0.0f;
	}
};

struct FEditorFrameRequest
{
	TArray<FViewportScenePassRequest> ScenePasses;
	TArray<FViewportCompositeItem> CompositeItems;
	FUIDrawList ScreenDrawList;
};
