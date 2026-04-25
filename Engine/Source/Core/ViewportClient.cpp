#include "Core/ViewportClient.h"
#include "World/World.h"
#include "Input/InputManager.h"
#include "Camera/Camera.h"
#include "Renderer/Renderer.h"
#include "Renderer/Common/RenderMode.h"
#include "Level/Level.h"
#include "Level/SceneRenderPacket.h"
#include "Debug/EngineLog.h"
#include "Core/Engine.h"
#include "Actor/Actor.h"
#include "Component/CameraComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/LocalHeightFogComponent.h"
#include "Math/Frustum.h"
#include "Component/PrimitiveComponent.h"
#include "Component/FireBallComponent.h"
#include "Component/SpotLightComponent.h"
#include "Core/ShowFlags.h"
#include <unordered_set>
#include <Math/MathUtility.h>

namespace
{
	void AppendHeightFogPrimitives(ULevel* Level, const FShowFlags& Flags, FSceneRenderPacket& OutPacket)
	{
		if (!Level || !Flags.HasFlag(EEngineShowFlags::SF_Fog))
		{
			return;
		}

		const TArray<AActor*>& Actors = Level->GetActors();
		OutPacket.FogPrimitives.reserve(OutPacket.FogPrimitives.size() + Actors.size());

		for (AActor* Actor : Actors)
		{
			if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible())
			{
				continue;
			}

			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (!Component || Component->IsPendingKill())
				{
					continue;
				}

				if (!Component->IsA(UHeightFogComponent::StaticClass()) && !Component->IsA(ULocalHeightFogComponent::StaticClass()))
				{
					continue;
				}

				OutPacket.FogPrimitives.push_back({ static_cast<UPrimitiveComponent*>(Component) });
			}
		}
	}
	
	void AppendFireBallPrimitives(ULevel* Level, FSceneRenderPacket& OutPacket)
	{
		if (!Level)
		{
			return;
		}
		
		const TArray<AActor*> Actors = Level->GetActors();
		OutPacket.FireBallPrimitives.reserve(OutPacket.FireBallPrimitives.size() + Actors.size());
		
		for (AActor* Actor : Actors)
		{
			if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible())
			{
				continue;
			}
			
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (!Component || Component->IsPendingKill() || !Component->IsA(UFireBallComponent::StaticClass()))
				{
					continue;
				}
				
				OutPacket.FireBallPrimitives.push_back({ static_cast<UFireBallComponent*>(Component) });
			}
		}
	}
}

void IViewportClient::Attach(FEngine* Engine, FRenderer* Renderer)
{
}

void IViewportClient::Detach(FEngine* Engine, FRenderer* Renderer)
{
}

void IViewportClient::Tick(FEngine* Engine, float DeltaTime)
{
}

void IViewportClient::HandleMessage(FEngine* Engine, HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
{
}

ULevel* IViewportClient::ResolveScene(FEngine* Engine) const
{
	return Engine ? Engine->GetActiveScene() : nullptr;
}

UWorld* IViewportClient::ResolveWorld(FEngine* Engine) const
{
	return Engine ? Engine->GetActiveWorld() : nullptr;
}

void IViewportClient::BuildSceneRenderPacket(
	FEngine* Engine,
	UWorld* World,
	const FFrustum& Frustum,
	const FShowFlags& Flags,
	FSceneRenderPacket& OutPacket)
{
	if (!World)
	{
		return;
	}

	ULevel* Level = World->GetPersistentLevel();
	if (!Level)
	{
		OutPacket.Clear();
		return;
	}

	TArray<UPrimitiveComponent*> VisiblePrimitives;
	Level->QueryPrimitivesByFrustum(Frustum, VisiblePrimitives);

	for (AActor* Actor : Level->GetActors())
	{
		if (!Actor || !Actor->IsVisible() || Actor->IsPendingDestroy())
			continue;

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USpotLightComponent* Spot = dynamic_cast<USpotLightComponent*>(Component))
			{
				if (Spot->GetVisible() && Spot->IsCastingShadows() && Spot->GetEffectiveIntensity() > 0.0f)
				{
					float OuterAngleRad = FMath::DegreesToRadians(Spot->GetOuterConeAngle());
					float FOV = OuterAngleRad * 2.0f;
					float NearZ = 0.1f;
					float FarZ = Spot->GetAttenuationRadius();
					if (FarZ > NearZ)
					{
						FMatrix Proj = FMatrix::MakePerspectiveFovLH(FOV, 1.0f, NearZ, FarZ);
						FVector Pos = Spot->GetWorldLocation();
						FVector Dir = Spot->GetEmissionDirectionWS().GetSafeNormal();
						FVector Up = FVector(0.0f, 1.0f, 0.0f);
						if (std::abs(Dir.Y) > 0.999f) Up = FVector(0.0f, 0.0f, 1.0f);
						FMatrix View = FMatrix::MakeViewLookAtLH(Pos, Pos + Dir, Up);
						
						FFrustum SpotFrustum;
						SpotFrustum.ExtractFromVP(View * Proj);
						Level->QueryPrimitivesByFrustum(SpotFrustum, VisiblePrimitives);
					}
				}
			}
		}
	}

	std::unordered_set<UPrimitiveComponent*> UniquePrimitives;
	TArray<UPrimitiveComponent*> DeduplicatedPrimitives;
	DeduplicatedPrimitives.reserve(VisiblePrimitives.size());
	for (UPrimitiveComponent* Prim : VisiblePrimitives)
	{
		if (UniquePrimitives.insert(Prim).second)
		{
			DeduplicatedPrimitives.push_back(Prim);
		}
	}

	ScenePacketBuilder.BuildScenePacket(DeduplicatedPrimitives, Flags, OutPacket);
	AppendHeightFogPrimitives(Level, Flags, OutPacket);
	AppendFireBallPrimitives(Level, OutPacket);
	OutPacket.bApplyFXAA = Flags.HasFlag(EEngineShowFlags::SF_FXAA);
}

void IViewportClient::HandleFileDoubleClick(const FString& FilePath)
{

}

void IViewportClient::HandleFileDropOnViewport(const FString& FilePath)
{

}

void IViewportClient::Render(FEngine* Engine, FRenderer* Renderer)
{
}


void FGameViewportClient::Attach(FEngine* Engine, FRenderer* Renderer)
{
	(void)Engine;
	(void)Renderer;
}

void FGameViewportClient::Detach(FEngine* Engine, FRenderer* Renderer)
{
	(void)Engine;
	(void)Renderer;
}

void FGameViewportClient::Render(FEngine* Engine, FRenderer* Renderer)
{
	if (!Engine || !Renderer)
	{
		return;
	}

	UWorld* ActiveWorld = ResolveWorld(Engine);
	if (!ActiveWorld)
	{
		return;
	}

	UCameraComponent* ActiveCamera = ActiveWorld->GetActiveCameraComponent();
	if (!ActiveCamera)
	{
		return;
	}

	// 게임 뷰포트는 활성 카메라 기준으로 프러스텀을 만들고 씬 패킷을 채운다.
	FSceneRenderPacket ScenePacket;
	FGameFrameRequest FrameRequest;
	FrameRequest.SceneView.ViewMatrix = ActiveCamera->GetViewMatrix();
	FrameRequest.SceneView.ProjectionMatrix = ActiveCamera->GetProjectionMatrix();
	FrameRequest.RenderMode = ERenderMode::Lit_Gouraud;

	FFrustum Frustum;
	Frustum.ExtractFromVP(FrameRequest.SceneView.ViewMatrix * FrameRequest.SceneView.ProjectionMatrix);

	FrameRequest.SceneView.CameraPosition = FrameRequest.SceneView.ViewMatrix.GetInverse().GetTranslation();
	FrameRequest.SceneView.NearZ = ActiveCamera->GetNearPlane();
	FrameRequest.SceneView.FarZ = ActiveCamera->GetFarPlane();
	FrameRequest.SceneView.TotalTimeSeconds = Engine ? static_cast<float>(Engine->GetTimer().GetTotalTime()) : 0.0f;
	const FShowFlags ShowFlags = {};
	BuildSceneRenderPacket(Engine, ActiveWorld, Frustum, ShowFlags, ScenePacket);
	FrameRequest.ScenePacket = std::move(ScenePacket);
	FrameRequest.DebugInputs.DrawManager = &Engine->GetDebugDrawManager();
	FrameRequest.DebugInputs.World = ActiveWorld;
	FrameRequest.DebugInputs.ShowFlags = ShowFlags;

	// 실제 씬 실행과 프레임 순서는 FRenderer 내부 서브시스템이 담당한다.
	Renderer->RenderGameFrame(FrameRequest);
}
