#pragma once

#include "CoreMinimal.h"

#include <d3d11.h>
#include <utility>
#include <vector>

enum class ETextureBindFlags : uint32
{
	None = 0u,
	SRV  = 1u << 0,
	RTV  = 1u << 1,
	DSV  = 1u << 2,
	UAV  = 1u << 3,
};

inline ETextureBindFlags operator|(ETextureBindFlags Lhs, ETextureBindFlags Rhs)
{
	return static_cast<ETextureBindFlags>(static_cast<uint32>(Lhs) | static_cast<uint32>(Rhs));
}

inline ETextureBindFlags& operator|=(ETextureBindFlags& Lhs, ETextureBindFlags Rhs)
{
	Lhs = Lhs | Rhs;
	return Lhs;
}

inline bool HasAnyTextureBindFlags(ETextureBindFlags Value, ETextureBindFlags Flags)
{
	return (static_cast<uint32>(Value) & static_cast<uint32>(Flags)) != 0u;
}

struct ENGINE_API FGPUTextureDesc
{
	uint32 Width     = 0;
	uint32 Height    = 0;
	uint32 MipLevels = 1;

	DXGI_FORMAT TextureFormat = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT SRVFormat     = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT RTVFormat     = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT DSVFormat     = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT UAVFormat     = DXGI_FORMAT_UNKNOWN;

	ETextureBindFlags BindFlags        = ETextureBindFlags::None;
	bool              bExternalWrapped = false;
};

struct ENGINE_API FGPUTexture2D
{
	FGPUTextureDesc                         Desc;
	ID3D11Texture2D*                        Texture = nullptr;
	ID3D11RenderTargetView*                 RTV     = nullptr;
	ID3D11ShaderResourceView*               SRV     = nullptr;
	ID3D11DepthStencilView*                 DSV     = nullptr;
	ID3D11UnorderedAccessView*              UAV     = nullptr;

	TArray<ID3D11DepthStencilView*> ArrayDSVs;
	std::vector<ID3D11ShaderResourceView*>  MipSRVs;
	std::vector<ID3D11UnorderedAccessView*> MipUAVs;

	bool IsValid() const
	{
		return Texture != nullptr || RTV != nullptr || SRV != nullptr || DSV != nullptr || UAV != nullptr;
	}

	bool IsExternal() const
	{
		return Desc.bExternalWrapped;
	}
};

struct ENGINE_API FSceneRenderTargets
{
	uint32 Width  = 0;
	uint32 Height = 0;

	FGPUTexture2D* FinalSceneColor = nullptr;
	FGPUTexture2D* SceneColorRead  = nullptr;
	FGPUTexture2D* SceneColorWrite = nullptr;
	FGPUTexture2D* OverlayColor    = nullptr;
	FGPUTexture2D* SceneDepth      = nullptr;
	FGPUTexture2D* ShadowMap		= nullptr;
	FGPUTexture2D* GBufferA        = nullptr;
	FGPUTexture2D* GBufferB        = nullptr;
	FGPUTexture2D* GBufferC        = nullptr;
	FGPUTexture2D* OutlineMask     = nullptr;

	ID3D11Texture2D*           SceneColorTexture = nullptr;
	ID3D11RenderTargetView*    SceneColorRTV     = nullptr;
	ID3D11ShaderResourceView*  SceneColorSRV     = nullptr;
	ID3D11UnorderedAccessView* SceneColorUAV     = nullptr;

	ID3D11Texture2D*           SceneColorScratchTexture = nullptr;
	ID3D11RenderTargetView*    SceneColorScratchRTV     = nullptr;
	ID3D11ShaderResourceView*  SceneColorScratchSRV     = nullptr;
	ID3D11UnorderedAccessView* SceneColorScratchUAV     = nullptr;

	ID3D11Texture2D*           OverlayColorTexture = nullptr;
	ID3D11RenderTargetView*    OverlayColorRTV     = nullptr;
	ID3D11ShaderResourceView*  OverlayColorSRV     = nullptr;
	ID3D11UnorderedAccessView* OverlayColorUAV     = nullptr;

	ID3D11Texture2D*          SceneDepthTexture = nullptr;
	ID3D11DepthStencilView*   SceneDepthDSV     = nullptr;
	ID3D11ShaderResourceView* SceneDepthSRV     = nullptr;

	ID3D11Texture2D*					ShadowMapTexture	= nullptr;
	ID3D11ShaderResourceView*			ShadowMapSRV		= nullptr;
	TArray<ID3D11DepthStencilView*>		ShadowMapDSVs;

	ID3D11Texture2D*           GBufferATexture = nullptr;
	ID3D11RenderTargetView*    GBufferARTV     = nullptr;
	ID3D11ShaderResourceView*  GBufferASRV     = nullptr;
	ID3D11UnorderedAccessView* GBufferAUAV     = nullptr;

	ID3D11Texture2D*           GBufferBTexture = nullptr;
	ID3D11RenderTargetView*    GBufferBRTV     = nullptr;
	ID3D11ShaderResourceView*  GBufferBSRV     = nullptr;
	ID3D11UnorderedAccessView* GBufferBUAV     = nullptr;

	ID3D11Texture2D*           GBufferCTexture = nullptr;
	ID3D11RenderTargetView*    GBufferCRTV     = nullptr;
	ID3D11ShaderResourceView*  GBufferCSRV     = nullptr;
	ID3D11UnorderedAccessView* GBufferCUAV     = nullptr;

	ID3D11Texture2D*           OutlineMaskTexture = nullptr;
	ID3D11RenderTargetView*    OutlineMaskRTV     = nullptr;
	ID3D11ShaderResourceView*  OutlineMaskSRV     = nullptr;
	ID3D11UnorderedAccessView* OutlineMaskUAV     = nullptr;

	void RefreshCompatibilityViews()
	{
		SceneColorTexture = SceneColorRead ? SceneColorRead->Texture : nullptr;
		SceneColorRTV     = SceneColorRead ? SceneColorRead->RTV : nullptr;
		SceneColorSRV     = SceneColorRead ? SceneColorRead->SRV : nullptr;
		SceneColorUAV     = SceneColorRead ? SceneColorRead->UAV : nullptr;

		SceneColorScratchTexture = SceneColorWrite ? SceneColorWrite->Texture : nullptr;
		SceneColorScratchRTV     = SceneColorWrite ? SceneColorWrite->RTV : nullptr;
		SceneColorScratchSRV     = SceneColorWrite ? SceneColorWrite->SRV : nullptr;
		SceneColorScratchUAV     = SceneColorWrite ? SceneColorWrite->UAV : nullptr;

		OverlayColorTexture = OverlayColor ? OverlayColor->Texture : nullptr;
		OverlayColorRTV     = OverlayColor ? OverlayColor->RTV : nullptr;
		OverlayColorSRV     = OverlayColor ? OverlayColor->SRV : nullptr;
		OverlayColorUAV     = OverlayColor ? OverlayColor->UAV : nullptr;

		SceneDepthTexture = SceneDepth ? SceneDepth->Texture : nullptr;
		SceneDepthDSV     = SceneDepth ? SceneDepth->DSV : nullptr;
		SceneDepthSRV     = SceneDepth ? SceneDepth->SRV : nullptr;

		ShadowMapTexture = ShadowMap ? ShadowMap->Texture : nullptr;
		if (ShadowMap)
		{
			ShadowMapDSVs = ShadowMap->ArrayDSVs;
		}
		else
		{
			ShadowMapDSVs.clear();
		}
		ShadowMapSRV = ShadowMap ? ShadowMap->SRV : nullptr;

		GBufferATexture = GBufferA ? GBufferA->Texture : nullptr;
		GBufferARTV     = GBufferA ? GBufferA->RTV : nullptr;
		GBufferASRV     = GBufferA ? GBufferA->SRV : nullptr;
		GBufferAUAV     = GBufferA ? GBufferA->UAV : nullptr;

		GBufferBTexture = GBufferB ? GBufferB->Texture : nullptr;
		GBufferBRTV     = GBufferB ? GBufferB->RTV : nullptr;
		GBufferBSRV     = GBufferB ? GBufferB->SRV : nullptr;
		GBufferBUAV     = GBufferB ? GBufferB->UAV : nullptr;

		GBufferCTexture = GBufferC ? GBufferC->Texture : nullptr;
		GBufferCRTV     = GBufferC ? GBufferC->RTV : nullptr;
		GBufferCSRV     = GBufferC ? GBufferC->SRV : nullptr;
		GBufferCUAV     = GBufferC ? GBufferC->UAV : nullptr;

		OutlineMaskTexture = OutlineMask ? OutlineMask->Texture : nullptr;
		OutlineMaskRTV     = OutlineMask ? OutlineMask->RTV : nullptr;
		OutlineMaskSRV     = OutlineMask ? OutlineMask->SRV : nullptr;
		OutlineMaskUAV     = OutlineMask ? OutlineMask->UAV : nullptr;
	}

	ID3D11RenderTargetView* GetSceneColorRenderTarget() const
	{
		return SceneColorRead ? SceneColorRead->RTV : SceneColorRTV;
	}

	ID3D11ShaderResourceView* GetSceneColorShaderResource() const
	{
		return SceneColorRead ? SceneColorRead->SRV : SceneColorSRV;
	}

	ID3D11Texture2D* GetSceneColorTexture() const
	{
		return SceneColorRead ? SceneColorRead->Texture : SceneColorTexture;
	}

	ID3D11UnorderedAccessView* GetSceneColorUAVView() const
	{
		return SceneColorRead ? SceneColorRead->UAV : SceneColorUAV;
	}

	ID3D11RenderTargetView* GetSceneColorWriteRenderTarget() const
	{
		return SceneColorWrite ? SceneColorWrite->RTV : SceneColorScratchRTV;
	}

	ID3D11ShaderResourceView* GetSceneColorWriteShaderResource() const
	{
		return SceneColorWrite ? SceneColorWrite->SRV : SceneColorScratchSRV;
	}

	ID3D11Texture2D* GetSceneColorWriteTexture() const
	{
		return SceneColorWrite ? SceneColorWrite->Texture : SceneColorScratchTexture;
	}

	ID3D11UnorderedAccessView* GetSceneColorWriteUAV() const
	{
		return SceneColorWrite ? SceneColorWrite->UAV : SceneColorScratchUAV;
	}

	void SwapSceneColor()
	{
		if (!SceneColorRead || !SceneColorWrite)
		{
			return;
		}

		std::swap(SceneColorRead, SceneColorWrite);
		RefreshCompatibilityViews();
	}

	bool NeedsSceneColorResolve() const
	{
		return FinalSceneColor != nullptr
				&& SceneColorRead != nullptr
				&& FinalSceneColor != SceneColorRead;
	}

	bool IsValid() const
	{
		return GetSceneColorRenderTarget() != nullptr
				&& SceneDepthDSV != nullptr
				&& Width > 0
				&& Height > 0;
	}
};
