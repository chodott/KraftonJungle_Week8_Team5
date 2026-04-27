#ifndef LIGHT_COMMON_HLSLI
#define LIGHT_COMMON_HLSLI

#ifndef ENABLE_SHADOWS
#define ENABLE_SHADOWS 0
#endif

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

#define INVALID_SHADOW_INDEX 0xFFFFFFFFu

#define SHADOW_LIGHT_DIRECTIONAL 0
#define SHADOW_LIGHT_SPOT        1
#define SHADOW_LIGHT_POINT       2

#define SHADOW_PROJECTION_ORTHOGRAPHIC 0
#define SHADOW_PROJECTION_PERSPECTIVE  1

struct FAmbientLightInfo
{
	float4 ColorIntensity;
};

struct FDirectionalLightInfo
{
	float4 ColorIntensity;
	float4 DirectionEtc;
};

cbuffer MaterialData : register(b2)
{
	float4 ColorTint;
	float2 UVScrollSpeed;
	float2 Padding0;
	float4 EmissiveColor;
	float Shininess;
	float3 Padding1;
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

	uint OrthographicView;
	uint RuntimeMaxLightsPerCluster;
	uint LightingEnabled;
	uint VisualizationMode;

	float NearZ;
	float FarZ;
	float LogZScale;
	float LogZBias;
};

bool IsOrthographicClusterView()
{
	return OrthographicView != 0u;
}

float LinearizeDeviceDepth(float deviceDepth)
{
	deviceDepth = saturate(deviceDepth);

	if (IsOrthographicClusterView())
	{
		return lerp(NearZ, FarZ, deviceDepth);
	}

	return (NearZ * FarZ) / max(FarZ - deviceDepth * (FarZ - NearZ), 1.0e-6f);
}

float ViewDepthToDeviceDepth(float viewDepth)
{
	float clampedViewDepth = clamp(viewDepth, NearZ, FarZ);

	if (IsOrthographicClusterView())
	{
		return saturate((clampedViewDepth - NearZ) / max(FarZ - NearZ, 1.0e-6f));
	}

	return saturate((FarZ - (NearZ * FarZ) / max(clampedViewDepth, NearZ)) / max(FarZ - NearZ, 1.0e-6f));
}

StructuredBuffer<FLightClusterHeader> ClusterLightHeaders : register(t10);
StructuredBuffer<uint>                ClusterLightIndices : register(t11);
StructuredBuffer<FLocalLightGPU>      LocalLights         : register(t12);
StructuredBuffer<uint>                ObjectLightIndices  : register(t13);

#if ENABLE_SHADOWS

struct FShadowLightGPU
{
	uint LightType;
	uint FirstViewIndex;
	uint ViewCount;
	uint Flags;

	float4 PositionType;
	float4 DirectionBias;
	float4 Params0;
};

struct FShadowViewGPU
{
	float4x4 LightViewProjection;

	uint ArraySlice;
	uint ProjectionType;
	uint FilterMode;
	uint Pad0;

	float4 ViewParams;
};

StructuredBuffer<FShadowLightGPU> ShadowLights       : register(t20);
StructuredBuffer<FShadowViewGPU>  ShadowViews        : register(t21);

Texture2DArray<float>             ShadowDepthArray   : register(t22); // PCF
Texture2DArray<float2>            ShadowMomentsArray : register(t23); // VSM
TextureCubeArray<float>		     ShadowDepthCubeArray: register(t24);// Cube Map
SamplerComparisonState            ShadowSampler      : register(s8); // PCF
SamplerState                      LinearClampSampler : register(s9); // VSM

float ComputeShadowBias(
	FShadowLightGPU shadowLight,
	float3 N,
	float3 L)
{
	float baseBias  = shadowLight.DirectionBias.w;
	float slopeBias = shadowLight.Params0.x;

	float ndotl = saturate(dot(N, L));
	return baseBias + slopeBias * (1.0f - ndotl);
}

bool ComputeShadowCoords(
	FShadowViewGPU shadowView,
	float3 worldPos,
	out float2 uv,
	out float compareDepth)
{
	float4 clip = mul(float4(worldPos, 1.0f), shadowView.LightViewProjection);

	if (clip.w <= 0.0f)
	{
		uv = 0.0f.xx;
		compareDepth = 0.0f;
		return false;
	}

	float3 ndc = clip.xyz / clip.w;

	if (ndc.x < -1.0f || ndc.x > 1.0f ||
		ndc.y < -1.0f || ndc.y > 1.0f ||
		ndc.z <  0.0f || ndc.z > 1.0f)
	{
		uv = 0.0f.xx;
		compareDepth = 0.0f;
		return false;
	}

	uv.x = ndc.x * 0.5f + 0.5f;
	uv.y = -ndc.y * 0.5f + 0.5f;

	compareDepth = saturate(ndc.z);
	return true;
}

float SampleShadowViewPCF(
	FShadowLightGPU shadowLight,
	FShadowViewGPU shadowView,
	float3 worldPos,
	float3 N,
	float3 L)
{
	float2 uv;
	float compareDepth;
	if (!ComputeShadowCoords(shadowView, worldPos, uv, compareDepth))
	{
		return 1.0f;
	}
	
	float bias = ComputeShadowBias(shadowLight, N, L);
	compareDepth = saturate(compareDepth - bias);
	
	float viewportScale = max(shadowView.ViewParams.z, 1.0e-6f);
	float2 texelSize = shadowView.ViewParams.w.xx;
	
	float2 scaledUV = uv * viewportScale;
	float2 minUV = texelSize * 0.5f;
	float2 maxUV = viewportScale.xx - texelSize * 0.5f;
	
	float visibility = 0.0f;

	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			float2 tapUV = clamp(scaledUV + float2(x, y) * texelSize, minUV, maxUV);

			visibility += ShadowDepthArray.SampleCmpLevelZero(
				ShadowSampler,
				float3(tapUV, (float)shadowView.ArraySlice),
				compareDepth);
		}
	}

	return visibility / 9.0f;
}
float SampleShadowViewPoint(FShadowLightGPU shadowLight, float3 worldPos, float3 N, float3 L) // TODO Point
{
	uint cubeIndex = (uint) shadowLight.Params0.w;
	float3 lightPos = shadowLight.PositionType.xyz;
	float3 lightToSurface = worldPos - lightPos;
	// 면판정 D3D 큐브 표준 순서와 일치해야함
	float3 absDir = abs(lightToSurface);
	uint faceIndex = 0;
	if (absDir.x >= absDir.y && absDir.x >= absDir.z)
		faceIndex = (lightToSurface.x > 0) ? 0 : 1;
	else if (absDir.y >= absDir.z)
		faceIndex = (lightToSurface.y > 0) ? 2 : 3;
	else
		faceIndex = (lightToSurface.z > 0) ? 4 : 5;
	// 그면의 ViewProjection으로  NDC 깊이 계산
	FShadowViewGPU view = ShadowViews[shadowLight.FirstViewIndex + faceIndex];
	float4 shadowPos = mul(float4(worldPos, 1.0f), view.LightViewProjection);
	shadowPos.xyz /= shadowPos.w;

	if (shadowPos.w <= 0 || shadowPos.z < 0 || shadowPos.z > 1)
		return 1.0f;
	float bias = shadowLight.DirectionBias.w;
	float rawDepth = ShadowDepthCubeArray.SampleLevel(LinearClampSampler,float4(lightToSurface, (float) cubeIndex),0.0f).r;
	
	return (shadowPos.z - bias <= rawDepth) ? 1.0f : 0.0f;
}
float ReduceLightBleeding(float pMax, float amount)
{
	return saturate((pMax - amount) / max(1.0f - amount, 1.0e-5f));
}

float SampleShadowViewVSM(
	FShadowLightGPU shadowLight,
	FShadowViewGPU shadowView,
	float3 worldPos,
	float3 N,
	float3 L)
{
	float2 uv;
	float compareDepth;
	if (!ComputeShadowCoords(shadowView, worldPos, uv, compareDepth))
	{
		return 1.0f;
	}
	
	float bias = ComputeShadowBias(shadowLight, N, L);
	compareDepth = saturate(compareDepth - bias);

	float viewportScale = max(shadowView.ViewParams.z, 1.0e-6f);
	float2 texelSize    = shadowView.ViewParams.w.xx;

	float2 scaledUV = uv * viewportScale;
	float2 minUV    = texelSize * 0.5f;
	float2 maxUV    = viewportScale.xx - texelSize * 0.5f;
	scaledUV        = clamp(scaledUV, minUV, maxUV);

	float2 moments = ShadowMomentsArray.SampleLevel(
		LinearClampSampler,
		float3(scaledUV, (float)shadowView.ArraySlice),
		0.0f
	).rg;

	float mean     = moments.x;
	float variance = moments.y - mean * mean;
	variance       = max(variance, 1.0e-6f);
	if (compareDepth <= mean)
	{
		return 1.0f;
	}

	float d    = compareDepth - mean;
	float pMax = variance / (variance + d * d);
	pMax       = ReduceLightBleeding(pMax, shadowLight.Params0.z);
	return saturate(pMax);
}

float SampleShadowViewRawDepth(
	FShadowLightGPU shadowLight,
	FShadowViewGPU shadowView,
	float3 worldPos,
	float3 N,
	float3 L)
{
	float2 uv;
	float compareDepth;
	if (!ComputeShadowCoords(shadowView, worldPos, uv, compareDepth))
	{
		return 1.0f;
	}

	float bias = ComputeShadowBias(shadowLight, N, L);
	compareDepth = saturate(compareDepth - bias);

	float viewportScale = max(shadowView.ViewParams.z, 1.0e-6f);
	float2 texelSize    = shadowView.ViewParams.w.xx;

	float2 scaledUV = uv * viewportScale;
	float2 minUV    = texelSize * 0.5f;
	float2 maxUV    = viewportScale.xx - texelSize * 0.5f;
	scaledUV        = clamp(scaledUV, minUV, maxUV);

	float rawDepth = ShadowDepthArray.SampleLevel(
		LinearClampSampler,
		float3(scaledUV, (float)shadowView.ArraySlice),
		0.0f
	).r;

	return (compareDepth <= rawDepth) ? 1.0f : 0.0f;
}

float EvaluateSpotShadow(
	FShadowLightGPU shadowLight,
	float3 worldPos,
	float3 N,
	float3 L)
{
	if (shadowLight.ViewCount == 0u)
	{
		return 1.0f;
	}

	FShadowViewGPU view = ShadowViews[shadowLight.FirstViewIndex];
	if (view.FilterMode == 0u) // Raw
	{
		return SampleShadowViewRawDepth(shadowLight, view, worldPos, N, L);
	}
	if (view.FilterMode == 1u) // PCF
	{
		return SampleShadowViewPCF(shadowLight, view, worldPos, N, L);
	}
	else // VSM
	{
		return SampleShadowViewVSM(shadowLight, view, worldPos, N, L);
	}
}

float EvaluateShadow(
	uint shadowIndex,
	uint lightClass,
	float3 worldPos,
	float3 N,
	float3 L)
{
	if (shadowIndex == INVALID_SHADOW_INDEX)
	{
		return 1.0f;
	}

	FShadowLightGPU shadowLight = ShadowLights[shadowIndex];

	if (shadowLight.LightType == SHADOW_LIGHT_SPOT && lightClass == LIGHT_CLASS_SPOT)
	{
		return EvaluateSpotShadow(shadowLight, worldPos, N, L);
	}
	if (shadowLight.LightType == SHADOW_LIGHT_POINT && lightClass == LIGHT_CLASS_POINT)
	{
		return SampleShadowViewPoint(shadowLight, worldPos, N, L);
	} 
	// TODO :  Directional
	return 1.0f;
}

#else

float EvaluateShadow(
	uint shadowIndex,
	uint lightClass,
	float3 worldPos,
	float3 N,
	float3 L)
{
	return 1.0f;
}

#endif

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

float3 BuildFallbackTangent(float3 normal)
{
	float3 referenceAxis = (abs(normal.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
	return normalize(cross(referenceAxis, normal));
}

void ReOrthonormalizeTBN(
	float3 inputNormal,
	float3 inputTangent,
	float3 inputBitangent,
	out float3 outNormal,
	out float3 outTangent,
	out float3 outBitangent)
{
	outNormal = normalize(inputNormal);

	float3 orthogonalTangent = inputTangent - outNormal * dot(inputTangent, outNormal);
	float tangentLengthSq = dot(orthogonalTangent, orthogonalTangent);
	if (tangentLengthSq > 1.0e-8f)
	{
		outTangent = orthogonalTangent * rsqrt(tangentLengthSq);
	}
	else
	{
		outTangent = BuildFallbackTangent(outNormal);
	}

	float handedness = (dot(cross(outNormal, outTangent), inputBitangent) < 0.0f) ? -1.0f : 1.0f;
	outBitangent = normalize(cross(outNormal, outTangent)) * handedness;
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

	float3 T;
	float3 B;
	float3 N;
	ReOrthonormalizeTBN(vertexNormal, tangent, bitangent, N, T, B);
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
	float spec = pow(max(0.0f, dot(N, H)), Shininess);
	float attenuation = CalculateAttenuation(distance, info.PositionRange.w);

	float shadow = EvaluateShadow(info.ShadowIndex, LIGHT_CLASS_SPOT, worldPos, N, L);

	float3 diffuse = info.ColorIntensity.xyz * info.ColorIntensity.w * diff * attenuation * intensity * shadow;
	float3 specular = info.ColorIntensity.xyz * info.ColorIntensity.w * spec * attenuation * intensity * shadow;

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
	bool applyShadow,
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
	
	float shadow = 1.0f;
	if (applyShadow)
	{
		shadow = EvaluateShadow(light.ShadowIndex, lightClass, worldPos, N, L);
	}

	float diff = max(dot(N, L), 0.0f);
	diffuseLighting = light.ColorIntensity.xyz * light.ColorIntensity.w * diff * attenuation * intensity * shadow;

	float3 H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0f), 32.0f);
	float3 specularLighting = light.ColorIntensity.xyz * light.ColorIntensity.w * spec * attenuation * intensity * shadow;

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
			ComputeLocalLightContributions(LocalLights[lightIndex], worldPos, N, V, true, lightTotal, lightDiffuse);
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
			ComputeLocalLightContributions(LocalLights[lightIndex], worldPos, N, V, true, lightTotal, lightDiffuse);
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
                    float shadow = EvaluateShadow(light.ShadowIndex, LIGHT_CLASS_SPOT, worldPos, N, L);

                    lighting += float4(light.ColorIntensity.xyz * light.ColorIntensity.w * diff * atten * cone * shadow, 1.0f);
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
