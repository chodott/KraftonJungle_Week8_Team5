#pragma once

#include "EngineAPI.h"
#include "Renderer/Common/RenderFrameContext.h"

#include <d3d11.h>
#include <utility>

class FRenderer;

struct ENGINE_API FFullscreenPassShaderSet
{
	ID3D11VertexShader* VS = nullptr;
	ID3D11PixelShader* PS = nullptr;
};

struct ENGINE_API FFullscreenPassPipelineState
{
	ID3D11BlendState* BlendState = nullptr;
	const float* BlendFactor = nullptr;
	UINT SampleMask = 0xFFFFFFFFu;
	ID3D11DepthStencilState* DepthStencilState = nullptr;
	UINT StencilRef = 0;
	ID3D11RasterizerState* RasterizerState = nullptr;
};

struct ENGINE_API FFullscreenPassConstantBufferBinding
{
	UINT Slot = 0;
	ID3D11Buffer* Buffer = nullptr;
};

struct ENGINE_API FFullscreenPassShaderResourceBinding
{
	UINT Slot = 0;
	ID3D11ShaderResourceView* ShaderResource = nullptr;
};

struct ENGINE_API FFullscreenPassSamplerBinding
{
	UINT Slot = 0;
	ID3D11SamplerState* Sampler = nullptr;
};

struct ENGINE_API FFullscreenPassBindings
{
	const FFullscreenPassConstantBufferBinding* ConstantBuffers = nullptr;
	uint32 ConstantBufferCount = 0;
	const FFullscreenPassShaderResourceBinding* ShaderResources = nullptr;
	uint32 ShaderResourceCount = 0;
	const FFullscreenPassSamplerBinding* Samplers = nullptr;
	uint32 SamplerCount = 0;
};

inline void BindFullscreenPassResources(ID3D11DeviceContext* Context, const FFullscreenPassBindings& Bindings)
{
	if (!Context)
	{
		return;
	}

	for (uint32 Index = 0; Index < Bindings.ConstantBufferCount; ++Index)
	{
		const FFullscreenPassConstantBufferBinding& Binding = Bindings.ConstantBuffers[Index];
		Context->PSSetConstantBuffers(Binding.Slot, 1, &Binding.Buffer);
	}

	for (uint32 Index = 0; Index < Bindings.ShaderResourceCount; ++Index)
	{
		const FFullscreenPassShaderResourceBinding& Binding = Bindings.ShaderResources[Index];
		Context->PSSetShaderResources(Binding.Slot, 1, &Binding.ShaderResource);
	}

	for (uint32 Index = 0; Index < Bindings.SamplerCount; ++Index)
	{
		const FFullscreenPassSamplerBinding& Binding = Bindings.Samplers[Index];
		Context->PSSetSamplers(Binding.Slot, 1, &Binding.Sampler);
	}
}

inline void ClearFullscreenPassResources(ID3D11DeviceContext* Context, const FFullscreenPassBindings& Bindings)
{
	if (!Context)
	{
		return;
	}

	ID3D11Buffer* NullBuffer = nullptr;
	for (uint32 Index = 0; Index < Bindings.ConstantBufferCount; ++Index)
	{
		const FFullscreenPassConstantBufferBinding& Binding = Bindings.ConstantBuffers[Index];
		Context->PSSetConstantBuffers(Binding.Slot, 1, &NullBuffer);
	}

	ID3D11ShaderResourceView* NullSRV = nullptr;
	for (uint32 Index = 0; Index < Bindings.ShaderResourceCount; ++Index)
	{
		const FFullscreenPassShaderResourceBinding& Binding = Bindings.ShaderResources[Index];
		Context->PSSetShaderResources(Binding.Slot, 1, &NullSRV);
	}

	ID3D11SamplerState* NullSampler = nullptr;
	for (uint32 Index = 0; Index < Bindings.SamplerCount; ++Index)
	{
		const FFullscreenPassSamplerBinding& Binding = Bindings.Samplers[Index];
		Context->PSSetSamplers(Binding.Slot, 1, &NullSampler);
	}
}

inline void BeginFullscreenPass(
	ID3D11DeviceContext* Context,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView,
	const D3D11_VIEWPORT& Viewport,
	const FFullscreenPassShaderSet& Shaders,
	const FFullscreenPassPipelineState& PipelineState = {})
{
	if (!Context)
	{
		return;
	}

	const float DefaultBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	Context->OMSetRenderTargets(1, &RenderTargetView, DepthStencilView);
	Context->RSSetViewports(1, &Viewport);
	Context->OMSetBlendState(
		PipelineState.BlendState,
		PipelineState.BlendFactor ? PipelineState.BlendFactor : DefaultBlendFactor,
		PipelineState.SampleMask);
	Context->OMSetDepthStencilState(PipelineState.DepthStencilState, PipelineState.StencilRef);
	Context->RSSetState(PipelineState.RasterizerState);
	Context->IASetInputLayout(nullptr);
	Context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Context->VSSetShader(Shaders.VS, nullptr, 0);
	Context->PSSetShader(Shaders.PS, nullptr, 0);
}

inline void EndFullscreenPass(ID3D11DeviceContext* Context)
{
	if (!Context)
	{
		return;
	}

	const float DefaultBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	Context->OMSetBlendState(nullptr, DefaultBlendFactor, 0xFFFFFFFFu);
	Context->OMSetDepthStencilState(nullptr, 0);
	Context->RSSetState(nullptr);
}

ENGINE_API void RestoreScenePassDefaults(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View);

ENGINE_API void BeginPass(
	FRenderer& Renderer,
	uint32 NumRenderTargets,
	ID3D11RenderTargetView* const* RenderTargetViews,
	ID3D11DepthStencilView* DepthStencilView,
	const D3D11_VIEWPORT& Viewport,
	const FFrameContext& Frame,
	const FViewContext& View);

inline void BeginPass(
	FRenderer& Renderer,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView,
	const D3D11_VIEWPORT& Viewport,
	const FFrameContext& Frame,
	const FViewContext& View)
{
	ID3D11RenderTargetView* RenderTargetViews[1] = { RenderTargetView };
	BeginPass(
		Renderer,
		RenderTargetView ? 1u : 0u,
		RenderTargetView ? RenderTargetViews : nullptr,
		DepthStencilView,
		Viewport,
		Frame,
		View);
}

ENGINE_API void EndPass(
	FRenderer& Renderer,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView,
	const D3D11_VIEWPORT& Viewport,
	const FFrameContext& Frame,
	const FViewContext& View);

ENGINE_API void BeginFullscreenPass(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView,
	const D3D11_VIEWPORT& Viewport,
	const FFullscreenPassShaderSet& Shaders,
	const FFullscreenPassPipelineState& PipelineState = {});

ENGINE_API void EndFullscreenPass(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView,
	const D3D11_VIEWPORT& Viewport);

ENGINE_API ID3D11DeviceContext* GetFullscreenPassDeviceContext(FRenderer& Renderer);

template <typename TDrawFunc>
inline bool ExecuteFullscreenPass(
	FRenderer& Renderer,
	const FFrameContext& Frame,
	const FViewContext& View,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView,
	const D3D11_VIEWPORT& Viewport,
	const FFullscreenPassShaderSet& Shaders,
	const FFullscreenPassPipelineState& PipelineState,
	const FFullscreenPassBindings& Bindings,
	TDrawFunc&& DrawFunc)
{
	ID3D11DeviceContext* Context = GetFullscreenPassDeviceContext(Renderer);
	if (!Context)
	{
		return false;
	}

	BeginFullscreenPass(Renderer, Frame, View, RenderTargetView, DepthStencilView, Viewport, Shaders, PipelineState);
	BindFullscreenPassResources(Context, Bindings);
	std::forward<TDrawFunc>(DrawFunc)(*Context);
	ClearFullscreenPassResources(Context, Bindings);
	EndFullscreenPass(Renderer, Frame, View, RenderTargetView, DepthStencilView, Viewport);
	return true;
}
