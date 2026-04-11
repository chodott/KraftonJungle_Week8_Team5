#pragma once
#include "CoreMinimal.h"
#include "EngineAPI.h"
enum class EEngineShowFlags : uint64
{
	SF_Primitives	= 1ull << 0,
	SF_UUID			= 1ull << 1,
	SF_DebugDraw	= 1ull << 2,
	SF_WorldAxis	= 1ull << 3,
	SF_Collision	= 1ull << 4,
	SF_Billboard	= 1ull << 5,
	SF_Text			= 1ull << 6,
	SF_Grid			= 1ull << 7,
	SF_Fog			= 1ull << 8,
};
class ENGINE_API FShowFlags
{
public:
	FShowFlags()
		: Flags(
			static_cast<uint64>(EEngineShowFlags::SF_Primitives) |
			static_cast<uint64>(EEngineShowFlags::SF_UUID) |
			static_cast<uint64>(EEngineShowFlags::SF_Billboard) |
			static_cast<uint64>(EEngineShowFlags::SF_Text) |
			static_cast<uint64>(EEngineShowFlags::SF_Fog))
	{
	}
	void SetFlag(EEngineShowFlags InFlag, bool bEnabled);
	bool HasFlag(EEngineShowFlags InFlag)const;
	void ToggleFlag(EEngineShowFlags InFlag);
private:
	uint64 Flags;
};