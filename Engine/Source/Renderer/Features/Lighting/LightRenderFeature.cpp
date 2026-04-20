#include "Renderer/Features/Lighting/LightRenderFeature.h"

#include <algorithm>
#include <cstring>

#include "Core/Paths.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene/SceneViewData.h"
#include "Renderer/Resources/Shader/ShaderResource.h"

namespace
{
	template <typename T>
	void SafeRelease(T*& Ptr)
	{
		if (Ptr)
		{
			Ptr->Release();
			Ptr = nullptr;
		}
	}

	UINT Align16(UINT Value)
	{
		return (Value + 15u) & ~15u;
	}

	bool UploadDynamicBuffer(
		ID3D11DeviceContext* DeviceContext,
		ID3D11Buffer*        Buffer,
		const void*          Data,
		size_t               SizeInBytes)
	{
		if (!DeviceContext || !Buffer)
		{
			return false;
		}

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (FAILED(DeviceContext->Map(Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			return false;
		}

		memcpy(Mapped.pData, Data, SizeInBytes);
		DeviceContext->Unmap(Buffer, 0);
		return true;
	}
}

FLightRenderFeature::~FLightRenderFeature()
{
	Release();
}

bool FLightRenderFeature::PrepareClusteredLightResources(
	FRenderer&                 Renderer,
	const FSceneViewData&      SceneViewData,
	const FSceneRenderTargets& Targets)
{
	if (!Initialize(Renderer))
	{
		return false;
	}

	UpdateClusterGlobalConstantBuffer(Renderer, SceneViewData);
	UploadLocalLightBuffers(Renderer, SceneViewData);

	if (SceneViewData.RenderMode == ERenderMode::Unlit)
	{
		return true;
	}

	if (!Targets.SceneDepthSRV)
	{
		return false;
	}

	const uint32     ViewportWidth  = static_cast<uint32>((std::max)(SceneViewData.View.Viewport.Width, 0.0f));
	const uint32     ViewportHeight = static_cast<uint32>((std::max)(SceneViewData.View.Viewport.Height, 0.0f));
	const uint32     ClusterCountX  = (ViewportWidth + LightCullingConfig::TileSizeX - 1u) / LightCullingConfig::TileSizeX;
	const uint32     ClusterCountY  = (ViewportHeight + LightCullingConfig::TileSizeY - 1u) / LightCullingConfig::TileSizeY;
	constexpr uint32 ClusterCountZ  = LightCullingConfig::ClusterCountZ;
	const uint32     ClusterCount   = ClusterCountX * ClusterCountY * ClusterCountZ;
	const uint32     TotalMaskWords = ClusterCount * LightMaskConfig::MaskWordCount;

	if (!EnsureDefaultStructuredBufferSRVUAV(
		Renderer,
		sizeof(uint32),
		(std::max)(1u, TotalMaskWords),
		ClusterLightMaskBuffer,
		ClusterLightMaskSRV,
		ClusterLightMaskUAV))
	{
		return false;
	}

	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	constexpr UINT ClearValues[4] = { 0, 0, 0, 0 };
	DeviceContext->ClearUnorderedAccessViewUint(ClusterLightMaskUAV, ClearValues);

	DeviceContext->CSSetShader(LightCullingCS, nullptr, 0);
	DeviceContext->CSSetConstantBuffers(LightClusterSlots::ClusterGlobalCB, 1, &ClusterGlobalConstantBuffer);

	ID3D11ShaderResourceView* CSRVs[2] = { LightCullProxySRV, Targets.SceneDepthSRV };
	DeviceContext->CSSetShaderResources(0, 2, CSRVs);

	ID3D11UnorderedAccessView* CSUAVs[1]           = { ClusterLightMaskUAV };
	UINT                       UAVInitialCounts[1] = { 0u };
	DeviceContext->CSSetUnorderedAccessViews(0, 1, CSUAVs, UAVInitialCounts);

	DeviceContext->Dispatch(ClusterCountX, ClusterCountY, ClusterCountZ);

	ID3D11ShaderResourceView*  NullSRV[2] = { nullptr, nullptr };
	ID3D11UnorderedAccessView* NullUAV[1] = { nullptr };
	ID3D11Buffer*              NullCB[1]  = { nullptr };

	DeviceContext->CSSetShaderResources(0, 2, NullSRV);
	DeviceContext->CSSetUnorderedAccessViews(0, 1, NullUAV, UAVInitialCounts);
	DeviceContext->CSSetConstantBuffers(LightClusterSlots::ClusterGlobalCB, 1, NullCB);
	DeviceContext->CSSetShader(nullptr, nullptr, 0);

	return true;
}

bool FLightRenderFeature::Render(
	FRenderer&                 Renderer,
	const FSceneViewData&      SceneViewData,
	const FSceneRenderTargets& Targets)
{
	if (!Initialize(Renderer))
	{
		return false;
	}

	UpdateGlobalLightConstantBuffer(Renderer, SceneViewData);

	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return false;
	}

	DeviceContext->VSSetConstantBuffers(LightClusterSlots::GlobalLightCB, 1, &GlobalLightConstantBuffer);
	DeviceContext->PSSetConstantBuffers(LightClusterSlots::GlobalLightCB, 1, &GlobalLightConstantBuffer);
	DeviceContext->VSSetConstantBuffers(LightClusterSlots::ClusterGlobalCB, 1, &ClusterGlobalConstantBuffer);
	DeviceContext->PSSetConstantBuffers(LightClusterSlots::ClusterGlobalCB, 1, &ClusterGlobalConstantBuffer);

	if (ObjectLightMaskSRV)
	{
		DeviceContext->VSSetShaderResources(LightClusterSlots::ObjectLightMaskSRV, 1, &ObjectLightMaskSRV);
	}
	if (LocalLightSRV)
	{
		DeviceContext->VSSetShaderResources(LightClusterSlots::LocalLightSRV, 1, &LocalLightSRV);
		DeviceContext->PSSetShaderResources(LightClusterSlots::LocalLightSRV, 1, &LocalLightSRV);
	}

	if (ClusterLightMaskSRV)
	{
		DeviceContext->PSSetShaderResources(LightClusterSlots::ClusterLightMaskSRV, 1, &ClusterLightMaskSRV);
	}

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

	return true;
}

void FLightRenderFeature::Release()
{
	SafeRelease(GlobalLightConstantBuffer);
	SafeRelease(ClusterGlobalConstantBuffer);

	SafeRelease(LocalLightSRV);
	SafeRelease(LocalLightBuffer);

	SafeRelease(LightCullProxySRV);
	SafeRelease(LightCullProxyBuffer);

	SafeRelease(ObjectLightMaskSRV);
	SafeRelease(ObjectLightMaskBuffer);

	SafeRelease(ClusterLightMaskUAV);
	SafeRelease(ClusterLightMaskSRV);
	SafeRelease(ClusterLightMaskBuffer);

	SafeRelease(LightCullingCS);

	SafeRelease(GouraudVS);
	SafeRelease(GouraudPS);
	SafeRelease(LambertVS);
	SafeRelease(LambertPS);
	SafeRelease(PhongVS);
	SafeRelease(PhongPS);
	SafeRelease(LightInputLayout);
	SafeRelease(DepthSampler);
}

ID3D11VertexShader* FLightRenderFeature::GetCurrentVS() const
{
	switch (CurrentLightingModel)
	{
	case ELightingModel::Gouraud: return GouraudVS;
	case ELightingModel::Lambert: return LambertVS;
	case ELightingModel::Phong:
	default: return PhongVS;
	}
}

ID3D11PixelShader* FLightRenderFeature::GetCurrentPS() const
{
	switch (CurrentLightingModel)
	{
	case ELightingModel::Gouraud: return GouraudPS;
	case ELightingModel::Lambert: return LambertPS;
	case ELightingModel::Phong:
	default: return PhongPS;
	}
}

bool FLightRenderFeature::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	if (!CompileShaderVariants(Renderer))
	{
		return false;
	}

	if (!GlobalLightConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage             = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth         = Align16(sizeof(FGlobalLightConstantBuffer));

		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &GlobalLightConstantBuffer)))
		{
			return false;
		}
	}

	if (!ClusterGlobalConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.Usage             = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		Desc.ByteWidth         = Align16(sizeof(FLightClusterGlobalCB));

		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &ClusterGlobalConstantBuffer)))
		{
			return false;
		}
	}

	if (!LightCullingCS)
	{
		const std::wstring CSPath   = FPaths::ShaderDir().wstring() + L"SceneLighting/LightCullingCS.hlsl";
		auto               Resource = FShaderResource::GetOrCompile(CSPath.c_str(), "main", "cs_5_0", nullptr);
		if (!Resource)
		{
			return false;
		}

		if (FAILED(Device->CreateComputeShader(
			Resource->GetBufferPointer(),
			Resource->GetBufferSize(),
			nullptr,
			&LightCullingCS)))
		{
			return false;
		}
	}

	if (!DepthSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_POINT;
		SamplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.ComparisonFunc     = D3D11_COMPARISON_NEVER;
		SamplerDesc.MinLOD             = 0.0f;
		SamplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;

		if (FAILED(Device->CreateSamplerState(&SamplerDesc, &DepthSampler)))
		{
			return false;
		}
	}

	return true;
}

bool FLightRenderFeature::CompileShaderVariants(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	const std::wstring VSPath = FPaths::ShaderDir().wstring() + L"SceneLighting/UberLitVertexShader.hlsl";
	const std::wstring PSPath = FPaths::ShaderDir().wstring() + L"SceneLighting/UberLitPixelShader.hlsl";

	D3D_SHADER_MACRO GouraudMacros[] = { { "LIGHTING_MODEL_GOURAUD", "1" }, { nullptr, nullptr } };
	D3D_SHADER_MACRO LambertMacros[] = { { "LIGHTING_MODEL_LAMBERT", "1" }, { nullptr, nullptr } };
	D3D_SHADER_MACRO PhongMacros[]   = { { "LIGHTING_MODEL_PHONG", "1" }, { nullptr, nullptr } };

	if (!GouraudVS)
	{
		auto Resource = FShaderResource::GetOrCompile(VSPath.c_str(), "main", "vs_5_0", GouraudMacros);
		if (!Resource)
		{
			return false;
		}

		if (!LightInputLayout)
		{
			const D3D11_INPUT_ELEMENT_DESC InputDesc[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
			Resource->GetBufferPointer(),
			Resource->GetBufferSize(),
			nullptr,
			&GouraudVS)))
		{
			return false;
		}
	}

	if (!GouraudPS)
	{
		auto Resource = FShaderResource::GetOrCompile(PSPath.c_str(), "main", "ps_5_0", GouraudMacros);
		if (!Resource)
		{
			return false;
		}

		if (FAILED(Device->CreatePixelShader(
			Resource->GetBufferPointer(),
			Resource->GetBufferSize(),
			nullptr,
			&GouraudPS)))
		{
			return false;
		}
	}

	if (!LambertVS)
	{
		auto Resource = FShaderResource::GetOrCompile(VSPath.c_str(), "main", "vs_5_0", LambertMacros);
		if (!Resource)
		{
			return false;
		}

		if (FAILED(Device->CreateVertexShader(
			Resource->GetBufferPointer(),
			Resource->GetBufferSize(),
			nullptr,
			&LambertVS)))
		{
			return false;
		}
	}

	if (!LambertPS)
	{
		auto Resource = FShaderResource::GetOrCompile(PSPath.c_str(), "main", "ps_5_0", LambertMacros);
		if (!Resource)
		{
			return false;
		}

		if (FAILED(Device->CreatePixelShader(
			Resource->GetBufferPointer(),
			Resource->GetBufferSize(),
			nullptr,
			&LambertPS)))
		{
			return false;
		}
	}

	if (!PhongVS)
	{
		auto Resource = FShaderResource::GetOrCompile(VSPath.c_str(), "main", "vs_5_0", PhongMacros);
		if (!Resource)
		{
			return false;
		}

		if (FAILED(Device->CreateVertexShader(
			Resource->GetBufferPointer(),
			Resource->GetBufferSize(),
			nullptr,
			&PhongVS)))
		{
			return false;
		}
	}

	if (!PhongPS)
	{
		auto Resource = FShaderResource::GetOrCompile(PSPath.c_str(), "main", "ps_5_0", PhongMacros);
		if (!Resource)
		{
			return false;
		}

		if (FAILED(Device->CreatePixelShader(
			Resource->GetBufferPointer(),
			Resource->GetBufferSize(),
			nullptr,
			&PhongPS)))
		{
			return false;
		}
	}

	return true;
}

void FLightRenderFeature::UpdateGlobalLightConstantBuffer(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	FGlobalLightConstantBuffer CB = {};

	CB.Ambient.ColorIntensity = FVector4(
		SceneViewData.LightingInputs.Ambient.Color.X,
		SceneViewData.LightingInputs.Ambient.Color.Y,
		SceneViewData.LightingInputs.Ambient.Color.Z,
		SceneViewData.LightingInputs.Ambient.Intensity);

	CB.AmbientEnabled = SceneViewData.LightingInputs.Ambient.Intensity > 0.0f ? 1u : 0u;

	if (!SceneViewData.LightingInputs.DirectionalLights.empty())
	{
		const FDirectionalLightRenderItem& Dir     = SceneViewData.LightingInputs.DirectionalLights[0];
		const FVector                      SafeDir = Dir.DirectionWS.GetSafeNormal();

		CB.Directional.ColorIntensity = FVector4(Dir.Color.X, Dir.Color.Y, Dir.Color.Z, Dir.Intensity);
		CB.Directional.DirectionEtc   = FVector4(SafeDir.X, SafeDir.Y, SafeDir.Z, 0.0f);
		CB.DirectionalLightCount      = 1;
	}
	else
	{
		CB.DirectionalLightCount = 0;
	}

	UploadDynamicBuffer(
		Renderer.GetDeviceContext(),
		GlobalLightConstantBuffer,
		&CB,
		sizeof(CB));
}

void FLightRenderFeature::UpdateClusterGlobalConstantBuffer(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	FLightClusterGlobalCB CB = {};
	CB.View                  = SceneViewData.View.View.GetTransposed();
	CB.Projection            = SceneViewData.View.Projection.GetTransposed();
	CB.InverseProjection     = SceneViewData.View.InverseProjection.GetTransposed();
	CB.InverseView           = SceneViewData.View.View.GetInverse().GetTransposed();

	CB.ClusterCameraPosition = FVector4(
		SceneViewData.View.CameraPosition.X,
		SceneViewData.View.CameraPosition.Y,
		SceneViewData.View.CameraPosition.Z,
		1.0f);

	const float Width  = SceneViewData.View.Viewport.Width;
	const float Height = SceneViewData.View.Viewport.Height;

	CB.ScreenParams = FVector4(
		Width,
		Height,
		Width > 0.0f ? 1.0f / Width : 0.0f,
		Height > 0.0f ? 1.0f / Height : 0.0f);

	const uint32 ViewportWidthPx  = static_cast<uint32>((std::max)(SceneViewData.View.Viewport.Width, 0.0f));
	const uint32 ViewportHeightPx = static_cast<uint32>((std::max)(SceneViewData.View.Viewport.Height, 0.0f));

	CB.ClusterCountX   = (ViewportWidthPx + LightCullingConfig::TileSizeX - 1u) / LightCullingConfig::TileSizeX;
	CB.ClusterCountY   = (ViewportHeightPx + LightCullingConfig::TileSizeY - 1u) / LightCullingConfig::TileSizeY;
	CB.ClusterCountZ   = LightCullingConfig::ClusterCountZ;
	CB.LocalLightCount = (std::min)(static_cast<uint32>(SceneViewData.LightingInputs.LocalLights.size()), LightMaskConfig::MaxLocalLights);

	CB.DirectionalLightCount = static_cast<uint32>(SceneViewData.LightingInputs.DirectionalLights.size());
	CB.LightMaskWordCount    = LightMaskConfig::MaskWordCount;
	CB.LightingEnabled       = SceneViewData.RenderMode != ERenderMode::Unlit ? 1u : 0u;

	CB.NearZ = (std::max)(SceneViewData.View.NearZ, 1e-4f);
	CB.FarZ  = (std::max)(SceneViewData.View.FarZ, CB.NearZ + 1e-3f);

	const float LogFarNear = logf(CB.FarZ / CB.NearZ);
	CB.LogZScale           = static_cast<float>(CB.ClusterCountZ) / LogFarNear;
	CB.LogZBias            = -logf(CB.NearZ) * CB.LogZScale;

	UploadDynamicBuffer(
		Renderer.GetDeviceContext(),
		ClusterGlobalConstantBuffer,
		&CB,
		sizeof(CB));
}

void FLightRenderFeature::UploadLocalLightBuffers(
	FRenderer&            Renderer,
	const FSceneViewData& SceneViewData)
{
	TArray<FLocalLightGPU>     LocalLightsGPU;
	TArray<FLightCullProxyGPU> ProxiesGPU;
	TArray<uint32>             ObjectMaskWordsGPU = SceneViewData.LightingInputs.ObjectLightMaskWords;

	LocalLightsGPU.reserve(SceneViewData.LightingInputs.LocalLights.size());
	ProxiesGPU.reserve(SceneViewData.LightingInputs.LocalLights.size());

	for (const FLocalLightRenderItem& Src : SceneViewData.LightingInputs.LocalLights)
	{
		FLocalLightGPU L = {};
		L.ColorIntensity = FVector4(Src.Color.X, Src.Color.Y, Src.Color.Z, Src.Intensity);
		L.PositionRange  = FVector4(Src.PositionWS.X, Src.PositionWS.Y, Src.PositionWS.Z, Src.Range);
		L.DirectionType  = FVector4(Src.DirectionWS.X, Src.DirectionWS.Y, Src.DirectionWS.Z, static_cast<float>(Src.LightClass));
		L.AngleParams    = FVector4(Src.InnerAngleCos, Src.OuterAngleCos, Src.FalloffExponent, 0.0f);
		L.Axis0Extent    = FVector4(Src.Axis0.X, Src.Axis0.Y, Src.Axis0.Z, Src.Extent0);
		L.Axis1Extent    = FVector4(Src.Axis1.X, Src.Axis1.Y, Src.Axis1.Z, Src.Extent1);
		L.Axis2Extent    = FVector4(Src.Axis2.X, Src.Axis2.Y, Src.Axis2.Z, Src.Extent2);
		L.Flags          = Src.Flags;
		L.ShadowIndex    = Src.ShadowIndex;
		L.CookieIndex    = Src.CookieIndex;
		L.IESIndex       = Src.IESIndex;

		const uint32 LightIndex = static_cast<uint32>(LocalLightsGPU.size());
		LocalLightsGPU.push_back(L);

		FLightCullProxyGPU P = {};
		P.CullCenterRadius   = FVector4(Src.CullCenterWS.X, Src.CullCenterWS.Y, Src.CullCenterWS.Z, Src.CullRadius);
		P.PositionRange      = L.PositionRange;
		P.DirectionType      = L.DirectionType;
		P.AngleParams        = L.AngleParams;
		P.Axis0Extent        = L.Axis0Extent;
		P.Axis1Extent        = L.Axis1Extent;
		P.Axis2Extent        = L.Axis2Extent;
		P.Flags              = Src.Flags;
		P.LightIndex         = LightIndex;
		P.CullShapeType      = static_cast<uint32>(Src.CullShape);

		ProxiesGPU.push_back(P);
	}

	if (LocalLightsGPU.empty())
	{
		LocalLightsGPU.push_back(FLocalLightGPU {});
	}
	if (ProxiesGPU.empty())
	{
		ProxiesGPU.push_back(FLightCullProxyGPU {});
	}
	if (ObjectMaskWordsGPU.empty())
	{
		ObjectMaskWordsGPU.push_back(0u);
	}

	EnsureDynamicStructuredBufferSRV(
		Renderer,
		sizeof(FLocalLightGPU),
		static_cast<uint32>(LocalLightsGPU.size()),
		LocalLightBuffer,
		LocalLightSRV);

	EnsureDynamicStructuredBufferSRV(
		Renderer,
		sizeof(FLightCullProxyGPU),
		static_cast<uint32>(ProxiesGPU.size()),
		LightCullProxyBuffer,
		LightCullProxySRV);

	EnsureDynamicStructuredBufferSRV(
		Renderer,
		sizeof(uint32),
		static_cast<uint32>(ObjectMaskWordsGPU.size()),
		ObjectLightMaskBuffer,
		ObjectLightMaskSRV);

	UploadDynamicBuffer(
		Renderer.GetDeviceContext(),
		LocalLightBuffer,
		LocalLightsGPU.data(),
		sizeof(FLocalLightGPU) * LocalLightsGPU.size());

	UploadDynamicBuffer(
		Renderer.GetDeviceContext(),
		LightCullProxyBuffer,
		ProxiesGPU.data(),
		sizeof(FLightCullProxyGPU) * ProxiesGPU.size());

	UploadDynamicBuffer(
		Renderer.GetDeviceContext(),
		ObjectLightMaskBuffer,
		ObjectMaskWordsGPU.data(),
		sizeof(uint32) * ObjectMaskWordsGPU.size());
}

bool FLightRenderFeature::EnsureDynamicStructuredBufferSRV(
	FRenderer&                 Renderer,
	uint32                     ElementStride,
	uint32                     ElementCount,
	ID3D11Buffer*&             Buffer,
	ID3D11ShaderResourceView*& SRV)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	const uint32 SafeCount = (std::max)(1u, ElementCount);
	const UINT   ByteWidth = ElementStride * SafeCount;

	bool bRecreate = !Buffer || !SRV;
	if (!bRecreate)
	{
		D3D11_BUFFER_DESC Desc = {};
		Buffer->GetDesc(&Desc);

		if (Desc.ByteWidth < ByteWidth ||
			Desc.StructureByteStride != ElementStride ||
			Desc.Usage != D3D11_USAGE_DYNAMIC ||
			Desc.BindFlags != D3D11_BIND_SHADER_RESOURCE)
		{
			bRecreate = true;
		}
	}

	if (!bRecreate)
	{
		return true;
	}

	SafeRelease(SRV);
	SafeRelease(Buffer);

	D3D11_BUFFER_DESC Desc   = {};
	Desc.ByteWidth           = ByteWidth;
	Desc.Usage               = D3D11_USAGE_DYNAMIC;
	Desc.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
	Desc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
	Desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	Desc.StructureByteStride = ElementStride;

	if (FAILED(Device->CreateBuffer(&Desc, nullptr, &Buffer)))
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement             = 0;
	SRVDesc.Buffer.NumElements              = SafeCount;

	if (FAILED(Device->CreateShaderResourceView(Buffer, &SRVDesc, &SRV)))
	{
		SafeRelease(Buffer);
		return false;
	}

	return true;
}

bool FLightRenderFeature::EnsureDefaultStructuredBufferSRVUAV(
	FRenderer&                  Renderer,
	uint32                      ElementStride,
	uint32                      ElementCount,
	ID3D11Buffer*&              Buffer,
	ID3D11ShaderResourceView*&  SRV,
	ID3D11UnorderedAccessView*& UAV)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
	{
		return false;
	}

	const uint32 SafeCount = (std::max)(1u, ElementCount);
	const UINT   ByteWidth = ElementStride * SafeCount;

	bool bRecreate = !Buffer || !SRV || !UAV;
	if (!bRecreate)
	{
		D3D11_BUFFER_DESC Desc = {};
		Buffer->GetDesc(&Desc);

		if (Desc.ByteWidth < ByteWidth ||
			Desc.StructureByteStride != ElementStride ||
			Desc.Usage != D3D11_USAGE_DEFAULT ||
			Desc.BindFlags != (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS))
		{
			bRecreate = true;
		}
	}

	if (!bRecreate)
	{
		return true;
	}

	SafeRelease(UAV);
	SafeRelease(SRV);
	SafeRelease(Buffer);

	D3D11_BUFFER_DESC Desc   = {};
	Desc.ByteWidth           = ByteWidth;
	Desc.Usage               = D3D11_USAGE_DEFAULT;
	Desc.BindFlags           = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	Desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	Desc.StructureByteStride = ElementStride;

	if (FAILED(Device->CreateBuffer(&Desc, nullptr, &Buffer)))
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format                          = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement             = 0;
	SRVDesc.Buffer.NumElements              = SafeCount;

	if (FAILED(Device->CreateShaderResourceView(Buffer, &SRVDesc, &SRV)))
	{
		SafeRelease(Buffer);
		return false;
	}

	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.Format                           = DXGI_FORMAT_UNKNOWN;
	UAVDesc.ViewDimension                    = D3D11_UAV_DIMENSION_BUFFER;
	UAVDesc.Buffer.FirstElement              = 0;
	UAVDesc.Buffer.NumElements               = SafeCount;
	UAVDesc.Buffer.Flags                     = 0;

	if (FAILED(Device->CreateUnorderedAccessView(Buffer, &UAVDesc, &UAV)))
	{
		SafeRelease(SRV);
		SafeRelease(Buffer);
		return false;
	}

	return true;
}
