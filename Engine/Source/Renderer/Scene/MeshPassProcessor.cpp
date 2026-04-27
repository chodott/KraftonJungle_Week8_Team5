#include "Renderer/Scene/MeshPassProcessor.h"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Mesh/RenderMesh.h"
#include "Renderer/Renderer.h"
#include "Debug/EngineLog.h"
#include "Renderer/Features/Lighting/LightRenderFeature.h"

#include <chrono>

namespace
{
	using FMeshPassClock = std::chrono::high_resolution_clock;

	double ToMilliseconds(FMeshPassClock::duration Duration)
	{
		return std::chrono::duration<double, std::milli>(Duration).count();
	}
}

void FMeshPassProcessor::BeginFrame()
{
	FrameStats = {};
}

void FMeshPassProcessor::UploadMeshBuffers(FRenderer& Renderer, const FSceneViewData& SceneViewData) const
{
	ID3D11Device*        Device  = Renderer.GetDevice();
	ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
	if (!Device || !Context)
	{
		return;
	}

	std::unordered_set<FRenderMesh*> UploadedMeshes;
	for (const FMeshBatch& Batch : SceneViewData.MeshInputs.Batches)
	{
		if (!Batch.Mesh || !UploadedMeshes.insert(Batch.Mesh).second)
		{
			continue;
		}

		Batch.Mesh->UpdateVertexAndIndexBuffer(Device, Context);
	}
}

void FMeshPassProcessor::ExecutePass(
	FRenderer&            Renderer,
	FSceneRenderTargets&  Targets,
	const FSceneViewData& SceneViewData,
	EMeshPassType         PassType) const
{
	const FMeshPassClock::time_point PassStartTime = FMeshPassClock::now();
	ID3D11DeviceContext*             DeviceContext = Renderer.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	std::vector<const FMeshBatch*> Batches;
	Batches.reserve(SceneViewData.MeshInputs.Batches.size());

	for (const FMeshBatch& Batch : SceneViewData.MeshInputs.Batches)
	{
		if (!Batch.Mesh || !Batch.Material || !ShouldDrawInPass(Batch, PassType))
		{
			continue;
		}

		if (PassType == EMeshPassType::DepthPrepass && Batch.Material->GetPassShaders(EMaterialPassType::DepthOnly) == nullptr)
		{
			continue;
		}

		if (PassType == EMeshPassType::GBuffer && Batch.Material->GetPassShaders(EMaterialPassType::GBuffer) == nullptr)
		{
			continue;
		}
		if (PassType == EMeshPassType::EditorPicking && Batch.Material->GetPassShaders(EMaterialPassType::Picking) == nullptr)
		{
			continue;
		}
		if (PassType == EMeshPassType::ShadowVSM && Batch.Material->GetPassShaders(EMaterialPassType::ShadowVSM) == nullptr)
		{
			continue;
		}

		Batches.push_back(&Batch);
	}

	if (Batches.empty())
	{
		return;
	}

	switch (PassType)
	{
	case EMeshPassType::ForwardTransparent:
		std::sort(
			Batches.begin(),
			Batches.end(),
			[](const FMeshBatch* A, const FMeshBatch* B)
			{
				if (A->DistanceSqToCamera != B->DistanceSqToCamera)
				{
					return A->DistanceSqToCamera > B->DistanceSqToCamera;
				}
				return A->SubmissionOrder < B->SubmissionOrder;
			});
		break;

	case EMeshPassType::EditorGrid:
	case EMeshPassType::EditorPrimitive:
		std::stable_sort(
			Batches.begin(),
			Batches.end(),
			[](const FMeshBatch* A, const FMeshBatch* B)
			{
				return A->SubmissionOrder < B->SubmissionOrder;
			});
		break;

	case EMeshPassType::DepthPrepass:
	case EMeshPassType::GBuffer:
	case EMeshPassType::ForwardOpaque:
	case EMeshPassType::ForwardMeshDecal:
	case EMeshPassType::EditorPicking:
	default:
		std::sort(
			Batches.begin(),
			Batches.end(),
			[PassType](const FMeshBatch* A, const FMeshBatch* B)
			{
				const uint64 KeyA = MakeBatchSortKey(*A, PassType);
				const uint64 KeyB = MakeBatchSortKey(*B, PassType);
				if (KeyA != KeyB)
				{
					return KeyA < KeyB;
				}
				return A->SubmissionOrder < B->SubmissionOrder;
			});
		break;
	}

	RestoreScenePassDefaults(Renderer, SceneViewData.Frame, SceneViewData.View);

	FMaterial*               CurrentMaterial  = nullptr;
	FRenderMesh*             CurrentMesh      = nullptr;
	D3D11_PRIMITIVE_TOPOLOGY CurrentTopology  = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	const EMaterialPassType  MaterialPassType = ToMaterialPassType(PassType);
	uint32                   LocalDrawCalls   = 0;

	FLightRenderFeature* Feature        = Renderer.GetLightFeature();
	const bool           bApplyLighting = (Feature != nullptr && (PassType == EMeshPassType::ForwardOpaque || PassType == EMeshPassType::ForwardMeshDecal)
		&& SceneViewData.RenderMode != ERenderMode::Unlit
		&& SceneViewData.RenderMode != ERenderMode::Wireframe);
	const bool           bUseShadowVariant = bApplyLighting
		&& (SceneViewData.RenderMode == ERenderMode::Lit_Gouraud ||
			SceneViewData.RenderMode == ERenderMode::Lit_Lambert ||
			SceneViewData.RenderMode == ERenderMode::Lit_Phong)
		&& !SceneViewData.LightingInputs.ShadowLights.empty()
		&& !SceneViewData.LightingInputs.ShadowViews.empty();

	if (bApplyLighting)
	{
		Feature->Render(Renderer, SceneViewData, Targets);
	}

	for (const FMeshBatch* Batch : Batches)
	{
		if (!Batch || !Batch->Mesh || !Batch->Material)
		{
			continue;
		}

		if (Batch->Material != CurrentMaterial)
		{
			ID3D11ShaderResourceView* NullSRVs[4] = { nullptr, nullptr, nullptr, nullptr };
			DeviceContext->VSSetShaderResources(0, 4, NullSRVs);
			DeviceContext->PSSetShaderResources(0, 4, NullSRVs);

			if (!Batch->Material->GetMaterialTexture())
			{
				Renderer.GetDefaultTextureMaterial()->Bind(DeviceContext, MaterialPassType);
			}
			Batch->Material->Bind(DeviceContext, MaterialPassType);
			Renderer.GetRenderStateManager()->BindState(Batch->Material->GetRasterizerState());
			Renderer.GetRenderStateManager()->BindState(Batch->Material->GetDepthStencilState());
			Renderer.GetRenderStateManager()->BindState(Batch->Material->GetBlendState());
			CurrentMaterial = Batch->Material;

			if (!CurrentMaterial->HasPixelTextureBinding())
			{
				ID3D11SamplerState* DefaultSampler = Renderer.GetDefaultSampler();
				DeviceContext->PSSetSamplers(0, 1, &DefaultSampler);
			}
		}

		if (bApplyLighting)
		{
			const bool                           bHasNormalMap = Batch->Material->HasNormalTexture();
			std::shared_ptr<FVertexShaderHandle> VSHandle      = Feature->GetCurrentVSHandle(bHasNormalMap, bUseShadowVariant, SceneViewData.RenderMode);
			std::shared_ptr<FPixelShaderHandle>  PSHandle      = Feature->GetCurrentPSHandle(bHasNormalMap, bUseShadowVariant, SceneViewData.RenderMode);
			if (VSHandle)
			{
				VSHandle->Bind(DeviceContext);
			}
			if (PSHandle)
			{
				PSHandle->Bind(DeviceContext);
			}
		}

		if (Batch->bDisableCulling)
		{
			FRasterizerStateOption RasterOpt = Batch->Material->GetRasterizerOption();
			RasterOpt.CullMode               = D3D11_CULL_NONE;
			Renderer.GetRenderStateManager()->BindState(Renderer.GetRenderStateManager()->GetOrCreateRasterizerState(RasterOpt));
		}
		else
		{
			Renderer.GetRenderStateManager()->BindState(Batch->Material->GetRasterizerState());
		}

		FDepthStencilStateOption DepthOpt = Batch->Material->GetDepthStencilOption();
		if (PassType == EMeshPassType::ShadowVSM)
		{
			const FMaterialPassShaders* ShadowVSMPass = Batch->Material->GetPassShaders(EMaterialPassType::ShadowVSM);
		}
		else if (PassType == EMeshPassType::GBuffer
			|| PassType == EMeshPassType::ForwardOpaque
			|| PassType == EMeshPassType::ForwardMeshDecal
			|| PassType == EMeshPassType::EditorPicking)
		{
			if (!Batch->bDisableDepthTest)
			{
				DepthOpt.DepthEnable = true;
			}

			DepthOpt.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			if (PassType == EMeshPassType::EditorPicking)
			{
				// ID picking pass is rendered standalone (without depth prepass), so it must test/write depth.
				DepthOpt.DepthEnable    = true;
				DepthOpt.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			}
			if (DepthOpt.DepthFunc == D3D11_COMPARISON_LESS)
			{
				DepthOpt.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			}
		}

		if (Batch->bDisableDepthTest && PassType != EMeshPassType::EditorPicking)
		{
			DepthOpt.DepthEnable = false;
		}
		if (Batch->bDisableDepthWrite && PassType != EMeshPassType::EditorPicking)
		{
			DepthOpt.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		}

		if (PassType == EMeshPassType::DepthPrepass ||
			PassType == EMeshPassType::ShadowVSM ||
			PassType == EMeshPassType::GBuffer ||
			PassType == EMeshPassType::ForwardOpaque ||
			PassType == EMeshPassType::EditorPicking ||
			PassType == EMeshPassType::ForwardMeshDecal ||
			Batch->bDisableDepthTest ||
			Batch->bDisableDepthWrite)
		{
			Renderer.GetRenderStateManager()->BindState(Renderer.GetRenderStateManager()->GetOrCreateDepthStencilState(DepthOpt));
		}
		else
		{
			Renderer.GetRenderStateManager()->BindState(Batch->Material->GetDepthStencilState());
		}

		if (PassType == EMeshPassType::EditorPicking ||
			PassType == EMeshPassType::ShadowVSM)
		{
			FBlendStateOption BlendOpt = Batch->Material->GetBlendOption();
			BlendOpt.BlendEnable       = false;

			Renderer.GetRenderStateManager()->BindState(
				Renderer.GetRenderStateManager()->GetOrCreateBlendState(BlendOpt));
		}

		if (Batch->Mesh != CurrentMesh)
		{
			Batch->Mesh->Bind(DeviceContext);
			CurrentMesh = Batch->Mesh;
		}

		const auto DesiredTopology = static_cast<D3D11_PRIMITIVE_TOPOLOGY>(Batch->Mesh->Topology);
		if (DesiredTopology != CurrentTopology)
		{
			DeviceContext->IASetPrimitiveTopology(DesiredTopology);
			CurrentTopology = DesiredTopology;
		}

		Renderer.UpdateObjectConstantBuffer(*Batch);

		if (!Batch->Mesh->Indices.empty())
		{
			const UINT DrawCount = (Batch->IndexCount > 0) ? Batch->IndexCount : static_cast<UINT>(Batch->Mesh->Indices.size());
			DeviceContext->DrawIndexed(DrawCount, Batch->IndexStart, 0);
			++LocalDrawCalls;
		}
		else
		{
			DeviceContext->Draw(static_cast<UINT>(Batch->Mesh->Vertices.size()), 0);
			++LocalDrawCalls;
		}
	}

	FrameStats.TotalDrawCalls += LocalDrawCalls;
	FrameStats.TotalTimeMs    += ToMilliseconds(FMeshPassClock::now() - PassStartTime);
}

EMaterialPassType FMeshPassProcessor::ToMaterialPassType(EMeshPassType PassType)
{
	switch (PassType)
	{
	case EMeshPassType::DepthPrepass:
		return EMaterialPassType::DepthOnly;
	case EMeshPassType::GBuffer:
		return EMaterialPassType::GBuffer;
	case EMeshPassType::ForwardTransparent:
		return EMaterialPassType::ForwardTransparent;
	case EMeshPassType::ForwardMeshDecal:
		return EMaterialPassType::ForwardOpaque;
	case EMeshPassType::EditorPicking:
		return EMaterialPassType::Picking;
	case EMeshPassType::EditorGrid:
		return EMaterialPassType::EditorGrid;
	case EMeshPassType::EditorPrimitive:
		return EMaterialPassType::EditorPrimitive;
	case EMeshPassType::ShadowVSM:
		return EMaterialPassType::ShadowVSM;
	case EMeshPassType::ForwardOpaque:
	default:
		return EMaterialPassType::ForwardOpaque;
	}
}

bool FMeshPassProcessor::ShouldDrawInPass(const FMeshBatch& Batch, EMeshPassType PassType)
{
	switch (PassType)
	{
	case EMeshPassType::DepthPrepass:
		return EnumHasAnyFlags(Batch.PassMask, EMeshPassMask::DepthPrepass);
	case EMeshPassType::GBuffer:
		return EnumHasAnyFlags(Batch.PassMask, EMeshPassMask::GBuffer);
	case EMeshPassType::ForwardOpaque:
		return EnumHasAnyFlags(Batch.PassMask, EMeshPassMask::ForwardOpaque);
	case EMeshPassType::ForwardMeshDecal:
		return EnumHasAnyFlags(Batch.PassMask, EMeshPassMask::ForwardMeshDecal);
	case EMeshPassType::ForwardTransparent:
		return EnumHasAnyFlags(Batch.PassMask, EMeshPassMask::ForwardTransparent);
	case EMeshPassType::EditorPicking:
		return EnumHasAnyFlags(Batch.PassMask, EMeshPassMask::EditorPicking);
	case EMeshPassType::EditorGrid:
		return EnumHasAnyFlags(Batch.PassMask, EMeshPassMask::EditorGrid);
	case EMeshPassType::EditorPrimitive:
		return EnumHasAnyFlags(Batch.PassMask, EMeshPassMask::EditorPrimitive);
	case EMeshPassType::ShadowVSM:
		return EnumHasAnyFlags(Batch.PassMask, EMeshPassMask::ShadowVSM);
	default:
		return false;
	}
}

uint64 FMeshPassProcessor::MakeBatchSortKey(const FMeshBatch& Batch, EMeshPassType PassType)
{
	const EMaterialPassType     MaterialPassType = ToMaterialPassType(PassType);
	const FMaterialPassShaders* PassShaders      = Batch.Material ? Batch.Material->GetPassShaders(MaterialPassType) : nullptr;
	const uintptr_t             VSKey            = reinterpret_cast<uintptr_t>(PassShaders && PassShaders->VS ? PassShaders->VS.get() : (Batch.Material ? Batch.Material->GetVertexShader() : nullptr));
	const uintptr_t             PSKey            = reinterpret_cast<uintptr_t>(PassShaders && PassShaders->PS ? PassShaders->PS.get() : (Batch.Material ? Batch.Material->GetPixelShader() : nullptr));
	const uint64                StateKey         =
			(static_cast<uint64>(Batch.Material ? Batch.Material->GetRasterizerOption().ToKey() : 0u) << 32) ^
			(static_cast<uint64>(Batch.Material ? Batch.Material->GetDepthStencilOption().ToKey() : 0u) << 8) ^
			static_cast<uint64>(Batch.Material ? Batch.Material->GetBlendOption().ToKey() : 0u);
	const uint64 ShaderKey = VSKey >> 4 ^ PSKey >> 4;
	const uint64 MeshKey   = Batch.Mesh ? static_cast<uint64>(Batch.Mesh->GetSortId()) : 0ull;
	return (ShaderKey << 32) ^ StateKey ^ MeshKey;
}
