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
	if (!EnsurePointShadowMapResources(Renderer, 512))
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
		BufDesc.StructureByteStride = sizeof(FShadowDataGPU);
		BufDesc.ByteWidth = sizeof(FShadowDataGPU) * MaxShadowLights;

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
	if (!PointShadowMatricesBuffer)
	{
		const uint32 NumPointShadowSlots = MaxPointShadows * 6;

		D3D11_BUFFER_DESC BufDesc = {};
		BufDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		BufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		BufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		BufDesc.StructureByteStride = sizeof(FShadowDataGPU);
		BufDesc.ByteWidth = sizeof(FShadowDataGPU) * NumPointShadowSlots;

		if (FAILED(Device->CreateBuffer(&BufDesc, nullptr, &PointShadowMatricesBuffer)))
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = NumPointShadowSlots;

		if (FAILED(Device->CreateShaderResourceView(
			PointShadowMatricesBuffer, &SRVDesc, &PointShadowMatricesSRV)))
			return false;
	}
	if (!ShadowDepthVS)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage = EShaderStage::Vertex;
		Recipe.SourcePath = FPaths::ShaderDir().wstring() + L"SceneShadow/ShadowDepthVS.hlsl";
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
	for (uint32 i = 0; i < static_cast<uint32>(ShadowDSVs.size()); i++)
	{
		if (ShadowDSVs[i])
		{
			ShadowDSVs[i]->Release();
			ShadowDSVs[i] = nullptr;
		}
	}
	ShadowDSVs.clear();
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
	for (uint32 i = 0; i < static_cast<uint32>(PointShadowDSVs.size()); i++)
	{
		if (PointShadowDSVs[i]) 
		{
			PointShadowDSVs[i]->Release();
			PointShadowDSVs[i] = nullptr;
		}
	}
	PointShadowDSVs.clear();
	if (PointShadowSRV)
	{
		PointShadowSRV->Release();
		PointShadowSRV = nullptr;
	}
	if (PointShadowCubeArray)
	{
		PointShadowCubeArray->Release();
		PointShadowCubeArray = nullptr;
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
	if (PointShadowMatricesSRV)
	{
		PointShadowMatricesSRV->Release();
		PointShadowMatricesSRV = nullptr;
	}
	if (PointShadowMatricesBuffer)
	{
		PointShadowMatricesBuffer->Release();
		PointShadowMatricesBuffer = nullptr;
	}
	if (ShadowMatricesBuffer)
	{
		ShadowMatricesBuffer->Release();
		ShadowMatricesBuffer = nullptr;
	}
}

void FShadowRenderFeature::BuildSpotShadowItem(const FLocalLightRenderItem& Light)
{

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

	Item.ViewMatrix = FMatrix::MakeViewLookAtLH(
		Item.LightPositionWS,
		Item.LightPositionWS + Item.LightDirectionWS,
		UpVector
	);
	// Projection Matrix
	float OuterAngleRad = std::acos(Light.OuterAngleCos);
	float FOV = OuterAngleRad * 2.0f;
	float AspectRatio = 1.0f;
	float NearZ = 1.f;
	Item.ProjectionMatrix = FMatrix::MakePerspectiveFovLH(FOV, AspectRatio, NearZ, Item.FarZ);

	Item.ViewProjectionMatrix = Item.ViewMatrix * Item.ProjectionMatrix;
	Item.Resolution = 1024;
	Item.DepthBias = 0.00f;
	Item.SlopeScaleDepthBias = 1.5f;

	ShadowItems.push_back(Item);


}
void FShadowRenderFeature::BuildPointShadowItems(const FLocalLightRenderItem& InLight)
{
	static const FVector CubeFaceLook[6] = {
		{  1,  0,  0 }, { -1,  0,  0 },
		{  0,  1,  0 }, {  0, -1,  0 },
		{  0,  0,  1 }, {  0,  0, -1 },
	};
	static const FVector CubeFaceUp[6] = {
		{ 0, 1,  0 }, { 0, 1,  0 },
		{ 0, 0, -1 }, { 0, 0,  1 },
		{ 0, 1,  0 }, { 0, 1,  0 },
	};
	const float NearZ = 0.1f;
	const float FarZ = InLight.Range;
	const FMatrix Proj = FMatrix::MakePerspectiveFovLH(
		FMath::DegreesToRadians(90.0f), 1.0f, NearZ, FarZ);

	for (uint32 F = 0; F < 6; F++)
	{
		FShadowRenderItem Item;
		Item.LightPositionWS = InLight.PositionWS;
		Item.LightDirectionWS = CubeFaceLook[F];
		Item.FarZ = FarZ;
		Item.FaceIndex = F;
		Item.bIsPointLight = 1;
		Item.Resolution = 512;
		Item.DepthBias = 0.000f; 
		Item.SlopeScaleDepthBias = 1.0f;

		Item.ViewMatrix = FMatrix::MakeViewLookAtLH(
			InLight.PositionWS,
			InLight.PositionWS + CubeFaceLook[F],
			CubeFaceUp[F]);

		Item.ProjectionMatrix = Proj;
		Item.ViewProjectionMatrix = Item.ViewMatrix * Proj;

		PointShadowItems.push_back(Item);
	}
}
void FShadowRenderFeature::UploadSpotShadowMatrices()
{
	if (!ShadowMatricesBuffer || ShadowItems.empty())
		return;

	ID3D11DeviceContext* DC = GEngine->GetRenderer()->GetDeviceContext();
	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DC->Map(ShadowMatricesBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		FShadowDataGPU* Data = static_cast<FShadowDataGPU*>(Mapped.pData);
		for (size_t i = 0; i < ShadowItems.size(); ++i)
		{
			Data[i].ViewProj = ShadowItems[i].ViewProjectionMatrix.GetTransposed();
			Data[i].DepthBias = ShadowItems[i].DepthBias;
			Data[i].Pad0 = Data[i].Pad1 = Data[i].Pad2 = 0.0f;
		}
		DC->Unmap(ShadowMatricesBuffer, 0);
	}
}
void FShadowRenderFeature::UploadPointShadowMatrices()
{
	if (!PointShadowMatricesBuffer || PointShadowItems.empty())
		return;

	ID3D11DeviceContext* DC = GEngine->GetRenderer()->GetDeviceContext();
	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(DC->Map(PointShadowMatricesBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		// 라이트 L의 면 F는 인덱스 L*6 + F에 저장
		FShadowDataGPU* Data = static_cast<FShadowDataGPU*>(Mapped.pData);
		for (size_t i = 0; i < PointShadowItems.size(); ++i)
		{
			Data[i].ViewProj = PointShadowItems[i].ViewProjectionMatrix.GetTransposed();
			Data[i].DepthBias = PointShadowItems[i].DepthBias;
			Data[i].Pad0 = Data[i].Pad1 = Data[i].Pad2 = 0.0f;
		}
		DC->Unmap(PointShadowMatricesBuffer, 0);
	}
}
void FShadowRenderFeature::PrepareShadowViews(const FSceneViewData& SceneViewData)
{
	ShadowItems.clear();
	PointShadowItems.clear();
	for (const FLocalLightRenderItem& Light : SceneViewData.LightingInputs.LocalLights)
	{

		if ((Light.Flags & /*CAST_SHADOW_FLAG*/1) == 0)
			continue;

		if (Light.LightClass == ELightClass::Spot)
		{
			if (ShadowItems.size() >= MaxShadowedLights)
				continue;
			BuildSpotShadowItem(Light);
		}
		else if (Light.LightClass == ELightClass::Point)
		{
			if (PointShadowItems.size() >= MaxPointShadows * 6)
				continue;
			BuildPointShadowItems(Light);  // 6개 push
		}
	

	}
	Stats.ActiveSpotShadows = static_cast<uint32>(ShadowItems.size());
	Stats.ActivePointShadowFaces = static_cast<uint32>(PointShadowItems.size() / 6);

	UploadSpotShadowMatrices();
	UploadPointShadowMatrices();
}


//Render Setup 라이트 시점에서의 Depth Buffer 렌더링
bool FShadowRenderFeature::RenderDepthPass(FRenderer& Renderer, const FSceneViewData& SceneViewData)
{
	if (ShadowItems.empty() && PointShadowItems.empty())
		return true;
	ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
	// 1. Rasterizer State 
	if (!ShadowRasterizerState)
	{
		D3D11_RASTERIZER_DESC RSDesc = {};
		RSDesc.FillMode = D3D11_FILL_SOLID;
		RSDesc.CullMode = D3D11_CULL_FRONT;
		RSDesc.DepthClipEnable = true;
		RSDesc.DepthBias = 100;
		RSDesc.SlopeScaledDepthBias = 1.5f;
		RSDesc.DepthBiasClamp = 0.01f;
		Renderer.GetDevice()->CreateRasterizerState(&RSDesc, &ShadowRasterizerState);
	}
	DeviceContext->RSSetState(ShadowRasterizerState);

	// state leak cause crupt Shadow Map rendering, so reset states to default
	DeviceContext->OMSetDepthStencilState(nullptr, 0); // Reset to default (depth enabled, less, write all)
	DeviceContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF); // Reset to default

	if (ShadowDepthVS)
	{
		ShadowDepthVS->Bind(DeviceContext);
	}

	// DepthOnly pass no pixel shader, no GS/HS/DS
	DeviceContext->PSSetShader(nullptr, nullptr, 0);
	DeviceContext->GSSetShader(nullptr, nullptr, 0);
	DeviceContext->HSSetShader(nullptr, nullptr, 0);
	DeviceContext->DSSetShader(nullptr, nullptr, 0);

	if (!ShadowItems.empty())
	{
		D3D11_VIEWPORT Viewport = {};
		Viewport.Width = static_cast<float>(ShadowItems[0].Resolution);
		Viewport.Height = static_cast<float>(ShadowItems[0].Resolution);
		Viewport.MinDepth = 0.0f;
		Viewport.MaxDepth = 1.0f;
		Viewport.TopLeftX = 0;
		Viewport.TopLeftY = 0;
		DeviceContext->RSSetViewports(1, &Viewport);
	}

	for (uint32 i = 0; i < ShadowItems.size(); ++i)
	{
		const auto& ShadowItem = ShadowItems[i];

		FShadowPassConstantsGPU CBData;
		DeviceContext->ClearDepthStencilView(ShadowDSVs[i], D3D11_CLEAR_DEPTH, 1.0f, 0);
		// NumViews=0 이면 DSV가 실제로 변경 안 되는 드라이버가 있으므로
		// NullRTV 배열을 명시적으로 넘겨야 DSV 바인딩이 보장됨
		ID3D11RenderTargetView* NullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		DeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, NullRTVs, ShadowDSVs[i]);

		CBData.LightViewProj = ShadowItem.ViewProjectionMatrix.GetTransposed();
		CBData.ShadowParams = FVector4(ShadowItem.DepthBias, ShadowItem.SlopeScaleDepthBias, ShadowItem.FarZ, 0.0f);

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (SUCCEEDED(DeviceContext->Map(ShadowPassCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			memcpy(Mapped.pData, &CBData, sizeof(FShadowPassConstantsGPU));
			DeviceContext->Unmap(ShadowPassCB, 0);
		}
		DeviceContext->VSSetConstantBuffers(2, 1, &ShadowPassCB);

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

	if (!PointShadowItems.empty())
	{

		D3D11_VIEWPORT PointVP = {};
		PointVP.Width = static_cast<float>(PointShadowItems[0].Resolution);
		PointVP.Height = static_cast<float>(PointShadowItems[0].Resolution);
		PointVP.MinDepth = 0.0f;
		PointVP.MaxDepth = 1.0f;
		DeviceContext->RSSetViewports(1, &PointVP);

		for (uint32 i = 0; i < PointShadowItems.size(); ++i)
		{
			const auto& Item = PointShadowItems[i];

			DeviceContext->ClearDepthStencilView(PointShadowDSVs[i], D3D11_CLEAR_DEPTH, 1.0f, 0);

			ID3D11RenderTargetView* NullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
			DeviceContext->OMSetRenderTargets(
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, NullRTVs, PointShadowDSVs[i]);

			FShadowPassConstantsGPU CBData;
			CBData.LightViewProj = Item.ViewProjectionMatrix.GetTransposed();
			CBData.ShadowParams = FVector4(Item.DepthBias, Item.SlopeScaleDepthBias, Item.FarZ, 0.0f);

			D3D11_MAPPED_SUBRESOURCE Mapped = {};
			if (SUCCEEDED(DeviceContext->Map(ShadowPassCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
			{
				memcpy(Mapped.pData, &CBData, sizeof(FShadowPassConstantsGPU));
				DeviceContext->Unmap(ShadowPassCB, 0);
			}
			DeviceContext->VSSetConstantBuffers(2, 1, &ShadowPassCB);

			for (const FMeshBatch& Batch : SceneViewData.MeshInputs.Batches)
			{
				if (Batch.Domain != EMaterialDomain::Opaque) continue;
				if (Batch.bDisableDepthWrite || !Batch.Mesh) continue;

				Renderer.UpdateObjectConstantBuffer(Batch);
				Batch.Mesh->Bind(DeviceContext);
				DeviceContext->IASetPrimitiveTopology(
					static_cast<D3D11_PRIMITIVE_TOPOLOGY>(Batch.Mesh->Topology));
				DeviceContext->DrawIndexed(Batch.IndexCount, Batch.IndexStart, 0);
			}
		}
	}
	DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	DeviceContext->RSSetState(nullptr); // 기본 래스터라이저로 복구

	return true;
}

bool FShadowRenderFeature::EnsurePointShadowMapResources(FRenderer& Renderer, uint32 Resolution)
{
	ID3D11Device* Device = Renderer.GetDevice();

	if (PointShadowCubeArray)
		return true;
	D3D11_TEXTURE2D_DESC TexDesc = {};
	TexDesc.Width = Resolution;
	TexDesc.Height = Resolution;
	TexDesc.MipLevels = 1;
	TexDesc.ArraySize = MaxPointShadows * 6;
	TexDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	TexDesc.SampleDesc.Count = 1;
	TexDesc.Usage = D3D11_USAGE_DEFAULT;
	TexDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	TexDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
	if (FAILED(Device->CreateTexture2D(&TexDesc, nullptr, &PointShadowCubeArray)))
		return false;


	PointShadowDSVs.resize(MaxPointShadows * 6, nullptr);
	for (uint32 L = 0; L < MaxPointShadows; ++L)
	{
		for (uint32 F = 0; F < 6; ++F)
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
			DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
			DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			DSVDesc.Texture2DArray.ArraySize = 1;
			DSVDesc.Texture2DArray.FirstArraySlice = L * 6 + F;
			DSVDesc.Texture2DArray.MipSlice = 0;
			if (FAILED(Device->CreateDepthStencilView(
				PointShadowCubeArray, &DSVDesc, &PointShadowDSVs[L * 6 + F])))
				return false;
		}
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
	SRVDesc.TextureCubeArray.MostDetailedMip = 0;
	SRVDesc.TextureCubeArray.MipLevels = 1;
	SRVDesc.TextureCubeArray.First2DArrayFace = 0;
	SRVDesc.TextureCubeArray.NumCubes = MaxPointShadows;
	if (FAILED(Device->CreateShaderResourceView(PointShadowCubeArray, &SRVDesc, &PointShadowSRV)))
		return false;

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
	TexDesc.ArraySize = MaxShadowedLights;
	TexDesc.Format = DXGI_FORMAT_R32_TYPELESS; // DSV와 SRV 양쪽에서 쓰기 위함
	TexDesc.SampleDesc.Count = 1;
	TexDesc.Usage = D3D11_USAGE_DEFAULT;
	TexDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&TexDesc, nullptr, &ShadowDepthTexture)))
		return false;

	// DSV 배열을 MaxShadowedLights 크기로 먼저 초기화 (없으면 [i] 접근 시 crash)
	ShadowDSVs.resize(MaxShadowedLights, nullptr);

	// 슬라이스별 DSV 생성
	for (uint32 i = 0; i < MaxShadowedLights; ++i) {
		D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
		DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
		DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		DSVDesc.Texture2DArray.ArraySize = 1;
		DSVDesc.Texture2DArray.FirstArraySlice = i;
		DSVDesc.Texture2DArray.MipSlice = 0;
		if (FAILED(Device->CreateDepthStencilView(ShadowDepthTexture, &DSVDesc, &ShadowDSVs[i])))
			return false;
	}

	// generate SRV
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	SRVDesc.Texture2DArray.ArraySize = MaxShadowedLights;
	SRVDesc.Texture2DArray.FirstArraySlice = 0;
	SRVDesc.Texture2DArray.MipLevels = 1;
	if (FAILED(Device->CreateShaderResourceView(ShadowDepthTexture, &SRVDesc, &ShadowSRV)))
		return false;
	Stats.TotalShadowMapMemoryBytes = Resolution * Resolution * 4 * MaxShadowedLights;
	return true;
}
