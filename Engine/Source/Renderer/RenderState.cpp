#include "RenderState.h"

std::shared_ptr<FRasterizerState> FRasterizerState::Create(
	ID3D11Device* InDevice,
	const FRasterizerStateOption& InOption)
{
	if (!InDevice)
	{
		return nullptr;
	}

	std::shared_ptr<FRasterizerState> RS(new FRasterizerState());

	D3D11_RASTERIZER_DESC Desc = {};
	Desc.FillMode = InOption.FillMode;
	Desc.CullMode = InOption.CullMode;
	Desc.DepthClipEnable = InOption.DepthClipEnable;
	Desc.DepthBias = InOption.DepthBias;
	Desc.FrontCounterClockwise = false;
	Desc.ScissorEnable = false;
	Desc.MultisampleEnable = false;
	Desc.AntialiasedLineEnable = false;

	if (FAILED(InDevice->CreateRasterizerState(&Desc, RS->State.GetAddressOf())))
	{
		return nullptr;
	}

	return RS;
}

void FRasterizerState::Bind(ID3D11DeviceContext* InDeviceContext) const
{
	InDeviceContext->RSSetState(State.Get());
}

std::shared_ptr<FDepthStencilState> FDepthStencilState::Create(
	ID3D11Device* InDevice,
	const FDepthStencilStateOption& InOption)
{
	if (!InDevice)
	{
		return nullptr;
	}

	std::shared_ptr<FDepthStencilState> DSS(new FDepthStencilState());

	D3D11_DEPTH_STENCIL_DESC Desc = {};
	Desc.DepthEnable = InOption.DepthEnable;
	Desc.DepthWriteMask = InOption.DepthWriteMask;
	Desc.DepthFunc = InOption.DepthFunc;
	Desc.StencilEnable = InOption.StencilEnable;
	Desc.StencilReadMask = InOption.StencilReadMask;
	Desc.StencilWriteMask = InOption.StencilWriteMask;
	Desc.FrontFace.StencilFailOp = InOption.StencilWriteMask ? D3D11_STENCIL_OP_REPLACE : D3D11_STENCIL_OP_KEEP;
	Desc.FrontFace.StencilDepthFailOp = InOption.StencilWriteMask ? D3D11_STENCIL_OP_REPLACE : D3D11_STENCIL_OP_KEEP;
	Desc.FrontFace.StencilPassOp = InOption.StencilWriteMask ? D3D11_STENCIL_OP_REPLACE : D3D11_STENCIL_OP_KEEP;
	Desc.FrontFace.StencilFunc =
		(InOption.StencilEnable && InOption.StencilReadMask)
		? D3D11_COMPARISON_NOT_EQUAL
		: D3D11_COMPARISON_ALWAYS;
	Desc.BackFace = Desc.FrontFace;

	if (FAILED(InDevice->CreateDepthStencilState(&Desc, DSS->State.GetAddressOf())))
	{
		return nullptr;
	}

	return DSS;
}

void FDepthStencilState::Bind(ID3D11DeviceContext* InDeviceContext, uint32 StencilRef) const
{
	InDeviceContext->OMSetDepthStencilState(State.Get(), StencilRef);
}

std::shared_ptr<FBlendState> FBlendState::Create(
	ID3D11Device* InDevice,
	const FBlendStateOption& InOption)
{
	if (!InDevice)
	{
		return nullptr;
	}

	std::shared_ptr<FBlendState> BS(new FBlendState());

	D3D11_BLEND_DESC Desc = {};
	Desc.AlphaToCoverageEnable = false;
	Desc.IndependentBlendEnable = false;
	Desc.RenderTarget[0].BlendEnable = InOption.BlendEnable;
	Desc.RenderTarget[0].SrcBlend = InOption.SrcBlend;
	Desc.RenderTarget[0].DestBlend = InOption.DestBlend;
	Desc.RenderTarget[0].BlendOp = InOption.BlendOp;
	Desc.RenderTarget[0].SrcBlendAlpha = InOption.SrcBlendAlpha;
	Desc.RenderTarget[0].DestBlendAlpha = InOption.DestBlendAlpha;
	Desc.RenderTarget[0].BlendOpAlpha = InOption.BlendOpAlpha;
	Desc.RenderTarget[0].RenderTargetWriteMask = InOption.RenderTargetWriteMask;

	if (FAILED(InDevice->CreateBlendState(&Desc, BS->State.GetAddressOf())))
	{
		return nullptr;
	}

	return BS;
}

void FBlendState::Bind(ID3D11DeviceContext* InDeviceContext, const float* BlendFactor, uint32 SampleMask) const
{
	static constexpr float DefaultBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	InDeviceContext->OMSetBlendState(State.Get(), BlendFactor ? BlendFactor : DefaultBlendFactor, SampleMask);
}
