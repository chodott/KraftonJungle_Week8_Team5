#pragma once

#include "CoreMinimal.h"
#include "Renderer/Material.h"
#include "Renderer/MeshData.h"
#include "Renderer/RenderFrameContext.h"
#include "Renderer/SceneRenderTargets.h"

class FRenderer;

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

class ENGINE_API FDebugLineRenderFeature
{
public:
	~FDebugLineRenderFeature();

	static void AppendLine(FDebugLinePassInputs& PassInputs, const FVector& Start, const FVector& End, const FVector4& Color);
	static void AppendCube(FDebugLinePassInputs& PassInputs, const FVector& Center, const FVector& BoxExtent, const FVector4& Color);

	bool Render(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		const FSceneRenderTargets& Targets,
		FDebugLinePassInputs& PassInputs);
	void Release();
};
