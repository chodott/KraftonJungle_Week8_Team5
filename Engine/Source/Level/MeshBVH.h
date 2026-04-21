#pragma once

#include "CoreMinimal.h"
#include "Level/BVH.h"

struct FRenderMesh;

class ENGINE_API FMeshBVH
{
public:
	struct FTriangleData
	{
		FVector V0 = FVector::ZeroVector;
		FVector V1 = FVector::ZeroVector;
		FVector V2 = FVector::ZeroVector;
		FAABB Bounds;
	};

	void Build(const FRenderMesh& Mesh);
	bool IntersectRay(const FVector& RayOrigin, const FVector& RayDirection, float& OutDistance) const;
	bool IsValid() const { return RootNodeIndex >= 0; }
	void VisitNodes(const FBVHNodeVisitor& Visitor) const;
	void QueryTriangles(const FAABB& Bounds, TArray<int32>& OutTriangleIndices) const;
	bool GetTriangleData(int32 TriangleIndex, FTriangleData& OutTriangleData) const;

private:
	struct FTriangleRef
	{
		FVector V0 = FVector::ZeroVector;
		FVector V1 = FVector::ZeroVector;
		FVector V2 = FVector::ZeroVector;
		FVector Centroid = FVector::ZeroVector;
		FAABB Bounds;
	};

	struct FNode
	{
		FAABB Bounds;
		int32 LeftChild = -1;
		int32 RightChild = -1;
		int32 FirstTriangle = 0;
		int32 TriangleCount = 0;
		int32 SplitAxis = 0;

		bool IsLeaf() const { return TriangleCount > 0; }
	};

	static constexpr int32 MaxTrianglesPerLeaf = 8;
	static constexpr int32 MaxDepth = 16;

	int32 BuildRecursive(int32 Start, int32 End, int32 Depth = 0);
	bool IntersectRayTriangle(const Ray& InRay, const FTriangleRef& Triangle, float& OutDistance) const;
	void VisitNodesRecursive(int32 NodeIndex, int32 Depth, const FBVHNodeVisitor& Visitor) const;
	void QueryTrianglesRecursive(int32 NodeIndex, const FAABB& Bounds, TArray<int32>& OutTriangleIndices) const;

	TArray<FTriangleRef> Triangles;
	TArray<int32> TriangleIndices;
	TArray<FNode> Nodes;
	int32 RootNodeIndex = -1;
};
