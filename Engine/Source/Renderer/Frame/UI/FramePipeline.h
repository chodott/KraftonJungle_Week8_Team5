#pragma once

#include "EngineAPI.h"
#include "Renderer/Frame/UI/FramePassContext.h"
#include "Renderer/Scene/Pipeline/PassPipeline.h"

#include <memory>

class ENGINE_API FFrameRenderPipeline
{
public:
	FFrameRenderPipeline() = default;
	FFrameRenderPipeline(const FFrameRenderPipeline&) = delete;
	FFrameRenderPipeline& operator=(const FFrameRenderPipeline&) = delete;
	FFrameRenderPipeline(FFrameRenderPipeline&&) = default;
	FFrameRenderPipeline& operator=(FFrameRenderPipeline&&) = default;

	void Reset();
	void AddPass(std::unique_ptr<IFrameRenderPass> Pass);
	bool Execute(FFramePassContext& Context) const;

private:
	TPassPipeline<IFrameRenderPass, FFramePassContext> PassSequence;
};
