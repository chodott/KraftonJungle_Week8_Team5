#pragma once

#include "CoreMinimal.h"
#include "Renderer/Resources/Shader/Shader.h"

#include <climits>
#include <d3d11.h>
#include <utility>

class FRenderer;

struct ENGINE_API FComputeBindingCB
{
	uint32 Slot = 0;
	ID3D11Buffer* Buffer = nullptr;
};

struct ENGINE_API FComputeBindingSRV
{
	uint32 Slot = 0;
	ID3D11ShaderResourceView* SRV = nullptr;
};

struct ENGINE_API FComputeBindingUAV
{
	uint32 Slot = 0;
	ID3D11UnorderedAccessView* UAV = nullptr;
	uint32 InitialCount = UINT_MAX;
};

struct ENGINE_API FComputeBindingSampler
{
	uint32 Slot = 0;
	ID3D11SamplerState* Sampler = nullptr;
};

class ENGINE_API FComputeDispatchContext
{
public:
	explicit FComputeDispatchContext(FRenderer& InRenderer)
		: Renderer(InRenderer)
	{
	}

	void SetShader(FComputeShader* Shader);
	void SetConstantBuffer(uint32 Slot, ID3D11Buffer* Buffer);
	void SetSRV(uint32 Slot, ID3D11ShaderResourceView* SRV);
	void SetUAV(uint32 Slot, ID3D11UnorderedAccessView* UAV, uint32 InitialCount = UINT_MAX);
	void SetSampler(uint32 Slot, ID3D11SamplerState* Sampler);
	void Dispatch(uint32 GroupX, uint32 GroupY, uint32 GroupZ);
	void Dispatch2D(uint32 Width, uint32 Height, uint32 ThreadsX, uint32 ThreadsY);
	void ClearBindings();

private:
	void UnbindConflictingResource(ID3D11View* View);

private:
	FRenderer& Renderer;
};

#include "Renderer/Renderer.h"

inline void FComputeDispatchContext::SetShader(FComputeShader* Shader)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	Renderer.PreparePassDomain(EPassDomain::Compute, FSceneRenderTargets{});
	if (Shader)
	{
		Shader->Bind(DeviceContext);
	}
	else
	{
		DeviceContext->CSSetShader(nullptr, nullptr, 0);
	}
}

inline void FComputeDispatchContext::SetConstantBuffer(uint32 Slot, ID3D11Buffer* Buffer)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	DeviceContext->CSSetConstantBuffers(Slot, 1, &Buffer);
}

inline void FComputeDispatchContext::UnbindConflictingResource(ID3D11View* View)
{
	if (!View || !Renderer.GetRenderStateManager())
	{
		return;
	}

	ID3D11Resource* Resource = nullptr;
	View->GetResource(&Resource);
	if (Resource)
	{
		Renderer.GetRenderStateManager()->UnbindResourceEverywhere(Resource);
		Resource->Release();
	}
}

inline void FComputeDispatchContext::SetSRV(uint32 Slot, ID3D11ShaderResourceView* SRV)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	UnbindConflictingResource(SRV);
	DeviceContext->CSSetShaderResources(Slot, 1, &SRV);
}

inline void FComputeDispatchContext::SetUAV(uint32 Slot, ID3D11UnorderedAccessView* UAV, uint32 InitialCount)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	UnbindConflictingResource(UAV);
	const uint32 ResolvedInitialCount = (InitialCount == UINT_MAX) ? 0u : InitialCount;
	DeviceContext->CSSetUnorderedAccessViews(Slot, 1, &UAV, &ResolvedInitialCount);
}

inline void FComputeDispatchContext::SetSampler(uint32 Slot, ID3D11SamplerState* Sampler)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	DeviceContext->CSSetSamplers(Slot, 1, &Sampler);
}

inline void FComputeDispatchContext::Dispatch(uint32 GroupX, uint32 GroupY, uint32 GroupZ)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext || GroupX == 0 || GroupY == 0 || GroupZ == 0)
	{
		return;
	}

	DeviceContext->Dispatch(GroupX, GroupY, GroupZ);
}

inline void FComputeDispatchContext::Dispatch2D(uint32 Width, uint32 Height, uint32 ThreadsX, uint32 ThreadsY)
{
	if (Width == 0 || Height == 0 || ThreadsX == 0 || ThreadsY == 0)
	{
		return;
	}

	const uint32 GroupX = (Width + ThreadsX - 1u) / ThreadsX;
	const uint32 GroupY = (Height + ThreadsY - 1u) / ThreadsY;
	Dispatch(GroupX, GroupY, 1u);
}

inline void FComputeDispatchContext::ClearBindings()
{
	if (Renderer.GetRenderStateManager())
	{
		Renderer.GetRenderStateManager()->ClearAllComputeState();
	}
}
