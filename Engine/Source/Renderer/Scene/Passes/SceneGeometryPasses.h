#pragma once

#include "Renderer/Scene/Passes/PassContext.h"

class FMeshPassProcessor;

class ENGINE_API FClearSceneTargetsPass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FUploadMeshBuffersPass : public IRenderPass
{
public:
	explicit FUploadMeshBuffersPass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};

class ENGINE_API FDepthPrepass : public IRenderPass
{
public:
	explicit FDepthPrepass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};

class ENGINE_API FGBufferPass : public IRenderPass
{
public:
	explicit FGBufferPass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};

class ENGINE_API FForwardOpaquePass : public IRenderPass
{
public:
	explicit FForwardOpaquePass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};

class ENGINE_API FShadowMapPass : public IRenderPass
{
public:
	explicit FShadowMapPass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}
	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;

};