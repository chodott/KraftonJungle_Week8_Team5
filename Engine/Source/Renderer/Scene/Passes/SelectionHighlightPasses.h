#pragma once

#include "Renderer/Scene/Passes/PassContext.h"

class ENGINE_API FOutlineMaskPass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;
};

class ENGINE_API FOutlineCompositePass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;
};
