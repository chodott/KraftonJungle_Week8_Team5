#include "Renderer/Frame/Viewport/ViewportCompositor.h"

#include "Core/Paths.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/Shader/ShaderRegistry.h"

FViewportCompositor::~FViewportCompositor()
{
	Release();
}

bool FViewportCompositor::Initialize(ID3D11Device* Device)
{
	if (bInitialized)
	{
		return true;
	}

	if (!Device)
	{
		return false;
	}

	const std::wstring ShaderDir = FPaths::ShaderDir().wstring();

	{
		FShaderRecipe Recipe = {};
		Recipe.Stage = EShaderStage::Vertex;
		Recipe.SourcePath = ShaderDir + L"FinalImagePostProcess/BlitVertexShader.hlsl";
		Recipe.EntryPoint = "main";
		Recipe.Target = "vs_5_0";
		Recipe.LayoutType = EVertexLayoutType::FullscreenNone;
		BlitVertexShader = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(Device, Recipe);
	}

	if (!BlitVertexShader)
	{
		Release();
		return false;
	}

	{
		FShaderRecipe Recipe = {};
		Recipe.Stage = EShaderStage::Pixel;
		Recipe.SourcePath = ShaderDir + L"FinalImagePostProcess/BlitPixelShader.hlsl";
		Recipe.EntryPoint = "main";
		Recipe.Target = "ps_5_0";
		BlitPixelShader = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(Device, Recipe);
	}

	if (!BlitPixelShader)
	{
		Release();
		return false;
	}

	{
		FShaderRecipe Recipe = {};
		Recipe.Stage = EShaderStage::Pixel;
		Recipe.SourcePath = ShaderDir + L"FinalImagePostProcess/DepthViewPixelShader.hlsl";
		Recipe.EntryPoint = "main";
		Recipe.Target = "ps_5_0";
		DepthViewPixelShader = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(Device, Recipe);
	}

	if (!DepthViewPixelShader)
	{
		Release();
		return false;
	}

	D3D11_SAMPLER_DESC SamplerDesc = {};
	SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SamplerDesc.MinLOD = 0.0f;
	SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(Device->CreateSamplerState(&SamplerDesc, &PointSampler)))
	{
		Release();
		return false;
	}

	D3D11_BLEND_DESC AlphaBlendDesc = {};
	AlphaBlendDesc.RenderTarget[0].BlendEnable = TRUE;
	AlphaBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	AlphaBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	AlphaBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	AlphaBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	AlphaBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	AlphaBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	AlphaBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(Device->CreateBlendState(&AlphaBlendDesc, &AlphaBlendState)))
	{
		Release();
		return false;
	}

	D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
	DepthDesc.DepthEnable = FALSE;
	DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	DepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	if (FAILED(Device->CreateDepthStencilState(&DepthDesc, &NoDepthState)))
	{
		Release();
		return false;
	}

	D3D11_RASTERIZER_DESC RasterizerDesc = {};
	RasterizerDesc.FillMode = D3D11_FILL_SOLID;
	RasterizerDesc.CullMode = D3D11_CULL_NONE;
	RasterizerDesc.ScissorEnable = TRUE;
	RasterizerDesc.DepthClipEnable = TRUE;
	if (FAILED(Device->CreateRasterizerState(&RasterizerDesc, &ScissorRasterizerState)))
	{
		Release();
		return false;
	}

	D3D11_BUFFER_DESC CBDesc = {};
	CBDesc.ByteWidth = sizeof(FViewportVisualizationParams);
	CBDesc.Usage = D3D11_USAGE_DYNAMIC;
	CBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	CBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	if (FAILED(Device->CreateBuffer(&CBDesc, nullptr, &VisualizationConstantBuffer)))
	{
		Release();
		return false;
	}

	bInitialized = true;
	return true;
}

void FViewportCompositor::Release()
{
	BlitVertexShader.reset();
	BlitPixelShader.reset();
	if (PointSampler)
	{
		PointSampler->Release();
		PointSampler = nullptr;
	}
	if (AlphaBlendState)
	{
		AlphaBlendState->Release();
		AlphaBlendState = nullptr;
	}
	if (NoDepthState)
	{
		NoDepthState->Release();
		NoDepthState = nullptr;
	}
	if (ScissorRasterizerState)
	{
		ScissorRasterizerState->Release();
		ScissorRasterizerState = nullptr;
	}

	DepthViewPixelShader.reset();

	if (VisualizationConstantBuffer)
	{
		VisualizationConstantBuffer->Release();
		VisualizationConstantBuffer = nullptr;
	}

	bInitialized = false;
}

bool FViewportCompositor::Compose(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView,
	const FViewportCompositePassInputs& Inputs) const
{
	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
	if (!bInitialized || !Context || Inputs.IsEmpty())
	{
		return bInitialized && Context != nullptr;
	}

	FFullscreenPassPipelineState BasePipelineState;
	BasePipelineState.DepthStencilState = NoDepthState;
	BasePipelineState.RasterizerState = ScissorRasterizerState;

	FFullscreenPassPipelineState OverlayPipelineState = BasePipelineState;
	OverlayPipelineState.BlendState = AlphaBlendState;

	for (const FViewportCompositeItem& Item : *Inputs.Items)
	{
		if (!Item.bVisible || !Item.Rect.IsValid())
		{
			continue;
		}

		ID3D11ShaderResourceView* SourceSRV = ResolveSourceSRV(Item);
		const std::shared_ptr<FPixelShaderHandle> PixelShader = ResolvePixelShader(Item);

		if (!SourceSRV || !PixelShader)
		{
			continue;
		}

		D3D11_VIEWPORT Viewport = {};
		Viewport.TopLeftX = static_cast<float>(Item.Rect.X);
		Viewport.TopLeftY = static_cast<float>(Item.Rect.Y);
		Viewport.Width = static_cast<float>(Item.Rect.Width);
		Viewport.Height = static_cast<float>(Item.Rect.Height);
		Viewport.MinDepth = 0.0f;
		Viewport.MaxDepth = 1.0f;
		Context->RSSetViewports(1, &Viewport);

		D3D11_RECT ScissorRect = {};
		ScissorRect.left = Item.Rect.X;
		ScissorRect.top = Item.Rect.Y;
		ScissorRect.right = Item.Rect.X + Item.Rect.Width;
		ScissorRect.bottom = Item.Rect.Y + Item.Rect.Height;

		const FFullscreenPassShaderResourceBinding ShaderResources[] =
		{
			{ 0, SourceSRV },
		};
		const FFullscreenPassSamplerBinding Samplers[] =
		{
			{ 0, PointSampler },
		};

		if (Item.Mode == EViewportCompositeMode::DepthView)
		{
			D3D11_MAPPED_SUBRESOURCE Mapped = {};
			if (FAILED(Context->Map(VisualizationConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
			{
				continue;
			}

			*reinterpret_cast<FViewportVisualizationParams*>(Mapped.pData) = Item.VisualizationParams;
			Context->Unmap(VisualizationConstantBuffer, 0);
		}

		const FFullscreenPassConstantBufferBinding ConstantBuffers[] =
		{
			{ 0, Item.Mode == EViewportCompositeMode::DepthView ? VisualizationConstantBuffer : nullptr },
		};
		const FFullscreenPassBindings Bindings
		{
			Item.Mode == EViewportCompositeMode::DepthView ? ConstantBuffers : nullptr,
			Item.Mode == EViewportCompositeMode::DepthView ? 1u : 0u,
			ShaderResources,
			static_cast<uint32>(sizeof(ShaderResources) / sizeof(ShaderResources[0])),
			Samplers,
			static_cast<uint32>(sizeof(Samplers) / sizeof(Samplers[0]))
		};

		if (!ExecuteFullscreenPass(
			Renderer,
			Frame,
			View,
			RenderTargetView,
			DepthStencilView,
			Viewport,
			{ BlitVertexShader, PixelShader },
			BasePipelineState,
			Bindings,
			[&](ID3D11DeviceContext& DrawContext)
			{
				DrawContext.RSSetScissorRects(1, &ScissorRect);
				DrawContext.Draw(3, 0);
			}))
		{
			return false;
		}

		if (Item.OverlayColorSRV)
		{
			const FFullscreenPassShaderResourceBinding OverlayShaderResources[] =
			{
				{ 0, Item.OverlayColorSRV },
			};
			const FFullscreenPassBindings OverlayBindings
			{
				nullptr,
				0u,
				OverlayShaderResources,
				static_cast<uint32>(sizeof(OverlayShaderResources) / sizeof(OverlayShaderResources[0])),
				Samplers,
				static_cast<uint32>(sizeof(Samplers) / sizeof(Samplers[0]))
			};

			if (!ExecuteFullscreenPass(
				Renderer,
				Frame,
				View,
				RenderTargetView,
				DepthStencilView,
				Viewport,
				{ BlitVertexShader, BlitPixelShader },
				OverlayPipelineState,
				OverlayBindings,
				[&](ID3D11DeviceContext& DrawContext)
				{
					DrawContext.RSSetScissorRects(1, &ScissorRect);
					DrawContext.Draw(3, 0);
				}))
			{
				return false;
			}
		}
	}

	return true;
}


std::shared_ptr<FPixelShaderHandle> FViewportCompositor::ResolvePixelShader(const FViewportCompositeItem& Item) const
{
	switch (Item.Mode)
	{
	case EViewportCompositeMode::DepthView:
		return DepthViewPixelShader;
	case EViewportCompositeMode::SceneColor:
	default:
		return BlitPixelShader;
	}
}

ID3D11ShaderResourceView* FViewportCompositor::ResolveSourceSRV(const FViewportCompositeItem& Item) const
{
	switch (Item.Mode)
	{
	case EViewportCompositeMode::DepthView:
		return Item.SceneDepthSRV;
	case EViewportCompositeMode::SceneColor:
	default:
		return Item.SceneColorSRV;
	}
}
