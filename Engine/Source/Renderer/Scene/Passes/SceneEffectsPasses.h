#pragma once

#include "Renderer/Scene/Passes/PassContext.h"

class FMeshPassProcessor;

class ENGINE_API FMeshDecalPass : public IRenderPass
{
public:
	explicit FMeshDecalPass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}

	FPassDesc Describe() const override
	{
		return {
			.Name     = "Mesh Decal Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Effects,
			.Reads    = PassTarget(ESceneTarget::SceneDepth),
			.Writes   = PassTarget(ESceneTarget::SceneColor),
		};
	}

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};

class ENGINE_API FDecalCompositePass : public IRenderPass
{
public:
	FPassDesc Describe() const override
	{
		return {
			.Name     = "Decal Composite Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Effects,
			.Reads    = PassTarget(ESceneTarget::SceneColor)
			          | PassTarget(ESceneTarget::SceneDepth),
			.Writes   = PassTarget(ESceneTarget::SceneColorScratch),
		};
	}

	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FForwardTransparentPass : public IRenderPass
{
public:
	explicit FForwardTransparentPass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}

	FPassDesc Describe() const override
	{
		return {
			.Name     = "Forward Transparent Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Effects,
			.Reads    = PassTarget(ESceneTarget::SceneDepth),
			.Writes   = PassTarget(ESceneTarget::SceneColor),
		};
	}

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};

class ENGINE_API FFogPostPass : public IRenderPass
{
public:
	FPassDesc Describe() const override
	{
		return {
			.Name     = "Fog Post Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Effects,
			.Reads    = PassTarget(ESceneTarget::SceneColor)
			          | PassTarget(ESceneTarget::SceneDepth),
			.Writes   = PassTarget(ESceneTarget::SceneColorScratch),
		};
	}

	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FFireBallPass : public IRenderPass
{
public:
	FPassDesc Describe() const override
	{
		return {
			.Name     = "FireBall Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Effects,
			.Reads    = PassTarget(ESceneTarget::SceneDepth),
			.Writes   = PassTarget(ESceneTarget::SceneColor),
		};
	}

	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FBloomPass : public IRenderPass
{
public:
	FPassDesc Describe() const override
	{
		return {
			.Name     = "Bloom Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Effects,
			.Reads    = PassTarget(ESceneTarget::SceneColor),
			.Writes   = PassTarget(ESceneTarget::SceneColorScratch),
		};
	}

	bool Execute(FPassContext& Context) override;
};
