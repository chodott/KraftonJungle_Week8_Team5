#include "EditorEngine.h"

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "Actor/Actor.h"
#include "Actor/PointLightActor.h"
#include "Actor/PlayerCameraActor.h"
#include "Actor/StaticMeshActor.h"
#include "Actor/SpotLightActor.h"
#include "Core/ShowFlags.h"
#include "Object/Class.h"
#include "Object/ObjectIterator.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/Renderer.h"
#include "Renderer/Features/Decal/DecalProjectionMode.h"
#include "Renderer/Features/Decal/DecalStats.h"
#include "Renderer/Features/Fog/FogStats.h"
#include "Renderer/GPUStats.h"
#include "Camera/Camera.h"
#include "Component/CameraComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/LineBatchComponent.h"
#include "Component/SpringArmComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/UUIDBillboardComponent.h"
#include "Core/ConsoleVariableManager.h"
#include "Core/Engine.h"
#include "Debug/EngineLog.h"
#include "Asset/ObjManager.h"
#include "Core/Paths.h"
#include "Object/ObjectFactory.h"
#include "Platform/Windows/WindowsWindow.h"
#include "Level/Level.h"
#include "Viewport/Viewport.h"
#include "Viewport/EditorViewportClient.h"
#include "Viewport/PreviewViewportClient.h"
#include "World/World.h"
#include "Slate/EditorViewportOverlay.h"

#include <cstring>
#include <filesystem>

namespace
{
	constexpr const char* PreviewSceneContextName = "PreviewScene";

	const TArray<FWorldContext*>& GetEmptyPreviewWorldContexts()
	{
		static TArray<FWorldContext*> EmptyPreviewWorldContexts;
		return EmptyPreviewWorldContexts;
	}

	void InitializeDefaultPreviewScene(FEditorEngine* Engine)
	{
		if (Engine == nullptr)
		{
			return;
		}

		FWorldContext* PreviewContext = Engine->CreatePreviewWorldContext(PreviewSceneContextName, 1280, 720);
		if (PreviewContext == nullptr || PreviewContext->World == nullptr)
		{
			return;
		}

		UWorld* PreviewWorld = PreviewContext->World;
		if (PreviewWorld->GetActors().empty())
		{
			/*AActor* PreviewActor = PreviewWorld->SpawnActor<AActor>("PreviewCube");
			if (PreviewActor)
			{
				UStaticMeshComponent* PreviewComponent = FObjectFactory::ConstructObject<UStaticMeshComponent>(PreviewActor);
				PreviewActor->AddOwnedComponent(PreviewComponent);
				PreviewActor->SetRootComponent(PreviewComponent);

				PreviewComponent->SetStaticMesh(FObjManager::GetPrimitiveCube());
				PreviewActor->SetActorLocation({ 0.0f, 0.0f, 0.0f });
			}*/
		}

		if (UCameraComponent* PreviewCamera = PreviewWorld->GetActiveCameraComponent())
		{
			PreviewCamera->GetCamera()->SetPosition({ -8.0f, -8.0f, 6.0f });
			PreviewCamera->GetCamera()->SetRotation(45.0f, -20.0f);
			PreviewCamera->SetFov(50.0f);
		}
	}

	void RefreshLightActorGizmosForLevel(FEditorEngine* Engine, ULevel* Level)
	{
		if (!Engine || !Level)
		{
			return;
		}

		for (AActor* Actor : Level->GetActors())
		{
			if (!Actor || Actor->IsPendingDestroy())
			{
				continue;
			}

			const bool bSelected = Engine->IsActorSelected(Actor);
			if (Actor->IsA(APointLightActor::StaticClass()))
			{
				static_cast<APointLightActor*>(Actor)->SetEditorGizmoVisible(bSelected);
			}
			else if (Actor->IsA(ASpotLightActor::StaticClass()))
			{
				static_cast<ASpotLightActor*>(Actor)->SetEditorGizmoVisible(bSelected);
			}
		}
	}

	bool IsLightComponentDebugGizmoName(const FString& Name)
	{
		constexpr const char* PointSuffix = "_PointRadiusGizmo";
		constexpr const char* SpotSuffix = "_SpotConeGizmo";

		auto HasSuffix = [](const FString& Value, const char* Suffix)
		{
			const size_t SuffixLength = std::strlen(Suffix);
			return Value.size() >= SuffixLength &&
				Value.compare(Value.size() - SuffixLength, SuffixLength, Suffix) == 0;
		};

		return HasSuffix(Name, PointSuffix) || HasSuffix(Name, SpotSuffix);
	}

	void RefreshAttachedLightComponentGizmosForLevel(FEditorEngine* Engine, ULevel* Level)
	{
		if (!Engine || !Level)
		{
			return;
		}

		for (AActor* Actor : Level->GetActors())
		{
			if (!Actor || Actor->IsPendingDestroy())
			{
				continue;
			}

			const bool bSelected = Engine->IsActorSelected(Actor);
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (!Component || !Component->IsA(ULineBatchComponent::StaticClass()))
				{
					continue;
				}

				ULineBatchComponent* LineBatchComponent = static_cast<ULineBatchComponent*>(Component);
				if (!IsLightComponentDebugGizmoName(LineBatchComponent->GetName()))
				{
					continue;
				}

				LineBatchComponent->SetEditorVisualization(bSelected);
			}
		}
	}

	UCameraComponent* EnsurePIECameraComponent(AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}

		USpringArmComponent* SpringArmComponent = Actor->GetComponentByClass<USpringArmComponent>();
		if (SpringArmComponent == nullptr)
		{
			SpringArmComponent = FObjectFactory::ConstructObject<USpringArmComponent>(Actor, "PIESpringArmComponent");
			if (SpringArmComponent)
			{
				Actor->AddOwnedComponent(SpringArmComponent);
				if (USceneComponent* RootSceneComponent = Actor->GetRootComponent())
				{
					SpringArmComponent->AttachTo(RootSceneComponent);
				}
				SpringArmComponent->SetTargetArmLength(4.0f);
				SpringArmComponent->SetSocketOffset(FVector(0.0f, 0.0f, 1.5f));
				if (!SpringArmComponent->IsRegistered())
				{
					SpringArmComponent->OnRegister();
				}
			}
		}

		UCameraComponent* CameraComponent = Actor->GetComponentByClass<UCameraComponent>();
		if (CameraComponent == nullptr)
		{
			CameraComponent = FObjectFactory::ConstructObject<UCameraComponent>(Actor, "PIEPlayerCameraComponent");
			if (CameraComponent)
			{
				Actor->AddOwnedComponent(CameraComponent);
				if (SpringArmComponent)
				{
					CameraComponent->AttachTo(SpringArmComponent);
				}
				else if (USceneComponent* RootSceneComponent = Actor->GetRootComponent())
				{
					CameraComponent->AttachTo(RootSceneComponent);
				}
				if (!CameraComponent->IsRegistered())
				{
					CameraComponent->OnRegister();
				}
			}
		}

		return CameraComponent;
	}

}

FEditorEngine::~FEditorEngine() = default;

void FEditorEngine::Shutdown()
{
	FEngineLog::Get().SetCallback({});
	if (IsPIEActive() || IsPIEPaused()) EndPIE();
	for (auto& Pair : BillboardSelectionTintOriginalColors)
	{
		if (Pair.first && !Pair.first->IsPendingKill())
		{
			Pair.first->SetBaseColorLinear(Pair.second);
		}
	}
	BillboardSelectionTintOriginalColors.clear();
	EditorUI.SaveEditorSettings();

	if (GetViewportClient() == PreviewViewportClient.get())
	{
		SetViewportClient(nullptr);
	}

	PreviewViewportClient.reset();
	CameraSubsystem.Shutdown();
	SelectionSubsystem.Shutdown();
	ReleaseEditorWorlds();

	FEngine::Shutdown();
}

void FEditorEngine::SetSelectedActor(AActor* InActor)
{
	SelectionSubsystem.SetSelectedActor(InActor);
	RefreshLightGizmoSelectionVisibility();
}

AActor* FEditorEngine::GetSelectedActor() const
{
	return SelectionSubsystem.GetSelectedActor();
}

TArray<AActor*> FEditorEngine::GetSelectedActors() const
{
	return SelectionSubsystem.GetSelectedActors();
}

bool FEditorEngine::IsActorSelected(AActor* InActor) const
{
	return SelectionSubsystem.IsActorSelected(InActor);
}

void FEditorEngine::AddSelectedActor(AActor* InActor)
{
	SelectionSubsystem.AddSelectedActor(InActor);
	RefreshLightGizmoSelectionVisibility();
}

void FEditorEngine::RemoveSelectedActor(AActor* InActor)
{
	SelectionSubsystem.RemoveSelectedActor(InActor);
	RefreshLightGizmoSelectionVisibility();
}

void FEditorEngine::ToggleSelectedActor(AActor* InActor)
{
	SelectionSubsystem.ToggleSelectedActor(InActor);
	RefreshLightGizmoSelectionVisibility();
}

void FEditorEngine::ClearSelectedActors()
{
	SelectionSubsystem.ClearSelection();
	RefreshLightGizmoSelectionVisibility();
}

void FEditorEngine::PlaySimulation()
{
	if (!bIsPIEActive)
	{
		StartPIE();
		return;
	}

	if (bIsPIEPaused)
	{
		TogglePIEPause();
	}
}

void FEditorEngine::PauseSimulation()
{
	if (bIsPIEActive && !bIsPIEPaused)
	{
		TogglePIEPause();
	}
}

void FEditorEngine::StopSimulation()
{
	if (bIsPIEActive)
	{
		EndPIE();
	}
}

FEditorEngine::ESimulationPlaybackState FEditorEngine::GetSimulationPlaybackState() const
{
	if (!bIsPIEActive)
	{
		return ESimulationPlaybackState::Stopped;
	}

	return bIsPIEPaused ? ESimulationPlaybackState::Paused : ESimulationPlaybackState::Playing;
}

void FEditorEngine::ActivateEditorScene()
{
	ActiveWorldContext = (EditorWorldContext && EditorWorldContext->World) ? EditorWorldContext : nullptr;
}

bool FEditorEngine::ActivatePreviewScene(const FString& ContextName)
{
	FWorldContext* PreviewContext = FindPreviewWorld(ContextName);
	if (PreviewContext == nullptr)
	{
		return false;
	}

	ActiveWorldContext = PreviewContext;
	return true;
}

ULevel* FEditorEngine::GetEditorScene() const
{
	return (EditorWorldContext && EditorWorldContext->World) ? EditorWorldContext->World->GetScene() : nullptr;
}

ULevel* FEditorEngine::GetPreviewScene(const FString& ContextName) const
{
	const FWorldContext* Context = FindPreviewWorld(ContextName);
	return (Context && Context->World) ? Context->World->GetScene() : nullptr;
}

UWorld* FEditorEngine::GetEditorWorld() const
{
	return EditorWorldContext ? EditorWorldContext->World : nullptr;
}

const TArray<FWorldContext*>& FEditorEngine::GetPreviewWorldContexts() const
{
	return PreviewWorldContexts.empty() ? GetEmptyPreviewWorldContexts() : PreviewWorldContexts;
}

FWorldContext* FEditorEngine::CreatePreviewWorldContext(const FString& ContextName, int32 Width, int32 Height)
{
	if (ContextName.empty())
	{
		return nullptr;
	}

	if (FWorldContext* ExistingContext = FindPreviewWorld(ContextName))
	{
		return ExistingContext;
	}

	const float AspectRatio = (Height > 0) ? (static_cast<float>(Width) / static_cast<float>(Height)) : 1.0f;
	FWorldContext* PreviewContext = CreateWorldContext(ContextName, EWorldType::Preview, AspectRatio, false);
	if (!PreviewContext)
	{
		return nullptr;
	}

	PreviewWorldContexts.push_back(PreviewContext);
	return PreviewContext;
}

ULevel* FEditorEngine::GetScene() const
{
	return GetActiveScene();
}

ULevel* FEditorEngine::GetActiveScene() const
{
	UWorld* ActiveWorld = GetActiveWorld();
	return ActiveWorld ? ActiveWorld->GetScene() : nullptr;
}

UWorld* FEditorEngine::GetActiveWorld() const
{
	return ActiveWorldContext ? ActiveWorldContext->World : FEngine::GetActiveWorld();
}

const FWorldContext* FEditorEngine::GetActiveWorldContext() const
{
	return ActiveWorldContext ? ActiveWorldContext : FEngine::GetActiveWorldContext();
}

void FEditorEngine::HandleResize(int32 Width, int32 Height)
{
	FEngine::HandleResize(Width, Height);

	if (Width == 0 || Height == 0)
	{
		return;
	}

	UpdateEditorWorldAspectRatio(static_cast<float>(Width) / static_cast<float>(Height));
}

void FEditorEngine::PreInitialize()
{
	ImGui_ImplWin32_EnableDpiAwareness();

	FEngineLog::Get().SetCallback([this](const char* Msg)
	{
		EditorUI.GetConsole().AddLog("%s", Msg);
	});
}

void FEditorEngine::BindHost(FWindowsWindow* InMainWindow)
{
	MainWindow = InMainWindow;
	EditorUI.SetupWindow(InMainWindow);
}

bool FEditorEngine::InitializeWorlds()
{
	return InitEditorWorlds();
}

bool FEditorEngine::InitializeMode()
{
	if (!InitEditorPreview())
	{
		return false;
	}

	InitEditorConsole();

	if (!InitEditorCamera())
	{
		return false;
	}

	InitEditorViewportRouting();
	return true;
}

void FEditorEngine::FinalizeInitialize()
{
	UE_LOG("EditorEngine initialized");
	const int32 W = MainWindow ? MainWindow->GetWidth() : 800;
	const int32 H = MainWindow ? MainWindow->GetHeight() : 600;

	TArray<FViewport>& Viewports = ViewportRegistry.GetViewports();
	FViewport* VPs[MAX_VIEWPORTS] = {
		&Viewports[0], &Viewports[1], &Viewports[2], &Viewports[3]
	};

	SlateApplication = std::make_unique<FSlateApplication>();
	SlateApplication->Initialize(FRect(0, 0, W, H), VPs, MAX_VIEWPORTS);
	EditorUI.OnSlateReady();
	CreateInitUI();
	RefreshLightGizmoSelectionVisibility();
}

void FEditorEngine::RefreshLightGizmoSelectionVisibility()
{
	RefreshLightActorGizmosForLevel(this, GetEditorScene());
	RefreshAttachedLightComponentGizmosForLevel(this, GetEditorScene());

	for (FWorldContext* PreviewContext : PreviewWorldContexts)
	{
		if (!PreviewContext || !PreviewContext->World)
		{
			continue;
		}

		RefreshLightActorGizmosForLevel(this, PreviewContext->World->GetScene());
		RefreshAttachedLightComponentGizmosForLevel(this, PreviewContext->World->GetScene());
	}

	RefreshSelectedBillboardTint();
}

void FEditorEngine::RefreshSelectedBillboardTint()
{
	const FLinearColor HighlightTint = FLinearColor(1.0f, 0.62f, 0.22f, 1.0f);
	const TArray<AActor*> SelectedActors = GetSelectedActors();
	TSet<UBillboardComponent*> SelectedBillboards;

	for (AActor* SelectedActor : SelectedActors)
	{
		if (!SelectedActor || SelectedActor->IsPendingDestroy())
		{
			continue;
		}

		for (UActorComponent* Component : SelectedActor->GetComponents())
		{
			if (!Component || !Component->IsA(UBillboardComponent::StaticClass()))
			{
				continue;
			}

			UBillboardComponent* BillboardComponent = static_cast<UBillboardComponent*>(Component);
			if (BillboardComponent->IsPendingKill() || BillboardComponent->IsA(UUUIDBillboardComponent::StaticClass()))
			{
				continue;
			}

			SelectedBillboards.insert(BillboardComponent);
		}
	}

	for (auto It = BillboardSelectionTintOriginalColors.begin(); It != BillboardSelectionTintOriginalColors.end();)
	{
		UBillboardComponent* BillboardComponent = It->first;
		if (!BillboardComponent || BillboardComponent->IsPendingKill())
		{
			It = BillboardSelectionTintOriginalColors.erase(It);
			continue;
		}

		if (SelectedBillboards.find(BillboardComponent) == SelectedBillboards.end())
		{
			BillboardComponent->SetBaseColorLinear(It->second);
			It = BillboardSelectionTintOriginalColors.erase(It);
			continue;
		}

		++It;
	}

	for (UBillboardComponent* BillboardComponent : SelectedBillboards)
	{
		if (!BillboardComponent || BillboardComponent->IsPendingKill())
		{
			continue;
		}

		if (BillboardSelectionTintOriginalColors.find(BillboardComponent) == BillboardSelectionTintOriginalColors.end())
		{
			BillboardSelectionTintOriginalColors[BillboardComponent] = BillboardComponent->GetBaseColor();
		}
		BillboardComponent->SetBaseColorLinear(HighlightTint);
	}
}

void FEditorEngine::PrepareFrame(float DeltaTime)
{
	FEngine::PrepareFrame(DeltaTime);

	if (bIsPIEActive && PIEViewportId != INVALID_VIEWPORT_ID)
	{
		if (SlateApplication && !SlateApplication->IsViewportActive(PIEViewportId))
		{
			EndPIE();
		}
	}

	SyncViewportClient();
	SyncFocusedViewportLocalState();
	RefreshSelectedBillboardTint();
	CameraSubsystem.PrepareFrame(GetActiveWorld(), GetScene(), DeltaTime);
}

void FEditorEngine::TickWorlds(float DeltaTime)
{
	if (bIsPIEActive)
	{
		if (bIsPIEPaused)
		{
			return;
		}

		if (PIEWorldContext && PIEWorldContext->World)
		{
			PIEWorldContext->World->Tick(DeltaTime);
		}
		return;
	}

	if (UWorld* ActiveWorld = GetActiveWorld())
	{
		ActiveWorld->Tick(DeltaTime);
	}
}

std::unique_ptr<IViewportClient> FEditorEngine::CreateViewportClient()
{
	auto Client = std::make_unique<FEditorViewportClient>(*this, EditorUI, ViewportRegistry, MainWindow);
	EditorViewportClientRaw = Client.get();
	return Client;
}

void FEditorEngine::RenderFrame()
{
	FRenderer* Renderer = GetRenderer();
	if (!Renderer || Renderer->IsOccluded())
	{
		return;
	}

	Renderer->BeginFrame();
	EditorUI.BeginFrame();

	if (EditorViewportClientRaw)
	{
		EditorViewportClientRaw->Render(this, Renderer);
	}

	EditorUI.EndFrame();
	Renderer->EndFrame();
}

void FEditorEngine::SyncPlatformState()
{
	SyncPlatformCursor();
	SyncPIECursorState();
}

FEditorViewportController* FEditorEngine::GetViewportController()
{
	return CameraSubsystem.GetViewportController();
}

void FEditorEngine::ClearDebugDrawForFrame()
{
	GetDebugDrawManager().Clear();
}

void FEditorEngine::CreateInitUI()
{
	auto* RawEditorVP = static_cast<FEditorViewportClient*>(ViewportClient.get());
	std::unique_ptr<SEditorViewportOverlay> Overlay = std::make_unique<SEditorViewportOverlay>(this, &EditorUI, RawEditorVP);
	SWidget* RawOverlay = SlateApplication->CreateWidget(std::move(Overlay));
	SlateApplication->AddOverlayWidget(RawOverlay);
}

bool FEditorEngine::StartPIE()
{
	if (bIsPIEActive)
	{
		return false;
	}

	if (EditorWorldContext == nullptr || EditorWorldContext->World == nullptr)
	{
		return false;
	}

	if (ULevel* EditorLevel = EditorWorldContext->World->GetScene())
	{
		EditorLevel->EnsureEssentialActors();
	}

	UWorld* PIEWorld = UWorld::DuplicateWorldForPIE(EditorWorldContext->World);
	if (PIEWorld == nullptr)
	{
		return false;
	}

	PIEWorld->ResetRuntimeState();

	const float AspectRatio = GetWindowAspectRatio();
	PIEWorldContext = CreateWorldContext("PIE", EWorldType::PIE, PIEWorld);
	if (PIEWorldContext == nullptr)
	{
		PIEWorld->CleanupWorld();
		PIEWorld->MarkPendingKill();
		return false;
	}

	UpdateWorldAspectRatio(PIEWorld, AspectRatio);

	SavedPIEViewportStates.clear();
	for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
	{
		if (!Entry.bActive)
		{
			continue;
		}

		FPIEViewportStateBackup Backup;
		Backup.ViewportId = Entry.Id;
		Backup.WorldContext = Entry.WorldContext;
		Backup.LocalState = Entry.LocalState;
		Backup.LocalState.ViewMode = Entry.LocalState.ViewMode;
		Backup.LocalState.ShowFlags = Entry.LocalState.ShowFlags;
		SavedPIEViewportStates.push_back(Backup);
	}

	SavedPIESelectedActor = GetSelectedActor();

	FViewportEntry* PIEViewportEntry = nullptr;
	if (SlateApplication)
	{
		const FViewportId FocusedId = SlateApplication->GetFocusedViewportId();
		if (FocusedId != INVALID_VIEWPORT_ID)
		{
			FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
			if (FocusedEntry && FocusedEntry->bActive)
			{
				PIEViewportEntry = FocusedEntry;
			}
		}
	}

	if (PIEViewportEntry == nullptr)
	{
		for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
		{
			if (Entry.bActive)
			{
				PIEViewportEntry = &Entry;
				break;
			}
		}
	}

	if (PIEViewportEntry)
	{
		PIEViewportEntry->WorldContext = PIEWorldContext;
		PIEViewportId = PIEViewportEntry->Id;
		PIEViewportEntry->LocalState.ShowFlags.SetFlag(EEngineShowFlags::SF_UUID, false);
		PIEViewportEntry->LocalState.ShowFlags.SetFlag(EEngineShowFlags::SF_DebugDraw, false);
		PIEViewportEntry->LocalState.ShowFlags.SetFlag(EEngineShowFlags::SF_DebugVolume, false);
		PIEViewportEntry->LocalState.ShowFlags.SetFlag(EEngineShowFlags::SF_WorldAxis, false);
		if (PIEViewportEntry->LocalState.ProjectionType == EViewportType::Perspective)
		{
			PIEViewportEntry->LocalState.Position = FVector::ZeroVector;
			PIEViewportEntry->LocalState.Rotation = FRotator::ZeroRotator;
			PIEViewportEntry->LocalState.bShowGrid = false;
		}
		else
		{
			// PIE should not inherit the last perspective camera when launched from
			// an ortho viewport. Start from a deterministic perspective state instead.
			const FViewportLocalState PreviousViewportState = PIEViewportEntry->LocalState;
			FViewportLocalState PIEViewportState = FViewportLocalState::CreateDefault(EViewportType::Perspective);
			PIEViewportState.ShowFlags = PreviousViewportState.ShowFlags;
			PIEViewportState.ViewMode = PreviousViewportState.ViewMode;
			PIEViewportState.GridSize = PreviousViewportState.GridSize;
			PIEViewportState.LineThickness = PreviousViewportState.LineThickness;
			PIEViewportState.NearPlane = PreviousViewportState.NearPlane;
			PIEViewportState.FarPlane = PreviousViewportState.FarPlane;
			PIEViewportState.bShowGrid = false;
			PIEViewportEntry->LocalState = PIEViewportState;
		}
	}

	ApplyPIEStartView();

	RefreshPIEPlayerCameraActors();

	SetSelectedActor(nullptr);

	PrePIEActiveWorldContext = ActiveWorldContext ? ActiveWorldContext : EditorWorldContext;
	ActiveWorldContext = PIEWorldContext;
	bIsPIEActive = true;
	bIsPIEPaused = false;
	bIsPIEInputCaptured = true;
	PIEPossessionState = EPIEPossessionState::Possessed;
	bWasCursorHiddenForPIE = true;
	bIsPIECursorCurrentlyHidden = false;
	CenterCursorInPIEViewport();
	SyncPIECursorState();

	PIEWorld->BeginPlay();
	return true;
}

void FEditorEngine::EndPIE()
{
	if (!bIsPIEActive)
	{
		return;
	}

	for (const FPIEViewportStateBackup& Backup : SavedPIEViewportStates)
	{
		FViewportEntry* RestoreViewportEntry = ViewportRegistry.FindEntryByViewportID(Backup.ViewportId);
		if (RestoreViewportEntry)
		{
			RestoreViewportEntry->WorldContext = Backup.WorldContext;
			RestoreViewportEntry->LocalState = Backup.LocalState;
		}
	}
	SavedPIEViewportStates.clear();

	DestroyWorldContext(PIEWorldContext);
	PIEWorldContext = nullptr;

	ActiveWorldContext = PrePIEActiveWorldContext ? PrePIEActiveWorldContext : EditorWorldContext;
	PrePIEActiveWorldContext = nullptr;
	SetSelectedActor(SavedPIESelectedActor.Get());
	SavedPIESelectedActor = nullptr;

	if (bWasCursorHiddenForPIE)
	{
		if (bIsPIECursorCurrentlyHidden)
		{
			::ShowCursor(TRUE);
		}
		bWasCursorHiddenForPIE = false;
		bIsPIECursorCurrentlyHidden = false;
	}
	::ClipCursor(nullptr);

	bIsPIEActive = false;
	bIsPIEPaused = false;
	bIsPIEInputCaptured = false;
	PIEPossessionState = EPIEPossessionState::Ejected;
	PIEViewportId = INVALID_VIEWPORT_ID;
	PIEPlayerCameraActors.clear();
	ActivePIEPlayerCameraIndex = -1;
}

void FEditorEngine::TogglePIEPause()
{
	if (bIsPIEActive)
	{
		bIsPIEPaused = !bIsPIEPaused;
	}
}

void FEditorEngine::CapturePIEInput()
{
	if (!bIsPIEActive || bIsPIEPaused || PIEViewportId == INVALID_VIEWPORT_ID)
	{
		return;
	}

	if (FViewportEntry* PIEViewportEntry = ViewportRegistry.FindEntryByViewportID(PIEViewportId))
	{
		PIEViewportEntry->LocalState.ShowFlags.SetFlag(EEngineShowFlags::SF_UUID, false);
		PIEViewportEntry->LocalState.ShowFlags.SetFlag(EEngineShowFlags::SF_DebugDraw, false);
		PIEViewportEntry->LocalState.ShowFlags.SetFlag(EEngineShowFlags::SF_DebugVolume, false);
		PIEViewportEntry->LocalState.ShowFlags.SetFlag(EEngineShowFlags::SF_WorldAxis, false);
		PIEViewportEntry->LocalState.bShowGrid = false;
	}

	bIsPIEInputCaptured = true;
	PIEPossessionState = EPIEPossessionState::Possessed;
	bWasCursorHiddenForPIE = true;
	CenterCursorInPIEViewport();
	SyncPIECursorState();
}

void FEditorEngine::ReleasePIEInputCapture()
{
	if (!bIsPIEActive)
	{
		return;
	}

	if (FViewportEntry* PIEViewportEntry = ViewportRegistry.FindEntryByViewportID(PIEViewportId))
	{
		for (const FPIEViewportStateBackup& Backup : SavedPIEViewportStates)
		{
			if (Backup.ViewportId == PIEViewportId)
			{
				PIEViewportEntry->LocalState = Backup.LocalState;
				break;
			}
		}
	}

	bIsPIEInputCaptured = false;
	PIEPossessionState = EPIEPossessionState::Ejected;
	SyncPIECursorState();
}

void FEditorEngine::TogglePIEPossession()
{
	if (!bIsPIEActive)
	{
		return;
	}

	if (PIEPossessionState == EPIEPossessionState::Possessed)
	{
		ReleasePIEInputCapture();
		return;
	}

	ApplyPIEStartView();
	CapturePIEInput();
}

bool FEditorEngine::CyclePIEPlayerCamera(int32 Direction)
{
	const int32 CameraCount = static_cast<int32>(PIEPlayerCameraActors.size());
	if (!bIsPIEActive || CameraCount <= 0)
	{
		return false;
	}

	int32 NextCameraIndex = ActivePIEPlayerCameraIndex;
	if (NextCameraIndex < 0 || NextCameraIndex >= CameraCount)
	{
		NextCameraIndex = 0;
	}
	else
	{
		NextCameraIndex = (NextCameraIndex + Direction) % CameraCount;
		if (NextCameraIndex < 0)
		{
			NextCameraIndex += CameraCount;
		}
	}

	return ApplyPIEPlayerCameraByIndex(NextCameraIndex);
}

bool FEditorEngine::InitEditorPreview()
{
	InitializeDefaultPreviewScene(this);
	PreviewViewportClient = std::make_unique<FPreviewViewportClient>(EditorUI, PreviewSceneContextName);
	return PreviewViewportClient != nullptr;
}

void FEditorEngine::InitEditorConsole()
{
	FConsoleVariableManager& CVM = FConsoleVariableManager::Get();
	if (!CVM.Find("r.DecalProjectionMode"))
	{
		CVM.Register(
			"r.DecalProjectionMode",
			static_cast<int32>(EDecalProjectionMode::ClusteredLookup),
			"Decal projection mode (0 = Clustered Lookup, 1 = Volume Draw)");
	}

	CVM.GetAllNames([this](const FString& Name)
	{
		EditorUI.GetConsole().RegisterCommand(Name.c_str());
	});

	EditorUI.GetConsole().SetCommandHandler([this](const char* CommandLine)
	{
		if (std::strcmp(CommandLine, "stat gpu") == 0)
		{
			FRenderer* Renderer = GetRenderer();
			if (!Renderer)
			{
				FEngineLog::Get().Log("[error] Renderer is not available.");
				return;
			}

			const FGPUFrameStats Stats = Renderer->GetGPUStats();
			FEngineLog::Get().Log("[GPU Stat]");
			FEngineLog::Get().Log("Geometry Draw Calls: %u", Stats.GeometryDrawCalls);
			FEngineLog::Get().Log("Fullscreen Pass Count: %u", Stats.FullscreenPassCount);
			FEngineLog::Get().Log("Total Draw Calls: %u", Stats.DrawCallCount);
			FEngineLog::Get().Log("Pass Count: %u", Stats.PassCount);
			FEngineLog::Get().Log("");
			FEngineLog::Get().Log("Geometry Cost: %.2f ms", Stats.GeometryTimeMs);
			FEngineLog::Get().Log("Pixel Shading Cost: %.2f ms", Stats.PixelShadingTimeMs);
			FEngineLog::Get().Log("Memory / Bandwidth Cost: %.2f ms", Stats.MemoryBandwidthTimeMs);
			FEngineLog::Get().Log("Overdraw / Fillrate Cost: %.2f ms", Stats.OverdrawFillrateTimeMs);
			FEngineLog::Get().Log("");
			FEngineLog::Get().Log("Decal Draw Calls: %u", Stats.DecalDrawCalls);
			FEngineLog::Get().Log("Fog Draw Calls: %u", Stats.FogDrawCalls);
			FEngineLog::Get().Log("Upload Bytes: %.2f KB", Stats.UploadBytes / 1024.0);
			FEngineLog::Get().Log("Scene Copy Bytes: %.2f MB", Stats.CopyBytes / (1024.0 * 1024.0));
			FEngineLog::Get().Log("Estimated Fullscreen Pixels: %.2f M", Stats.EstimatedFullscreenPixels / 1000000.0);
			FEngineLog::Get().Log("[note] Geometry/overdraw are engine-side aggregates. D3D11 hardware counters are not sampled here.");
			return;
		}

		if (std::strcmp(CommandLine, "stat fog") == 0)
		{
			FRenderer* Renderer = GetRenderer();
			if (!Renderer)
			{
				FEngineLog::Get().Log("[error] Renderer is not available.");
				return;
			}

			const FFogStats Stats = Renderer->GetFogStats();
			FEngineLog::Get().Log("[Fog Stat]");
			FEngineLog::Get().Log("Total Fog Volumes: %u", Stats.Common.TotalFogVolumes);
			FEngineLog::Get().Log("Global Fog Volumes: %u", Stats.Common.GlobalFogVolumes);
			FEngineLog::Get().Log("Local Fog Volumes: %u", Stats.Common.LocalFogVolumes);
			FEngineLog::Get().Log("Registered Local Fog Volumes: %u", Stats.Common.RegisteredLocalFogVolumes);
			FEngineLog::Get().Log("");
			FEngineLog::Get().Log("Cluster Count: %u", Stats.Common.ClusterCount);
			FEngineLog::Get().Log("Non-Empty Clusters: %u", Stats.Common.NonEmptyClusterCount);
			FEngineLog::Get().Log("Cluster Index Count: %u", Stats.Common.ClusterIndexCount);
			FEngineLog::Get().Log("Max Fog Per Cluster: %u", Stats.Common.MaxFogPerCluster);
			FEngineLog::Get().Log("");
			FEngineLog::Get().Log("Fullscreen Pass Count: %u", Stats.Common.FullscreenPassCount);
			FEngineLog::Get().Log("Draw Call Count: %u", Stats.Common.DrawCallCount);
			FEngineLog::Get().Log("Cluster Build Time: %.2f ms", Stats.Common.ClusterBuildTimeMs);
			FEngineLog::Get().Log("Constant Buffer Update Time: %.2f ms", Stats.Common.ConstantBufferUpdateTimeMs);
			FEngineLog::Get().Log("Structured Buffer Upload Time: %.2f ms", Stats.Common.StructuredBufferUploadTimeMs);
			FEngineLog::Get().Log("Shading Pass Time: %.2f ms", Stats.Common.ShadingPassTimeMs);
			FEngineLog::Get().Log("Total Fog Time: %.2f ms", Stats.Common.TotalFogTimeMs);
			FEngineLog::Get().Log("");
			FEngineLog::Get().Log("Global Fog Buffer: %.2f KB", Stats.Common.GlobalFogBufferBytes / 1024.0);
			FEngineLog::Get().Log("Local Fog Buffer: %.2f KB", Stats.Common.LocalFogBufferBytes / 1024.0);
			FEngineLog::Get().Log("Cluster Header Buffer: %.2f KB", Stats.Common.ClusterHeaderBufferBytes / 1024.0);
			FEngineLog::Get().Log("Cluster Index Buffer: %.2f KB", Stats.Common.ClusterIndexBufferBytes / 1024.0);
			FEngineLog::Get().Log("SceneColor Copy: %.2f MB", Stats.Common.SceneColorCopyBytes / (1024.0 * 1024.0));
			FEngineLog::Get().Log("Total Upload: %.2f KB", Stats.Common.TotalUploadBytes / 1024.0);
			return;
		}

		if (std::strcmp(CommandLine, "stat decal") == 0)
		{
			FRenderer* Renderer = GetRenderer();
			if (!Renderer)
			{
				FEngineLog::Get().Log("[error] Renderer is not available.");
				return;
			}

			const FDecalStats Stats = Renderer->GetDecalStats();
			FEngineLog::Get().Log("[Decal Stat]");
			FEngineLog::Get().Log("");
			FEngineLog::Get().Log("Build Time: %.2f ms", Stats.Common.BuildTimeMs);
			FEngineLog::Get().Log("Cull / Intersection Time: %.2f ms", Stats.Common.CullIntersectionTimeMs);
			FEngineLog::Get().Log("Shading Pass Time: %.2f ms", Stats.Common.ShadingPassTimeMs);
			FEngineLog::Get().Log("Total Decal Time: %.2f ms", Stats.Common.TotalDecalTimeMs);
			FEngineLog::Get().Log("");

			if (Stats.Common.Mode == EDecalProjectionMode::VolumeDraw)
			{
				FEngineLog::Get().Log("[Volume Draw]");
				FEngineLog::Get().Log("Candidate Objects: %u", Stats.Volume.CandidateObjects);
				FEngineLog::Get().Log("Intersect Passed: %u", Stats.Volume.IntersectPassed);
				FEngineLog::Get().Log("Decal Draw Calls: %u", Stats.Volume.DecalDrawCalls);
			}
			else
			{
				FEngineLog::Get().Log("[Clustered Lookup]");
				FEngineLog::Get().Log("Clusters Built: %u", Stats.ClusteredLookup.ClustersBuilt);
				FEngineLog::Get().Log("Decal-Cell Registrations: %u", Stats.ClusteredLookup.DecalCellRegistrations);
				FEngineLog::Get().Log("Avg Decals Per Cell: %.1f", Stats.ClusteredLookup.AvgDecalsPerCell);
				FEngineLog::Get().Log("Max Decals Per Cell: %u", Stats.ClusteredLookup.MaxDecalsPerCell);
			}
			return;
		}

		FString Result;
		if (FConsoleVariableManager::Get().Execute(CommandLine, Result))
		{
			FEngineLog::Get().Log("%s", Result.c_str());
		}
		else
		{
			FEngineLog::Get().Log("[error] Unknown command: '%s'", CommandLine);
		}
	});
}

bool FEditorEngine::InitEditorCamera()
{
	return CameraSubsystem.Initialize(GetActiveWorld(), GetInputManager(), GetEnhancedInputManager());
}

void FEditorEngine::InitEditorViewportRouting()
{
	SyncViewportClient();

	FViewportEntry* PerspEntry = nullptr;
	if (SlateApplication)
	{
		const FViewportId FocusedId = SlateApplication->GetFocusedViewportId();
		if (FocusedId != INVALID_VIEWPORT_ID)
		{
			FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
			if (FocusedEntry && FocusedEntry->bActive && FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
			{
				PerspEntry = FocusedEntry;
			}
		}
	}

	if (!PerspEntry)
	{
		PerspEntry = ViewportRegistry.FindEntryByType(EViewportType::Perspective);
	}

	if (PerspEntry)
	{
		CameraSubsystem.GetViewportController()->SetActiveLocalState(&PerspEntry->LocalState);
	}
}

bool FEditorEngine::InitEditorWorlds()
{
	const float AspectRatio = GetWindowAspectRatio();
	EditorWorldContext = CreateWorldContext("EditorScene", EWorldType::Editor, AspectRatio, true);
	if (!EditorWorldContext)
	{
		return false;
	}

	for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
	{
		Entry.WorldContext = EditorWorldContext;
	}

	ActivateEditorScene();
	return true;
}

void FEditorEngine::ReleaseEditorWorlds()
{
	ActiveWorldContext = nullptr;

	for (FWorldContext* PreviewContext : PreviewWorldContexts)
	{
		DestroyWorldContext(PreviewContext);
	}
	PreviewWorldContexts.clear();

	DestroyWorldContext(EditorWorldContext);
	EditorWorldContext = nullptr;
}

FWorldContext* FEditorEngine::FindPreviewWorld(const FString& ContextName)
{
	for (FWorldContext* Context : PreviewWorldContexts)
	{
		if (Context && Context->ContextName == ContextName)
		{
			return Context;
		}
	}

	return nullptr;
}

const FWorldContext* FEditorEngine::FindPreviewWorld(const FString& ContextName) const
{
	for (const FWorldContext* Context : PreviewWorldContexts)
	{
		if (Context && Context->ContextName == ContextName)
		{
			return Context;
		}
	}

	return nullptr;
}

void FEditorEngine::UpdateEditorWorldAspectRatio(float AspectRatio)
{
	UpdateWorldAspectRatio(EditorWorldContext ? EditorWorldContext->World : nullptr, AspectRatio);

	for (FWorldContext* PreviewContext : PreviewWorldContexts)
	{
		UpdateWorldAspectRatio(PreviewContext ? PreviewContext->World : nullptr, AspectRatio);
	}
}

void FEditorEngine::RefreshPIEPlayerCameraActors()
{
	PIEPlayerCameraActors.clear();
	ActivePIEPlayerCameraIndex = -1;

	if (PIEWorldContext == nullptr || PIEWorldContext->World == nullptr)
	{
		return;
	}

	for (AActor* Actor : PIEWorldContext->World->GetActors())
	{
		if (Actor && Actor->IsA(APlayerCameraActor::StaticClass()))
		{
			Actor->SetVisible(false);
			PIEPlayerCameraActors.push_back(static_cast<APlayerCameraActor*>(Actor));
		}
	}

	if (!PIEPlayerCameraActors.empty())
	{
		ActivePIEPlayerCameraIndex = 0;
	}
}

bool FEditorEngine::ApplyPIEPlayerCameraByIndex(int32 CameraIndex)
{
	if (PIEWorldContext == nullptr || PIEWorldContext->World == nullptr)
	{
		return false;
	}

	const int32 CameraCount = static_cast<int32>(PIEPlayerCameraActors.size());
	if (CameraIndex < 0 || CameraIndex >= CameraCount)
	{
		return false;
	}

	APlayerCameraActor* PlayerCameraActor = PIEPlayerCameraActors[CameraIndex].Get();
	if (PlayerCameraActor == nullptr || PlayerCameraActor->IsPendingDestroy())
	{
		return false;
	}

	UCameraComponent* CameraComponent = PlayerCameraActor->GetCameraComponent();
	if (CameraComponent == nullptr)
	{
		return false;
	}

	PlayerCameraActor->SyncCameraComponentState();
	if (FCamera* Camera = CameraComponent->GetCamera())
	{
		Camera->SetAspectRatio(GetWindowAspectRatio());
	}
	PIEWorldContext->World->SetActiveCameraComponent(CameraComponent);

	if (FViewportEntry* PIEViewportEntry = ViewportRegistry.FindEntryByViewportID(PIEViewportId))
	{
		if (FCamera* Camera = CameraComponent->GetCamera())
		{
			PIEViewportEntry->LocalState.ProjectionType = EViewportType::Perspective;
			PIEViewportEntry->LocalState.Position = Camera->GetPosition();
			PIEViewportEntry->LocalState.Rotation = FRotator(Camera->GetPitch(), Camera->GetYaw(), 0.0f);
			PIEViewportEntry->LocalState.FovY = Camera->GetFOV();
			PIEViewportEntry->LocalState.bShowGrid = false;
		}
	}

	ActivePIEPlayerCameraIndex = CameraIndex;
	return true;
}

void FEditorEngine::ApplyPIEStartView()
{
	if (PIEWorldContext == nullptr || PIEWorldContext->World == nullptr)
	{
		return;
	}

	ULevel* PIELevel = PIEWorldContext->World->GetScene();
	if (PIELevel == nullptr)
	{
		return;
	}

	PIELevel->EnsureEssentialActors();
	AActor* PlayerStartActor = PIELevel->FindPlayerStartActor();
	if (PlayerStartActor == nullptr)
	{
		PlayerStartActor = PIELevel->EnsurePlayerStartActor();
	}
	AActor* DefaultPawnActor = ResolvePIEDefaultPawnActor(PIEWorldContext->World);
	if (DefaultPawnActor && PlayerStartActor && DefaultPawnActor != PlayerStartActor)
	{
		DefaultPawnActor->SetActorTransform(PlayerStartActor->GetActorTransform());
	}

	AActor* StartActor = DefaultPawnActor ? DefaultPawnActor : PlayerStartActor;

	FVector StartLocation = FVector::ZeroVector;
	FRotator StartRotation = FRotator::ZeroRotator;
	if (StartActor)
	{
		const FTransform StartTransform = StartActor->GetActorTransform();
		StartLocation = StartTransform.GetLocation();
		StartRotation = StartTransform.Rotator();
	}

	UCameraComponent* PawnCameraComponent = EnsurePIECameraComponent(StartActor);
	USpringArmComponent* PawnSpringArmComponent = StartActor ? StartActor->GetComponentByClass<USpringArmComponent>() : nullptr;
	if (PawnCameraComponent)
	{
		FCamera* PawnCamera = PawnCameraComponent->GetCamera();
		if (PawnCamera)
		{
			FVector CameraLocation = StartLocation;
			FRotator CameraRotation = StartRotation;
			if (PawnSpringArmComponent)
			{
				CameraLocation = PawnSpringArmComponent->GetSocketWorldLocation();
				CameraRotation = PawnSpringArmComponent->GetSocketWorldRotation();
			}
			else
			{
				CameraLocation = PawnCameraComponent->GetWorldLocation();
			}

			PawnCamera->SetPosition(CameraLocation);
			PawnCamera->SetRotation(CameraRotation.Yaw, CameraRotation.Pitch);
			PawnCamera->SetAspectRatio(GetWindowAspectRatio());
		}

		PIEWorldContext->World->SetActiveCameraComponent(PawnCameraComponent);
	}
	else if (UCameraComponent* SceneCameraComponent = PIEWorldContext->World->GetActiveCameraComponent())
	{
		if (FCamera* SceneCamera = SceneCameraComponent->GetCamera())
		{
			// 기본 Pawn에 카메라가 없으면, Pawn 뒤쪽에서 바라보는 간단한 3인칭 뷰를 사용한다.
			FVector CameraLocation = StartLocation;
			FRotator CameraRotation = StartRotation;
			if (DefaultPawnActor && StartActor == DefaultPawnActor)
			{
				const FVector PawnForward = StartRotation.Vector().GetSafeNormal();
				CameraLocation = StartLocation - PawnForward * 5.0f + FVector(0.0f, 0.0f, 2.0f);
			}

			SceneCamera->SetPosition(CameraLocation);
			SceneCamera->SetRotation(CameraRotation.Yaw, CameraRotation.Pitch);
			SceneCamera->SetAspectRatio(GetWindowAspectRatio());
		}

		PIEWorldContext->World->SetActiveCameraComponent(SceneCameraComponent);
	}

	if (FViewportEntry* PIEViewportEntry = ViewportRegistry.FindEntryByViewportID(PIEViewportId))
	{
		PIEViewportEntry->LocalState.ProjectionType = EViewportType::Perspective;
		if (PawnCameraComponent && PawnCameraComponent->GetCamera())
		{
			FCamera* PawnCamera = PawnCameraComponent->GetCamera();
			PIEViewportEntry->LocalState.Position = PawnCamera->GetPosition();
			PIEViewportEntry->LocalState.Rotation = FRotator(PawnCamera->GetPitch(), PawnCamera->GetYaw(), 0.0f);
		}
		else if (DefaultPawnActor && StartActor == DefaultPawnActor)
		{
			const FVector PawnForward = StartRotation.Vector().GetSafeNormal();
			PIEViewportEntry->LocalState.Position = StartLocation - PawnForward * 5.0f + FVector(0.0f, 0.0f, 2.0f);
		}
		else
		{
			PIEViewportEntry->LocalState.Position = StartLocation;
		}
		if (!PawnCameraComponent || !PawnCameraComponent->GetCamera())
		{
			PIEViewportEntry->LocalState.Rotation = StartRotation;
		}
		PIEViewportEntry->LocalState.bShowGrid = false;
	}
}

AActor* FEditorEngine::ResolvePIEDefaultPawnActor(UWorld* PIEWorld) const
{
	if (!PIEWorld)
	{
		return nullptr;
	}

	ULevel* PIELevel = PIEWorld->GetScene();
	if (!PIELevel)
	{
		return nullptr;
	}

	const FLevelGameplaySettings& GameplaySettings = PIELevel->GetGameplaySettings();
	const FString& DefaultPawnMeshAsset = GameplaySettings.DefaultPawnMeshAsset;
	if (DefaultPawnMeshAsset.empty())
	{
		return nullptr;
	}

	AStaticMeshActor* SpawnedPawnActor = nullptr;
	for (AActor* ExistingActor : PIELevel->GetActors())
	{
		if (ExistingActor == nullptr || ExistingActor->IsPendingDestroy())
		{
			continue;
		}

		if (ExistingActor->GetName() == "PIE_DefaultPawn")
		{
			if (ExistingActor->IsA(AStaticMeshActor::StaticClass()))
			{
				SpawnedPawnActor = static_cast<AStaticMeshActor*>(ExistingActor);
				break;
			}
			return ExistingActor;
		}
	}

	if (!SpawnedPawnActor)
	{
		SpawnedPawnActor = PIELevel->SpawnActor<AStaticMeshActor>("PIE_DefaultPawn");
	}
	if (!SpawnedPawnActor)
	{
		return nullptr;
	}

	UStaticMeshComponent* MeshComponent = SpawnedPawnActor->GetComponentByClass<UStaticMeshComponent>();
	if (!MeshComponent)
	{
		return SpawnedPawnActor;
	}

	USpringArmComponent* SpringArmComponent = SpawnedPawnActor->GetComponentByClass<USpringArmComponent>();
	if (!SpringArmComponent)
	{
		SpringArmComponent = FObjectFactory::ConstructObject<USpringArmComponent>(
			SpawnedPawnActor, "SpringArmComponent");
	}
	if (SpringArmComponent)
	{
		if (SpringArmComponent->GetOwner() != SpawnedPawnActor)
		{
			SpawnedPawnActor->AddOwnedComponent(SpringArmComponent);
			if (USceneComponent* RootSceneComponent = SpawnedPawnActor->GetRootComponent())
			{
				SpringArmComponent->AttachTo(RootSceneComponent);
			}
		}
		// FPS 기본값: 팔 길이를 0으로 두고, 눈높이만 소폭 올린다.
		SpringArmComponent->SetTargetArmLength(GameplaySettings.DefaultPawnSpringArmLength);
		SpringArmComponent->SetSocketOffset(GameplaySettings.DefaultPawnSpringArmSocketOffset);
		if (!SpringArmComponent->IsRegistered())
		{
			SpringArmComponent->OnRegister();
		}
	}

	UCameraComponent* CameraComponent = SpawnedPawnActor->GetComponentByClass<UCameraComponent>();
	if (!CameraComponent)
	{
		CameraComponent = FObjectFactory::ConstructObject<UCameraComponent>(
			SpawnedPawnActor, "PawnCameraComponent");
	}
	if (CameraComponent)
	{
		if (CameraComponent->GetOwner() != SpawnedPawnActor)
		{
			SpawnedPawnActor->AddOwnedComponent(CameraComponent);
			if (SpringArmComponent)
			{
				CameraComponent->AttachTo(SpringArmComponent);
			}
			else if (USceneComponent* RootSceneComponent = SpawnedPawnActor->GetRootComponent())
			{
				CameraComponent->AttachTo(RootSceneComponent);
			}
		}

		if (!CameraComponent->IsRegistered())
		{
			CameraComponent->OnRegister();
		}
	}

	std::filesystem::path CandidateMeshPath = FPaths::ToPath(DefaultPawnMeshAsset);
	if (!CandidateMeshPath.is_absolute())
	{
		CandidateMeshPath = FPaths::MeshDir() / CandidateMeshPath;
	}

	UStaticMesh* PawnMesh = nullptr;
	for (TObjectIterator<UStaticMesh> It; It; ++It)
	{
		UStaticMesh* MeshAsset = It.Get();
		if (!MeshAsset)
		{
			continue;
		}

		if (MeshAsset->GetAssetPathFileName() == DefaultPawnMeshAsset)
		{
			PawnMesh = MeshAsset;
			break;
		}
	}

	if (!PawnMesh)
	{
		PawnMesh = FObjManager::LoadStaticMeshAsset(FPaths::FromPath(CandidateMeshPath));
	}

	if (PawnMesh)
	{
		MeshComponent->SetStaticMesh(PawnMesh);
		MeshComponent->UpdateBounds();
	}

	SpawnedPawnActor->SetVisible(true);
	return SpawnedPawnActor;
}

void FEditorEngine::SyncFocusedViewportLocalState()
{
	if (!EditorViewportClientRaw || !SlateApplication)
	{
		return;
	}

	FViewportId FocusedId = SlateApplication->GetFocusedViewportId();
	FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
	FViewportLocalState* LocalState = nullptr;
	if (FocusedEntry && FocusedEntry->bActive)
	{
		if (FocusedEntry->WorldContext && FocusedEntry->WorldContext->World)
		{
			ActiveWorldContext = FocusedEntry->WorldContext;
		}

		const bool bIsPIEViewport =
			FocusedEntry->WorldContext &&
			FocusedEntry->WorldContext->WorldType == EWorldType::PIE;

		if (!bIsPIEViewport && FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
		{
			LocalState = &FocusedEntry->LocalState;
		}
	}

	CameraSubsystem.GetViewportController()->SetActiveLocalState(LocalState);
}

void FEditorEngine::SyncPlatformCursor()
{
	if (!SlateApplication || !SlateApplication->GetIsCoursorInArea())
	{
		return;
	}

	const EMouseCursor SlateCursor = SlateApplication->GetCurrentCursor();
	LPCWSTR WinCursorName = IDC_ARROW;
	switch (SlateCursor)
	{
	case EMouseCursor::Default:         WinCursorName = IDC_ARROW;  break;
	case EMouseCursor::ResizeLeftRight: WinCursorName = IDC_SIZEWE; break;
	case EMouseCursor::ResizeUpDown:    WinCursorName = IDC_SIZENS; break;
	case EMouseCursor::Hand:            WinCursorName = IDC_HAND;   break;
	case EMouseCursor::None:            WinCursorName = nullptr;    break;
	}

	if (WinCursorName)
	{
		::SetCursor(::LoadCursor(NULL, WinCursorName));
	}
	else
	{
		::SetCursor(nullptr);
	}
}

void FEditorEngine::SyncPIECursorState()
{
	if (!bIsPIEActive || PIEViewportId == INVALID_VIEWPORT_ID || MainWindow == nullptr)
	{
		if (bWasCursorHiddenForPIE && bIsPIECursorCurrentlyHidden)
		{
			::ShowCursor(TRUE);
			bIsPIECursorCurrentlyHidden = false;
		}
		::ClipCursor(nullptr);
		return;
	}

	if (!bIsPIEInputCaptured)
	{
		if (bWasCursorHiddenForPIE && bIsPIECursorCurrentlyHidden)
		{
			::ShowCursor(TRUE);
			bIsPIECursorCurrentlyHidden = false;
		}
		::ClipCursor(nullptr);
		return;
	}

	HWND Hwnd = MainWindow->GetHwnd();
	const bool bWindowActive = (Hwnd != nullptr) &&
		(::GetForegroundWindow() == Hwnd);

	if (!bWindowActive)
	{
		if (bWasCursorHiddenForPIE && bIsPIECursorCurrentlyHidden)
		{
			::ShowCursor(TRUE);
			bIsPIECursorCurrentlyHidden = false;
		}
		::ClipCursor(nullptr);
		return;
	}

	if (bWasCursorHiddenForPIE && !bIsPIECursorCurrentlyHidden)
	{
		::ShowCursor(FALSE);
		bIsPIECursorCurrentlyHidden = true;
	}

	FViewport* PIEViewport = FindViewport(PIEViewportId);
	if (PIEViewport == nullptr)
	{
		::ClipCursor(nullptr);
		return;
	}

	const FRect& Rect = PIEViewport->GetRect();
	if (!Rect.IsValid())
	{
		::ClipCursor(nullptr);
		return;
	}

	if (Hwnd == nullptr)
	{
		::ClipCursor(nullptr);
		return;
	}

	POINT TopLeft = { Rect.X, Rect.Y };
	POINT BottomRight = { Rect.X + Rect.Width, Rect.Y + Rect.Height };
	::ClientToScreen(Hwnd, &TopLeft);
	::ClientToScreen(Hwnd, &BottomRight);

	RECT ClipRect = { TopLeft.x, TopLeft.y, BottomRight.x, BottomRight.y };
	::ClipCursor(&ClipRect);
}

void FEditorEngine::CenterCursorInPIEViewport()
{
	if (!bIsPIEActive || PIEViewportId == INVALID_VIEWPORT_ID || MainWindow == nullptr)
	{
		return;
	}

	FViewport* PIEViewport = FindViewport(PIEViewportId);
	if (PIEViewport == nullptr)
	{
		return;
	}

	HWND Hwnd = MainWindow->GetHwnd();
	if (Hwnd == nullptr)
	{
		return;
	}

	const FRect& Rect = PIEViewport->GetRect();
	if (!Rect.IsValid())
	{
		return;
	}

	POINT Center = { Rect.X + Rect.Width / 2, Rect.Y + Rect.Height / 2 };
	::ClientToScreen(Hwnd, &Center);
	::SetCursorPos(Center.x, Center.y);
}

void FEditorEngine::SyncViewportClient()
{
	if (!GetActiveWorldContext())
	{
		return;
	}

	IViewportClient* TargetViewportClient = ViewportClient.get();
	const FWorldContext* ActiveSceneContext = GetActiveWorldContext();
	if (ActiveSceneContext && ActiveSceneContext->WorldType == EWorldType::Preview && PreviewViewportClient)
	{
		TargetViewportClient = PreviewViewportClient.get();
	}

	if (GetViewportClient() != TargetViewportClient)
	{
		SetViewportClient(TargetViewportClient);
	}
}

FViewport* FEditorEngine::FindViewport(FViewportId Id)
{
	for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
	{
		if (Entry.Id == Id && Entry.bActive)
		{
			return Entry.Viewport;
		}
	}

	return nullptr;
}
