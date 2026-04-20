#include "../FrameCommon.hlsli"
#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"
#include "../ShaderCommon.hlsli"
#include "../LightCommon.hlsli"

#if VERTEX_NORMAL_MAP
Texture2D NormalMap : register(t1);
SamplerState Sampler : register(s0);

float3 GetVertexNormalFromMap(float3 vertexNormal, float3 tangent, float3 bitangent, float2 uv)
{
	float3 tangentNormal = NormalMap.SampleLevel(Sampler, uv, 0.0f).rgb * 2.0f - 1.0f;

	float3 T = normalize(tangent);
	float3 B = normalize(bitangent);
	float3 N = normalize(vertexNormal);
	float3x3 TBN = float3x3(T, B, N);

	return normalize(mul(tangentNormal, TBN));
}
#endif

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
	Output.Tangent = normalize(mul(Input.Tangent.xyz, (float3x3)World));
	Output.Tangent = normalize(Output.Tangent - dot(Output.Tangent, Output.Normal) * Output.Normal);
	Output.Bitangent = cross(Output.Normal, Output.Tangent) * Input.Tangent.w;
	
	Output.VertexLighting = float4(1, 1, 1, 1);
	
	// ── Gouraud: VS에서 Blinn-Phong으로 모든 광원 계산 ──
#if LIGHTING_MODEL_GOURAUD
#if VERTEX_NORMAL_MAP
	float3 N = GetVertexNormalFromMap(Output.Normal, Output.Tangent, Output.Bitangent, Output.UV);
#else
	float3 N = Output.Normal;
#endif
	float3 V = normalize(CameraPosition.xyz - Output.WorldPosition);

	float3 lighting = float3(0, 0, 0);
	
	if (AmbientEnabled != 0)
	{
		lighting += CalculateAmbientLight(Ambient).rgb;
	}
	
	if (DirectionalLightCount > 0)
	{
		lighting += CalculateDirectionalLight(Directional, Output.WorldPosition, N, V).rgb;
	}
	
	lighting += ComputeObjectLocalLighting(LocalLightListOffset, LocalLightListCount, Output.WorldPosition, N, V).rgb;
	Output.VertexLighting = float4(lighting, 1.0f);

#elif LIGHTING_MODEL_LAMBERT

#elif LIGHTING_MODEL_PHONG

#endif
	return Output;
}
