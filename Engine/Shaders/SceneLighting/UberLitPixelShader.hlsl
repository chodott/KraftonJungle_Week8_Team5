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
	float4 finalPixel = TextureColor;

	    // ── 법선 결정 ──
#if HAS_NORMAL_MAP
    float3 N = GetNormalFromMap(Input.Normal, Input.Tangent, Input.Bitangent, Input.UV);
#else
	float3 N = normalize(Input.Normal);
#endif

	float3 V = normalize(CameraPosition.xyz - Input.WorldPosition);
	
#if LIGHTING_MODEL_GOURAUD
	finalPixel *= Input.VertexLighting;
	
#elif LIGHTING_MODEL_LAMBERT
	
	float4 lighting = 0.0f.xxxx;
    
    if (AmbientEnabled != 0)
    {
        lighting += CalculateAmbientLight(Ambient);
    }
    
    if (DirectionalLightCount > 0)
    {
        float3 L_dir = normalize(-Directional.DirectionEtc.xyz);
        float diff = max(0.0f, dot(N, L_dir));
        lighting += float4(Directional.ColorIntensity.xyz * Directional.ColorIntensity.w * diff, 1.0f);
    }
    
    lighting += ComputeClusteredLocalLightingLambert(Input.Position, Input.WorldPosition, N);
    
    finalPixel *= lighting;

#elif LIGHTING_MODEL_PHONG

	float4 lighting = 0.0f.xxxx;
	
    // Diffuse + Specular (Blinn-Phong)
    if (AmbientEnabled != 0)
    {
	    lighting += CalculateAmbientLight(Ambient);
    }
    
    if (DirectionalLightCount > 0)
    {
		lighting += CalculateDirectionalLight(Directional, Input.WorldPosition, N, V);
    }
    
    lighting += ComputeClusteredLocalLighting(Input.Position, Input.WorldPosition, N, V);

    finalPixel *= lighting;
	
#endif
	return finalPixel;
}