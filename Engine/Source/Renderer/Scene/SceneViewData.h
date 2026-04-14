#pragma once

#include "CoreMinimal.h"
#include "Core/ShowFlags.h"
#include "Renderer/Features/Debug/DebugTypes.h"
#include "Renderer/Features/Decal/DecalTypes.h"
#include "Renderer/Features/Fog/FogTypes.h"
#include "Renderer/Features/Outline/OutlineTypes.h"
#include "Renderer/Features/FireBall/FireBallTypes.h"
#include "Renderer/Mesh/MeshBatch.h"
#include "Renderer/Common/RenderFrameContext.h"

#include <d3d11.h>

struct ENGINE_API FSceneMeshInputs
{
	TArray<FMeshBatch> Batches;

	void Clear()
	{
		Batches.clear();
	}
};

struct ENGINE_API FScenePostProcessInputs
{
	TArray<FFogRenderItem> FogItems;
	TArray<FDecalRenderItem> DecalItems;
	ID3D11ShaderResourceView* DecalBaseColorTextureArraySRV = nullptr;
	TArray<FOutlineRenderItem> OutlineItems;
	TArray<FFireBallRenderItem> FireBallItems;
	bool bOutlineEnabled = false;
	bool bApplyFXAA = false;

	void Clear()
	{
		FogItems.clear();
		DecalItems.clear();
		DecalBaseColorTextureArraySRV = nullptr;
		OutlineItems.clear();
		FireBallItems.clear();
		bOutlineEnabled = false;
		bApplyFXAA = false;
	}
};

struct ENGINE_API FSceneDebugInputs
{
	FDebugLinePassInputs LinePass;

	void Clear()
	{
		LinePass.Clear();
	}
};

struct ENGINE_API FSceneViewData
{
	FFrameContext Frame;
	FViewContext View;

	FSceneMeshInputs MeshInputs;
	FScenePostProcessInputs PostProcessInputs;
	FSceneDebugInputs DebugInputs;
	FShowFlags ShowFlags;
	bool bForceWireframe = false;
};
