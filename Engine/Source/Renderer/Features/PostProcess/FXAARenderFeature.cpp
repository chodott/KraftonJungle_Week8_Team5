#include "Renderer/Features/PostProcess/FXAARenderFeature.h"

#include "Core/Paths.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/Shader/ShaderRegistry.h"

namespace
{
	struct FFXAAConstantBuffer
	{
		float InvScreenWidth  = 0.0f;
		float InvScreenHeight = 0.0f;
		float Pad0            = 0.0f;
		float Pad1            = 0.0f;
	};
}

FFXAARenderFeature::~FFXAARenderFeature()
{
	Release();
}

bool FFXAARenderFeature::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (!FXAAConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage          = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth      = sizeof(FFXAAConstantBuffer);
		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &FXAAConstantBuffer)))
		{
			return false;
		}
	}

	if (!NoDepthState)
	{
		D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
		DepthDesc.DepthEnable    = FALSE;
		DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		DepthDesc.DepthFunc      = D3D11_COMPARISON_ALWAYS;
		if (FAILED(Device->CreateDepthStencilState(&DepthDesc, &NoDepthState)))
		{
			return false;
		}
	}

	if (!RasterizerState)
	{
		D3D11_RASTERIZER_DESC RasterDesc = {};
		RasterDesc.FillMode      = D3D11_FILL_SOLID;
		RasterDesc.CullMode      = D3D11_CULL_NONE;
		RasterDesc.DepthClipEnable = TRUE;
		if (FAILED(Device->CreateRasterizerState(&RasterDesc, &RasterizerState)))
		{
			return false;
		}
	}

	if (!LinearSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		SamplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		SamplerDesc.MinLOD         = 0.0f;
		SamplerDesc.MaxLOD         = D3D11_FLOAT32_MAX;
		if (FAILED(Device->CreateSamplerState(&SamplerDesc, &LinearSampler)))
		{
			return false;
		}
	}

	const std::wstring ShaderDir = FPaths::ShaderDir().wstring();

	if (!FullscreenVS)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage = EShaderStage::Vertex;
		Recipe.SourcePath = ShaderDir + L"FinalImagePostProcess/BlitVertexShader.hlsl";
		Recipe.EntryPoint = "main";
		Recipe.Target = "vs_5_0";
		Recipe.LayoutType = EVertexLayoutType::FullscreenNone;
		FullscreenVS = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(Device, Recipe);
	}

	if (!FXAAPS)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage = EShaderStage::Pixel;
		Recipe.SourcePath = ShaderDir + L"FinalImagePostProcess/FXAAPixelShader.hlsl";
		Recipe.EntryPoint = "main";
		Recipe.Target = "ps_5_0";
		FXAAPS = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(Device, Recipe);
	}

	return FullscreenVS != nullptr && FXAAPS != nullptr;
}

void FFXAARenderFeature::UpdateConstantBuffer(FRenderer& Renderer, const FViewContext& View)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!FXAAConstantBuffer || !DeviceContext)
	{
		return;
	}

	FFXAAConstantBuffer CBData = {};
	CBData.InvScreenWidth  = (View.Viewport.Width  > 0.0f) ? 1.0f / View.Viewport.Width  : 0.0f;
	CBData.InvScreenHeight = (View.Viewport.Height > 0.0f) ? 1.0f / View.Viewport.Height : 0.0f;

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DeviceContext->Map(FXAAConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(FXAAConstantBuffer, 0);
	}
}

bool FFXAARenderFeature::Render(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	FSceneRenderTargets& Targets)
{
	if (!Targets.GetSceneColorShaderResource() || !Targets.GetSceneColorWriteRenderTarget())
	{
		return true;
	}

	if (!Initialize(Renderer))
	{
		return false;
	}

	UpdateConstantBuffer(Renderer, View);

	// 블렌드 없음 — 픽셀 셰이더 출력이 그대로 Scratch에 쓰인다
	FFullscreenPassPipelineState PipelineState;
	PipelineState.BlendState        = nullptr;
	PipelineState.DepthStencilState = NoDepthState;
	PipelineState.RasterizerState   = RasterizerState;

	const FFullscreenPassConstantBufferBinding ConstantBuffers[] =
	{
		{ 0, FXAAConstantBuffer },
	};
	const FFullscreenPassShaderResourceBinding ShaderResources[] =
	{
		{ 0, Targets.GetSceneColorShaderResource() },
	};
	const FFullscreenPassSamplerBinding Samplers[] =
	{
		{ 0, LinearSampler },
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

	const bool bOk = ExecuteFullscreenPass(
		Renderer,
		Frame,
		View,
		Targets.GetSceneColorWriteRenderTarget(),
		nullptr,
		View.Viewport,
		{ FullscreenVS, FXAAPS },
		PipelineState,
		Bindings,
		[](ID3D11DeviceContext& Context)
		{
			Context.Draw(3, 0);
		});

	if (!bOk)
	{
		return false;
	}

	Targets.SwapSceneColor();
	return true;
}

void FFXAARenderFeature::Release()
{
	if (FXAAConstantBuffer)  { FXAAConstantBuffer->Release();  FXAAConstantBuffer  = nullptr; }
	if (NoDepthState)        { NoDepthState->Release();        NoDepthState        = nullptr; }
	if (RasterizerState)     { RasterizerState->Release();     RasterizerState     = nullptr; }
	if (LinearSampler)       { LinearSampler->Release();       LinearSampler       = nullptr; }
	FullscreenVS.reset();
	FXAAPS.reset();
}
