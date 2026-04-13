#pragma once

#include "Renderer/MeshPassProcessor.h"
#include "Renderer/PassContext.h"

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

class ENGINE_API FOutlineMaskPass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FOutlineCompositePass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FOverlayPass : public IRenderPass
{
public:
	explicit FOverlayPass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};

class ENGINE_API FDebugLinePass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FFireBallPass : public IRenderPass
{
	public:
	bool Execute(FPassContext& Context) override;
};
