#include "Component/LineBatchComponent.h"
#include "Math/MathUtility.h"
#include "Object/Class.h"
#include <algorithm>
#include <cmath>

namespace
{
	void CopyRenderMeshCPUState(const FRenderMesh* SourceMesh, FRenderMesh* DuplicatedMesh)
	{
		if (!SourceMesh || !DuplicatedMesh)
		{
			return;
		}

		DuplicatedMesh->Release();
		DuplicatedMesh->Topology = SourceMesh->Topology;
		DuplicatedMesh->Vertices = SourceMesh->Vertices;
		DuplicatedMesh->Indices = SourceMesh->Indices;
		DuplicatedMesh->Sections = SourceMesh->Sections;
		DuplicatedMesh->PathFileName = SourceMesh->PathFileName;
		DuplicatedMesh->bIsDirty = true;
		DuplicatedMesh->UpdateLocalBound();
	}
}

IMPLEMENT_RTTI(ULineBatchComponent, UPrimitiveComponent)

void ULineBatchComponent::PostConstruct()
{
	LineMesh = std::make_shared<FDynamicMesh>();
	LineMesh->Topology = EMeshTopology::EMT_LineList;
}

void ULineBatchComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UPrimitiveComponent::DuplicateShallow(DuplicatedObject, Context);

	ULineBatchComponent* DuplicatedLineBatchComponent = static_cast<ULineBatchComponent*>(DuplicatedObject);
	CopyRenderMeshCPUState(LineMesh.get(), DuplicatedLineBatchComponent->LineMesh.get());
}

void ULineBatchComponent::DrawLine(FVector InStart, FVector InEnd, FVector4 InColor)
{
	if (!LineMesh) return;

	uint32 CurrentSize = static_cast<uint32>(LineMesh->Vertices.size());

	FVertex V1, V2;
	V1.Position = InStart;
	V1.Color = InColor;
	V1.Normal = FVector::ZeroVector; // 노멀 초기화

	V2.Position = InEnd;
	V2.Color = InColor;
	V2.Normal = FVector::ZeroVector;

	LineMesh->Vertices.push_back(V1);
	LineMesh->Vertices.push_back(V2);

	// 인덱스 버퍼 업데이트
	LineMesh->Indices.push_back(CurrentSize);
	LineMesh->Indices.push_back(CurrentSize + 1);

	// 상태 갱신
	LineMesh->bIsDirty = true;
	LineMesh->UpdateLocalBound();
}

void ULineBatchComponent::DrawWireCube(FVector InCenter, FQuat InRotation, FVector InScale, FVector4 InColor)
{
	if (!LineMesh) return;
	const FVector BaseCube[12][2] = {
		{{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}},  // 왼쪽 위
		{{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}},  // 왼쪽 아래
		{{-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}},  // 오른쪽 위
		{{-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}},  // 오른쪽 아래
		{{0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}},  // 앞쪽 위
		{{0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}},  // 앞쪽 아래
		{{-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}},  // 뒷쪽 위
		{{-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}},  // 뒷쪽 아래
		{{0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}},  // 앞쪽 왼
		{{0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}},  // 앞쪽 오른
		{{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}},  // 뒷쪽 왼
		{{-0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, 0.5f}}  // 뒷쪽 오른
	};
	for (int i = 0; i < 12; i++)
	{
		FVector Start = InRotation * FVector::Multiply(BaseCube[i][0], InScale) + InCenter;
		FVector End = InRotation * FVector::Multiply(BaseCube[i][1], InScale) + InCenter;
		DrawLine(Start, End, InColor);
	}
}

void ULineBatchComponent::DrawWireSphere(FVector InCenter, float InRadius, FVector4 InColor)
{
	if (!LineMesh) return;

	const int32 Segments = 16; // 선의 개수 (정밀도)
	const float AngleStep = 2.0f * FMath::PI / Segments;

	for (int32 i = 0; i < Segments; i++)
	{
		float A1 = i * AngleStep;
		float A2 = (i + 1) * AngleStep;

		float S1 = sinf(A1) * InRadius;
		float C1 = cosf(A1) * InRadius;
		float S2 = sinf(A2) * InRadius;
		float C2 = cosf(A2) * InRadius;

		// XY 평면 원 (가로)
		DrawLine(
			InCenter + FVector(C1, S1, 0.0f),
			InCenter + FVector(C2, S2, 0.0f),
			InColor
		);

		// YZ 평면 원 (세로 1)
		DrawLine(
			InCenter + FVector(0.0f, C1, S1),
			InCenter + FVector(0.0f, C2, S2),
			InColor
		);

		// ZX 평면 원 (세로 2)
		DrawLine(
			InCenter + FVector(S1, 0.0f, C1),
			InCenter + FVector(S2, 0.0f, C2),
			InColor
		);
	}
}

void ULineBatchComponent::DrawWireCone(
	FVector InOrigin,
	FVector InDirection,
	float InLength,
	float InAngleDegrees,
	FVector4 InColor,
	int32 InSegments,
	int32 InSpokes,
	bool bDrawSphericalCap)
{
	if (!LineMesh)
	{
		return;
	}

	const float ConeLength = (std::max)(0.0f, InLength);
	if (ConeLength <= FMath::SmallNumber)
	{
		return;
	}

	const FVector ConeDirection = InDirection.GetSafeNormal();
	const FVector NormalizedDirection = ConeDirection.IsNearlyZero() ? FVector::ForwardVector : ConeDirection;

	// UE-style spotlight gizmo:
	// volume is intersection of a sphere (radius=InLength) and a cone (half-angle=InAngleDegrees).
	// so the cone boundary ring must lie on the sphere: axisDist = R*cos(a), ringRadius = R*sin(a).
	const float ClampedHalfAngleDeg = FMath::Clamp(InAngleDegrees, 0.0f, 89.0f);
	const float HalfAngleRad = FMath::DegreesToRadians(ClampedHalfAngleDeg);
	const float ConeRadius = ConeLength * std::sin(HalfAngleRad);
	const float ConeAxisDistance = ConeLength * std::cos(HalfAngleRad);
	const FVector ConeBaseCenter = InOrigin + NormalizedDirection * ConeAxisDistance;

	if (ConeRadius <= FMath::SmallNumber)
	{
		DrawLine(InOrigin, InOrigin + NormalizedDirection * ConeLength, InColor);
		return;
	}

	const FVector UpCandidate = (std::abs(FVector::DotProduct(NormalizedDirection, FVector::UpVector)) > 0.99f)
		? FVector::RightVector
		: FVector::UpVector;
	const FVector Tangent = FVector::CrossProduct(UpCandidate, NormalizedDirection).GetSafeNormal();
	const FVector Bitangent = FVector::CrossProduct(NormalizedDirection, Tangent).GetSafeNormal();

	const int32 SegmentCount = (std::max)(InSegments, 3);
	const int32 SpokeCount = (std::max)(InSpokes, 0);
	const float AngleStep = FMath::TwoPi / static_cast<float>(SegmentCount);
	const int32 SpokeStep = SpokeCount > 0 ? (std::max)(1, SegmentCount / SpokeCount) : 0;

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const float AngleA = static_cast<float>(SegmentIndex) * AngleStep;
		const float AngleB = static_cast<float>(SegmentIndex + 1) * AngleStep;

		const FVector RingPointA = ConeBaseCenter +
			(Tangent * (std::cos(AngleA) * ConeRadius)) +
			(Bitangent * (std::sin(AngleA) * ConeRadius));
		const FVector RingPointB = ConeBaseCenter +
			(Tangent * (std::cos(AngleB) * ConeRadius)) +
			(Bitangent * (std::sin(AngleB) * ConeRadius));

		DrawLine(RingPointA, RingPointB, InColor);

		if (SpokeStep > 0 && SegmentIndex % SpokeStep == 0)
		{
			DrawLine(InOrigin, RingPointA, InColor);
		}
	}

	if (bDrawSphericalCap)
	{
		const int32 CapArcCount = (std::max)(3, (std::min)(InSpokes, 8));
		const int32 CapSteps = 10;
		const float CapArcStep = FMath::TwoPi / static_cast<float>(CapArcCount);

		for (int32 ArcIndex = 0; ArcIndex < CapArcCount; ++ArcIndex)
		{
			const float ArcAngle = static_cast<float>(ArcIndex) * CapArcStep;
			const FVector ArcDir = (Tangent * std::cos(ArcAngle)) + (Bitangent * std::sin(ArcAngle));

			FVector PrevPoint = InOrigin + NormalizedDirection * ConeLength;
			for (int32 Step = 1; Step <= CapSteps; ++Step)
			{
				const float T = static_cast<float>(Step) / static_cast<float>(CapSteps);
				const float Phi = HalfAngleRad * T;
				const FVector CurrentPoint =
					InOrigin +
					(NormalizedDirection * (std::cos(Phi) * ConeLength)) +
					(ArcDir * (std::sin(Phi) * ConeLength));

				DrawLine(PrevPoint, CurrentPoint, InColor);
				PrevPoint = CurrentPoint;
			}
		}
	}
}

FBoxSphereBounds ULineBatchComponent::GetLocalBounds() const
{
	if (LineMesh)
	{
		return { LineMesh->GetCenterCoord(), LineMesh->GetLocalBoundRadius(),
				 (LineMesh->GetMaxCoord() - LineMesh->GetMinCoord()) * 0.5f };
	}
	return { FVector::ZeroVector, 0.f, FVector::ZeroVector };
}

void ULineBatchComponent::Clear()
{
	if (LineMesh)
	{
		LineMesh->Vertices.clear();
		LineMesh->Indices.clear();
		LineMesh->bIsDirty = true;
		LineMesh->UpdateLocalBound();
	}
}
