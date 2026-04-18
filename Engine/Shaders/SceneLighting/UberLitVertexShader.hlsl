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
	
	// ── Gouraud: VS에서 Blinn-Phong으로 모든 광원 계산 ──
#if LIGHTING_MODEL_GOURAUD
	float3 N = Output.Normal;
	float3 cameraPos = float3(
        View._41, // View 행렬의 이동 성분
        View._42,
        View._43
    );
	float3 V = normalize(cameraPos - Output.WorldPosition);

    float4 lighting = CalculateAmbientLight(Ambient);
    lighting += CalculateDirectionalLight(Directional, Output.WorldPosition, N, V);

    [unroll]
    for (int i = 0; i < NUM_POINT_LIGHT; ++i)
        lighting += CalculatePointLight(PointLights[i], Output.WorldPosition, N, V);

    [unroll]
    for (int j = 0; j < NUM_SPOT_LIGHT; ++j)
        lighting += CalculateSpotLight(SpotLights[j], Output.WorldPosition, N, V);

    Output.Color *= lighting;

#elif LIGHTING_MODEL_LAMBERT

#elif LIGHTING_MODEL_PHONG

#endif
	return Output;
}