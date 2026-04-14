#include "Renderer/Scene/Builders/DebugSceneBuilder.h"

#include "Actor/Actor.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Debug/DebugDrawManager.h"
#include "Level/PrimitiveVisibilityUtils.h"
#include "Renderer/Features/Debug/DebugLineRenderFeature.h"
#include "World/World.h"

namespace
{
	void AppendActorMeshBVHDebug(
		AActor* BoundsActor,
		const FShowFlags& ShowFlags,
		UWorld* World,
		FDebugPrimitiveList& OutPrimitives)
	{
		if (!BoundsActor || !World)
		{
			return;
		}

		UStaticMeshComponent* MeshComp = nullptr;
		for (UActorComponent* Comp : BoundsActor->GetComponents())
		{
			if (!Comp || !Comp->IsA(UStaticMeshComponent::StaticClass()))
			{
				continue;
			}

			UStaticMeshComponent* Candidate = static_cast<UStaticMeshComponent*>(Comp);
			if (Candidate->IsEditorVisualization() || IsHiddenByArrowVisualizationShowFlags(Candidate, ShowFlags))
			{
				continue;
			}

			if (Candidate->GetStaticMesh())
			{
				MeshComp = Candidate;
				break;
			}
		}

		if (!MeshComp)
		{
			return;
		}

		if (ULevel* Scene = World->GetScene())
		{
			Scene->VisitBVHNodesForPrimitive(MeshComp, [&OutPrimitives](const FAABB& Bounds, int32 Depth, bool bIsLeaf)
				{
					(void)Depth;
					const FVector Center = (Bounds.PMin + Bounds.PMax) * 0.5f;
					const FVector Extent = (Bounds.PMax - Bounds.PMin) * 0.5f;
					const FVector4 Color = bIsLeaf
						? FVector4(1.0f, 1.0f, 0.0f, 1.0f)
						: FVector4(0.0f, 1.0f, 0.0f, 1.0f);
					OutPrimitives.Cubes.push_back({ Center, Extent, Color });
				});
		}

		UStaticMesh* StaticMesh = MeshComp->GetStaticMesh();
		if (!StaticMesh)
		{
			return;
		}

		const FMatrix& LocalToWorld = MeshComp->GetWorldTransform();
		StaticMesh->VisitMeshBVHNodes([&OutPrimitives, &LocalToWorld](const FAABB& LocalBounds, int32 Depth, bool bIsLeaf)
			{
				(void)Depth;
				const FVector& PMin = LocalBounds.PMin;
				const FVector& PMax = LocalBounds.PMax;
				const FVector Corners[8] = {
					{ PMin.X, PMin.Y, PMin.Z }, { PMax.X, PMin.Y, PMin.Z },
					{ PMin.X, PMax.Y, PMin.Z }, { PMax.X, PMax.Y, PMin.Z },
					{ PMin.X, PMin.Y, PMax.Z }, { PMax.X, PMin.Y, PMax.Z },
					{ PMin.X, PMax.Y, PMax.Z }, { PMax.X, PMax.Y, PMax.Z },
				};

				FVector WorldMin = LocalToWorld.TransformPosition(Corners[0]);
				FVector WorldMax = WorldMin;
				for (int32 Index = 1; Index < 8; ++Index)
				{
					const FVector W = LocalToWorld.TransformPosition(Corners[Index]);
					WorldMin.X = (W.X < WorldMin.X) ? W.X : WorldMin.X;
					WorldMin.Y = (W.Y < WorldMin.Y) ? W.Y : WorldMin.Y;
					WorldMin.Z = (W.Z < WorldMin.Z) ? W.Z : WorldMin.Z;
					WorldMax.X = (W.X > WorldMax.X) ? W.X : WorldMax.X;
					WorldMax.Y = (W.Y > WorldMax.Y) ? W.Y : WorldMax.Y;
					WorldMax.Z = (W.Z > WorldMax.Z) ? W.Z : WorldMax.Z;
				}

				const FVector Center = (WorldMin + WorldMax) * 0.5f;
				const FVector Extent = (WorldMax - WorldMin) * 0.5f;
				const FVector4 Color = bIsLeaf
					? FVector4(0.0f, 0.5f, 1.0f, 1.0f)
					: FVector4(0.0f, 1.0f, 1.0f, 1.0f);
				OutPrimitives.Cubes.push_back({ Center, Extent, Color });
			});
	}
}

void BuildDebugLinePassInputs(const FDebugPrimitiveList& Primitives, FDebugLinePassInputs& OutPassInputs)
{
	OutPassInputs.Clear();

	for (const FDebugCube& Cube : Primitives.Cubes)
	{
		FDebugLineRenderFeature::AppendCube(OutPassInputs, Cube.Center, Cube.Extent, Cube.Color);
	}

	for (const FDebugLine& Line : Primitives.Lines)
	{
		FDebugLineRenderFeature::AppendLine(OutPassInputs, Line.Start, Line.End, Line.Color);
	}
}

void BuildDebugLinePassInputs(const FDebugSceneBuildInputs& Inputs, FDebugLinePassInputs& OutPassInputs)
{
	OutPassInputs.Clear();
	if (!Inputs.DrawManager || !Inputs.World)
	{
		return;
	}

	FDebugPrimitiveList Primitives;
	Inputs.DrawManager->BuildPrimitiveList(Inputs.ShowFlags, Inputs.World, Primitives);
	if (Inputs.ShowFlags.HasFlag(EEngineShowFlags::SF_DebugDraw))
	{
		AppendActorMeshBVHDebug(Inputs.BoundsActor, Inputs.ShowFlags, Inputs.World, Primitives);
	}

	BuildDebugLinePassInputs(Primitives, OutPassInputs);
}
