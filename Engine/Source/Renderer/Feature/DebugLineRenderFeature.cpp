#include "Renderer/Feature/DebugLineRenderFeature.h"

#include "Renderer/FullscreenPass.h"
#include "Renderer/Renderer.h"

FDebugLineRenderFeature::~FDebugLineRenderFeature()
{
	Release();
}

void FDebugLineRenderFeature::AppendLine(
	FDebugLinePassInputs& PassInputs,
	const FVector& Start,
	const FVector& End,
	const FVector4& Color)
{
	FDynamicMesh& LineMesh = PassInputs.GetOrCreateLineMesh();
	LineMesh.Vertices.push_back({ Start, Color, FVector::ZeroVector });
	LineMesh.Vertices.push_back({ End, Color, FVector::ZeroVector });
	LineMesh.bIsDirty = true;
}

void FDebugLineRenderFeature::AppendCube(
	FDebugLinePassInputs& PassInputs,
	const FVector& Center,
	const FVector& BoxExtent,
	const FVector4& Color)
{
	const FVector Vertices[8] = {
		Center + FVector(-BoxExtent.X, -BoxExtent.Y, -BoxExtent.Z),
		Center + FVector(-BoxExtent.X, -BoxExtent.Y, BoxExtent.Z),
		Center + FVector(-BoxExtent.X, BoxExtent.Y, -BoxExtent.Z),
		Center + FVector(-BoxExtent.X, BoxExtent.Y, BoxExtent.Z),
		Center + FVector(BoxExtent.X, -BoxExtent.Y, -BoxExtent.Z),
		Center + FVector(BoxExtent.X, -BoxExtent.Y, BoxExtent.Z),
		Center + FVector(BoxExtent.X, BoxExtent.Y, -BoxExtent.Z),
		Center + FVector(BoxExtent.X, BoxExtent.Y, BoxExtent.Z)
	};

	AppendLine(PassInputs, Vertices[0], Vertices[4], Color);
	AppendLine(PassInputs, Vertices[4], Vertices[6], Color);
	AppendLine(PassInputs, Vertices[6], Vertices[2], Color);
	AppendLine(PassInputs, Vertices[2], Vertices[0], Color);
	AppendLine(PassInputs, Vertices[1], Vertices[5], Color);
	AppendLine(PassInputs, Vertices[5], Vertices[7], Color);
	AppendLine(PassInputs, Vertices[7], Vertices[3], Color);
	AppendLine(PassInputs, Vertices[3], Vertices[1], Color);
	AppendLine(PassInputs, Vertices[0], Vertices[1], Color);
	AppendLine(PassInputs, Vertices[4], Vertices[5], Color);
	AppendLine(PassInputs, Vertices[6], Vertices[7], Color);
	AppendLine(PassInputs, Vertices[2], Vertices[3], Color);
}

bool FDebugLineRenderFeature::Render(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	const FSceneRenderTargets& Targets,
	FDebugLinePassInputs& PassInputs)
{
	if (PassInputs.IsEmpty())
	{
		return true;
	}

	ID3D11Device* Device = Renderer.GetDevice();
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	FMaterial* Material = PassInputs.Material ? PassInputs.Material : Renderer.GetDefaultMaterial();
	FDynamicMesh* LineMesh = PassInputs.LineMesh.get();
	if (!Device || !DeviceContext || !Material || !LineMesh || !Targets.SceneColorRTV || !Targets.SceneDepthDSV)
	{
		return false;
	}

	if (!LineMesh->UpdateVertexAndIndexBuffer(Device, DeviceContext))
	{
		return false;
	}

	BeginPass(Renderer, Targets.SceneColorRTV, Targets.SceneDepthDSV, View.Viewport, Frame, View);
	Material->Bind(DeviceContext, EMaterialPassType::ForwardOpaque);
	Renderer.GetRenderStateManager()->BindState(Material->GetRasterizerState());
	Renderer.GetRenderStateManager()->BindState(Material->GetDepthStencilState());
	Renderer.GetRenderStateManager()->BindState(Material->GetBlendState());
	if (!Material->HasPixelTextureBinding())
	{
		ID3D11SamplerState* DefaultSampler = Renderer.GetDefaultSampler();
		DeviceContext->PSSetSamplers(0, 1, &DefaultSampler);
	}

	LineMesh->Bind(DeviceContext);
	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
	Renderer.UpdateObjectConstantBuffer(FMatrix::Identity);
	DeviceContext->Draw(static_cast<UINT>(LineMesh->Vertices.size()), 0);
	EndPass(Renderer, Targets.SceneColorRTV, Targets.SceneDepthDSV, View.Viewport, Frame, View);
	return true;
}

void FDebugLineRenderFeature::Release()
{
}
