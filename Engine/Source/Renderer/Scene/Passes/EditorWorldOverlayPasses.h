#pragma once

#include "Renderer/Scene/Passes/PassContext.h"

class FMeshPassProcessor;

class ENGINE_API FEditorGridPass : public IRenderPass
{
public:
	explicit FEditorGridPass(const FMeshPassProcessor& InProcessor)
		: Processor(InProcessor)
	{
	}

	FPassDesc Describe() const override
	{
		return {
			.Name     = "Editor Grid Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::EditorOverlay,
			.Reads    = PassTarget(ESceneTarget::SceneDepth),
			.Writes   = PassTarget(ESceneTarget::SceneColor),
		};
	}

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};
