#pragma once

#include "CoreMinimal.h"
#include "Core/ShowFlags.h"
#include "Level/SceneRenderPacket.h"
#include "Renderer/Feature/DebugLineRenderFeature.h"
#include "Renderer/Feature/OutlineRenderFeature.h"
#include "Renderer/Feature/SubUVRenderFeature.h"
#include "Renderer/Feature/TextRenderFeature.h"
#include "Renderer/Feature/BillboardRenderFeature.h"
#include "Renderer/Feature/DecalRenderFeature.h"
#include "Renderer/Feature/FXAARenderFeature.h"
#include "Renderer/MeshBatch.h"
#include "Renderer/RenderDevice.h"
#include "Renderer/RenderFeatureInterfaces.h"
#include "Renderer/RenderFrameContext.h"
#include "Renderer/RenderStateManager.h"
#include "Renderer/SceneRenderTargets.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/ScreenUIRenderer.h"
#include "Renderer/UIDrawList.h"
#include "Renderer/ViewportCompositor.h"
#include "ShaderManager.h"

#include <d3d11.h>
#include <filesystem>
#include <memory>

#include "Feature/FireballRenderFeature.h"

struct FVertex;
struct FRenderMesh;
class FPixelShader;
class FMaterial;
class ULevel;
class UWorld;
class AActor;
class FFogRenderFeature;
class FDebugDrawManager;

struct FSceneViewRenderRequest
{
	FMatrix ViewMatrix = FMatrix::Identity;
	FMatrix ProjectionMatrix = FMatrix::Identity;
	FVector CameraPosition = FVector::ZeroVector;
	float TotalTimeSeconds = 0.0f;
	float NearZ = 0.1f;
	float FarZ = 1000.0f;
};

struct FDebugSceneBuildInputs
{
	const FDebugDrawManager* DrawManager = nullptr;
	UWorld* World = nullptr;
	AActor* BoundsActor = nullptr;
	FShowFlags ShowFlags = {};
};

struct FGameFrameRequest
{
	FSceneRenderPacket ScenePacket;
	FSceneViewRenderRequest SceneView;
	TArray<FMeshBatch> AdditionalMeshBatches;
	FDebugSceneBuildInputs DebugInputs;
	EViewportCompositeMode CompositeMode = EViewportCompositeMode::SceneColor;
	bool bForceWireframe = false;
	FMaterial* WireframeMaterial = nullptr;
	float ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
};

struct FViewportScenePassRequest
{
	ID3D11RenderTargetView* RenderTargetView = nullptr;
	ID3D11ShaderResourceView* RenderTargetShaderResourceView = nullptr;
	ID3D11DepthStencilView* DepthStencilView = nullptr;
	ID3D11ShaderResourceView* DepthShaderResourceView = nullptr;
	D3D11_VIEWPORT Viewport = {};
	FSceneRenderPacket ScenePacket;
	FSceneViewRenderRequest SceneView;
	TArray<FMeshBatch> AdditionalMeshBatches;
	FOutlineRenderRequest OutlineRequest;
	FDebugSceneBuildInputs DebugInputs;
	bool bForceWireframe = false;
	FMaterial* WireframeMaterial = nullptr;
	float ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };

	bool IsValid() const
	{
		return RenderTargetView != nullptr
			&& DepthStencilView != nullptr
			&& RenderTargetShaderResourceView != nullptr
			&& DepthShaderResourceView != nullptr
			&& Viewport.Width > 0.0f
			&& Viewport.Height > 0.0f;
	}
};

struct FEditorFrameRequest
{
	TArray<FViewportScenePassRequest> ScenePasses;
	TArray<FViewportCompositeItem> CompositeItems;
	FUIDrawList ScreenDrawList;
};

class ENGINE_API FRenderer
{
public:
	FRenderer(HWND InHwnd, int32 InWidth, int32 InHeight);
	~FRenderer();

	bool Initialize(HWND InHwnd, int32 InWidth, int32 InHeight);
	void BeginFrame();
	void EndFrame();
	void Release();
	bool IsOccluded();
	void OnResize(int32 NewWidth, int32 NewHeight);

	void SetVSync(bool bEnable) { RenderDevice.SetVSync(bEnable); }
	bool IsVSyncEnabled() const { return RenderDevice.IsVSyncEnabled(); }

	bool RenderScreenUIPass(
		const FScreenUIPassInputs& PassInputs,
		const FFrameContext& Frame,
		ID3D11RenderTargetView* RenderTargetView,
		ID3D11DepthStencilView* DepthStencilView = nullptr);
	bool ComposeViewports(
		const FViewportCompositePassInputs& Inputs,
		const FFrameContext& Frame,
		const FViewContext& View,
		ID3D11RenderTargetView* RenderTargetView,
		ID3D11DepthStencilView* DepthStencilView = nullptr);
	bool RenderGameFrame(const FGameFrameRequest& Request);
	bool RenderEditorFrame(const FEditorFrameRequest& Request);

	bool CreateTextureFromSTB(ID3D11Device* Device, const char* FilePath, ID3D11ShaderResourceView** OutSRV);
	bool CreateTextureFromSTB(ID3D11Device* Device, const std::filesystem::path& FilePath, ID3D11ShaderResourceView** OutSRV);

	void ConfigureMaterialPasses(FMaterial& Material, bool bTexturedMaterial);

	FMaterial* GetDefaultMaterial() const { return DefaultMaterial.get(); }
	FMaterial* GetDefaultTextureMaterial() const { return DefaultTextureMaterial.get(); }
	size_t GetPrevCommandCount() const;
	std::unique_ptr<FRenderStateManager>& GetRenderStateManager() { return RenderStateManager; }
	ID3D11Device* GetDevice() const { return RenderDevice.GetDevice(); }
	ID3D11DeviceContext* GetDeviceContext() const { return RenderDevice.GetDeviceContext(); }
	ID3D11RenderTargetView* GetRenderTargetView() const { return RenderDevice.GetRenderTargetView(); }
	IDXGISwapChain* GetSwapChain() const { return RenderDevice.GetSwapChain(); }
	HWND GetHwnd() const { return RenderDevice.GetHwnd(); }
	const D3D11_VIEWPORT& GetBackBufferViewport() const { return RenderDevice.GetViewport(); }

	ISceneTextFeature* GetSceneTextFeature() const { return TextFeature.get(); }
	ISceneSubUVFeature* GetSceneSubUVFeature() const { return SubUVFeature.get(); }
	ISceneBillboardFeature* GetSceneBillboardFeature() const { return BillboardFeature.get(); }
	FFogRenderFeature* GetFogFeature() const { return FogFeature.get(); }
	FOutlineRenderFeature* GetOutlineFeature() const { return OutlineFeature.get(); }
	FDebugLineRenderFeature* GetDebugLineFeature() const { return DebugLineFeature.get(); }
	FDecalRenderFeature* GetDecalFeature() const { return DecalFeature.get(); }
	FFireBallRenderFeature* GetFireBallFeature() const { return FireBallFeature.get(); }
	FFXAARenderFeature* GetFXAAFeature() const { return FXAAFeature.get(); }
	FSceneRenderer& GetSceneRenderer() { return SceneRenderer; }
	FScreenUIRenderer& GetScreenUIRenderer() { return ScreenUIRenderer; }
	FRenderDevice& GetRenderDevice() { return RenderDevice; }
	FBillboardRenderer& GetBillboardRenderer() { return BillboardFeature->GetRenderer(); }
	const FDecalFrameStats& GetDecalFrameStats() const;
	ID3D11SamplerState* GetDefaultSampler() const { return NormalSampler; }

	void SetConstantBuffers();
	void UpdateFrameConstantBuffer(const FFrameContext& Frame, const FViewContext& View);
	void UpdateObjectConstantBuffer(const FMatrix& WorldMatrix);
	void ClearDepthBuffer(ID3D11DepthStencilView* DepthStencilView);

	ID3D11ShaderResourceView* GetFolderIconSRV() const { return FolderIconSRV; }
	ID3D11ShaderResourceView* GetFileIconSRV() const { return FileIconSRV; }

public:
	FShaderManager ShaderManager;

private:
	friend class FSceneRenderer;
	friend class FFogRenderFeature;
	friend class FOutlineRenderFeature;
	friend class FDebugLineRenderFeature;
	friend class FDecalRenderFeature;
	friend class FScreenUIRenderer;
	friend class FBillboardRenderFeature;
	bool CreateConstantBuffers();
	bool CreateSamplers();

	FFrameContext BuildFrameContext(float TotalTimeSeconds) const;
	FViewContext BuildViewContext(const FSceneViewRenderRequest& SceneView, const D3D11_VIEWPORT& Viewport) const;

	bool EnsureGameSceneTargets(uint32 Width, uint32 Height);
	bool EnsureSupplementalTargets(uint32 Width, uint32 Height);
	bool AcquireGameSceneTargets(FSceneRenderTargets& OutTargets);
	bool WrapExternalSceneTargets(
		ID3D11RenderTargetView* RenderTargetView,
		ID3D11ShaderResourceView* RenderTargetShaderResourceView,
		ID3D11DepthStencilView* DepthStencilView,
		ID3D11ShaderResourceView* DepthShaderResourceView,
		const D3D11_VIEWPORT& Viewport,
		FSceneRenderTargets& OutTargets);
	void ReleaseSceneTargets();

	bool CreateSolidColorTextureSRV(ID3D11Device* Device, uint32 PackedRGBA, ID3D11ShaderResourceView** OutSRV);
	ID3D11ShaderResourceView* GetOrLoadDecalBaseColorTexture(const std::wstring& TexturePath);
	ID3D11ShaderResourceView* ResolveDecalBaseColorTexture(const FSceneRenderPacket& ScenePacket, std::wstring& OutResolvedTexturePath);
	void ResolveDecalBaseColorTextureArray(FSceneViewData& InOutSceneViewData);

private:
	std::unique_ptr<FRenderStateManager> RenderStateManager = nullptr;

	FRenderDevice RenderDevice;

	ID3D11Buffer* FrameConstantBuffer = nullptr;
	ID3D11Buffer* ObjectConstantBuffer = nullptr;

	std::shared_ptr<FMaterial> DefaultMaterial;
	std::shared_ptr<FMaterial> DefaultTextureMaterial;

	FSceneRenderer SceneRenderer;
	FViewportCompositor ViewportCompositor;
	FScreenUIRenderer ScreenUIRenderer;
	std::unique_ptr<FTextRenderFeature> TextFeature;
	std::unique_ptr<FSubUVRenderFeature> SubUVFeature;
	std::unique_ptr<FBillboardRenderFeature> BillboardFeature;
	std::unique_ptr<FFogRenderFeature> FogFeature;
	std::unique_ptr<FOutlineRenderFeature> OutlineFeature;
	std::unique_ptr<FDebugLineRenderFeature> DebugLineFeature;
	std::unique_ptr<FDecalRenderFeature> DecalFeature;
	std::unique_ptr<FFireBallRenderFeature> FireBallFeature;
	std::unique_ptr<FFXAARenderFeature> FXAAFeature;

	ID3D11ShaderResourceView* FolderIconSRV = nullptr;
	ID3D11ShaderResourceView* FileIconSRV = nullptr;
	ID3D11ShaderResourceView* DecalFallbackBaseColorSRV = nullptr;
	ID3D11SamplerState* NormalSampler = nullptr;
	TMap<std::wstring, ID3D11ShaderResourceView*> DecalBaseColorTextureCache;

	// Texture2DArray for clustered decal rendering (rebuilt when path set changes)
	ID3D11Texture2D* DecalBaseColorTextureArrayResource = nullptr;
	ID3D11ShaderResourceView* DecalBaseColorTextureArraySRV = nullptr;
	TArray<std::wstring> DecalBaseColorTextureArrayPaths;

	ID3D11Texture2D* GameSceneColorTexture = nullptr;
	ID3D11RenderTargetView* GameSceneColorRTV = nullptr;
	ID3D11ShaderResourceView* GameSceneColorSRV = nullptr;
	ID3D11Texture2D* GameSceneDepthTexture = nullptr;
	ID3D11DepthStencilView* GameSceneDepthDSV = nullptr;
	ID3D11ShaderResourceView* GameSceneDepthSRV = nullptr;

	ID3D11Texture2D* GBufferATexture = nullptr;
	ID3D11RenderTargetView* GBufferARTV = nullptr;
	ID3D11ShaderResourceView* GBufferASRV = nullptr;
	ID3D11Texture2D* GBufferBTexture = nullptr;
	ID3D11RenderTargetView* GBufferBRTV = nullptr;
	ID3D11ShaderResourceView* GBufferBSRV = nullptr;
	ID3D11Texture2D* GBufferCTexture = nullptr;
	ID3D11RenderTargetView* GBufferCRTV = nullptr;
	ID3D11ShaderResourceView* GBufferCSRV = nullptr;
	ID3D11Texture2D* SceneColorScratchTexture = nullptr;
	ID3D11RenderTargetView* SceneColorScratchRTV = nullptr;
	ID3D11ShaderResourceView* SceneColorScratchSRV = nullptr;
	ID3D11Texture2D* OutlineMaskTexture = nullptr;
	ID3D11RenderTargetView* OutlineMaskRTV = nullptr;
	ID3D11ShaderResourceView* OutlineMaskSRV = nullptr;

	uint32 GameSceneTargetCacheWidth = 0;
	uint32 GameSceneTargetCacheHeight = 0;
	uint32 SupplementalTargetCacheWidth = 0;
	uint32 SupplementalTargetCacheHeight = 0;
};
