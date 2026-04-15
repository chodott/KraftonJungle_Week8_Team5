#include "Component/PrimitiveComponent.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"
#include "Debug/EngineLog.h"
#include "Actor/Actor.h"
#include "Level/Level.h"
#include <cmath>

IMPLEMENT_RTTI(UPrimitiveComponent, USceneComponent)

FRenderMesh* UPrimitiveComponent::GetRenderMesh() const
{
	return GetRenderMesh(FRenderMeshSelectionContext{});
}

FRenderMesh* UPrimitiveComponent::GetRenderMesh(const FRenderMeshSelectionContext& SelectionContext) const
{
	return GetRenderMesh(SelectionContext.Distance);
}

FRenderMesh* UPrimitiveComponent::GetRenderMesh(const float& Distance) const
{
	(void)Distance;
	return nullptr;
}

void UPrimitiveComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	USceneComponent::DuplicateShallow(DuplicatedObject, Context);

	UPrimitiveComponent* DuplicatedPrimitiveComponent = static_cast<UPrimitiveComponent*>(DuplicatedObject);
	DuplicatedPrimitiveComponent->Bounds = {};
	DuplicatedPrimitiveComponent->bDrawDebugBounds = bDrawDebugBounds;
	DuplicatedPrimitiveComponent->bIgnoreParentScaleInRender = bIgnoreParentScaleInRender;
	DuplicatedPrimitiveComponent->bEditorVisualization = bEditorVisualization;
}

void UPrimitiveComponent::PostDuplicate(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	USceneComponent::PostDuplicate(DuplicatedObject, Context);

	UPrimitiveComponent* DuplicatedPrimitiveComponent = static_cast<UPrimitiveComponent*>(DuplicatedObject);
	DuplicatedPrimitiveComponent->UpdateBounds();
}

void UPrimitiveComponent::MarkTransformDirty()
{
	USceneComponent::MarkTransformDirty();

	if (AActor* Owner = GetOwner())
	{
		if (ULevel* Level = Owner->GetLevel())
		{
			Level->MarkSpatialDirty();
		}
	}
}

FBoxSphereBounds UPrimitiveComponent::GetLocalBounds() const
{
	return { FVector(0, 0, 0), 0.f, FVector(0, 0, 0) };
}

FVector UPrimitiveComponent::GetRenderWorldScale() const
{
	const FVector WorldScale = GetWorldTransform().GetScaleVector();
	if (!bIgnoreParentScaleInRender)
	{
		return WorldScale;
	}

	const USceneComponent* ParentComponent = GetAttachParent();
	if (!ParentComponent)
	{
		return WorldScale;
	}

	const FVector ParentScale = ParentComponent->GetWorldTransform().GetScaleVector();
	const auto SafeDivide = [](float Value, float Divisor)
	{
		return std::abs(Divisor) > 1.e-6f ? (Value / Divisor) : Value;
	};

	return FVector(
		SafeDivide(WorldScale.X, ParentScale.X),
		SafeDivide(WorldScale.Y, ParentScale.Y),
		SafeDivide(WorldScale.Z, ParentScale.Z));
}

FMatrix UPrimitiveComponent::GetRenderWorldTransform() const
{
	FTransform RenderTransform(GetWorldTransform());
	RenderTransform.SetScale3D(GetRenderWorldScale());
	return RenderTransform.ToMatrixWithScale();
}

FMatrix UPrimitiveComponent::GetBoundsWorldTransform() const
{
	return bIgnoreParentScaleInRender ? GetRenderWorldTransform() : GetWorldTransform();
}

void UPrimitiveComponent::UpdateBounds()
{
	Bounds = CalcBounds(GetBoundsWorldTransform());
}

FBoxSphereBounds UPrimitiveComponent::CalcBounds(const FMatrix& LocalToWorld) const
{
	FBoxSphereBounds LocalBound = GetLocalBounds();

	if (LocalBound.Radius <= 0.f && LocalBound.BoxExtent.X == 0.f)
	{
		FVector Translation(LocalToWorld.M[3][0], LocalToWorld.M[3][1], LocalToWorld.M[3][2]);
		return { Translation, 1.0f, FVector(1, 1, 1) };
	}

	FVector Center = LocalToWorld.TransformPosition(LocalBound.Center);

	FMatrix AbsM = FMatrix::Abs(LocalToWorld);

	FVector WorldBoxExtent;
	WorldBoxExtent.X = AbsM.M[0][0] * LocalBound.BoxExtent.X
		+ AbsM.M[1][0] * LocalBound.BoxExtent.Y
		+ AbsM.M[2][0] * LocalBound.BoxExtent.Z;

	WorldBoxExtent.Y = AbsM.M[0][1] * LocalBound.BoxExtent.X
		+ AbsM.M[1][1] * LocalBound.BoxExtent.Y
		+ AbsM.M[2][1] * LocalBound.BoxExtent.Z;

	WorldBoxExtent.Z = AbsM.M[0][2] * LocalBound.BoxExtent.X
		+ AbsM.M[1][2] * LocalBound.BoxExtent.Y
		+ AbsM.M[2][2] * LocalBound.BoxExtent.Z;

	return { Center, WorldBoxExtent.Size(), WorldBoxExtent };
}

void UPrimitiveComponent::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);
	Ar.Serialize("DrawDebugBounds", bDrawDebugBounds);
	Ar.Serialize("IgnoreParentScaleInRender", bIgnoreParentScaleInRender);
	Ar.Serialize("EditorVisualization", bEditorVisualization);
}
