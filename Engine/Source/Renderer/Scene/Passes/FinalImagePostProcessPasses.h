#pragma once

#include "Renderer/Scene/Passes/PassContext.h"

class ENGINE_API FFXAAPass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;
};
