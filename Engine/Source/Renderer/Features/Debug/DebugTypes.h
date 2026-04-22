#pragma once

#include "CoreMinimal.h"
#include "Renderer/Mesh/MeshData.h"

class FMaterial;

struct ENGINE_API FEditorLinePassInputs
{
	FEditorLinePassInputs() = default;
	FEditorLinePassInputs(const FEditorLinePassInputs&) = delete;
	FEditorLinePassInputs& operator=(const FEditorLinePassInputs&) = delete;
	FEditorLinePassInputs(FEditorLinePassInputs&&) noexcept = default;
	FEditorLinePassInputs& operator=(FEditorLinePassInputs&&) noexcept = default;

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
