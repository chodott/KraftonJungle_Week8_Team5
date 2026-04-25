#include "Renderer/Features/Shadow/ShadowRenderFeature.h"

#include "Renderer/Renderer.h"
#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/Passes/ScenePassExecutionUtils.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/SceneViewData.h"

#include <algorithm>
#include <cstring>
#include <vector>

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

void FShadowRenderFeature::BindShadowResources(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	const bool bHasShadowData =
			!SceneViewData.LightingInputs.ShadowLights.empty()
			&& !SceneViewData.LightingInputs.ShadowViews.empty()
			&& ShadowLightBufferSRV
			&& ShadowViewBufferSRV
			&& ShadowDepthArraySRV
			&& ShadowComparisonSampler;

	if (!bHasShadowData)
	{
		UnbindShadowResources(Renderer);
		return;
	}

	ID3D11ShaderResourceView* SRVs[3] =
	{
		ShadowLightBufferSRV,
		ShadowViewBufferSRV,
		ShadowDepthArraySRV
	};

	DeviceContext->VSSetShaderResources(ShadowSlots::ShadowLightSRV, 3, SRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::ShadowLightSRV, 3, SRVs);
	DeviceContext->VSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);
	DeviceContext->PSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);
}

void FShadowRenderFeature::UnbindShadowResources(FRenderer& Renderer)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	ID3D11ShaderResourceView* NullSRVs[3] = { nullptr, nullptr, nullptr };
	ID3D11SamplerState*       NullSampler = nullptr;
	DeviceContext->VSSetShaderResources(ShadowSlots::ShadowLightSRV, 3, NullSRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::ShadowLightSRV, 3, NullSRVs);
	DeviceContext->VSSetSamplers(ShadowSlots::ShadowSampler, 1, &NullSampler);
	DeviceContext->PSSetSamplers(ShadowSlots::ShadowSampler, 1, &NullSampler);
}

void FShadowRenderFeature::Release()
{
	SafeRelease(ShadowComparisonSampler);

	SafeRelease(ShadowViewBufferSRV);
	SafeRelease(ShadowViewBuffer);

	SafeRelease(ShadowLightBufferSRV);
	SafeRelease(ShadowLightBuffer);

	SafeRelease(ShadowDepthArraySRV);

	for (ID3D11DepthStencilView*& DSV : ShadowViewDSVs)
	{
		SafeRelease(DSV);
	}

	SafeRelease(ShadowDepthArray);
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

	if (!EnsureResources(Renderer, SceneViewData))
	{
		UnbindShadowResources(Renderer);
		return false;
	}

	// 같은 depth array를 DSV로 쓰기 전에 shader SRV 바인딩을 해제합니다.
	UnbindShadowResources(Renderer);

	RenderShadowViews(Renderer, Processor, Targets, SceneViewData);
	UploadShadowBuffers(Renderer, SceneViewData);
	BindShadowResources(Renderer, SceneViewData);

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

	return EnsureShadowDepthArray(Renderer)
			&& EnsureShadowBuffers(Renderer, ShadowLightCount, ShadowViewCount)
			&& EnsureComparisonSampler(Renderer);
}

bool FShadowRenderFeature::EnsureShadowDepthArray(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (ShadowDepthArray && ShadowDepthArraySRV && ShadowViewDSVs[0])
	{
		return true;
	}

	SafeRelease(ShadowDepthArraySRV);

	for (ID3D11DepthStencilView*& DSV : ShadowViewDSVs)
	{
		SafeRelease(DSV);
	}

	SafeRelease(ShadowDepthArray);

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width                = ShadowConfig::ShadowMapResolution;
	TextureDesc.Height               = ShadowConfig::ShadowMapResolution;
	TextureDesc.MipLevels            = 1;
	TextureDesc.ArraySize            = ShadowConfig::MaxShadowViews;
	TextureDesc.Format               = DXGI_FORMAT_R32_TYPELESS;
	TextureDesc.SampleDesc.Count     = 1;
	TextureDesc.SampleDesc.Quality   = 0;
	TextureDesc.Usage                = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags            = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &ShadowDepthArray)) || !ShadowDepthArray)
	{
		Release();
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
		Release();
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
			Release();
			return false;
		}
	}

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

			FShadowViewGPU& Dst     = GPUData[Index];
			Dst.LightViewProjection = Src.ViewProjection.GetTransposed();
			Dst.ArraySlice          = Src.ArraySlice;
			Dst.ProjectionType      = static_cast<uint32>(Src.ProjectionType);
			Dst.Pad0                = 0;
			Dst.Pad1                = 0;
			Dst.ViewParams          = FVector4(Src.NearZ, Src.FarZ, 0.0f, 0.0f);
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

	const FViewContext OriginalView = SceneViewData.View;

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

		DeviceContext->ClearDepthStencilView(ShadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

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
		SceneViewData.View.Viewport              = ShadowView.Viewport;

		BeginPass(
			Renderer,
			0,
			nullptr,
			ShadowDSV,
			ShadowView.Viewport,
			SceneViewData.Frame,
			SceneViewData.View);

		Processor.ExecutePass(
			Renderer,
			Targets,
			SceneViewData,
			EMeshPassType::DepthPrepass);
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
