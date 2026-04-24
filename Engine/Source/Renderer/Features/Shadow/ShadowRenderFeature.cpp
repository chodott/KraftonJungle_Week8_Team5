#include "ShadowRenderFeature.h"
#include "Renderer/Renderer.h"
#include "Component/SpotLightComponent.h"
#include "Math/MathUtility.h"
#include "Core/Engine.h"
#include <Core/Paths.h>
#include "Renderer/Resources/Shader/ShaderHandles.h" 
#include "Renderer/Resources/Shader/ShaderRegistry.h"
FShadowRenderFeature::~FShadowRenderFeature()
{
	Release();
}
bool FShadowRenderFeature::Initialize(FRenderer& Renderer)
{
	ID3D11Device* Device = Renderer.GetDevice();
	if (!Device)
		return false;
	if (!EnsureShadowMapResources(Renderer, 1024))
		return false;


	if (!ShadowPassCB)
	{
		D3D11_BUFFER_DESC CBDesc = {};
		CBDesc.Usage = D3D11_USAGE_DYNAMIC;
		CBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		CBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		// HLSL 상수 버퍼는 무조건 16바이트 정렬
		CBDesc.ByteWidth = (sizeof(FShadowPassConstantsGPU) + 15) & ~15;

		if (FAILED(Device->CreateBuffer(&CBDesc, nullptr, &ShadowPassCB)))
		{
			return false;
		}
	}
	if (!ShadowMatricesBuffer)
	{
		const uint32 MaxShadowLights = 1024; // 엔진의 MaxLocalLights와 동일하게 설정

		D3D11_BUFFER_DESC BufDesc = {};
		BufDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		BufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		BufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		BufDesc.StructureByteStride = sizeof(FMatrix); // HLSL의 float4x4 크기와 동일
		BufDesc.ByteWidth = sizeof(FMatrix) * MaxShadowLights;

		if (FAILED(Device->CreateBuffer(&BufDesc, nullptr, &ShadowMatricesBuffer)))
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = MaxShadowLights;

		if (FAILED(Device->CreateShaderResourceView(ShadowMatricesBuffer, &SRVDesc, &ShadowMatricesSRV)))
			return false;
	}
	if (!ShadowDepthVS)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage = EShaderStage::Vertex;
		Recipe.SourcePath = FPaths::ShaderDir().wstring() + L"SceneLighting/ShadowDepthVS.hlsl";
		Recipe.EntryPoint = "main";
		Recipe.Target = "vs_5_0";
		Recipe.LayoutType = EVertexLayoutType::MeshVertex; 

	
		auto VSHandle = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(Device, Recipe);
		if (VSHandle)
		{
			ShadowDepthVS = VSHandle; 
		}
		else
		{
			return false;
		}
	}
	return true;
}
void FShadowRenderFeature::Release()
{
	if(ShadowDSV)
	{
		ShadowDSV->Release();
		ShadowDSV = nullptr;
	}
	if (ShadowSRV)
	{
		ShadowSRV->Release();
		ShadowSRV = nullptr;
	}
	if (ShadowDepthTexture)
	{
		ShadowDepthTexture->Release();
		ShadowDepthTexture = nullptr;
	}

	if (ShadowRasterizerState)
	{
		ShadowRasterizerState->Release();
		ShadowRasterizerState = nullptr;
	}
	if (ShadowPassCB)
	{
		ShadowPassCB->Release();
		ShadowPassCB = nullptr;
	}
	ShadowDepthVS.reset();
	if (ShadowMatricesSRV)
	{
		ShadowMatricesSRV->Release();
		ShadowMatricesSRV = nullptr;
	}
	if (ShadowMatricesBuffer)
	{
		ShadowMatricesBuffer->Release();
		ShadowMatricesBuffer = nullptr;
	}
}

void FShadowRenderFeature::PrepareShadowViews(const FSceneViewData& SceneViewData)
{
	ShadowItems.clear();
	for (const FLocalLightRenderItem& Light : SceneViewData.LightingInputs.LocalLights)
	{
		if (Light.LightClass != ELightClass::Spot || (Light.Flags & /*CAST_SHADOW_FLAG*/1) == 0)
		{
			continue;
		}
		FShadowRenderItem Item;
		Item.LightPositionWS = Light.PositionWS;
		Item.LightDirectionWS = Light.DirectionWS;
		Item.FarZ = Light.Range;
		// privent zimberock
		FVector UpVector = FVector(0.0f, 1.0f, 0.0f);
		if (std::abs(Item.LightDirectionWS.Y) > 0.999f)
		{
			UpVector = FVector(0.0f, 0.0f, 1.0f);
		}

		Item.ViewMatrix = FMatrix::MakeLookAt(
			Item.LightPositionWS,
			Item.LightPositionWS + Item.LightDirectionWS,
			UpVector
		);
		// Projection Matrix
		float OuterAngleRad = std::acos(Light.OuterAngleCos);
		float FOV = OuterAngleRad * 2.0f;
		float AspectRatio = 1.0f;
		float NearZ = 10.f;
		Item.ProjectionMatrix = FMatrix::MakePerspectiveFovLH(FOV, AspectRatio, NearZ, Item.FarZ);

		Item.ViewProjectionMatrix = Item.ViewMatrix * Item.ProjectionMatrix;
		Item.Resolution = 1024;
		Item.DepthBias = 0.005f;
		Item.SlopeScaleDepthBias = 1.5f;

		ShadowItems.push_back(Item);
	}
	Stats.ActiveSpotShadows = static_cast<uint32>(ShadowItems.size());
	if (ShadowMatricesBuffer && !ShadowItems.empty())
	{
	
		
		ID3D11DeviceContext* DeviceContext = GEngine->GetRenderer()->GetDeviceContext();

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (SUCCEEDED(DeviceContext->Map(ShadowMatricesBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			FMatrix* MatricesGPU = static_cast<FMatrix*>(Mapped.pData);

			for (size_t i = 0; i < ShadowItems.size(); ++i)
			{
		
				MatricesGPU[i] = ShadowItems[i].ViewProjectionMatrix.GetTransposed();
			}

			DeviceContext->Unmap(ShadowMatricesBuffer, 0);
		}
	}
}


//Render Setup 라이트 시점에서의 Depth Buffer 렌더링
bool FShadowRenderFeature::RenderDepthPass(FRenderer& Renderer, const FSceneViewData& SceneViewData)
{
	if (ShadowItems.empty()) 
		return true;
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	// 1. Rasterizer State 
	if (!ShadowRasterizerState)
	{
		D3D11_RASTERIZER_DESC RSDesc = {};
		RSDesc.FillMode = D3D11_FILL_SOLID;
		RSDesc.CullMode = D3D11_CULL_FRONT; 
		RSDesc.DepthClipEnable = true;
		Renderer.GetDevice()->CreateRasterizerState(&RSDesc, &ShadowRasterizerState);
	}
	DeviceContext->RSSetState(ShadowRasterizerState);

	// Viewport setup shadow map resolution
	D3D11_VIEWPORT Viewport = {};
	Viewport.Width = static_cast<float>(ShadowItems[0].Resolution);
	Viewport.Height = static_cast<float>(ShadowItems[0].Resolution);
	Viewport.MinDepth = 0.0f;
	Viewport.MaxDepth = 1.0f;
	Viewport.TopLeftX = 0;
	Viewport.TopLeftY = 0;
	DeviceContext->RSSetViewports(1, &Viewport);

	DeviceContext->ClearDepthStencilView(ShadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
	ID3D11RenderTargetView* NullRTV = nullptr;
	DeviceContext->OMSetRenderTargets(1, &NullRTV, ShadowDSV);

	if (ShadowDepthVS)
	{
		ShadowDepthVS->Bind(DeviceContext);
	}

	DeviceContext->PSSetShader(nullptr, nullptr, 0);// need only depth, so no pixel shader
	for (const auto& ShadowItem : ShadowItems)
	{
	
		FShadowPassConstantsGPU CBData;
		CBData.LightViewProj = ShadowItem.ViewProjectionMatrix.GetTransposed();
		CBData.ShadowParams = FVector4(ShadowItem.DepthBias, ShadowItem.SlopeScaleDepthBias, ShadowItem.FarZ, 0.0f);

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (SUCCEEDED(DeviceContext->Map(ShadowPassCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			memcpy(Mapped.pData, &CBData, sizeof(FShadowPassConstantsGPU));
			DeviceContext->Unmap(ShadowPassCB, 0);
		}
		DeviceContext->VSSetConstantBuffers(1, 1, &ShadowPassCB);

		for (const FMeshBatch& Batch : SceneViewData.MeshInputs.Batches)
		{
			if (Batch.Domain != EMaterialDomain::Opaque)
			{
				continue;
			}
			if (Batch.bDisableDepthWrite || !Batch.Mesh)
			{
				continue;
			}

			Renderer.UpdateObjectConstantBuffer(Batch);

			Batch.Mesh->Bind(DeviceContext);

			DeviceContext->IASetPrimitiveTopology(static_cast<D3D11_PRIMITIVE_TOPOLOGY>(Batch.Mesh->Topology));

			DeviceContext->DrawIndexed(Batch.IndexCount, Batch.IndexStart, 0);
		}
	}
	DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	DeviceContext->RSSetState(nullptr); // 기본 래스터라이저로 복구

	return true;
}

bool FShadowRenderFeature::EnsureShadowMapResources(FRenderer& Renderer, uint32 Resolution)
{
	ID3D11Device* Device = Renderer.GetDevice();

	if (ShadowDepthTexture)
		return true;

	D3D11_TEXTURE2D_DESC TexDesc = {};
	TexDesc.Width = Resolution;
	TexDesc.Height = Resolution;
	TexDesc.MipLevels = 1;
	TexDesc.ArraySize = 1;
	TexDesc.Format = DXGI_FORMAT_R32_TYPELESS; // DSV와 SRV 양쪽에서 쓰기 위함
	TexDesc.SampleDesc.Count = 1;
	TexDesc.Usage = D3D11_USAGE_DEFAULT;
	TexDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&TexDesc, nullptr, &ShadowDepthTexture)))
		return false;
	// generate Dsv
	D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
	DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
	DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	if (FAILED(Device->CreateDepthStencilView(ShadowDepthTexture, &DSVDesc, &ShadowDSV)))
		return false;

	// generate SRV
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MipLevels = 1;
	if (FAILED(Device->CreateShaderResourceView(ShadowDepthTexture, &SRVDesc, &ShadowSRV)))
		return false;
	Stats.TotalShadowMapMemoryBytes = Resolution * Resolution * 4;
	return true;
}
