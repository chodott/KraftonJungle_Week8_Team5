#pragma once

#include "CoreMinimal.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/Common/RenderFrameContext.h"
#include "Renderer/UI/Screen/UIDrawList.h"

#include <memory>

class FRenderer;
class FScreenUIPassBuilder;
class FScreenUIBatchRenderer;

struct ENGINE_API FUIBatchCommand
{
    FDynamicMesh* Mesh = nullptr;
    FMaterial* Material = nullptr;
    int32 Layer = 0;
    float Depth = 0.0f;
    int32 DepthSortKey = 0;
};

struct ENGINE_API FUIRenderBatch
{
    TArray<FUIBatchCommand> Commands;
    FMatrix ViewMatrix = FMatrix::Identity;
    FMatrix ProjectionMatrix = FMatrix::Identity;

    void Clear()
    {
        Commands.clear();
        ViewMatrix = FMatrix::Identity;
        ProjectionMatrix = FMatrix::Identity;
    }

    void Reserve(size_t Count)
    {
        Commands.reserve(Count);
    }
};

struct ENGINE_API FScreenUIPassInputs
{
    FScreenUIPassInputs() = default;
    FScreenUIPassInputs(const FScreenUIPassInputs&) = delete;
    FScreenUIPassInputs& operator=(const FScreenUIPassInputs&) = delete;
    FScreenUIPassInputs(FScreenUIPassInputs&&) noexcept = default;
    FScreenUIPassInputs& operator=(FScreenUIPassInputs&&) noexcept = default;

    FUIRenderBatch Batch;
    FViewContext View;
    TArray<std::unique_ptr<FDynamicMesh>> MeshStorage;

    void Clear()
    {
        Batch.Clear();
        View = {};
        MeshStorage.clear();
    }

    bool IsEmpty() const
    {
        return Batch.Commands.empty();
    }

    void Reserve(size_t Count)
    {
        Batch.Reserve(Count);
        MeshStorage.reserve(Count);
    }
};

class ENGINE_API FScreenUIRenderer
{
public:
    FScreenUIRenderer();
    ~FScreenUIRenderer();

    bool BuildPassInputs(
        FRenderer& Renderer,
        const FUIDrawList& DrawList,
        const D3D11_VIEWPORT& OutputViewport,
        FScreenUIPassInputs& OutPassInputs);

    bool Render(
        FRenderer& Renderer,
        const FFrameContext& Frame,
        const FScreenUIPassInputs& PassInputs,
        ID3D11RenderTargetView* RenderTargetView,
        ID3D11DepthStencilView* DepthStencilView);

private:
    std::unique_ptr<FScreenUIPassBuilder> PassBuilder;
    std::unique_ptr<FScreenUIBatchRenderer> BatchRenderer;
};
