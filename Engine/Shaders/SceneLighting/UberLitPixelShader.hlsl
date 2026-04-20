#include "../FrameCommon.hlsli"
#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"
#include "../ShaderCommon.hlsli"
#include "../LightCommon.hlsli"

Texture2D Texture : register(t0);
SamplerState Sampler : register(s0);

#if HAS_NORMAL_MAP
Texture2D NormalMap : register(t1);
#endif

/*cbuffer MaterialData : register(b2)
{
	float4 EmissiveColor;
	float Shininess;
	float3 MaterialPadding;
};
*/

cbuffer MaterialData : register(b2)
{
	float4 ColorTint;
	float2 UVScrollSpeed;
	float2 Padding;
};

// Diffuse 텍스처 색상
#define TextureColor (Texture.Sample(Sampler, Input.UV) * ColorTint)

float4 main(VS_OUTPUT Input) : SV_TARGET
{
	if (VisualizationMode == LIGHT_VISUALIZATION_CLUSTER_HEATMAP)
	{
		return VisualizeClusterLightCulling(Input.Position, Input.WorldPosition);
	}

	float4 finalPixel = TextureColor;

	    // ── 법선 결정 ──
#if HAS_NORMAL_MAP
    float3 N = GetNormalFromMap(Input.Normal, Input.Tangent, Input.Bitangent, Input.UV);
#else
	float3 N = normalize(Input.Normal);
#endif

	float3 V = normalize(CameraPosition.xyz - Input.WorldPosition);

	float4 baseColor = TextureColor;
	
#if LIGHTING_MODEL_GOURAUD

	baseColor.rgb *= Input.VertexLighting.rgb;
	return float4(baseColor.rgb, baseColor.a);
	
#elif LIGHTING_MODEL_LAMBERT
	
	float3 lighting = 0.0f.xxx;
    
    if (AmbientEnabled != 0)
    {
        lighting += CalculateAmbientLight(Ambient);
    }
    
    if (DirectionalLightCount > 0)
    {
		float3 L_dir = normalize(-Directional.DirectionEtc.xyz);
		float diff = max(0.0f, dot(N, L_dir));
		lighting += Directional.ColorIntensity.xyz * Directional.ColorIntensity.w * diff;
    }
    
	lighting += ComputeClusteredLocalLightingLambert(Input.Position, Input.WorldPosition, N).rgb;

	baseColor.rgb *= lighting;
	return float4(baseColor.rgb, baseColor.a);
	
#elif LIGHTING_MODEL_PHONG

	float3 lighting = 0.0f.xxx;
	
    // Diffuse + Specular (Blinn-Phong)
	if (AmbientEnabled != 0)
	{
		lighting += CalculateAmbientLight(Ambient).rgb;
	}

	if (DirectionalLightCount > 0)
	{
		lighting += CalculateDirectionalLight(Directional, Input.WorldPosition, N, V).rgb;
	}

	lighting += ComputeClusteredLocalLighting(Input.Position, Input.WorldPosition, N, V).rgb;

	baseColor.rgb *= lighting;
	return float4(baseColor.rgb, baseColor.a);
	
#endif

	return baseColor;
}