#include "Renderer/Features/Shadow/ShadowRenderFeature.h"

#include "Renderer/Renderer.h"
#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/Passes/ScenePassExecutionUtils.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/SceneViewData.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "Math/MathUtility.h"

namespace
{
	template <typename T>
	void SafeRelease(T*& Ptr)
	{
		if (Ptr)
		{
			Ptr->Release();
			Ptr = nullptr;
		}
	}
}

FShadowRenderFeature::~FShadowRenderFeature()
{
	Release();
}

void FShadowRenderFeature::SetDefaultShadowMapResolution(uint32 Resolution)
{
	const uint32 NewResolution = FMath::Clamp(
		Resolution,
		ShadowConfig::MinShadowMapResolution,
		ShadowConfig::MaxShadowMapResolution);

	if (DefaultShadowMapResolution == NewResolution)
	{
		return;
	}

	DefaultShadowMapResolution = NewResolution;
	bShadowDepthArrayDirty     = true;
}

void FShadowRenderFeature::BindShadowResources(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	const uint32 ShadowViewCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.ShadowViews.size()),
		ShadowConfig::MaxShadowViews);

	const bool bNeedVSM = (GlobalFilterMode == EShadowFilterMode::VSM);

	const bool bHasCommonShadowData =
			!SceneViewData.LightingInputs.ShadowLights.empty() &&
			!SceneViewData.LightingInputs.ShadowViews.empty() &&
			ShadowLightBufferSRV &&
			ShadowViewBufferSRV &&
			ShadowDepthArraySRV &&
			ShadowComparisonSampler;

	const bool bHasVSMData =
			!bNeedVSM ||
			(ShadowMomentsArraySRV && ShadowLinearSampler);

	if (!(bHasCommonShadowData && bHasVSMData))
	{
		UnbindShadowResources(Renderer);
		return;
	}

	ID3D11ShaderResourceView* MomentsSRV = nullptr;
	if (bNeedVSM)
	{
		MomentsSRV =
				(bMomentsBlurValid && ShadowMomentsBlurSRV)
					? ShadowMomentsBlurSRV
					: ShadowMomentsArraySRV;
	}

	ID3D11ShaderResourceView* SRVs[4] =
	{
		ShadowLightBufferSRV,
		ShadowViewBufferSRV,
		ShadowDepthArraySRV,
		MomentsSRV
	};

	DeviceContext->VSSetShaderResources(ShadowSlots::ShadowLightSRV, 4, SRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::ShadowLightSRV, 4, SRVs);

	DeviceContext->VSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);
	DeviceContext->PSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);

	if (bNeedVSM)
	{
		DeviceContext->VSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &ShadowLinearSampler);
		DeviceContext->PSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &ShadowLinearSampler);
	}
	else
	{
		ID3D11SamplerState* NullSampler = nullptr;
		DeviceContext->VSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &NullSampler);
		DeviceContext->PSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &NullSampler);
	}
}

void FShadowRenderFeature::UnbindShadowResources(FRenderer& Renderer)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	ID3D11ShaderResourceView* NullSRVs[4] = { nullptr, nullptr, nullptr, nullptr };
	ID3D11SamplerState*       NullSampler = nullptr;
	DeviceContext->VSSetShaderResources(ShadowSlots::ShadowLightSRV, 4, NullSRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::ShadowLightSRV, 4, NullSRVs);
	DeviceContext->VSSetSamplers(ShadowSlots::ShadowSampler, 1, &NullSampler);
	DeviceContext->PSSetSamplers(ShadowSlots::ShadowSampler, 1, &NullSampler);
	DeviceContext->VSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &NullSampler);
	DeviceContext->PSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &NullSampler);
}

void FShadowRenderFeature::Release()
{
	SafeRelease(ShadowLinearSampler);
	SafeRelease(ShadowComparisonSampler);

	SafeRelease(ShadowViewBufferSRV);
	SafeRelease(ShadowViewBuffer);

	SafeRelease(ShadowLightBufferSRV);
	SafeRelease(ShadowLightBuffer);

	SafeRelease(ShadowMomentsBlurSRV);
	for (ID3D11RenderTargetView*& RTV : ShadowMomentsBlurRTV)
	{
		SafeRelease(RTV);
	}
	SafeRelease(ShadowMomentsBlur);

	SafeRelease(ShadowMomentsArraySRV);
	for (ID3D11RenderTargetView*& RTV : ShadowMomentsRTV)
	{
		SafeRelease(RTV);
	}
	SafeRelease(ShadowMomentsArray);

	SafeRelease(ShadowDepthArraySRV);
	for (ID3D11DepthStencilView*& DSV : ShadowViewDSVs)
	{
		SafeRelease(DSV);
	}
	SafeRelease(ShadowDepthArray);

	bMomentsBlurValid      = false;
	bShadowDepthArrayDirty = true;
}

bool FShadowRenderFeature::RenderShadows(
	FRenderer&                Renderer,
	const FMeshPassProcessor& Processor,
	FSceneRenderTargets&      Targets,
	FSceneViewData&           SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	if (SceneViewData.LightingInputs.ShadowLights.empty() || SceneViewData.LightingInputs.ShadowViews.empty())
	{
		UnbindShadowResources(Renderer);
		return true;
	}

	UnbindShadowResources(Renderer);

	if (!EnsureResources(Renderer, SceneViewData))
	{
		UnbindShadowResources(Renderer);
		return false;
	}

	RenderShadowViews(Renderer, Processor, Targets, SceneViewData);
	UploadShadowBuffers(Renderer, SceneViewData);
	BindShadowResources(Renderer, SceneViewData);

	return true;
}

bool FShadowRenderFeature::EnsureLinearSampler(const FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (ShadowLinearSampler)
	{
		return true;
	}

	D3D11_SAMPLER_DESC Desc = {};
	Desc.Filter             = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	Desc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
	Desc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
	Desc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
	Desc.MinLOD             = 0.0f;
	Desc.MaxLOD             = D3D11_FLOAT32_MAX;

	return SUCCEEDED(Device->CreateSamplerState(&Desc, &ShadowLinearSampler)) && ShadowLinearSampler;
}

bool FShadowRenderFeature::EnsureMomentsArray(const FRenderer& Renderer, uint32 RequiredResolution)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	RequiredResolution = FMath::Clamp(
		RequiredResolution,
		ShadowConfig::MinShadowMapResolution,
		ShadowConfig::MaxShadowMapResolution);

	bool bAllRTVsValid = true;
	for (uint32 Slice = 0; Slice < ShadowConfig::MaxShadowViews; ++Slice)
	{
		if (!ShadowMomentsRTV[Slice] || !ShadowMomentsBlurRTV[Slice])
		{
			bAllRTVsValid = false;
			break;
		}
	}

	bool bRecreate =
			bShadowDepthArrayDirty ||
			!ShadowMomentsArray ||
			!ShadowMomentsArraySRV ||
			!ShadowMomentsBlur ||
			!ShadowMomentsBlurSRV ||
			!bAllRTVsValid;

	if (!bRecreate)
	{
		D3D11_TEXTURE2D_DESC ExistingDesc = {};
		ShadowMomentsArray->GetDesc(&ExistingDesc);

		if (ExistingDesc.Width != RequiredResolution ||
			ExistingDesc.Height != RequiredResolution ||
			ExistingDesc.ArraySize != ShadowConfig::MaxShadowViews ||
			ExistingDesc.Format != DXGI_FORMAT_R32G32_FLOAT)
		{
			bRecreate = true;
		}
	}

	if (!bRecreate)
	{
		return true;
	}

	SafeRelease(ShadowMomentsArray);
	SafeRelease(ShadowMomentsBlurSRV);

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxShadowViews; ++Slice)
	{
		SafeRelease(ShadowMomentsRTV[Slice]);
		SafeRelease(ShadowMomentsBlurRTV[Slice]);
	}

	SafeRelease(ShadowMomentsArraySRV);
	SafeRelease(ShadowMomentsBlur);

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width                = ShadowDepthArrayResolution;
	TextureDesc.Height               = ShadowDepthArrayResolution;
	TextureDesc.MipLevels            = 1;
	TextureDesc.ArraySize            = ShadowConfig::MaxShadowViews;
	TextureDesc.Format               = DXGI_FORMAT_R32G32_FLOAT;
	TextureDesc.SampleDesc.Count     = 1;
	TextureDesc.SampleDesc.Quality   = 0;
	TextureDesc.Usage                = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &ShadowMomentsArray)) || !ShadowMomentsArray)
	{
		return false;
	}

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &ShadowMomentsBlur)) || !ShadowMomentsBlur)
	{
		SafeRelease(ShadowMomentsArray);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = TextureDesc.Format;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	SRVDesc.Texture2DArray.MostDetailedMip  = 0;
	SRVDesc.Texture2DArray.MipLevels        = 1;
	SRVDesc.Texture2DArray.FirstArraySlice  = 0;
	SRVDesc.Texture2DArray.ArraySize        = ShadowConfig::MaxShadowViews;

	if (FAILED(Device->CreateShaderResourceView(ShadowMomentsArray, &SRVDesc, &ShadowMomentsArraySRV)) || !ShadowMomentsArraySRV)
	{
		SafeRelease(ShadowMomentsArray);
		SafeRelease(ShadowMomentsBlur);
		return false;
	}

	if (FAILED(Device->CreateShaderResourceView(ShadowMomentsBlur, &SRVDesc, &ShadowMomentsBlurSRV)) || !ShadowMomentsBlurSRV)
	{
		SafeRelease(ShadowMomentsArray);
		SafeRelease(ShadowMomentsBlur);
		return false;
	}

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxShadowViews; ++Slice)
	{
		D3D11_RENDER_TARGET_VIEW_DESC RTVDesc  = {};
		RTVDesc.Format                         = DXGI_FORMAT_R32G32_FLOAT;
		RTVDesc.ViewDimension                  = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		RTVDesc.Texture2DArray.MipSlice        = 0;
		RTVDesc.Texture2DArray.FirstArraySlice = Slice;
		RTVDesc.Texture2DArray.ArraySize       = 1;

		if (FAILED(Device->CreateRenderTargetView(ShadowMomentsArray, &RTVDesc, &ShadowMomentsRTV[Slice])) || !ShadowMomentsRTV[Slice])
		{
			return false;
		}

		if (FAILED(Device->CreateRenderTargetView(ShadowMomentsBlur, &RTVDesc, &ShadowMomentsBlurRTV[Slice])) || !ShadowMomentsBlurRTV[Slice])
		{
			return false;
		}
	}

	return true;
}

bool FShadowRenderFeature::EnsureResources(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	const uint32 ShadowLightCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.ShadowLights.size()),
		ShadowConfig::MaxShadowLights);

	const uint32 ShadowViewCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.ShadowViews.size()),
		ShadowConfig::MaxShadowViews);

	const uint32 RequiredResolution = ComputeRequiredShadowDepthArrayResolution(SceneViewData);

	const bool bNeedVSM = (GlobalFilterMode == EShadowFilterMode::VSM);

	bool bOk =
			EnsureShadowDepthArray(Renderer, RequiredResolution) &&
			EnsureShadowBuffers(Renderer, ShadowLightCount, ShadowViewCount) &&
			EnsureComparisonSampler(Renderer);

	if (!bOk)
	{
		return false;
	}

	if (bNeedVSM)
	{
		bOk =
				EnsureLinearSampler(Renderer) &&
				EnsureMomentsArray(Renderer, RequiredResolution);

		if (!bOk)
		{
			return false;
		}
	}

	return true;
}

bool FShadowRenderFeature::EnsureShadowDepthArray(FRenderer& Renderer, uint32 RequiredResolution)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	RequiredResolution = FMath::Clamp(
		RequiredResolution,
		ShadowConfig::MinShadowMapResolution,
		ShadowConfig::MaxShadowMapResolution);

	bool bAllDSVsValid = true;
	for (ID3D11DepthStencilView* DSV : ShadowViewDSVs)
	{
		if (!DSV)
		{
			bAllDSVsValid = false;
			break;
		}
	}

	bool bRecreate =
			bShadowDepthArrayDirty ||
			!ShadowDepthArray ||
			!ShadowDepthArraySRV ||
			!bAllDSVsValid ||
			ShadowDepthArrayResolution != RequiredResolution;

	if (!bRecreate)
	{
		D3D11_TEXTURE2D_DESC ExistingDesc = {};
		ShadowDepthArray->GetDesc(&ExistingDesc);

		if (ExistingDesc.Width != RequiredResolution ||
			ExistingDesc.Height != RequiredResolution ||
			ExistingDesc.ArraySize != ShadowConfig::MaxShadowViews)
		{
			bRecreate = true;
		}
	}

	if (!bRecreate)
	{
		return true;
	}

	UnbindShadowResources(Renderer);

	SafeRelease(ShadowDepthArraySRV);

	for (ID3D11DepthStencilView*& DSV : ShadowViewDSVs)
	{
		SafeRelease(DSV);
	}

	SafeRelease(ShadowDepthArray);

	ShadowDepthArrayResolution = RequiredResolution;

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width                = ShadowDepthArrayResolution;
	TextureDesc.Height               = ShadowDepthArrayResolution;
	TextureDesc.MipLevels            = 1;
	TextureDesc.ArraySize            = ShadowConfig::MaxShadowViews;
	TextureDesc.Format               = DXGI_FORMAT_R32_TYPELESS;
	TextureDesc.SampleDesc.Count     = 1;
	TextureDesc.SampleDesc.Quality   = 0;
	TextureDesc.Usage                = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags            = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &ShadowDepthArray)) || !ShadowDepthArray)
	{
		bShadowDepthArrayDirty = true;
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	SRVDesc.Texture2DArray.MostDetailedMip  = 0;
	SRVDesc.Texture2DArray.MipLevels        = 1;
	SRVDesc.Texture2DArray.FirstArraySlice  = 0;
	SRVDesc.Texture2DArray.ArraySize        = ShadowConfig::MaxShadowViews;

	if (FAILED(Device->CreateShaderResourceView(ShadowDepthArray, &SRVDesc, &ShadowDepthArraySRV)) || !ShadowDepthArraySRV)
	{
		SafeRelease(ShadowDepthArray);
		bShadowDepthArrayDirty = true;
		return false;
	}

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxShadowViews; ++Slice)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc  = {};
		DSVDesc.Format                         = DXGI_FORMAT_D32_FLOAT;
		DSVDesc.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		DSVDesc.Texture2DArray.MipSlice        = 0;
		DSVDesc.Texture2DArray.FirstArraySlice = Slice;
		DSVDesc.Texture2DArray.ArraySize       = 1;

		if (FAILED(Device->CreateDepthStencilView(ShadowDepthArray, &DSVDesc, &ShadowViewDSVs[Slice])) || !ShadowViewDSVs[Slice])
		{
			SafeRelease(ShadowDepthArraySRV);

			for (ID3D11DepthStencilView*& DSV : ShadowViewDSVs)
			{
				SafeRelease(DSV);
			}

			SafeRelease(ShadowDepthArray);
			bShadowDepthArrayDirty = true;
			return false;
		}
	}

	bShadowDepthArrayDirty = false;
	return true;
}

bool FShadowRenderFeature::EnsureShadowBuffers(
	FRenderer& Renderer,
	uint32     ShadowLightCount,
	uint32     ShadowViewCount)
{
	return EnsureDynamicStructuredBufferSRV(
				Renderer,
				sizeof(FShadowLightGPU),
				ShadowLightCount,
				ShadowLightBuffer,
				ShadowLightBufferSRV)
			&& EnsureDynamicStructuredBufferSRV(
				Renderer,
				sizeof(FShadowViewGPU),
				ShadowViewCount,
				ShadowViewBuffer,
				ShadowViewBufferSRV);
}

bool FShadowRenderFeature::EnsureDynamicStructuredBufferSRV(
	FRenderer&                 Renderer,
	uint32                     ElementStride,
	uint32                     ElementCount,
	ID3D11Buffer*&             Buffer,
	ID3D11ShaderResourceView*& SRV)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device || ElementStride == 0)
	{
		return false;
	}

	const uint32 SafeElementCount = (std::max)(1u, ElementCount);
	const UINT   ByteWidth        = ElementStride * SafeElementCount;

	bool bRecreate = !Buffer || !SRV;
	if (!bRecreate)
	{
		D3D11_BUFFER_DESC ExistingDesc = {};
		Buffer->GetDesc(&ExistingDesc);

		if (ExistingDesc.ByteWidth < ByteWidth || ExistingDesc.StructureByteStride != ElementStride)
		{
			bRecreate = true;
		}
	}

	if (!bRecreate)
	{
		return true;
	}

	SafeRelease(SRV);
	SafeRelease(Buffer);

	D3D11_BUFFER_DESC BufferDesc   = {};
	BufferDesc.ByteWidth           = ByteWidth;
	BufferDesc.Usage               = D3D11_USAGE_DYNAMIC;
	BufferDesc.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
	BufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
	BufferDesc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	BufferDesc.StructureByteStride = ElementStride;

	if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, &Buffer)) || !Buffer)
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement             = 0;
	SRVDesc.Buffer.NumElements              = SafeElementCount;

	if (FAILED(Device->CreateShaderResourceView(Buffer, &SRVDesc, &SRV)) || !SRV)
	{
		SafeRelease(Buffer);
		return false;
	}

	return true;
}

bool FShadowRenderFeature::EnsureComparisonSampler(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (ShadowComparisonSampler)
	{
		return true;
	}

	D3D11_SAMPLER_DESC Desc = {};
	Desc.Filter             = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	Desc.AddressU           = D3D11_TEXTURE_ADDRESS_BORDER;
	Desc.AddressV           = D3D11_TEXTURE_ADDRESS_BORDER;
	Desc.AddressW           = D3D11_TEXTURE_ADDRESS_BORDER;
	Desc.BorderColor[0]     = 1.0f;
	Desc.BorderColor[1]     = 1.0f;
	Desc.BorderColor[2]     = 1.0f;
	Desc.BorderColor[3]     = 1.0f;
	Desc.ComparisonFunc     = D3D11_COMPARISON_LESS_EQUAL;
	Desc.MinLOD             = 0.0f;
	Desc.MaxLOD             = D3D11_FLOAT32_MAX;

	return SUCCEEDED(Device->CreateSamplerState(&Desc, &ShadowComparisonSampler)) && ShadowComparisonSampler;
}

void FShadowRenderFeature::UploadShadowBuffers(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext || !ShadowLightBuffer || !ShadowViewBuffer)
	{
		return;
	}

	const uint32 ShadowLightCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.ShadowLights.size()),
		ShadowConfig::MaxShadowLights);

	const uint32 ShadowViewCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.ShadowViews.size()),
		ShadowConfig::MaxShadowViews);

	{
		std::vector<FShadowLightGPU> GPUData;
		GPUData.resize((std::max)(1u, ShadowLightCount));

		for (uint32 Index = 0; Index < ShadowLightCount; ++Index)
		{
			const FShadowLightRenderItem& Src = SceneViewData.LightingInputs.ShadowLights[Index];

			FShadowLightGPU& Dst = GPUData[Index];
			Dst.LightType        = static_cast<uint32>(Src.LightType);
			Dst.FirstViewIndex   = Src.FirstViewIndex;
			Dst.ViewCount        = Src.ViewCount;
			Dst.Flags            = 0;
			Dst.PositionType     = FVector4(Src.PositionWS.X, Src.PositionWS.Y, Src.PositionWS.Z, 0.0f);
			Dst.DirectionBias    = FVector4(Src.DirectionWS.X, Src.DirectionWS.Y, Src.DirectionWS.Z, Src.Bias);
			Dst.Params0          = FVector4(Src.SlopeBias, Src.NormalBias, 0.0f, 0.0f);
		}

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (SUCCEEDED(DeviceContext->Map(ShadowLightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			std::memcpy(Mapped.pData, GPUData.data(), sizeof(FShadowLightGPU) * GPUData.size());
			DeviceContext->Unmap(ShadowLightBuffer, 0);
		}
	}

	{
		std::vector<FShadowViewGPU> GPUData;
		GPUData.resize((std::max)(1u, ShadowViewCount));

		for (uint32 Index = 0; Index < ShadowViewCount; ++Index)
		{
			const FShadowViewRenderItem& Src = SceneViewData.LightingInputs.ShadowViews[Index];

			const float ViewportScale = GetShadowViewportScale(Src.RequestedResolution);
			const float TexelSize     = ShadowDepthArrayResolution > 0
				                        ? 1.0f / static_cast<float>(ShadowDepthArrayResolution)
				                        : 1.0f;

			FShadowViewGPU& Dst     = GPUData[Index];
			Dst.LightViewProjection = Src.ViewProjection.GetTransposed();
			Dst.ArraySlice          = Src.ArraySlice;
			Dst.ProjectionType      = static_cast<uint32>(Src.ProjectionType);
			Dst.FilterMode          = static_cast<uint32>(GlobalFilterMode);
			Dst.Pad0                = 0;
			Dst.ViewParams          = FVector4(Src.NearZ, Src.FarZ, ViewportScale, TexelSize);
		}

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (SUCCEEDED(DeviceContext->Map(ShadowViewBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			std::memcpy(Mapped.pData, GPUData.data(), sizeof(FShadowViewGPU) * GPUData.size());
			DeviceContext->Unmap(ShadowViewBuffer, 0);
		}
	}
}

void FShadowRenderFeature::RenderShadowViews(
	FRenderer&                Renderer,
	const FMeshPassProcessor& Processor,
	FSceneRenderTargets&      Targets,
	FSceneViewData&           SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	const uint32 ShadowViewCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.ShadowViews.size()),
		ShadowConfig::MaxShadowViews);

	if (ShadowViewCount == 0)
	{
		return;
	}

	const FViewContext OriginalView    = SceneViewData.View;
	static const float ClearMoments[4] = { 1.0f, 1.0f, 0.0f, 0.0f };

	for (uint32 ViewIndex = 0; ViewIndex < ShadowViewCount; ++ViewIndex)
	{
		const FShadowViewRenderItem& ShadowView = SceneViewData.LightingInputs.ShadowViews[ViewIndex];

		if (ShadowView.ArraySlice >= ShadowConfig::MaxShadowViews)
		{
			continue;
		}

		ID3D11DepthStencilView* ShadowDSV = ShadowViewDSVs[ShadowView.ArraySlice];
		if (!ShadowDSV)
		{
			continue;
		}

		const D3D11_VIEWPORT ShadowViewport = BuildShadowViewport(ShadowView.RequestedResolution);

		SceneViewData.View.View                  = ShadowView.View;
		SceneViewData.View.Projection            = ShadowView.Projection;
		SceneViewData.View.ViewProjection        = ShadowView.ViewProjection;
		SceneViewData.View.InverseView           = ShadowView.View.GetInverse();
		SceneViewData.View.InverseProjection     = ShadowView.Projection.GetInverse();
		SceneViewData.View.InverseViewProjection = ShadowView.ViewProjection.GetInverse();
		SceneViewData.View.CameraPosition        = ShadowView.PositionWS;
		SceneViewData.View.NearZ                 = ShadowView.NearZ;
		SceneViewData.View.FarZ                  = ShadowView.FarZ;
		SceneViewData.View.bOrthographic         = ShadowView.ProjectionType == EShadowProjectionType::Orthographic;
		SceneViewData.View.Viewport              = ShadowViewport;

		if (GlobalFilterMode == EShadowFilterMode::Raw ||
			GlobalFilterMode == EShadowFilterMode::PCF)
		{
			DeviceContext->ClearDepthStencilView(ShadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

			BeginPass(
				Renderer,
				0,
				nullptr,
				ShadowDSV,
				ShadowViewport,
				SceneViewData.Frame,
				SceneViewData.View);

			Processor.ExecutePass(
				Renderer,
				Targets,
				SceneViewData,
				EMeshPassType::DepthPrepass);
		}
		else
		{
			ID3D11RenderTargetView* MomentsRTV = ShadowMomentsRTV[ShadowView.ArraySlice];
			if (!MomentsRTV)
			{
				continue;
			}
			DeviceContext->ClearRenderTargetView(MomentsRTV, ClearMoments);
			DeviceContext->ClearDepthStencilView(ShadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

			BeginPass(
				Renderer,
				MomentsRTV,
				ShadowDSV,
				ShadowViewport,
				SceneViewData.Frame,
				SceneViewData.View);

			Processor.ExecutePass(
				Renderer,
				Targets,
				SceneViewData,
				EMeshPassType::ShadowVSM);
		}
	}

	SceneViewData.View = OriginalView;

	BeginPass(
		Renderer,
		Targets.SceneColorRTV,
		Targets.SceneDepthDSV,
		OriginalView.Viewport,
		SceneViewData.Frame,
		SceneViewData.View);
}

uint32 FShadowRenderFeature::ResolveShadowViewResolution(uint32 RequestedResolution) const
{
	const uint32 Resolution = RequestedResolution > 0
		                          ? RequestedResolution
		                          : DefaultShadowMapResolution;

	return FMath::Clamp(
		Resolution,
		ShadowConfig::MinShadowMapResolution,
		ShadowConfig::MaxShadowMapResolution);
}

uint32 FShadowRenderFeature::ComputeRequiredShadowDepthArrayResolution(
	const FSceneViewData& SceneViewData) const
{
	uint32 RequiredResolution = DefaultShadowMapResolution;

	const uint32 ShadowViewCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.ShadowViews.size()),
		ShadowConfig::MaxShadowViews);

	for (uint32 Index = 0; Index < ShadowViewCount; ++Index)
	{
		const FShadowViewRenderItem& View = SceneViewData.LightingInputs.ShadowViews[Index];
		RequiredResolution                = (std::max)(RequiredResolution, ResolveShadowViewResolution(View.RequestedResolution));
	}

	return FMath::Clamp(
		RequiredResolution,
		ShadowConfig::MinShadowMapResolution,
		ShadowConfig::MaxShadowMapResolution);
}

D3D11_VIEWPORT FShadowRenderFeature::BuildShadowViewport(uint32 RequestedResolution) const
{
	const uint32 ResolvedResolution = ResolveShadowViewResolution(RequestedResolution);
	const float  EffectiveSize      = static_cast<float>((std::min)(ResolvedResolution, ShadowDepthArrayResolution));

	D3D11_VIEWPORT Viewport = {};
	Viewport.TopLeftX       = 0.0f;
	Viewport.TopLeftY       = 0.0f;
	Viewport.Width          = EffectiveSize;
	Viewport.Height         = EffectiveSize;
	Viewport.MinDepth       = 0.0f;
	Viewport.MaxDepth       = 1.0f;

	return Viewport;
}

float FShadowRenderFeature::GetShadowViewportScale(uint32 RequestedResolution) const
{
	if (ShadowDepthArrayResolution == 0)
	{
		return 1.0f;
	}

	const D3D11_VIEWPORT Viewport = BuildShadowViewport(RequestedResolution);
	return Viewport.Width / static_cast<float>(ShadowDepthArrayResolution);
}
