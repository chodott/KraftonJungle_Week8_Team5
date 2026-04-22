#include "ControlPanelWindow.h"
#include "Actor/Actor.h"
#include "Camera/Camera.h"
#include "Component/CameraComponent.h"
#include "Component/SkyComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextComponent.h"
#include "Controller/EditorViewportController.h"
#include "Core/Paths.h"
#include "Debug/EngineLog.h"
#include "EditorEngine.h"
#include "Level/Level.h"
#include "Object/ObjectFactory.h"
#include "Object/ObjectIterator.h"
#include "Renderer/Renderer.h"
#include "Serializer/SceneSerializer.h"
#include "World/WorldContext.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <random>

#include "Actor/BillboardActor.h"
#include "Actor/CubeActor.h"
#include "Actor/DecalActor.h"
#include "Actor/MeshDecalActor.h"
#include "Actor/DirectionalLightActor.h"
#include "Actor/AmbientLightActor.h"
#include "Actor/HeightFogActor.h"
#include "Actor/LocalHeightFogActor.h"
#include "Actor/PlaneActor.h"
#include "Actor/PlayerCameraActor.h"
#include "Actor/PointLightActor.h"
#include "Actor/SpotLightFakeActor.h"
#include "Actor/SpotLightActor.h"
#include "Actor/SphereActor.h"
#include "Actor/StaticMeshActor.h"
#include "Actor/SubUVActor.h"
#include "Actor/TextActor.h"
#include "Asset/ObjManager.h"
#include "Math/MathUtility.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Features/Lighting/BloomRenderFeature.h"

namespace
{
const char *GetWorldTypeLabel(EWorldType WorldType)
{
    switch (WorldType)
    {
    case EWorldType::Game:
        return "Game";
    case EWorldType::Editor:
        return "Editor";
    case EWorldType::PIE:
        return "PIE";
    case EWorldType::Preview:
        return "Preview";
    case EWorldType::Inactive:
        return "Inactive";
    default:
        return "Unknown";
    }
}

void RenderLevelGameplaySettings(FEditorEngine* Engine)
{
	if (!Engine)
	{
		return;
	}

	ULevel* CurrentScene = Engine->GetScene();
	if (!CurrentScene)
	{
		ImGui::TextDisabled("Scene unavailable.");
		return;
	}

	FLevelGameplaySettings GameplaySettings = CurrentScene->GetGameplaySettings();

	bool bAutoSpawnPlayerStart = GameplaySettings.bAutoSpawnPlayerStart;
	if (ImGui::Checkbox("Auto Spawn PlayerStart", &bAutoSpawnPlayerStart))
	{
		GameplaySettings.bAutoSpawnPlayerStart = bAutoSpawnPlayerStart;
		CurrentScene->SetGameplaySettings(GameplaySettings);

		if (bAutoSpawnPlayerStart)
		{
			CurrentScene->EnsurePlayerStartActor();
		}
	}

	std::string CurrentPawnAsset = GameplaySettings.DefaultPawnMeshAsset.empty()
		? "None"
		: GameplaySettings.DefaultPawnMeshAsset;

	ImGui::PushItemWidth(-1.0f);
	if (ImGui::BeginCombo("Default Pawn Mesh", CurrentPawnAsset.c_str()))
	{
		for (TObjectIterator<UStaticMesh> It; It; ++It)
		{
			UStaticMesh* MeshAsset = It.Get();
			if (!MeshAsset)
			{
				continue;
			}

			const FString AssetPathName = MeshAsset->GetAssetPathFileName();
			if (AssetPathName.empty())
			{
				continue;
			}

			const bool bSelected = (GameplaySettings.DefaultPawnMeshAsset == AssetPathName);
			if (ImGui::Selectable(AssetPathName.c_str(), bSelected))
			{
				GameplaySettings.DefaultPawnMeshAsset = AssetPathName;
				CurrentScene->SetGameplaySettings(GameplaySettings);
			}

			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}
	ImGui::PopItemWidth();

	float SpringArmLength = GameplaySettings.DefaultPawnSpringArmLength;
	if (ImGui::DragFloat("Spring Arm Length", &SpringArmLength, 0.05f, 0.0f, 1000.0f, "%.2f"))
	{
		GameplaySettings.DefaultPawnSpringArmLength = std::max(0.0f, SpringArmLength);
		CurrentScene->SetGameplaySettings(GameplaySettings);
	}

	float SpringArmSocketOffset[3] =
	{
		GameplaySettings.DefaultPawnSpringArmSocketOffset.X,
		GameplaySettings.DefaultPawnSpringArmSocketOffset.Y,
		GameplaySettings.DefaultPawnSpringArmSocketOffset.Z
	};
	if (ImGui::DragFloat3("Spring Arm Socket Offset", SpringArmSocketOffset, 0.05f))
	{
		GameplaySettings.DefaultPawnSpringArmSocketOffset =
			FVector(SpringArmSocketOffset[0], SpringArmSocketOffset[1], SpringArmSocketOffset[2]);
		CurrentScene->SetGameplaySettings(GameplaySettings);
	}

	ImGui::Text("PlayerStart Count: %d", CurrentScene->GetPlayerStartActorCount());
	if (ImGui::Button("Repair Essential Actors"))
	{
		CurrentScene->EnsureEssentialActors();
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Ensure Directional/Ambient/PlayerStart policy and fix duplicates.");
	}
}
} // namespace

void FControlPanelWindow::Render(FEditorEngine *Engine)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    const bool bOpen = ImGui::Begin("Control Panel");
    ImGui::PopStyleVar();

    if (!bOpen)
    {
        ImGui::End();
        return;
    }

    if (Engine && Engine->GetScene())
    {

        const FWorldContext *ActiveSceneContext = Engine->GetActiveWorldContext();
        const TArray<FWorldContext *> &PreviewSceneContexts = Engine->GetPreviewWorldContexts();
        const bool bPreviewActive = ActiveSceneContext && ActiveSceneContext->WorldType == EWorldType::Preview;

        /*
            PreviewScene 등 아마 확장의 여지를 둔 것으로 보이나 아무 기능도 없어 주석 처리함
        */
        /*
        ImGui::SeparatorText("World");

        if (ActiveSceneContext)
        {
            ImGui::Text("Active: %s", ActiveSceneContext->ContextName.c_str());
            ImGui::Text("Type: %s", GetWorldTypeLabel(ActiveSceneContext->WorldType));
        }
        */

        /*
        if (ImGui::Button("Editor Scene"))
        {
            Core->ActivateEditorScene();
        }
        */

        /*
        ImGui::SameLine();

        if (PreviewSceneContexts.empty())
        {
            ImGui::BeginDisabled();
            ImGui::Button("Preview Scene");
            ImGui::EndDisabled();
        }
        else if (ImGui::Button("Preview Scene"))
        {
            Core->ActivatePreviewScene(PreviewSceneContexts.front()->ContextName);
        }

        if (bPreviewActive)
        {
            ImGui::TextUnformatted("Preview scene is editor-only. Scene save/load is disabled.");
        }
        */

        ImGui::Dummy(ImVec2(0.0f, 5.0f));
        ImGui::SeparatorText("Camera");
        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        /*
        if (ImGui::Button("Spawn Test"))
        {
            UScene* Scene = Core->GetScene();
            AActor* NewActor = nullptr;

            for (int i = 0; i < 1000; i++)
            {
                // 시드: 현재 시간 기반
                static std::mt19937 rng(static_cast<unsigned int>(
                    std::chrono::steady_clock::now().time_since_epoch().count()
                    ));

                std::uniform_real_distribution<float> dist(-10, 10);

                FVector V{ 0, 0, 0 };
                NewActor = Scene->SpawnActor<ACubeActor>("Test");
                NewActor->SetActorLocation(V);
            }
        }
        */

        FSlateApplication *Slate = Engine->GetSlateApplication();
        FViewportId FocusedId = Slate ? Slate->GetFocusedViewportId() : INVALID_VIEWPORT_ID;
        FEditorViewportRegistry &ViewportRegistry = Engine->GetViewportRegistry();
        FViewportEntry *Entry = ViewportRegistry.FindEntryByViewportID(FocusedId);
        if (!Entry && !ViewportRegistry.GetEntries().empty())
            Entry = &ViewportRegistry.GetEntries().front();

        // Speed/Sensitivity는 FCamera에서 읽기 (렌더 무관 설정값)
        if (FCamera *Camera = Engine->GetScene()->GetCamera())
        {
            float Sensitivity = Camera->GetMouseSensitivity();
            if (ImGui::SliderFloat("Mouse Sensitivity", &Sensitivity, 0.01f, 1.0f))
                Camera->SetMouseSensitivity(Sensitivity);

            float Speed = Camera->GetSpeed();
            if (ImGui::SliderFloat("Move Speed", &Speed, 0.1f, 20.0f))
                Camera->SetSpeed(Speed);
        }
        if (Entry)
        {
            const bool bIsOrtho = (Entry->LocalState.ProjectionType != EViewportType::Perspective);
            FVector &PositionRef = bIsOrtho ? Entry->LocalState.OrthoTarget : Entry->LocalState.Position;
            float Position[3] = {PositionRef.X, PositionRef.Y, PositionRef.Z};
            if (ImGui::DragFloat3("Position", Position, 0.1f))
                PositionRef = {Position[0], Position[1], Position[2]};

            if (Entry->LocalState.ProjectionType == EViewportType::Perspective)
            {
                float Yaw = Entry->LocalState.Rotation.Yaw;
                float Pitch = Entry->LocalState.Rotation.Pitch;
                bool bRotationChanged = false;
                bRotationChanged |= ImGui::DragFloat("Yaw", &Yaw, 0.5f);
                bRotationChanged |= ImGui::DragFloat("Pitch", &Pitch, 0.5f, -89.0f, 89.0f);
                if (bRotationChanged)
                {
                    Entry->LocalState.Rotation.Yaw = Yaw;
                    Entry->LocalState.Rotation.Pitch = Pitch;
                }

                float FovY = Entry->LocalState.FovY;
                if (ImGui::SliderFloat("FOV", &FovY, 10.0f, 120.0f))
                    Entry->LocalState.FovY = FovY;
            }
            else
            {
                float OrthoZoom = Entry->LocalState.OrthoZoom;
                if (ImGui::DragFloat("Ortho Zoom", &OrthoZoom, 1.0f, 1.0f, 10000.0f))
                    Entry->LocalState.OrthoZoom = OrthoZoom;
            }
        }

		ImGui::Dummy(ImVec2(0.0f, 5.0f));
		ImGui::SeparatorText("Place Actor");
        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        static int32 SpawnTypeIndex = 0;
        static int32 SpawnAmount = 1;
		const char *SpawnTypes[] = {"Cube",           "Sphere",          "Plane",            "SubUV",
									"Text",           "Billboard",       "StaticMesh",       "HeightFog",
									"LocalHeightFog", "PlayerCamera",    "Decal",            "DirectionalLight",
									"PointLight",     "SpotLight",       "AmbientLight",     "SpotLightFake",
									"MeshDecal",      "PlayerStart"};

        ImGui::Combo("Type", &SpawnTypeIndex, SpawnTypes, IM_ARRAYSIZE(SpawnTypes));
        ImGui::InputInt("Count", &SpawnAmount);
        SpawnAmount = std::max(1, SpawnAmount);

        static char SpawnTextBuffer[256] = "Text";

        if (SpawnTypeIndex == 4)
        {
            ImGui::InputText("Text", SpawnTextBuffer, IM_ARRAYSIZE(SpawnTextBuffer));
        }

        if (ImGui::Button("Spawn"))
        {
            ULevel *Scene = Engine->GetScene();
            static int32 SpawnCount = 0;
            AActor *LastSpawnedActor = nullptr;

            for (int32 SpawnIndex = 0; SpawnIndex < SpawnAmount; ++SpawnIndex)
            {
                const FString Name = FString(SpawnTypes[SpawnTypeIndex]) + "_Spawned_" + std::to_string(SpawnCount++);
                AActor *NewActor = nullptr;

                // ─── 1. 특수 액터 (미리 조립된 테스트용 액터) ───
                if (SpawnTypeIndex == 0)
                {
                    NewActor = Scene->SpawnActor<ACubeActor>(Name);
                }
                else if (SpawnTypeIndex == 1)
                {
                    NewActor = Scene->SpawnActor<ASphereActor>(Name);
                }
                else if (SpawnTypeIndex == 2)
                {
                    NewActor = Scene->SpawnActor<APlaneActor>(Name);
                }
                // ─── 2. 순수 컴포넌트 조립 방식 (대통합!) ───
                else if (SpawnTypeIndex == 3)
                {
                    NewActor = Scene->SpawnActor<ASubUVActor>(Name);
                }
                else if (SpawnTypeIndex == 4)
                {
                    NewActor = Scene->SpawnActor<ATextActor>(Name);

                    if (NewActor)
                    {
                        ATextActor *TextActor = static_cast<ATextActor *>(NewActor);
                        if (UTextRenderComponent *TextComponent = TextActor->GetComponentByClass<UTextRenderComponent>())
                        {
                            if (SpawnTextBuffer[0] != '\0')
                                TextComponent->SetText(SpawnTextBuffer);
                            else
                                TextComponent->SetText("Text");
                        }
                    }
                }
                else if (SpawnTypeIndex == 5)
                {
                    NewActor = Scene->SpawnActor<ABillboardActor>(Name);
                }
                else if (SpawnTypeIndex == 6)
                {
                    NewActor = Scene->SpawnActor<AActor>(Name);
                    if (NewActor)
                    {
                        UStaticMeshComponent *MeshComp =
                            FObjectFactory::ConstructObject<UStaticMeshComponent>(nullptr, "StaticMeshComponent");

                        std::filesystem::path ModelPath = FPaths::MeshDir() / "cube-tex.obj";
                        FString FullPath = FPaths::FromPath(ModelPath);

                        UStaticMesh *MeshData = FObjManager::LoadObjStaticMeshAsset(FullPath);
                        if (MeshData)
                        {
                            MeshComp->SetStaticMesh(MeshData);
                            UE_LOG("[테스트] OBJ 파일 로드 성공! 섹션 개수: %d", MeshData->GetNumSections());

                            MeshComp->SetRelativeLocation(FVector(0, 0, 3.0f));
                        }
                        else
                        {
                            UE_LOG("[테스트 실패] OBJ 파일을 찾을 수 없거나 파싱에 실패했습니다.");
                        }
                        NewActor->AddOwnedComponent(MeshComp);
                        NewActor->SetRootComponent(MeshComp);
                    }
                }
                else if (SpawnTypeIndex == 7)
                {
                    NewActor = Scene->SpawnActor<AHeightFogActor>(Name);
                }
                else if (SpawnTypeIndex == 8)
                {
                    NewActor = Scene->SpawnActor<ALocalHeightFogActor>(Name);
                }
                else if (SpawnTypeIndex == 9)
                {
                    NewActor = Scene->SpawnActor<APlayerCameraActor>(Name);
                }
                else if (SpawnTypeIndex == 10)
                {
                    NewActor = Scene->SpawnActor<ADecalActor>(Name);
                }
                else if (SpawnTypeIndex == 11)
                {
                    NewActor = Scene->SpawnActor<ADirectionalLightActor>(Name);
                }
                else if (SpawnTypeIndex == 12)
                {
                    NewActor = Scene->SpawnActor<APointLightActor>(Name);
                }
                else if (SpawnTypeIndex == 13)
                {
                    NewActor = Scene->SpawnActor<ASpotLightActor>(Name);
                }
                else if (SpawnTypeIndex == 14)
                {
                    NewActor = Scene->SpawnActor<AAmbientLightActor>(Name);
                }
				else if (SpawnTypeIndex == 15)
				{
					NewActor = Scene->SpawnActor<ASpotLightFakeActor>(Name);
				}
				else if (SpawnTypeIndex == 16)
				{
					NewActor = Scene->SpawnActor<AMeshDecalActor>(Name);
				}
				else if (SpawnTypeIndex == 17)
				{
					NewActor = Scene->EnsurePlayerStartActor();
				}

                LastSpawnedActor = NewActor;
            }

            // ─── 마무리: 에디터 선택 및 로그 출력 ───
            Engine->SetSelectedActor(LastSpawnedActor);
            UE_LOG("Spawned %d %s actor(s)", SpawnAmount, SpawnTypes[SpawnTypeIndex]);
        }

        ImGui::Dummy(ImVec2(0.0f, 5.0f));
        ImGui::SeparatorText("Decal Projection Mode");
        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        if (FRenderer *Renderer = Engine->GetRenderer())
        {
            const EDecalProjectionMode Mode = Renderer->GetDecalProjectionMode();
            if (ImGui::RadioButton("Volume Draw", Mode == EDecalProjectionMode::VolumeDraw))
            {
                Renderer->SetDecalProjectionMode(EDecalProjectionMode::VolumeDraw);
            }
            if (ImGui::RadioButton("Clustered Lookup", Mode == EDecalProjectionMode::ClusteredLookup))
            {
                Renderer->SetDecalProjectionMode(EDecalProjectionMode::ClusteredLookup);
            }
        }
        else
        {
            ImGui::TextDisabled("Renderer unavailable.");
        }

		ImGui::Dummy(ImVec2(0.0f, 5.0f));
		ImGui::SeparatorText("Post Processing");
		ImGui::Dummy(ImVec2(0.0f, 5.0f));
		ImGui::Text("Bloom");

		if (FRenderer* Renderer = Engine->GetRenderer())
		{
			FBloomRenderFeature* Bloom = Renderer->GetBloomFeature();
			bool bApplyBloom = Bloom ? Bloom->IsBloomApplied() : false;
			if (ImGui::Checkbox("Apply", &bApplyBloom))
			{
				if (Bloom) Bloom->SetApplyBloom(bApplyBloom);
			}

			float Threshold = Bloom ? Bloom->GetThreshold() : 0.0f;
			float BloomIntensity = Bloom ? Bloom->GetBloomIntensity() : 0.0f;
			float Exposure = Bloom ? Bloom->GetExposure() : 0.0f;
			int Range = Bloom ? Bloom->GetBlurIterations() * 4 : 1 * 4;

			if (ImGui::DragInt("Range (px)", &Range, 4.0f, 0, 50 * 4))
			{
				if (Bloom) Bloom->SetBlurIterations(Range / 4);
			}
			if (ImGui::DragFloat("Threshold", &Threshold, 0.01f, 0.0f, 1.0f))
			{
				if (Bloom) Bloom->SetThreshold(Threshold);
			}
			if (ImGui::DragFloat("Bloom Intensity", &BloomIntensity, 0.05f, 0.0f, 30.0f))
			{
				if (Bloom) Bloom->SetBloomIntensity(BloomIntensity);
			}
			if (ImGui::DragFloat("Exposure", &Exposure, 0.05f, 0.0f, 10.0f))
			{
				if (Bloom) Bloom->SetExposure(Exposure);
			}
		}
		else
		{
			ImGui::TextDisabled("Renderer unavailable.");
		}
    }

	ImGui::End();
}

void FControlPanelWindow::RenderLevelGameplay(FEditorEngine* Engine, bool* bOpen)
{
	bool bWindowOpen = true;
	if (bOpen)
	{
		bWindowOpen = *bOpen;
	}

	if (!bWindowOpen)
	{
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
	const bool bVisible = ImGui::Begin("Level Gameplay", bOpen);
	ImGui::PopStyleVar();

	if (bVisible)
	{
		RenderLevelGameplaySettings(Engine);
	}

	ImGui::End();
}
