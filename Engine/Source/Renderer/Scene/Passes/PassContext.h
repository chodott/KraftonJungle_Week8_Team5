#pragma once

#include "CoreMinimal.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/SceneViewData.h"

class FRenderer;

enum class EPassDomain : uint8
{
	Graphics,
	Compute,
	Copy,
};

struct ENGINE_API FPassContext
{
	FRenderer& Renderer;
	FSceneRenderTargets& Targets;
	FSceneViewData& SceneViewData;
	FVector4 ClearColor = FVector4(0.1f, 0.1f, 0.1f, 1.0f);
};

class ENGINE_API IRenderPass
{
public:
	virtual ~IRenderPass() = default;
	virtual const char* GetName() const { return "UnnamedPass"; }
	virtual EPassDomain GetDomain() const { return EPassDomain::Graphics; }
	virtual bool Execute(FPassContext& Context) = 0;
};
