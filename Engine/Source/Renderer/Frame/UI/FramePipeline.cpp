#include "Renderer/Frame/UI/FramePipeline.h"

void FFrameRenderPipeline::Reset()
{
	PassSequence.Reset();
}

void FFrameRenderPipeline::AddPass(std::unique_ptr<IFrameRenderPass> Pass)
{
	PassSequence.AddPass(std::move(Pass));
}

bool FFrameRenderPipeline::Execute(FFramePassContext& Context) const
{
	return PassSequence.Execute(Context);
}
