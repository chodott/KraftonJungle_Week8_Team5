#pragma once

#include "Renderer/Scene/Passes/PassContext.h"

class FMeshPassProcessor;

class ENGINE_API FClearSceneTargetsPass : public IRenderPass
{
public:
	FPassDesc Describe() const override
	{
		return {
			.Name     = "Clear Scene Targets",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Setup,
			.Reads    = 0,
			.Writes   = PassTarget(ESceneTarget::SceneColor)
			          | PassTarget(ESceneTarget::SceneDepth)
			          | PassTarget(ESceneTarget::GBufferA)
			          | PassTarget(ESceneTarget::GBufferB)
			          | PassTarget(ESceneTarget::GBufferC)
			          | PassTarget(ESceneTarget::OutlineMask),
		};
	}

	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FUploadMeshBuffersPass : public IRenderPass
{
public:
	explicit FUploadMeshBuffersPass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}

	FPassDesc Describe() const override
	{
		return {
			.Name     = "Upload Mesh Buffers",
			.Domain   = EPassDomain::Copy,
			.Category = EPassCategory::Setup,
		};
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

	FPassDesc Describe() const override
	{
		return {
			.Name     = "Depth Prepass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Geometry,
			.Reads    = 0,
			.Writes   = PassTarget(ESceneTarget::SceneDepth),
		};
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

	FPassDesc Describe() const override
	{
		return {
			.Name     = "GBuffer Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Geometry,
			.Reads    = PassTarget(ESceneTarget::SceneDepth),
			.Writes   = PassTarget(ESceneTarget::GBufferA)
			          | PassTarget(ESceneTarget::GBufferB)
			          | PassTarget(ESceneTarget::GBufferC),
		};
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

	FPassDesc Describe() const override
	{
		return {
			.Name     = "Forward Opaque Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Geometry,
			.Reads    = PassTarget(ESceneTarget::SceneDepth)
			          | PassTarget(ESceneTarget::ShadowMap),
			.Writes   = PassTarget(ESceneTarget::SceneColor),
		};
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

	FPassDesc Describe() const override
	{
		return {
			.Name     = "Shadow Map Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Geometry,
			.Reads    = 0,
			.Writes   = PassTarget(ESceneTarget::ShadowMap),
		};
	}

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;

};