#ifndef LIGHT_COMMON_HLSLI
#define LIGHT_COMMON_HLSLI

#define LIGHT_CLASS_POINT 0
#define LIGHT_CLASS_SPOT 1
#define LIGHT_CLASS_RECT 2
#define LIGHT_CLASS_TUBE 3
#define LIGHT_CLASS_CUSTOM 4

#define CULL_SHAPE_SPHERE 0
#define CULL_SHAPE_CONE 1
#define CULL_SHAPE_OBB 2
#define CULL_SHAPE_CAPSULE 3
#define CULL_SHAPE_CUSTOM 4

#define MAX_LOCAL_LIGHTS 1024
#define MAX_LIGHTS_PER_CLUSTER 1024
#define LIGHT_VISUALIZATION_NONE 0
#define LIGHT_VISUALIZATION_CLUSTER_HEATMAP 1

struct FAmbientLightInfo
{
	float4 ColorIntensity;
};

struct FDirectionalLightInfo
{
	float4 ColorIntensity;
	float4 DirectionEtc;
};

cbuffer Lighting : register(b4)
{
	FAmbientLightInfo Ambient;
	FDirectionalLightInfo Directional;
	uint AmbientEnabled;
	uint DirectionalLightCount;
	uint LightingPad0;
	uint LightingPad1;
};

struct FLocalLightGPU
{
	float4 ColorIntensity;
	float4 PositionRange;
	float4 DirectionType;
	float4 AngleParams;

	float4 Axis0Extent;
	float4 Axis1Extent;
	float4 Axis2Extent;

	uint Flags;
	uint ShadowIndex;
	uint CookieIndex;
	uint IESIndex;
};

struct FLightCullProxyGPU
{
	float4 CullCenterRadius;

	float4 PositionRange;
	float4 DirectionType;
	float4 AngleParams;

	float4 Axis0Extent;
	float4 Axis1Extent;
	float4 Axis2Extent;

	uint Flags;
	uint LightIndex;
	uint CullShapeType;
	uint Reserved;
};

struct FLightClusterHeader
{
	uint Offset;
	uint Count;
	uint RawCount;
	uint Pad1;
};

struct FTileDepthBoundsGPU
{
	float MinViewZ;
	float MaxViewZ;
	uint TileMinSlice;
	uint TileMaxSlice;
	uint HasGeometry;
	uint Pad0;
	uint Pad1;
	uint Pad2;
};

cbuffer LightClusterGlobals : register(b8)
{
	float4x4 ClusterView;
	float4x4 ClusterProjection;
	float4x4 ClusterInverseProjection;
	float4x4 ClusterInverseView;

	float4 ClusterCameraPosition;
	float4 ScreenParams;

	uint ClusterCountX;
	uint ClusterCountY;
	uint ClusterCountZ;
	uint LocalLightCount;

	uint DirectionalLightCount2;
	uint RuntimeMaxLightsPerCluster;
	uint LightingEnabled;
	uint VisualizationMode;

	float NearZ;
	float FarZ;
	float LogZScale;
	float LogZBias;
};

StructuredBuffer<FLightClusterHeader> ClusterLightHeaders : register(t10);
StructuredBuffer<uint>                ClusterLightIndices : register(t11);
StructuredBuffer<FLocalLightGPU>      LocalLights         : register(t12);
StructuredBuffer<uint>                ObjectLightIndices  : register(t13);

float CalculateAttenuation(float distance, float range)
{
	float safeRange = max(range, 1.0e-4f);
	float d = max(distance, 0.0f);
	float distanceSq = d * d;
	float invRangeSq = 1.0f / (safeRange * safeRange);

	// UE ?????radius window: (1 - (d/r)^4)^2
	float x = distanceSq * invRangeSq; // (d/r)^2
	float rangeMask = saturate(1.0f - x * x);
	rangeMask *= rangeMask;

	// 域뱀눊援끿뵳??⑥눖?귞빊?獄쎻뫗?
	const float MinDistanceSq = 0.25f;
	float inverseSquare = 1.0f / max(distanceSq, MinDistanceSq);

	return rangeMask * inverseSquare;
}

#if HAS_NORMAL_MAP
float3 GetNormalFromMap(
	Texture2D normalMapTexture,
	SamplerState normalMapSampler,
	float3 vertexNormal,
	float3 tangent,
	float3 bitangent,
	float2 uv)
{
    float3 tangentNormal = normalMapTexture.Sample(normalMapSampler, uv).rgb * 2.0f - 1.0f;

    float3 T = normalize(tangent);
    float3 B = normalize(bitangent);
    float3 N = normalize(vertexNormal);
    float3x3 TBN = float3x3(T, B, N);

    return normalize(mul(tangentNormal, TBN));
}
#endif

float4 CalculateAmbientLight(FAmbientLightInfo info)
{
	return float4(info.ColorIntensity.xyz * info.ColorIntensity.w, 1.0f);
}

float4 CalculateDirectionalLight(FDirectionalLightInfo info,
                                 float3 worldPos, float3 N, float3 V)
{
	float3 L = normalize(-info.DirectionEtc.xyz);
	float3 H = normalize(L + V);

	float diff = max(0.0f, dot(N, L));
	float Shininess = 32.0f;
	float spec = pow(max(0.0f, dot(N, H)), Shininess);

    float3 diffuse = info.ColorIntensity.xyz * info.ColorIntensity.w * diff;
    float3 specular = info.ColorIntensity.xyz * info.ColorIntensity.w * spec;

    return float4(diffuse + specular, 1.0f);
}

float4 CalculatePointLight(FLocalLightGPU info,
                           float3 worldPos, float3 N, float3 V)
{
	float3 toLight = info.PositionRange.xyz - worldPos;
	float distance = length(toLight);

	if (distance > info.PositionRange.w)
		return float4(0, 0, 0, 0);

	float3 L = normalize(toLight);
	float3 H = normalize(L + V);

	float diff = max(0.0f, dot(N, L));
	float Shininess = 32.0f;
	float spec = pow(max(0.0f, dot(N, H)), Shininess);
	float attenuation = CalculateAttenuation(distance, info.PositionRange.w);

    float3 diffuse = info.ColorIntensity.xyz * info.ColorIntensity.w * diff * attenuation;
    float3 specular = info.ColorIntensity.xyz * info.ColorIntensity.w * spec * attenuation;

    return float4(diffuse + specular, 1.0f);
}

float4 CalculateSpotLight(FLocalLightGPU info,
                          float3 worldPos, float3 N, float3 V)
{
	float3 toLight = info.PositionRange.xyz - worldPos;
	float distance = length(toLight);

	if (distance > info.PositionRange.w)
		return float4(0, 0, 0, 0);

	float3 L = normalize(toLight);
	float3 H = normalize(L + V);

	float theta = dot(L, normalize(-info.DirectionType.xyz));
	float innerCutoff = info.AngleParams.x;
	float outerCutoff = info.AngleParams.y;
	float intensity = saturate((theta - outerCutoff) / max(innerCutoff - outerCutoff, 1.0e-5f));

	if (intensity <= 0.0f)
		return float4(0, 0, 0, 0);

	float diff = max(0.0f, dot(N, L));
	float Shininess = 32.0f;
	float spec = pow(max(0.0f, dot(N, H)), Shininess);
	float attenuation = CalculateAttenuation(distance, info.PositionRange.w);

    float3 diffuse = info.ColorIntensity.xyz * info.ColorIntensity.w * diff * attenuation * intensity;
    float3 specular = info.ColorIntensity.xyz * info.ColorIntensity.w * spec * attenuation * intensity;

    return float4(diffuse + specular, 1.0f);
}

float4 ComputeLocalLight(FLocalLightGPU light, float3 worldPos, float3 N, float3 V)
{
	uint lightClass = (uint)light.DirectionType.w;

	switch (lightClass)
	{
	case LIGHT_CLASS_POINT: return CalculatePointLight(light, worldPos, N, V);
	case LIGHT_CLASS_SPOT: return CalculateSpotLight(light, worldPos, N, V);
	default: return 0.0f.xxxx;
	}
}

void ComputeLocalLightContributions(
	FLocalLightGPU light,
	float3 worldPos,
	float3 N,
	float3 V,
	out float3 totalLighting,
	out float3 diffuseLighting)
{
	totalLighting = 0.0f.xxx;
	diffuseLighting = 0.0f.xxx;

	float3 toLight = light.PositionRange.xyz - worldPos;
	float distance = length(toLight);
	if (distance > light.PositionRange.w)
	{
		return;
	}

	float3 L = toLight / max(distance, 1.0e-5f);
	float attenuation = CalculateAttenuation(distance, light.PositionRange.w);
	float intensity = 1.0f;

	const uint lightClass = (uint)light.DirectionType.w;
	if (lightClass == LIGHT_CLASS_SPOT)
	{
		float theta = dot(L, normalize(-light.DirectionType.xyz));
		float innerCutoff = light.AngleParams.x;
		float outerCutoff = light.AngleParams.y;
		intensity = saturate((theta - outerCutoff) / max(innerCutoff - outerCutoff, 1.0e-5f));
		if (intensity <= 0.0f)
		{
			return;
		}
	}
	else if (lightClass != LIGHT_CLASS_POINT)
	{
		return;
	}

	float diff = max(dot(N, L), 0.0f);
	diffuseLighting = light.ColorIntensity.xyz * light.ColorIntensity.w * diff * attenuation * intensity;

	float3 H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0f), 32.0f);
	float3 specularLighting = light.ColorIntensity.xyz * light.ColorIntensity.w * spec * attenuation * intensity;

	totalLighting = diffuseLighting + specularLighting;
}

uint ComputeZSlice(float viewZ)
{
	float z = max(viewZ, NearZ);
	float sliceF = log(z) * LogZScale + LogZBias;
	return clamp((uint)floor(sliceF), 0, ClusterCountZ - 1);
}

uint ComputeClusterIndex(float4 svPosition, float3 worldPos)
{
	uint2 pixel = uint2(svPosition.xy);
	uint tileX = min(pixel.x / 16u, ClusterCountX - 1u);
	uint tileY = min(pixel.y / 16u, ClusterCountY - 1u);

	float3 viewPos = mul(float4(worldPos, 1.0f), ClusterView).xyz;
	float viewDepth = max(viewPos.x, NearZ);
	uint zSlice = ComputeZSlice(viewDepth);

	return zSlice * (ClusterCountX * ClusterCountY) + tileY * ClusterCountX + tileX;
}

float4 ComputeObjectLocalLighting(uint localLightListOffset, uint localLightListCount, float3 worldPos, float3 N, float3 V)
{
    float4 lighting = 0.0f.xxxx;

    [loop]
    for (uint i = 0; i < localLightListCount; ++i)
    {
        uint lightIndex = ObjectLightIndices[localLightListOffset + i];
        if (lightIndex < LocalLightCount)
        {
            lighting += ComputeLocalLight(LocalLights[lightIndex], worldPos, N, V);
        }
    }

    return lighting;
}

float4 ComputeObjectLocalLightingLambert(uint localLightListOffset, uint localLightListCount, float3 worldPos, float3 N)
{
	float4 lighting = 0.0f.xxxx;

	[loop]
	for (uint i = 0; i < localLightListCount; ++i)
	{
		uint lightIndex = ObjectLightIndices[localLightListOffset + i];
		if (lightIndex < LocalLightCount)
		{
			FLocalLightGPU light = LocalLights[lightIndex];

			float3 toLight = light.PositionRange.xyz - worldPos;
			float distance = length(toLight);

			if (distance < light.PositionRange.w)
			{
				float3 L = toLight / max(distance, 1.0e-5f);
				float diff = max(dot(N, L), 0.0f);
				float atten = CalculateAttenuation(distance, light.PositionRange.w);
				uint lightClass = (uint)light.DirectionType.w;

				if (lightClass == LIGHT_CLASS_POINT)
				{
					lighting += float4(light.ColorIntensity.xyz * light.ColorIntensity.w * diff * atten, 1.0f);
				}
				else if (lightClass == LIGHT_CLASS_SPOT)
				{
					float theta = dot(L, normalize(-light.DirectionType.xyz));
					float innerCutoff = light.AngleParams.x;
					float outerCutoff = light.AngleParams.y;
					float cone = saturate((theta - outerCutoff) / max(innerCutoff - outerCutoff, 1.0e-5f));
					lighting += float4(light.ColorIntensity.xyz * light.ColorIntensity.w * diff * atten * cone, 1.0f);
				}
			}
		}
	}

	return lighting;
}

void ComputeObjectLocalLightingContributions(
	uint localLightListOffset,
	uint localLightListCount,
	float3 worldPos,
	float3 N,
	float3 V,
	out float3 totalLighting,
	out float3 diffuseLighting)
{
	totalLighting = 0.0f.xxx;
	diffuseLighting = 0.0f.xxx;

	[loop]
	for (uint i = 0; i < localLightListCount; ++i)
	{
		uint lightIndex = ObjectLightIndices[localLightListOffset + i];
		if (lightIndex < LocalLightCount)
		{
			float3 lightTotal;
			float3 lightDiffuse;
			ComputeLocalLightContributions(LocalLights[lightIndex], worldPos, N, V, lightTotal, lightDiffuse);
			totalLighting += lightTotal;
			diffuseLighting += lightDiffuse;
		}
	}
}

FLightClusterHeader GetClusterLightHeader(float4 svPosition, float3 worldPos)
{
	uint clusterIndex = ComputeClusterIndex(svPosition, worldPos);
	return ClusterLightHeaders[clusterIndex];
}

float4 ComputeClusteredLocalLighting(float4 svPosition, float3 worldPos, float3 N, float3 V)
{
	if (LightingEnabled == 0 || LocalLightCount == 0)
		return 0.0f.xxxx;

	FLightClusterHeader header = GetClusterLightHeader(svPosition, worldPos);
	float4 lighting = 0.0f.xxxx;

	[loop]
	for (uint i = 0; i < header.Count; ++i)
	{
		uint lightIndex = ClusterLightIndices[header.Offset + i];
		if (lightIndex < LocalLightCount)
		{
			lighting += ComputeLocalLight(LocalLights[lightIndex], worldPos, N, V);
		}
	}

	return lighting;
}

void ComputeClusteredLocalLightingContributions(
	float4 svPosition,
	float3 worldPos,
	float3 N,
	float3 V,
	out float3 totalLighting,
	out float3 diffuseLighting)
{
	totalLighting = 0.0f.xxx;
	diffuseLighting = 0.0f.xxx;

	if (LightingEnabled == 0 || LocalLightCount == 0)
	{
		return;
	}

	FLightClusterHeader header = GetClusterLightHeader(svPosition, worldPos);

	[loop]
	for (uint i = 0; i < header.Count; ++i)
	{
		uint lightIndex = ClusterLightIndices[header.Offset + i];
		if (lightIndex < LocalLightCount)
		{
			float3 lightTotal;
			float3 lightDiffuse;
			ComputeLocalLightContributions(LocalLights[lightIndex], worldPos, N, V, lightTotal, lightDiffuse);
			totalLighting += lightTotal;
			diffuseLighting += lightDiffuse;
		}
	}
}

float4 ComputeClusteredLocalLightingLambert(float4 svPosition, float3 worldPos, float3 N)
{
    if (LightingEnabled == 0 || LocalLightCount == 0)
        return 0.0f.xxxx;

    float4 lighting = 0.0f.xxxx;
    FLightClusterHeader header = GetClusterLightHeader(svPosition, worldPos);

    [loop]
    for (uint i = 0; i < header.Count; ++i)
    {
        uint lightIndex = ClusterLightIndices[header.Offset + i];
        if (lightIndex < LocalLightCount)
        {
            FLocalLightGPU light = LocalLights[lightIndex];

            float3 toLight = light.PositionRange.xyz - worldPos;
            float distance = length(toLight);

            if (distance < light.PositionRange.w)
            {
                float3 L = toLight / max(distance, 1.0e-5f);
                float diff = max(dot(N, L), 0.0f);
	                float atten = CalculateAttenuation(distance, light.PositionRange.w);

                uint lightClass = (uint)light.DirectionType.w;

                if (lightClass == LIGHT_CLASS_POINT)
                {
                    lighting += float4(light.ColorIntensity.xyz * light.ColorIntensity.w * diff * atten, 1.0f);
                }
                else if (lightClass == LIGHT_CLASS_SPOT)
                {
                    float theta = dot(L, normalize(-light.DirectionType.xyz));
                    float innerCutoff = light.AngleParams.x;
                    float outerCutoff = light.AngleParams.y;
                    float cone = saturate((theta - outerCutoff) / max(innerCutoff - outerCutoff, 1.0e-5f));

                    lighting += float4(light.ColorIntensity.xyz * light.ColorIntensity.w * diff * atten * cone, 1.0f);
                }
            }
        }
    }

    return lighting;
}

float3 HeatmapColor(float t)
{
    t = saturate(t);

    const float3 c0 = float3(0.0f, 0.0f, 0.0f);
    const float3 c1 = float3(0.0f, 0.0f, 1.0f);
    const float3 c2 = float3(0.0f, 1.0f, 1.0f);
    const float3 c3 = float3(0.0f, 1.0f, 0.0f);
    const float3 c4 = float3(1.0f, 1.0f, 0.0f);
    const float3 c5 = float3(1.0f, 0.0f, 0.0f);

    if (t < 0.2f) return lerp(c0, c1, t / 0.2f);
    if (t < 0.4f) return lerp(c1, c2, (t - 0.2f) / 0.2f);
    if (t < 0.6f) return lerp(c2, c3, (t - 0.4f) / 0.2f);
    if (t < 0.8f) return lerp(c3, c4, (t - 0.6f) / 0.2f);
    return lerp(c4, c5, (t - 0.8f) / 0.2f);
}

float4 VisualizeClusterLightCulling(float4 svPosition, float3 worldPos)
{
    FLightClusterHeader header = GetClusterLightHeader(svPosition, worldPos);
    const float maxVisualizedLights = 16.0f;
    float normalized = saturate((float)header.RawCount / maxVisualizedLights);
    float3 color = HeatmapColor(normalized);
    return float4(color, 1.0f);
}

#endif
