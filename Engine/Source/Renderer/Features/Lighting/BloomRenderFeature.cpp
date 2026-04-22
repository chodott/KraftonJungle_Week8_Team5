#include "Renderer/Features/Lighting/BloomRenderFeature.h"

#include "Renderer/Renderer.h"
#include "Core/Paths.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Resources/Shader/ShaderRegistry.h"
#include "Renderer/Scene/SceneViewData.h"

namespace
{
	struct FBloomThresholdParams
	{
		float Threshold = 0.3f;
		float Knee = 0.1f;
		float Pad[2] = {};
	};

	struct FBloomBlurParams
	{
		float TexelSize[2] = {};
		float Pad[2] = {};
	};

	struct FBloomCompositeParams
	{
		float BloomIntensity = 3.0f;
		float Exposure = 1.0f;
		float Pad[2] = {};
	};
}

FBloomRenderFeature::~FBloomRenderFeature()
{
	Release();
}

bool FBloomRenderFeature::Render(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	FSceneRenderTargets& Targets)
{
	if(!bApplyBloom)
		return true;

	if (!Targets.SceneColorRTV || !Targets.SceneColorSRV)
		return true;

	if (!Initialize(Renderer, Targets))
		return false;

	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	ID3D11RenderTargetView* nullRTV[] = { nullptr };
	DeviceContext->OMSetRenderTargets(1, nullRTV, nullptr);

	const UINT Width = Targets.Width;
	const UINT Height = Targets.Height;

	// ── Pass 1: Threshold ─────────────────────────────────────────────
	{
		UpdateThresholdConstantBuffer(Renderer);

		ID3D11ShaderResourceView* srvs[] = { Targets.SceneColorSRV };
		ID3D11UnorderedAccessView* uavs[] = { BloomBrightnessUAV };
		ID3D11Buffer* cbs[] = { ThresholdConstantBuffer };

		ThresholdCS->Bind(DeviceContext);
		DeviceContext->CSSetConstantBuffers(0, 1, cbs);
		DeviceContext->CSSetShaderResources(0, 1, srvs);
		DeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		DeviceContext->Dispatch((Width + 7) / 8, (Height + 7) / 8, 1);

		// 언바인딩
		ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
		ID3D11ShaderResourceView* nullSRV[] = { nullptr };
		DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		DeviceContext->CSSetShaderResources(0, 1, nullSRV);
	}

	// ── Pass 2: Blur ──────────────────────────────────────────────────
	{
		UpdateBlurConstantBuffer(Renderer, Width, Height);

		for (int i = 0; i < BlurIterations; ++i)
		{
			bool bPingPong = (i % 2 == 0);

			ID3D11ShaderResourceView* srvs[] = { bPingPong ? BloomBrightnessSRV : BloomScratchSRV };
			ID3D11UnorderedAccessView* uavs[] = { bPingPong ? BloomScratchUAV : BloomBrightnessUAV };
			ID3D11Buffer* cbs[] = { BlurConstantBuffer };

			BlurCS->Bind(DeviceContext);
			DeviceContext->CSSetConstantBuffers(0, 1, cbs);
			DeviceContext->CSSetShaderResources(0, 1, srvs);
			DeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

			DeviceContext->Dispatch((Width + 15) / 16, (Height + 15) / 16, 1);

			ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
			ID3D11ShaderResourceView* nullSRV[] = { nullptr };
			DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
			DeviceContext->CSSetShaderResources(0, 1, nullSRV);
		}
	}

	ID3D11ShaderResourceView* finalBloomSRV =
		(BlurIterations % 2 == 1) ? BloomScratchSRV : BloomBrightnessSRV;

	// ── Pass 3: Composite → SceneColorWrite ────────────────
	{
		UpdateCompositeConstantBuffer(Renderer);

		ID3D11ShaderResourceView* srvs[] = { Targets.SceneColorSRV, finalBloomSRV };
		ID3D11UnorderedAccessView* uavs[] = { Targets.GetSceneColorWriteUAV() };
		ID3D11Buffer* cbs[] = { CompositeConstantBuffer };

		CompositeCS->Bind(DeviceContext);
		DeviceContext->CSSetConstantBuffers(0, 1, cbs);
		DeviceContext->CSSetShaderResources(0, 2, srvs);
		DeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		DeviceContext->Dispatch((Width + 7) / 8, (Height + 7) / 8, 1);

		ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
		ID3D11ShaderResourceView* nullSRV[] = { nullptr, nullptr };
		DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		DeviceContext->CSSetShaderResources(0, 2, nullSRV);
		DeviceContext->CSSetShader(nullptr, nullptr, 0);
	}

	// Composite 결과가 SceneColorWrite에 있으므로 스왑
	Targets.SwapSceneColor();

	return true;

}

void FBloomRenderFeature::Release()
{
	auto SafeRelease = [](auto*& ptr)
		{
			if (ptr) { ptr->Release(); ptr = nullptr; }
		};

	SafeRelease(ThresholdConstantBuffer);
	SafeRelease(BlurConstantBuffer);
	SafeRelease(CompositeConstantBuffer);

	// Bloom 전용 버퍼
	SafeRelease(BloomBrightnessTexture);
	SafeRelease(BloomBrightnessRTV);
	SafeRelease(BloomBrightnessSRV);
	SafeRelease(BloomBrightnessUAV);

	SafeRelease(BloomScratchTexture);
	SafeRelease(BloomScratchSRV);
	SafeRelease(BloomScratchUAV);

	ThresholdCS.reset();
	BlurCS.reset();
	CompositeCS.reset();
}

bool FBloomRenderFeature::Initialize(FRenderer& Renderer, const FSceneRenderTargets& Targets)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	// 해상도 변경 시 버퍼 재생성
	if (BloomBrightnessTexture)
	{
		D3D11_TEXTURE2D_DESC Desc = {};
		BloomBrightnessTexture->GetDesc(&Desc);
		if (Desc.Width != Targets.Width || Desc.Height != Targets.Height)
		{
			// 텍스처만 해제하고 재생성
			auto SafeRelease = [](auto*& ptr) { if (ptr) { ptr->Release(); ptr = nullptr; } };
			SafeRelease(BloomBrightnessTexture);
			SafeRelease(BloomBrightnessRTV);
			SafeRelease(BloomBrightnessSRV);
			SafeRelease(BloomBrightnessUAV);
			SafeRelease(BloomScratchTexture);
			SafeRelease(BloomScratchSRV);
			SafeRelease(BloomScratchUAV);
		}
	}

	// ── Bloom Brightness 버퍼 ─────────────────────────────────────────
	if (!BloomBrightnessTexture)
	{
		D3D11_TEXTURE2D_DESC Desc = {};
		Desc.Width = Targets.Width;
		Desc.Height = Targets.Height;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_RENDER_TARGET
			| D3D11_BIND_SHADER_RESOURCE
			| D3D11_BIND_UNORDERED_ACCESS;

		if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &BloomBrightnessTexture)))
			return false;
		if (FAILED(Device->CreateRenderTargetView(BloomBrightnessTexture, nullptr, &BloomBrightnessRTV)))
			return false;
		if (FAILED(Device->CreateShaderResourceView(BloomBrightnessTexture, nullptr, &BloomBrightnessSRV)))
			return false;
		if (FAILED(Device->CreateUnorderedAccessView(BloomBrightnessTexture, nullptr, &BloomBrightnessUAV)))
			return false;
	}

	// ── Bloom Scratch 버퍼 (Blur 출력용) ─────────────────────────────
	if (!BloomScratchTexture)
	{
		D3D11_TEXTURE2D_DESC Desc = {};
		Desc.Width = Targets.Width;
		Desc.Height = Targets.Height;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &BloomScratchTexture)))
			return false;
		if (FAILED(Device->CreateShaderResourceView(BloomScratchTexture, nullptr, &BloomScratchSRV)))
			return false;
		if (FAILED(Device->CreateUnorderedAccessView(BloomScratchTexture, nullptr, &BloomScratchUAV)))
			return false;
	}

	// ── Constant Buffers ──────────────────────────────────────────────
	auto CreateCB = [&](UINT size, ID3D11Buffer** outBuf) -> bool
		{
			if (*outBuf) return true;
			D3D11_BUFFER_DESC Desc = {};
			Desc.Usage = D3D11_USAGE_DYNAMIC;
			Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			Desc.ByteWidth = size;
			return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, outBuf));
		};

	if (!CreateCB(sizeof(FBloomThresholdParams), &ThresholdConstantBuffer))  return false;
	if (!CreateCB(sizeof(FBloomBlurParams), &BlurConstantBuffer))       return false;
	if (!CreateCB(sizeof(FBloomCompositeParams), &CompositeConstantBuffer))  return false;

	// ── Shaders ───────────────────────────────────────────────────────
	const std::wstring ShaderDir = FPaths::ShaderDir().wstring();

	auto MakeCS = [&](const std::wstring& path, std::shared_ptr<FComputeShaderHandle>& handle)
		{
			if (handle) return;
			FShaderRecipe Recipe = {};
			Recipe.Stage = EShaderStage::Compute;
			Recipe.SourcePath = ShaderDir + path;
			Recipe.EntryPoint = "main";
			Recipe.Target = "cs_5_0";
			handle = FShaderRegistry::Get().GetOrCreateComputeShaderHandle(Device, Recipe);
		};

	MakeCS(L"SceneLighting/BloomThresholdCS.hlsl", ThresholdCS);
	MakeCS(L"SceneLighting/BloomBlurCS.hlsl", BlurCS);
	MakeCS(L"SceneLighting/BloomCompositeCS.hlsl", CompositeCS);

	return ThresholdCS && BlurCS && CompositeCS;
}

void FBloomRenderFeature::UpdateThresholdConstantBuffer(FRenderer& Renderer)
{
	ID3D11DeviceContext* DC = Renderer.GetDeviceContext();
	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DC->Map(ThresholdConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		FBloomThresholdParams Params;
		Params.Threshold = Threshold;
		Params.Knee = 0.1f;
		memcpy(Mapped.pData, &Params, sizeof(Params));
		DC->Unmap(ThresholdConstantBuffer, 0);
	}
}

void FBloomRenderFeature::UpdateBlurConstantBuffer(FRenderer& Renderer, UINT Width, UINT Height)
{
	ID3D11DeviceContext* DC = Renderer.GetDeviceContext();
	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DC->Map(BlurConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		FBloomBlurParams Params;
		Params.TexelSize[0] = 1.0f / static_cast<float>(Width);
		Params.TexelSize[1] = 1.0f / static_cast<float>(Height);
		memcpy(Mapped.pData, &Params, sizeof(Params));
		DC->Unmap(BlurConstantBuffer, 0);
	}
}

void FBloomRenderFeature::UpdateCompositeConstantBuffer(FRenderer& Renderer)
{
	ID3D11DeviceContext* DC = Renderer.GetDeviceContext();
	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DC->Map(CompositeConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		FBloomCompositeParams Params;
		Params.BloomIntensity = BloomIntensity;
		Params.Exposure = Exposure;
		memcpy(Mapped.pData, &Params, sizeof(Params));
		DC->Unmap(CompositeConstantBuffer, 0);
	}
}


