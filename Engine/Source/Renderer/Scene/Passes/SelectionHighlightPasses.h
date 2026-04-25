#pragma once

#include "Renderer/Scene/Passes/PassContext.h"

class ENGINE_API FOutlineMaskPass : public IRenderPass
{
public:
	FPassDesc Describe() const override
	{
		return {
			.Name     = "Outline Mask Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::EditorOverlay,
			.Reads    = 0,
			.Writes   = PassTarget(ESceneTarget::OutlineMask),
		};
	}

	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FOutlineCompositePass : public IRenderPass
{
public:
	FPassDesc Describe() const override
	{
		return {
			.Name     = "Outline Composite Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::EditorOverlay,
			.Reads    = PassTarget(ESceneTarget::OutlineMask),
			.Writes   = PassTarget(ESceneTarget::SceneColor),
		};
	}

	bool Execute(FPassContext& Context) override;
};
