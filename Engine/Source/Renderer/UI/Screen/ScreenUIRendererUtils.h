#pragma once

#include "CoreMinimal.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/UI/Screen/UIDrawList.h"

struct FVertex;

namespace ScreenUIRendererUtils
{
    ENGINE_API FVector4 ToColor(uint32 Color);
    ENGINE_API void ConvertTextMeshToScreenSpace(FDynamicMesh& Mesh);
    ENGINE_API bool HasClip(const FUIDrawElement& Element);
    ENGINE_API FUIRect IntersectUIRect(const FUIRect& A, const FUIRect& B);
    ENGINE_API bool ResolveClippedRect(const FUIDrawElement& Element, FUIRect& OutRect);
    ENGINE_API void AppendQuad(
        FDynamicMesh& Mesh,
        float X0,
        float Y0,
        float X1,
        float Y1,
        float U0,
        float V0,
        float U1,
        float V1,
        const FVector4& Color);
    ENGINE_API void AppendClippedQuad(
        FDynamicMesh& Mesh,
        float X0,
        float Y0,
        float X1,
        float Y1,
        float U0,
        float V0,
        float U1,
        float V1,
        const FVector4& Color,
        const FUIRect* ClipRect);
    ENGINE_API bool MeasureMeshBounds(const FDynamicMesh& Mesh, float& OutMaxX, float& OutMaxY);
    ENGINE_API int32 MakeDepthSortKey(float Depth);
}
