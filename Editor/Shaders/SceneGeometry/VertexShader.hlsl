#include "../FrameCommon.hlsli"
#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"

VS_OUTPUT main(VS_INPUT Input)
{
	VS_OUTPUT Output;
	float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
	float4 ViewPos = mul(WorldPos, View);
	Output.Position = mul(ViewPos, Projection);
	Output.Color = Input.Color;
	Output.Normal = mul(Input.Normal, (float3x3) World);
	Output.UV = Input.UV;
	Output.WorldPosition = WorldPos.xyz;
	return Output;
}
