#include "Renderer/Features/Lighting/LightRenderFeature.h"

#include "Renderer/Renderer.h"
#include "Core/Paths.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Resources/Shader/ShaderResource.h"
#include "World/World.h"
#include "Actor/Actor.h"
#include "Component/LightComponent.h"
#include "Component/AmbientLightComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/PointLightComponent.h"
#include "Component/SpotLightComponent.h"
#include "Math/MathUtility.h"

#include <algorithm>
#include <cmath>

FLightRenderFeature::~FLightRenderFeature()
{
	Release();
}

bool FLightRenderFeature::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	std::shared_ptr<FShaderResource> VSResource;
	std::shared_ptr<FShaderResource> PSResource;

	const std::wstring ShaderDir = FPaths::ShaderDir().wstring();

	CompileShaderVariants(Renderer);

	if (!LightConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth = sizeof(FLightConstantBuffer);
		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &LightConstantBuffer)))
		{
			return false;
		}
	}

	if (!LightBlendState)
	{
		D3D11_BLEND_DESC BlendDesc = {};
		BlendDesc.RenderTarget[0].BlendEnable = TRUE;
		BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(Device->CreateBlendState(&BlendDesc, &LightBlendState)))
		{
			return false;
		}
	}

	if (!NoDepthState)
	{
		D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
		DepthDesc.DepthEnable = FALSE;
		DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		DepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		if (FAILED(Device->CreateDepthStencilState(&DepthDesc, &NoDepthState)))
		{
			return false;
		}
	}

	if (!LightRasterizerState)
	{
		D3D11_RASTERIZER_DESC RasterDesc = {};
		RasterDesc.FillMode = D3D11_FILL_SOLID;
		RasterDesc.CullMode = D3D11_CULL_NONE;
		RasterDesc.DepthClipEnable = TRUE;
		if (FAILED(Device->CreateRasterizerState(&RasterDesc, &LightRasterizerState)))
		{
			return false;
		}
	}

	if (!DepthSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		SamplerDesc.MinLOD = 0.0f;
		SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(Device->CreateSamplerState(&SamplerDesc, &DepthSampler)))
		{
			return false;
		}
	}

	return true;
}

void FLightRenderFeature::UpdateLightConstantBuffer(FRenderer& Renderer, const FSceneViewData& SceneViewData)
{
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!LightConstantBuffer || !DeviceContext)
	{
		return;
	}

	FLightConstantBuffer CBData = {};

	const UWorld* World = SceneViewData.DebugInputs.World;
	if (World)
	{
		const TArray<AActor*> Actors = World->GetAllActors();
		size_t PointLightCount = 0;
		size_t SpotLightCount = 0;

		for (AActor* Actor : Actors)
		{
			if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible())
			{
				continue;
			}

			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (!Component || Component->IsPendingKill() || !Component->IsRegistered())
				{
					continue;
				}

				if (!Component->IsA(ULightComponent::StaticClass()))
				{
					continue;
				}

				ULightComponent* LightComponent = static_cast<ULightComponent*>(Component);
				if (!LightComponent->GetVisible())
				{
					continue;
				}

				const FLinearColor LightColor = LightComponent->GetColor();
				const FVector4 PackedColor(LightColor.R, LightColor.G, LightColor.B, 1.0f);
				const float EffectiveIntensity = LightComponent->GetEffectiveIntensity();

				if (LightComponent->IsA(UAmbientLightComponent::StaticClass()))
				{
					CBData.Ambient.Color = PackedColor;
					CBData.Ambient.Intensity = EffectiveIntensity;
					continue;
				}

				if (LightComponent->IsA(UDirectionalLightComponent::StaticClass()))
				{
					const FVector LightDirectionWS = LightComponent->GetEmissionDirectionWS().GetSafeNormal();
					CBData.Directional.Color = PackedColor;
					CBData.Directional.Direction = LightDirectionWS.IsNearlyZero() ? FVector::ForwardVector : LightDirectionWS;
					CBData.Directional.Intensity = EffectiveIntensity;
					continue;
				}

				if (LightComponent->IsA(USpotLightComponent::StaticClass()))
				{
					if (SpotLightCount >= static_cast<size_t>(_countof(CBData.SpotLights)))
					{
						continue;
					}

					USpotLightComponent* SpotLight = static_cast<USpotLightComponent*>(LightComponent);
					FSpotLightInfo& OutSpotLight = CBData.SpotLights[SpotLightCount++];

					OutSpotLight.Color = PackedColor;
					OutSpotLight.Position = SpotLight->GetWorldLocation();
					OutSpotLight.Intensity = EffectiveIntensity;
					OutSpotLight.Direction = SpotLight->GetEmissionDirectionWS().GetSafeNormal();
					OutSpotLight.Range = SpotLight->GetAttenuationRadius();
					OutSpotLight.FalloffExponent = SpotLight->GetLightFalloffExponent();

					const float InnerAngleRad = FMath::DegreesToRadians(FMath::Clamp(SpotLight->GetInnerConeAngle(), 0.0f, 89.0f));
					const float OuterAngleRad = FMath::DegreesToRadians(FMath::Clamp(SpotLight->GetOuterConeAngle(), 0.0f, 89.0f));
					OutSpotLight.InnerCutoff = std::cos(InnerAngleRad);
					OutSpotLight.OuterCutoff = std::cos(OuterAngleRad);
					if (OutSpotLight.InnerCutoff < OutSpotLight.OuterCutoff)
					{
						std::swap(OutSpotLight.InnerCutoff, OutSpotLight.OuterCutoff);
					}
					continue;
				}

				if (LightComponent->IsA(UPointLightComponent::StaticClass()))
				{
					if (PointLightCount >= static_cast<size_t>(_countof(CBData.PointLights)))
					{
						continue;
					}

					UPointLightComponent* PointLight = static_cast<UPointLightComponent*>(LightComponent);
					FPointLightInfo& OutPointLight = CBData.PointLights[PointLightCount++];

					OutPointLight.Color = PackedColor;
					OutPointLight.Position = PointLight->GetWorldLocation();
					OutPointLight.Intensity = EffectiveIntensity;
					OutPointLight.Range = PointLight->GetAttenuationRadius();
					OutPointLight.FalloffExponent = PointLight->GetLightFalloffExponent();
				}
			}
		}
	}

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DeviceContext->Map(LightConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(LightConstantBuffer, 0);
	}
}

bool FLightRenderFeature::CompileShaderVariants(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	const std::wstring VS = FPaths::ShaderDir().wstring() + L"SceneLighting/UberLitVertexShader.hlsl";
	const wchar_t* VSPath = VS.c_str();

	const std::wstring PS = FPaths::ShaderDir().wstring() + L"SceneLighting/UberLitPixelShader.hlsl";
	const wchar_t* PSPath = PS.c_str();

	D3D_SHADER_MACRO GouraudMacros[] = { { "LIGHTING_MODEL_GOURAUD", "1" }, { nullptr, nullptr } };
	D3D_SHADER_MACRO LambertMacros[] = { { "LIGHTING_MODEL_LAMBERT", "1" }, { nullptr, nullptr } };
	D3D_SHADER_MACRO PhongMacros[] = { { "LIGHTING_MODEL_PHONG",   "1" }, { nullptr, nullptr } };

	// Gouraud
	if (!GouraudVS)
	{
		auto Resource = FShaderResource::GetOrCompile(VSPath, "main", "vs_5_0", GouraudMacros);
		if (!Resource) return false;

		if (!LightInputLayout)
		{
			const D3D11_INPUT_ELEMENT_DESC InputDesc[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};
			if (FAILED(Device->CreateInputLayout(
				InputDesc, _countof(InputDesc),
				Resource->GetBufferPointer(), Resource->GetBufferSize(),
				&LightInputLayout)))
			{
				return false;
			}
		}

		if (FAILED(Device->CreateVertexShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &GouraudVS)))
		{
			return false;
		}
	}
	if (!GouraudPS)
	{
		auto Resource = FShaderResource::GetOrCompile(PSPath, "main", "ps_5_0", GouraudMacros);
		if (!Resource) return false;
		if (FAILED(Device->CreatePixelShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &GouraudPS)))
		{
			return false;
		}
	}

	// Lambert
	if (!LambertVS)
	{
		auto Resource = FShaderResource::GetOrCompile(VSPath, "main", "vs_5_0", LambertMacros);
		if (!Resource) return false;
		if (FAILED(Device->CreateVertexShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &LambertVS)))
		{
			return false;
		}
	}
	if (!LambertPS)
	{
		auto Resource = FShaderResource::GetOrCompile(PSPath, "main", "ps_5_0", LambertMacros);
		if (!Resource) return false;
		if (FAILED(Device->CreatePixelShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &LambertPS)))
		{
			return false;
		}
	}

	// Phong
	if (!PhongVS)
	{
		auto Resource = FShaderResource::GetOrCompile(VSPath, "main", "vs_5_0", PhongMacros);
		if (!Resource) return false;
		if (FAILED(Device->CreateVertexShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &PhongVS)))
		{
			return false;
		}
	}
	if (!PhongPS)
	{
		auto Resource = FShaderResource::GetOrCompile(PSPath, "main", "ps_5_0", PhongMacros);
		if (!Resource) return false;
		if (FAILED(Device->CreatePixelShader(
			Resource->GetBufferPointer(), Resource->GetBufferSize(),
			nullptr, &PhongPS)))
		{
			return false;
		}
	}

	return true;
}

bool FLightRenderFeature::Render(
	FRenderer& Renderer,
	const FSceneViewData& SceneViewData,
	const FSceneRenderTargets& Targets)
{
	if (!Targets.SceneColorRTV || !Targets.SceneDepthSRV)
	{
		return true;
	}
	
	if (!Initialize(Renderer))
	{
		return false;
	}

	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	UpdateLightConstantBuffer(Renderer, SceneViewData);

	DeviceContext->VSSetConstantBuffers(4, 1, &LightConstantBuffer);
	DeviceContext->PSSetConstantBuffers(4, 1, &LightConstantBuffer);

	DeviceContext->IASetInputLayout(LightInputLayout);

	switch (CurrentLightingModel)
	{
	case ELightingModel::Gouraud:
		DeviceContext->VSSetShader(GouraudVS, nullptr, 0);
		DeviceContext->PSSetShader(GouraudPS, nullptr, 0);
		break;
	case ELightingModel::Lambert:
		DeviceContext->VSSetShader(LambertVS, nullptr, 0);
		DeviceContext->PSSetShader(LambertPS, nullptr, 0);
		break;
	case ELightingModel::Phong:
	default:
		DeviceContext->VSSetShader(PhongVS, nullptr, 0);
		DeviceContext->PSSetShader(PhongPS, nullptr, 0);
		break;
	}

	DeviceContext->PSSetShaderResources(0, 1, &Targets.SceneDepthSRV);
	DeviceContext->PSSetSamplers(0, 1, &DepthSampler);

	return true;
}

void FLightRenderFeature::Release()
{
	if (GouraudVS) { GouraudVS->Release();        GouraudVS = nullptr; }
	if (GouraudPS) { GouraudPS->Release();        GouraudPS = nullptr; }
	if (LambertVS) { LambertVS->Release();        LambertVS = nullptr; }
	if (LambertPS) { LambertPS->Release();        LambertPS = nullptr; }
	if (PhongVS) { PhongVS->Release();          PhongVS = nullptr; }
	if (PhongPS) { PhongPS->Release();          PhongPS = nullptr; }
	if (LightInputLayout) { LightInputLayout->Release(); LightInputLayout = nullptr; }
	if (LightConstantBuffer) { LightConstantBuffer->Release();  LightConstantBuffer = nullptr; }
	if (LightBlendState) { LightBlendState->Release();      LightBlendState = nullptr; }
	if (NoDepthState) { NoDepthState->Release();         NoDepthState = nullptr; }
	if (LightRasterizerState) { LightRasterizerState->Release(); LightRasterizerState = nullptr; }
	if (DepthSampler) { DepthSampler->Release();         DepthSampler = nullptr; }
}
