#include "Picker.h"

#include "Actor/Actor.h"
#include "Component/ActorComponent.h"
#include "Camera/Camera.h"
#include "Component/PrimitiveComponent.h"
#include "Level/PrimitiveVisibilityUtils.h"
#include "Level/ScenePacketBuilder.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/Mesh/MeshBatch.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/SceneViewData.h"
#include "Renderer/Scene/Builders/SceneCommandBuilder.h"
#include "Renderer/Scene/Builders/SceneViewAssembler.h"
#include "Renderer/Frame/RenderFrameUtils.h"
#include "Renderer/Frame/FrameRequests.h"
#include "Level/Level.h"
#include "Viewport/Viewport.h"
#include "Core/ConsoleVariableManager.h"
#include "Debug/EngineLog.h"
#include "Math/Frustum.h"
#include "Types/ObjectPtr.h"
#include "World/WorldContext.h"

#include <algorithm>
#include "EditorEngine.h"
#include <cmath>
#include <limits>

namespace
{
	struct FGPUIdPickerResources
	{
		uint32 Width = 0;
		uint32 Height = 0;
		ID3D11Texture2D* IdTexture = nullptr;
		ID3D11RenderTargetView* IdRTV = nullptr;
		ID3D11Texture2D* IdReadbackTexture = nullptr;
		ID3D11Texture2D* DepthTexture = nullptr;
		ID3D11DepthStencilView* DepthDSV = nullptr;
	};

	FGPUIdPickerResources GGPUIdPickerResources;

	void ReleaseCOM(IUnknown*& Resource)
	{
		if (!Resource)
		{
			return;
		}

		Resource->Release();
		Resource = nullptr;
	}

	void ReleaseGPUIdPickerResources()
	{
		ReleaseCOM(reinterpret_cast<IUnknown*&>(GGPUIdPickerResources.DepthDSV));
		ReleaseCOM(reinterpret_cast<IUnknown*&>(GGPUIdPickerResources.DepthTexture));
		ReleaseCOM(reinterpret_cast<IUnknown*&>(GGPUIdPickerResources.IdReadbackTexture));
		ReleaseCOM(reinterpret_cast<IUnknown*&>(GGPUIdPickerResources.IdRTV));
		ReleaseCOM(reinterpret_cast<IUnknown*&>(GGPUIdPickerResources.IdTexture));
		GGPUIdPickerResources.Width = 0;
		GGPUIdPickerResources.Height = 0;
	}

	bool EnsureGPUIdPickerResources(ID3D11Device* Device, uint32 Width, uint32 Height)
	{
		if (!Device || Width == 0 || Height == 0)
		{
			return false;
		}

		if (GGPUIdPickerResources.IdRTV
			&& GGPUIdPickerResources.IdReadbackTexture
			&& GGPUIdPickerResources.DepthDSV
			&& GGPUIdPickerResources.Width == Width
			&& GGPUIdPickerResources.Height == Height)
		{
			return true;
		}

		ReleaseGPUIdPickerResources();

		D3D11_TEXTURE2D_DESC IdDesc = {};
		IdDesc.Width = Width;
		IdDesc.Height = Height;
		IdDesc.MipLevels = 1;
		IdDesc.ArraySize = 1;
		IdDesc.Format = DXGI_FORMAT_R32_UINT;
		IdDesc.SampleDesc.Count = 1;
		IdDesc.Usage = D3D11_USAGE_DEFAULT;
		IdDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		if (FAILED(Device->CreateTexture2D(&IdDesc, nullptr, &GGPUIdPickerResources.IdTexture)))
		{
			ReleaseGPUIdPickerResources();
			return false;
		}

		D3D11_RENDER_TARGET_VIEW_DESC IdRTVDesc = {};
		IdRTVDesc.Format = DXGI_FORMAT_R32_UINT;
		IdRTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		if (FAILED(Device->CreateRenderTargetView(GGPUIdPickerResources.IdTexture, &IdRTVDesc, &GGPUIdPickerResources.IdRTV)))
		{
			ReleaseGPUIdPickerResources();
			return false;
		}

		D3D11_TEXTURE2D_DESC StagingDesc = IdDesc;
		StagingDesc.Usage = D3D11_USAGE_STAGING;
		StagingDesc.BindFlags = 0;
		StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		if (FAILED(Device->CreateTexture2D(&StagingDesc, nullptr, &GGPUIdPickerResources.IdReadbackTexture)))
		{
			ReleaseGPUIdPickerResources();
			return false;
		}

		D3D11_TEXTURE2D_DESC DepthDesc = {};
		DepthDesc.Width = Width;
		DepthDesc.Height = Height;
		DepthDesc.MipLevels = 1;
		DepthDesc.ArraySize = 1;
		DepthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		DepthDesc.SampleDesc.Count = 1;
		DepthDesc.Usage = D3D11_USAGE_DEFAULT;
		DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(Device->CreateTexture2D(&DepthDesc, nullptr, &GGPUIdPickerResources.DepthTexture)))
		{
			ReleaseGPUIdPickerResources();
			return false;
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC DepthDSVDesc = {};
		DepthDSVDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		DepthDSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(Device->CreateDepthStencilView(GGPUIdPickerResources.DepthTexture, &DepthDSVDesc, &GGPUIdPickerResources.DepthDSV)))
		{
			ReleaseGPUIdPickerResources();
			return false;
		}

		GGPUIdPickerResources.Width = Width;
		GGPUIdPickerResources.Height = Height;
		return true;
	}

	bool IsAccurateMeshPickingEnabled()
	{
		FConsoleVariableManager& CVM = FConsoleVariableManager::Get();
		FConsoleVariable* AccuratePickingVar = CVM.Find("r.Picking.AccurateMeshRay");
		if (!AccuratePickingVar)
		{
			AccuratePickingVar = CVM.Register(
				"r.Picking.AccurateMeshRay",
				0,
				"Accurate mesh ray picking in editor (0 = bounds only, 1 = triangle ray)");
		}
		return AccuratePickingVar->GetInt() != 0;
	}

	bool IsGPUIdPickingEnabled()
	{
		FConsoleVariableManager& CVM = FConsoleVariableManager::Get();
		FConsoleVariable* GPUPickingVar = CVM.Find("r.Picking.GPUId");
		if (!GPUPickingVar)
		{
			GPUPickingVar = CVM.Register(
				"r.Picking.GPUId",
				1,
				"GPU object-id based picking (0 = off, 1 = on)");
		}
		return GPUPickingVar->GetInt() != 0;
	}

	bool IsGPUIdPickingDebugEnabled()
	{
		FConsoleVariableManager& CVM = FConsoleVariableManager::Get();
		FConsoleVariable* DebugVar = CVM.Find("r.Picking.GPUId.Debug");
		if (!DebugVar)
		{
			DebugVar = CVM.Register(
				"r.Picking.GPUId.Debug",
				0,
				"GPU ID picking debug log (0 = off, 1 = on)");
		}
		return DebugVar->GetInt() != 0;
	}

	FVector TransformPointRowVector(const FVector& P, const FMatrix& M)
	{
		return {
			P.X * M.M[0][0] + P.Y * M.M[1][0] + P.Z * M.M[2][0] + M.M[3][0],
			P.X * M.M[0][1] + P.Y * M.M[1][1] + P.Z * M.M[2][1] + M.M[3][1],
			P.X * M.M[0][2] + P.Y * M.M[1][2] + P.Z * M.M[2][2] + M.M[3][2]
		};
	}

	FVector TransformVectorRowVector(const FVector& V, const FMatrix& M)
	{
		return {
			V.X * M.M[0][0] + V.Y * M.M[1][0] + V.Z * M.M[2][0],
			V.X * M.M[0][1] + V.Y * M.M[1][1] + V.Z * M.M[2][1],
			V.X * M.M[0][2] + V.Y * M.M[1][2] + V.Z * M.M[2][2]
		};
	}

	bool RayIntersectsSphere(const FRay& Ray, const FVector& Center, float Radius, float& OutT)
	{
		const FVector M = Ray.Origin - Center;
		const float B = FVector::DotProduct(M, Ray.Direction);
		const float C = FVector::DotProduct(M, M) - Radius * Radius;

		if (C > 0.0f && B > 0.0f)
		{
			return false;
		}

		const float Discriminant = B * B - C;
		if (Discriminant < 0.0f)
		{
			return false;
		}

		float T = -B - std::sqrt(Discriminant);
		if (T < 0.0f)
		{
			T = 0.0f;
		}

		OutT = T;
		return true;
	}

	bool RayIntersectsAABB(const FRay& Ray, const FVector& BoxMin, const FVector& BoxMax, float& OutTNear, float& OutTFar)
	{
		constexpr float Epsilon = 1.0e-8f;

		float TNear = 0.0f;
		float TFar = (std::numeric_limits<float>::max)();

		auto TestAxis = [&](float Origin, float Dir, float MinV, float MaxV) -> bool
			{
				if (std::abs(Dir) < Epsilon)
				{
					return (Origin >= MinV && Origin <= MaxV);
				}

				const float InvDir = 1.0f / Dir;
				float T1 = (MinV - Origin) * InvDir;
				float T2 = (MaxV - Origin) * InvDir;

				if (T1 > T2)
				{
					std::swap(T1, T2);
				}

				TNear = (std::max)(TNear, T1);
				TFar = (std::min)(TFar, T2);

				return TNear <= TFar;
			};

		if (!TestAxis(Ray.Origin.X, Ray.Direction.X, BoxMin.X, BoxMax.X)) return false;
		if (!TestAxis(Ray.Origin.Y, Ray.Direction.Y, BoxMin.Y, BoxMax.Y)) return false;
		if (!TestAxis(Ray.Origin.Z, Ray.Direction.Z, BoxMin.Z, BoxMax.Z)) return false;

		if (TFar < 0.0f)
		{
			return false;
		}

		OutTNear = TNear;
		OutTFar = TFar;
		return true;
	}

	bool TryPickActorByGPUId(ULevel* Scene, const FViewportEntry* Entry, int32 ScreenX, int32 ScreenY, FEditorEngine* Engine, AActor*& OutActor)
	{
		OutActor = nullptr;
		const bool bDebugLog = IsGPUIdPickingDebugEnabled();
		if (!Scene || !Entry || !Entry->Viewport || !Entry->WorldContext || !Entry->WorldContext->World || !Engine)
		{
			if (bDebugLog)
			{
				UE_LOG("[IDPick] GPU path aborted: invalid inputs (scene=%p entry=%p viewport=%p worldCtx=%p world=%p engine=%p)",
					Scene, Entry, Entry ? Entry->Viewport : nullptr, Entry ? Entry->WorldContext : nullptr,
					(Entry && Entry->WorldContext) ? Entry->WorldContext->World : nullptr, Engine);
			}
			return false;
		}

		FRenderer* Renderer = Engine->GetRenderer();
		if (!Renderer)
		{
			return false;
		}

		ID3D11Device* Device = Renderer->GetDevice();
		ID3D11DeviceContext* DeviceContext = Renderer->GetDeviceContext();
		if (!Device || !DeviceContext)
		{
			return false;
		}

		const FRect& Rect = Entry->Viewport->GetRect();
		if (Rect.Width <= 0 || Rect.Height <= 0
			|| ScreenX < 0 || ScreenY < 0
			|| ScreenX >= Rect.Width || ScreenY >= Rect.Height)
		{
			if (bDebugLog)
			{
				UE_LOG("[IDPick] GPU path aborted: out-of-range click local=(%d,%d) rect=(%d,%d)",
					ScreenX, ScreenY, Rect.Width, Rect.Height);
			}
			return false;
		}

		if (!EnsureGPUIdPickerResources(Device, static_cast<uint32>(Rect.Width), static_cast<uint32>(Rect.Height)))
		{
			return false;
		}

		D3D11_VIEWPORT Viewport = {};
		Viewport.TopLeftX = 0.0f;
		Viewport.TopLeftY = 0.0f;
		Viewport.Width = static_cast<float>(Rect.Width);
		Viewport.Height = static_cast<float>(Rect.Height);
		Viewport.MinDepth = 0.0f;
		Viewport.MaxDepth = 1.0f;

		const float AspectRatio = static_cast<float>(Rect.Width) / static_cast<float>(Rect.Height);
		const FMatrix ViewMatrix = Entry->LocalState.BuildViewMatrix();
		const FMatrix ProjectionMatrix = Entry->LocalState.BuildProjMatrix(AspectRatio);
		const FVector CameraPosition = ViewMatrix.GetInverse().GetTranslation();

		FSceneViewRenderRequest SceneViewRequest;
		SceneViewRequest.ViewMatrix = ViewMatrix;
		SceneViewRequest.ProjectionMatrix = ProjectionMatrix;
		SceneViewRequest.CameraPosition = CameraPosition;
		SceneViewRequest.NearZ = Entry->LocalState.NearPlane;
		SceneViewRequest.FarZ = Entry->LocalState.FarPlane;
		SceneViewRequest.TotalTimeSeconds = static_cast<float>(Engine->GetTimer().GetTotalTime());

		const FFrameContext Frame = BuildRenderFrameContext(SceneViewRequest.TotalTimeSeconds);
		const FViewContext View = BuildRenderViewContext(SceneViewRequest, Viewport);

		FFrustum Frustum;
		Frustum.ExtractFromVP(ViewMatrix * ProjectionMatrix);

		TArray<UPrimitiveComponent*> VisiblePrimitives;
		Scene->QueryPrimitivesByFrustum(Frustum, VisiblePrimitives);

		FScenePacketBuilder ScenePacketBuilder;
		FSceneRenderPacket ScenePacket;
		ScenePacketBuilder.BuildScenePacket(VisiblePrimitives, Entry->LocalState.ShowFlags, ScenePacket);

		FSceneCommandBuilder CommandBuilder;
		FSceneCommandResourceCache ResourceCache;
		FSceneViewData SceneViewData;
		TArray<FMeshBatch> AdditionalMeshBatches;
		BuildSceneViewDataFromPacket(
			*Renderer,
			CommandBuilder,
			ResourceCache,
			ScenePacket,
			Frame,
			View,
			Entry->WorldContext->World,
			AdditionalMeshBatches,
			SceneViewData);
		SceneViewData.RenderMode = ERenderMode::Unlit;
		SceneViewData.ShowFlags = Entry->LocalState.ShowFlags;
		if (bDebugLog)
		{
			const uint32 PickingMaskBit = static_cast<uint32>(EMeshPassMask::EditorPicking);
			int32 PickingBatchCount = 0;
			for (const FMeshBatch& Batch : SceneViewData.MeshInputs.Batches)
			{
				if ((Batch.PassMask & PickingMaskBit) != 0u)
				{
					++PickingBatchCount;
				}
			}

			UE_LOG("[IDPick] Packet mesh=%d billboard=%d text=%d subuv=%d | builtBatches=%d pickingBatches=%d",
				static_cast<int32>(ScenePacket.MeshPrimitives.size()),
				static_cast<int32>(ScenePacket.BillboardPrimitives.size()),
				static_cast<int32>(ScenePacket.TextPrimitives.size()),
				static_cast<int32>(ScenePacket.SubUVPrimitives.size()),
				static_cast<int32>(SceneViewData.MeshInputs.Batches.size()),
				PickingBatchCount);
		}

		ID3D11RenderTargetView* PrevRTV = nullptr;
		ID3D11DepthStencilView* PrevDSV = nullptr;
		DeviceContext->OMGetRenderTargets(1, &PrevRTV, &PrevDSV);

		D3D11_VIEWPORT PrevViewport = {};
		UINT PrevViewportCount = 1;
		DeviceContext->RSGetViewports(&PrevViewportCount, &PrevViewport);

		const float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		DeviceContext->OMSetRenderTargets(1, &GGPUIdPickerResources.IdRTV, GGPUIdPickerResources.DepthDSV);
		DeviceContext->RSSetViewports(1, &Viewport);
		DeviceContext->ClearRenderTargetView(GGPUIdPickerResources.IdRTV, ClearColor);
		DeviceContext->ClearDepthStencilView(GGPUIdPickerResources.DepthDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		Renderer->SetConstantBuffers();
		Renderer->UpdateFrameConstantBuffer(Frame, View);

		FMeshPassProcessor MeshPassProcessor;
		MeshPassProcessor.BeginFrame();
		MeshPassProcessor.UploadMeshBuffers(*Renderer, SceneViewData);

		FSceneRenderTargets DummyTargets = {};
		DummyTargets.Width = static_cast<uint32>(Rect.Width);
		DummyTargets.Height = static_cast<uint32>(Rect.Height);
		MeshPassProcessor.ExecutePass(*Renderer, DummyTargets, SceneViewData, EMeshPassType::EditorPicking);

		const UINT SrcX = static_cast<UINT>(ScreenX);
		const UINT SrcY = static_cast<UINT>(ScreenY);
		D3D11_BOX SrcBox = {};
		SrcBox.left = SrcX;
		SrcBox.top = SrcY;
		SrcBox.front = 0;
		SrcBox.right = SrcX + 1;
		SrcBox.bottom = SrcY + 1;
		SrcBox.back = 1;
		DeviceContext->CopySubresourceRegion(
			GGPUIdPickerResources.IdReadbackTexture,
			0,
			0,
			0,
			0,
			GGPUIdPickerResources.IdTexture,
			0,
			&SrcBox);

		uint32 PickedUUID = 0u;
		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (SUCCEEDED(DeviceContext->Map(GGPUIdPickerResources.IdReadbackTexture, 0, D3D11_MAP_READ, 0, &Mapped)))
		{
			PickedUUID = *reinterpret_cast<const uint32*>(Mapped.pData);
			DeviceContext->Unmap(GGPUIdPickerResources.IdReadbackTexture, 0);
		}
		else if (bDebugLog)
		{
			UE_LOG("[IDPick] GPU readback map failed");
		}

		if (bDebugLog)
		{
			UE_LOG("[IDPick] GPU readback local=(%d,%d) uuid=%u", ScreenX, ScreenY, PickedUUID);
		}

		DeviceContext->OMSetRenderTargets(1, &PrevRTV, PrevDSV);
		if (PrevViewportCount > 0)
		{
			DeviceContext->RSSetViewports(1, &PrevViewport);
		}

		if (PrevDSV)
		{
			PrevDSV->Release();
		}
		if (PrevRTV)
		{
			PrevRTV->Release();
		}

		if (PickedUUID == 0u)
		{
			// GPU pass executed successfully and reported "no object hit".
			// Do not fallback to CPU picking in this case.
			if (bDebugLog)
			{
				UE_LOG("[IDPick] GPU miss (uuid=0), skip CPU fallback");
			}
			return true;
		}

		auto It = GUUIDToObjectMap.find(PickedUUID);
		if (It == GUUIDToObjectMap.end() || !It->second || It->second->IsPendingKill())
		{
			if (bDebugLog)
			{
				UE_LOG("[IDPick] UUID map miss/stale uuid=%u", PickedUUID);
			}
			return false;
		}

		UObject* PickedObject = It->second;
		if (PickedObject->IsA(AActor::StaticClass()))
		{
			OutActor = static_cast<AActor*>(PickedObject);
			return OutActor != nullptr;
		}

		if (PickedObject->IsA(UActorComponent::StaticClass()))
		{
			UActorComponent* Component = static_cast<UActorComponent*>(PickedObject);
			if (Component->GetOwner() && !Component->GetOwner()->IsPendingDestroy())
			{
				OutActor = Component->GetOwner();
			}
		}

		return OutActor != nullptr;
	}
}

FRay FPicker::ScreenToRay(const FViewportEntry& Entry, int32 ScreenX, int32 ScreenY) const
{
	if (!Entry.Viewport)
	{
		return { FVector::ZeroVector, FVector::ForwardVector };
	}

	const auto& Rect = Entry.Viewport->GetRect();
	if (Rect.Width <= 0 || Rect.Height <= 0)
	{
		return { FVector::ZeroVector, FVector::ForwardVector };
	}

	const float AspectRatio = static_cast<float>(Rect.Width) / static_cast<float>(Rect.Height);

	const FMatrix ViewMatrix = Entry.LocalState.BuildViewMatrix();
	const FMatrix ProjMatrix = Entry.LocalState.BuildProjMatrix(AspectRatio);
	const FMatrix ViewInverse = ViewMatrix.GetInverse();

	const float NdcX = (2.0f * (ScreenX + 0.5f) / Rect.Width) - 1.0f;
	const float NdcY = 1.0f - (2.0f * (ScreenY + 0.5f) / Rect.Height);

	if (Entry.LocalState.ProjectionType != EViewportType::Perspective)
	{
		const float ViewHeight = Entry.LocalState.OrthoZoom * 2.0f;
		const float ViewWidth = ViewHeight * AspectRatio;

		const float ViewRight = NdcX * (ViewWidth * 0.5f);
		const float ViewUp = NdcY * (ViewHeight * 0.5f);

		FVector RayOrigin;
		RayOrigin.X = ViewRight * ViewInverse.M[1][0] + ViewUp * ViewInverse.M[2][0] + ViewInverse.M[3][0];
		RayOrigin.Y = ViewRight * ViewInverse.M[1][1] + ViewUp * ViewInverse.M[2][1] + ViewInverse.M[3][1];
		RayOrigin.Z = ViewRight * ViewInverse.M[1][2] + ViewUp * ViewInverse.M[2][2] + ViewInverse.M[3][2];

		FVector Forward = FVector::ForwardVector;
		switch (Entry.LocalState.ProjectionType)
		{
		case EViewportType::OrthoTop: Forward = FVector::DownVector; break;
		case EViewportType::OrthoBottom: Forward = FVector::UpVector; break;
		case EViewportType::OrthoLeft: Forward = FVector::RightVector; break;
		case EViewportType::OrthoRight: Forward = FVector::LeftVector; break;
		case EViewportType::OrthoFront: Forward = FVector::BackwardVector; break;
		case EViewportType::OrthoBack: Forward = FVector::ForwardVector; break;
		default: break;
		}

		return { RayOrigin, Forward };
	}

	const float ViewForward = 1.0f;
	const float ViewRight = NdcX / ProjMatrix.M[1][0];
	const float ViewUp = NdcY / ProjMatrix.M[2][1];

	FVector RayDirectionWorld;
	RayDirectionWorld.X = ViewForward * ViewInverse.M[0][0] + ViewRight * ViewInverse.M[1][0] + ViewUp * ViewInverse.M[2][0];
	RayDirectionWorld.Y = ViewForward * ViewInverse.M[0][1] + ViewRight * ViewInverse.M[1][1] + ViewUp * ViewInverse.M[2][1];
	RayDirectionWorld.Z = ViewForward * ViewInverse.M[0][2] + ViewRight * ViewInverse.M[1][2] + ViewUp * ViewInverse.M[2][2];
	RayDirectionWorld = RayDirectionWorld.GetSafeNormal();

	FVector RayOrigin;
	RayOrigin.X = ViewInverse.M[3][0];
	RayOrigin.Y = ViewInverse.M[3][1];
	RayOrigin.Z = ViewInverse.M[3][2];

	return { RayOrigin, RayDirectionWorld };
}

bool FPicker::RayTriangleIntersect(const FRay& Ray,
	const FVector& V0, const FVector& V1, const FVector& V2,
	float& OutDistance) const
{
	constexpr float Epsilon = 1.e-6f;

	const FVector Edge1 = V1 - V0;
	const FVector Edge2 = V2 - V0;

	const FVector H = FVector::CrossProduct(Ray.Direction, Edge2);
	const float A = FVector::DotProduct(Edge1, H);
	if (A <= Epsilon)
	{
		return false;
	}

	const float F = 1.0f / A;
	const FVector S = Ray.Origin - V0;
	const float U = F * FVector::DotProduct(S, H);
	if (U < 0.0f || U > 1.0f)
	{
		return false;
	}

	const FVector Q = FVector::CrossProduct(S, Edge1);
	const float V = F * FVector::DotProduct(Ray.Direction, Q);
	if (V < 0.0f || U + V > 1.0f)
	{
		return false;
	}

	const float T = F * FVector::DotProduct(Edge2, Q);
	if (T > Epsilon)
	{
		OutDistance = T;
		return true;
	}

	return false;
}

AActor* FPicker::PickActor(ULevel* Scene, const FViewportEntry* Entry, int32 ScreenX, int32 ScreenY, FEditorEngine* Engine) const
{
	if (!Scene || !Entry)
	{
		return nullptr;
	}

	if (IsGPUIdPickingEnabled())
	{
		AActor* GPUPickedActor = nullptr;
		const bool bGPUHandled = TryPickActorByGPUId(Scene, Entry, ScreenX, ScreenY, Engine, GPUPickedActor);
		if (IsGPUIdPickingDebugEnabled())
		{
			UE_LOG("[IDPick] GPU handled=%d actor=%s",
				bGPUHandled ? 1 : 0,
				GPUPickedActor ? GPUPickedActor->GetName().c_str() : "None");
		}
		if (bGPUHandled)
		{
			return GPUPickedActor;
		}
	}

	const FRay WorldRay = ScreenToRay(*Entry, ScreenX, ScreenY);
	const bool bUseAccurateMeshRay = IsAccurateMeshPickingEnabled();

	AActor* ClosestActor = nullptr;
	float ClosestDistance = (std::numeric_limits<float>::max)();

	Scene->VisitPrimitivesByRay(
		WorldRay.Origin,
		WorldRay.Direction,
		ClosestDistance,
		[&](UPrimitiveComponent* PrimComp, float BoundsNear, float BoundsFar, float& InOutClosestDistance)
		{
			if (!PrimComp || PrimComp->IsPendingKill())
			{
				return;
			}

			if (IsArrowVisualizationPrimitive(PrimComp) || IsHiddenByArrowVisualizationShowFlags(PrimComp, Entry->LocalState.ShowFlags))
			{
				return;
			}

			AActor* Actor = PrimComp->GetOwner();
			if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible())
			{
				return;
			}

			if (!PrimComp->IsPickable())
			{
				return;
			}

			if (PrimComp->UseSpherePicking())
			{
				const FBoxSphereBounds Bounds = PrimComp->GetWorldBounds();
				float SphereT = 0.0f;
				if (RayIntersectsSphere(WorldRay, Bounds.Center, Bounds.Radius, SphereT) && SphereT < InOutClosestDistance)
				{
					InOutClosestDistance = SphereT;
					ClosestActor = Actor;
				}
				return;
			}

			const float BoundsHitT = (BoundsNear >= 0.0f) ? BoundsNear : BoundsFar;
			if (BoundsHitT > InOutClosestDistance)
			{
				return;
			}

			if (PrimComp->HasMeshIntersection() && bUseAccurateMeshRay)
			{
				const FMatrix World = PrimComp->GetBoundsWorldTransform();
				const FMatrix InvWorld = World.GetInverse();

				FRay LocalRay;
				LocalRay.Origin = TransformPointRowVector(WorldRay.Origin, InvWorld);
				LocalRay.Direction = TransformVectorRowVector(WorldRay.Direction, InvWorld).GetSafeNormal();
				if (!LocalRay.Direction.IsZero())
				{
					float LocalDistance = (std::numeric_limits<float>::max)();
					if (PrimComp->IntersectLocalRay(LocalRay.Origin, LocalRay.Direction, LocalDistance))
					{
						const FVector LocalHitPoint = LocalRay.Origin + LocalRay.Direction * LocalDistance;
						const FVector WorldHitPoint = TransformPointRowVector(LocalHitPoint, World);
						const float WorldDistance = (WorldHitPoint - WorldRay.Origin).Size();

						if (WorldDistance < InOutClosestDistance)
						{
							InOutClosestDistance = WorldDistance;
							ClosestActor = Actor;
						}
					}
				}
				return;
			}

			if (BoundsHitT < InOutClosestDistance)
			{
				InOutClosestDistance = BoundsHitT;
				ClosestActor = Actor;
			}
		});
	return ClosestActor;
}
