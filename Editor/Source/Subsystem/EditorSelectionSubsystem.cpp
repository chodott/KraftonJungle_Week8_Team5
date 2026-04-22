#include "Subsystem/EditorSelectionSubsystem.h"

#include <algorithm>

#include "Actor/Actor.h"

void FEditorSelectionSubsystem::Shutdown()
{
	ClearSelection();
}

void FEditorSelectionSubsystem::ClearSelection()
{
	SelectedActor = nullptr;
	SelectedActors.clear();
}

void FEditorSelectionSubsystem::RemoveInvalidSelections()
{
	SelectedActors.erase(
		std::remove_if(SelectedActors.begin(), SelectedActors.end(), [](const TObjectPtr<AActor>& Actor)
		{
			return !Actor || Actor->IsPendingDestroy();
		}),
		SelectedActors.end());

	if (SelectedActor && SelectedActor->IsPendingDestroy())
	{
		SelectedActor = nullptr;
	}

	if (!SelectedActor && !SelectedActors.empty())
	{
		SelectedActor = SelectedActors.back();
	}
}

void FEditorSelectionSubsystem::SetSelectedActor(AActor* InActor)
{
	ClearSelection();

	if (!InActor || InActor->IsPendingDestroy())
	{
		return;
	}

	SelectedActor = InActor;
	SelectedActors.push_back(InActor);
}

void FEditorSelectionSubsystem::AddSelectedActor(AActor* InActor)
{
	if (!InActor || InActor->IsPendingDestroy())
	{
		return;
	}

	RemoveInvalidSelections();
	if (!IsActorSelected(InActor))
	{
		SelectedActors.push_back(InActor);
	}
	SelectedActor = InActor;
}

void FEditorSelectionSubsystem::RemoveSelectedActor(AActor* InActor)
{
	if (!InActor)
	{
		return;
	}

	RemoveInvalidSelections();
	SelectedActors.erase(
		std::remove_if(SelectedActors.begin(), SelectedActors.end(), [InActor](const TObjectPtr<AActor>& Actor)
		{
			return Actor.Get() == InActor;
		}),
		SelectedActors.end());

	if (SelectedActor.Get() == InActor)
	{
		SelectedActor = SelectedActors.empty() ? nullptr : SelectedActors.back();
	}
}

void FEditorSelectionSubsystem::ToggleSelectedActor(AActor* InActor)
{
	if (!InActor || InActor->IsPendingDestroy())
	{
		return;
	}

	if (IsActorSelected(InActor))
	{
		RemoveSelectedActor(InActor);
	}
	else
	{
		AddSelectedActor(InActor);
	}
}

AActor* FEditorSelectionSubsystem::GetSelectedActor() const
{
	const_cast<FEditorSelectionSubsystem*>(this)->RemoveInvalidSelections();
	return SelectedActor.Get();
}

TArray<AActor*> FEditorSelectionSubsystem::GetSelectedActors() const
{
	const_cast<FEditorSelectionSubsystem*>(this)->RemoveInvalidSelections();

	TArray<AActor*> Result;
	Result.reserve(SelectedActors.size());
	for (const TObjectPtr<AActor>& Actor : SelectedActors)
	{
		if (Actor && !Actor->IsPendingDestroy())
		{
			Result.push_back(Actor.Get());
		}
	}
	return Result;
}

bool FEditorSelectionSubsystem::IsActorSelected(AActor* InActor) const
{
	if (!InActor || InActor->IsPendingDestroy())
	{
		return false;
	}

	const_cast<FEditorSelectionSubsystem*>(this)->RemoveInvalidSelections();
	for (const TObjectPtr<AActor>& Actor : SelectedActors)
	{
		if (Actor.Get() == InActor)
		{
			return true;
		}
	}

	return false;
}
