#include "../FrameCommon.hlsli"
#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"
#include "../ShaderCommon.hlsli"

cbuffer DecalMaterialData : register(b2)
{
	float4 BaseColorTint;
	float4 AtlasScaleBias;
	float3 DecalExtents;
	float DecalEdgeFade;
};

struct DECAL_VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float3 WorldPosition : TEXCOORD0;
	float3 LocalPosition : TEXCOORD1;
	float2 ProjectedUV : TEXCOORD2;
	float4 Color : COLOR;
};

DECAL_VS_OUTPUT main(VS_INPUT Input)
{
	DECAL_VS_OUTPUT Output;

	float3 LocalPosition = Input.Position * DecalExtents;
	float4 WorldPos = mul(float4(LocalPosition, 1.0f), World);
	float4 ViewPos = mul(WorldPos, View);

	Output.Position = mul(ViewPos, Projection);
	Output.WorldPosition = WorldPos.xyz;
	Output.LocalPosition = LocalPosition;
	Output.ProjectedUV = (Input.Position.yz * 0.5f + 0.5f) * AtlasScaleBias.xy + AtlasScaleBias.zw;
	Output.Color = Input.Color * BaseColorTint;

	return Output;
}
