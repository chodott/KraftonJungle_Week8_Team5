#pragma once

#include "CoreMinimal.h"
#include "Types/ObjectPtr.h"

class AActor;

class FEditorSelectionSubsystem
{
public:
	void Shutdown();
	void ClearSelection();
	void SetSelectedActor(AActor* InActor);
	void AddSelectedActor(AActor* InActor);
	void RemoveSelectedActor(AActor* InActor);
	void ToggleSelectedActor(AActor* InActor);
	AActor* GetSelectedActor() const;
	TArray<AActor*> GetSelectedActors() const;
	bool IsActorSelected(AActor* InActor) const;

private:
	void RemoveInvalidSelections();

private:
	TObjectPtr<AActor> SelectedActor;
	TArray<TObjectPtr<AActor>> SelectedActors;
};
