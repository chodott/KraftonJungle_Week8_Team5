Texture2D DecalTexture : register(t0);
SamplerState DecalSampler : register(s0);

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

float ComputeDecalFade(float3 LocalPosition, float3 Extents, float EdgeFade)
{
	float3 SafeExtents = max(Extents, float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
	float3 DistanceToEdge = 1.0f - abs(LocalPosition) / SafeExtents;
	return saturate(min(DistanceToEdge.x, min(DistanceToEdge.y, DistanceToEdge.z)) * max(EdgeFade, 0.0f));
}

float4 main(DECAL_VS_OUTPUT Input) : SV_TARGET
{
	if (any(abs(Input.LocalPosition) > max(DecalExtents, float3(1.0e-4f, 1.0e-4f, 1.0e-4f))))
	{
		discard;
	}

	float4 DecalColor = DecalTexture.Sample(DecalSampler, Input.ProjectedUV) * Input.Color;
	//DecalColor.a *= ComputeDecalFade(Input.LocalPosition, DecalExtents, DecalEdgeFade);

	DecalColor.a = saturate(DecalColor.a);
	return DecalColor;
}
