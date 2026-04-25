#pragma once

#include "PassContext.h"

class ENGINE_API FLightCullingComputePass : public IRenderPass
{
public:
	FPassDesc Describe() const override
	{
		return {
			.Name     = "Light Culling Compute Pass",
			.Domain   = EPassDomain::Compute,
			.Category = EPassCategory::Lighting,
			.Reads    = PassTarget(ESceneTarget::SceneDepth),
			.Writes   = 0,
		};
	}

	bool Execute(FPassContext& Context) override;
};
