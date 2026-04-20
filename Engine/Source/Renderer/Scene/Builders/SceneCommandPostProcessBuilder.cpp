#include "Renderer/Scene/Builders/SceneCommandPostProcessBuilder.h"

#include "Renderer/Scene/Builders/SceneCommandBuilder.h"
#include "Renderer/Scene/Builders/SceneCommandBuilderUtils.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Component/DecalComponent.h"
#include "Component/FireBallComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/LocalHeightFogComponent.h"
#include "Component/MeshDecalComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Level/BVH.h"
#include "Renderer/Mesh/MeshData.h"

namespace
{
	float ComputeMeshDecalCullRadius(const FMeshDecalRenderItem& Item)
	{
		const FVector Scale = Item.DecalWorld.GetScaleVector();
		const float MaxScale = (std::max)((std::max)(std::fabs(Scale.X), std::fabs(Scale.Y)), std::fabs(Scale.Z));
		return Item.Extents.Size() * MaxScale;
	}

	bool IntersectBoundsWithSphere(const FBoxSphereBounds& Bounds, const FVector& SphereCenter, float SphereRadius)
	{
		const float CombinedRadius = Bounds.Radius + SphereRadius;
		return FVector::DistSquared(Bounds.Center, SphereCenter) <= CombinedRadius * CombinedRadius;
	}

	FAABB BuildDecalWorldAABB(const FMeshDecalRenderItem& Item)
	{
		const FVector E = Item.Extents;
		const FVector Corners[8] =
		{
			FVector(-E.X, -E.Y, -E.Z),
			FVector(-E.X, -E.Y,  E.Z),
			FVector(-E.X,  E.Y, -E.Z),
			FVector(-E.X,  E.Y,  E.Z),
			FVector( E.X, -E.Y, -E.Z),
			FVector( E.X, -E.Y,  E.Z),
			FVector( E.X,  E.Y, -E.Z),
			FVector( E.X,  E.Y,  E.Z),
		};

		FAABB Bounds;
		for (const FVector& Corner : Corners)
		{
			Bounds.Expand(Item.DecalWorld.TransformPosition(Corner));
		}
		return Bounds;
	}

	FAABB TransformAABB(const FAABB& InWorldAABB, const FMatrix& WorldToLocal)
	{
		const FVector Min = InWorldAABB.PMin;
		const FVector Max = InWorldAABB.PMax;
		const FVector Corners[8] =
		{
			FVector(Min.X, Min.Y, Min.Z),
			FVector(Min.X, Min.Y, Max.Z),
			FVector(Min.X, Max.Y, Min.Z),
			FVector(Min.X, Max.Y, Max.Z),
			FVector(Max.X, Min.Y, Min.Z),
			FVector(Max.X, Min.Y, Max.Z),
			FVector(Max.X, Max.Y, Min.Z),
			FVector(Max.X, Max.Y, Max.Z),
		};

		FAABB OutBounds;
		for (const FVector& Corner : Corners)
		{
			OutBounds.Expand(WorldToLocal.TransformPosition(Corner));
		}
		return OutBounds;
	}

	bool TriangleIntersectsDecalBox(
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& Extents)
	{
		const FVector SafeExtents(
			(std::max)(Extents.X, 1.0e-4f),
			(std::max)(Extents.Y, 1.0e-4f),
			(std::max)(Extents.Z, 1.0e-4f));

		const std::array<FVector, 3> Local = {A, B, C};
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			const float Min = (std::min)({GetAxis(Local[0], Axis), GetAxis(Local[1], Axis), GetAxis(Local[2], Axis)});
			const float Max = (std::max)({GetAxis(Local[0], Axis), GetAxis(Local[1], Axis), GetAxis(Local[2], Axis)});
			const float E = GetAxis(SafeExtents, Axis);
			if (Max < -E || Min > E)
			{
				return false;
			}
		}
		return true;
	}

	FVector2 ComputeDecalUV(const FVector& LocalPosition, const FMeshDecalRenderItem& Item)
	{
		const FVector SafeExtents(
			(std::max)(Item.Extents.X, 1.0e-4f),
			(std::max)(Item.Extents.Y, 1.0e-4f),
			(std::max)(Item.Extents.Z, 1.0e-4f));

		FVector2 UV;
		UV.X = LocalPosition.Y / (SafeExtents.Y * 2.0f) + 0.5f;
		UV.Y = 1.0f - (LocalPosition.Z / (SafeExtents.Z * 2.0f) + 0.5f);
		UV.X = UV.X * Item.AtlasScaleBias.X + Item.AtlasScaleBias.Z;
		UV.Y = UV.Y * Item.AtlasScaleBias.Y + Item.AtlasScaleBias.W;
		return UV;
	}

	bool IsUVInUnitRange(const FVector2& UV)
	{
		return UV.X >= 0.0f && UV.X <= 1.0f && UV.Y >= 0.0f && UV.Y <= 1.0f;
	}
}

void FSceneCommandPostProcessBuilder::BuildFogInputs(
	const FSceneRenderPacket& Packet,
	FSceneViewData& OutSceneViewData) const
{
	OutSceneViewData.PostProcessInputs.FogItems.reserve(Packet.FogPrimitives.size());
	for (const FSceneFogPrimitive& Primitive : Packet.FogPrimitives)
	{
		const UPrimitiveComponent* FogPrimitive = Primitive.Component;
		if (!FogPrimitive)
		{
			continue;
		}

		FFogRenderItem& Item = OutSceneViewData.PostProcessInputs.FogItems.emplace_back();
		Item.FogOrigin = FogPrimitive->GetWorldLocation();

		if (FogPrimitive->IsA(UHeightFogComponent::StaticClass()))
		{
			const UHeightFogComponent* FogComponent = static_cast<const UHeightFogComponent*>(FogPrimitive);
			Item.FogDensity = FogComponent->FogDensity;
			Item.FogHeightFalloff = FogComponent->FogHeightFalloff;
			Item.StartDistance = FogComponent->StartDistance;
			Item.FogCutoffDistance = FogComponent->FogCutoffDistance;
			Item.FogMaxOpacity = FogComponent->FogMaxOpacity;
			Item.FogInscatteringColor = FogComponent->FogInscatteringColor;
			Item.AllowBackground = FogComponent->AllowBackground;
			Item.bLocalFogVolume = false;
			Item.FogVolumeWorld = FMatrix::Identity;
			Item.WorldToFogVolume = FMatrix::Identity;
		}
		else if (FogPrimitive->IsA(ULocalHeightFogComponent::StaticClass()))
		{
			const ULocalHeightFogComponent* FogComponent = static_cast<const ULocalHeightFogComponent*>(FogPrimitive);
			Item.FogDensity = FogComponent->FogDensity;
			Item.FogHeightFalloff = FogComponent->FogHeightFalloff;
			Item.StartDistance = 0.0f;
			Item.FogMaxOpacity = FogComponent->FogMaxOpacity;
			Item.FogInscatteringColor = FogComponent->FogInscatteringColor;
			Item.AllowBackground = FogComponent->AllowBackground;
			Item.FogExtents = FogComponent->FogExtents;
			Item.bLocalFogVolume = true;

			FTransform FogVolumeTransform = FTransform(FogPrimitive->GetWorldTransform());
			const FVector FullExtents = Item.FogExtents * 2.0f;
			const FVector S = FogVolumeTransform.GetScale3D();
			FogVolumeTransform.SetScale3D(FVector(S.X * FullExtents.X, S.Y * FullExtents.Y, S.Z * FullExtents.Z));
			Item.FogVolumeWorld = FogVolumeTransform.ToMatrixWithScale();
			Item.WorldToFogVolume = Item.FogVolumeWorld.GetInverse();
		}
		else
		{
			OutSceneViewData.PostProcessInputs.FogItems.pop_back();
		}
	}
}

void FSceneCommandPostProcessBuilder::BuildFireBallInputs(
	const FSceneRenderPacket& Packet,
	FSceneViewData& OutSceneViewData) const
{
	OutSceneViewData.PostProcessInputs.FireBallItems.reserve(Packet.FireBallPrimitives.size());
	for (const FSceneFireBallPrimitive& Primitive : Packet.FireBallPrimitives)
	{
		const UFireBallComponent* FireballComponent = Primitive.Component;
		if (!FireballComponent)
		{
			continue;
		}

		FFireBallRenderItem& Item = OutSceneViewData.PostProcessInputs.FireBallItems.emplace_back();
		Item.Color = FireballComponent->GetColor();
		Item.FireballOrigin = FireballComponent->GetWorldLocation();
		Item.Intensity = FireballComponent->GetIntensity();
		Item.Radius = FireballComponent->GetRadius();
		Item.RadiusFallOff = FireballComponent->GetRadiusFallOff();
	}
}

void FSceneCommandPostProcessBuilder::BuildDecalInputs(
	const FSceneRenderPacket& Packet,
	FSceneViewData& OutSceneViewData) const
{
	OutSceneViewData.PostProcessInputs.DecalItems.reserve(Packet.DecalPrimitives.size());
	for (const FSceneDecalPrimitive& Primitive : Packet.DecalPrimitives)
	{
		const UDecalComponent* DecalComponent = Primitive.Component;
		if (!DecalComponent || !DecalComponent->IsEnabled())
		{
			continue;
		}

		FDecalRenderItem& Item = OutSceneViewData.PostProcessInputs.DecalItems.emplace_back();
		Item.AtlasScaleBias = DecalComponent->GetAtlasScaleBias();
		Item.BaseColorTint = DecalComponent->GetBaseColorTint();
		Item.DecalWorld = DecalComponent->GetWorldTransform();
		Item.EdgeFade = DecalComponent->GetEdgeFade();
		Item.EmissiveBlend = DecalComponent->GetEmissiveBlend();
		Item.Extents = DecalComponent->GetExtents();
		Item.Flags = DecalComponent->GetRenderFlags();
		Item.NormalBlend = DecalComponent->GetNormalBlend();
		Item.Priority = DecalComponent->GetPriority();
		Item.ReceiverLayerMask = DecalComponent->GetReceiverLayerMask();
		Item.RoughnessBlend = DecalComponent->GetRoughnessBlend();
		Item.TexturePath = DecalComponent->GetTexturePath();
		Item.TextureIndex = 0;
		Item.WorldToDecal = Item.DecalWorld.GetInverse();
		Item.bIsFading = DecalComponent->GetFadeState() != EDecalFadeState::None;
		Item.SourceComponentId = static_cast<uint64>(reinterpret_cast<uintptr_t>(DecalComponent));
		Item.VisibleRevision = DecalComponent->GetVisibleRevision();
		Item.ClusterRevision = DecalComponent->GetClusterRevision();
		const float AngleRad = DecalComponent->GetAllowAngle() * (3.14159265f / 180.0f);
		Item.AllowAngle = std::cos(AngleRad);
	}
}

void FSceneCommandPostProcessBuilder::BuildMeshDecalInputs(
	const FSceneCommandBuildContext& BuildContext,
	const FSceneRenderPacket& Packet,
	FSceneViewData& OutSceneViewData) const
{
	FScenePostProcessInputs& Inputs = OutSceneViewData.PostProcessInputs;

	Inputs.MeshDecalItems.reserve(Packet.MeshDecalPrimitives.size());
	for (const FSceneMeshDecalPrimitive& Primitive : Packet.MeshDecalPrimitives)
	{
		const UMeshDecalComponent* MeshDecalComponent = Primitive.Component;
		if (!MeshDecalComponent || !MeshDecalComponent->IsEnabled())
		{
			continue;
		}

		FMeshDecalRenderItem& Item = Inputs.MeshDecalItems.emplace_back();
		Item.DecalWorld = MeshDecalComponent->GetWorldTransform();
		Item.WorldToDecal = Item.DecalWorld.GetInverse();
		Item.Extents = MeshDecalComponent->GetExtents();
		Item.AtlasScaleBias = MeshDecalComponent->GetAtlasScaleBias();
		Item.TexturePath = MeshDecalComponent->GetTexturePath();
		Item.TextureIndex = 0;
		Item.Flags = MeshDecalComponent->GetRenderFlags();
		Item.BaseColorTint = MeshDecalComponent->GetBaseColorTint();
		Item.Priority = MeshDecalComponent->GetPriority();
		Item.ReceiverLayerMask = MeshDecalComponent->GetReceiverLayerMask();
		Item.NormalBlend = MeshDecalComponent->GetNormalBlend();
		Item.RoughnessBlend = MeshDecalComponent->GetRoughnessBlend();
		Item.EmissiveBlend = MeshDecalComponent->GetEmissiveBlend();
		Item.EdgeFade = MeshDecalComponent->GetEdgeFade();
		const float AngleRad = MeshDecalComponent->GetAllowAngle() * (3.14159265f / 180.0f);
		Item.AllowAngle = std::cos(AngleRad);
		Item.bIsFading = MeshDecalComponent->GetFadeState() != EDecalFadeState::None;
		Item.SurfaceOffset = MeshDecalComponent->GetSurfaceOffset();
		Item.SourceComponentId = static_cast<uint64>(reinterpret_cast<uintptr_t>(MeshDecalComponent));
		Item.VisibleRevision = MeshDecalComponent->GetVisibleRevision();
		Item.ClusterRevision = MeshDecalComponent->GetGeometryRevision();
	}

	Inputs.MeshDecalReceiverCandidates.reserve(Packet.MeshPrimitives.size());
	for (const FSceneMeshPrimitive& Primitive : Packet.MeshPrimitives)
	{
		UPrimitiveComponent* Receiver = Primitive.Component;
		if (!Receiver || Receiver->IsEditorVisualization())
		{
			continue;
		}

		if (!Receiver->IsA(UStaticMeshComponent::StaticClass()))
		{
			continue;
		}

		FRenderMeshSelectionContext SelectionContext;
		SelectionContext.Distance = FVector::Dist(OutSceneViewData.View.CameraPosition, Receiver->GetWorldLocation());
		if (Receiver->GetRenderMesh(SelectionContext) == nullptr)
		{
			continue;
		}

		FMeshDecalReceiverCandidate& Candidate = Inputs.MeshDecalReceiverCandidates.emplace_back();
		Candidate.Component = Receiver;
		Candidate.World = Receiver->GetRenderWorldTransform();
		Candidate.WorldBounds = Receiver->GetWorldBounds();
		Candidate.SourceComponentId = static_cast<uint64>(reinterpret_cast<uintptr_t>(Receiver));
	}

	Inputs.MeshDecalStats.InputDecalCount = static_cast<uint32>(Inputs.MeshDecalItems.size());
	Inputs.MeshDecalStats.ReceiverCandidateCount = static_cast<uint32>(Inputs.MeshDecalReceiverCandidates.size());
	Inputs.MeshDecalStats.CoarseIntersectPairCount = 0;
	Inputs.MeshDecalStats.ClippedTriangleCount = 0;

	for (const FMeshDecalRenderItem& Item : Inputs.MeshDecalItems)
	{
		if (!Item.IsValid())
		{
			continue;
		}

		const FVector DecalCenter = Item.DecalWorld.GetTranslation();
		const float DecalCullRadius = ComputeMeshDecalCullRadius(Item);
		for (const FMeshDecalReceiverCandidate& Candidate : Inputs.MeshDecalReceiverCandidates)
		{
			if (!Candidate.Component)
			{
				continue;
			}

			if ((Item.ReceiverLayerMask & Candidate.ReceiverLayerMask) == 0u)
			{
				continue;
			}

			if (IntersectBoundsWithSphere(Candidate.WorldBounds, DecalCenter, DecalCullRadius))
			{
				++Inputs.MeshDecalStats.CoarseIntersectPairCount;

				const UStaticMeshComponent* StaticMeshComponent = static_cast<const UStaticMeshComponent*>(Candidate.Component);
				UStaticMesh* StaticMesh = StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr;
				if (StaticMesh)
				{
					const FAABB DecalWorldAABB = BuildDecalWorldAABB(Item);
					const FAABB DecalLocalAABB = TransformAABB(DecalWorldAABB, Candidate.World.GetInverse());
					TArray<int32> TriangleCandidates;
					StaticMesh->QueryMeshBVHTriangles(DecalLocalAABB, TriangleCandidates);
					Inputs.MeshDecalStats.ClippedTriangleCount += static_cast<uint32>(TriangleCandidates.size());

					std::shared_ptr<FDynamicMesh> DecalMesh = std::make_shared<FDynamicMesh>();
					DecalMesh->Topology = EMeshTopology::EMT_TriangleList;
					DecalMesh->Vertices.reserve(TriangleCandidates.size() * 3);
					DecalMesh->Indices.reserve(TriangleCandidates.size() * 3);

					const FVector4 VertexColor(Item.BaseColorTint.R, Item.BaseColorTint.G, Item.BaseColorTint.B, Item.BaseColorTint.A);
					const FMatrix WorldToDecal = Item.WorldToDecal;
					const FMatrix ReceiverWorld = Candidate.World;

					for (int32 TriangleIndex : TriangleCandidates)
					{
						FMeshBVH::FTriangleData TriangleData;
						if (!StaticMesh->GetMeshBVHTriangleData(TriangleIndex, TriangleData))
						{
							continue;
						}

						const FVector V0W = ReceiverWorld.TransformPosition(TriangleData.V0);
						const FVector V1W = ReceiverWorld.TransformPosition(TriangleData.V1);
						const FVector V2W = ReceiverWorld.TransformPosition(TriangleData.V2);

						const FVector V0D = WorldToDecal.TransformPosition(V0W);
						const FVector V1D = WorldToDecal.TransformPosition(V1W);
						const FVector V2D = WorldToDecal.TransformPosition(V2W);

						if (!TriangleIntersectsDecalBox(V0D, V1D, V2D, Item.Extents))
						{
							continue;
						}

						const FVector2 UV0 = ComputeDecalUV(V0D, Item);
						const FVector2 UV1 = ComputeDecalUV(V1D, Item);
						const FVector2 UV2 = ComputeDecalUV(V2D, Item);
						if (!IsUVInUnitRange(UV0) && !IsUVInUnitRange(UV1) && !IsUVInUnitRange(UV2))
						{
							continue;
						}

						const FVector FaceNormalWS = FVector::CrossProduct(V1W - V0W, V2W - V0W).GetSafeNormal();
						if (FaceNormalWS.IsZero())
						{
							continue;
						}

						const FVector Offset = FaceNormalWS * Item.SurfaceOffset;
						const uint32 BaseIndex = static_cast<uint32>(DecalMesh->Vertices.size());

						FVertex Vertex0;
						Vertex0.Position = V0W + Offset;
						Vertex0.Normal = FaceNormalWS;
						Vertex0.UV = UV0;
						Vertex0.Color = VertexColor;
						Vertex0.Tangent = FVector4(FVector::ZeroVector, 0.0f);

						FVertex Vertex1;
						Vertex1.Position = V1W + Offset;
						Vertex1.Normal = FaceNormalWS;
						Vertex1.UV = UV1;
						Vertex1.Color = VertexColor;
						Vertex1.Tangent = FVector4(FVector::ZeroVector, 0.0f);

						FVertex Vertex2;
						Vertex2.Position = V2W + Offset;
						Vertex2.Normal = FaceNormalWS;
						Vertex2.UV = UV2;
						Vertex2.Color = VertexColor;
						Vertex2.Tangent = FVector4(FVector::ZeroVector, 0.0f);

						DecalMesh->Vertices.push_back(Vertex0);
						DecalMesh->Vertices.push_back(Vertex1);
						DecalMesh->Vertices.push_back(Vertex2);
						DecalMesh->Indices.push_back(BaseIndex + 0u);
						DecalMesh->Indices.push_back(BaseIndex + 1u);
						DecalMesh->Indices.push_back(BaseIndex + 2u);
					}

					if (DecalMesh->Indices.empty())
					{
						continue;
					}

					DecalMesh->Sections.push_back({0u, 0u, static_cast<uint32>(DecalMesh->Indices.size())});
					DecalMesh->UpdateLocalBound();

					const UMeshDecalComponent* MeshDecalComponent =
						reinterpret_cast<const UMeshDecalComponent*>(static_cast<uintptr_t>(Item.SourceComponentId));
					FMeshBatch Batch;
					Batch.MeshOwner = DecalMesh;
					Batch.Mesh = DecalMesh.get();
					Batch.Material = BuildContext.ResourceCache
						? BuildContext.ResourceCache->GetOrCreateMeshDecalMaterial(BuildContext, MeshDecalComponent)
						: BuildContext.DefaultTextureMaterial;
						Batch.World = FMatrix::Identity;
						Batch.WorldBounds.Center = DecalMesh->GetCenterCoord();
						Batch.WorldBounds.Radius = DecalMesh->GetLocalBoundRadius();
						Batch.WorldBounds.BoxExtent = (DecalMesh->GetMaxCoord() - DecalMesh->GetMinCoord()) * 0.5f;
					Batch.DistanceSqToCamera = (Batch.WorldBounds.Center - OutSceneViewData.View.CameraPosition).SizeSquared();
					Batch.Domain = EMaterialDomain::Opaque;
					Batch.PassMask = static_cast<uint32>(EMeshPassMask::ForwardMeshDecal);
					Batch.bDisableDepthWrite = true;
					Batch.bDisableCulling = true;
					SceneCommandBuilderUtils::AddBatch(BuildContext, OutSceneViewData, std::move(Batch));
				}
			}
		}
	}
}
