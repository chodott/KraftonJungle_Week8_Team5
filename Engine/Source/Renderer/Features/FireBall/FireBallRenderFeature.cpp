#include "Renderer/Features/FireBall/FireBallRenderFeature.h"

#include "Renderer/Renderer.h"
#include "Core/Paths.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Resources/Shader/ShaderResource.h"

namespace
{
	struct FFireBallConstantBuffer
	{
		FMatrix InverseViewProjection = FMatrix::Identity;
		FVector4 Color = FLinearColor::White.ToVector4();
		FVector4 FireballOrigin = FVector4(FVector::Zero(), 1.0f);
		float Intensity = 1.0f;
		float Radius = 1.0f;
		float RadiusFalloff = 1.0f;
	};
}

FFireBallRenderFeature::~FFireBallRenderFeature()
{
	Release();
}

bool FFireBallRenderFeature::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (!FireBallConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth = sizeof(FFireBallConstantBuffer);
		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &FireBallConstantBuffer)))
		{
			return false;
		}
	}

	if (!FireBallBlendState)
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
		if (FAILED(Device->CreateBlendState(&BlendDesc, &FireBallBlendState)))
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

	if (!FireBallRasterizerState)
	{
		D3D11_RASTERIZER_DESC RasterDesc = {};
		RasterDesc.FillMode = D3D11_FILL_SOLID;
		RasterDesc.CullMode = D3D11_CULL_NONE;
		RasterDesc.DepthClipEnable = TRUE;
		if (FAILED(Device->CreateRasterizerState(&RasterDesc, &FireBallRasterizerState)))
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
	if (!FireBallPostVS)
	{
		auto Resource = FShaderResource::GetOrCompile((ShaderDir + L"FinalImagePostProcess/BlitVertexShader.hlsl").c_str(), "main", "vs_5_0");
		if (!Resource || FAILED(Device->CreateVertexShader(Resource->GetBufferPointer(), Resource->GetBufferSize(), nullptr, &FireBallPostVS)))
		{
			return false;
		}
	}

	if (!FireBallPostPS)
	{
		auto Resource = FShaderResource::GetOrCompile((ShaderDir + L"SceneEffects/FireBallPixelShader.hlsl").c_str(), "main", "ps_5_0");
		if (!Resource || FAILED(Device->CreatePixelShader(Resource->GetBufferPointer(), Resource->GetBufferSize(), nullptr, &FireBallPostPS)))
		{
			return false;
		}
	}

	return true;
}


void FFireBallRenderFeature::UpdateFireBallConstantBuffer(FRenderer& Renderer, const FViewContext& View, const FFireBallRenderItem& Item)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!FireBallConstantBuffer || !DeviceContext)
	{
		return;
	}

	FFireBallConstantBuffer CBData = {};
	CBData.InverseViewProjection = View.InverseViewProjection.GetTransposed();
	CBData.FireballOrigin = FVector4(Item.FireballOrigin.X, Item.FireballOrigin.Y, Item.FireballOrigin.Z, 0.0f);
	CBData.Color = Item.Color.ToVector4();
	CBData.Intensity = Item.Intensity;
	CBData.RadiusFalloff = Item.RadiusFallOff;
	CBData.Radius = Item.Radius;
	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DeviceContext->Map(FireBallConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(FireBallConstantBuffer, 0);
	}
}

bool FFireBallRenderFeature::Render(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	const FSceneRenderTargets& Targets,
	const TArray<FFireBallRenderItem>& Items)
{
	if (Items.empty() || !Targets.SceneColorRTV || !Targets.SceneDepthSRV || !Initialize(Renderer))
	{
		return true;
	}

	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	constexpr float BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	FFullscreenPassPipelineState PipelineState;
	PipelineState.BlendState = FireBallBlendState;
	PipelineState.BlendFactor = BlendFactor;
	PipelineState.DepthStencilState = NoDepthState;
	PipelineState.RasterizerState = FireBallRasterizerState;

	const FFullscreenPassConstantBufferBinding ConstantBuffers[] =
	{
		{ 0, FireBallConstantBuffer },
	};
	const FFullscreenPassShaderResourceBinding ShaderResources[] =
	{
		{ 0, Targets.SceneDepthSRV },
	};
	const FFullscreenPassSamplerBinding Samplers[] =
	{
		{ 0, DepthSampler },
	};
	const FFullscreenPassBindings Bindings
	{
		ConstantBuffers,
		static_cast<uint32>(sizeof(ConstantBuffers) / sizeof(ConstantBuffers[0])),
		ShaderResources,
		static_cast<uint32>(sizeof(ShaderResources) / sizeof(ShaderResources[0])),
		Samplers,
		static_cast<uint32>(sizeof(Samplers) / sizeof(Samplers[0]))
	};

	return ExecuteFullscreenPass(
		Renderer,
		Frame,
		View,
		Targets.SceneColorRTV,
		nullptr,
		View.Viewport,
		{ FireBallPostVS, FireBallPostPS },
		PipelineState,
		Bindings,
		[&](ID3D11DeviceContext& Context)
		{
			for (const FFireBallRenderItem& Item : Items)
			{
				UpdateFireBallConstantBuffer(Renderer, View, Item);
				Context.Draw(3, 0);
			}
		});
}

void FFireBallRenderFeature::Release()
{
	if (FireBallConstantBuffer)
	{
		FireBallConstantBuffer->Release();
		FireBallConstantBuffer = nullptr;
	}
	if (FireBallBlendState)
	{
		FireBallBlendState->Release();
		FireBallBlendState = nullptr;
	}
	if (NoDepthState)
	{
		NoDepthState->Release();
		NoDepthState = nullptr;
	}
	if (FireBallRasterizerState)
	{
		FireBallRasterizerState->Release();
		FireBallRasterizerState = nullptr;
	}
	if (DepthSampler)
	{
		DepthSampler->Release();
		DepthSampler = nullptr;
	}
	if (FireBallPostVS)
	{
		FireBallPostVS->Release();
		FireBallPostVS = nullptr;
	}
	if (FireBallPostPS)
	{
		FireBallPostPS->Release();
		FireBallPostPS = nullptr;
	}
}