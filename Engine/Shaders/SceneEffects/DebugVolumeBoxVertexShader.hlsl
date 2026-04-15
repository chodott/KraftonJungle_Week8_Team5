#include "../FrameCommon.hlsli"
#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"

cbuffer DecalMaterialData : register(b2)
{
	float4 BaseColorTint;
	float4 AtlasScaleBias;
	float3 DecalExtents;
	float DecalEdgeFade;
};

struct DEBUG_BOX_VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR;
};

DEBUG_BOX_VS_OUTPUT main(VS_INPUT Input)
{
	DEBUG_BOX_VS_OUTPUT Output;

	float3 LocalPosition = Input.Position * DecalExtents;
	float4 WorldPos = mul(float4(LocalPosition, 1.0f), World);
	float4 ViewPos = mul(WorldPos, View);

	Output.Position = mul(ViewPos, Projection);
	Output.Color = BaseColorTint;
	return Output;
}
