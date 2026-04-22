#pragma once

#include "PassContext.h"

class ENGINE_API FLightCullingComputePass : public IRenderPass
{
public:
	const char* GetName() const override
	{
		return "Light Culling Compute Pass";
	}

	EPassDomain GetDomain() const override
	{
		return EPassDomain::Compute;
	}

	bool Execute(FPassContext& Context) override;
};
