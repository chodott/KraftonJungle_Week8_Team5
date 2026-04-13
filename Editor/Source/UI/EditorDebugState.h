#pragma once

#include <cstdint>

enum class EStatDisplayMode : uint8_t
{
	None,
	Memory,
	Decal,
};

struct FDebugState
{
	bool FPS = false;
	bool FPSShowing = false;
	EStatDisplayMode StatDisplayMode = EStatDisplayMode::None;
};
