#pragma once

#include "CoreMinimal.h"

#include <d3d11.h>

struct ENGINE_API FSceneRenderTargets
{
	uint32 Width = 0;
	uint32 Height = 0;

	ID3D11Texture2D* SceneColorTexture = nullptr;
	ID3D11RenderTargetView* SceneColorRTV = nullptr;
	ID3D11ShaderResourceView* SceneColorSRV = nullptr;

	ID3D11Texture2D* SceneColorScratchTexture = nullptr;
	ID3D11RenderTargetView* SceneColorScratchRTV = nullptr;
	ID3D11ShaderResourceView* SceneColorScratchSRV = nullptr;

	ID3D11Texture2D* OverlayColorTexture = nullptr;
	ID3D11RenderTargetView* OverlayColorRTV = nullptr;
	ID3D11ShaderResourceView* OverlayColorSRV = nullptr;

	ID3D11Texture2D* SceneDepthTexture = nullptr;
	ID3D11DepthStencilView* SceneDepthDSV = nullptr;
	ID3D11ShaderResourceView* SceneDepthSRV = nullptr;

	ID3D11Texture2D* GBufferATexture = nullptr;
	ID3D11RenderTargetView* GBufferARTV = nullptr;
	ID3D11ShaderResourceView* GBufferASRV = nullptr;

	ID3D11Texture2D* GBufferBTexture = nullptr;
	ID3D11RenderTargetView* GBufferBRTV = nullptr;
	ID3D11ShaderResourceView* GBufferBSRV = nullptr;

	ID3D11Texture2D* GBufferCTexture = nullptr;
	ID3D11RenderTargetView* GBufferCRTV = nullptr;
	ID3D11ShaderResourceView* GBufferCSRV = nullptr;

	ID3D11Texture2D* OutlineMaskTexture = nullptr;
	ID3D11RenderTargetView* OutlineMaskRTV = nullptr;
	ID3D11ShaderResourceView* OutlineMaskSRV = nullptr;

	bool IsValid() const
	{
		return SceneColorRTV != nullptr && SceneDepthDSV != nullptr && Width > 0 && Height > 0;
	}
};
