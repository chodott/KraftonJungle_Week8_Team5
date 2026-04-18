#include "Renderer/Features/Lighting/LightRenderFeature.h"

#include "Renderer/Renderer.h"
#include "Core/Paths.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Resources/Shader/ShaderResource.h"

FLightRenderFeature::~FLightRenderFeature()
{
	Release();
}

bool FLightRenderFeature::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	std::shared_ptr<FShaderResource> VSResource;
	std::shared_ptr<FShaderResource> PSResource;

	const std::wstring ShaderDir = FPaths::ShaderDir().wstring();

	CompileShaderVariants(Renderer);

	if (!LightConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth = sizeof(FLightConstantBuffer);
		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &LightConstantBuffer)))
		{
			return false;
		}
	}

	if (!LightBlendState)
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
		if (FAILED(Device->CreateBlendState(&BlendDesc, &LightBlendState)))
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

	if (!LightRasterizerState)
	{
		D3D11_RASTERIZER_DESC RasterDesc = {};
		RasterDesc.FillMode = D3D11_FILL_SOLID;
		RasterDesc.CullMode = D3D11_CULL_NONE;
		RasterDesc.DepthClipEnable = TRUE;
		if (FAILED(Device->CreateRasterizerState(&RasterDesc, &LightRasterizerState)))
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

	return true;
}

void FLightRenderFeature::UpdateLightConstantBuffer(FRenderer& Renderer, const FViewContext& View)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!LightConstantBuffer || !DeviceContext)
	{
		return;
	}

	FLightConstantBuffer CBData = {};

	// Test용 하드코딩 -> 이후 Component 나오면 거기서 데이터 받아오기 
	// ── Ambient: 약한 전체 밝기 ──
	CBData.Ambient.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	CBData.Ambient.Intensity = 0.8f;

	// ── Directional: 태양광 (위에서 약간 앞쪽으로) ──
	CBData.Directional.Color = FVector4(1.0f, 0.95f, 0.8f, 1.0f); // 따뜻한 흰색
	CBData.Directional.Direction = FVector(0.0f, -1.0f, 1.0f);        // 위→아래, 뒤→앞
	CBData.Directional.Intensity = 1.0f;

	// ── Point Light 0: 붉은 포인트 라이트 ──
	CBData.PointLights[0].Color = FVector4(1.0f, 0.2f, 0.2f, 1.0f);
	CBData.PointLights[0].Position = FVector(300.0f, 200.0f, 0.0f);
	CBData.PointLights[0].Intensity = 2.0f;
	CBData.PointLights[0].Range = 500.0f;

	// ── Point Light 1: 푸른 포인트 라이트 ──
	CBData.PointLights[1].Color = FVector4(0.2f, 0.4f, 1.0f, 1.0f);
	CBData.PointLights[1].Position = FVector(-300.0f, 200.0f, 0.0f);
	CBData.PointLights[1].Intensity = 2.0f;
	CBData.PointLights[1].Range = 500.0f;

	// ── Point Light 2, 3: 비활성화 ──
	CBData.PointLights[2].Intensity = 0.0f;
	CBData.PointLights[3].Intensity = 0.0f;

	// ── Spot Light 0, 1, 2, 3: 비활성화 ──
	CBData.SpotLights[0].Intensity = 0.0f;
	CBData.SpotLights[1].Intensity = 0.0f;
	CBData.SpotLights[2].Intensity = 0.0f;
	CBData.SpotLights[3].Intensity = 0.0f;

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DeviceContext->Map(LightConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(LightConstantBuffer, 0);
	}
}

bool FLightRenderFeature::CompileShaderVariants(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	const std::wstring VS = FPaths::ShaderDir().wstring() + L"SceneLighting/UberLitVertexShader.hlsl";
	const wchar_t* VSPath = VS.c_str();

	const std::wstring PS = FPaths::ShaderDir().wstring() + L"SceneLighting/UberLitPixelShader.hlsl";
	const wchar_t* PSPath = PS.c_str();

	D3D_SHADER_MACRO GouraudMacros[] = { { "LIGHTING_MODEL_GOURAUD", "1" }, { nullptr, nullptr } };
	D3D_SHADER_MACRO LambertMacros[] = { { "LIGHTING_MODEL_LAMBERT", "1" }, { nullptr, nullptr } };
	D3D_SHADER_MACRO PhongMacros[] = { { "LIGHTING_MODEL_PHONG",   "1" }, { nullptr, nullptr } };

	// ── Gouraud ──
	if (!GouraudVS)
	{
		auto Resource = FShaderResource::GetOrCompile(VSPath, "main", "vs_5_0", GouraudMacros);
		if (!Resource) return false;

		// InputLayout은 VS 바이트코드가 필요하므로 여기서 생성
		if (!LightInputLayout)
		{
			const D3D11_INPUT_ELEMENT_DESC InputDesc[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};
			if (FAILED(Device->CreateInputLayout(
				InputDesc, _countof(InputDesc),
				Resource->GetBufferPointer(), Resource->GetBufferSize(),
				&LightInputLayout)))
			{
				return false;
			}
		}

		if (FAILED(Device->CreateVertexShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &GouraudVS)))
		{
			return false;
		}
	}
	if (!GouraudPS)
	{
		auto Resource = FShaderResource::GetOrCompile(PSPath, "main", "ps_5_0", GouraudMacros);
		if (!Resource) return false;
		if (FAILED(Device->CreatePixelShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &GouraudPS)))
		{
			return false;
		}
	}

	// ── Lambert ──
	if (!LambertVS)
	{
		auto Resource = FShaderResource::GetOrCompile(VSPath, "main", "vs_5_0", LambertMacros);
		if (!Resource) return false;
		if (FAILED(Device->CreateVertexShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &LambertVS)))
		{
			return false;
		}
	}
	if (!LambertPS)
	{
		auto Resource = FShaderResource::GetOrCompile(PSPath, "main", "ps_5_0", LambertMacros);
		if (!Resource) return false;
		if (FAILED(Device->CreatePixelShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &LambertPS)))
		{
			return false;
		}
	}

	// ── Phong ──
	if (!PhongVS)
	{
		auto Resource = FShaderResource::GetOrCompile(VSPath, "main", "vs_5_0", PhongMacros);
		if (!Resource) return false;
		if (FAILED(Device->CreateVertexShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &PhongVS)))
		{
			return false;
		}
	}
	if (!PhongPS)
	{
		auto Resource = FShaderResource::GetOrCompile(PSPath, "main", "ps_5_0", PhongMacros);
		if (!Resource) return false;
		if (FAILED(Device->CreatePixelShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &PhongPS)))
		{
			return false;
		}
	}

	return true;
}

bool FLightRenderFeature::Render(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	const FSceneRenderTargets& Targets)
{
	if (!Targets.SceneColorRTV || !Targets.SceneDepthSRV || !Initialize(Renderer))
	{
		return true;
	}

	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	UpdateLightConstantBuffer(Renderer, View);

	DeviceContext->VSSetConstantBuffers(4, 1, &LightConstantBuffer);
	DeviceContext->PSSetConstantBuffers(4, 1, &LightConstantBuffer);

	DeviceContext->IASetInputLayout(LightInputLayout);

	switch (CurrentLightingModel)
	{
	case ELightingModel::Gouraud:
		DeviceContext->VSSetShader(GouraudVS, nullptr, 0);
		DeviceContext->PSSetShader(GouraudPS, nullptr, 0);
		break;
	case ELightingModel::Lambert:
		DeviceContext->VSSetShader(LambertVS, nullptr, 0);
		DeviceContext->PSSetShader(LambertPS, nullptr, 0);
		break;
	case ELightingModel::Phong:
	default:
		DeviceContext->VSSetShader(PhongVS, nullptr, 0);
		DeviceContext->PSSetShader(PhongPS, nullptr, 0);
		break;
	}

	DeviceContext->PSSetShaderResources(0, 1, &Targets.SceneDepthSRV);
	DeviceContext->PSSetSamplers(0, 1, &DepthSampler);

	return true;
}

void FLightRenderFeature::Release()
{
	if (GouraudVS) { GouraudVS->Release();        GouraudVS = nullptr; }
	if (GouraudPS) { GouraudPS->Release();        GouraudPS = nullptr; }
	if (LambertVS) { LambertVS->Release();        LambertVS = nullptr; }
	if (LambertPS) { LambertPS->Release();        LambertPS = nullptr; }
	if (PhongVS) { PhongVS->Release();          PhongVS = nullptr; }
	if (PhongPS) { PhongPS->Release();          PhongPS = nullptr; }
	if (LightInputLayout) { LightInputLayout->Release(); LightInputLayout = nullptr; }
	if (LightConstantBuffer) { LightConstantBuffer->Release();  LightConstantBuffer = nullptr; }
	if (LightBlendState) { LightBlendState->Release();      LightBlendState = nullptr; }
	if (NoDepthState) { NoDepthState->Release();         NoDepthState = nullptr; }
	if (LightRasterizerState) { LightRasterizerState->Release(); LightRasterizerState = nullptr; }
	if (DepthSampler) { DepthSampler->Release();         DepthSampler = nullptr; }
}


