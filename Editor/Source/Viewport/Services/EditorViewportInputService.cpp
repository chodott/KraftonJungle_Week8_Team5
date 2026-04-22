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
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Component/SpringArmComponent.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include <algorithm>
#include <cmath>
#include <limits>

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

	FString BuildUniqueDuplicateName(ULevel* Scene, const FString& SourceName)
	{
		if (!Scene)
		{
			return SourceName + "_Copy";
		}

		TSet<FString> ExistingNames;
		for (AActor* Actor : Scene->GetActors())
		{
			if (!Actor || Actor->IsPendingDestroy())
			{
				continue;
			}
			ExistingNames.insert(Actor->GetName());
		}

		const FString BaseName = SourceName + "_Copy";
		if (ExistingNames.find(BaseName) == ExistingNames.end())
		{
			return BaseName;
		}

		for (int32 Index = 1; Index < 10000; ++Index)
		{
			const FString Candidate = BaseName + "_" + std::to_string(Index);
			if (ExistingNames.find(Candidate) == ExistingNames.end())
			{
				return Candidate;
			}
		}

		return BaseName + "_" + std::to_string(FObjectFactory::GetLastUUID());
	}

	void DuplicateSelectedActors(FEditorEngine* EditorEngine, ULevel* Scene, const TArray<AActor*>& SelectedActors)
	{
		if (!EditorEngine || !Scene || SelectedActors.empty())
		{
			return;
		}

		// 선택 틴트가 적용된 상태로 복제되지 않도록,
		// 복제 전에 선택을 한 번 비워 원본 색상을 먼저 복원한다.
		EditorEngine->ClearSelectedActors();

		FDuplicateContext DuplicateContext;
		DuplicateContext.Register(Scene, Scene);
		TArray<std::pair<AActor*, AActor*>> DuplicatePairs;
		DuplicatePairs.reserve(SelectedActors.size());

		for (AActor* SourceActor : SelectedActors)
		{
			if (!SourceActor || SourceActor->IsPendingDestroy())
			{
				continue;
			}

			const FString NewName = BuildUniqueDuplicateName(Scene, SourceActor->GetName());
			AActor* DuplicatedActor = static_cast<AActor*>(SourceActor->Duplicate(Scene, NewName, DuplicateContext));
			if (!DuplicatedActor)
			{
				continue;
			}

			Scene->RegisterActor(DuplicatedActor);
			if (USceneComponent* RootComponent = DuplicatedActor->GetRootComponent())
			{
				RootComponent->SetRelativeLocation(RootComponent->GetRelativeLocation() + FVector(0.1f, 0.1f, 0.0f));
			}

			for (UActorComponent* Component : DuplicatedActor->GetComponents())
			{
				if (!Component || !Component->IsA(UPrimitiveComponent::StaticClass()))
				{
					continue;
				}

				UPrimitiveComponent* PrimitiveComponent = static_cast<UPrimitiveComponent*>(Component);
				PrimitiveComponent->UpdateBounds();
			}
			DuplicatePairs.push_back({ SourceActor, DuplicatedActor });
		}

		for (const auto& Pair : DuplicatePairs)
		{
			Pair.first->FixupDuplicatedReferences(Pair.second, DuplicateContext);
		}

		for (const auto& Pair : DuplicatePairs)
		{
			Pair.first->PostDuplicate(Pair.second, DuplicateContext);
		}

		EditorEngine->ClearSelectedActors();
		for (const auto& Pair : DuplicatePairs)
		{
			EditorEngine->AddSelectedActor(Pair.second);
		}
		Scene->MarkSpatialDirty();
	}

	bool ComputeSelectionBounds(const TArray<AActor*>& SelectedActors, FVector& OutCenter, float& OutRadius)
	{
		if (SelectedActors.empty())
		{
			return false;
		}

		bool bHasAnyPoint = false;
		FVector BoundsMin = FVector::ZeroVector;
		FVector BoundsMax = FVector::ZeroVector;
		auto Expand = [&](const FVector& Point)
		{
			if (!bHasAnyPoint)
			{
				BoundsMin = Point;
				BoundsMax = Point;
				bHasAnyPoint = true;
				return;
			}

			BoundsMin.X = std::min(BoundsMin.X, Point.X);
			BoundsMin.Y = std::min(BoundsMin.Y, Point.Y);
			BoundsMin.Z = std::min(BoundsMin.Z, Point.Z);
			BoundsMax.X = std::max(BoundsMax.X, Point.X);
			BoundsMax.Y = std::max(BoundsMax.Y, Point.Y);
			BoundsMax.Z = std::max(BoundsMax.Z, Point.Z);
		};

		for (AActor* Actor : SelectedActors)
		{
			if (!Actor || Actor->IsPendingDestroy())
			{
				continue;
			}

			bool bUsedPrimitiveBounds = false;
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (!Component || !Component->IsA(UPrimitiveComponent::StaticClass()))
				{
					continue;
				}

				UPrimitiveComponent* PrimitiveComponent = static_cast<UPrimitiveComponent*>(Component);
				if (PrimitiveComponent->IsPendingKill())
				{
					continue;
				}

				const FBoxSphereBounds Bounds = PrimitiveComponent->GetWorldBounds();
				Expand(Bounds.Center - Bounds.BoxExtent);
				Expand(Bounds.Center + Bounds.BoxExtent);
				bUsedPrimitiveBounds = true;
			}

			if (!bUsedPrimitiveBounds)
			{
				if (USceneComponent* RootComponent = Actor->GetRootComponent())
				{
					Expand(RootComponent->GetWorldLocation());
				}
			}
		}

		if (!bHasAnyPoint)
		{
			return false;
		}

		OutCenter = (BoundsMin + BoundsMax) * 0.5f;
		OutRadius = std::max((BoundsMax - OutCenter).Size(), 10.0f);
		return true;
	}

	void FocusSelectedActors(FEditorEngine* EditorEngine, FViewportEntry* Entry)
	{
		if (!EditorEngine || !Entry || !Entry->Viewport)
		{
			return;
		}

		const TArray<AActor*> SelectedActors = EditorEngine->GetSelectedActors();
		if (SelectedActors.empty())
		{
			return;
		}

		FVector FocusCenter = FVector::ZeroVector;
		float FocusRadius = 0.0f;
		if (!ComputeSelectionBounds(SelectedActors, FocusCenter, FocusRadius))
		{
			return;
		}

		if (Entry->LocalState.ProjectionType == EViewportType::Perspective)
		{
			const float SafeFovDegrees = std::clamp(Entry->LocalState.FovY, 15.0f, 150.0f);
			const float SafeFovRadians = SafeFovDegrees * (3.14159265358979323846f / 180.0f);
			float AspectRatio = 16.0f / 9.0f;
			if (Entry->Viewport)
			{
				const FRect& Rect = Entry->Viewport->GetRect();
				if (Rect.Height > 0)
				{
					AspectRatio = static_cast<float>(Rect.Width) / static_cast<float>(Rect.Height);
				}
			}

			const float VerticalHalfFov = SafeFovRadians * 0.5f;
			const float HorizontalHalfFov = std::atan(std::tan(VerticalHalfFov) * AspectRatio);
			const float LimitingHalfFov = std::min(VerticalHalfFov, HorizontalHalfFov);
			const float TargetFillRatio = 0.7f;
			const float Distance =
				(FocusRadius / std::max(std::tan(LimitingHalfFov) * TargetFillRatio, 1.0e-3f))
				+ FocusRadius * 0.1f;
			const FVector ViewForward = Entry->LocalState.Rotation.Vector().GetSafeNormal();
			Entry->LocalState.Position = FocusCenter - ViewForward * Distance;
		}
		else
		{
			Entry->LocalState.OrthoTarget = FocusCenter;
			Entry->LocalState.OrthoZoom = std::max(FocusRadius * 1.5f, 1.0f);
		}
	}

	bool ProjectWorldToScreen(const FViewportEntry* Entry, const FVector& WorldPosition, FVector2& OutScreenPosition, float& OutDepth)
	{
		if (!Entry || !Entry->Viewport)
		{
			return false;
		}

		const FRect& Rect = Entry->Viewport->GetRect();
		if (!Rect.IsValid())
		{
			return false;
		}

		const float AspectRatio = static_cast<float>(Rect.Width) / static_cast<float>(Rect.Height);
		const FMatrix ViewMatrix = Entry->LocalState.BuildViewMatrix();
		const FMatrix ProjectionMatrix = Entry->LocalState.BuildProjMatrix(AspectRatio);

		const FVector ViewPosition(
			WorldPosition.X * ViewMatrix.M[0][0] + WorldPosition.Y * ViewMatrix.M[1][0] + WorldPosition.Z * ViewMatrix.M[2][0] + ViewMatrix.M[3][0],
			WorldPosition.X * ViewMatrix.M[0][1] + WorldPosition.Y * ViewMatrix.M[1][1] + WorldPosition.Z * ViewMatrix.M[2][1] + ViewMatrix.M[3][1],
			WorldPosition.X * ViewMatrix.M[0][2] + WorldPosition.Y * ViewMatrix.M[1][2] + WorldPosition.Z * ViewMatrix.M[2][2] + ViewMatrix.M[3][2]);

		const float ClipX = ViewPosition.X * ProjectionMatrix.M[0][0] + ViewPosition.Y * ProjectionMatrix.M[1][0] + ViewPosition.Z * ProjectionMatrix.M[2][0] + ProjectionMatrix.M[3][0];
		const float ClipY = ViewPosition.X * ProjectionMatrix.M[0][1] + ViewPosition.Y * ProjectionMatrix.M[1][1] + ViewPosition.Z * ProjectionMatrix.M[2][1] + ProjectionMatrix.M[3][1];
		const float ClipZ = ViewPosition.X * ProjectionMatrix.M[0][2] + ViewPosition.Y * ProjectionMatrix.M[1][2] + ViewPosition.Z * ProjectionMatrix.M[2][2] + ProjectionMatrix.M[3][2];
		const float ClipW = ViewPosition.X * ProjectionMatrix.M[0][3] + ViewPosition.Y * ProjectionMatrix.M[1][3] + ViewPosition.Z * ProjectionMatrix.M[2][3] + ProjectionMatrix.M[3][3];
		if (std::abs(ClipW) < 1.0e-5f)
		{
			return false;
		}

		const float NdcX = ClipX / ClipW;
		const float NdcY = ClipY / ClipW;
		const float NdcZ = ClipZ / ClipW;
		OutScreenPosition.X = (NdcX * 0.5f + 0.5f) * static_cast<float>(Rect.Width);
		OutScreenPosition.Y = (1.0f - (NdcY * 0.5f + 0.5f)) * static_cast<float>(Rect.Height);
		OutDepth = NdcZ;
		return std::isfinite(OutScreenPosition.X) && std::isfinite(OutScreenPosition.Y) && std::isfinite(OutDepth);
	}

	void SelectActorsInMarquee(FEditorEngine* EditorEngine, ULevel* Scene, const FViewportEntry* Entry, const FRect& MarqueeRect)
	{
		if (!EditorEngine || !Scene || !Entry || !MarqueeRect.IsValid())
		{
			return;
		}

		TArray<std::pair<AActor*, float>> Hits;
		for (AActor* Actor : Scene->GetActors())
		{
			if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible())
			{
				continue;
			}

			FVector ProbePoint = FVector::ZeroVector;
			bool bHasProbe = false;
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (!Component || !Component->IsA(UPrimitiveComponent::StaticClass()))
				{
					continue;
				}

				UPrimitiveComponent* PrimitiveComponent = static_cast<UPrimitiveComponent*>(Component);
				if (PrimitiveComponent->IsPendingKill() || !PrimitiveComponent->IsPickable())
				{
					continue;
				}

				ProbePoint = PrimitiveComponent->GetWorldBounds().Center;
				bHasProbe = true;
				break;
			}

			if (!bHasProbe)
			{
				if (USceneComponent* Root = Actor->GetRootComponent())
				{
					ProbePoint = Root->GetWorldLocation();
					bHasProbe = true;
				}
			}

			if (!bHasProbe)
			{
				continue;
			}

			FVector2 Projected(0.0f, 0.0f);
			float Depth = 0.0f;
			if (!ProjectWorldToScreen(Entry, ProbePoint, Projected, Depth))
			{
				continue;
			}

			if (Projected.X < 0.0f || Projected.X > static_cast<float>(Entry->Viewport->GetRect().Width) ||
				Projected.Y < 0.0f || Projected.Y > static_cast<float>(Entry->Viewport->GetRect().Height))
			{
				continue;
			}

			const int32 ScreenX = static_cast<int32>(Projected.X);
			const int32 ScreenY = static_cast<int32>(Projected.Y);
			if (ScreenX >= MarqueeRect.X && ScreenX <= MarqueeRect.X + MarqueeRect.Width &&
				ScreenY >= MarqueeRect.Y && ScreenY <= MarqueeRect.Y + MarqueeRect.Height)
			{
				Hits.push_back({ Actor, Depth });
			}
		}

		std::sort(Hits.begin(), Hits.end(), [](const auto& Left, const auto& Right)
		{
			return Left.second < Right.second;
		});

		EditorEngine->ClearSelectedActors();
		for (const auto& Hit : Hits)
		{
			EditorEngine->AddSelectedActor(Hit.first);
		}
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
			const bool bRightMouseDown = Engine->GetInputManager() &&
				Engine->GetInputManager()->IsMouseButtonDown(FInputManager::MOUSE_RIGHT);
			if (FocusedEntry &&
				FocusedEntry->bActive &&
				FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective &&
				bRightMouseDown)
			{
				const float WheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(WParam)) / WHEEL_DELTA;
				const float SpeedScale = std::pow(1.1f, WheelDelta);

				FCamera* TargetCamera = nullptr;
				if (IsPIEViewportEntry(FocusedEntry))
				{
					if (UWorld* PIEWorld = GetViewportWorld(FocusedEntry))
					{
						if (UCameraComponent* ActiveCameraComponent = PIEWorld->GetActiveCameraComponent())
						{
							TargetCamera = ActiveCameraComponent->GetCamera();
						}
					}
				}
				else if (IsEditorViewportEntry(FocusedEntry))
				{
					if (ULevel* Scene = GetViewportScene(FocusedEntry))
					{
						TargetCamera = Scene->GetCamera();
					}
				}

				if (TargetCamera)
				{
					const float NewSpeed = std::clamp(TargetCamera->GetSpeed() * SpeedScale, 0.1f, 20.0f);
					TargetCamera->SetSpeed(NewSpeed);
				}
			}
			else if (FocusedEntry &&
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
		const bool bCtrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
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

		if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard)
		{
			return;
		}

		if (bCtrlDown && WParam == 'D')
		{
			if (ULevel* Scene = GetViewportScene(Entry))
			{
				DuplicateSelectedActors(EditorEngine, Scene, EditorEngine->GetSelectedActors());
				if (OnSelectionChanged)
				{
					OnSelectionChanged();
				}
			}
			return;
		}

		if (!bCtrlDown && WParam == 'F')
		{
			FocusSelectedActors(EditorEngine, Entry);
			return;
		}

		if (!bCtrlDown && WParam == VK_DELETE)
		{
			if (ULevel* Scene = GetViewportScene(Entry))
			{
				const TArray<AActor*> ActorsToDelete = EditorEngine->GetSelectedActors();
				for (AActor* Actor : ActorsToDelete)
				{
					if (!Actor || Actor->IsPendingDestroy())
					{
						continue;
					}
					Scene->DestroyActor(Actor);
				}
				EditorEngine->ClearSelectedActors();
				if (OnSelectionChanged)
				{
					OnSelectionChanged();
				}
			}
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

		const bool bCtrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
		const bool bShiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
		const bool bAltDown = (::GetKeyState(VK_MENU) & 0x8000) != 0;
		if (IsEditorViewportEntry(Entry) && bCtrlDown && bAltDown)
		{
			bIsMarqueeSelecting = true;
			MarqueeViewportId = Entry->Id;
			MarqueeStartWindowX = MouseX;
			MarqueeStartWindowY = MouseY;
			MarqueeCurrentWindowX = MouseX;
			MarqueeCurrentWindowY = MouseY;
			Gizmo.ClearHover();
			return;
		}

		const TArray<AActor*> SelectedActors = EditorEngine->GetSelectedActors();
		if (SelectedActor && Gizmo.BeginDrag(SelectedActor, SelectedActors, Entry, Picker, ScreenMouseX, ScreenMouseY))
		{
			return;
		}

		AActor* PickedActor = Picker.PickActor(Scene, Entry, ScreenMouseX, ScreenMouseY, EditorEngine);
		if (bShiftDown)
		{
			if (PickedActor)
			{
				EditorEngine->AddSelectedActor(PickedActor);
			}
		}
		else if (bCtrlDown)
		{
			if (PickedActor)
			{
				EditorEngine->ToggleSelectedActor(PickedActor);
			}
		}
		else
		{
			EditorEngine->SetSelectedActor(PickedActor);
		}
		if (OnSelectionChanged)
		{
			OnSelectionChanged();
		}
		return;
	}

	case WM_MOUSEMOVE:
	{
		if (bIsMarqueeSelecting)
		{
			MarqueeCurrentWindowX = MouseX;
			MarqueeCurrentWindowY = MouseY;
			return;
		}

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

		if (Gizmo.UpdateDrag(SelectedActor, EditorEngine->GetSelectedActors(), HoveredEntry, Picker, ScreenMouseX, ScreenMouseY) && OnSelectionChanged)
		{
			OnSelectionChanged();
		}
		return;
	}

	case WM_LBUTTONUP:
	{
		if (bIsMarqueeSelecting)
		{
			bIsMarqueeSelecting = false;
			const FViewportId CapturedViewportId = MarqueeViewportId;
			MarqueeViewportId = INVALID_VIEWPORT_ID;

			FViewportEntry* MarqueeEntry = ViewportRegistry.FindEntryByViewportID(CapturedViewportId);
			if (!MarqueeEntry || !MarqueeEntry->Viewport || !IsEditorViewportEntry(MarqueeEntry))
			{
				return;
			}

			MarqueeCurrentWindowX = MouseX;
			MarqueeCurrentWindowY = MouseY;
			const FRect& ViewportRect = MarqueeEntry->Viewport->GetRect();
			FRect SelectionRect;
			SelectionRect.X = std::min(MarqueeStartWindowX, MarqueeCurrentWindowX) - ViewportRect.X;
			SelectionRect.Y = std::min(MarqueeStartWindowY, MarqueeCurrentWindowY) - ViewportRect.Y;
			SelectionRect.Width = std::abs(MarqueeCurrentWindowX - MarqueeStartWindowX);
			SelectionRect.Height = std::abs(MarqueeCurrentWindowY - MarqueeStartWindowY);
			SelectionRect = IntersectRect(SelectionRect, FRect(0, 0, ViewportRect.Width, ViewportRect.Height));

			if (SelectionRect.Width > 2 && SelectionRect.Height > 2)
			{
				if (ULevel* Scene = GetViewportScene(MarqueeEntry))
				{
					SelectActorsInMarquee(EditorEngine, Scene, MarqueeEntry, SelectionRect);
					if (OnSelectionChanged)
					{
						OnSelectionChanged();
					}
				}
			}
			return;
		}

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

bool FEditorViewportInputService::GetMarqueeSelectionRect(FRect& OutRect) const
{
	if (!bIsMarqueeSelecting)
	{
		return false;
	}

	OutRect.X = std::min(MarqueeStartWindowX, MarqueeCurrentWindowX);
	OutRect.Y = std::min(MarqueeStartWindowY, MarqueeCurrentWindowY);
	OutRect.Width = std::abs(MarqueeCurrentWindowX - MarqueeStartWindowX);
	OutRect.Height = std::abs(MarqueeCurrentWindowY - MarqueeStartWindowY);
	return OutRect.IsValid();
}
