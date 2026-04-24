#pragma once

#include "CoreMinimal.h"

#include <memory>

#include "Component/PrimitiveComponent.h"

struct FRenderMesh;
class FMaterial;

enum class EMaterialDomain
{
	Opaque,
	Transparent,
	EditorGrid,
	EditorPrimitive,
};

enum class EMeshPassType : uint32
{
	DepthPrepass = 0,
	ShadowDepthPrepass,
	GBuffer,
	ForwardOpaque,
	ForwardMeshDecal,
	ForwardTransparent,
	EditorPicking,
	EditorGrid,
	EditorPrimitive,
	Count,
};

enum class EMeshPassMask : uint32
{
	None               = 0,
	DepthPrepass       = 1u << 0,
	ShadowDepthPrepass = 1u << 1,
	GBuffer            = 1u << 2,
	ForwardOpaque      = 1u << 3,
	ForwardMeshDecal   = 1u << 4,
	ForwardTransparent = 1u << 5,
	EditorPicking      = 1u << 6,
	EditorGrid         = 1u << 7,
	EditorPrimitive    = 1u << 8,
};

inline uint32 operator|(EMeshPassMask A, EMeshPassMask B)
{
	return static_cast<uint32>(A) | static_cast<uint32>(B);
}

inline bool EnumHasAnyFlags(uint32 Value, EMeshPassMask Mask)
{
	return (Value & static_cast<uint32>(Mask)) != 0u;
}

struct ENGINE_API FMeshBatch
{
	FRenderMesh*                 Mesh      = nullptr;
	std::shared_ptr<FRenderMesh> MeshOwner = nullptr;

	FMaterial*       Material = nullptr;
	FMatrix          World    = FMatrix::Identity;
	FBoxSphereBounds WorldBounds;
	UPrimitiveComponent* SourceComponent = nullptr;

	uint32 SectionIndex = 0;
	uint32 IndexStart   = 0;
	uint32 IndexCount   = 0;

	EMaterialDomain Domain   = EMaterialDomain::Opaque;
	uint32          PassMask = static_cast<uint32>(EMeshPassMask::ForwardOpaque);

	bool bDisableDepthTest  = false;
	bool bDisableDepthWrite = false;
	bool bDisableCulling    = false;
	bool bEditorOnly        = false;

	float  DistanceSqToCamera = 0.0f;
	uint64 SubmissionOrder    = 0;

	uint32 LocalLightListOffset = 0;
	uint32 LocalLightListCount  = 0;
};
