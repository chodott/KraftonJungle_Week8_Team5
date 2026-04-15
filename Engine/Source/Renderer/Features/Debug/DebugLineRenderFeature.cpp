#include "Renderer/Features/Debug/DebugLineRenderFeature.h"

#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Renderer.h"

FDebugLineRenderFeature::~FDebugLineRenderFeature()
{
	Release();
}

bool FDebugLineRenderFeature::EnsureDebugDepthState(ID3D11Device* Device)
{
	if (DebugDepthOffState)
	{
		return true;
	}

	if (!Device)
	{
		return false;
	}

	D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
	DepthDesc.DepthEnable = FALSE;
	DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	DepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	DepthDesc.StencilEnable = FALSE;

	return SUCCEEDED(Device->CreateDepthStencilState(&DepthDesc, &DebugDepthOffState));
}

void FDebugLineRenderFeature::AppendLine(
	FEditorLinePassInputs& PassInputs,
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
	FEditorLinePassInputs& PassInputs,
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
	FEditorLinePassInputs& PassInputs)
{
	if (PassInputs.IsEmpty())
	{
		return true;
	}

	ID3D11Device* Device = Renderer.GetDevice();
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	FMaterial* Material = PassInputs.Material ? PassInputs.Material : Renderer.GetDefaultMaterial();
	FDynamicMesh* LineMesh = PassInputs.LineMesh.get();
	ID3D11RenderTargetView* OverlayRenderTarget = Targets.OverlayColorRTV
		? Targets.OverlayColorRTV
		: Targets.SceneColorRTV;
	if (!Device || !DeviceContext || !Material || !LineMesh || !OverlayRenderTarget || !Targets.SceneDepthDSV)
	{
		return false;
	}

	if (!LineMesh->UpdateVertexAndIndexBuffer(Device, DeviceContext))
	{
		return false;
	}

	if (!EnsureDebugDepthState(Device))
	{
		return false;
	}

	BeginPass(Renderer, OverlayRenderTarget, Targets.SceneDepthDSV, View.Viewport, Frame, View);
	Material->Bind(DeviceContext, EMaterialPassType::ForwardOpaque);
	Renderer.GetRenderStateManager()->BindState(Material->GetRasterizerState());
	DeviceContext->OMSetDepthStencilState(DebugDepthOffState, 0);
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
	if (DebugDepthOffState)
	{
		DebugDepthOffState->Release();
		DebugDepthOffState = nullptr;
	}
}
