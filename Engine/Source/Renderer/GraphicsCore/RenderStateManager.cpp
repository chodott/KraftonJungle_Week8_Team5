#include "Renderer/GraphicsCore/RenderStateManager.h"

#include <array>
#include <vector>

namespace
{
	template <typename TView>
	void ReleaseViewArray(TView* const* Views, uint32 Count)
	{
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			if (Views[Index])
			{
				Views[Index]->Release();
			}
		}
	}

	bool DoesViewReferenceResource(ID3D11View* View, ID3D11Resource* Resource)
	{
		if (!View || !Resource)
		{
			return false;
		}

		ID3D11Resource* ViewResource = nullptr;
		View->GetResource(&ViewResource);
		const bool bMatches = ViewResource == Resource;
		if (ViewResource)
		{
			ViewResource->Release();
		}
		return bMatches;
	}
}

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

void FRenderStateManager::ClearShaderResourcesVS(uint32 StartSlot, uint32 Count)
{
	if (Count == 0)
	{
		return;
	}

	std::vector<ID3D11ShaderResourceView*> NullSRVs(Count, nullptr);
	DeviceContext->VSSetShaderResources(StartSlot, Count, NullSRVs.data());
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

void FRenderStateManager::ClearShaderResourcesCS(uint32 StartSlot, uint32 Count)
{
	if (Count == 0)
	{
		return;
	}

	std::vector<ID3D11ShaderResourceView*> NullSRVs(Count, nullptr);
	DeviceContext->CSSetShaderResources(StartSlot, Count, NullSRVs.data());
}

void FRenderStateManager::ClearUnorderedAccessViewsCS(uint32 StartSlot, uint32 Count)
{
	if (Count == 0)
	{
		return;
	}

	std::vector<ID3D11UnorderedAccessView*> NullUAVs(Count, nullptr);
	std::vector<uint32> InitialCounts(Count, 0u);
	DeviceContext->CSSetUnorderedAccessViews(StartSlot, Count, NullUAVs.data(), InitialCounts.data());
}

void FRenderStateManager::ClearAllGraphicsState()
{
	ClearShaderResourcesVS(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
	ClearShaderResourcesPS(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
	DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	DeviceContext->VSSetShader(nullptr, nullptr, 0);
	DeviceContext->PSSetShader(nullptr, nullptr, 0);
	DeviceContext->OMSetDepthStencilState(nullptr, 0);
	const float BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	DeviceContext->OMSetBlendState(nullptr, BlendFactor, 0xFFFFFFFFu);
	DeviceContext->RSSetState(nullptr);
}

void FRenderStateManager::ClearAllComputeState()
{
	ClearShaderResourcesCS(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
	ClearUnorderedAccessViewsCS(0, D3D11_PS_CS_UAV_REGISTER_COUNT);
	DeviceContext->CSSetShader(nullptr, nullptr, 0);

	std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> NullBuffers = {};
	DeviceContext->CSSetConstantBuffers(0, static_cast<uint32>(NullBuffers.size()), NullBuffers.data());

	std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> NullSamplers = {};
	DeviceContext->CSSetSamplers(0, static_cast<uint32>(NullSamplers.size()), NullSamplers.data());
}

void FRenderStateManager::UnbindResourceFromGraphics(ID3D11Resource* Resource)
{
	if (!Resource)
	{
		return;
	}

	{
		std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> BoundSRVs = {};
		DeviceContext->VSGetShaderResources(0, static_cast<uint32>(BoundSRVs.size()), BoundSRVs.data());

		bool bChanged = false;
		for (ID3D11ShaderResourceView*& SRV : BoundSRVs)
		{
			if (DoesViewReferenceResource(SRV, Resource))
			{
				if (SRV)
				{
					SRV->Release();
				}
				SRV = nullptr;
				bChanged = true;
			}
		}

		if (bChanged)
		{
			DeviceContext->VSSetShaderResources(0, static_cast<uint32>(BoundSRVs.size()), BoundSRVs.data());
		}

		ReleaseViewArray(BoundSRVs.data(), static_cast<uint32>(BoundSRVs.size()));
	}

	{
		std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> BoundSRVs = {};
		DeviceContext->PSGetShaderResources(0, static_cast<uint32>(BoundSRVs.size()), BoundSRVs.data());

		bool bChanged = false;
		for (ID3D11ShaderResourceView*& SRV : BoundSRVs)
		{
			if (DoesViewReferenceResource(SRV, Resource))
			{
				if (SRV)
				{
					SRV->Release();
				}
				SRV = nullptr;
				bChanged = true;
			}
		}

		if (bChanged)
		{
			DeviceContext->PSSetShaderResources(0, static_cast<uint32>(BoundSRVs.size()), BoundSRVs.data());
		}

		ReleaseViewArray(BoundSRVs.data(), static_cast<uint32>(BoundSRVs.size()));
	}

	{
		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> BoundRTVs = {};
		ID3D11DepthStencilView* BoundDSV = nullptr;
		DeviceContext->OMGetRenderTargets(static_cast<uint32>(BoundRTVs.size()), BoundRTVs.data(), &BoundDSV);

		bool bChanged = false;
		for (ID3D11RenderTargetView*& RTV : BoundRTVs)
		{
			if (DoesViewReferenceResource(RTV, Resource))
			{
				if (RTV)
				{
					RTV->Release();
				}
				RTV = nullptr;
				bChanged = true;
			}
		}

		if (DoesViewReferenceResource(BoundDSV, Resource))
		{
			if (BoundDSV)
			{
				BoundDSV->Release();
			}
			BoundDSV = nullptr;
			bChanged = true;
		}

		if (bChanged)
		{
			DeviceContext->OMSetRenderTargets(static_cast<uint32>(BoundRTVs.size()), BoundRTVs.data(), BoundDSV);
		}

		ReleaseViewArray(BoundRTVs.data(), static_cast<uint32>(BoundRTVs.size()));
		if (BoundDSV)
		{
			BoundDSV->Release();
		}
	}
}

void FRenderStateManager::UnbindResourceFromCompute(ID3D11Resource* Resource)
{
	if (!Resource)
	{
		return;
	}

	{
		std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> BoundSRVs = {};
		DeviceContext->CSGetShaderResources(0, static_cast<uint32>(BoundSRVs.size()), BoundSRVs.data());

		bool bChanged = false;
		for (ID3D11ShaderResourceView*& SRV : BoundSRVs)
		{
			if (DoesViewReferenceResource(SRV, Resource))
			{
				if (SRV)
				{
					SRV->Release();
				}
				SRV = nullptr;
				bChanged = true;
			}
		}

		if (bChanged)
		{
			DeviceContext->CSSetShaderResources(0, static_cast<uint32>(BoundSRVs.size()), BoundSRVs.data());
		}

		ReleaseViewArray(BoundSRVs.data(), static_cast<uint32>(BoundSRVs.size()));
	}

	{
		std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> BoundUAVs = {};
		DeviceContext->CSGetUnorderedAccessViews(0, static_cast<uint32>(BoundUAVs.size()), BoundUAVs.data());

		bool bChanged = false;
		for (ID3D11UnorderedAccessView*& UAV : BoundUAVs)
		{
			if (DoesViewReferenceResource(UAV, Resource))
			{
				if (UAV)
				{
					UAV->Release();
				}
				UAV = nullptr;
				bChanged = true;
			}
		}

		if (bChanged)
		{
			std::array<uint32, D3D11_PS_CS_UAV_REGISTER_COUNT> InitialCounts = {};
			DeviceContext->CSSetUnorderedAccessViews(
				0,
				static_cast<uint32>(BoundUAVs.size()),
				BoundUAVs.data(),
				InitialCounts.data());
		}

		ReleaseViewArray(BoundUAVs.data(), static_cast<uint32>(BoundUAVs.size()));
	}
}

void FRenderStateManager::UnbindResourceEverywhere(ID3D11Resource* Resource)
{
	UnbindResourceFromGraphics(Resource);
	UnbindResourceFromCompute(Resource);
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
