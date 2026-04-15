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

	bool Execute(FPassContext& Context) override;

private:
	const FMeshPassProcessor& Processor;
};
