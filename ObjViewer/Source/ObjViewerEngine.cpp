#include "ObjViewerEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

#include "ObjViewerShell.h"
#include "ObjViewerViewportClient.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "Actor/Actor.h"
#include "Actor/StaticMeshActor.h"
#include "Asset/StaticMeshLODBuilder.h"
#include "Asset/ObjManager.h"
#include "Camera/Camera.h"
#include "Component/CameraComponent.h"
#include "Component/SceneComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Core/Paths.h"
#include "Core/ShowFlags.h"
#include "Debug/EngineLog.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Frustum.h"
#include "Platform/Windows/WindowsWindow.h"
#include "Renderer/Frame/FrameRequests.h"
#include "Renderer/Frame/RenderFrameUtils.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Features/Debug/DebugLineRenderFeature.h"
#include "Renderer/Features/Debug/DebugTypes.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Mesh/MeshBatch.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/Renderer.h"
#include "Renderer/GraphicsCore/RenderStateManager.h"
#include "Renderer/Resources/Shader/ShaderMap.h"
#include "Level/Level.h"
#include "World/World.h"

namespace
{
	FString WideToUtf8(const std::wstring& WideString)
	{
		if (WideString.empty())
		{
			return "";
		}

		const int32 RequiredBytes = ::WideCharToMultiByte(
			CP_UTF8,
			0,
			WideString.c_str(),
			-1,
			nullptr,
			0,
			nullptr,
			nullptr);
		if (RequiredBytes <= 1)
		{
			return "";
		}

		FString Result;
		Result.resize(static_cast<size_t>(RequiredBytes));
		::WideCharToMultiByte(
			CP_UTF8,
			0,
			WideString.c_str(),
			-1,
			Result.data(),
			RequiredBytes,
			nullptr,
			nullptr);
		Result.pop_back();
		return Result;
	}

	bool MeshHasAnyUVs(const UStaticMesh* Mesh)
	{
		if (Mesh == nullptr || Mesh->GetRenderData() == nullptr)
		{
			return false;
		}

		for (const FVertex& Vertex : Mesh->GetRenderData()->Vertices)
		{
			if (Vertex.UV.X != 0.0f || Vertex.UV.Y != 0.0f)
			{
				return true;
			}
		}

		return false;
	}

	float GetMaxAbsScale(const FVector& Scale)
	{
		const float AbsX = std::fabs(Scale.X);
		const float AbsY = std::fabs(Scale.Y);
		const float AbsZ = std::fabs(Scale.Z);
		return (std::max)(AbsX, (std::max)(AbsY, AbsZ));
	}

	TArray<FString> BuildMaterialSlotNames(const UStaticMesh* Mesh)
	{
		TArray<FString> MaterialSlotNames;
		if (Mesh == nullptr || Mesh->GetRenderData() == nullptr)
		{
			return MaterialSlotNames;
		}

		uint32 SlotCount = static_cast<uint32>(Mesh->GetDefaultMaterials().size());
		for (const FMeshSection& Section : Mesh->GetRenderData()->Sections)
		{
			SlotCount = (std::max)(SlotCount, Section.MaterialIndex + 1);
		}

		if (SlotCount == 0)
		{
			SlotCount = 1;
		}

		MaterialSlotNames.resize(SlotCount, "M_Default");

		const TArray<std::shared_ptr<FMaterial>>& DefaultMaterials = Mesh->GetDefaultMaterials();
		for (uint32 SlotIndex = 0; SlotIndex < SlotCount && SlotIndex < DefaultMaterials.size(); ++SlotIndex)
		{
			const std::shared_ptr<FMaterial>& Material = DefaultMaterials[SlotIndex];
			if (Material && !Material->GetOriginName().empty())
			{
				MaterialSlotNames[SlotIndex] = Material->GetOriginName();
			}
		}

		return MaterialSlotNames;
	}

	FStaticMesh BuildBakedMeshCopy(const FStaticMesh& SourceMesh, float UniformScale)
	{
		const float BakedScale = (std::max)(UniformScale, 0.01f);

		FStaticMesh BakedMesh;
		BakedMesh.Topology = SourceMesh.Topology;
		BakedMesh.PathFileName = SourceMesh.PathFileName;
		BakedMesh.Sections = SourceMesh.Sections;
		BakedMesh.Indices = SourceMesh.Indices;
		BakedMesh.Vertices = SourceMesh.Vertices;

		for (FVertex& Vertex : BakedMesh.Vertices)
		{
			Vertex.Position = Vertex.Position * BakedScale;
			if (!Vertex.Normal.IsNearlyZero())
			{
				Vertex.Normal = Vertex.Normal.GetSafeNormal();
			}
		}

		BakedMesh.bIsDirty = true;
		return BakedMesh;
	}

	FVector GetLoadedModelWorldCenter(const FObjViewerModelState& ModelState)
	{
		if (ModelState.DisplayActor == nullptr)
		{
			return FVector::ZeroVector;
		}

		USceneComponent* RootComponent = ModelState.DisplayActor->GetRootComponent();
		if (RootComponent == nullptr)
		{
			return FVector::ZeroVector;
		}

		return RootComponent->GetWorldTransform().TransformPosition(ModelState.BoundsCenter);
	}

	float GetDisplayedBoundsRadius(const FObjViewerModelState& ModelState)
	{
		float Radius = ModelState.BoundsRadius;
		if (ModelState.DisplayActor)
		{
			if (USceneComponent* RootComponent = ModelState.DisplayActor->GetRootComponent())
			{
				Radius *= GetMaxAbsScale(RootComponent->GetRelativeTransform().GetScale3D());
			}
		}

		return Radius;
	}

	FVector4 GetNormalVisualizationColor(EObjViewerNormalVisualizationMode Mode)
	{
		switch (Mode)
		{
		case EObjViewerNormalVisualizationMode::Face:
			return FVector4(1.0f, 0.55f, 0.1f, 1.0f);
		case EObjViewerNormalVisualizationMode::Vertex:
		default:
			return FVector4(0.15f, 0.85f, 1.0f, 1.0f);
		}
	}

	D3D11_PRIMITIVE_TOPOLOGY ToD3DTopology(EMeshTopology Topology)
	{
		switch (Topology)
		{
		case EMeshTopology::EMT_Point:
			return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
		case EMeshTopology::EMT_LineList:
			return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
		case EMeshTopology::EMT_LineStrip:
			return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
		case EMeshTopology::EMT_TriangleStrip:
			return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		case EMeshTopology::EMT_TriangleList:
		default:
			return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		}
	}

	FString GetLodFilePath(const FString& MeshPathFileName, int32 LodLevel)
	{
		const std::filesystem::path MeshPath = FPaths::ToPath(FPaths::ToAbsolutePath(MeshPathFileName)).lexically_normal();
		const std::filesystem::path LodPath = MeshPath.parent_path()
			/ (MeshPath.stem().string() + "_lod" + std::to_string(LodLevel) + ".lod");
		return FPaths::FromPath(LodPath);
	}

	uint64 GetFileWriteTimestamp(const FString& PathFileName)
	{
		std::error_code ErrorCode;
		const std::filesystem::path Path = FPaths::ToPath(FPaths::ToAbsolutePath(PathFileName)).lexically_normal();
		if (Path.empty() || !std::filesystem::exists(Path, ErrorCode))
		{
			return 0;
		}

		const auto FileTime = std::filesystem::last_write_time(Path, ErrorCode);
		if (ErrorCode)
		{
			return 0;
		}

		const auto SystemTime = std::chrono::time_point_cast<std::chrono::nanoseconds>(
			FileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
		return static_cast<uint64>(SystemTime.time_since_epoch().count());
	}
}

FObjViewerEngine::FObjViewerEngine(const FObjViewerLaunchOptions& InLaunchOptions)
	: LaunchOptions(InLaunchOptions)
	, ViewerShell(std::make_unique<FObjViewerShell>())
{
}

FObjViewerEngine::~FObjViewerEngine() = default;

ULevel* FObjViewerEngine::GetActiveScene() const
{
	return (ViewerWorldContext && ViewerWorldContext->World) ? ViewerWorldContext->World->GetScene() : nullptr;
}

UWorld* FObjViewerEngine::GetActiveWorld() const
{
	return ViewerWorldContext ? ViewerWorldContext->World : nullptr;
}

const FWorldContext* FObjViewerEngine::GetActiveWorldContext() const
{
	return (ViewerWorldContext && ViewerWorldContext->IsValid()) ? ViewerWorldContext : nullptr;
}

bool FObjViewerEngine::LoadModelFromFile(const FString& FilePath, const FString& ImportSource)
{
	FObjImportSummary ImportOptions;
	ImportOptions.ImportSource = ImportSource;
	return LoadModelFromFile(FilePath, ImportOptions);
}

bool FObjViewerEngine::LoadModelFromFile(const FString& FilePath, const FObjImportSummary& ImportOptions)
{
	if (FilePath.empty())
	{
		return false;
	}

	UWorld* ViewerWorld = GetActiveWorld();
	ULevel* ViewerScene = GetActiveScene();
	if (!ViewerWorld || !ViewerScene)
	{
		return false;
	}

	FObjImportSummary AppliedImportOptions = ImportOptions;
	AppliedImportOptions.bReplaceCurrentModel = true;
	AppliedImportOptions.UniformScale = (std::max)(AppliedImportOptions.UniformScale, 0.01f);

	FObjLoadOptions LoadOptions;
	LoadOptions.bUseLegacyObjConversion = false;
	LoadOptions.ForwardAxis = AppliedImportOptions.ForwardAxis;
	LoadOptions.UpAxis = AppliedImportOptions.UpAxis;

	UStaticMesh* LoadedMesh = FObjManager::LoadObjStaticMeshAsset(FilePath, LoadOptions);
	if (!LoadedMesh)
	{
		UE_LOG("[ObjViewer] Failed to load OBJ: %s", FilePath.c_str());
		return false;
	}

	if (AppliedImportOptions.bReplaceCurrentModel || !HasLoadedModel())
	{
		ClearLoadedModel();
	}

	AStaticMeshActor* DisplayActor = ViewerWorld->SpawnActor<AStaticMeshActor>("DroppedObjActor");
	if (!DisplayActor)
	{
		UE_LOG("[ObjViewer] Failed to spawn actor for OBJ: %s", FilePath.c_str());
		return false;
	}

	UStaticMeshComponent* MeshComponent = DisplayActor->GetComponentByClass<UStaticMeshComponent>();
	if (!MeshComponent)
	{
		UE_LOG("[ObjViewer] Failed to get mesh component for OBJ: %s", FilePath.c_str());
		return false;
	}

	MeshComponent->SetStaticMesh(LoadedMesh);
	MeshComponent->SetLODEnabled(AppliedImportOptions.bEnableLOD);

	if (USceneComponent* RootComponent = DisplayActor->GetRootComponent())
	{
		FTransform Transform = RootComponent->GetRelativeTransform();
		Transform.SetScale3D(FVector(
			AppliedImportOptions.UniformScale,
			AppliedImportOptions.UniformScale,
			AppliedImportOptions.UniformScale));

		const FVector ScaledCenter = LoadedMesh->LocalBounds.Center * AppliedImportOptions.UniformScale;
		const FVector ScaledExtent = LoadedMesh->LocalBounds.BoxExtent * AppliedImportOptions.UniformScale;
		FVector Translation = FVector::ZeroVector;

		if (AppliedImportOptions.bCenterToOrigin)
		{
			Translation = -ScaledCenter;
		}

		if (AppliedImportOptions.bPlaceOnGround)
		{
			const float BottomZ = (ScaledCenter.Z + Translation.Z) - ScaledExtent.Z;
			Translation.Z -= BottomZ;
		}

		Transform.SetTranslation(Translation);
		RootComponent->SetRelativeTransform(Transform);
	}

	UpdateLoadedModelState(FilePath, AppliedImportOptions, LoadedMesh, DisplayActor);
	if (AppliedImportOptions.bFrameCameraAfterImport)
	{
		FrameLoadedModel();
	}

	return true;
}

void FObjViewerEngine::SetLoadedModelLODEnabled(bool bEnabled)
{
	ModelState.bLodEnabled = bEnabled;
	ModelState.LastImportSummary.bEnableLOD = bEnabled;

	if (ModelState.DisplayActor == nullptr)
	{
		return;
	}

	if (UStaticMeshComponent* MeshComponent = ModelState.DisplayActor->GetComponentByClass<UStaticMeshComponent>())
	{
		MeshComponent->SetLODEnabled(bEnabled);
	}
}

void FObjViewerEngine::SetLoadedModelLodDistance(int32 LODIndex, float Distance)
{
	if (ModelState.DisplayActor)
	{
		if (UStaticMeshComponent* MeshComponent = ModelState.DisplayActor->GetComponentByClass<UStaticMeshComponent>())
		{
			MeshComponent->SetLODDistance(LODIndex, Distance);
		}
	}
}

float FObjViewerEngine::GetLoadedModelLodDistance(int32 LODIndex) const
{
	if (ModelState.DisplayActor)
	{
		if (UStaticMeshComponent* MeshComponent = ModelState.DisplayActor->GetComponentByClass<UStaticMeshComponent>())
		{
			return MeshComponent->GetLODDistance(LODIndex);
		}
	}
	return 0.0f;
}

int32 FObjViewerEngine::GetLoadedModelLodDistanceCount() const
{
	if (ModelState.DisplayActor)
	{
		if (UStaticMeshComponent* MeshComponent = ModelState.DisplayActor->GetComponentByClass<UStaticMeshComponent>())
		{
			return MeshComponent->GetLODDistanceCount();
		}
	}
	return 0;
}

int32 FObjViewerEngine::GetLoadedModelCurrentLODIndex() const
{
	if (ModelState.DisplayActor)
	{
		if (UStaticMeshComponent* MeshComponent = ModelState.DisplayActor->GetComponentByClass<UStaticMeshComponent>())
		{
			return MeshComponent->GetCurrentLODIndex();
		}
	}
	return 0;
}

float FObjViewerEngine::GetLoadedModelCurrentLODDistance() const
{
	if (ModelState.DisplayActor)
	{
		if (UStaticMeshComponent* MeshComponent = ModelState.DisplayActor->GetComponentByClass<UStaticMeshComponent>())
		{
			return MeshComponent->GetLastLODSelectionDistance();
		}
	}
	return 0.0f;
}

bool FObjViewerEngine::ExportLoadedModelAsModel(const FString& FilePath) const
{
	if (FilePath.empty() || !HasLoadedModel() || ModelState.Mesh == nullptr || ModelState.Mesh->GetRenderData() == nullptr)
	{
		UE_LOG("[ObjViewer] Export skipped because no valid mesh is loaded.");
		return false;
	}

	FStaticMesh BakedMesh = BuildBakedMeshCopy(
		*ModelState.Mesh->GetRenderData(),
		ModelState.LastImportSummary.UniformScale);
	const TArray<FString> MaterialSlotNames = BuildMaterialSlotNames(ModelState.Mesh);
	TArray<FModelMaterialInfo> MaterialInfos;
	const bool bBuiltMaterialInfos = FObjManager::BuildModelMaterialInfosFromObj(ModelState.SourceFilePath, FilePath, MaterialSlotNames, MaterialInfos);
	if (!bBuiltMaterialInfos)
	{
		UE_LOG("[ObjViewer] Falling back to default embedded material metadata for export: %s", ModelState.SourceFilePath.c_str());
	}

	const bool bSaved = FObjManager::SaveModelStaticMeshAsset(FilePath, BakedMesh, MaterialInfos, GetFileWriteTimestamp(ModelState.SourceFilePath));
	UE_LOG("[ObjViewer] %s .Model export: %s", bSaved ? "Succeeded" : "Failed", FilePath.c_str());
	return bSaved;
}

bool FObjViewerEngine::GenerateLoadedModelLODs()
{
	if (!HasLoadedModel() || ModelState.Mesh == nullptr || ModelState.Mesh->GetRenderData() == nullptr)
	{
		LastOperationStatus = "LOD generation skipped: no valid mesh loaded.";
		UE_LOG("[ObjViewer] LOD generation skipped because no valid mesh is loaded.");
		return false;
	}

	const FString SourcePath = ModelState.SourceFilePath;
	if (SourcePath.empty())
	{
		LastOperationStatus = "LOD generation skipped: source path is empty.";
		UE_LOG("[ObjViewer] LOD generation skipped because source path is empty.");
		return false;
	}

	const uint64 SourceTimestamp = GetFileWriteTimestamp(SourcePath);

	FStaticMeshLODSettings Settings;
	Settings.NumLODs = (std::max)(LODBuilderSettings.NumLODs, 0);
	Settings.TriangleReductionStep = (std::clamp)(LODBuilderSettings.TriangleReductionStep, 0.01f, 0.95f);
	Settings.DistanceStep = (std::max)(LODBuilderSettings.DistanceStep, 1.0f);

	FStaticMeshLODBuilder::BuildLODs(*ModelState.Mesh, Settings);

	const uint32 LodCount = ModelState.Mesh->GetLodCount();
	for (uint32 LodIndex = 1; LodIndex < LodCount; ++LodIndex)
	{
		FStaticMesh* LodMesh = ModelState.Mesh->GetRenderData(static_cast<int32>(LodIndex));
		if (!LodMesh)
		{
			continue;
		}

		const FString LodPath = GetLodFilePath(SourcePath, static_cast<int32>(LodIndex));
		if (!FObjManager::SaveLodAsset(LodPath, *LodMesh, SourceTimestamp, ModelState.Mesh->GetLodDistance(static_cast<int32>(LodIndex))))
		{
			LastOperationStatus = "LOD generation failed while saving files.";
			UE_LOG("[ObjViewer] Failed to save generated LOD%d: %s", static_cast<int32>(LodIndex), LodPath.c_str());
			return false;
		}
	}

	if (LodCount <= 1)
	{
		LastOperationStatus = "LOD generation completed, but no additional LODs were produced.";
		UE_LOG("[ObjViewer] No additional LODs were generated for: %s", SourcePath.c_str());
	}
	else
	{
		LastOperationStatus = "LOD files generated successfully.";
		UE_LOG("[ObjViewer] Generated %u LOD(s) for: %s", LodCount - 1, SourcePath.c_str());
	}

	UpdateLoadedModelState(SourcePath, ModelState.LastImportSummary, ModelState.Mesh, ModelState.DisplayActor);
	SetLoadedModelLODEnabled(ModelState.LastImportSummary.bEnableLOD);
	return true;
}

bool FObjViewerEngine::DeleteLoadedModelLODs()
{
	if (!HasLoadedModel() || ModelState.SourceFilePath.empty())
	{
		LastOperationStatus = "LOD deletion skipped: no valid mesh loaded.";
		UE_LOG("[ObjViewer] LOD deletion skipped because no valid mesh is loaded.");
		return false;
	}

	bool bDeletedAny = false;
	for (int32 LodIndex = 1; LodIndex <= 64; ++LodIndex)
	{
		const std::filesystem::path LodPath = FPaths::ToPath(GetLodFilePath(ModelState.SourceFilePath, LodIndex)).lexically_normal();
		std::error_code ErrorCode;
		if (std::filesystem::exists(LodPath, ErrorCode) && std::filesystem::remove(LodPath, ErrorCode))
		{
			bDeletedAny = true;
		}
	}

	if (!bDeletedAny)
	{
		LastOperationStatus = "No LOD files were found to delete.";
		UE_LOG("[ObjViewer] No LOD files found to delete for: %s", ModelState.SourceFilePath.c_str());
		return true;
	}

	ModelState.Mesh->ClearLods();
	UpdateLoadedModelState(ModelState.SourceFilePath, ModelState.LastImportSummary, ModelState.Mesh, ModelState.DisplayActor);
	SetLoadedModelLODEnabled(ModelState.LastImportSummary.bEnableLOD);
	LastOperationStatus = "LOD files deleted.";
	UE_LOG("[ObjViewer] Deleted LOD files for: %s", ModelState.SourceFilePath.c_str());
	return true;
}

bool FObjViewerEngine::ReloadLoadedModel()
{
	if (ModelState.SourceFilePath.empty())
	{
		return false;
	}

	FObjImportSummary ReloadOptions = ModelState.LastImportSummary;
	ReloadOptions.ImportSource = "Reload";
	return LoadModelFromFile(ModelState.SourceFilePath, ReloadOptions);
}

void FObjViewerEngine::ClearLoadedModel()
{
	UWorld* ViewerWorld = GetActiveWorld();
	ULevel* ViewerScene = GetActiveScene();
	if (ViewerScene)
	{
		const TArray<AActor*> ExistingActors = ViewerScene->GetActors();
		for (AActor* Actor : ExistingActors)
		{
			if (Actor == nullptr || Actor->IsPendingDestroy())
			{
				continue;
			}

			if (ViewerWorld)
			{
				ViewerWorld->DestroyActor(Actor);
			}
			else
			{
				ViewerScene->DestroyActor(Actor);
			}
		}

		ViewerScene->CleanupDestroyedActors();
	}

	ModelState = {};
}

void FObjViewerEngine::FrameLoadedModel()
{
	if (!HasLoadedModel())
	{
		ResetViewerCamera();
		return;
	}

	UWorld* ViewerWorld = GetActiveWorld();
	if (!ViewerWorld)
	{
		return;
	}

	UCameraComponent* ActiveCamera = ViewerWorld->GetActiveCameraComponent();
	if (!ActiveCamera)
	{
		return;
	}

	if (FCamera* Camera = ActiveCamera->GetCamera())
	{
		float Radius = ModelState.BoundsRadius;
		if (ModelState.DisplayActor)
		{
			if (USceneComponent* RootComponent = ModelState.DisplayActor->GetRootComponent())
			{
				Radius *= GetMaxAbsScale(RootComponent->GetRelativeTransform().GetScale3D());
			}
		}

		const float Distance = (std::max)(Radius, 1.0f) * 3.0f;
		const FVector WorldCenter = GetLoadedModelWorldCenter(ModelState);
		Camera->SetPosition(WorldCenter + FVector(Distance, 0.0f, 0.0f));
		Camera->SetRotation(180.0f, 0.0f);
	}

	ActiveCamera->SetFov(50.0f);
}

void FObjViewerEngine::ResetViewerCamera()
{
	InitializeViewerCamera();
}

FObjViewerShell& FObjViewerEngine::GetShell() const
{
	return *ViewerShell;
}

void FObjViewerEngine::BindHost(FWindowsWindow* InMainWindow)
{
	MainWindow = InMainWindow;

	if (ViewerShell)
	{
		ViewerShell->Initialize(this);
		ViewerShell->SetupWindow(InMainWindow);
	}
}

bool FObjViewerEngine::InitializeWorlds()
{
	const float AspectRatio = GetWindowAspectRatio();

	ViewerWorldContext = CreateWorldContext("ObjViewerScene", EWorldType::Preview, AspectRatio, false);
	if (!ViewerWorldContext || !ViewerWorldContext->World)
	{
		return false;
	}

	InitializeViewerCamera();
	return true;
}

bool FObjViewerEngine::InitializeMode()
{
	WireframeMaterial = FMaterialManager::Get().FindByName(WireframeMaterialName);
	CreateGridResources();
	CreateAxisResources();
	return true;
}

void FObjViewerEngine::FinalizeInitialize()
{
	FEngine::FinalizeInitialize();
	ProcessLaunchOptions();
}

std::unique_ptr<IViewportClient> FObjViewerEngine::CreateViewportClient()
{
	return std::make_unique<FObjViewerViewportClient>();
}

void FObjViewerEngine::TickWorlds(float DeltaTime)
{
	if (UWorld* ViewerWorld = GetActiveWorld())
	{
		ViewerWorld->Tick(DeltaTime);
	}
}

void FObjViewerEngine::RenderFrame()
{
	FRenderer* Renderer = GetRenderer();
	if (!Renderer || Renderer->IsOccluded())
	{
		return;
	}

	UWorld* ActiveWorld = GetActiveWorld();
	ULevel* Scene = GetViewportClient() ? GetViewportClient()->ResolveScene(this) : GetActiveScene();

	auto RenderShellOnly = [&]()
	{
		Renderer->BeginFrame();
		if (ViewerShell)
		{
			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			ViewerShell->Render();
			ImGui::Render();
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}
		Renderer->EndFrame();
		GetDebugDrawManager().Clear();
	};

	if (!Scene || !ActiveWorld)
	{
		RenderShellOnly();
		return;
	}

	UCameraComponent* ActiveCamera = ActiveWorld->GetActiveCameraComponent();
	if (!ActiveCamera)
	{
		RenderShellOnly();
		return;
	}

	FShowFlags ViewerShowFlags;
	ViewerShowFlags.SetFlag(EEngineShowFlags::SF_UUID, false);
	ViewerShowFlags.SetFlag(EEngineShowFlags::SF_DebugDraw, false);

	FGameFrameRequest FrameRequest;
	FrameRequest.SceneView.ViewMatrix = ActiveCamera->GetViewMatrix();
	FrameRequest.SceneView.ProjectionMatrix = ActiveCamera->GetProjectionMatrix();
	FrameRequest.SceneView.CameraPosition = FrameRequest.SceneView.ViewMatrix.GetInverse().GetTranslation();
	FrameRequest.SceneView.NearZ = ActiveCamera->GetNearPlane();
	FrameRequest.SceneView.FarZ = ActiveCamera->GetFarPlane();
	FrameRequest.SceneView.TotalTimeSeconds = static_cast<float>(GetTimer().GetTotalTime());

	FFrustum Frustum;
	Frustum.ExtractFromVP(FrameRequest.SceneView.ViewMatrix * FrameRequest.SceneView.ProjectionMatrix);

	if (IViewportClient* ViewportClient = GetViewportClient())
	{
		ViewportClient->BuildSceneRenderPacket(this, ActiveWorld, Frustum, ViewerShowFlags, FrameRequest.ScenePacket);
	}

	FrameRequest.DebugInputs.DrawManager = &GetDebugDrawManager();
	FrameRequest.DebugInputs.World = ActiveWorld;
	FrameRequest.DebugInputs.ShowFlags = ViewerShowFlags;

	if (bWireframeEnabled && WireframeMaterial)
	{
		FrameRequest.bForceWireframe = true;
		FrameRequest.WireframeMaterial = WireframeMaterial.get();
	}

	AppendGridMeshBatch(FrameRequest);

	Renderer->BeginFrame();

	if (ViewerShell)
	{
		ViewerShell->PrepareViewportSurface(Renderer);
		const FObjViewerViewportSurface& Surface = ViewerShell->GetViewportSurface();

		if (Surface.IsValid())
		{
			const float SurfaceW = static_cast<float>(Surface.GetWidth());
			const float SurfaceH = static_cast<float>(Surface.GetHeight());
			UpdateWorldAspectRatio(ActiveWorld, SurfaceW / SurfaceH);

			FViewportScenePassRequest ScenePass;
			ScenePass.RenderTargetView = Surface.GetRTV();
			ScenePass.RenderTargetShaderResourceView = Surface.GetSRV();
			ScenePass.DepthStencilView = Surface.GetDSV();
			ScenePass.DepthShaderResourceView = Surface.GetDepthSRV();
			ScenePass.Viewport.TopLeftX = 0.0f;
			ScenePass.Viewport.TopLeftY = 0.0f;
			ScenePass.Viewport.Width = SurfaceW;
			ScenePass.Viewport.Height = SurfaceH;
			ScenePass.Viewport.MinDepth = 0.0f;
			ScenePass.Viewport.MaxDepth = 1.0f;
			ScenePass.SceneView = FrameRequest.SceneView;
			ScenePass.ScenePacket = std::move(FrameRequest.ScenePacket);
			ScenePass.AdditionalMeshBatches = std::move(FrameRequest.AdditionalMeshBatches);
			ScenePass.DebugInputs = FrameRequest.DebugInputs;
			ScenePass.bForceWireframe = FrameRequest.bForceWireframe;
			ScenePass.WireframeMaterial = FrameRequest.WireframeMaterial;

			FEditorFrameRequest EditorRequest;
			EditorRequest.ScenePasses.push_back(std::move(ScenePass));
			Renderer->RenderEditorFrame(EditorRequest);
			RenderViewportOverlays(*Renderer, Surface, FrameRequest.SceneView);
			Renderer->GetRenderDevice().BindSwapChainRTV();
		}
		else
		{
			FrameRequest.DebugInputs.ShowFlags.SetFlag(EEngineShowFlags::SF_DebugDraw, NormalSettings.bVisible);
			AppendNormalVisualizationDebugDraw();
			Renderer->RenderGameFrame(FrameRequest);
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		ViewerShell->Render();

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}
	else
	{
		FrameRequest.DebugInputs.ShowFlags.SetFlag(EEngineShowFlags::SF_DebugDraw, NormalSettings.bVisible);
		AppendNormalVisualizationDebugDraw();
		Renderer->RenderGameFrame(FrameRequest);
	}

	Renderer->EndFrame();
	GetDebugDrawManager().Clear();
}

void FObjViewerEngine::InitializeViewerCamera() const
{
	UWorld* ViewerWorld = GetActiveWorld();
	if (!ViewerWorld)
	{
		return;
	}

	UCameraComponent* ActiveCamera = ViewerWorld->GetActiveCameraComponent();
	if (!ActiveCamera)
	{
		return;
	}

	if (FCamera* Camera = ActiveCamera->GetCamera())
	{
		Camera->SetPosition({ 8.0f, 0.0f, 0.0f });
		Camera->SetRotation(180.0f, 0.0f);
	}

	ActiveCamera->SetFov(50.0f);
}

void FObjViewerEngine::CreateGridResources()
{
	FRenderer* Renderer = GetRenderer();
	if (Renderer == nullptr || GridMesh || GridMaterial)
	{
		return;
	}

	ID3D11Device* Device = Renderer->GetDevice();
	if (Device == nullptr)
	{
		return;
	}

	constexpr int32 GridVertexCount = 6;

	GridMesh = std::make_unique<FDynamicMesh>();
	GridMesh->Topology = EMeshTopology::EMT_TriangleList;
	for (int32 Index = 0; Index < GridVertexCount; ++Index)
	{
		FVertex Vertex;
		GridMesh->Vertices.push_back(Vertex);
		GridMesh->Indices.push_back(Index);
	}
	GridMesh->CreateVertexAndIndexBuffer(Device);

	std::wstring ShaderDirW = FPaths::ShaderDir();
	std::wstring VSPath = ShaderDirW + L"GridVertexShader.hlsl";
	std::wstring PSPath = ShaderDirW + L"GridPixelShader.hlsl";
	auto VS = FShaderMap::Get().GetOrCreateVertexShader(Device, VSPath.c_str());
	auto PS = FShaderMap::Get().GetOrCreatePixelShader(Device, PSPath.c_str());

	GridMaterial = std::make_shared<FMaterial>();
	GridMaterial->SetOriginName("M_ObjViewerGrid");
	GridMaterial->SetVertexShader(VS);
	GridMaterial->SetPixelShader(PS);

	FRasterizerStateOption RasterizerOption;
	RasterizerOption.FillMode = D3D11_FILL_SOLID;
	RasterizerOption.CullMode = D3D11_CULL_NONE;
	auto RS = Renderer->GetRenderStateManager()->GetOrCreateRasterizerState(RasterizerOption);
	GridMaterial->SetRasterizerOption(RasterizerOption);
	GridMaterial->SetRasterizerState(RS);

	FDepthStencilStateOption DepthStencilOption;
	DepthStencilOption.DepthEnable = true;
	DepthStencilOption.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	auto DSS = Renderer->GetRenderStateManager()->GetOrCreateDepthStencilState(DepthStencilOption);
	GridMaterial->SetDepthStencilOption(DepthStencilOption);
	GridMaterial->SetDepthStencilState(DSS);

	int32 SlotIndex = GridMaterial->CreateConstantBuffer(Device, 64);
	if (SlotIndex < 0)
	{
		return;
	}

	GridMaterial->RegisterParameter("GridSize", SlotIndex, 0, 4);
	GridMaterial->RegisterParameter("LineThickness", SlotIndex, 4, 4);
	GridMaterial->RegisterParameter("GridAxisU", SlotIndex, 16, 16);
	GridMaterial->RegisterParameter("GridAxisV", SlotIndex, 32, 16);
	GridMaterial->RegisterParameter("ViewForward", SlotIndex, 48, 16);

	const FVector4 DefaultGridAxisU = FVector4(FVector::ForwardVector, 0.0f);
	const FVector4 DefaultGridAxisV = FVector4(FVector::RightVector, 0.0f);
	const FVector4 DefaultViewForward = FVector4(FVector::ForwardVector, 0.0f);
	GridMaterial->SetParameterData("GridSize", &GridSettings.GridSize, 4);
	GridMaterial->SetParameterData("LineThickness", &GridSettings.LineThickness, 4);
	GridMaterial->SetParameterData("GridAxisU", &DefaultGridAxisU, sizeof(FVector4));
	GridMaterial->SetParameterData("GridAxisV", &DefaultGridAxisV, sizeof(FVector4));
	GridMaterial->SetParameterData("ViewForward", &DefaultViewForward, sizeof(FVector4));
}

void FObjViewerEngine::CreateAxisResources()
{
	FRenderer* Renderer = GetRenderer();
	if (Renderer == nullptr || WorldAxisMesh || WorldAxisMaterial)
	{
		return;
	}

	ID3D11Device* Device = Renderer->GetDevice();
	if (Device == nullptr)
	{
		return;
	}

	// AxisVertexShader: 3축 x 2변형 x 6정점 = 36 정점을 SV_VertexID로 생성
	constexpr int32 AxisVertexCount = 36;

	WorldAxisMesh = std::make_unique<FDynamicMesh>();
	WorldAxisMesh->Topology = EMeshTopology::EMT_TriangleList;
	for (int32 Index = 0; Index < AxisVertexCount; ++Index)
	{
		FVertex Vertex;
		WorldAxisMesh->Vertices.push_back(Vertex);
		WorldAxisMesh->Indices.push_back(Index);
	}
	WorldAxisMesh->CreateVertexAndIndexBuffer(Device);

	std::wstring ShaderDirW = FPaths::ShaderDir();
	std::wstring VSPath = ShaderDirW + L"EditorScreenOverlay/AxisVertexShader.hlsl";
	std::wstring PSPath = ShaderDirW + L"EditorScreenOverlay/AxisPixelShader.hlsl";
	auto VS = FShaderMap::Get().GetOrCreateVertexShader(Device, VSPath.c_str());
	auto PS = FShaderMap::Get().GetOrCreatePixelShader(Device, PSPath.c_str());

	WorldAxisMaterial = std::make_shared<FMaterial>();
	WorldAxisMaterial->SetOriginName("M_ObjViewerWorldAxis");
	WorldAxisMaterial->SetVertexShader(VS);
	WorldAxisMaterial->SetPixelShader(PS);

	FRasterizerStateOption RasterizerOption;
	RasterizerOption.FillMode = D3D11_FILL_SOLID;
	RasterizerOption.CullMode = D3D11_CULL_NONE;
	auto RS = Renderer->GetRenderStateManager()->GetOrCreateRasterizerState(RasterizerOption);
	WorldAxisMaterial->SetRasterizerOption(RasterizerOption);
	WorldAxisMaterial->SetRasterizerState(RS);

	FDepthStencilStateOption DepthStencilOption;
	DepthStencilOption.DepthEnable = true;
	DepthStencilOption.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	DepthStencilOption.DepthFunc = D3D11_COMPARISON_LESS;
	auto DSS = Renderer->GetRenderStateManager()->GetOrCreateDepthStencilState(DepthStencilOption);
	WorldAxisMaterial->SetDepthStencilOption(DepthStencilOption);
	WorldAxisMaterial->SetDepthStencilState(DSS);

	FBlendStateOption BlendOption;
	BlendOption.BlendEnable = true;
	BlendOption.SrcBlend = D3D11_BLEND_SRC_ALPHA;
	BlendOption.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	BlendOption.BlendOp = D3D11_BLEND_OP_ADD;
	BlendOption.SrcBlendAlpha = D3D11_BLEND_ONE;
	BlendOption.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	BlendOption.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	auto BS = Renderer->GetRenderStateManager()->GetOrCreateBlendState(BlendOption);
	WorldAxisMaterial->SetBlendOption(BlendOption);
	WorldAxisMaterial->SetBlendState(BS);

	int32 SlotIndex = WorldAxisMaterial->CreateConstantBuffer(Device, 64);
	if (SlotIndex < 0)
	{
		return;
	}

	WorldAxisMaterial->RegisterParameter("GridSize", SlotIndex, 0, 4);
	WorldAxisMaterial->RegisterParameter("LineThickness", SlotIndex, 4, 4);
	WorldAxisMaterial->RegisterParameter("GridAxisU", SlotIndex, 16, 16);
	WorldAxisMaterial->RegisterParameter("GridAxisV", SlotIndex, 32, 16);
	WorldAxisMaterial->RegisterParameter("ViewForward", SlotIndex, 48, 16);

	const FVector4 DefaultGridAxisU = FVector4(FVector::ForwardVector, 0.0f);
	const FVector4 DefaultGridAxisV = FVector4(FVector::RightVector, 0.0f);
	const FVector4 DefaultViewForward = FVector4(FVector::ForwardVector, 0.0f);
	WorldAxisMaterial->SetParameterData("GridSize", &GridSettings.GridSize, 4);
	WorldAxisMaterial->SetParameterData("LineThickness", &GridSettings.LineThickness, 4);
	WorldAxisMaterial->SetParameterData("GridAxisU", &DefaultGridAxisU, sizeof(FVector4));
	WorldAxisMaterial->SetParameterData("GridAxisV", &DefaultGridAxisV, sizeof(FVector4));
	WorldAxisMaterial->SetParameterData("ViewForward", &DefaultViewForward, sizeof(FVector4));
}

void FObjViewerEngine::ApplyWireframeOverride(FGameFrameRequest& Request) const
{
	if (!bWireframeEnabled || !WireframeMaterial)
	{
		return;
	}

	Request.bForceWireframe = true;
	Request.WireframeMaterial = WireframeMaterial.get();
}

void FObjViewerEngine::RenderViewportOverlays(
	FRenderer& Renderer,
	const FObjViewerViewportSurface& Surface,
	const FSceneViewRenderRequest& SceneView) const
{
	if (!Surface.IsValid())
	{
		return;
	}

	FSceneRenderTargets Targets;
	Targets.Width = static_cast<uint32>(Surface.GetWidth());
	Targets.Height = static_cast<uint32>(Surface.GetHeight());
	Targets.SceneColorRTV = Surface.GetRTV();
	Targets.SceneColorSRV = Surface.GetSRV();
	Targets.SceneDepthDSV = Surface.GetDSV();
	Targets.SceneDepthSRV = Surface.GetDepthSRV();

	D3D11_VIEWPORT Viewport = {};
	Viewport.TopLeftX = 0.0f;
	Viewport.TopLeftY = 0.0f;
	Viewport.Width = static_cast<float>(Surface.GetWidth());
	Viewport.Height = static_cast<float>(Surface.GetHeight());
	Viewport.MinDepth = 0.0f;
	Viewport.MaxDepth = 1.0f;

	const FFrameContext Frame = BuildRenderFrameContext(SceneView.TotalTimeSeconds);
	const FViewContext View = BuildRenderViewContext(SceneView, Viewport);
	const FMatrix ViewInverse = SceneView.ViewMatrix.GetInverse();
	const FVector4 GridAxisU = FVector4(FVector::ForwardVector, 0.0f);
	const FVector4 GridAxisV = FVector4(FVector::RightVector, 0.0f);
	const FVector4 ViewForward = FVector4(ViewInverse.GetForwardVector().GetSafeNormal(), 0.0f);

	if (GridSettings.bVisible && GridMesh && GridMaterial)
	{
		GridMaterial->SetParameterData("GridSize", &GridSettings.GridSize, 4);
		GridMaterial->SetParameterData("LineThickness", &GridSettings.LineThickness, 4);
		GridMaterial->SetParameterData("GridAxisU", &GridAxisU, sizeof(FVector4));
		GridMaterial->SetParameterData("GridAxisV", &GridAxisV, sizeof(FVector4));
		GridMaterial->SetParameterData("ViewForward", &ViewForward, sizeof(FVector4));

		FMeshBatch GridBatch;
		GridBatch.Mesh = GridMesh.get();
		GridBatch.Material = GridMaterial.get();
		GridBatch.World = FMatrix::Identity;
		RenderOverlayMeshBatch(Renderer, Frame, View, Targets, GridBatch, EMaterialPassType::EditorGrid);
	}

	if (GridSettings.bShowWorldAxis && WorldAxisMesh && WorldAxisMaterial)
	{
		WorldAxisMaterial->SetParameterData("GridSize", &GridSettings.GridSize, 4);
		WorldAxisMaterial->SetParameterData("LineThickness", &GridSettings.LineThickness, 4);
		WorldAxisMaterial->SetParameterData("GridAxisU", &GridAxisU, sizeof(FVector4));
		WorldAxisMaterial->SetParameterData("GridAxisV", &GridAxisV, sizeof(FVector4));
		WorldAxisMaterial->SetParameterData("ViewForward", &ViewForward, sizeof(FVector4));

		FMeshBatch AxisBatch;
		AxisBatch.Mesh = WorldAxisMesh.get();
		AxisBatch.Material = WorldAxisMaterial.get();
		AxisBatch.World = FMatrix::Identity;
		RenderOverlayMeshBatch(Renderer, Frame, View, Targets, AxisBatch, EMaterialPassType::EditorPrimitive);
	}

	if (NormalSettings.bVisible)
	{
		FEditorLinePassInputs LineInputs;
		AppendNormalVisualizationLines(LineInputs);
		if (FDebugLineRenderFeature* DebugLineFeature = Renderer.GetDebugLineFeature())
		{
			DebugLineFeature->Render(Renderer, Frame, View, Targets, LineInputs);
		}
	}
}

void FObjViewerEngine::RenderOverlayMeshBatch(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	const FSceneRenderTargets& Targets,
	const FMeshBatch& MeshBatch,
	EMaterialPassType PassType) const
{
	ID3D11Device* Device = Renderer.GetDevice();
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	FRenderMesh* Mesh = MeshBatch.Mesh;
	FMaterial* Material = MeshBatch.Material;
	if (!Device || !DeviceContext || !Mesh || !Material || !Targets.SceneColorRTV || !Targets.SceneDepthDSV)
	{
		return;
	}

	if (!Mesh->UpdateVertexAndIndexBuffer(Device, DeviceContext))
	{
		return;
	}

	BeginPass(Renderer, Targets.SceneColorRTV, Targets.SceneDepthDSV, View.Viewport, Frame, View);
	Material->Bind(DeviceContext, PassType);
	Renderer.GetRenderStateManager()->BindState(Material->GetRasterizerState());
	Renderer.GetRenderStateManager()->BindState(Material->GetDepthStencilState());
	Renderer.GetRenderStateManager()->BindState(Material->GetBlendState());

	if (!Material->HasPixelTextureBinding())
	{
		ID3D11SamplerState* DefaultSampler = Renderer.GetDefaultSampler();
		DeviceContext->PSSetSamplers(0, 1, &DefaultSampler);
	}

	Mesh->Bind(DeviceContext);
	DeviceContext->IASetPrimitiveTopology(ToD3DTopology(Mesh->Topology));
	Renderer.UpdateObjectConstantBuffer(MeshBatch.World);

	if (!Mesh->Indices.empty())
	{
		DeviceContext->DrawIndexed(static_cast<UINT>(Mesh->Indices.size()), 0, 0);
	}
	else
	{
		DeviceContext->Draw(static_cast<UINT>(Mesh->Vertices.size()), 0);
	}

	EndPass(Renderer, Targets.SceneColorRTV, Targets.SceneDepthDSV, View.Viewport, Frame, View);
}

void FObjViewerEngine::AppendNormalVisualizationDebugDraw()
{
	if (!NormalSettings.bVisible)
	{
		return;
	}

	FEditorLinePassInputs LineInputs;
	AppendNormalVisualizationLines(LineInputs);
	if (LineInputs.IsEmpty() || LineInputs.LineMesh == nullptr)
	{
		return;
	}

	UWorld* ActiveWorld = GetActiveWorld();
	if (!ActiveWorld)
	{
		return;
	}

	for (size_t VertexIndex = 0; VertexIndex + 1 < LineInputs.LineMesh->Vertices.size(); VertexIndex += 2)
	{
		const FVertex& StartVertex = LineInputs.LineMesh->Vertices[VertexIndex];
		const FVertex& EndVertex = LineInputs.LineMesh->Vertices[VertexIndex + 1];
		GetDebugDrawManager().DrawLine(ActiveWorld, StartVertex.Position, EndVertex.Position, StartVertex.Color);
	}
}

void FObjViewerEngine::AppendNormalVisualizationLines(FEditorLinePassInputs& LineInputs) const
{
	if (!NormalSettings.bVisible || !HasLoadedModel() || ModelState.Mesh == nullptr || ModelState.Mesh->GetRenderData() == nullptr)
	{
		return;
	}

	AStaticMeshActor* DisplayActor = ModelState.DisplayActor;
	if (DisplayActor == nullptr)
	{
		return;
	}

	USceneComponent* RootComponent = DisplayActor->GetRootComponent();
	if (RootComponent == nullptr)
	{
		return;
	}

	const FStaticMesh* RenderData = ModelState.Mesh->GetRenderData();
	const FMatrix& WorldMatrix = RootComponent->GetWorldTransform();
	const float NormalLength = (std::max)(GetDisplayedBoundsRadius(ModelState) * NormalSettings.LengthScale, 0.001f);
	const FVector4 NormalColor = GetNormalVisualizationColor(NormalSettings.Mode);

	if (NormalSettings.Mode == EObjViewerNormalVisualizationMode::Face)
	{
		for (size_t Index = 0; Index + 2 < RenderData->Indices.size(); Index += 3)
		{
			const uint32 Index0 = RenderData->Indices[Index];
			const uint32 Index1 = RenderData->Indices[Index + 1];
			const uint32 Index2 = RenderData->Indices[Index + 2];
			if (Index0 >= RenderData->Vertices.size() || Index1 >= RenderData->Vertices.size() || Index2 >= RenderData->Vertices.size())
			{
				continue;
			}

			const FVector WorldPosition0 = WorldMatrix.TransformPosition(RenderData->Vertices[Index0].Position);
			const FVector WorldPosition1 = WorldMatrix.TransformPosition(RenderData->Vertices[Index1].Position);
			const FVector WorldPosition2 = WorldMatrix.TransformPosition(RenderData->Vertices[Index2].Position);
			const FVector FaceNormal = FVector::CrossProduct(WorldPosition1 - WorldPosition0, WorldPosition2 - WorldPosition0).GetSafeNormal();
			if (FaceNormal.IsNearlyZero())
			{
				continue;
			}

			const FVector FaceCenter = (WorldPosition0 + WorldPosition1 + WorldPosition2) / 3.0f;
			FDebugLineRenderFeature::AppendLine(
				LineInputs,
				FaceCenter,
				FaceCenter + (FaceNormal * NormalLength),
				NormalColor);
		}

		return;
	}

	for (const FVertex& Vertex : RenderData->Vertices)
	{
		if (Vertex.Normal.IsNearlyZero())
		{
			continue;
		}

		const FVector WorldStart = WorldMatrix.TransformPosition(Vertex.Position);
		const FVector WorldNormal = WorldMatrix.TransformVector(Vertex.Normal).GetSafeNormal();
		if (WorldNormal.IsNearlyZero())
		{
			continue;
		}

		FDebugLineRenderFeature::AppendLine(
			LineInputs,
			WorldStart,
			WorldStart + (WorldNormal * NormalLength),
			NormalColor);
	}
}

void FObjViewerEngine::AppendGridMeshBatch(FGameFrameRequest& Request) const
{
	const FMatrix ViewInverse = Request.SceneView.ViewMatrix.GetInverse();
	const FVector4 GridAxisU = FVector4(FVector::ForwardVector, 0.0f);
	const FVector4 GridAxisV = FVector4(FVector::RightVector, 0.0f);
	const FVector4 ViewForward = FVector4(ViewInverse.GetForwardVector().GetSafeNormal(), 0.0f);

	if (GridSettings.bVisible && GridMesh && GridMaterial)
	{
		GridMaterial->SetParameterData("GridSize", &GridSettings.GridSize, 4);
		GridMaterial->SetParameterData("LineThickness", &GridSettings.LineThickness, 4);
		GridMaterial->SetParameterData("GridAxisU", &GridAxisU, sizeof(FVector4));
		GridMaterial->SetParameterData("GridAxisV", &GridAxisV, sizeof(FVector4));
		GridMaterial->SetParameterData("ViewForward", &ViewForward, sizeof(FVector4));

		FMeshBatch GridBatch;
		GridBatch.Mesh = GridMesh.get();
		GridBatch.Material = GridMaterial.get();
		GridBatch.World = FMatrix::Identity;
		GridBatch.Domain = EMaterialDomain::EditorGrid;
		GridBatch.PassMask = static_cast<uint32>(EMeshPassMask::EditorGrid);
		GridBatch.bDisableDepthWrite = true;
		GridBatch.bDisableCulling = true;
		GridBatch.IndexStart = 0;
		GridBatch.IndexCount = static_cast<uint32>(GridMesh->Indices.size());
		Request.AdditionalMeshBatches.push_back(GridBatch);
	}

	if (GridSettings.bShowWorldAxis && WorldAxisMesh && WorldAxisMaterial)
	{
		WorldAxisMaterial->SetParameterData("GridSize", &GridSettings.GridSize, 4);
		WorldAxisMaterial->SetParameterData("LineThickness", &GridSettings.LineThickness, 4);
		WorldAxisMaterial->SetParameterData("GridAxisU", &GridAxisU, sizeof(FVector4));
		WorldAxisMaterial->SetParameterData("GridAxisV", &GridAxisV, sizeof(FVector4));
		WorldAxisMaterial->SetParameterData("ViewForward", &ViewForward, sizeof(FVector4));

		FMeshBatch WorldAxisBatch;
		WorldAxisBatch.Mesh = WorldAxisMesh.get();
		WorldAxisBatch.Material = WorldAxisMaterial.get();
		WorldAxisBatch.World = FMatrix::Identity;
		WorldAxisBatch.Domain = EMaterialDomain::EditorPrimitive;
		WorldAxisBatch.PassMask = static_cast<uint32>(EMeshPassMask::EditorPrimitive);
		WorldAxisBatch.bDisableDepthWrite = true;
		WorldAxisBatch.bDisableDepthTest = false;
		WorldAxisBatch.bDisableCulling = true;
		WorldAxisBatch.IndexStart = 0;
		WorldAxisBatch.IndexCount = static_cast<uint32>(WorldAxisMesh->Indices.size());
		Request.AdditionalMeshBatches.push_back(WorldAxisBatch);
	}
}

void FObjViewerEngine::UpdateLoadedModelState(
	const FString& FilePath,
	const FObjImportSummary& ImportOptions,
	UStaticMesh* Mesh,
	AStaticMeshActor* DisplayActor)
{
	ModelState = {};
	ModelState.bLoaded = true;
	ModelState.SourceFilePath = FilePath;
	ModelState.DisplayActor = DisplayActor;
	ModelState.Mesh = Mesh;
	ModelState.LastImportSummary = ImportOptions;

	const std::filesystem::path SourcePath(FPaths::ToWide(FilePath));
	ModelState.FileName = WideToUtf8(SourcePath.filename().wstring());

	std::error_code ErrorCode;
	ModelState.FileSizeBytes = std::filesystem::file_size(SourcePath, ErrorCode);
	if (ErrorCode)
	{
		ModelState.FileSizeBytes = 0;
	}

	if (Mesh && Mesh->GetRenderData())
	{
		FStaticMesh* RenderData = Mesh->GetRenderData();
		ModelState.VertexCount = static_cast<int32>(RenderData->Vertices.size());
		ModelState.IndexCount = static_cast<int32>(RenderData->Indices.size());
		ModelState.TriangleCount = static_cast<int32>(RenderData->Indices.size() / 3);
		ModelState.SectionCount = RenderData->GetNumSection();
		ModelState.LodCount = static_cast<int32>(Mesh->GetLodCount());
		ModelState.bHasUV = MeshHasAnyUVs(Mesh);
	}

	if (Mesh)
	{
		ModelState.BoundsCenter = Mesh->LocalBounds.Center;
		ModelState.BoundsRadius = Mesh->LocalBounds.Radius;
		ModelState.BoundsExtent = Mesh->LocalBounds.BoxExtent;
	}

	ModelState.bLodEnabled = ImportOptions.bEnableLOD;
}

void FObjViewerEngine::ProcessLaunchOptions()
{
	if (LaunchOptions.InputFilePath.empty())
	{
		return;
	}

	bool bSucceeded = true;
	if (!LaunchOptions.ExportModelPath.empty())
	{
		bSucceeded = LoadModelFromFile(LaunchOptions.InputFilePath, "Command Line");
		if (bSucceeded)
		{
			bSucceeded = ExportLoadedModelAsModel(LaunchOptions.ExportModelPath);
		}
	}
	else if (ViewerShell)
	{
		ViewerShell->RequestImportDialog(LaunchOptions.InputFilePath, "Open in ObjViewer");
	}
	else
	{
		bSucceeded = LoadModelFromFile(LaunchOptions.InputFilePath, "Command Line");
	}

	LaunchOptions.InputFilePath.clear();
	if (LaunchOptions.bCloseWhenDone)
	{
		::PostQuitMessage(bSucceeded ? 0 : 1);
	}
}
