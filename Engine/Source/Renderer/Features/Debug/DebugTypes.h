#pragma once

#include "CoreMinimal.h"
#include "Renderer/Mesh/MeshData.h"

class FMaterial;

struct ENGINE_API FDebugLinePassInputs
{
	FDebugLinePassInputs() = default;
	FDebugLinePassInputs(const FDebugLinePassInputs&) = delete;
	FDebugLinePassInputs& operator=(const FDebugLinePassInputs&) = delete;
	FDebugLinePassInputs(FDebugLinePassInputs&&) noexcept = default;
	FDebugLinePassInputs& operator=(FDebugLinePassInputs&&) noexcept = default;

	std::unique_ptr<FDynamicMesh> LineMesh;
	FMaterial* Material = nullptr;

	void Clear()
	{
		if (LineMesh)
		{
			LineMesh->Topology = EMeshTopology::EMT_LineList;
			LineMesh->Vertices.clear();
			LineMesh->Indices.clear();
			LineMesh->bIsDirty = true;
		}

		Material = nullptr;
	}

	bool IsEmpty() const
	{
		return LineMesh == nullptr || LineMesh->Vertices.empty();
	}

	FDynamicMesh& GetOrCreateLineMesh()
	{
		if (!LineMesh)
		{
			LineMesh = std::make_unique<FDynamicMesh>();
		}

		LineMesh->Topology = EMeshTopology::EMT_LineList;
		return *LineMesh;
	}
};
