Texture2D<float4> InputTex : register(t0);
RWTexture2D<float4> OutputTex : register(u0);

cbuffer BlurParams : register(b0)
{
	float2 TexelSize;
	float2 Pad;
};

#define TILE_SIZE     16
#define KERNEL_RADIUS 4
#define SHARED_SIZE   (TILE_SIZE + KERNEL_RADIUS * 2)

groupshared float4 LoadedMem[SHARED_SIZE][SHARED_SIZE]; // 원본 로드용
groupshared float4 HBlurMem[SHARED_SIZE][SHARED_SIZE]; // 가로 블러 결과용
                                                         // apron 행도 세로 블러에 필요하므로 동일 크기

static const float GaussianWeights[5] =
{
	0.227027f, 0.194595f, 0.121622f, 0.054054f, 0.016216f
};

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(
    uint3 GroupID : SV_GroupID,
    uint3 LocalID : SV_GroupThreadID,
    uint3 GlobalID : SV_DispatchThreadID)
{
	int2 tileOrigin = int2(GroupID.xy) * TILE_SIZE - KERNEL_RADIUS;
	int2 texSize = int2(1.0f / TexelSize);

    // ── 1. 공유 메모리 로드 (apron 포함) ─────────────────────
	uint totalCells = SHARED_SIZE * SHARED_SIZE;
	uint threadCount = TILE_SIZE * TILE_SIZE;
	uint linearLocal = LocalID.y * TILE_SIZE + LocalID.x;

	for (uint i = linearLocal; i < totalCells; i += threadCount)
	{
		uint sx = i % SHARED_SIZE;
		uint sy = i / SHARED_SIZE;
		int2 samplePos = clamp(tileOrigin + int2(sx, sy), int2(0, 0), texSize - 1);
		LoadedMem[sy][sx] = InputTex[samplePos];
	}
	GroupMemoryBarrierWithGroupSync();

    // ── 2. 가로 블러 → HBlurMem (apron 행도 계산해야 세로 블러에서 쓸 수 있음) ──
	for (uint j = linearLocal; j < totalCells; j += threadCount)
	{
		uint sx = j % SHARED_SIZE;
		uint sy = j / SHARED_SIZE;

		float4 h = LoadedMem[sy][sx] * GaussianWeights[0];
        [unroll]
		for (int k = 1; k <= KERNEL_RADIUS; ++k)
		{
			uint lx = clamp((int) sx - k, 0, SHARED_SIZE - 1);
			uint rx = clamp((int) sx + k, 0, SHARED_SIZE - 1);
			h += LoadedMem[sy][lx] * GaussianWeights[k];
			h += LoadedMem[sy][rx] * GaussianWeights[k];
		}
		HBlurMem[sy][sx] = h;
	}
	GroupMemoryBarrierWithGroupSync();

    // ── 3. 세로 블러 → 출력 ──────────────────────────────────
	uint cx = LocalID.x + KERNEL_RADIUS;
	uint cy = LocalID.y + KERNEL_RADIUS;

	float4 v = HBlurMem[cy][cx] * GaussianWeights[0];
    [unroll]
	for (int k = 1; k <= KERNEL_RADIUS; ++k)
	{
		v += HBlurMem[cy - k][cx] * GaussianWeights[k];
		v += HBlurMem[cy + k][cx] * GaussianWeights[k];
	}

	if (GlobalID.x < (uint) texSize.x && GlobalID.y < (uint) texSize.y)
	{
		OutputTex[GlobalID.xy] = v;
	}
}