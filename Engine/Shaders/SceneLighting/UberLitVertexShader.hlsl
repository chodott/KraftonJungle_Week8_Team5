#include "../FrameCommon.hlsli"
#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"
#include "../ShaderCommon.hlsli"
#include "../LightCommon.hlsli"

VS_OUTPUT main(VS_INPUT Input)
{
	VS_OUTPUT Output;
	
	float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
	float4 ViewPos = mul(WorldPos, View);
	Output.WorldPosition = WorldPos.xyz;
	Output.Position = mul(ViewPos, Projection);
	Output.UV = Input.UV;
	Output.Color = Input.Color;
	
	Output.Normal = normalize(mul(Input.Normal, (float3x3) WorldInvTranspose));
	
	// ── Normal Map 여부에 따라 Tangent 계산 ── 
#if HAS_NORMAL_MAP
    Output.Tangent   = normalize(mul(Input.Tangent, (float3x3)World));
    Output.Bitangent = cross(Output.Normal, Output.Tangent);
#else
	Output.Tangent = float3(0, 0, 0);
	Output.Bitangent = float3(0, 0, 0);
#endif
	
	Output.VertexLighting = float4(1, 1, 1, 1);
	
	// ── Gouraud: VS에서 Blinn-Phong으로 모든 광원 계산 ──
#if LIGHTING_MODEL_GOURAUD
	float3 N = Output.Normal;
	float3 V = normalize(CameraPosition.xyz - Output.WorldPosition);

	float4 lighting = float4(0, 0, 0, 0);
	
	if (AmbientEnabled != 0)
	{
		lighting += CalculateAmbientLight(Ambient);
	}
	
	if (DirectionalLightCount > 0)
	{
		lighting += CalculateDirectionalLight(Directional, Output.WorldPosition, N, V);
	}
	
	lighting += ComputeObjectLocalLighting(Output.WorldPosition, N, V);
	Output.VertexLighting = lighting;

#elif LIGHTING_MODEL_LAMBERT

#elif LIGHTING_MODEL_PHONG

#endif
	return Output;
}