#include "SceneManagerWindow.h"

#include "imgui.h"
#include "EditorEngine.h"
#include "Level/Level.h"
#include "Actor/Actor.h"
#include "Debug/EngineLog.h"

void FSceneManagerWindow::Render(FEditorEngine* Engine)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
	const bool bOpen = ImGui::Begin("Scene Manager");
	ImGui::PopStyleVar();
	if (!bOpen)
	{
		ImGui::End();
		return;
	}
	if (!Engine || !Engine->GetScene())
	{
		ImGui::End();
		return;
	}

	ImGuiIO& IO = ImGui::GetIO();
	const bool bShiftPressed = IO.KeyShift;
	ULevel* Scene = Engine->GetScene();
	const TArray<AActor*>& Actors = Scene->GetActors();
	const TArray<AActor*> SelectedActors = Engine->GetSelectedActors();
	const bool bHasSelection = !SelectedActors.empty();


	if (ImGui::Button("Clear Selection"))
	{
		Engine->ClearSelectedActors();
	}

	ImGui::SeparatorText("Actors");
	ImGui::Spacing();

	if (ImGui::BeginTable("SceneManagerTable", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Select", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableSetupColumn("Actor", ImGuiTableColumnFlags_WidthStretch, 0.0f);
		ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableHeadersRow();

		for (AActor* Actor : Actors)
		{
			if (!Actor || Actor->IsPendingDestroy())
			{
				continue;
			}

			const bool bSelected = Engine->IsActorSelected(Actor);
			ImGui::PushID(Actor);
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			bool bChecked = bSelected;
			if (ImGui::Checkbox("##selected", &bChecked))
			{
				if (bShiftPressed)
				{
					if (bChecked)
					{
						Engine->AddSelectedActor(Actor);
					}
					else
					{
						Engine->RemoveSelectedActor(Actor);
					}
				}
				else
				{
					if (bChecked)
					{
						Engine->SetSelectedActor(Actor);
					}
					else
					{
						Engine->ClearSelectedActors();
					}
				}
			}

			ImGui::TableSetColumnIndex(1);
			if (ImGui::Selectable(Actor->GetName().c_str(), bSelected))
			{
				if (bShiftPressed)
				{
					Engine->ToggleSelectedActor(Actor);
				}
				else
				{
					Engine->SetSelectedActor(Actor);
				}
			}

			ImGui::TableSetColumnIndex(2);
			bool bVisible = Actor->IsVisible();
			if (ImGui::Checkbox("##visible", &bVisible))
			{
				Actor->SetVisible(bVisible);
			}

			ImGui::TableSetColumnIndex(3);
			if (ImGui::SmallButton("Delete"))
			{
				const FString ActorName = Actor->GetName();
				Engine->RemoveSelectedActor(Actor);
				Scene->DestroyActor(Actor);
				UE_LOG("Deleted actor from Scene Manager: %s", ActorName.c_str());
				ImGui::PopID();
				continue;
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	ImGui::End();
}
