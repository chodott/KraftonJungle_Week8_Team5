#pragma once

#include "CoreMinimal.h"
#include "Core/ShowFlags.h"
#include "Renderer/Features/Debug/DebugTypes.h"
#include "Renderer/Features/Decal/DecalTypes.h"
#include "Renderer/Features/Decal/MeshDecalTypes.h"
#include "Renderer/Features/Fog/FogTypes.h"
#include "Renderer/Features/Outline/OutlineTypes.h"
#include "Renderer/Features/FireBall/FireBallTypes.h"
#include "Renderer/Mesh/MeshBatch.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Common/RenderMode.h"
#include "Renderer/Features/Lighting/LightTypes.h"
#include "Renderer/Features/Shadow/ShadowTypes.h"

#include <d3d11.h>

class UWorld;

struct ENGINE_API FAmbientLightRenderItem
{
	FVector Color     = FVector::OneVector;
	float   Intensity = 0.1f;
};

struct ENGINE_API FLocalLightRenderItem
{
	ELightClass    LightClass = ELightClass::Point;
	ECullShapeType CullShape  = ECullShapeType::Sphere;

	FVector Color     = FVector::OneVector;
	float   Intensity = 1.0f;

	FVector PositionWS = FVector::ZeroVector;
	float   Range      = 0.0f;

	FVector DirectionWS     = FVector(0.0f, 0.0f, -1.0f);
	float   InnerAngleCos   = 1.0f;
	float   OuterAngleCos   = 1.0f;
	float   FalloffExponent = 0.0f;

	uint32 Flags = 0;

	FVector Axis0   = FVector(1.0f, 0.0f, 0.0f);
	float   Extent0 = 0.0f;

	FVector Axis1   = FVector(0.0f, 1.0f, 0.0f);
	float   Extent1 = 0.0f;

	FVector Axis2   = FVector(0.0f, 0.0f, 1.0f);
	float   Extent2 = 0.0f;

	// broadphase용 conservative sphere
	FVector CullCenterWS = FVector::ZeroVector;
	float   CullRadius   = 0.0f;

	uint32 ShadowIndex = UINT32_MAX;
	uint32 CookieIndex = UINT32_MAX;
	uint32 IESIndex    = UINT32_MAX;
};

struct ENGINE_API FDirectionalLightRenderItem
{
	FVector DirectionWS = FVector(0.0f, 0.0f, -1.0f);
	float   Intensity   = 1.0f;

	FVector Color = FVector::OneVector;
	uint32  Flags = 0;

	FVector4 CascadeSplits;

	uint32 ShadowIndex = UINT32_MAX;
};

struct ENGINE_API FSceneLightingInputs
{
	FAmbientLightRenderItem             Ambient;
	TArray<FLocalLightRenderItem>       LocalLights;
	TArray<FDirectionalLightRenderItem> DirectionalLights;

	TArray<FShadowLightRenderItem> ShadowLights;
	TArray<FShadowViewRenderItem>  ShadowViews;

	TArray<FShadowLightRenderItem> DirShadowLights;
	TArray<FShadowViewRenderItem>  DirShadowViews;

	TArray<uint32> ObjectLightIndices;

	void Clear()
	{
		Ambient = {};
		LocalLights.clear();
		DirectionalLights.clear();
		ShadowLights.clear();
		ShadowViews.clear();
		ObjectLightIndices.clear();
	}
};

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
	TArray<FFogRenderItem>              FogItems;
	TArray<FDecalRenderItem>            DecalItems;
	TArray<FMeshDecalRenderItem>        MeshDecalItems;
	TArray<FMeshDecalReceiverCandidate> MeshDecalReceiverCandidates;
	FMeshDecalBuildStats                MeshDecalStats;
	ID3D11ShaderResourceView*           DecalBaseColorTextureArraySRV = nullptr;
	TArray<FOutlineRenderItem>          OutlineItems;
	TArray<FFireBallRenderItem>         FireBallItems;
	bool                                bOutlineEnabled = false;
	bool                                bApplyFXAA      = false;

	void Clear()
	{
		FogItems.clear();
		DecalItems.clear();
		MeshDecalItems.clear();
		MeshDecalReceiverCandidates.clear();
		MeshDecalStats                = {};
		DecalBaseColorTextureArraySRV = nullptr;
		OutlineItems.clear();
		FireBallItems.clear();
		bOutlineEnabled = false;
		bApplyFXAA      = false;
	}
};

struct ENGINE_API FSceneDebugInputs
{
	UWorld*               World = nullptr;
	FEditorLinePassInputs LinePass;

	void Clear()
	{
		World = nullptr;
		LinePass.Clear();
	}
};

struct ENGINE_API FSceneViewData
{
	FFrameContext Frame;
	FViewContext  View;

	FSceneMeshInputs        MeshInputs;
	FSceneLightingInputs    LightingInputs;
	FScenePostProcessInputs PostProcessInputs;
	FSceneDebugInputs       DebugInputs;

	FShowFlags  ShowFlags;
	ERenderMode RenderMode      = ERenderMode::Lit_Gouraud;
	bool        bForceWireframe = false;
};
