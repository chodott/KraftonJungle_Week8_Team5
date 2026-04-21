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

	float3 T;
	float3 B;
	float3 N;
	ReOrthonormalizeTBN(vertexNormal, tangent, bitangent, N, T, B);
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
	float3 WorldTangent = mul(Input.Tangent.xyz, (float3x3)World);
	float3 WorldBitangent = cross(Output.Normal, WorldTangent) * Input.Tangent.w;
	ReOrthonormalizeTBN(Output.Normal, WorldTangent, WorldBitangent, Output.Normal, Output.Tangent, Output.Bitangent);
	
	Output.VertexLighting = float4(1, 1, 1, 1);
	Output.VertexSpecular = 0.0f.xxx;
	
	// ── Gouraud: VS에서 Blinn-Phong으로 모든 광원 계산 ──
#if LIGHTING_MODEL_GOURAUD
#if VERTEX_NORMAL_MAP
	float3 N = GetVertexNormalFromMap(Output.Normal, Output.Tangent, Output.Bitangent, Output.UV);
#else
	float3 N = Output.Normal;
#endif
	float3 V = normalize(CameraPosition.xyz - Output.WorldPosition);

	float3 totalLighting = float3(0, 0, 0);
	float3 diffuseLighting = float3(0, 0, 0);
	
	if (AmbientEnabled != 0)
	{
		float3 ambient = CalculateAmbientLight(Ambient).rgb;
		totalLighting += ambient;
		diffuseLighting += ambient;
	}
	
	if (DirectionalLightCount > 0)
	{
		float3 L_dir = normalize(-Directional.DirectionEtc.xyz);
		float diff = max(0.0f, dot(N, L_dir));
		float3 dirDiffuse = Directional.ColorIntensity.xyz * Directional.ColorIntensity.w * diff;
		diffuseLighting += dirDiffuse;
		totalLighting += CalculateDirectionalLight(Directional, Output.WorldPosition, N, V).rgb;
	}
	
	totalLighting += ComputeObjectLocalLighting(LocalLightListOffset, LocalLightListCount, Output.WorldPosition, N, V).rgb;
	diffuseLighting += ComputeObjectLocalLightingLambert(LocalLightListOffset, LocalLightListCount, Output.WorldPosition, N).rgb;

	float3 specularLighting = max(totalLighting - diffuseLighting, 0.0f.xxx);
	Output.VertexLighting = float4(diffuseLighting, 1.0f);
	Output.VertexSpecular = specularLighting;

#elif LIGHTING_MODEL_LAMBERT

#elif LIGHTING_MODEL_PHONG

#endif
	return Output;
}
