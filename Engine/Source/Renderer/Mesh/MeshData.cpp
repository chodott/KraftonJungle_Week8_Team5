#include "Renderer/Mesh/MeshData.h"

#include "Object/Class.h"
#include "Core/ConsoleVariableManager.h"
#include "Renderer/Mesh/Vertex.h"
#include "Level/MeshBVH.h"
#include <cstring>
#include <cmath>
#include <limits>

namespace
{
	bool IsMeshTriangleBVHEnabled()
	{
		FConsoleVariableManager& CVM = FConsoleVariableManager::Get();
		FConsoleVariable* MeshTriangleBVHVar = CVM.Find("r.MeshTriangleBVH");
		if (!MeshTriangleBVHVar)
		{
			MeshTriangleBVHVar = CVM.Register("r.MeshTriangleBVH", 0, "Enable per-mesh triangle BVH (0 = off, 1 = on)");
		}

		return MeshTriangleBVHVar->GetInt() != 0;
	}

	template <typename FTriangleCallback>
	void ForEachTriangleInStaticMesh(const FStaticMesh& InMesh, FTriangleCallback&& Callback)
	{
		const bool bHasIndexBuffer = !InMesh.Indices.empty();
		int32 LinearTriangleIndex = 0;

		auto EmitTriangle = [&](const FVector& V0, const FVector& V1, const FVector& V2) -> bool
		{
			const bool bContinue = Callback(LinearTriangleIndex, V0, V1, V2);
			++LinearTriangleIndex;
			return bContinue;
		};

		if (!bHasIndexBuffer)
		{
			const size_t TriangleCount = InMesh.Vertices.size() / 3;
			for (size_t TriangleIdx = 0; TriangleIdx < TriangleCount; ++TriangleIdx)
			{
				const size_t BaseVertex = TriangleIdx * 3;
				if (!EmitTriangle(
					InMesh.Vertices[BaseVertex + 0].Position,
					InMesh.Vertices[BaseVertex + 1].Position,
					InMesh.Vertices[BaseVertex + 2].Position))
				{
					return;
				}
			}
			return;
		}

		auto EmitIndexedRange = [&](size_t StartIndex, size_t IndexCount) -> bool
		{
			const size_t EndIndex = (std::min)(StartIndex + IndexCount, InMesh.Indices.size());
			for (size_t Index = StartIndex; Index + 2 < EndIndex; Index += 3)
			{
				const uint32 I0 = InMesh.Indices[Index + 0];
				const uint32 I1 = InMesh.Indices[Index + 1];
				const uint32 I2 = InMesh.Indices[Index + 2];

				if (I0 >= InMesh.Vertices.size() || I1 >= InMesh.Vertices.size() || I2 >= InMesh.Vertices.size())
				{
					continue;
				}

				if (!EmitTriangle(
					InMesh.Vertices[I0].Position,
					InMesh.Vertices[I1].Position,
					InMesh.Vertices[I2].Position))
				{
					return false;
				}
			}
			return true;
		};

		if (!InMesh.Sections.empty())
		{
			for (const FMeshSection& Section : InMesh.Sections)
			{
				if (!EmitIndexedRange(static_cast<size_t>(Section.StartIndex), static_cast<size_t>(Section.IndexCount)))
				{
					return;
				}
			}
			return;
		}

		(void)EmitIndexedRange(0, InMesh.Indices.size());
	}

	bool ComputeTriangleDataByLinearIndex(const FStaticMesh& InMesh, int32 TriangleIndex, FMeshBVH::FTriangleData& OutTriangleData)
	{
		if (TriangleIndex < 0)
		{
			return false;
		}

		bool bFound = false;
		ForEachTriangleInStaticMesh(InMesh, [&](int32 LinearIndex, const FVector& V0, const FVector& V1, const FVector& V2)
		{
			if (LinearIndex != TriangleIndex)
			{
				return true;
			}

			OutTriangleData.V0 = V0;
			OutTriangleData.V1 = V1;
			OutTriangleData.V2 = V2;
			OutTriangleData.Bounds = FAABB(V0, V0);
			OutTriangleData.Bounds.Expand(V1);
			OutTriangleData.Bounds.Expand(V2);
			bFound = true;
			return false;
		});
		return bFound;
	}

	bool IntersectTriangleRay(const FVector& RayOrigin, const FVector& RayDirection, const FVector& V0, const FVector& V1, const FVector& V2, float& OutDistance)
	{
		constexpr float Epsilon = 1.0e-6f;

		const FVector Edge1 = V1 - V0;
		const FVector Edge2 = V2 - V0;
		const FVector P = FVector::CrossProduct(RayDirection, Edge2);
		const float Determinant = FVector::DotProduct(Edge1, P);
		if (std::fabs(Determinant) <= Epsilon)
		{
			return false;
		}

		const float InvDeterminant = 1.0f / Determinant;
		const FVector T = RayOrigin - V0;
		const float U = FVector::DotProduct(T, P) * InvDeterminant;
		if (U < 0.0f || U > 1.0f)
		{
			return false;
		}

		const FVector Q = FVector::CrossProduct(T, Edge1);
		const float V = FVector::DotProduct(RayDirection, Q) * InvDeterminant;
		if (V < 0.0f || (U + V) > 1.0f)
		{
			return false;
		}

		const float Distance = FVector::DotProduct(Edge2, Q) * InvDeterminant;
		if (Distance <= Epsilon)
		{
			return false;
		}

		OutDistance = Distance;
		return true;
	}
}

bool FStaticMesh::UpdateVertexAndIndexBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context)
{
	if (!bIsDirty) return true;
	bIsDirty = false;
	return CreateVertexAndIndexBuffer(Device);
}

bool FStaticMesh::CreateVertexAndIndexBuffer(ID3D11Device* Device)
{
	Release();
	if (Vertices.empty())
	{
		return false;
	}

	// Vertex Buffer
	D3D11_BUFFER_DESC VBDesc = {};
	VBDesc.Usage = D3D11_USAGE_IMMUTABLE;
	VBDesc.ByteWidth = static_cast<UINT>(sizeof(FVertex) * Vertices.size());
	VBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA VBData = {};
	VBData.pSysMem = Vertices.data();

	HRESULT Hr = Device->CreateBuffer(&VBDesc, &VBData, &VertexBuffer);
	if (FAILED(Hr))
	{
		printf("[FMeshData] Failed to create vertex buffer\n");
		return false;
	}

	// Non-indexed mesh path.
	if (Indices.empty())
	{
		return true;
	}

	// Index Buffer
	D3D11_BUFFER_DESC IBDesc = {};
	IBDesc.Usage = D3D11_USAGE_IMMUTABLE;
	IBDesc.ByteWidth = static_cast<UINT>(sizeof(uint32) * Indices.size());
	IBDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA IBData = {};
	IBData.pSysMem = Indices.data();

	Hr = Device->CreateBuffer(&IBDesc, &IBData, &IndexBuffer);
	if (FAILED(Hr))
	{
		printf("[FMeshData] Failed to create index buffer\n");
		VertexBuffer->Release();
		VertexBuffer = nullptr;
		return false;
	}

	return true;
}

bool FDynamicMesh::CreateVertexAndIndexBuffer(ID3D11Device* Device)
{
	Release();

	if (Vertices.empty())
	{
		return false;
	}

	MaxVertexCapacity = static_cast<uint32>(Vertices.size());
	MaxIndexCapacity = static_cast<uint32>(Indices.size());

	// 1. Vertex Buffer (DYNAMIC + CPU_ACCESS_WRITE)
	D3D11_BUFFER_DESC VBDesc = {};
	VBDesc.Usage = D3D11_USAGE_DYNAMIC;
	VBDesc.ByteWidth = sizeof(FVertex) * MaxVertexCapacity;
	VBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	VBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	D3D11_SUBRESOURCE_DATA VBData = {};
	VBData.pSysMem = Vertices.data();

	HRESULT Hr = Device->CreateBuffer(&VBDesc, &VBData, &VertexBuffer);
	if (FAILED(Hr))
	{
		printf("[FDynamicMesh] Failed to create vertex buffer\n");
		return false;
	}

	// Non-indexed mesh path.
	if (MaxIndexCapacity == 0)
	{
		return true;
	}

	// 2. Index Buffer (DYNAMIC + CPU_ACCESS_WRITE)
	D3D11_BUFFER_DESC IBDesc = {};
	IBDesc.Usage = D3D11_USAGE_DYNAMIC;
	IBDesc.ByteWidth = sizeof(uint32) * MaxIndexCapacity;
	IBDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	IBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	D3D11_SUBRESOURCE_DATA IBData = {};
	IBData.pSysMem = Indices.data();

	Hr = Device->CreateBuffer(&IBDesc, &IBData, &IndexBuffer);
	if (FAILED(Hr))
	{
		printf("[FDynamicMesh] Failed to create index buffer\n");
		Release();
		return false;
	}

	return true;
}

bool FDynamicMesh::UpdateVertexAndIndexBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context)
{
	if (!bIsDirty)
	{
		return true;
	}

	if (Vertices.empty())
	{
		return false;
	}

	const bool bHasIndices = !Indices.empty();
	const bool bNeedRecreateVertexBuffer = (!VertexBuffer || Vertices.size() > MaxVertexCapacity);
	const bool bNeedRecreateIndexBuffer = bHasIndices
		? (!IndexBuffer || Indices.size() > MaxIndexCapacity)
		: (IndexBuffer != nullptr || MaxIndexCapacity != 0);

	if (bNeedRecreateVertexBuffer || bNeedRecreateIndexBuffer)
	{
		bIsDirty = false;
		return CreateVertexAndIndexBuffer(Device);
	}

	D3D11_MAPPED_SUBRESOURCE MappedVB;
	if (SUCCEEDED(Context->Map(VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedVB)))
	{
		memcpy(MappedVB.pData, Vertices.data(), sizeof(FVertex) * Vertices.size());
		Context->Unmap(VertexBuffer, 0);
	}

	if (bHasIndices && IndexBuffer)
	{
		D3D11_MAPPED_SUBRESOURCE MappedIB;
		if (SUCCEEDED(Context->Map(IndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedIB)))
		{
			memcpy(MappedIB.pData, Indices.data(), sizeof(uint32) * Indices.size());
			Context->Unmap(IndexBuffer, 0);
		}
	}

	bIsDirty = false;
	return true;
}

IMPLEMENT_RTTI(UStaticMesh, UObject)
const FString& UStaticMesh::GetAssetPathFileName() const
{
	if (StaticMeshAsset) return StaticMeshAsset->PathFileName;
	static FString EmptyPath = "";
	return EmptyPath;
}

void UStaticMesh::SetStaticMeshAsset(FStaticMesh* InStaticMesh)
{
	if (StaticMeshAsset == InStaticMesh)
	{
		return;
	}

	if (StaticMeshAsset)
	{
		delete StaticMeshAsset;
	}

	StaticMeshAsset = InStaticMesh;
	ClearLods();
	TriangleBVH.reset();
}

void UStaticMesh::BuildAccelerationStructureIfNeeded() const
{
	if (!IsMeshTriangleBVHEnabled())
	{
		TriangleBVH.reset();
		return;
	}

	if (TriangleBVH || !StaticMeshAsset)
	{
		return;
	}

	TriangleBVH = std::make_unique<FMeshBVH>();
	TriangleBVH->Build(*StaticMeshAsset);
}

void UStaticMesh::VisitMeshBVHNodes(const FBVHNodeVisitor& Visitor) const
{
	BuildAccelerationStructureIfNeeded();
	if (TriangleBVH && TriangleBVH->IsValid())
	{
		TriangleBVH->VisitNodes(Visitor);
	}
}

void UStaticMesh::QueryMeshBVHTriangles(const FAABB& Bounds, TArray<int32>& OutTriangleIndices) const
{
	OutTriangleIndices.clear();
	BuildAccelerationStructureIfNeeded();
	if (TriangleBVH && TriangleBVH->IsValid())
	{
		TriangleBVH->QueryTriangles(Bounds, OutTriangleIndices);
		return;
	}

	if (!StaticMeshAsset)
	{
		return;
	}

	ForEachTriangleInStaticMesh(*StaticMeshAsset, [&](int32 TriangleIndex, const FVector& V0, const FVector& V1, const FVector& V2)
	{
		FAABB TriangleBounds(V0, V0);
		TriangleBounds.Expand(V1);
		TriangleBounds.Expand(V2);
		if (TriangleBounds.Overlaps(Bounds))
		{
			OutTriangleIndices.push_back(TriangleIndex);
		}
		return true;
	});
}

bool UStaticMesh::GetMeshBVHTriangleData(int32 TriangleIndex, FMeshBVH::FTriangleData& OutTriangleData) const
{
	BuildAccelerationStructureIfNeeded();
	if (TriangleBVH && TriangleBVH->IsValid())
	{
		return TriangleBVH->GetTriangleData(TriangleIndex, OutTriangleData);
	}

	if (!StaticMeshAsset)
	{
		return false;
	}

	return ComputeTriangleDataByLinearIndex(*StaticMeshAsset, TriangleIndex, OutTriangleData);
}


FStaticMesh* UStaticMesh::GetRenderData(int32 LODIndex) const
{
	if (LODIndex <= 0)
	{
		return StaticMeshAsset;
	}

	const size_t ExtraLodIndex = static_cast<size_t>(LODIndex - 1);
	if (ExtraLodIndex >= LODs.size())
	{
		return StaticMeshAsset;
	}

	return LODs[ExtraLodIndex].Mesh ? LODs[ExtraLodIndex].Mesh.get() : StaticMeshAsset;
}

int32 UStaticMesh::GetLODIndexForDistance(const FStaticMeshLODSelectionContext& SelectionContext) const
{
	if (!StaticMeshAsset)
	{
		return 0;
	}

	const float EffectiveDistance = (std::max)(SelectionContext.Distance, 0.0f);
	int32 SelectedLODIndex = 0;
	for (size_t i = 0; i < LODs.size(); ++i)
	{
		const FStaticMeshLOD& Lod = LODs[i];
		if (!Lod.Mesh)
		{
			break;
		}

		const float Threshold = (i < SelectionContext.PerLODDistances.size())
			? SelectionContext.PerLODDistances[i]
			: Lod.Distance;

		if (EffectiveDistance < Threshold)
		{
			break;
		}

		SelectedLODIndex = static_cast<int32>(i) + 1;
	}

	return SelectedLODIndex;
}

FStaticMesh* UStaticMesh::GetRenderDataForDistance(const FStaticMeshLODSelectionContext& SelectionContext, int32* OutSelectedLODIndex) const
{
	FStaticMesh* SelectedMesh = StaticMeshAsset;
	if (!SelectedMesh)
	{
		if (OutSelectedLODIndex)
		{
			*OutSelectedLODIndex = 0;
		}
		return nullptr;
	}

	const int32 SelectedLODIndex = GetLODIndexForDistance(SelectionContext);
	if (OutSelectedLODIndex)
	{
		*OutSelectedLODIndex = SelectedLODIndex;
	}

	if (SelectedLODIndex <= 0)
	{
		return SelectedMesh;
	}

	const size_t ExtraLodIndex = static_cast<size_t>(SelectedLODIndex - 1);
	if (ExtraLodIndex >= LODs.size() || !LODs[ExtraLodIndex].Mesh)
	{
		return SelectedMesh;
	}

	return LODs[ExtraLodIndex].Mesh.get();
}

void UStaticMesh::AddLod(std::unique_ptr<FStaticMesh> InMesh, float InDistance)
{
	if (!InMesh)
	{
		return;
	}

	FStaticMeshLOD NewLOD;
	NewLOD.VertexCount = static_cast<uint32>(InMesh->Vertices.size());
	NewLOD.Distance = (std::max)(InDistance, 0.0f);
	NewLOD.Mesh = std::move(InMesh);
	LODs.push_back(std::move(NewLOD));

	std::sort(LODs.begin(), LODs.end(), [](const FStaticMeshLOD& A, const FStaticMeshLOD& B)
	{
		return A.Distance < B.Distance;
	});
}

void UStaticMesh::ClearLods()
{
	LODs.clear();
}

uint32 UStaticMesh::GetLodCount() const
{
	return StaticMeshAsset ? static_cast<uint32>(LODs.size() + 1) : 0;
}

float UStaticMesh::GetLodDistance(int32 LODIndex) const
{
	if (LODIndex <= 0)
	{
		return 0.0f;
	}

	const size_t ExtraLodIndex = static_cast<size_t>(LODIndex - 1);
	if (ExtraLodIndex >= LODs.size())
	{
		return 0.0f;
	}

	return LODs[ExtraLodIndex].Distance;
}


bool UStaticMesh::IntersectLocalRay(const FVector& RayOrigin, const FVector& RayDirection, float& OutDistance) const
{
	BuildAccelerationStructureIfNeeded();
	if (TriangleBVH && TriangleBVH->IsValid())
	{
		return TriangleBVH->IntersectRay(RayOrigin, RayDirection, OutDistance);
	}

	if (!StaticMeshAsset)
	{
		return false;
	}

	bool bHit = false;
	float ClosestDistance = (std::numeric_limits<float>::max)();
	ForEachTriangleInStaticMesh(*StaticMeshAsset, [&](int32 /*TriangleIndex*/, const FVector& V0, const FVector& V1, const FVector& V2)
	{
		float HitDistance = 0.0f;
		if (IntersectTriangleRay(RayOrigin, RayDirection, V0, V1, V2, HitDistance) && HitDistance < ClosestDistance)
		{
			ClosestDistance = HitDistance;
			bHit = true;
		}
		return true;
	});

	if (bHit)
	{
		OutDistance = ClosestDistance;
	}
	return bHit;
}
