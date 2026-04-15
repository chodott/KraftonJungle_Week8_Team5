#ifndef MESH_VERTEX_COMMON_HLSLI
#define MESH_VERTEX_COMMON_HLSLI

struct VS_INPUT
{
	float3 Position : POSITION;
	float4 Color : COLOR;
	float3 Normal : NORMAL;
	float2 UV : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR;
	float3 Normal : NORMAL;
	float2 UV : TEXCOORD0;
	float3 WorldPosition : TEXCOORD1;
};

#endif
