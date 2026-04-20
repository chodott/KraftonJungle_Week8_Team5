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

// Diffuse texture color
#define TextureColor (Texture.Sample(Sampler, Input.UV))

// Emissive
#define Emissive (EmissiveColor)

float4 main(VS_OUTPUT Input) : SV_TARGET
{
	float4 finalPixel = TextureColor;

	// Surface normal
#if HAS_NORMAL_MAP
	float3 N = GetNormalFromMap(Input.Normal, Input.Tangent, Input.Bitangent, Input.UV);
#else
	float3 N = normalize(Input.Normal);
#endif

	// Surface view vector (from world position to camera)
	// Use camera position from FrameData.
	float3 V = normalize(CameraPosition - Input.WorldPosition);

#if LIGHTING_MODEL_GOURAUD
	finalPixel *= Input.Color;

#elif LIGHTING_MODEL_LAMBERT
	// Diffuse only (no specular)
	float4 lighting = CalculateAmbientLight(Ambient);

	float3 L_dir = normalize(-Directional.Direction);
	float diff = max(0.0f, dot(N, L_dir));
	lighting += Directional.Color * Directional.Intensity * diff;

	[unroll]
	for (int i = 0; i < NUM_POINT_LIGHT; ++i)
	{
		float3 toLight = PointLights[i].Position - Input.WorldPosition;
		float distance = length(toLight);
		if (distance < PointLights[i].Range)
		{
			float3 L = normalize(toLight);
			float diff_p = max(0.0f, dot(N, L));
			float atten = CalculateAttenuation(distance, PointLights[i].Range);
			lighting += PointLights[i].Color * PointLights[i].Intensity * diff_p * atten;
		}
	}

	[unroll]
	for (int j = 0; j < NUM_SPOT_LIGHT; ++j)
	{
		float3 toLight = SpotLights[j].Position - Input.WorldPosition;
		float distance = length(toLight);
		if (distance < SpotLights[j].Range)
		{
			float3 L = normalize(toLight);
			float theta = dot(L, normalize(-SpotLights[j].Direction));
			float intensity = saturate(
				(theta - SpotLights[j].OuterCutoff) /
				(SpotLights[j].InnerCutoff - SpotLights[j].OuterCutoff)
			);
			float diff_s = max(0.0f, dot(N, L));
			float atten = CalculateAttenuation(distance, SpotLights[j].Range);
			lighting += SpotLights[j].Color * SpotLights[j].Intensity
				* diff_s * atten * intensity;
		}
	}

	finalPixel *= lighting;

#elif LIGHTING_MODEL_PHONG
	// Diffuse + Specular (Blinn-Phong)
	float4 lighting = CalculateAmbientLight(Ambient);
	lighting += CalculateDirectionalLight(Directional, Input.WorldPosition, N, V);

	[unroll]
	for (int i = 0; i < NUM_POINT_LIGHT; ++i)
	{
		lighting += CalculatePointLight(PointLights[i], Input.WorldPosition, N, V);
	}

	[unroll]
	for (int j = 0; j < NUM_SPOT_LIGHT; ++j)
	{
		lighting += CalculateSpotLight(SpotLights[j], Input.WorldPosition, N, V);
	}

	finalPixel *= lighting;

#endif
	return finalPixel;
}
