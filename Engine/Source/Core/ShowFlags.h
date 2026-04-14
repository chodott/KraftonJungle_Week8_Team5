#pragma once
#include "CoreMinimal.h"
#include "EngineAPI.h"
enum class EEngineShowFlags : uint64
{
	SF_Primitives      = 1ull << 0,
	SF_UUID            = 1ull << 1,
	SF_DebugDraw       = 1ull << 2,
	SF_WorldAxis       = 1ull << 3,
	SF_Collision       = 1ull << 4,
	SF_Billboard       = 1ull << 5,
	SF_Text            = 1ull << 6,
	SF_Grid            = 1ull << 7,
	SF_Fog             = 1ull << 8,
	SF_Decal           = 1ull << 9,
	SF_FXAA            = 1ull << 10,
	SF_SceneBVH        = 1ull << 11,
	SF_MeshBVH         = 1ull << 12,
	SF_DecalDebug      = 1ull << 13,
	SF_DepthView       = 1ull << 14,
	SF_DecalArrow      = 1ull << 15,
	SF_ProjectileArrow = 1ull << 16,
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
			static_cast<uint64>(EEngineShowFlags::SF_Fog) |
			static_cast<uint64>(EEngineShowFlags::SF_Decal) |
			static_cast<uint64>(EEngineShowFlags::SF_DecalArrow) |
			static_cast<uint64>(EEngineShowFlags::SF_ProjectileArrow)
		)
	{
	}
	void SetFlag(EEngineShowFlags InFlag, bool bEnabled);
	bool HasFlag(EEngineShowFlags InFlag)const;
	void ToggleFlag(EEngineShowFlags InFlag);
private:
	uint64 Flags;
};
