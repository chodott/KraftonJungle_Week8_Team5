#pragma once

#include "Renderer/Scene/Passes/PassContext.h"

class FMeshPassProcessor;

class ENGINE_API FEditorPrimitivePass : public IRenderPass
{
public:
	explicit FEditorPrimitivePass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}

	FPassDesc Describe() const override
	{
		return {
			.Name     = "Editor Primitive Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::EditorOverlay,
			.Reads    = 0,
			.Writes   = PassTarget(ESceneTarget::OverlayColor),
		};
	}

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};

class ENGINE_API FEditorLinePass : public IRenderPass
{
public:
	FPassDesc Describe() const override
	{
		return {
			.Name     = "Editor Line Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::EditorOverlay,
			.Reads    = 0,
			.Writes   = PassTarget(ESceneTarget::OverlayColor),
		};
	}

	bool Execute(FPassContext& Context) override;
};
