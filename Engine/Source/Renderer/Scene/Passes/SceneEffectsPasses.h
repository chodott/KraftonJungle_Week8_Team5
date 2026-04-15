#pragma once

#include "Renderer/Scene/Passes/PassContext.h"

class FMeshPassProcessor;

class ENGINE_API FDecalCompositePass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FForwardTransparentPass : public IRenderPass
{
public:
	explicit FForwardTransparentPass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};

class ENGINE_API FFogPostPass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FFireBallPass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;
};
