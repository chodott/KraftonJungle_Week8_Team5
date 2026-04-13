#pragma once

#include "CoreMinimal.h"
#include "Renderer/Feature/DebugLineRenderFeature.h"
#include "Renderer/Feature/DecalRenderFeature.h"
#include "Renderer/Feature/FogRenderFeature.h"
#include "Renderer/Feature/OutlineRenderFeature.h"
#include  "Renderer/Feature/FireBallRenderFeature.h"
#include "Renderer/MeshBatch.h"
#include "Renderer/RenderFrameContext.h"

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

	void Clear()
	{
		FogItems.clear();
		DecalItems.clear();
		DecalBaseColorTextureArraySRV = nullptr;
		OutlineItems.clear();
		FireBallItems.clear();
		bOutlineEnabled = false;
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
};
