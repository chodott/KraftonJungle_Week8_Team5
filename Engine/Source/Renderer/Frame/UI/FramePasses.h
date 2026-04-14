#pragma once

#include "Renderer/Frame/UI/FramePassContext.h"

class ENGINE_API FViewportCompositePass : public IFrameRenderPass
{
public:
	bool Execute(FFramePassContext& Context) override;
};

class ENGINE_API FScreenUIPass : public IFrameRenderPass
{
public:
	bool Execute(FFramePassContext& Context) override;
};
