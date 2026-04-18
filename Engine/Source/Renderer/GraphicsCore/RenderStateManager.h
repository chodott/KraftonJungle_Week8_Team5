#pragma once

#include "Renderer/GraphicsCore/RenderState.h"
#include "Types/CoreTypes.h"
#include "Types/Map.h"

#include <memory>

class ENGINE_API FRenderStateManager
{
private:
	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* DeviceContext = nullptr;
	TMap<uint32, std::shared_ptr<FRasterizerState>> RasterizerStateMap;
	TMap<uint32, std::shared_ptr<FDepthStencilState>> DepthStencilStateMap;
	TMap<uint32, std::shared_ptr<FBlendState>> BlendStateMap;

	std::shared_ptr<FRasterizerState> CurrentRasterizerState = nullptr;
	std::shared_ptr<FDepthStencilState> CurrentDepthStencilState = nullptr;
	std::shared_ptr<FBlendState> CurrentBlendState = nullptr;

	uint32 CurrentStencilRef = 0;
	float CurrentBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	uint32 CurrentSampleMask = 0xFFFFFFFF;

public:
	FRenderStateManager(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext)
		: Device(InDevice)
		, DeviceContext(InDeviceContext)
	{
	}

	void PrepareCommonStates();

	std::shared_ptr<FRasterizerState> GetOrCreateRasterizerState(const FRasterizerStateOption& opt);
	std::shared_ptr<FDepthStencilState> GetOrCreateDepthStencilState(const FDepthStencilStateOption& opt);
	std::shared_ptr<FBlendState> GetOrCreateBlendState(const FBlendStateOption& opt);

	void BindState(std::shared_ptr<FRasterizerState> InRS);
	void BindState(std::shared_ptr<FDepthStencilState> InDSS);
	void BindState(std::shared_ptr<FBlendState> InBS);

	void BindDepthStencilState(std::shared_ptr<FDepthStencilState> InDSS, uint32 StencilRef);
	void BindBlendState(std::shared_ptr<FBlendState> InBS, const float BlendFactor[4], uint32 SampleMask);
	void SetRenderTargets(uint32 NumRTs, ID3D11RenderTargetView* const* RTVs, ID3D11DepthStencilView* DSV);
	void ClearShaderResourcesVS(uint32 StartSlot, uint32 Count);
	void ClearShaderResourcesPS(uint32 StartSlot, uint32 Count);
	void ClearShaderResourcesCS(uint32 StartSlot, uint32 Count);
	void ClearUnorderedAccessViewsCS(uint32 StartSlot, uint32 Count);
	void ClearAllGraphicsState();
	void ClearAllComputeState();
	void UnbindResourceFromGraphics(ID3D11Resource* Resource);
	void UnbindResourceFromCompute(ID3D11Resource* Resource);
	void UnbindResourceEverywhere(ID3D11Resource* Resource);

	void RebindState();
};
