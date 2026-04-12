#include "RenderStateManager.h"

#include <vector>

void FRenderStateManager::PrepareCommonStates()
{
	const D3D11_FILL_MODE FillModes[] = { D3D11_FILL_SOLID, D3D11_FILL_WIREFRAME };
	const D3D11_CULL_MODE CullModes[] = { D3D11_CULL_NONE, D3D11_CULL_FRONT, D3D11_CULL_BACK };

	for (const D3D11_FILL_MODE FillMode : FillModes)
	{
		for (const D3D11_CULL_MODE CullMode : CullModes)
		{
			FRasterizerStateOption RasterOpt;
			RasterOpt.FillMode = FillMode;
			RasterOpt.CullMode = CullMode;
			GetOrCreateRasterizerState(RasterOpt);
		}
	}

	FBlendStateOption OpaqueBlendOpt;
	GetOrCreateBlendState(OpaqueBlendOpt);

	FBlendStateOption AlphaBlendOpt;
	AlphaBlendOpt.BlendEnable = true;
	AlphaBlendOpt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
	AlphaBlendOpt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	AlphaBlendOpt.BlendOp = D3D11_BLEND_OP_ADD;
	AlphaBlendOpt.SrcBlendAlpha = D3D11_BLEND_ONE;
	AlphaBlendOpt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	AlphaBlendOpt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	GetOrCreateBlendState(AlphaBlendOpt);
}

std::shared_ptr<FRasterizerState> FRenderStateManager::GetOrCreateRasterizerState(const FRasterizerStateOption& opt)
{
	const uint32 Key = opt.ToKey();
	auto It = RasterizerStateMap.find(Key);
	if (It != RasterizerStateMap.end())
	{
		return It->second;
	}

	std::shared_ptr<FRasterizerState> State = FRasterizerState::Create(Device, opt);
	RasterizerStateMap[Key] = State;
	return State;
}

std::shared_ptr<FDepthStencilState> FRenderStateManager::GetOrCreateDepthStencilState(const FDepthStencilStateOption& opt)
{
	const uint32 Key = opt.ToKey();
	auto It = DepthStencilStateMap.find(Key);
	if (It != DepthStencilStateMap.end())
	{
		return It->second;
	}

	std::shared_ptr<FDepthStencilState> State = FDepthStencilState::Create(Device, opt);
	DepthStencilStateMap[Key] = State;
	return State;
}

std::shared_ptr<FBlendState> FRenderStateManager::GetOrCreateBlendState(const FBlendStateOption& opt)
{
	const uint32 Key = opt.ToKey();
	auto It = BlendStateMap.find(Key);
	if (It != BlendStateMap.end())
	{
		return It->second;
	}

	std::shared_ptr<FBlendState> State = FBlendState::Create(Device, opt);
	BlendStateMap[Key] = State;
	return State;
}

void FRenderStateManager::BindState(std::shared_ptr<FRasterizerState> InRS)
{
	if (InRS && CurrentRasterizerState != InRS)
	{
		InRS->Bind(DeviceContext);
		CurrentRasterizerState = InRS;
	}
}

void FRenderStateManager::BindState(std::shared_ptr<FDepthStencilState> InDSS)
{
	BindDepthStencilState(InDSS, CurrentStencilRef);
}

void FRenderStateManager::BindState(std::shared_ptr<FBlendState> InBS)
{
	BindBlendState(InBS, CurrentBlendFactor, CurrentSampleMask);
}

void FRenderStateManager::BindDepthStencilState(std::shared_ptr<FDepthStencilState> InDSS, uint32 StencilRef)
{
	const bool bStencilRefChanged = CurrentStencilRef != StencilRef;
	CurrentStencilRef = StencilRef;
	if (!InDSS)
	{
		return;
	}

	if (CurrentDepthStencilState == InDSS && !bStencilRefChanged)
	{
		return;
	}

	InDSS->Bind(DeviceContext, StencilRef);
	CurrentDepthStencilState = InDSS;
}

void FRenderStateManager::BindBlendState(std::shared_ptr<FBlendState> InBS, const float BlendFactor[4], uint32 SampleMask)
{
	const bool bSampleMaskChanged = CurrentSampleMask != SampleMask;
	bool bBlendFactorChanged = false;
	float ResolvedBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	CurrentSampleMask = SampleMask;
	if (BlendFactor)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			ResolvedBlendFactor[Index] = BlendFactor[Index];
			bBlendFactorChanged = bBlendFactorChanged || CurrentBlendFactor[Index] != BlendFactor[Index];
			CurrentBlendFactor[Index] = BlendFactor[Index];
		}
	}
	else
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			ResolvedBlendFactor[Index] = CurrentBlendFactor[Index];
		}
	}

	if (InBS && (CurrentBlendState != InBS || bSampleMaskChanged || bBlendFactorChanged))
	{
		InBS->Bind(DeviceContext, ResolvedBlendFactor, CurrentSampleMask);
		CurrentBlendState = InBS;
	}
}

void FRenderStateManager::SetRenderTargets(uint32 NumRTs, ID3D11RenderTargetView* const* RTVs, ID3D11DepthStencilView* DSV)
{
	DeviceContext->OMSetRenderTargets(NumRTs, RTVs, DSV);
}

void FRenderStateManager::ClearShaderResourcesPS(uint32 StartSlot, uint32 Count)
{
	if (Count == 0)
	{
		return;
	}

	std::vector<ID3D11ShaderResourceView*> NullSRVs(Count, nullptr);
	DeviceContext->PSSetShaderResources(StartSlot, Count, NullSRVs.data());
}

void FRenderStateManager::RebindState()
{
	if (CurrentRasterizerState)
	{
		CurrentRasterizerState->Bind(DeviceContext);
	}
	if (CurrentDepthStencilState)
	{
		CurrentDepthStencilState->Bind(DeviceContext, CurrentStencilRef);
	}
	if (CurrentBlendState)
	{
		CurrentBlendState->Bind(DeviceContext, CurrentBlendFactor, CurrentSampleMask);
	}
}
