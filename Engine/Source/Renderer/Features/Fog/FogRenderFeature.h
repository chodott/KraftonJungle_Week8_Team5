
#pragma once

#include "CoreMinimal.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Features/Fog/FogTypes.h"

#include <d3d11.h>

class FRenderer;

class ENGINE_API FFogRenderFeature {
public:
  ~FFogRenderFeature();

  bool Render(FRenderer &Renderer, const FFrameContext &Frame,
              const FViewContext &View, const FSceneRenderTargets &Targets,
              const TArray<FFogRenderItem> &Items);
  void Release();

private:
  bool Initialize(FRenderer &Renderer);
  bool UpdateFogCompositeConstantBuffer(FRenderer &Renderer,
                                        const FViewContext &View,
                                        uint32 TotalFogCount,
                                        uint32 GlobalFogCount,
                                        uint32 LocalFogCount);
  bool UpdateFogClusterConstantBuffer(FRenderer &Renderer,
                                      const FViewContext &View);
  bool BuildFogClusters(const FViewContext &View,
                        const TArray<FFogRenderItem> &Items);
  bool UploadGlobalFogStructuredBuffer(FRenderer &Renderer);
  bool UploadLocalFogStructuredBuffer(FRenderer &Renderer);
  bool UploadClusterHeaderStructuredBuffer(FRenderer &Renderer);
  bool UploadClusterIndexStructuredBuffer(FRenderer &Renderer);

private:
  ID3D11Buffer *FogCompositeConstantBuffer = nullptr;
  ID3D11Buffer *FogClusterConstantBuffer = nullptr;
  ID3D11DepthStencilState *NoDepthState = nullptr;
  ID3D11RasterizerState *FogRasterizerState = nullptr;
  ID3D11SamplerState *LinearSampler = nullptr;
  ID3D11SamplerState *DepthSampler = nullptr;
  ID3D11VertexShader *FogPostVS = nullptr;
  ID3D11PixelShader *FogPostPS = nullptr;

  ID3D11Buffer *LocalFogStructuredBuffer = nullptr;
  ID3D11ShaderResourceView *LocalFogStructuredBufferSRV = nullptr;
  ID3D11Buffer *GlobalFogStructuredBuffer = nullptr;
  ID3D11ShaderResourceView *GlobalFogStructuredBufferSRV = nullptr;
  ID3D11Buffer *ClusterHeaderStructuredBuffer = nullptr;
  ID3D11ShaderResourceView *ClusterHeaderStructuredBufferSRV = nullptr;
  ID3D11Buffer *ClusterIndexStructuredBuffer = nullptr;
  ID3D11ShaderResourceView *ClusterIndexStructuredBufferSRV = nullptr;

  TArray<FFogRenderItem> PreparedFogItems;
  TArray<FFogRenderItem> PreparedGlobalFogItems;
  TArray<FFogRenderItem> PreparedLocalFogItems;
  TArray<uint32> ClusterHeadersCPU;
  TArray<uint32> ClusterIndexListCPU;
};
