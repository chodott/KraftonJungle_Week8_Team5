#pragma once

#include "EngineAPI.h"
#include "Renderer/Scene/Passes/PassContext.h"
#include "Renderer/Scene/Pipeline/PassPipeline.h"

#include <memory>

class ENGINE_API FRenderPipeline
{
public:
	FRenderPipeline() = default;
	FRenderPipeline(const FRenderPipeline&) = delete;
	FRenderPipeline& operator=(const FRenderPipeline&) = delete;
	FRenderPipeline(FRenderPipeline&&) = default;
	FRenderPipeline& operator=(FRenderPipeline&&) = default;

	void Reset();
	void AddPass(std::unique_ptr<IRenderPass> Pass);
	bool Execute(FPassContext& Context) const;

private:
	TPassPipeline<IRenderPass, FPassContext> PassSequence;
};
