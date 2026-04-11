#include "Renderer/Feature/FogRenderFeature.h"

#include "Core/Paths.h"
#include "Renderer/Renderer.h"
#include "Renderer/ShaderResource.h"

namespace
{
	struct FFogConstantBuffer
	{
		FMatrix InverseViewProjection = FMatrix::Identity;
		FVector4 CameraPosition = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		FVector4 FogOrigin = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		FVector4 FogColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		FVector4 FogParams = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		FVector4 FogParams2 = FVector4(1.0f, 1.0f, 0.0f, 0.0f);
	};
}

FFogRenderFeature::~FFogRenderFeature()
{
	Release();
}

bool FFogRenderFeature::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (!FogConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth = sizeof(FFogConstantBuffer);
		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &FogConstantBuffer)))
		{
			return false;
		}
	}

	if (!FogBlendState)
	{
		D3D11_BLEND_DESC BlendDesc = {};
		BlendDesc.RenderTarget[0].BlendEnable = TRUE;
		BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(Device->CreateBlendState(&BlendDesc, &FogBlendState)))
		{
			return false;
		}
	}

	if (!NoDepthState)
	{
		D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
		DepthDesc.DepthEnable = FALSE;
		DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		DepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		if (FAILED(Device->CreateDepthStencilState(&DepthDesc, &NoDepthState)))
		{
			return false;
		}
	}

	if (!FogRasterizerState)
	{
		D3D11_RASTERIZER_DESC RasterDesc = {};
		RasterDesc.FillMode = D3D11_FILL_SOLID;
		RasterDesc.CullMode = D3D11_CULL_NONE;
		RasterDesc.DepthClipEnable = TRUE;
		if (FAILED(Device->CreateRasterizerState(&RasterDesc, &FogRasterizerState)))
		{
			return false;
		}
	}

	if (!DepthSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		SamplerDesc.MinLOD = 0.0f;
		SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(Device->CreateSamplerState(&SamplerDesc, &DepthSampler)))
		{
			return false;
		}
	}

	const std::wstring ShaderDir = FPaths::ShaderDir().wstring();
	if (!FogPostVS)
	{
		auto Resource = FShaderResource::GetOrCompile((ShaderDir + L"BlitVertexShader.hlsl").c_str(), "main", "vs_5_0");
		if (!Resource || FAILED(Device->CreateVertexShader(Resource->GetBufferPointer(), Resource->GetBufferSize(), nullptr, &FogPostVS)))
		{
			return false;
		}
	}

	if (!FogPostPS)
	{
		auto Resource = FShaderResource::GetOrCompile((ShaderDir + L"FogPostPixelShader.hlsl").c_str(), "main", "ps_5_0");
		if (!Resource || FAILED(Device->CreatePixelShader(Resource->GetBufferPointer(), Resource->GetBufferSize(), nullptr, &FogPostPS)))
		{
			return false;
		}
	}

	return true;
}

void FFogRenderFeature::UpdateFogConstantBuffer(FRenderer& Renderer, const FFogRenderRequest& Request, const FFogRenderItem& Item)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!FogConstantBuffer || !DeviceContext)
	{
		return;
	}

	FFogConstantBuffer CBData = {};
	CBData.InverseViewProjection = Request.InverseViewProjection.GetTransposed();
	CBData.CameraPosition = FVector4(Request.CameraPosition.X, Request.CameraPosition.Y, Request.CameraPosition.Z, 0.0f);
	CBData.FogOrigin = FVector4(Item.FogOrigin.X, Item.FogOrigin.Y, Item.FogOrigin.Z, 0.0f);
	CBData.FogColor = Item.FogInscatteringColor.ToVector4();
	CBData.FogParams = FVector4(
		Item.FogDensity,
		Item.FogHeightFalloff,
		Item.StartDistance,
		Item.FogCutoffDistance);
	CBData.FogParams2 = FVector4(Item.FogMaxOpacity, Item.AllowBackground, 0.0f, 0.0f);

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DeviceContext->Map(FogConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(FogConstantBuffer, 0);
	}

	ID3D11Buffer* Buffer = FogConstantBuffer;
	DeviceContext->PSSetConstantBuffers(0, 1, &Buffer);
}

bool FFogRenderFeature::Render(FRenderer& Renderer, const FFogRenderRequest& Request)
{
	if (Request.IsEmpty() || !Initialize(Renderer))
	{
		return true;
	}

	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	ID3D11RenderTargetView* BoundRTV = nullptr;
	ID3D11DepthStencilView* BoundDSV = nullptr;
	DeviceContext->OMGetRenderTargets(1, &BoundRTV, &BoundDSV);
	if (!BoundRTV)
	{
		if (BoundDSV)
		{
			BoundDSV->Release();
		}
		return false;
	}

	constexpr float BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	ID3D11ShaderResourceView* DepthSRV = Request.DepthTextureSRV;
	ID3D11ShaderResourceView* NullSRV = nullptr;
	ID3D11Buffer* NullCB = nullptr;

	DeviceContext->OMSetRenderTargets(1, &BoundRTV, nullptr);
	DeviceContext->OMSetBlendState(FogBlendState, BlendFactor, 0xFFFFFFFF);
	DeviceContext->OMSetDepthStencilState(NoDepthState, 0);
	DeviceContext->RSSetState(FogRasterizerState);
	DeviceContext->IASetInputLayout(nullptr);
	DeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DeviceContext->VSSetShader(FogPostVS, nullptr, 0);
	DeviceContext->PSSetShader(FogPostPS, nullptr, 0);
	DeviceContext->PSSetSamplers(0, 1, &DepthSampler);
	DeviceContext->PSSetShaderResources(0, 1, &DepthSRV);

	for (const FFogRenderItem& Item : Request.Items)
	{
		UpdateFogConstantBuffer(Renderer, Request, Item);
		DeviceContext->Draw(6, 0);
	}

	DeviceContext->PSSetShaderResources(0, 1, &NullSRV);
	DeviceContext->PSSetConstantBuffers(0, 1, &NullCB);
	DeviceContext->OMSetBlendState(nullptr, BlendFactor, 0xFFFFFFFF);
	DeviceContext->OMSetDepthStencilState(nullptr, 0);
	DeviceContext->RSSetState(nullptr);
	DeviceContext->OMSetRenderTargets(1, &BoundRTV, BoundDSV);

	Renderer.ShaderManager.Bind(DeviceContext);
	Renderer.SetConstantBuffers();
	Renderer.GetRenderStateManager()->RebindState();

	BoundRTV->Release();
	if (BoundDSV)
	{
		BoundDSV->Release();
	}
	return true;
}

void FFogRenderFeature::Release()
{
	if (FogConstantBuffer)
	{
		FogConstantBuffer->Release();
		FogConstantBuffer = nullptr;
	}
	if (FogBlendState)
	{
		FogBlendState->Release();
		FogBlendState = nullptr;
	}
	if (NoDepthState)
	{
		NoDepthState->Release();
		NoDepthState = nullptr;
	}
	if (FogRasterizerState)
	{
		FogRasterizerState->Release();
		FogRasterizerState = nullptr;
	}
	if (DepthSampler)
	{
		DepthSampler->Release();
		DepthSampler = nullptr;
	}
	if (FogPostVS)
	{
		FogPostVS->Release();
		FogPostVS = nullptr;
	}
	if (FogPostPS)
	{
		FogPostPS->Release();
		FogPostPS = nullptr;
	}
}
