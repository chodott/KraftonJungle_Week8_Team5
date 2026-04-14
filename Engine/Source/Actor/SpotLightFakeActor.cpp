#include "Actor/SpotLightFakeActor.h"

#include <algorithm>

#include "Component/BillboardComponent.h"
#include "Component/DecalComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Paths.h"
#include "Math/LinearColor.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(ASpotLightFakeActor, AActor)

namespace
{
	const std::wstring GSpotLightFakeCircularMaskPath = L"__SpotLightFakeCircularMask__";

	template <typename TComponent>
	TComponent* FindSpotLightComponentByName(const ASpotLightFakeActor* Actor, const char* ComponentName)
	{
		if (!Actor)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component || !Component->IsA(TComponent::StaticClass()) || Component->GetName() != ComponentName)
			{
				continue;
			}

			return static_cast<TComponent*>(Component);
		}

		return nullptr;
	}
}

void ASpotLightFakeActor::PostSpawnInitialize()
{
	RootSceneComponent = FObjectFactory::ConstructObject<USceneComponent>(this, "RootSceneComponent");
	AddOwnedComponent(RootSceneComponent);

	DecalComponent = FObjectFactory::ConstructObject<UDecalComponent>(this, "DecalComponent");
	if (DecalComponent)
	{
		AddOwnedComponent(DecalComponent);
		DecalComponent->AttachTo(RootSceneComponent);
		DecalComponent->SetRelativeTransform(FTransform(
			FRotator(90.0f, 0.0f, 0.0f),
			FVector::ZeroVector,
			FVector::OneVector));
		DecalComponent->SetExtents(FVector(3.0f, 1.5f, 1.5f));
		SetDecalTexturePath(L"");
		DecalComponent->SetBaseColorTint(FLinearColor::White);
		DecalComponent->SetEnabled(true);
		DecalComponent->SetEdgeFade(DecalFadeRadius);
	}

	BillboardComponent = FObjectFactory::ConstructObject<UBillboardComponent>(this, "BillboardComponent");
	if (BillboardComponent)
	{
		AddOwnedComponent(BillboardComponent);
		BillboardComponent->AttachTo(RootSceneComponent);
		BillboardComponent->SetSize(FVector2(2.5f, 2.5f));
		BillboardComponent->SetAxisLockMode(UBillboardComponent::EAxisLockMode::LocalZ);
		BillboardComponent->SetTexturePath((FPaths::IconDir() / L"SpotLight1.png").wstring());
		BillboardComponent->SetHiddenInGame(false);
	}

	UpdateBillboardPlacement();

	AActor::PostSpawnInitialize();
}

void ASpotLightFakeActor::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);
	Ar.Serialize("DecalFadeEnabled", bDecalFadeEnabled);
	Ar.Serialize("DecalFadeRadius", DecalFadeRadius);

	if (Ar.IsLoading())
	{
		RootSceneComponent = FindSpotLightComponentByName<USceneComponent>(this, "RootSceneComponent");
		DecalComponent = FindSpotLightComponentByName<UDecalComponent>(this, "DecalComponent");
		BillboardComponent = FindSpotLightComponentByName<UBillboardComponent>(this, "BillboardComponent");

		if (RootSceneComponent)
		{
			SetRootComponent(RootSceneComponent);
		}

		DecalFadeRadius = (std::max)(0.0f, DecalFadeRadius);
		if (DecalComponent)
		{
			DecalComponent->DetachFromParent();
			if (RootSceneComponent)
			{
				DecalComponent->AttachTo(RootSceneComponent);
			}

			const std::filesystem::path TexturePath = FPaths::ToPath(FPaths::FromWide(DecalComponent->GetTexturePath()));
			if (TexturePath.filename() == std::filesystem::path(GSpotLightFakeCircularMaskPath))
			{
				SetDecalTexturePath(L"");
			}

			DecalComponent->SetEdgeFade(bDecalFadeEnabled ? DecalFadeRadius : 0.0f);
		}

		if (BillboardComponent)
		{
			BillboardComponent->DetachFromParent();
			if (RootSceneComponent)
			{
				BillboardComponent->AttachTo(RootSceneComponent);
			}
		}

		UpdateBillboardPlacement();
	}
}

void ASpotLightFakeActor::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	AActor::FixupDuplicatedReferences(DuplicatedObject, Context);
	ASpotLightFakeActor* Duplicated = static_cast<ASpotLightFakeActor*>(DuplicatedObject);
	Duplicated->RootSceneComponent = Context.FindDuplicate(RootSceneComponent);
	Duplicated->BillboardComponent = Context.FindDuplicate(BillboardComponent);
	Duplicated->DecalComponent = Context.FindDuplicate(DecalComponent);
}

void ASpotLightFakeActor::SetBillboardTexturePath(const std::wstring& InPath)
{
	if (BillboardComponent)
	{
		BillboardComponent->SetTexturePath(InPath);
	}
}

const std::wstring& ASpotLightFakeActor::GetBillboardTexturePath() const
{
	static const std::wstring Empty;
	return BillboardComponent ? BillboardComponent->GetTexturePath() : Empty;
}

void ASpotLightFakeActor::SetBillboardSize(const FVector2& InSize)
{
	if (BillboardComponent)
	{
		BillboardComponent->SetSize(InSize);
		UpdateBillboardPlacement();
	}
}

const FVector2& ASpotLightFakeActor::GetBillboardSize() const
{
	static const FVector2 Default(0.5f, 0.5f);
	return BillboardComponent ? BillboardComponent->GetSize() : Default;
}

void ASpotLightFakeActor::SetDecalTexturePath(const std::wstring& InPath)
{
	if (DecalComponent)
	{
		DecalComponent->SetTexturePath(InPath.empty() ? GSpotLightFakeCircularMaskPath : InPath);
	}
}

const std::wstring& ASpotLightFakeActor::GetDecalTexturePath() const
{
	static const std::wstring Empty;
	if (!DecalComponent)
	{
		return Empty;
	}

	const std::wstring& TexturePath = DecalComponent->GetTexturePath();
	return TexturePath == GSpotLightFakeCircularMaskPath ? Empty : TexturePath;
}

void ASpotLightFakeActor::SetDecalExtent(const FVector& InExtent)
{
	if (DecalComponent)
	{
		DecalComponent->SetExtents(InExtent);
		UpdateBillboardPlacement();
	}
}

FVector ASpotLightFakeActor::GetDecalExtent() const
{
	return DecalComponent ? DecalComponent->GetExtents() : FVector(3.0f, 1.5f, 1.5f);
}

void ASpotLightFakeActor::SetDecalFadeEnabled(bool bInEnabled)
{
	bDecalFadeEnabled = bInEnabled;

	if (DecalComponent)
	{
		DecalComponent->SetEdgeFade(bDecalFadeEnabled ? DecalFadeRadius : 0.0f);
	}
}

bool ASpotLightFakeActor::IsDecalFadeEnabled() const
{
	return bDecalFadeEnabled;
}

void ASpotLightFakeActor::SetDecalFadeRadius(float InRadius)
{
	DecalFadeRadius = (std::max)(0.0f, InRadius);
	if (DecalComponent && bDecalFadeEnabled)
	{
		DecalComponent->SetEdgeFade(DecalFadeRadius);
	}
}

float ASpotLightFakeActor::GetDecalFadeRadius() const
{
	return DecalFadeRadius;
}

void ASpotLightFakeActor::UpdateBillboardPlacement()
{
	if (!BillboardComponent || !DecalComponent)
	{
		return;
	}

	const FVector DecalExtent = DecalComponent->GetExtents();
	const FTransform& DecalTransform = DecalComponent->GetRelativeTransform();

	// 데칼 원점은 중심 → X 도 ±Extent.X 로 대칭
	const FVector Corners[8] =
	{
		FVector(-DecalExtent.X, -DecalExtent.Y, -DecalExtent.Z),
		FVector(-DecalExtent.X, -DecalExtent.Y,  DecalExtent.Z),
		FVector(-DecalExtent.X,  DecalExtent.Y, -DecalExtent.Z),
		FVector(-DecalExtent.X,  DecalExtent.Y,  DecalExtent.Z),
		FVector(DecalExtent.X, -DecalExtent.Y, -DecalExtent.Z),
		FVector(DecalExtent.X, -DecalExtent.Y,  DecalExtent.Z),
		FVector(DecalExtent.X,  DecalExtent.Y, -DecalExtent.Z),
		FVector(DecalExtent.X,  DecalExtent.Y,  DecalExtent.Z),
	};

	FVector MinBounds = DecalTransform.TransformPosition(Corners[0]);
	FVector MaxBounds = MinBounds;
	for (int32 CornerIndex = 1; CornerIndex < 8; ++CornerIndex)
	{
		const FVector WorldCorner = DecalTransform.TransformPosition(Corners[CornerIndex]);
		MinBounds.X = (std::min)(MinBounds.X, WorldCorner.X);
		MinBounds.Y = (std::min)(MinBounds.Y, WorldCorner.Y);
		MinBounds.Z = (std::min)(MinBounds.Z, WorldCorner.Z);
		MaxBounds.X = (std::max)(MaxBounds.X, WorldCorner.X);
		MaxBounds.Y = (std::max)(MaxBounds.Y, WorldCorner.Y);
		MaxBounds.Z = (std::max)(MaxBounds.Z, WorldCorner.Z);
	}

	const FVector2& BillboardSize = BillboardComponent->GetSize();
	BillboardComponent->SetRelativeLocation(FVector(
		(MinBounds.X + MaxBounds.X) * 0.5f,
		(MinBounds.Y + MaxBounds.Y) * 0.5f,
		MaxBounds.Z + BillboardSize.Y * 0.5f));
}
