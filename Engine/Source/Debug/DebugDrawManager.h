#pragma once
#include "CoreMinimal.h"
#include "EngineAPI.h"
#include "Math/Vector.h"

class FShowFlags;
class UWorld;

// 디버그 라인 요청으로 변환되기 전, 엔진 쪽에서 임시로 보관하는 선분 데이터다.
struct FDebugLine
{
    FVector Start;
    FVector End;
    FVector4 Color;
};

// 디버그 큐브 요청으로 변환되기 전, 엔진 쪽에서 임시로 보관하는 박스 데이터다.
struct FDebugCube
{
    FVector Center;
    FVector Extent;
    FVector4 Color;
};

struct FDebugPrimitiveList
{
	TArray<FDebugLine> Lines;
	TArray<FDebugCube> Cubes;

	void Clear()
	{
		Lines.clear();
		Cubes.clear();
	}

	bool IsEmpty() const
	{
		return Lines.empty() && Cubes.empty();
	}
};

struct FWorldDebugDrawBucket
{
	TArray<FDebugLine> Lines;
	TArray<FDebugCube> Cubes;

	void Clear()
	{
		Lines.clear();
		Cubes.clear();
	}

	bool IsEmpty() const
	{
		return Lines.empty() && Cubes.empty();
	}
};

/**
 * 게임/에디터 공통 디버그 도형 수집기다.
 * 실제 GPU 렌더링은 하지 않고, 프레임 끝에 renderer가 소비할 primitive 데이터만 만든다.
 */
class ENGINE_API FDebugDrawManager
{
public:
	// 디버그 선 하나를 현재 프레임 큐에 추가한다.
	void DrawLine(UWorld* World, const FVector& Start, const FVector& End, const FVector4& Color);
	// 디버그 박스 하나를 현재 프레임 큐에 추가한다.
	void DrawCube(UWorld* World, const FVector& Center, const FVector& Extent, const FVector4& Color);

	// 누적된 디버그 도형을 renderer-neutral primitive 목록으로 변환한다.
	void BuildPrimitiveList(const FShowFlags& ShowFlags, UWorld* World, FDebugPrimitiveList& OutPrimitives) const;
	// 파괴된 월드가 남긴 프레임 버킷을 정리한다.
	void ReleaseWorld(UWorld* World);
	// 현재 프레임에 모인 모든 디버그 도형을 비운다.
	void Clear();
private:
	FWorldDebugDrawBucket* FindOrAddBucket(UWorld* World);
	const FWorldDebugDrawBucket* FindBucket(UWorld* World) const;
	TMap<UWorld*, FWorldDebugDrawBucket> WorldBuckets;
	// 충돌 바운드를 순회해 renderer-neutral primitive 목록에 추가한다.
	void DrawAllCollisionBounds(const FShowFlags& ShowFlags, UWorld* World, FDebugPrimitiveList& OutPrimitives) const;
};
