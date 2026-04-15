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

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};

class ENGINE_API FEditorLinePass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;
};
