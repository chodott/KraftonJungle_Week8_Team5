#pragma once

#include "CoreMinimal.h"
#include "ShadowTypes.h"
#include <d3d11.h>

enum class EShadowResolutionClass
{
	R128,
	R256,
	R512,
	R1024,
	Count
};

struct FPointShadowAllocation
{
	bool bValid = false;

	EShadowResolutionClass PoolClass;
	int SlotIndex = -1;
	int BaseSlice = -1;
	int Resolution = 0;
};

struct FPointShadowPoolResource
{
	FPointShadowPoolResource()
	{
		Reset();
	}

	~FPointShadowPoolResource()
	{
		Release();
	}

	int Resolution = 0;
	int MaxPointLights = 0;
	int MaxSlices = 0; // MaxPointLights * 6

	ID3D11Texture2D* DepthArray = nullptr;
	ID3D11ShaderResourceView* DepthSRV = nullptr;
	ID3D11DepthStencilView* DepthDSVs[ShadowConfig::MaxShadowViews];

	ID3D11Texture2D* MomentsArray = nullptr;
	ID3D11ShaderResourceView* MomentsSRV = nullptr;
	ID3D11RenderTargetView* MomentsRTVs[ShadowConfig::MaxShadowViews];

	std::vector<bool> UsedSlots;

	template <typename T>
	static void SafeRelease(T*& Ptr)
	{
		if (Ptr != nullptr)
		{
			Ptr->Release();
			Ptr = nullptr;
		}
	}

	void Release();
	bool Create(ID3D11Device* Device, int InResolution, int InMaxPointLights);
	void ResetFrame();
	void Reset();
};

class PointShadowPoolManager
{
public:
	FPointShadowAllocation Allocate(int RequestedResolution);

	void ResetFrame();
	bool Initialize(ID3D11Device* Device);
	void Release();
	int GetSlice(const FPointShadowAllocation& Alloc, int FaceIndex) const
	{
		return Alloc.BaseSlice + FaceIndex;
	}

	FPointShadowPoolResource& GetPool(EShadowResolutionClass Class)
	{
		return Pools[(int)Class];
	}

	const FPointShadowPoolResource& GetPool(EShadowResolutionClass Class) const
	{
		return Pools[(int)Class];
	}

private:
	FPointShadowAllocation TryAllocate(EShadowResolutionClass Class);
	EShadowResolutionClass SelectClass(int RequestedResolution) const;

private:
	FPointShadowPoolResource Pools[(int)EShadowResolutionClass::Count];
};

