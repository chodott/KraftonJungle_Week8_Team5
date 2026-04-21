#include "Viewport/Services/EditorViewportInputService.h"

#include "EditorEngine.h"
#include "Viewport/EditorViewportRegistry.h"
#include "Actor/Actor.h"
#include "Camera/Camera.h"
#include "Core/Engine.h"
#include "Debug/EngineLog.h"
#include "Gizmo/Gizmo.h"
#include "imgui.h"
#include "Input/InputManager.h"
#include "Picking/Picker.h"
#include "Level/Level.h"
#include "Platform/Windows/WindowsWindow.h"
#include "Slate/SlateApplication.h"
#include "Viewport/Viewport.h"
#include "World/World.h"
#include "World/WorldContext.h"
#include "Component/CameraComponent.h"
#include "Component/SpringArmComponent.h"
#include <algorithm>

namespace
{
	bool IsEditorViewportEntry(const FViewportEntry* Entry)
	{
		return Entry &&
			Entry->WorldContext &&
			Entry->WorldContext->World &&
			Entry->WorldContext->WorldType == EWorldType::Editor;
	}

	bool IsPIEViewportEntry(const FViewportEntry* Entry)
	{
		return Entry &&
			Entry->WorldContext &&
			Entry->WorldContext->World &&
			Entry->WorldContext->WorldType == EWorldType::PIE;
	}

	UWorld* GetViewportWorld(const FViewportEntry* Entry)
	{
		if (!Entry || !Entry->WorldContext)
		{
			return nullptr;
		}

		return Entry->WorldContext->World;
	}

	ULevel* GetViewportScene(const FViewportEntry* Entry)
	{
		UWorld* World = GetViewportWorld(Entry);
		if (!World)
		{
			return nullptr;
		}

		return World->GetScene();
	}
}

void FEditorViewportInputService::TickCameraNavigation(
	FEngine* Engine,
	FEditorEngine* EditorEngine,
	FEditorViewportRegistry& ViewportRegistry,
	const FGizmo& Gizmo)
{
	if (!Engine || !EditorEngine)
	{
		return;
	}

	FSlateApplication* Slate = EditorEngine->GetSlateApplication();
	if (!Slate || Slate->GetFocusedViewportId() == INVALID_VIEWPORT_ID)
	{
		return;
	}

	if (ImGui::GetCurrentContext())
	{
		const ImGuiIO& IO = ImGui::GetIO();
		if (IO.WantCaptureKeyboard || IO.WantCaptureMouse)
		{
			return;
		}
	}

	FInputManager* Input = Engine->GetInputManager();
	if (!Input)
	{
		return;
	}

	FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(Slate->GetFocusedViewportId());
	if (!FocusedEntry || !FocusedEntry->bActive)
	{
		return;
	}

	if (IsPIEViewportEntry(FocusedEntry))
	{
		if (EditorEngine->IsPIEPaused() || FocusedEntry->Viewport == nullptr)
		{
			return;
		}

		if (!EditorEngine->IsPIEInputCaptured())
		{
			if (!Input->IsMouseButtonDown(FInputManager::MOUSE_RIGHT) || Gizmo.IsDragging())
			{
				return;
			}

			float Sensitivity = 0.2f;
			float Speed = 5.0f;
			if (ULevel* Scene = GetViewportScene(FocusedEntry))
			{
				if (FCamera* Cam = Scene->GetCamera())
				{
					Sensitivity = Cam->GetMouseSensitivity();
					Speed = Cam->GetSpeed();
				}
			}

			const float DeltaX = Input->GetMouseDeltaX();
			const float DeltaY = Input->GetMouseDeltaY();
			FocusedEntry->LocalState.Rotation.Yaw += DeltaX * Sensitivity;
			FocusedEntry->LocalState.Rotation.Pitch -= DeltaY * Sensitivity;
			FocusedEntry->LocalState.Rotation.Pitch = std::clamp(FocusedEntry->LocalState.Rotation.Pitch, -89.0f, 89.0f);

			const FVector Forward = FocusedEntry->LocalState.Rotation.Vector().GetSafeNormal();
			const FVector Right = FVector::CrossProduct(FVector(0.0f, 0.0f, 1.0f), Forward).GetSafeNormal();
			FVector MoveDelta = FVector::ZeroVector;
			if (Input->IsKeyDown('W')) MoveDelta += Forward;
			if (Input->IsKeyDown('S')) MoveDelta -= Forward;
			if (Input->IsKeyDown('D')) MoveDelta += Right;
			if (Input->IsKeyDown('A')) MoveDelta -= Right;
			if (Input->IsKeyDown('E')) MoveDelta += FVector(0.0f, 0.0f, 1.0f);
			if (Input->IsKeyDown('Q')) MoveDelta -= FVector(0.0f, 0.0f, 1.0f);
			if (!MoveDelta.IsNearlyZero())
			{
				FocusedEntry->LocalState.Position += MoveDelta.GetSafeNormal() * (Speed * EditorEngine->GetDeltaTime());
			}

			return;
		}

		float Sensitivity = 0.2f;
		float Speed = 5.0f;
		FCamera* ActiveCamera = nullptr;
		UCameraComponent* ActiveCameraComponent = nullptr;
		AActor* ActiveCameraOwner = nullptr;
		USpringArmComponent* OwnerSpringArm = nullptr;
		if (UWorld* PIEWorld = GetViewportWorld(FocusedEntry))
		{
			ActiveCameraComponent = PIEWorld->GetActiveCameraComponent();
			if (ActiveCameraComponent)
			{
				ActiveCamera = ActiveCameraComponent->GetCamera();
				ActiveCameraOwner = ActiveCameraComponent->GetOwner();
				if (ActiveCameraOwner)
				{
					OwnerSpringArm = ActiveCameraOwner->GetComponentByClass<USpringArmComponent>();
				}
			}
		}

		if (ActiveCamera)
		{
			Sensitivity = ActiveCamera->GetMouseSensitivity();
			Speed = ActiveCamera->GetSpeed();
		}
		else if (ULevel* Scene = GetViewportScene(FocusedEntry))
		{
			if (FCamera* Cam = Scene->GetCamera())
			{
				Sensitivity = Cam->GetMouseSensitivity();
				Speed = Cam->GetSpeed();
			}
		}

		const FRect& Rect = FocusedEntry->Viewport->GetRect();
		if (!Rect.IsValid())
		{
			return;
		}

		HWND Hwnd = nullptr;
		if (FWindowsWindow* MainWindow = EditorEngine->GetMainWindow())
		{
			Hwnd = MainWindow->GetHwnd();
		}

		if (Hwnd == nullptr)
		{
			return;
		}

		if (::GetForegroundWindow() != Hwnd)
		{
			return;
		}

		POINT Center = { Rect.X + Rect.Width / 2, Rect.Y + Rect.Height / 2 };
		::ClientToScreen(Hwnd, &Center);

		POINT CursorPos;
		if (!::GetCursorPos(&CursorPos))
		{
			return;
		}

		const float DeltaX = static_cast<float>(CursorPos.x - Center.x);
		const float DeltaY = static_cast<float>(CursorPos.y - Center.y);
		const bool bUsePawnDrivenMotion =
			ActiveCamera &&
			ActiveCameraOwner &&
			ActiveCameraOwner->GetName() == "PIE_DefaultPawn";
		float CameraYaw = ActiveCamera ? ActiveCamera->GetYaw() : FocusedEntry->LocalState.Rotation.Yaw;
		float CameraPitch = ActiveCamera ? ActiveCamera->GetPitch() : FocusedEntry->LocalState.Rotation.Pitch;

		if (bUsePawnDrivenMotion)
		{
			FTransform PawnTransform = ActiveCameraOwner->GetActorTransform();
			FRotator PawnRotation = PawnTransform.Rotator();
			CameraYaw = PawnRotation.Yaw + DeltaX * Sensitivity;
			CameraPitch = std::clamp(ActiveCamera->GetPitch() - DeltaY * Sensitivity, -89.0f, 89.0f);

			PawnRotation.Yaw = CameraYaw;
			PawnRotation.Pitch = 0.0f;
			PawnRotation.Roll = 0.0f;
			PawnTransform.SetRotation(PawnRotation);
			ActiveCameraOwner->SetActorTransform(PawnTransform);
			ActiveCamera->SetRotation(CameraYaw, CameraPitch);
		}
		else if (ActiveCamera)
		{
			ActiveCamera->Rotate(DeltaX * Sensitivity, -DeltaY * Sensitivity);
			CameraYaw = ActiveCamera->GetYaw();
			CameraPitch = ActiveCamera->GetPitch();
		}
		else
		{
			CameraYaw += DeltaX * Sensitivity;
			CameraPitch = std::clamp(CameraPitch - DeltaY * Sensitivity, -89.0f, 89.0f);
			FocusedEntry->LocalState.Rotation.Yaw = CameraYaw;
			FocusedEntry->LocalState.Rotation.Pitch = CameraPitch;
		}

		const float DeltaTime = EditorEngine->GetDeltaTime();
		if (bUsePawnDrivenMotion)
		{
			const FRotator PawnRotation = ActiveCameraOwner->GetActorTransform().Rotator();
			const FVector MoveForward = FRotator(0.0f, PawnRotation.Yaw, 0.0f).Vector().GetSafeNormal();
			const FVector MoveRight = FVector::CrossProduct(FVector(0.0f, 0.0f, 1.0f), MoveForward).GetSafeNormal();
			FVector MoveDelta = FVector::ZeroVector;
			if (Input->IsKeyDown('W')) MoveDelta += MoveForward;
			if (Input->IsKeyDown('S')) MoveDelta -= MoveForward;
			if (Input->IsKeyDown('D')) MoveDelta += MoveRight;
			if (Input->IsKeyDown('A')) MoveDelta -= MoveRight;
			if (Input->IsKeyDown('E')) MoveDelta += FVector(0.0f, 0.0f, 1.0f);
			if (Input->IsKeyDown('Q')) MoveDelta -= FVector(0.0f, 0.0f, 1.0f);
			if (!MoveDelta.IsNearlyZero())
			{
				const FVector NewLocation = ActiveCameraOwner->GetActorLocation() + MoveDelta.GetSafeNormal() * (Speed * DeltaTime);
				ActiveCameraOwner->SetActorLocation(NewLocation);
			}

			if (OwnerSpringArm)
			{
				const FVector PawnPivot = ActiveCameraOwner->GetActorLocation();
				const float ArmLength = OwnerSpringArm->GetTargetArmLength();
				const FVector Forward = FRotator(CameraPitch, CameraYaw, 0.0f).Vector().GetSafeNormal();
				ActiveCamera->SetPosition(PawnPivot - Forward * ArmLength);
			}
			else if (ActiveCameraComponent)
			{
				ActiveCamera->SetPosition(ActiveCameraComponent->GetWorldLocation());
			}
			ActiveCamera->SetRotation(CameraYaw, CameraPitch);

			FocusedEntry->LocalState.Position = ActiveCamera->GetPosition();
			FocusedEntry->LocalState.Rotation = FRotator(ActiveCamera->GetPitch(), ActiveCamera->GetYaw(), 0.0f);
		}
		else if (ActiveCamera)
		{
			ActiveCamera->SetSpeed(Speed);
			if (Input->IsKeyDown('W')) ActiveCamera->MoveForward(DeltaTime);
			if (Input->IsKeyDown('S')) ActiveCamera->MoveForward(-DeltaTime);
			if (Input->IsKeyDown('D')) ActiveCamera->MoveRight(DeltaTime);
			if (Input->IsKeyDown('A')) ActiveCamera->MoveRight(-DeltaTime);
			if (Input->IsKeyDown('E')) ActiveCamera->MoveUp(DeltaTime);
			if (Input->IsKeyDown('Q')) ActiveCamera->MoveUp(-DeltaTime);

			FocusedEntry->LocalState.Position = ActiveCamera->GetPosition();
			FocusedEntry->LocalState.Rotation = FRotator(ActiveCamera->GetPitch(), ActiveCamera->GetYaw(), 0.0f);
		}
		else
		{
			const FVector Forward = FocusedEntry->LocalState.Rotation.Vector().GetSafeNormal();
			const FVector Right = FVector::CrossProduct(FVector(0.0f, 0.0f, 1.0f), Forward).GetSafeNormal();
			FVector MoveDelta = FVector::ZeroVector;
			if (Input->IsKeyDown('W')) MoveDelta += Forward;
			if (Input->IsKeyDown('S')) MoveDelta -= Forward;
			if (Input->IsKeyDown('D')) MoveDelta += Right;
			if (Input->IsKeyDown('A')) MoveDelta -= Right;
			if (Input->IsKeyDown('E')) MoveDelta += FVector(0.0f, 0.0f, 1.0f);
			if (Input->IsKeyDown('Q')) MoveDelta -= FVector(0.0f, 0.0f, 1.0f);
			if (!MoveDelta.IsNearlyZero())
			{
				FocusedEntry->LocalState.Position += MoveDelta.GetSafeNormal() * (Speed * DeltaTime);
			}
		}

		::SetCursorPos(Center.x, Center.y);
		return;
	}

	if (!Input->IsMouseButtonDown(FInputManager::MOUSE_RIGHT) || Gizmo.IsDragging())
	{
		return;
	}

	if (!IsEditorViewportEntry(FocusedEntry))
	{
		return;
	}

	const float DeltaX = Input->GetMouseDeltaX();
	const float DeltaY = Input->GetMouseDeltaY();

	if (FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
	{
		float Sensitivity = 0.2f;
		if (ULevel* Scene = GetViewportScene(FocusedEntry))
		{
			if (FCamera* Cam = Scene->GetCamera())
			{
				Sensitivity = Cam->GetMouseSensitivity();
			}
		}

		FocusedEntry->LocalState.Rotation.Yaw += DeltaX * Sensitivity;
		FocusedEntry->LocalState.Rotation.Pitch -= DeltaY * Sensitivity;
		if (FocusedEntry->LocalState.Rotation.Pitch > 89.0f)
		{
			FocusedEntry->LocalState.Rotation.Pitch = 89.0f;
		}
		if (FocusedEntry->LocalState.Rotation.Pitch < -89.0f)
		{
			FocusedEntry->LocalState.Rotation.Pitch = -89.0f;
		}
		return;
	}

	FVector ViewFwd;
	FVector ViewUp;
	switch (FocusedEntry->LocalState.ProjectionType)
	{
	case EViewportType::OrthoTop:
		ViewFwd = FVector(0, 0, -1);
		ViewUp = FVector(1, 0, 0);
		break;

	case EViewportType::OrthoBottom:
		ViewFwd = FVector(0, 0, 1);
		ViewUp = FVector(1, 0, 0);
		break;

	case EViewportType::OrthoLeft:
		ViewFwd = FVector(0, 1, 0);
		ViewUp = FVector(0, 0, 1);
		break;

	case EViewportType::OrthoRight:
		ViewFwd = FVector(0, -1, 0);
		ViewUp = FVector(0, 0, 1);
		break;

	case EViewportType::OrthoFront:
		ViewFwd = FVector(-1, 0, 0);
		ViewUp = FVector(0, 0, 1);
		break;

	case EViewportType::OrthoBack:
		ViewFwd = FVector(1, 0, 0);
		ViewUp = FVector(0, 0, 1);
		break;

	default:
		return;
	}

	const FVector ViewRight = FVector::CrossProduct(ViewUp, ViewFwd).GetSafeNormal();
	const int32 H = FocusedEntry->Viewport->GetRect().Height;
	if (H <= 0) return;
	float WorldPerPixel = (2.0f * FocusedEntry->LocalState.OrthoZoom) / static_cast<float>(H);
	FocusedEntry->LocalState.OrthoTarget -= ViewRight * DeltaX * WorldPerPixel;
	FocusedEntry->LocalState.OrthoTarget += ViewUp * DeltaY * WorldPerPixel;
}

void FEditorViewportInputService::HandleMessage(
	FEngine* Engine,
	FEditorEngine* EditorEngine,
	HWND Hwnd,
	UINT Msg,
	WPARAM WParam,
	LPARAM LParam,
	FEditorViewportRegistry& ViewportRegistry,
	FPicker& Picker,
	FGizmo& Gizmo,
	const std::function<void()>& OnSelectionChanged)
{
	(void)Hwnd;

	if (!Engine || !EditorEngine)
	{
		return;
	}

	FSlateApplication* Slate = EditorEngine->GetSlateApplication();
	if (!Slate)
	{
		return;
	}

	const int32 MouseX = static_cast<int32>(static_cast<short>(LOWORD(LParam)));
	const int32 MouseY = static_cast<int32>(static_cast<short>(HIWORD(LParam)));

	switch (Msg)
	{
	case WM_LBUTTONDOWN:
		Slate->ProcessMouseDown(MouseX, MouseY);
		break;
	case WM_LBUTTONDBLCLK:
		Slate->ProcessMouseDoubleClick(MouseX, MouseY);
		return;
	case WM_RBUTTONDOWN:
		Slate->ProcessMouseDown(MouseX, MouseY);
		break;
	case WM_MOUSEMOVE:
		Slate->ProcessMouseMove(MouseX, MouseY);
		break;
	case WM_LBUTTONUP:
		Slate->ProcessMouseUp(MouseX, MouseY);
		break;
	default:
		break;
	}

	if (Msg == WM_MOUSEWHEEL)
	{
		if (!ImGui::GetCurrentContext() || !ImGui::GetIO().WantCaptureMouse)
		{
			FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(Slate->GetFocusedViewportId());
			if (FocusedEntry &&
				FocusedEntry->bActive &&
				IsEditorViewportEntry(FocusedEntry) &&
				FocusedEntry->LocalState.ProjectionType != EViewportType::Perspective)
			{
				const float WheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(WParam)) / WHEEL_DELTA;
				FocusedEntry->LocalState.OrthoZoom *= (1.0f - WheelDelta * 0.1f);
				if (FocusedEntry->LocalState.OrthoZoom < 1.0f)
				{
					FocusedEntry->LocalState.OrthoZoom = 1.0f;
				}
				if (FocusedEntry->LocalState.OrthoZoom > 10000.0f)
				{
					FocusedEntry->LocalState.OrthoZoom = 10000.0f;
				}
			}
		}
		return;
	}

	if (Slate->IsDraggingSplitter())
	{
		return;
	}

	if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse)
	{
		return;
	}

	AActor* SelectedActor = EditorEngine->GetSelectedActor();

	const bool bRightMouseDown = Engine->GetInputManager() &&
		Engine->GetInputManager()->IsMouseButtonDown(FInputManager::MOUSE_RIGHT);
	FViewportEntry* Entry = ViewportRegistry.FindEntryByViewportID(Slate->GetFocusedViewportId());

	switch (Msg)
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	{
		const bool bAltDown = (::GetKeyState(VK_MENU) & 0x8000) != 0;
		if (bAltDown && WParam == 'P')
		{
			const bool bWantsKeyboard = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
			if (!bWantsKeyboard)
			{
				EditorEngine->PlaySimulation();
				return;
			}
		}

		if (Entry && IsPIEViewportEntry(Entry))
		{
			if (WParam == VK_F8)
			{
				EditorEngine->TogglePIEPossession();
				return;
			}

			const bool bShiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
			if (WParam == VK_F1 && bShiftDown && EditorEngine->IsPIEInputCaptured())
			{
				EditorEngine->ReleasePIEInputCapture();
				return;
			}

			if (WParam == VK_ESCAPE && EditorEngine->IsPIEInputCaptured())
			{
				EditorEngine->EndPIE();
				return;
			}

			if (EditorEngine->IsPIEInputCaptured())
			{
				if (WParam == VK_RIGHT)
				{
					EditorEngine->CyclePIEPlayerCamera(1);
					return;
				}

				if (WParam == VK_LEFT)
				{
					EditorEngine->CyclePIEPlayerCamera(-1);
					return;
				}
			}
			return;
		}

		if (Slate->GetFocusedViewportId() == INVALID_VIEWPORT_ID || bRightMouseDown || !Entry || !IsEditorViewportEntry(Entry))
		{
			return;
		}

		switch (WParam)
		{
		case 'W':
			Gizmo.SetMode(EGizmoMode::Location);
			return;
		case 'E':
			Gizmo.SetMode(EGizmoMode::Rotation);
			return;
		case 'R':
			Gizmo.SetMode(EGizmoMode::Scale);
			return;
		case 'L':
			Gizmo.ToggleCoordinateSpace();
			UE_LOG("Gizmo Space: %s", Gizmo.GetCoordinateSpace() == EGizmoCoordinateSpace::Local ? "Local" : "World");
			return;
		case VK_SPACE:
			Gizmo.CycleMode();
			return;
		default:
			return;
		}
	}

	case WM_LBUTTONDOWN:
	{
		FViewport* ClickedViewport = ViewportRegistry.GetViewportById(Slate->GetFocusedViewportId());
		FViewport* Viewport = ClickedViewport;
		const bool bEditablePIEViewport = Entry && IsPIEViewportEntry(Entry) && !EditorEngine->IsPIEInputCaptured();
		if (!Viewport || !Entry || (!IsEditorViewportEntry(Entry) && !bEditablePIEViewport))
		{
			return;
		}

		ULevel* Scene = GetViewportScene(Entry);
		if (!Scene)
		{
			return;
		}

		const FRect& Rect = Viewport->GetRect();
		ScreenWidth = Rect.Width;
		ScreenHeight = Rect.Height;
		ScreenMouseX = MouseX - Rect.X;
		ScreenMouseY = MouseY - Rect.Y;

		if (SelectedActor && Gizmo.BeginDrag(SelectedActor, Entry, Picker, ScreenMouseX, ScreenMouseY))
		{
			return;
		}

		AActor* PickedActor = Picker.PickActor(Scene, Entry, ScreenMouseX, ScreenMouseY);
		EditorEngine->SetSelectedActor(PickedActor);
		if (OnSelectionChanged)
		{
			OnSelectionChanged();
		}
		return;
	}

	case WM_MOUSEMOVE:
	{
		FViewport* Viewport = ViewportRegistry.GetViewportById(Slate->GetHoveredViewportId());
		if (!Viewport)
		{
			Gizmo.ClearHover();
			return;
		}

		FViewportEntry* HoveredEntry = ViewportRegistry.FindEntryByViewportID(Slate->GetHoveredViewportId());
		const bool bEditablePIEViewport =
			HoveredEntry &&
			IsPIEViewportEntry(HoveredEntry) &&
			!EditorEngine->IsPIEInputCaptured();
		if (!IsEditorViewportEntry(HoveredEntry) && !bEditablePIEViewport)
		{
			if (Gizmo.IsDragging())
			{
				Gizmo.EndDrag();
				if (OnSelectionChanged)
				{
					OnSelectionChanged();
				}
			}

			Gizmo.ClearHover();
			return;
		}

		const FRect& Rect = Viewport->GetRect();
		ScreenWidth = Rect.Width;
		ScreenHeight = Rect.Height;
		ScreenMouseX = MouseX - Rect.X;
		ScreenMouseY = MouseY - Rect.Y;

		if (!Gizmo.IsDragging())
		{
			Gizmo.UpdateHover(SelectedActor, HoveredEntry, Picker, ScreenMouseX, ScreenMouseY);
			return;
		}

		if (Gizmo.UpdateDrag(SelectedActor, HoveredEntry, Picker, ScreenMouseX, ScreenMouseY) && OnSelectionChanged)
		{
			OnSelectionChanged();
		}
		return;
	}

	case WM_LBUTTONUP:
	{
		const bool bEditablePIEViewport = Entry && IsPIEViewportEntry(Entry) && !EditorEngine->IsPIEInputCaptured();
		if (!Entry || (!IsEditorViewportEntry(Entry) && !bEditablePIEViewport))
		{
			Gizmo.EndDrag();
			Gizmo.ClearHover();
			return;
		}

		if (!Gizmo.IsDragging())
		{
			return;
		}

		Gizmo.EndDrag();
		FViewport* Viewport = ViewportRegistry.GetViewportById(Slate->GetHoveredViewportId());
		if (Viewport)
		{
			FViewportEntry* HoveredEntry = ViewportRegistry.FindEntryByViewportID(Slate->GetHoveredViewportId());
			const FRect& Rect = Viewport->GetRect();
			ScreenWidth = Rect.Width;
			ScreenHeight = Rect.Height;
			ScreenMouseX = MouseX - Rect.X;
			ScreenMouseY = MouseY - Rect.Y;
			Gizmo.UpdateHover(SelectedActor, HoveredEntry, Picker, ScreenMouseX, ScreenMouseY);
		}
		else
		{
			Gizmo.ClearHover();
		}

		if (OnSelectionChanged)
		{
			OnSelectionChanged();
		}
		return;
	}

	default:
		return;
	}
}
