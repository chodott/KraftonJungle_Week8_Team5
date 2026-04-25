#pragma once

#include "Renderer/Scene/Passes/PassContext.h"

class ENGINE_API FFXAAPass : public IRenderPass
{
public:
	bool Execute(FPassContext& Context) override;

	FPassDesc Describe() const override
	{
		return {
			.Name     = "FXAA Pass",
			.Domain   = EPassDomain::Graphics,
			.Category = EPassCategory::Effects,
			.Reads    = PassTarget(ESceneTarget::SceneColor),
			.Writes   = PassTarget(ESceneTarget::SceneColorScratch),
		};
	}
};
