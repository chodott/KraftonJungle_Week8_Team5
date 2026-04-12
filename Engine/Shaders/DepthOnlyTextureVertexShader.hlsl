#include "FrameCommon.hlsli"
#include "ObjectCommon.hlsli"
#include "MeshVertexCommon.hlsli"

struct DEPTH_ONLY_VS_OUTPUT
{
	float4 Position : SV_POSITION;
};

DEPTH_ONLY_VS_OUTPUT main(VS_INPUT Input)
{
	DEPTH_ONLY_VS_OUTPUT Output;
	float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
	float4 ViewPos = mul(WorldPos, View);
	Output.Position = mul(ViewPos, Projection);
	return Output;
}
