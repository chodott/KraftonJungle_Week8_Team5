#include "PointShadowPoolManager.h"

void FPointShadowPoolResource::Release()
{
	for (int i = 0; i < ShadowConfig::MaxShadowViews; ++i)
	{
		SafeRelease(DepthDSVs[i]);
		SafeRelease(MomentsRTVs[i]);

	}
	SafeRelease(DepthSRV);
	SafeRelease(DepthArray);

	SafeRelease(MomentsSRV);
	SafeRelease(MomentsArray);

	UsedSlots.clear();

	Resolution = 0;
	MaxPointLights = 0;
	MaxSlices = 0;
}

bool FPointShadowPoolResource::Create(
	ID3D11Device* Device,
	int InResolution,
	int InMaxPointLights)
{
	if (Device == nullptr)
	{
		return false;
	}

	if (InResolution <= 0 || InMaxPointLights <= 0)
	{
		return false;
	}

	const int InMaxSlices = InMaxPointLights * 6;

	if (InMaxSlices > ShadowConfig::MaxShadowViews)
	{
		return false;
	}

	Release();

	Resolution = InResolution;
	MaxPointLights = InMaxPointLights;
	MaxSlices = InMaxSlices;
	UsedSlots.assign(MaxPointLights, false);

	HRESULT Hr = S_OK;

	// Depth TextureCubeArray
	{
		D3D11_TEXTURE2D_DESC TextureDesc = {};
		TextureDesc.Width = Resolution;
		TextureDesc.Height = Resolution;
		TextureDesc.MipLevels = 1;
		TextureDesc.ArraySize = MaxSlices;
		TextureDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		TextureDesc.SampleDesc.Count = 1;
		TextureDesc.SampleDesc.Quality = 0;
		TextureDesc.Usage = D3D11_USAGE_DEFAULT;
		TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		TextureDesc.CPUAccessFlags = 0;
		TextureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

		Hr = Device->CreateTexture2D(&TextureDesc, nullptr, &DepthArray);
		if (FAILED(Hr) || DepthArray == nullptr)
		{
			Release();
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
		SRVDesc.TextureCubeArray.MostDetailedMip = 0;
		SRVDesc.TextureCubeArray.MipLevels = 1;
		SRVDesc.TextureCubeArray.First2DArrayFace = 0;
		SRVDesc.TextureCubeArray.NumCubes = MaxPointLights;

		Hr = Device->CreateShaderResourceView(DepthArray, &SRVDesc, &DepthSRV);
		if (FAILED(Hr) || DepthSRV == nullptr)
		{
			Release();
			return false;
		}

		for (int Slice = 0; Slice < MaxSlices; ++Slice)
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
			DSVDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			DSVDesc.Texture2DArray.MipSlice = 0;
			DSVDesc.Texture2DArray.FirstArraySlice = Slice;
			DSVDesc.Texture2DArray.ArraySize = 1;

			Hr = Device->CreateDepthStencilView(DepthArray, &DSVDesc, &DepthDSVs[Slice]);
			if (FAILED(Hr) || DepthDSVs[Slice] == nullptr)
			{
				Release();
				return false;
			}
		}
	}

	// Moments TextureCubeArray
	{
		D3D11_TEXTURE2D_DESC TextureDesc = {};
		TextureDesc.Width = Resolution;
		TextureDesc.Height = Resolution;
		TextureDesc.MipLevels = 1;
		TextureDesc.ArraySize = MaxSlices;
		TextureDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		TextureDesc.SampleDesc.Count = 1;
		TextureDesc.SampleDesc.Quality = 0;
		TextureDesc.Usage = D3D11_USAGE_DEFAULT;
		TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		TextureDesc.CPUAccessFlags = 0;
		TextureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

		Hr = Device->CreateTexture2D(&TextureDesc, nullptr, &MomentsArray);
		if (FAILED(Hr) || MomentsArray == nullptr)
		{
			Release();
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
		SRVDesc.TextureCubeArray.MostDetailedMip = 0;
		SRVDesc.TextureCubeArray.MipLevels = 1;
		SRVDesc.TextureCubeArray.First2DArrayFace = 0;
		SRVDesc.TextureCubeArray.NumCubes = MaxPointLights;

		Hr = Device->CreateShaderResourceView(MomentsArray, &SRVDesc, &MomentsSRV);
		if (FAILED(Hr) || MomentsSRV == nullptr)
		{
			Release();
			return false;
		}

		for (int Slice = 0; Slice < MaxSlices; ++Slice)
		{
			D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
			RTVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
			RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			RTVDesc.Texture2DArray.MipSlice = 0;
			RTVDesc.Texture2DArray.FirstArraySlice = Slice;
			RTVDesc.Texture2DArray.ArraySize = 1;

			Hr = Device->CreateRenderTargetView(MomentsArray, &RTVDesc, &MomentsRTVs[Slice]);
			if (FAILED(Hr) || MomentsRTVs[Slice] == nullptr)
			{
				Release();
				return false;
			}
		}
	}

	return true;
}

void FPointShadowPoolResource::ResetFrame()
{
	std::fill(UsedSlots.begin(), UsedSlots.end(), false);
}

void FPointShadowPoolResource::Reset()
{
	Resolution = 0;
	MaxPointLights = 0;
	MaxSlices = 0;

	DepthArray = nullptr;
	DepthSRV = nullptr;
	MomentsArray = nullptr;
	MomentsSRV = nullptr;

	for (int i = 0; i < ShadowConfig::MaxShadowViews; ++i)
	{
		DepthDSVs[i] = nullptr;
		MomentsRTVs[i] = nullptr;
	}

	UsedSlots.clear();
}

bool PointShadowPoolManager::Initialize(ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		return false;
	}

	//Release();

	bool bOk = true;

	bOk &= Pools[(int)EShadowResolutionClass::R128].Create(Device, 128, 8);
	bOk &= Pools[(int)EShadowResolutionClass::R256].Create(Device, 256, 8);
	bOk &= Pools[(int)EShadowResolutionClass::R512].Create(Device, 512, 8);
	bOk &= Pools[(int)EShadowResolutionClass::R1024].Create(Device, 1024, 4);

	if (!bOk)
	{
		Release();
		return false;
	}

	return true;
}

void PointShadowPoolManager::Release()
{
	for (int i = 0; i < (int)EShadowResolutionClass::Count; ++i)
	{
		Pools[i].Release();
	}
}

void PointShadowPoolManager::ResetFrame()
{
	for (int i = 0; i < (int)EShadowResolutionClass::Count; ++i)
	{
		Pools[i].ResetFrame();
	}
}

FPointShadowAllocation PointShadowPoolManager::Allocate(int RequestedResolution)
{
	EShadowResolutionClass Class = SelectClass(RequestedResolution);

	FPointShadowAllocation Alloc = TryAllocate(Class);
	if (Alloc.bValid)
	{
		return Alloc;
	}

	// fallback: 높은 해상도 풀이 꽉 차면 낮은 해상도 풀로 강등
	if (Class == EShadowResolutionClass::R1024)
	{
		Alloc = TryAllocate(EShadowResolutionClass::R512);
		if (Alloc.bValid)
		{
			return Alloc;
		}

		Alloc = TryAllocate(EShadowResolutionClass::R256);
		if (Alloc.bValid)
		{
			return Alloc;
		}

		Alloc = TryAllocate(EShadowResolutionClass::R128);
		if (Alloc.bValid)
		{
			return Alloc;
		}
	}
	else if (Class == EShadowResolutionClass::R512)
	{
		Alloc = TryAllocate(EShadowResolutionClass::R256);
		if (Alloc.bValid)
		{
			return Alloc;
		}

		Alloc = TryAllocate(EShadowResolutionClass::R128);
		if (Alloc.bValid)
		{
			return Alloc;
		}
	}
	else if (Class == EShadowResolutionClass::R256)
	{
		Alloc = TryAllocate(EShadowResolutionClass::R128);
		if (Alloc.bValid)
		{
			return Alloc;
		}
	}

	return {};
}

FPointShadowAllocation PointShadowPoolManager::TryAllocate(EShadowResolutionClass Class)
{
	FPointShadowPoolResource& Pool = Pools[(int)Class];

	for (int SlotIndex = 0; SlotIndex < Pool.MaxPointLights; ++SlotIndex)
	{
		if (!Pool.UsedSlots[SlotIndex])
		{
			Pool.UsedSlots[SlotIndex] = true;

			FPointShadowAllocation Alloc;
			Alloc.bValid = true;
			Alloc.PoolClass = Class;
			Alloc.SlotIndex = SlotIndex;
			Alloc.BaseSlice = SlotIndex * 6;
			Alloc.Resolution = Pool.Resolution;

			return Alloc;
		}
	}

	return {};
}

EShadowResolutionClass PointShadowPoolManager::SelectClass(int RequestedResolution) const
{
	if (RequestedResolution <= 128)
	{
		return EShadowResolutionClass::R128;
	}

	if (RequestedResolution <= 256)
	{
		return EShadowResolutionClass::R256;
	}

	if (RequestedResolution <= 512)
	{
		return EShadowResolutionClass::R512;
	}

	return EShadowResolutionClass::R1024;
}
