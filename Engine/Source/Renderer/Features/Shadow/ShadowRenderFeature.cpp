#include "Renderer/Features/Shadow/ShadowRenderFeature.h"
#include "Renderer/Features/Shadow/ShadowAtlasAllocator.h"

#include "Renderer/Renderer.h"
#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/Passes/ScenePassExecutionUtils.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/SceneViewData.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "Core/Paths.h"
#include "Math/MathUtility.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Resources/Shader/ShaderRegistry.h"

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

	const bool bHasLocalSRVs =
		ShadowLightBufferSRV && ShadowViewBufferSRV  && ShadowComparisonSampler;

	const bool bHasDirSRVs =
		DirShadowLightBufferSRV && DirShadowViewBufferSRV && DirShadowDepthArraySRV && ShadowComparisonSampler;

	const bool bHasVSMData =
		!bNeedVSM || (LocalShadowMomentsAtlasSRV && DirShadowMomentsArraySRV && ShadowLinearSampler);

	if (!(bHasLocalSRVs && bHasDirSRVs && bHasVSMData))
	{
		UnbindShadowResources(Renderer);
		return;
	}

	ID3D11ShaderResourceView* MomentsSRV = nullptr;
	ID3D11ShaderResourceView* DirMomentsSRV = nullptr;
	if (bNeedVSM)
	{
		DirMomentsSRV =
			(bMomentsBlurValid && DirShadowMomentsBlurSRV)
			? DirShadowMomentsBlurSRV
			: DirShadowMomentsArraySRV;

		MomentsSRV = LocalShadowMomentsAtlasSRV;
	}

	ID3D11ShaderResourceView* SRVs[4] =
	{
		ShadowLightBufferSRV,
		ShadowViewBufferSRV,
		LocalShadowDepthAtlasSRV,
		MomentsSRV
	};

	DeviceContext->VSSetShaderResources(ShadowSlots::ShadowLightSRV, 4, SRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::ShadowLightSRV, 4, SRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::ShadowCubeSRV, 1 , &ShadowDepthCubeArraySRV);
	DeviceContext->PSSetShaderResources(ShadowSlots::ShadowMomentCubeSRV, 1, &ShadowMomentsCubeArraySRV);

	DeviceContext->VSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);
	DeviceContext->PSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);

	ID3D11ShaderResourceView* DirSRVs[4] =
	{
		DirShadowLightBufferSRV,
		DirShadowViewBufferSRV,
		DirShadowDepthArraySRV,
		DirMomentsSRV
	};

	DeviceContext->VSSetShaderResources(ShadowSlots::DirShadowLightSRV, 4, DirSRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::DirShadowLightSRV, 4, DirSRVs);

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
	DeviceContext->VSSetShaderResources(ShadowSlots::DirShadowLightSRV, 4, NullSRVs);
	DeviceContext->PSSetShaderResources(ShadowSlots::DirShadowLightSRV, 4, NullSRVs);
	
	EnsureComparisonSampler(Renderer);
	EnsureLinearSampler(Renderer);

	if (ShadowComparisonSampler)
	{
		DeviceContext->VSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);
		DeviceContext->PSSetSamplers(ShadowSlots::ShadowSampler, 1, &ShadowComparisonSampler);
	}

	if (ShadowLinearSampler)
	{
		DeviceContext->VSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &ShadowLinearSampler);
		DeviceContext->PSSetSamplers(ShadowSlots::ShadowLinearSampler, 1, &ShadowLinearSampler);
	}
}

void FShadowRenderFeature::Release()
{
	SafeRelease(ShadowDebugPreviewSRV);
	SafeRelease(ShadowDebugPreviewRTV);
	SafeRelease(ShadowDebugPreviewTexture);
	SafeRelease(ShadowDebugSampler);
	SafeRelease(ShadowDebugCB);

	ShadowDebugVS.reset();
	ShadowDebugPS.reset();

	SafeRelease(ShadowLinearSampler);
	SafeRelease(ShadowComparisonSampler);

	SafeRelease(ShadowViewBufferSRV);
	SafeRelease(ShadowViewBuffer);

	SafeRelease(ShadowLightBufferSRV);
	SafeRelease(ShadowLightBuffer);

	SafeRelease(LocalShadowDepthAtlasSRV);
	SafeRelease(LocalShadowDepthAtlasDSV);
	SafeRelease(LocalShadowDepthAtlas);

	SafeRelease(LocalShadowMomentsAtlasSRV);
	SafeRelease(LocalShadowMomentsAtlasRTV);
	SafeRelease(LocalShadowMomentsAtlas);


	for (ID3D11DepthStencilView*& DSV : ShadowDepthCubeDSVs)
	{
		SafeRelease(DSV);
	}

	SafeRelease(DirShadowDepthArray);

	SafeRelease(DirShadowViewBufferSRV);
	SafeRelease(DirShadowViewBuffer);

	SafeRelease(DirShadowLightBufferSRV);
	SafeRelease(DirShadowLightBuffer);

	SafeRelease(DirShadowMomentsBlurSRV);
	for (ID3D11RenderTargetView*& RTV : DirShadowMomentsBlurRTV)
	{
		SafeRelease(RTV);
	}
	SafeRelease(DirShadowMomentsBlur);

	SafeRelease(DirShadowMomentsArraySRV);
	for (ID3D11RenderTargetView*& RTV : DirShadowMomentsRTV)
	{
		SafeRelease(RTV);
	}
	SafeRelease(DirShadowMomentsArray);

	SafeRelease(DirShadowDepthArraySRV);
	for (ID3D11DepthStencilView*& DSV : DirShadowViewDSVs)
	{
		SafeRelease(DSV);
	}
	SafeRelease(DirShadowDepthArray);

	SafeRelease(ShadowDepthCubeArray);
	SafeRelease(ShadowDepthCubeArraySRV);

	for(ID3D11RenderTargetView*& RTV : ShadowMomentsCubeRTVs)
	{
		SafeRelease(RTV);
	}
	SafeRelease(ShadowMomentsCubeArray);
	SafeRelease(ShadowMomentsCubeArraySRV);

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

	EnsureComparisonSampler(Renderer);
	EnsureLinearSampler(Renderer);

	if ((SceneViewData.LightingInputs.ShadowLights.empty() || SceneViewData.LightingInputs.ShadowViews.empty())
		&& (SceneViewData.LightingInputs.DirShadowLights.empty() || SceneViewData.LightingInputs.DirShadowViews.empty()))
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

	if (DebugViewMode != EShadowDebugViewMode::None)
	{
		RenderDebugPreview(Renderer, Targets, SceneViewData);
	}
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

bool FShadowRenderFeature::EnsureMomentsAtlas(const FRenderer& Renderer, uint32 RequiredResolution)
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

	if (LocalShadowMomentsAtlas)
	{
		return true;
	}

	/////////////////////////////////////////////////////////////////////
	// Atlas
	/////////////////////////////////////////////////////////////////////
	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width                = ShadowConfig::MaxShadowMapResolution;
	TextureDesc.Height               = ShadowConfig::MaxShadowMapResolution;
	TextureDesc.MipLevels            = 1;
	TextureDesc.Format               = DXGI_FORMAT_R32G32_FLOAT;
	TextureDesc.ArraySize = 1;
	TextureDesc.SampleDesc.Count     = 1;
	TextureDesc.SampleDesc.Quality   = 0;
	TextureDesc.Usage                = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &LocalShadowMomentsAtlas)) || !LocalShadowMomentsAtlas)
	{
		SafeRelease(LocalShadowMomentsAtlas);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = TextureDesc.Format;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;

	if (FAILED(Device->CreateShaderResourceView(LocalShadowMomentsAtlas, &SRVDesc, &LocalShadowMomentsAtlasSRV)) || !LocalShadowMomentsAtlasSRV)
	{
		SafeRelease(LocalShadowMomentsAtlas);
		return false;
	}

	D3D11_RENDER_TARGET_VIEW_DESC RTVDesc  = {};
	RTVDesc.Format                         = DXGI_FORMAT_R32G32_FLOAT;
	RTVDesc.ViewDimension                  = D3D11_RTV_DIMENSION_TEXTURE2D;
	RTVDesc.Texture2D.MipSlice = 0;

	if (FAILED(Device->CreateRenderTargetView(LocalShadowMomentsAtlas, &RTVDesc, &LocalShadowMomentsAtlasRTV)) || !LocalShadowMomentsAtlasRTV)
	{
		return false;
	}

	////////////////////////////////////////////////////////////////////
	// Cube array (for point light shadows)
	/////////////////////////////////////////////////////////////////////

	D3D11_TEXTURE2D_DESC CubeTextureDesc = {};
	CubeTextureDesc.Width = ShadowDepthArrayResolution;   // 깊이 큐브와 동일 크기
	CubeTextureDesc.Height = ShadowDepthArrayResolution;
	CubeTextureDesc.MipLevels = 1;
	CubeTextureDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	CubeTextureDesc.ArraySize = ShadowConfig::MaxShadowViews;
	CubeTextureDesc.SampleDesc.Count = 1;
	CubeTextureDesc.SampleDesc.Quality = 0;
	CubeTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	CubeTextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	CubeTextureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	if (FAILED(Device->CreateTexture2D(&CubeTextureDesc, nullptr, &ShadowMomentsCubeArray)) || !ShadowMomentsCubeArray)
	{
		SafeRelease(ShadowMomentsCubeArray);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC CubeSRVDesc = {};
	CubeSRVDesc.Format = CubeTextureDesc.Format;
	CubeSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
	CubeSRVDesc.Texture2D.MostDetailedMip = 0;
	CubeSRVDesc.Texture2D.MipLevels = 1;
	CubeSRVDesc.TextureCubeArray.First2DArrayFace = ShadowConfig::PointShadowSliceOffset;
	CubeSRVDesc.TextureCubeArray.NumCubes = ShadowConfig::MaxPointShadowCubes;

	if (FAILED(Device->CreateShaderResourceView(ShadowMomentsCubeArray, &CubeSRVDesc, &ShadowMomentsCubeArraySRV)) || !ShadowMomentsCubeArraySRV)
	{
		SafeRelease(ShadowMomentsCubeArraySRV);
		SafeRelease(ShadowMomentsCubeArray);
		return false;
	}

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxShadowViews; ++Slice)
	{
		D3D11_RENDER_TARGET_VIEW_DESC CubeRTVDesc = {};
		CubeRTVDesc.Format = CubeTextureDesc.Format;
		CubeRTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		CubeRTVDesc.Texture2DArray.MipSlice = 0;
		CubeRTVDesc.Texture2DArray.FirstArraySlice = Slice;
		CubeRTVDesc.Texture2DArray.ArraySize = 1;

		if (FAILED(Device->CreateRenderTargetView(ShadowMomentsCubeArray, &CubeRTVDesc, &ShadowMomentsCubeRTVs[Slice])) || !ShadowMomentsCubeRTVs[Slice])
		{
			// 에러 발생 시 릴리즈 로직 (팀원분들의 기존 스타일 유지)
			SafeRelease(ShadowMomentsCubeArraySRV);
			for (ID3D11RenderTargetView*& RTV : ShadowMomentsCubeRTVs)
			{
				SafeRelease(RTV);
			}
			SafeRelease(ShadowMomentsCubeArray);
			bShadowDepthArrayDirty = true;
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
			EnsureShadowDepthAtlas(Renderer, RequiredResolution) &&
			EnsureShadowBuffers(Renderer, ShadowLightCount, ShadowViewCount) &&
			EnsureComparisonSampler(Renderer);

	const uint32 DirLightCount = (std::min)(static_cast<uint32>(SceneViewData.LightingInputs.DirShadowLights.size()), ShadowConfig::MaxShadowLights);
	const uint32 DirViewCount = (std::min)(static_cast<uint32>(SceneViewData.LightingInputs.DirShadowViews.size()), ShadowConfig::MaxDirCascade);

	// 태양광은 해상도를 DirShadowDepthArrayResolution(예: 4096)으로 고정!
	bool bDirOk = EnsureDirShadowDepthArray(Renderer, ShadowConfig::DirShadowDepthArrayResolution) &&
		EnsureDirShadowBuffers(Renderer, DirLightCount, DirViewCount);

	if (!bOk || !bDirOk) return false;

	if (bNeedVSM)
	{
		bOk =
				EnsureLinearSampler(Renderer) &&
				EnsureMomentsAtlas(Renderer, RequiredResolution) && 
				EnsureDirMomentsArray(Renderer, ShadowConfig::DirShadowDepthArrayResolution);

		if (!bOk)
		{
			return false;
		}
	}

	return true;
}

bool FShadowRenderFeature::EnsureShadowDepthAtlas(FRenderer& Renderer, uint32 RequiredResolution)
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

	if (LocalShadowDepthAtlas)
	{
		return true;
	}


	/////////////////////////////////////////////////////////////////////
	// Atlas
	/////////////////////////////////////////////////////////////////////


	ShadowAtlasAllocator = new FShadowAtlasAllocator(ShadowConfig::MaxShadowMapResolution);

	D3D11_TEXTURE2D_DESC AtlasTextureDesc = {};
	AtlasTextureDesc.Width = ShadowConfig::MaxShadowMapResolution;
	AtlasTextureDesc.Height = ShadowConfig::MaxShadowMapResolution;
	AtlasTextureDesc.MipLevels = 1;
	AtlasTextureDesc.ArraySize = 1;
	AtlasTextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	AtlasTextureDesc.SampleDesc.Count = 1;
	AtlasTextureDesc.SampleDesc.Quality = 0;
	AtlasTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	AtlasTextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	AtlasTextureDesc.CPUAccessFlags = 0;
	AtlasTextureDesc.MiscFlags = 0;

	if (FAILED(Device->CreateTexture2D(&AtlasTextureDesc, nullptr, &LocalShadowDepthAtlas)) || !LocalShadowDepthAtlas)
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC AtlasSRVDesc = {};
	AtlasSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	AtlasSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	AtlasSRVDesc.Texture2D.MostDetailedMip = 0;
	AtlasSRVDesc.Texture2D.MipLevels = 1;

	if (FAILED(Device->CreateShaderResourceView(LocalShadowDepthAtlas, &AtlasSRVDesc, &LocalShadowDepthAtlasSRV)) || !LocalShadowDepthAtlasSRV)
	{
		SafeRelease(LocalShadowDepthAtlas);
		return false;
	}

	D3D11_DEPTH_STENCIL_VIEW_DESC AtlasDSVDesc = {};
	AtlasDSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
	AtlasDSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	AtlasDSVDesc.Texture2D.MipSlice = 0;
	AtlasDSVDesc.Flags = 0;

	if (FAILED(Device->CreateDepthStencilView(LocalShadowDepthAtlas, &AtlasDSVDesc, &LocalShadowDepthAtlasDSV)) || !LocalShadowDepthAtlasDSV)
	{
		SafeRelease(LocalShadowDepthAtlasSRV);
		SafeRelease(LocalShadowDepthAtlas);
		return false;
	}


	/////////////////////////////////////////////////////////////////////
	// CubeArray Resources (포인트용) — 슬라이스 [PointShadowSliceOffset, ...) 영역
	/////////////////////////////////////////////////////////////////////
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
	TextureDesc.MiscFlags            = D3D11_RESOURCE_MISC_TEXTURECUBE;

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &ShadowDepthCubeArray)) || !ShadowDepthCubeArray)
	{
		bShadowDepthArrayDirty = true;
		return false;
	}


	D3D11_SHADER_RESOURCE_VIEW_DESC CubeSRVDesc      = {};
	CubeSRVDesc.Format                               = DXGI_FORMAT_R32_FLOAT;
	CubeSRVDesc.ViewDimension                        = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
	CubeSRVDesc.TextureCubeArray.MostDetailedMip     = 0;
	CubeSRVDesc.TextureCubeArray.MipLevels           = 1;
	CubeSRVDesc.TextureCubeArray.First2DArrayFace    = ShadowConfig::PointShadowSliceOffset;
	CubeSRVDesc.TextureCubeArray.NumCubes            = ShadowConfig::MaxPointShadowCubes;

	if (FAILED(Device->CreateShaderResourceView(ShadowDepthCubeArray, &CubeSRVDesc, &ShadowDepthCubeArraySRV)))
	{
		SafeRelease(ShadowDepthCubeArraySRV);
		SafeRelease(ShadowDepthCubeArray);
		bShadowDepthArrayDirty = true;
		return false;
	}

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxShadowViews; ++Slice)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
		DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
		DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		DSVDesc.Texture2DArray.MipSlice = 0;
		DSVDesc.Texture2DArray.FirstArraySlice = Slice;
		DSVDesc.Texture2DArray.ArraySize = 1;

		if (FAILED(Device->CreateDepthStencilView(ShadowDepthCubeArray, &DSVDesc, &ShadowDepthCubeDSVs[Slice])) || !ShadowDepthCubeDSVs[Slice])
		{
			SafeRelease(ShadowDepthCubeArraySRV);

			for (ID3D11DepthStencilView*& DSV : ShadowDepthCubeDSVs)
			{
				SafeRelease(DSV);
			}

			SafeRelease(ShadowDepthCubeArray);
			bShadowDepthArrayDirty = true;
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

bool FShadowRenderFeature::EnsureDirMomentsArray(const FRenderer& Renderer, uint32 RequiredResolution)
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
	for (uint32 Slice = 0; Slice < ShadowConfig::MaxDirCascade; ++Slice)
	{
		if (!DirShadowMomentsRTV[Slice] || !DirShadowMomentsBlurRTV[Slice])
		{
			bAllRTVsValid = false;
			break;
		}
	}

	bool bRecreate =
		bDirShadowDepthArrayDirty ||
		!DirShadowMomentsArray ||
		!DirShadowMomentsArraySRV ||
		!DirShadowMomentsBlur ||
		!DirShadowMomentsBlurSRV ||
		!bAllRTVsValid;

	if (!bRecreate)
	{
		D3D11_TEXTURE2D_DESC ExistingDesc = {};
		DirShadowMomentsArray->GetDesc(&ExistingDesc);

		if (ExistingDesc.Width != RequiredResolution ||
			ExistingDesc.Height != RequiredResolution ||
			ExistingDesc.ArraySize != ShadowConfig::MaxDirCascade ||
			ExistingDesc.Format != DXGI_FORMAT_R32G32_FLOAT)
		{
			bRecreate = true;
		}
	}

	if (!bRecreate)
	{
		return true;
	}

	SafeRelease(DirShadowMomentsArraySRV);
	SafeRelease(DirShadowMomentsBlurSRV);

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxDirCascade; ++Slice)
	{
		SafeRelease(DirShadowMomentsRTV[Slice]);
		SafeRelease(DirShadowMomentsBlurRTV[Slice]);
	}

	SafeRelease(DirShadowMomentsArray);
	SafeRelease(DirShadowMomentsBlur);

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = RequiredResolution;
	TextureDesc.Height = RequiredResolution;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = ShadowConfig::MaxDirCascade;
	TextureDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.SampleDesc.Quality = 0;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &DirShadowMomentsArray)) || !DirShadowMomentsArray)
	{
		return false;
	}

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &DirShadowMomentsBlur)) || !DirShadowMomentsBlur)
	{
		SafeRelease(DirShadowMomentsArray);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = TextureDesc.Format;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	SRVDesc.Texture2DArray.MostDetailedMip = 0;
	SRVDesc.Texture2DArray.MipLevels = 1;
	SRVDesc.Texture2DArray.FirstArraySlice = 0;
	SRVDesc.Texture2DArray.ArraySize = ShadowConfig::MaxDirCascade;

	if (FAILED(Device->CreateShaderResourceView(DirShadowMomentsArray, &SRVDesc, &DirShadowMomentsArraySRV)) || !DirShadowMomentsArraySRV)
	{
		SafeRelease(DirShadowDepthArray);
		SafeRelease(DirShadowMomentsBlur);
		return false;
	}

	if (FAILED(Device->CreateShaderResourceView(DirShadowMomentsBlur, &SRVDesc, &DirShadowMomentsBlurSRV)) || !DirShadowMomentsBlurSRV)
	{
		SafeRelease(DirShadowMomentsArray);
		SafeRelease(DirShadowMomentsBlur);
		return false;
	}

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxDirCascade; ++Slice)
	{
		D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
		RTVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		RTVDesc.Texture2DArray.MipSlice = 0;
		RTVDesc.Texture2DArray.FirstArraySlice = Slice;
		RTVDesc.Texture2DArray.ArraySize = 1;

		if (FAILED(Device->CreateRenderTargetView(DirShadowMomentsArray, &RTVDesc, &DirShadowMomentsRTV[Slice])) || !DirShadowMomentsRTV[Slice])
		{
			return false;
		}

		if (FAILED(Device->CreateRenderTargetView(DirShadowMomentsBlur, &RTVDesc, &DirShadowMomentsBlurRTV[Slice])) || !DirShadowMomentsBlurRTV[Slice])
		{
			return false;
		}
	}

	return true;
}

bool FShadowRenderFeature::EnsureDirShadowDepthArray(FRenderer& Renderer, uint32 RequiredResolution)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device) return false;

	bool bAllDSVsValid = true;
	for (ID3D11DepthStencilView* DSV : DirShadowViewDSVs)
	{
		if (!DSV) { bAllDSVsValid = false; break; }
	}

	bool bRecreate = bDirShadowDepthArrayDirty || !DirShadowDepthArray || !DirShadowDepthArraySRV || !bAllDSVsValid;

	if (!bRecreate) return true;

	SafeRelease(DirShadowDepthArraySRV);
	for (ID3D11DepthStencilView*& DSV : DirShadowViewDSVs) SafeRelease(DSV);
	SafeRelease(DirShadowDepthArray);

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = RequiredResolution;
	TextureDesc.Height = RequiredResolution;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = ShadowConfig::MaxDirCascade;
	TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.SampleDesc.Quality = 0;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &DirShadowDepthArray)) || !DirShadowDepthArray)
	{
		bDirShadowDepthArrayDirty = true;
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	SRVDesc.Texture2DArray.MostDetailedMip = 0;
	SRVDesc.Texture2DArray.MipLevels = 1;
	SRVDesc.Texture2DArray.FirstArraySlice = 0;
	SRVDesc.Texture2DArray.ArraySize = ShadowConfig::MaxDirCascade;

	if (FAILED(Device->CreateShaderResourceView(DirShadowDepthArray, &SRVDesc, &DirShadowDepthArraySRV)) || !DirShadowDepthArraySRV)
	{
		SafeRelease(DirShadowDepthArray);
		bDirShadowDepthArrayDirty = true;
		return false;
	}

	for (uint32 Slice = 0; Slice < ShadowConfig::MaxDirCascade; ++Slice)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
		DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
		DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		DSVDesc.Texture2DArray.MipSlice = 0;
		DSVDesc.Texture2DArray.FirstArraySlice = Slice;
		DSVDesc.Texture2DArray.ArraySize = 1;

		if (FAILED(Device->CreateDepthStencilView(DirShadowDepthArray, &DSVDesc, &DirShadowViewDSVs[Slice])) || !DirShadowViewDSVs[Slice])
		{
			SafeRelease(DirShadowDepthArraySRV);
			for (ID3D11DepthStencilView*& DSV : DirShadowViewDSVs) SafeRelease(DSV);
			SafeRelease(DirShadowDepthArray);
			bDirShadowDepthArrayDirty = true;
			return false;
		}
	}

	bDirShadowDepthArrayDirty = false;
	return true;
}

bool FShadowRenderFeature::EnsureDirShadowBuffers(FRenderer& Renderer, uint32 ShadowLightCount, uint32 ShadowViewCount)
{
	return EnsureDynamicStructuredBufferSRV(
		Renderer,
		sizeof(FShadowLightGPU),
		ShadowLightCount,
		DirShadowLightBuffer,
		DirShadowLightBufferSRV)
		&& EnsureDynamicStructuredBufferSRV(
			Renderer,
			sizeof(FShadowViewGPU),
			ShadowViewCount,
			DirShadowViewBuffer,
			DirShadowViewBufferSRV);
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
			const float CubeIndexAsFloat = (Src.CubeArrayIndex == UINT32_MAX)
				? 0.0f
				: static_cast<float>(Src.CubeArrayIndex);
			Dst.Params0          = FVector4(Src.SlopeBias, Src.NormalBias, Src.Sharpen, CubeIndexAsFloat);
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

			const float AtlasScale = ShadowConfig::MaxShadowMapResolution;
			const float TexelSize = AtlasScale > 0
				? 1.0f / static_cast<float>(AtlasScale)
				: 1.0f;

			FShadowViewGPU& Dst     = GPUData[Index];
			Dst.LightViewProjection = Src.ViewProjection.GetTransposed();
			Dst.ArraySlice          = Src.ArraySlice;
			Dst.ProjectionType      = static_cast<uint32>(Src.ProjectionType);
			Dst.FilterMode          = static_cast<uint32>(GlobalFilterMode);
			Dst.Pad0                = 0;
			Dst.ViewParams          = FVector4(Src.NearZ, Src.FarZ, AtlasScale, TexelSize);
			Dst.AtlasUV				= Src.AtlasUV;
		}

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (SUCCEEDED(DeviceContext->Map(ShadowViewBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			std::memcpy(Mapped.pData, GPUData.data(), sizeof(FShadowViewGPU) * GPUData.size());
			DeviceContext->Unmap(ShadowViewBuffer, 0);
		}
	}

	if (DirShadowLightBuffer && DirShadowViewBuffer)
	{
		const uint32 DirLightCount = (std::min)(
			static_cast<uint32>(SceneViewData.LightingInputs.DirShadowLights.size()),
			ShadowConfig::MaxDirCascade);

		const uint32 DirViewCount = (std::min)(
			static_cast<uint32>(SceneViewData.LightingInputs.DirShadowViews.size()),
			ShadowConfig::MaxDirCascade);

		{
			std::vector<FShadowLightGPU> DirGPUData;
			DirGPUData.resize((std::max)(1u, DirLightCount));

			for (uint32 Index = 0; Index < DirLightCount; ++Index)
			{
				const FShadowLightRenderItem& Src = SceneViewData.LightingInputs.DirShadowLights[Index];
				FShadowLightGPU& Dst = DirGPUData[Index];

				Dst.LightType = static_cast<uint32>(Src.LightType);
				Dst.FirstViewIndex = Src.FirstViewIndex;
				Dst.ViewCount = Src.ViewCount;
				Dst.Flags = 0;
				Dst.PositionType = FVector4(Src.PositionWS.X, Src.PositionWS.Y, Src.PositionWS.Z, 0.0f);
				Dst.DirectionBias = FVector4(Src.DirectionWS.X, Src.DirectionWS.Y, Src.DirectionWS.Z, Src.Bias);
				Dst.Params0 = FVector4(Src.SlopeBias, Src.NormalBias, Src.Sharpen, 0.0f);
			}

			D3D11_MAPPED_SUBRESOURCE Mapped = {};
			if (SUCCEEDED(DeviceContext->Map(DirShadowLightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
			{
				std::memcpy(Mapped.pData, DirGPUData.data(), sizeof(FShadowLightGPU) * DirGPUData.size());
				DeviceContext->Unmap(DirShadowLightBuffer, 0);
			}
		}

		{
			std::vector<FShadowViewGPU> DirGPUData;
			DirGPUData.resize((std::max)(1u, DirViewCount));

			for (uint32 Index = 0; Index < DirViewCount; ++Index)
			{
				const FShadowViewRenderItem& Src = SceneViewData.LightingInputs.DirShadowViews[Index];

				const float ViewportScale = 1.0f;
				const float TexelSize = 1.0f / static_cast<float>(ShadowConfig::DirShadowDepthArrayResolution);

				FShadowViewGPU& Dst = DirGPUData[Index];
				Dst.LightViewProjection = Src.ViewProjection.GetTransposed();
				Dst.ArraySlice = Src.ArraySlice;
				Dst.ProjectionType = static_cast<uint32>(Src.ProjectionType);
				Dst.FilterMode = static_cast<uint32>(GlobalFilterMode);
				Dst.Pad0 = 0;
				Dst.ViewParams = FVector4(Src.NearZ, Src.FarZ, ViewportScale, TexelSize);
				Dst.BiasParams = Src.BiasParams;
			}

			D3D11_MAPPED_SUBRESOURCE Mapped = {};
			if (SUCCEEDED(DeviceContext->Map(DirShadowViewBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
			{
				std::memcpy(Mapped.pData, DirGPUData.data(), sizeof(FShadowViewGPU) * DirGPUData.size());
				DeviceContext->Unmap(DirShadowViewBuffer, 0);
			}
		}
	}
}

void FShadowRenderFeature::RenderShadowViews(
	FRenderer&                Renderer,
	const FMeshPassProcessor& Processor,
	FSceneRenderTargets&      Targets,
	FSceneViewData&           SceneViewData)
{
	RenderDirectionalShadows(Renderer, Processor, Targets, SceneViewData);

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

	//현재 매프레임 아틀라스 배치 초기화 중. 개선 필요
	ShadowAtlasAllocator->Reset();



	for (uint32 ViewIndex = 0; ViewIndex < ShadowViewCount; ++ViewIndex)
	{
		FShadowViewRenderItem& ShadowView = SceneViewData.LightingInputs.ShadowViews[ViewIndex];

		if (ShadowView.ArraySlice >= ShadowConfig::MaxShadowViews)
		{
			continue;
		}

		D3D11_VIEWPORT ShadowViewport = {};
		const uint32 ResolvedResolution = ResolveShadowViewResolution(ShadowView.RequestedResolution);
		ID3D11DepthStencilView* ShadowDSV = nullptr;
		ID3D11RenderTargetView* MomentsRTV = nullptr;

		switch (ShadowView.LightType)
		{
		case EShadowLightType::Spot:
		{
			ShadowDSV = LocalShadowDepthAtlasDSV;
			ShadowAtlasNode* ShadowAtlasNode = ShadowAtlasAllocator->Allocate(ResolvedResolution);
			if (ShadowAtlasNode == nullptr)
			{
				continue;
			}
			ShadowView.AtlasUV = FVector(ShadowAtlasNode->X, ShadowAtlasNode->Y, ShadowAtlasNode->Size);
			ShadowViewport = BuildShadowViewport(ShadowAtlasNode->X, ShadowAtlasNode->Y, ShadowAtlasNode->Size);

			MomentsRTV = LocalShadowMomentsAtlasRTV;
			DeviceContext->ClearRenderTargetView(MomentsRTV, ClearMoments);
			DeviceContext->ClearDepthStencilView(LocalShadowDepthAtlasDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
			break;
		}
		case EShadowLightType::Point:

			ShadowDSV = ShadowDepthCubeDSVs[ShadowView.ArraySlice];
			MomentsRTV = ShadowMomentsCubeRTVs[ShadowView.ArraySlice];
			DeviceContext->ClearRenderTargetView(MomentsRTV, ClearMoments);
			DeviceContext->ClearDepthStencilView(ShadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
			ShadowViewport = BuildShadowViewport(0, 0, ShadowDepthArrayResolution);
			break;
		}

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
			if (!MomentsRTV)
			{
				continue;
			}

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

void FShadowRenderFeature::RenderDirectionalShadows(
	FRenderer& Renderer, 
	const FMeshPassProcessor& Processor, 
	FSceneRenderTargets& Targets, 
	FSceneViewData& SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	const uint32 DirShadowViewCount = (std::min)(
		static_cast<uint32>(SceneViewData.LightingInputs.DirShadowViews.size()),
		ShadowConfig::MaxDirCascade);

	if (DirShadowViewCount == 0)
	{
		return;
	}

	const FViewContext OriginalView = SceneViewData.View;
	static const float ClearMoments[4] = { 1.0f, 1.0f, 0.0f, 0.0f };

	for (uint32 ViewIndex = 0; ViewIndex < DirShadowViewCount; ++ViewIndex)
	{
		const FShadowViewRenderItem& DirShadowView = SceneViewData.LightingInputs.DirShadowViews[ViewIndex];

		if (DirShadowView.ArraySlice >= ShadowConfig::MaxDirCascade)
		{
			continue;
		}

		ID3D11DepthStencilView* DirShadowDSV = DirShadowViewDSVs[DirShadowView.ArraySlice];
		if (!DirShadowDSV)
		{
			continue;
		}

		D3D11_VIEWPORT DirShadowViewport = {};
		DirShadowViewport.TopLeftX = 0.0f;
		DirShadowViewport.TopLeftY = 0.0f;
		DirShadowViewport.Width = static_cast<float>(ShadowConfig::DirShadowDepthArrayResolution);
		DirShadowViewport.Height = static_cast<float>(ShadowConfig::DirShadowDepthArrayResolution);
		DirShadowViewport.MinDepth = 0.0f;
		DirShadowViewport.MaxDepth = 1.0f;

		SceneViewData.View.View = DirShadowView.View;
		SceneViewData.View.Projection = DirShadowView.Projection;
		SceneViewData.View.ViewProjection = DirShadowView.ViewProjection;
		SceneViewData.View.InverseView = DirShadowView.View.GetInverse();
		SceneViewData.View.InverseProjection = DirShadowView.Projection.GetInverse();
		SceneViewData.View.InverseViewProjection = DirShadowView.ViewProjection.GetInverse();
		SceneViewData.View.CameraPosition = DirShadowView.PositionWS;
		SceneViewData.View.NearZ = DirShadowView.NearZ;
		SceneViewData.View.FarZ = DirShadowView.FarZ;
		SceneViewData.View.bOrthographic = DirShadowView.ProjectionType == EShadowProjectionType::Orthographic;
		SceneViewData.View.Viewport = DirShadowViewport;

		if (GlobalFilterMode == EShadowFilterMode::Raw ||
			GlobalFilterMode == EShadowFilterMode::PCF)
		{
			DeviceContext->ClearDepthStencilView(DirShadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

			BeginPass(
				Renderer,
				0,
				nullptr,
				DirShadowDSV,
				DirShadowViewport,
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
			ID3D11RenderTargetView* DirMomentsRTV = DirShadowMomentsRTV[DirShadowView.ArraySlice];
			if (!DirMomentsRTV)
			{
				continue;
			}
			DeviceContext->ClearRenderTargetView(DirMomentsRTV, ClearMoments);
			DeviceContext->ClearDepthStencilView(DirShadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

			BeginPass(
				Renderer,
				DirMomentsRTV,
				DirShadowDSV,
				DirShadowViewport,
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

D3D11_VIEWPORT FShadowRenderFeature::BuildShadowViewport(int X, int Y, int Size) const
{
	D3D11_VIEWPORT Viewport = {};
	Viewport.TopLeftX       = static_cast<float>(X);
	Viewport.TopLeftY       = static_cast<float>(Y);
	Viewport.Width          = static_cast<float>(Size);
	Viewport.Height         = static_cast<float>(Size);
	Viewport.MinDepth       = 0.0f;
	Viewport.MaxDepth       = 1.0f;

	return Viewport;
}


bool FShadowRenderFeature::EnsureDebugPreviewResources(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	const uint32 Size = ShadowDepthArrayResolution > 0
		                    ? ShadowDepthArrayResolution
		                    : DefaultShadowMapResolution;

	bool bRecreate =
			!ShadowDebugPreviewTexture ||
			!ShadowDebugPreviewRTV ||
			!ShadowDebugPreviewSRV;

	if (!bRecreate)
	{
		D3D11_TEXTURE2D_DESC ExistingDesc = {};
		ShadowDebugPreviewTexture->GetDesc(&ExistingDesc);

		if (ExistingDesc.Width != Size ||
			ExistingDesc.Height != Size ||
			ExistingDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			bRecreate = true;
		}
	}

	if (bRecreate)
	{
		SafeRelease(ShadowDebugPreviewSRV);
		SafeRelease(ShadowDebugPreviewRTV);
		SafeRelease(ShadowDebugPreviewTexture);

		D3D11_TEXTURE2D_DESC Desc = {};
		Desc.Width                = Size;
		Desc.Height               = Size;
		Desc.MipLevels            = 1;
		Desc.ArraySize            = 1;
		Desc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.SampleDesc.Count     = 1;
		Desc.SampleDesc.Quality   = 0;
		Desc.Usage                = D3D11_USAGE_DEFAULT;
		Desc.BindFlags            = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

		if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &ShadowDebugPreviewTexture)) ||
			!ShadowDebugPreviewTexture)
		{
			return false;
		}

		if (FAILED(Device->CreateRenderTargetView(ShadowDebugPreviewTexture, nullptr, &ShadowDebugPreviewRTV)) ||
			!ShadowDebugPreviewRTV)
		{
			SafeRelease(ShadowDebugPreviewTexture);
			return false;
		}

		if (FAILED(Device->CreateShaderResourceView(ShadowDebugPreviewTexture, nullptr, &ShadowDebugPreviewSRV)) ||
			!ShadowDebugPreviewSRV)
		{
			SafeRelease(ShadowDebugPreviewRTV);
			SafeRelease(ShadowDebugPreviewTexture);
			return false;
		}
	}

	if (!ShadowDebugSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_POINT;
		SamplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.MinLOD             = 0.0f;
		SamplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;

		if (FAILED(Device->CreateSamplerState(&SamplerDesc, &ShadowDebugSampler)) ||
			!ShadowDebugSampler)
		{
			return false;
		}
	}

	if (!ShadowDebugCB)
	{
		struct FShadowDebugCBData
		{
			uint32 DebugMode;
			uint32 SliceIndex;
			float  NearZ;
			float  FarZ;

			uint32 bOrthographic;
			float  Exposure;
			float  Padding0;
			float  Padding1;
		};

		D3D11_BUFFER_DESC CBDesc = {};
		CBDesc.ByteWidth         = sizeof(FShadowDebugCBData);
		CBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		CBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		CBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;

		if (FAILED(Device->CreateBuffer(&CBDesc, nullptr, &ShadowDebugCB)) ||
			!ShadowDebugCB)
		{
			return false;
		}
	}

	const std::wstring ShaderDir = FPaths::ShaderDir().wstring();

	if (!ShadowDebugVS)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage         = EShaderStage::Vertex;
		Recipe.SourcePath    = ShaderDir + L"FinalImagePostProcess/BlitVertexShader.hlsl";
		Recipe.EntryPoint    = "main";
		Recipe.Target        = "vs_5_0";
		Recipe.LayoutType    = EVertexLayoutType::FullscreenNone;

		ShadowDebugVS = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(Device, Recipe);
	}

	if (!ShadowDebugPS)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage         = EShaderStage::Pixel;
		Recipe.SourcePath    = ShaderDir + L"Shadow/ShadowDebugPreviewPixelShader.hlsl";
		Recipe.EntryPoint    = "main";
		Recipe.Target        = "ps_5_0";

		ShadowDebugPS = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(Device, Recipe);
	}

	return ShadowDebugVS != nullptr && ShadowDebugPS != nullptr;
}

bool FShadowRenderFeature::RenderDebugPreview(
	FRenderer&            Renderer,
	FSceneRenderTargets&  Targets,
	const FSceneViewData& SceneViewData)
{
	if (DebugViewMode == EShadowDebugViewMode::None)
	{
		return true;
	}

	if (!LocalShadowDepthAtlasSRV)
	{
		return false;
	}

	if ((DebugViewMode == EShadowDebugViewMode::VSMMean ||
			DebugViewMode == EShadowDebugViewMode::VSMVariance) &&
		!LocalShadowMomentsAtlasSRV)
	{
		return false;
	}

	if (!EnsureDebugPreviewResources(Renderer))
	{
		return false;
	}

	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
	if (!Context || !ShadowDebugPreviewRTV || !ShadowDebugCB)
	{
		return false;
	}

	const uint32 ShadowViewCount = static_cast<uint32>(SceneViewData.LightingInputs.ShadowViews.size());
	if (ShadowViewCount == 0)
	{
		return false;
	}

	DebugAvailableSlices.clear();

	for (const FShadowViewRenderItem& View : SceneViewData.LightingInputs.ShadowViews)
	{
		if (View.ArraySlice < ShadowConfig::MaxShadowViews)
		{
			if (std::find(DebugAvailableSlices.begin(), DebugAvailableSlices.end(), View.ArraySlice) == DebugAvailableSlices.end())
			{
				DebugAvailableSlices.push_back(View.ArraySlice);
			}
		}
	}

	std::sort(DebugAvailableSlices.begin(), DebugAvailableSlices.end());

	if (DebugAvailableSlices.empty())
	{
		return false;
	}

	const uint32 RequestedSlice = (std::min)(DebugViewSlice, ShadowConfig::MaxShadowViews - 1u);

	const FShadowViewRenderItem* SelectedView = nullptr;
	for (const FShadowViewRenderItem& View : SceneViewData.LightingInputs.ShadowViews)
	{
		if (View.ArraySlice == RequestedSlice)
		{
			SelectedView = &View;
			break;
		}
	}

	if (!SelectedView)
	{
		DebugViewSlice = DebugAvailableSlices[0];

		for (const FShadowViewRenderItem& View : SceneViewData.LightingInputs.ShadowViews)
		{
			if (View.ArraySlice == DebugViewSlice)
			{
				SelectedView = &View;
				break;
			}
		}
	}

	if (!SelectedView)
	{
		return false;
	}

	struct FShadowDebugCBData
	{
		uint32 DebugMode;
		uint32 SliceIndex;
		float  NearZ;
		float  FarZ;

		uint32 bOrthographic;
		float  Exposure;
		float  Padding0;
		float  Padding1;
	};

	FShadowDebugCBData CBData = {};
	CBData.DebugMode          = static_cast<uint32>(DebugViewMode);
	CBData.SliceIndex         = SelectedView->ArraySlice;
	CBData.NearZ              = SelectedView->NearZ;
	CBData.FarZ               = SelectedView->FarZ;
	CBData.bOrthographic      = SelectedView->ProjectionType == EShadowProjectionType::Orthographic ? 1u : 0u;
	CBData.Exposure           = DebugVarianceExposure;

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (FAILED(Context->Map(ShadowDebugCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}

	std::memcpy(Mapped.pData, &CBData, sizeof(CBData));
	Context->Unmap(ShadowDebugCB, 0);

	const float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	Context->ClearRenderTargetView(ShadowDebugPreviewRTV, ClearColor);

	const float Size = static_cast<float>(
		ShadowDepthArrayResolution > 0
			? ShadowDepthArrayResolution
			: DefaultShadowMapResolution);

	D3D11_VIEWPORT PreviewViewport = {};
	PreviewViewport.TopLeftX       = 0.0f;
	PreviewViewport.TopLeftY       = 0.0f;
	PreviewViewport.Width          = Size;
	PreviewViewport.Height         = Size;
	PreviewViewport.MinDepth       = 0.0f;
	PreviewViewport.MaxDepth       = 1.0f;

	const FFullscreenPassConstantBufferBinding ConstantBuffers[] =
	{
		{ 0, ShadowDebugCB },
	};

	const FFullscreenPassShaderResourceBinding ShaderResources[] =
	{
		{ 0, LocalShadowDepthAtlasSRV },
		{ 1, LocalShadowMomentsAtlasSRV },
	};

	const FFullscreenPassSamplerBinding Samplers[] =
	{
		{ 0, ShadowDebugSampler },
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

	const bool bRendered = ExecuteFullscreenPass(
		Renderer,
		SceneViewData.Frame,
		SceneViewData.View,
		ShadowDebugPreviewRTV,
		nullptr,
		PreviewViewport,
		{ ShadowDebugVS, ShadowDebugPS },
		{},
		Bindings,
		[](ID3D11DeviceContext& DrawContext)
		{
			DrawContext.Draw(3, 0);
		});

	BeginPass(
		Renderer,
		Targets.SceneColorRTV,
		Targets.SceneDepthDSV,
		SceneViewData.View.Viewport,
		SceneViewData.Frame,
		SceneViewData.View);

	return bRendered;
}
