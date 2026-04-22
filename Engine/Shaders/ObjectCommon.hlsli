#ifndef OBJECT_COMMON_HLSLI
#define OBJECT_COMMON_HLSLI

cbuffer ObjectData : register(b1)
{
	float4x4 World;
	float4x4 WorldInvTranspose;

	uint LocalLightListOffset;
	uint LocalLightListCount;
	uint ObjectFlags;
	uint ObjectUUID;

	uint ObjectPadding0;
	uint ObjectPadding1;
	uint ObjectPadding2;
	uint ObjectPadding3;
};

#endif
