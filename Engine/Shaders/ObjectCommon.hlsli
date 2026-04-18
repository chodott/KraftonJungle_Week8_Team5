#ifndef OBJECT_COMMON_HLSLI
#define OBJECT_COMMON_HLSLI

cbuffer ObjectData : register(b1)
{
	float4x4 World;
	float4x4 WorldInvTranspose;
};

#endif
