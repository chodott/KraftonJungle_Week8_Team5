#include "../FrameCommon.hlsli"
#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"
#include "../ShaderCommon.hlsli"

cbuffer DecalMaterialData : register(b2)
{
	float4x4 InverseViewProjection;
	float4x4 WorldToDecal;
	float4 AtlasScaleBias;
	float4 BaseColorTint;
	float4 DecalExtentsAndEdgeFade;
	float4 InvViewportSizeAndAllowAngleAndTextureIndex;
	float4 DecalForwardWSAndPad;
};

struct DECAL_VS_OUTPUT
{
	float4 Position : SV_POSITION;
};

DECAL_VS_OUTPUT main(VS_INPUT Input)
{
	DECAL_VS_OUTPUT Output;

	const float3 DecalExtents = max(DecalExtentsAndEdgeFade.xyz, float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
	const float3 LocalPosition = Input.Position * DecalExtents;
	const float4 WorldPosition = mul(float4(LocalPosition, 1.0f), World);
	const float4 ViewPosition = mul(WorldPosition, View);
	Output.Position = mul(ViewPosition, Projection);

	return Output;
}
