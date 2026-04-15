#include "Renderer/Mesh/RenderMesh.h"
#include "Renderer/Mesh/Vertex.h"

void FRenderMesh::Bind(ID3D11DeviceContext* Context)
{
	if (!VertexBuffer) return;
	UINT Stride = sizeof(FVertex);
	UINT Offset = 0;
	Context->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);
	if (IndexBuffer)
	{
		Context->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	}
	else
	{
		Context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	}
}

void FRenderMesh::Release()
{
	if (IndexBuffer) { IndexBuffer->Release(); IndexBuffer = nullptr; }
	if (VertexBuffer) { VertexBuffer->Release(); VertexBuffer = nullptr; }
}

void FRenderMesh::UpdateLocalBound()
{
    MinCoord = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
    MaxCoord = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    LocalBoundRadius = 0.f;

    if (Vertices.empty())
    {
        return;
    }

    for (const FVertex& Vertex : Vertices)
    {
        MinCoord.X = std::min(MinCoord.X, Vertex.Position.X);
        MinCoord.Y = std::min(MinCoord.Y, Vertex.Position.Y);
        MinCoord.Z = std::min(MinCoord.Z, Vertex.Position.Z);

        MaxCoord.X = std::max(MaxCoord.X, Vertex.Position.X);
        MaxCoord.Y = std::max(MaxCoord.Y, Vertex.Position.Y);
        MaxCoord.Z = std::max(MaxCoord.Z, Vertex.Position.Z);
    }

    LocalBoundRadius = ((MaxCoord - MinCoord) * 0.5f).Size();
}