#pragma once

#include "CoreMinimal.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/UI/Screen/ScreenUIRenderer.h"

class FRenderer;

class ENGINE_API FScreenUIPassBuilder
{
public:
    bool BuildPassInputs(
        FRenderer& Renderer,
        const FUIDrawList& DrawList,
        const D3D11_VIEWPORT& OutputViewport,
        FScreenUIPassInputs& OutPassInputs);

private:
    FDynamicMesh* CreateFrameMesh(FScreenUIPassInputs& PassInputs, EMeshTopology Topology);
    FDynamicMaterial* GetOrCreateColorMaterial(FRenderer& Renderer);
    FDynamicMaterial* GetOrCreateFontMaterial(FRenderer& Renderer, uint32 Color);
    void EnqueueMesh(FScreenUIPassInputs& PassInputs, FDynamicMesh* Mesh, FMaterial* Material, int32 Layer, float Depth);
    void AppendFilledRect(FScreenUIPassInputs& PassInputs, const FUIDrawElement& Element);
    void AppendRectOutline(FScreenUIPassInputs& PassInputs, const FUIDrawElement& Element);
    void AppendText(FRenderer& Renderer, FScreenUIPassInputs& PassInputs, const FUIDrawElement& Element);
    void ApplyOrthoProjection(int32 Width, int32 Height, const D3D11_VIEWPORT& OutputViewport, FScreenUIPassInputs& OutPassInputs);

private:
    std::unique_ptr<FDynamicMaterial> UIColorMaterial;
    TMap<uint32, std::unique_ptr<FDynamicMaterial>> FontMaterialByColor;
};
