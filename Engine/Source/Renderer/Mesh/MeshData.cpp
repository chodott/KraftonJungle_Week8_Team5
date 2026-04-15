#include "Renderer/Mesh/MeshData.h"

#include "Object/Class.h"
#include "Renderer/Mesh/Vertex.h"
#include "Level/MeshBVH.h"
#include <cstring>


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

FStaticMesh* UStaticMesh::GetRenderDataForScreenSize(const FStaticMeshLODSelectionContext& SelectionContext) const
{
	FStaticMesh* SelectedMesh = StaticMeshAsset;
	if (!SelectedMesh)
	{
		return nullptr;
	}

	const float EffectiveScreenSize = (std::max)(SelectionContext.ScreenSize, 0.0f);
	const float EffectiveThresholdScale = (std::max)(SelectionContext.ThresholdScale, 0.01f);
	for (const FStaticMeshLOD& Lod : LODs)
	{
		if (!Lod.Mesh)
		{
			break;
		}

		const float EffectiveThreshold = (std::max)(Lod.ScreenSize * EffectiveThresholdScale + SelectionContext.ThresholdBias, 0.0f);
		if (EffectiveScreenSize > EffectiveThreshold)
		{
			break;
		}

		SelectedMesh = Lod.Mesh.get();
	}

	return SelectedMesh;
}

void UStaticMesh::AddLod(std::unique_ptr<FStaticMesh> InMesh, float InScreenSize)
{
	if (!InMesh)
	{
		return;
	}

	FStaticMeshLOD NewLOD;
	NewLOD.VertexCount = static_cast<uint32>(InMesh->Vertices.size());
	NewLOD.ScreenSize = (std::max)(InScreenSize, 0.0f);
	NewLOD.Mesh = std::move(InMesh);
	LODs.push_back(std::move(NewLOD));

	std::sort(LODs.begin(), LODs.end(), [](const FStaticMeshLOD& A, const FStaticMeshLOD& B)
	{
		return A.ScreenSize > B.ScreenSize;
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

float UStaticMesh::GetLodScreenSize(int32 LODIndex) const
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

	return LODs[ExtraLodIndex].ScreenSize;
}

bool UStaticMesh::IntersectLocalRay(const FVector& RayOrigin, const FVector& RayDirection, float& OutDistance) const
{
	BuildAccelerationStructureIfNeeded();
	if (!TriangleBVH || !TriangleBVH->IsValid())
	{
		return false;
	}

	return TriangleBVH->IntersectRay(RayOrigin, RayDirection, OutDistance);
}
