#include "Renderer/UI/Screen/ScreenUIPassBuilder.h"

#include "Renderer/Common/RenderFeatureInterfaces.h"
#include "Renderer/Mesh/Vertex.h"
#include "Renderer/GraphicsCore/RenderStateManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/UI/Screen/ScreenUIRendererUtils.h"

#include <algorithm>

using namespace ScreenUIRendererUtils;

FDynamicMesh* FScreenUIPassBuilder::CreateFrameMesh(FScreenUIPassInputs& PassInputs, EMeshTopology Topology)
{
    auto Mesh = std::make_unique<FDynamicMesh>();
    Mesh->Topology = Topology;
    Mesh->bIsDirty = true;

    FDynamicMesh* RawMesh = Mesh.get();
    PassInputs.MeshStorage.push_back(std::move(Mesh));
    return RawMesh;
}

FDynamicMaterial* FScreenUIPassBuilder::GetOrCreateColorMaterial(FRenderer& Renderer)
{
    if (UIColorMaterial)
    {
        return UIColorMaterial.get();
    }

    if (!Renderer.GetDefaultMaterial())
    {
        return nullptr;
    }

    UIColorMaterial = Renderer.GetDefaultMaterial()->CreateDynamicMaterial();
    if (!UIColorMaterial)
    {
        return nullptr;
    }

    const FVector4 White(1.0f, 1.0f, 1.0f, 1.0f);
    UIColorMaterial->SetVectorParameter("BaseColor", White);

    FDepthStencilStateOption DepthOpt = UIColorMaterial->GetDepthStencilOption();
    DepthOpt.DepthEnable = false;
    DepthOpt.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    auto DSS = Renderer.GetRenderStateManager()->GetOrCreateDepthStencilState(DepthOpt);
    UIColorMaterial->SetDepthStencilOption(DepthOpt);
    UIColorMaterial->SetDepthStencilState(DSS);

    return UIColorMaterial.get();
}

FDynamicMaterial* FScreenUIPassBuilder::GetOrCreateFontMaterial(FRenderer& Renderer, uint32 Color)
{
    auto Found = FontMaterialByColor.find(Color);
    if (Found != FontMaterialByColor.end())
    {
        return Found->second.get();
    }

    ISceneTextFeature* TextFeature = Renderer.GetSceneTextFeature();
    if (!TextFeature)
    {
        return nullptr;
    }

    FMaterial* FontMaterial = TextFeature->GetBaseMaterial();
    if (!FontMaterial)
    {
        return nullptr;
    }

    auto Material = FontMaterial->CreateDynamicMaterial();
    if (!Material)
    {
        return nullptr;
    }

    Material->SetVectorParameter("TextColor", ToDisplayColor(Color));

    FDepthStencilStateOption DepthOpt = Material->GetDepthStencilOption();
    DepthOpt.DepthEnable = false;
    DepthOpt.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    auto DSS = Renderer.GetRenderStateManager()->GetOrCreateDepthStencilState(DepthOpt);
    Material->SetDepthStencilOption(DepthOpt);
    Material->SetDepthStencilState(DSS);

    FDynamicMaterial* Raw = Material.get();
    FontMaterialByColor[Color] = std::move(Material);
    return Raw;
}

void FScreenUIPassBuilder::EnqueueMesh(
    FScreenUIPassInputs& PassInputs,
    FDynamicMesh* Mesh,
    FMaterial* Material,
    int32 Layer,
    float Depth)
{
    if (!Mesh || !Material || Mesh->Vertices.empty())
    {
        return;
    }

    const int32 DepthSortKey = MakeDepthSortKey(Depth);

    if (!PassInputs.Batch.Commands.empty())
    {
        FUIBatchCommand& Last = PassInputs.Batch.Commands.back();
        if (Last.Mesh && Last.Material == Material
            && Last.Layer == Layer
            && Last.DepthSortKey == DepthSortKey
            && Last.Mesh->Topology == Mesh->Topology
            && (Last.Mesh->Indices.empty() == Mesh->Indices.empty()))
        {
            const uint32 VertexBase = static_cast<uint32>(Last.Mesh->Vertices.size());
            Last.Mesh->Vertices.insert(Last.Mesh->Vertices.end(), Mesh->Vertices.begin(), Mesh->Vertices.end());
            if (!Mesh->Indices.empty())
            {
                Last.Mesh->Indices.reserve(Last.Mesh->Indices.size() + Mesh->Indices.size());
                for (const uint32 Index : Mesh->Indices)
                {
                    Last.Mesh->Indices.push_back(VertexBase + Index);
                }
            }

            Last.Mesh->bIsDirty = true;
            Mesh->Vertices.clear();
            Mesh->Indices.clear();
            Mesh->bIsDirty = true;
            return;
        }
    }

    FUIBatchCommand Command;
    Command.Mesh = Mesh;
    Command.Material = Material;
    Command.Layer = Layer;
    Command.Depth = Depth;
    Command.DepthSortKey = DepthSortKey;
    PassInputs.Batch.Commands.push_back(Command);
}

void FScreenUIPassBuilder::AppendFilledRect(FScreenUIPassInputs& PassInputs, const FUIDrawElement& Element)
{
    FUIRect DrawRect;
    if (!ResolveClippedRect(Element, DrawRect))
    {
        return;
    }

    FDynamicMesh* Mesh = CreateFrameMesh(PassInputs, EMeshTopology::EMT_TriangleList);
    if (!Mesh)
    {
        return;
    }

    const FVector4 Color = ToDisplayColor(Element.Color);
    AppendQuad(
        *Mesh,
        DrawRect.X,
        DrawRect.Y,
        DrawRect.X + DrawRect.Width,
        DrawRect.Y + DrawRect.Height,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        Color);

    Mesh->bIsDirty = true;
}

void FScreenUIPassBuilder::AppendRectOutline(FScreenUIPassInputs& PassInputs, const FUIDrawElement& Element)
{
    if (!Element.Rect.IsValid())
    {
        return;
    }

    FDynamicMesh* Mesh = CreateFrameMesh(PassInputs, EMeshTopology::EMT_TriangleList);
    if (!Mesh)
    {
        return;
    }

    const FVector4 Color = ToDisplayColor(Element.Color);
    const FUIRect* Clip = HasClip(Element) ? &Element.ClipRect : nullptr;
    const float X = Element.Rect.X;
    const float Y = Element.Rect.Y;
    const float W = Element.Rect.Width;
    const float H = Element.Rect.Height;
    const float T = 1.0f;

    AppendClippedQuad(*Mesh, X, Y, X + W, Y + T, 0, 0, 0, 0, Color, Clip);
    AppendClippedQuad(*Mesh, X, Y + H - T, X + W, Y + H, 0, 0, 0, 0, Color, Clip);
    AppendClippedQuad(*Mesh, X, Y + T, X + T, Y + H - T, 0, 0, 0, 0, Color, Clip);
    AppendClippedQuad(*Mesh, X + W - T, Y + T, X + W, Y + H - T, 0, 0, 0, 0, Color, Clip);

    Mesh->bIsDirty = true;
}

void FScreenUIPassBuilder::AppendText(FRenderer& Renderer, FScreenUIPassInputs& PassInputs, const FUIDrawElement& Element)
{
    if (Element.Text.empty())
    {
        return;
    }

    FDynamicMaterial* FontMaterial = GetOrCreateFontMaterial(Renderer, Element.Color);
    if (!FontMaterial)
    {
        return;
    }

    FDynamicMesh SourceMesh;
    SourceMesh.Topology = EMeshTopology::EMT_TriangleList;
    ISceneTextFeature* TextFeature = Renderer.GetSceneTextFeature();
    if (!TextFeature || !TextFeature->BuildMesh(Element.Text, SourceMesh, Element.LetterSpacing))
    {
        return;
    }

    ConvertTextMeshToScreenSpace(SourceMesh);

    float MaxX = 0.0f;
    float MaxY = 0.0f;
    MeasureMeshBounds(SourceMesh, MaxX, MaxY);

    if (HasClip(Element))
    {
        FUIRect TextBounds;
        TextBounds.X = Element.Point.X;
        TextBounds.Y = Element.Point.Y;
        TextBounds.Width = MaxX * Element.FontSize;
        TextBounds.Height = MaxY * Element.FontSize;

        const FUIRect Clipped = IntersectUIRect(TextBounds, Element.ClipRect);
        if (!Clipped.IsValid())
        {
            return;
        }
    }

    FDynamicMesh* Mesh = CreateFrameMesh(PassInputs, EMeshTopology::EMT_TriangleList);
    if (!Mesh)
    {
        return;
    }

    const FUIRect* Clip = HasClip(Element) ? &Element.ClipRect : nullptr;
    const FVector4 White(1.0f, 1.0f, 1.0f, 1.0f);

    for (size_t VertexBase = 0; VertexBase + 3 < SourceMesh.Vertices.size(); VertexBase += 4)
    {
        const FVertex& SV0 = SourceMesh.Vertices[VertexBase + 0];
        const FVertex& SV2 = SourceMesh.Vertices[VertexBase + 2];

        const float X0 = Element.Point.X + SV0.Position.X * Element.FontSize;
        const float Y0 = Element.Point.Y + SV0.Position.Y * Element.FontSize;
        const float X1 = Element.Point.X + SV2.Position.X * Element.FontSize;
        const float Y1 = Element.Point.Y + SV2.Position.Y * Element.FontSize;

        AppendClippedQuad(
            *Mesh,
            X0,
            Y0,
            X1,
            Y1,
            SV0.UV.X,
            SV0.UV.Y,
            SV2.UV.X,
            SV2.UV.Y,
            White,
            Clip);
    }

    if (Mesh->Vertices.empty())
    {
        return;
    }

    Mesh->bIsDirty = true;
    EnqueueMesh(PassInputs, Mesh, FontMaterial, Element.Layer, Element.Depth);
}

void FScreenUIPassBuilder::ApplyOrthoProjection(
    int32 Width,
    int32 Height,
    const D3D11_VIEWPORT& OutputViewport,
    FScreenUIPassInputs& OutPassInputs)
{
    if (Width <= 0 || Height <= 0)
    {
        OutPassInputs.Batch.ViewMatrix = FMatrix::Identity;
        OutPassInputs.Batch.ProjectionMatrix = FMatrix::Identity;
        OutPassInputs.View = {};
        OutPassInputs.View.Viewport = OutputViewport;
        return;
    }

    const FMatrix OrthoProjection(
        2.0f / Width, 0, 0, 0,
        0, -2.0f / Height, 0, 0,
        0, 0, 1, 0,
        -1, 1, 0, 1);

    OutPassInputs.Batch.ViewMatrix = FMatrix::Identity;
    OutPassInputs.Batch.ProjectionMatrix = OrthoProjection;
    OutPassInputs.View.View = OutPassInputs.Batch.ViewMatrix;
    OutPassInputs.View.Projection = OutPassInputs.Batch.ProjectionMatrix;
    OutPassInputs.View.ViewProjection = OutPassInputs.View.View * OutPassInputs.View.Projection;
    OutPassInputs.View.InverseView = OutPassInputs.View.View.GetInverse();
    OutPassInputs.View.InverseProjection = OutPassInputs.View.Projection.GetInverse();
    OutPassInputs.View.InverseViewProjection = OutPassInputs.View.ViewProjection.GetInverse();
    OutPassInputs.View.Viewport = OutputViewport;
}

bool FScreenUIPassBuilder::BuildPassInputs(
    FRenderer& Renderer,
    const FUIDrawList& DrawList,
    const D3D11_VIEWPORT& OutputViewport,
    FScreenUIPassInputs& OutPassInputs)
{
    OutPassInputs.Clear();
    if (DrawList.Elements.empty() || DrawList.ScreenWidth <= 0 || DrawList.ScreenHeight <= 0)
    {
        return true;
    }

    ApplyOrthoProjection(DrawList.ScreenWidth, DrawList.ScreenHeight, OutputViewport, OutPassInputs);
    OutPassInputs.Reserve(DrawList.Elements.size());

    FDynamicMaterial* ColorMaterial = GetOrCreateColorMaterial(Renderer);
    if (!ColorMaterial)
    {
        OutPassInputs.Clear();
        return false;
    }

    TArray<const FUIDrawElement*> SortedElements;
    SortedElements.reserve(DrawList.Elements.size());
    for (const FUIDrawElement& Element : DrawList.Elements)
    {
        SortedElements.push_back(&Element);
    }

    std::stable_sort(
        SortedElements.begin(),
        SortedElements.end(),
        [](const FUIDrawElement* A, const FUIDrawElement* B)
        {
            if (A->Layer != B->Layer)
            {
                return A->Layer < B->Layer;
            }

            const int32 ADepthKey = MakeDepthSortKey(A->Depth);
            const int32 BDepthKey = MakeDepthSortKey(B->Depth);
            if (ADepthKey != BDepthKey)
            {
                return ADepthKey < BDepthKey;
            }

            return A->Order < B->Order;
        });

    for (const FUIDrawElement* ElementPtr : SortedElements)
    {
        if (!ElementPtr)
        {
            continue;
        }

        const FUIDrawElement& Element = *ElementPtr;
        const size_t PrevMeshCount = OutPassInputs.MeshStorage.size();

        switch (Element.Type)
        {
        case EUIDrawElementType::FilledRect:
            AppendFilledRect(OutPassInputs, Element);
            if (OutPassInputs.MeshStorage.size() > PrevMeshCount)
            {
                EnqueueMesh(OutPassInputs, OutPassInputs.MeshStorage.back().get(), ColorMaterial, Element.Layer, Element.Depth);
            }
            break;

        case EUIDrawElementType::RectOutline:
            AppendRectOutline(OutPassInputs, Element);
            if (OutPassInputs.MeshStorage.size() > PrevMeshCount)
            {
                EnqueueMesh(OutPassInputs, OutPassInputs.MeshStorage.back().get(), ColorMaterial, Element.Layer, Element.Depth);
            }
            break;

        case EUIDrawElementType::Text:
            AppendText(Renderer, OutPassInputs, Element);
            break;
        }
    }

    return true;
}
