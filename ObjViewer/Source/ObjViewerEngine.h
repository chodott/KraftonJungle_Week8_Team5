#pragma once

#include "Asset/StaticMeshLODBuilder.h"
#include "Asset/ObjManager.h"
#include "Core/Engine.h"

struct FGameFrameRequest;

class AStaticMeshActor;
class FMaterial;
class FObjViewerViewportSurface;
class FObjViewerShell;
class FObjViewerViewportClient;
class FWindowsWindow;
class UStaticMesh;
class USceneComponent;
struct FDynamicMesh;
struct FEditorLinePassInputs;
struct FSceneRenderTargets;
enum class EMaterialPassType : uint8;

enum class EObjImportPreset : uint8_t
{
	Auto,
	Custom,
	Blender,
	Maya,
	ThreeDSMax,
	Unreal,
	Unity
};

struct FObjImportSummary
{
	FString ImportSource = "Unknown";
	EObjImportPreset SourcePreset = EObjImportPreset::Auto;
	EObjImportAxis ForwardAxis = EObjImportAxis::PosX;
	EObjImportAxis UpAxis = EObjImportAxis::PosZ;
	bool bReplaceCurrentModel = true;
	bool bCenterToOrigin = true;
	bool bPlaceOnGround = false;
	bool bFrameCameraAfterImport = true;
	bool bEnableLOD = true;
	float UniformScale = 1.0f;
};

struct FObjViewerModelState
{
	bool bLoaded = false;

	FString SourceFilePath;
	FString FileName;

	uint64 FileSizeBytes = 0;
	AStaticMeshActor* DisplayActor = nullptr;
	UStaticMesh* Mesh = nullptr;

	int32 VertexCount = 0;
	int32 IndexCount = 0;
	int32 TriangleCount = 0;
	int32 SectionCount = 0;
	int32 LodCount = 0;
	bool bHasUV = false;
	bool bLodEnabled = true;

	FVector BoundsCenter = FVector::ZeroVector;
	float BoundsRadius = 0.0f;
	FVector BoundsExtent = FVector::ZeroVector;

	FObjImportSummary LastImportSummary;
};

struct FObjViewerGridSettings
{
	bool bVisible = true;
	bool bShowWorldAxis = true;
	float GridSize = 10.0f;
	float LineThickness = 1.0f;
};

enum class EObjViewerNormalVisualizationMode : uint8_t
{
	Vertex,
	Face
};

struct FObjViewerNormalSettings
{
	bool bVisible = false;
	EObjViewerNormalVisualizationMode Mode = EObjViewerNormalVisualizationMode::Vertex;
	float LengthScale = 0.05f;
};

struct FObjViewerLODBuilderSettings
{
	int32 NumLODs = 3;
	float TriangleReductionStep = 0.5f;
	float DistanceStep = 8.0f;
};

struct FObjViewerLaunchOptions
{
	FString InputFilePath;
	FString ExportModelPath;
	bool bCloseWhenDone = false;
};

class FObjViewerEngine : public FEngine
{
public:
	explicit FObjViewerEngine(const FObjViewerLaunchOptions& InLaunchOptions = {});
	~FObjViewerEngine() override;

	ULevel* GetActiveScene() const override;
	UWorld* GetActiveWorld() const override;
	const FWorldContext* GetActiveWorldContext() const override;

	bool LoadModelFromFile(const FString& FilePath, const FObjImportSummary& ImportOptions);
	bool LoadModelFromFile(const FString& FilePath, const FString& ImportSource = "Unknown");
	bool ExportLoadedModelAsModel(const FString& FilePath) const;
	bool GenerateLoadedModelLODs();
	bool DeleteLoadedModelLODs();
	bool ReloadLoadedModel();
	void ClearLoadedModel();
	void FrameLoadedModel();
	void ResetViewerCamera();

	bool HasLoadedModel() const { return ModelState.bLoaded && ModelState.Mesh != nullptr; }
	const FObjViewerModelState& GetModelState() const { return ModelState; }
	const FObjViewerGridSettings& GetGridSettings() const { return GridSettings; }
	FObjViewerGridSettings& GetMutableGridSettings() { return GridSettings; }
	const FObjViewerNormalSettings& GetNormalSettings() const { return NormalSettings; }
	FObjViewerNormalSettings& GetMutableNormalSettings() { return NormalSettings; }
	const FObjViewerLODBuilderSettings& GetLODBuilderSettings() const { return LODBuilderSettings; }
	FObjViewerLODBuilderSettings& GetMutableLODBuilderSettings() { return LODBuilderSettings; }
	bool IsWireframeEnabled() const { return bWireframeEnabled; }
	void SetWireframeEnabled(bool bEnabled) { bWireframeEnabled = bEnabled; }
	bool IsLoadedModelLODEnabled() const { return ModelState.bLodEnabled; }
	void SetLoadedModelLODEnabled(bool bEnabled);
	void SetLoadedModelLodDistance(int32 LODIndex, float Distance);
	float GetLoadedModelLodDistance(int32 LODIndex) const;
	int32 GetLoadedModelLodDistanceCount() const;
	int32 GetLoadedModelCurrentLODIndex() const;
	float GetLoadedModelCurrentLODDistance() const;
	const FString& GetLastOperationStatus() const { return LastOperationStatus; }
	FObjViewerShell& GetShell() const;

protected:
	void BindHost(FWindowsWindow* InMainWindow) override;
	bool InitializeWorlds() override;
	bool InitializeMode() override;
	void FinalizeInitialize() override;
	std::unique_ptr<IViewportClient> CreateViewportClient() override;
	void TickWorlds(float DeltaTime) override;
	void RenderFrame() override;

private:
	void InitializeViewerCamera() const;
	void CreateGridResources();
	void CreateAxisResources();
	void RenderViewportOverlays(
		FRenderer& Renderer,
		const FObjViewerViewportSurface& Surface,
		const FSceneViewRenderRequest& SceneView) const;
	void RenderOverlayMeshBatch(
		FRenderer& Renderer,
		const FFrameContext& Frame,
		const FViewContext& View,
		const FSceneRenderTargets& Targets,
		const FMeshBatch& MeshBatch,
		EMaterialPassType PassType) const;
	void ApplyWireframeOverride(FGameFrameRequest& Request) const;
	void AppendNormalVisualizationDebugDraw();
	void AppendNormalVisualizationLines(FEditorLinePassInputs& LineInputs) const;
	void AppendGridMeshBatch(FGameFrameRequest& Request) const;
	void UpdateLoadedModelState(
		const FString& FilePath,
		const FObjImportSummary& ImportOptions,
		UStaticMesh* Mesh,
		AStaticMeshActor* DisplayActor);
	void ProcessLaunchOptions();

	FWorldContext* ViewerWorldContext = nullptr;
	FWindowsWindow* MainWindow = nullptr;
	FObjViewerLaunchOptions LaunchOptions;
	FObjViewerModelState ModelState;
	FObjViewerGridSettings GridSettings;
	FObjViewerNormalSettings NormalSettings;
	FObjViewerLODBuilderSettings LODBuilderSettings;
	std::unique_ptr<FDynamicMesh> GridMesh;
	std::shared_ptr<FMaterial> GridMaterial;
	std::unique_ptr<FDynamicMesh> WorldAxisMesh;
	std::shared_ptr<FMaterial> WorldAxisMaterial;
	bool bWireframeEnabled = false;
	FString LastOperationStatus;
	const FString WireframeMaterialName = "M_Wireframe";
	std::shared_ptr<FMaterial> WireframeMaterial = nullptr;
	std::unique_ptr<FObjViewerShell> ViewerShell;
};
