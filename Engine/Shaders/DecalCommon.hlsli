#ifndef DECAL_COMMON_HLSLI
#define DECAL_COMMON_HLSLI

#define DECAL_RENDER_FLAG_BASECOLOR 1u

struct FDecalClusterHeader
{
	uint Offset;
	uint Count;
	uint Pad0;
	uint Pad1;
};

struct FDecalGPUData
{
	float4x4 WorldToDecal;
	float4 AtlasScaleBias;
	float4 BaseColorTint;
	float3 Extents;
	uint TextureIndex;
	float3 AxisXWS;
	uint Flags;
	float3 AxisYWS;
	float NormalBlend;
	float3 AxisZWS;
	float RoughnessBlend;
	float EmissiveBlend;
	float EdgeFade;
	float AllowAngle;
	uint PadA;
};

cbuffer DecalClusterData : register(b3)
{
	uint DecalClusterCountX;
	uint DecalClusterCountY;
	uint DecalClusterCountZ;
	uint DecalMaxClusterItems;

	float DecalViewportWidth;
	float DecalViewportHeight;
	float DecalNearZ;
	float DecalFarZ;

	float DecalLogZScale;
	float DecalLogZBias;
	float DecalTileWidth;
	float DecalTileHeight;
};

StructuredBuffer<FDecalClusterHeader> DecalClusterHeaders : register(t10);
StructuredBuffer<uint> DecalClusterIndices : register(t11);
StructuredBuffer<FDecalGPUData> DecalDataBuffer : register(t12);
Texture2DArray DecalBaseColorTextureArray : register(t13);

float ComputeDecalEdgeFade(float3 LocalPosition, float3 Extents, float EdgeFade)
{
	float3 SafeExtents = max(Extents, float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
	float3 DistanceToEdge = 1.0f - abs(LocalPosition) / SafeExtents;
	float EdgeFactor = min(DistanceToEdge.x, min(DistanceToEdge.y, DistanceToEdge.z));
	return saturate(EdgeFactor * max(EdgeFade, 0.0f));
}

uint ComputeDecalClusterId(float2 PixelPosition, float ViewDepth)
{
	if (DecalClusterCountX == 0 ||
		DecalClusterCountY == 0 ||
		DecalClusterCountZ == 0 ||
		DecalTileWidth <= 0.0f ||
		DecalTileHeight <= 0.0f ||
		DecalNearZ <= 0.0f ||
		DecalFarZ <= DecalNearZ)
	{
		return 0xFFFFFFFFu;
	}

	if (ViewDepth < DecalNearZ || ViewDepth > DecalFarZ)
	{
		return 0xFFFFFFFFu;
	}

	const uint TileX = (uint) clamp(
		(int) floor(PixelPosition.x / DecalTileWidth),
		0,
		(int) DecalClusterCountX - 1);

	const uint TileY = (uint) clamp(
		(int) floor(PixelPosition.y / DecalTileHeight),
		0,
		(int) DecalClusterCountY - 1);

	const uint SliceZ = (uint) clamp(
		(int) floor(log(max(ViewDepth, DecalNearZ)) * DecalLogZScale + DecalLogZBias),
		0,
		(int) DecalClusterCountZ - 1);

	return TileX
		+ TileY * DecalClusterCountX
		+ SliceZ * DecalClusterCountX * DecalClusterCountY;
}

float4 ApplyBaseColorDecals(
	float3 WorldPosition,
	float ViewDepth,
	float2 PixelPosition,
	float4 BaseColor,
	SamplerState DecalSampler)
{
	const uint2 PixelCoord = (uint2) PixelPosition;
	const uint ClusterId = ComputeDecalClusterId(PixelPosition, ViewDepth);
	if (ClusterId == 0xFFFFFFFFu)
	{
		return BaseColor;
	}

	const FDecalClusterHeader Header = DecalClusterHeaders[ClusterId];
	if (Header.Count == 0)
	{
		return BaseColor;
	}

	// 화면공간 ddx/ddy로 표면 법선 추정 (뒷면 컬링용)
	const float3 ddxWP = ddx(WorldPosition);
	const float3 ddyWP = ddy(WorldPosition);
	const float3 SurfaceNormal = normalize(cross(ddxWP, ddyWP));

	float3 ResultColor = BaseColor.rgb;
	const uint DecalCount = min(Header.Count, DecalMaxClusterItems);

	[loop]
	for (uint DecalListIndex = 0; DecalListIndex < DecalCount; ++DecalListIndex)
	{
		const uint DecalIndex = DecalClusterIndices[Header.Offset + DecalListIndex];
		const FDecalGPUData Decal = DecalDataBuffer[DecalIndex];

		if ((Decal.Flags & DECAL_RENDER_FLAG_BASECOLOR) == 0u)
		{
			continue;
		}

		// 데칼 전진 방향(-AxisXWS)과 표면 법선이 AllowAngle(cos값) 미만이면 스킵
		const float3 DecalForward = -normalize(Decal.AxisXWS);
		if (dot(SurfaceNormal, DecalForward) < Decal.AllowAngle)
		{
			continue;
		}

		const float3 SafeExtents = max(Decal.Extents, float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
		const float3 LocalPosition = mul(float4(WorldPosition, 1.0f), Decal.WorldToDecal).xyz;
		if (abs(LocalPosition.x) > SafeExtents.x ||
			abs(LocalPosition.y) > SafeExtents.y || abs(LocalPosition.z) > SafeExtents.z)
		{
			continue;
		}
		
		float2 DecalUV = LocalPosition.yz / (SafeExtents.yz * 2.0f) + float2(0.5f, 0.5f);
		DecalUV.y = 1.0f - DecalUV.y;
		DecalUV = DecalUV * Decal.AtlasScaleBias.xy + Decal.AtlasScaleBias.zw;
		if (any(DecalUV < float2(0.0f, 0.0f)) || any(DecalUV > float2(1.0f, 1.0f)))
		{
			continue;
		}

		if (Decal.TextureIndex >= 16u)
		{
			continue;
		}

		const float4 DecalColor = DecalBaseColorTextureArray.Sample(DecalSampler, float3(DecalUV, (float)Decal.TextureIndex)) * Decal.BaseColorTint;
		const float BlendAlpha = saturate(DecalColor.a);
		ResultColor = lerp(ResultColor, DecalColor.rgb, BlendAlpha);
	}

	return float4(ResultColor, BaseColor.a);
}

#endif
