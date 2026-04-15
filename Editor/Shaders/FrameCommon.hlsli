#ifndef FRAME_COMMON_HLSLI
#define FRAME_COMMON_HLSLI

cbuffer FrameData : register(b0)
{
	float4x4 View;
	float4x4 Projection;
	float Time;
	float DeltaTime;
	float2 Framepadding;
};

#endif
