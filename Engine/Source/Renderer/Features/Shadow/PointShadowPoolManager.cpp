#include "PointShadowPoolManager.h"

FPointShadowAllocation PointShadowPoolManager::Allocate(int RequestedResolution)
{
	EShadowResolutionClass Class = SelectClass(RequestedResolution);

	FPointShadowAllocation Alloc = TryAllocate(Class);
	if (Alloc.bValid)
	{
		return Alloc;
	}

	// 높은 해상도 풀이 꽉 찼으면 한 단계 낮춰서라도 그림자 제공
	if (Class == EShadowResolutionClass::R1024)
	{
		Alloc = TryAllocate(EShadowResolutionClass::R512);
		if (Alloc.bValid) return Alloc;

		Alloc = TryAllocate(EShadowResolutionClass::R256);
		if (Alloc.bValid) return Alloc;
	}
	else if (Class == EShadowResolutionClass::R512)
	{
		Alloc = TryAllocate(EShadowResolutionClass::R256);
		if (Alloc.bValid) return Alloc;
	}

	return {};
}

void PointShadowPoolManager::ResetFrame()
{
	for (FPointShadowPoolResource& Pool : Pools)
	{
		std::fill(Pool.UsedSlots.begin(), Pool.UsedSlots.end(), false);
	}
}

FPointShadowAllocation PointShadowPoolManager::TryAllocate(EShadowResolutionClass Class)
{
	FPointShadowPoolResource& Pool = Pools[(int)Class];

	for (int i = 0; i < Pool.MaxPointLights; ++i)
	{
		if (!Pool.UsedSlots[i])
		{
			Pool.UsedSlots[i] = true;

			FPointShadowAllocation Alloc;
			Alloc.bValid = true;
			Alloc.PoolClass = Class;
			Alloc.SlotIndex = i;
			Alloc.BaseSlice = i * 6;
			Alloc.Resolution = Pool.Resolution;
			return Alloc;
		}
	}

	return {};
}

EShadowResolutionClass PointShadowPoolManager::SelectClass(int RequestedResolution) const
{
	if (RequestedResolution <= 256)
		return EShadowResolutionClass::R256;

	if (RequestedResolution <= 512)
		return EShadowResolutionClass::R512;

	return EShadowResolutionClass::R1024;
}