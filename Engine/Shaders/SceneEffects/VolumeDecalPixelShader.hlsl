Texture2D DepthTexture : register(t0);
Texture2DArray DecalBaseColorTextureArray : register(t1);

SamplerState DepthSampler : register(s0);
SamplerState DecalSampler : register(s1);

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

float3 ReconstructWorldPosition(float2 UV, float Depth)
{
	const float2 NdcXY = float2(UV.x * 2.0f - 1.0f, 1.0f - UV.y * 2.0f);
	const float4 ClipPosition = float4(NdcXY, Depth, 1.0f);
	const float4 WorldPosition = mul(ClipPosition, InverseViewProjection);
	return WorldPosition.xyz / max(WorldPosition.w, 1.0e-6f);
}

float ComputeDecalFade(float3 LocalPosition, float3 Extents, float EdgeFade)
{
	const float3 SafeExtents = max(Extents, float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
	const float3 DistanceToEdge = 1.0f - abs(LocalPosition) / SafeExtents;
	return saturate(min(DistanceToEdge.x, min(DistanceToEdge.y, DistanceToEdge.z)) * max(EdgeFade, 0.0f));
}

float4 main(DECAL_VS_OUTPUT Input) : SV_TARGET
{
	const float2 InvViewportSize = max(InvViewportSizeAndAllowAngleAndTextureIndex.xy, float2(1.0e-6f, 1.0e-6f));
	const float2 UV = Input.Position.xy * InvViewportSize;
	if (any(UV < float2(0.0f, 0.0f)) || any(UV > float2(1.0f, 1.0f)))
	{
		discard;
	}

	const float Depth = DepthTexture.Sample(DepthSampler, UV).r;
	if (Depth >= 0.999999f)
	{
		discard;
	}

	const float3 WorldPosition = ReconstructWorldPosition(UV, Depth);
	const float3 LocalPosition = mul(float4(WorldPosition, 1.0f), WorldToDecal).xyz;

	const float3 DecalExtents = max(DecalExtentsAndEdgeFade.xyz, float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
	if (abs(LocalPosition.x) > DecalExtents.x ||
		abs(LocalPosition.y) > DecalExtents.y ||
		abs(LocalPosition.z) > DecalExtents.z)
	{
		discard;
	}

	const float3 ddxWorld = ddx(WorldPosition);
	const float3 ddyWorld = ddy(WorldPosition);
	const float3 SurfaceNormal = normalize(cross(ddxWorld, ddyWorld));
	const float3 DecalForward = -normalize(DecalForwardWSAndPad.xyz);
	const float AllowAngle = InvViewportSizeAndAllowAngleAndTextureIndex.z;
	if (dot(SurfaceNormal, DecalForward) < AllowAngle)
	{
		discard;
	}

	float2 DecalUV = LocalPosition.yz / (DecalExtents.yz * 2.0f) + float2(0.5f, 0.5f);
	DecalUV.y = 1.0f - DecalUV.y;
	DecalUV = DecalUV * AtlasScaleBias.xy + AtlasScaleBias.zw;
	if (any(DecalUV < float2(0.0f, 0.0f)) || any(DecalUV > float2(1.0f, 1.0f)))
	{
		discard;
	}

	const float TextureIndex = InvViewportSizeAndAllowAngleAndTextureIndex.w;
	const float4 DecalColor = DecalBaseColorTextureArray.Sample(DecalSampler, float3(DecalUV, TextureIndex)) * BaseColorTint;
	const float BlendAlpha = saturate(DecalColor.a * ComputeDecalFade(LocalPosition, DecalExtents, DecalExtentsAndEdgeFade.w));
	return float4(DecalColor.rgb, BlendAlpha);
}
