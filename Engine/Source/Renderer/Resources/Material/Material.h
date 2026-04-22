#pragma once

#include "CoreMinimal.h"
#include "Math/LinearColor.h"
#include "Renderer/GraphicsCore/RenderState.h"
#include <d3d11.h>
#include <array>
#include <memory>

class FVertexShaderHandle;
class FPixelShaderHandle;

enum class EMaterialPassType : uint8
{
	DepthOnly = 0,
	GBuffer,
	OutlineMask,
	ForwardOpaque,
	ForwardTransparent,
	EditorGrid,
	EditorPrimitive,
	Count,
};

struct FMaterialPassShaders
{
	std::shared_ptr<FVertexShaderHandle> VS = nullptr;
	std::shared_ptr<FPixelShaderHandle> PS = nullptr;

	bool IsValid() const
	{
		return VS != nullptr || PS != nullptr;
	}
};

struct FMaterialTexture
{
	ID3D11ShaderResourceView* TextureSRV = nullptr;
	ID3D11SamplerState* SamplerState = nullptr;

	FMaterialTexture() = default;
	~FMaterialTexture();

	FMaterialTexture(const FMaterialTexture&) = delete;
	FMaterialTexture& operator=(const FMaterialTexture&) = delete;

	void Release();
	void Bind(ID3D11DeviceContext* DeviceContext);
};

struct FMaterialPixelTextureBinding
{
	uint32 Slot = 0;
	ID3D11ShaderResourceView* TextureSRV = nullptr;
	ID3D11SamplerState* SamplerState = nullptr;

	bool IsValid() const { return TextureSRV != nullptr; }
};

// ?뚮씪誘명꽣 ?대쫫 ???곸닔 踰꾪띁 ???꾩튂 留ㅽ븨
struct FMaterialParameterInfo
{
	int32 BufferIndex;  // ConstantBuffers 諛곗뿴 ?몃뜳??
	uint32 Offset;      // 踰꾪띁 ??諛붿씠???ㅽ봽??
	uint32 Size;        // 諛붿씠???ш린
};

// Material???뚯쑀?섎뒗 ?곸닔 踰꾪띁 ?щ’ ?섎굹
// GPU 踰꾪띁 ?앹꽦, CPU ?곗씠??愿由? Dirty ?뚮옒洹?湲곕컲 ?낅줈?쒕? 紐⑤몢 ?대떦
struct ENGINE_API FMaterialConstantBuffer
{
	ID3D11Buffer* GPUBuffer = nullptr;
	uint8* CPUData = nullptr; // CPU 履?shadow copy
	uint32 Size = 0;
	bool bDirty = false;

	FMaterialConstantBuffer() = default;
	~FMaterialConstantBuffer();

	// 蹂듭궗 湲덉?
	FMaterialConstantBuffer(const FMaterialConstantBuffer&) = delete;
	FMaterialConstantBuffer& operator=(const FMaterialConstantBuffer&) = delete;

	// Move 吏??(?뚯쑀沅??댁쟾)
	FMaterialConstantBuffer(FMaterialConstantBuffer&& Other) noexcept
		: GPUBuffer(Other.GPUBuffer), CPUData(Other.CPUData), Size(Other.Size), bDirty(Other.bDirty)
	{
		Other.GPUBuffer = nullptr;
		Other.CPUData = nullptr;
		Other.Size = 0;
		Other.bDirty = false;
	}
	FMaterialConstantBuffer& operator=(FMaterialConstantBuffer&& Other) noexcept
	{
		if (this != &Other)
		{
			Release();
			GPUBuffer = Other.GPUBuffer;
			CPUData = Other.CPUData;
			Size = Other.Size;
			bDirty = Other.bDirty;
			Other.GPUBuffer = nullptr;
			Other.CPUData = nullptr;
			Other.Size = 0;
			Other.bDirty = false;
		}
		return *this;
	}

	// Device濡?GPU 踰꾪띁 ?앹꽦 + CPU 硫붾え由??좊떦
	bool Create(ID3D11Device* Device, uint32 InSize);

	// CPU ?곗씠?곗쓽 ?뱀젙 ?ㅽ봽?뗭뿉 媛??곌린 (Dirty 留덊궧)
	void SetData(const void* Data, uint32 InSize, uint32 Offset = 0);

	// Dirty硫?Map/Unmap?쇰줈 GPU???낅줈??
	void Upload(ID3D11DeviceContext* DeviceContext);

	void Release();
};

// Material: VS/PS ?곗씠??議고빀 + 異붽? ?곸닔 踰꾪띁 (b2+)
// ?앹꽦 ???뚮씪誘명꽣 媛?蹂寃?遺덇? (?쎄린 ?꾩슜). ?고???蹂寃쎌씠 ?꾩슂?섎㈃ FDynamicMaterial ?ъ슜.
class ENGINE_API FMaterial
{
public:
	FMaterial() : ShaderId(NextShaderId++) {}
	virtual ~FMaterial();

	FMaterial(const FMaterial&) = delete;
	FMaterial& operator=(const FMaterial&) = delete;
	FMaterial(FMaterial&&) = default;
	FMaterial& operator=(FMaterial&&) = default;

	uint64 GetSortId() const;

	// ?먯뀑 ?먮낯 ?대쫫 (JSON?먯꽌 濡쒕뱶???대쫫, 吏곷젹?????ъ슜)
	void SetOriginName(const FString& InName) { OriginName = InName; }
	const FString& GetOriginName() const { return OriginName; }

	// ?몄뒪?댁뒪 ?대쫫 (?고??꾩뿉??援щ텇?? DynamicMaterial ??
	void SetInstanceName(const FString& InName) { InstanceName = InName; }
	const FString& GetInstanceName() const { return InstanceName; }

	// ?몄뒪?댁뒪 ?대쫫???덉쑝硫??몄뒪?댁뒪 ?대쫫, ?놁쑝硫??먮낯 ?대쫫 諛섑솚
	const FString& GetName() const { return InstanceName.empty() ? OriginName : InstanceName; }

	void SetVertexShader(const std::shared_ptr<FVertexShaderHandle>& InVS) { VertexShader = InVS; }
	void SetPixelShader(const std::shared_ptr<FPixelShaderHandle>& InPS) { PixelShader = InPS; }
	void SetPassShaders(EMaterialPassType PassType, const FMaterialPassShaders& InShaders);
	void SetRasterizerOption(const FRasterizerStateOption InOption) { RasterizerOption = InOption; }
	void SetRasterizerState(const std::shared_ptr<FRasterizerState> InState) { RasterizerState = InState; }
	void SetDepthStencilOption(const FDepthStencilStateOption InOption) { DepthStencilOption = InOption; }
	void SetDepthStencilState(const std::shared_ptr<FDepthStencilState> InState) { DepthStencilState = InState; }
	void SetBlendOption(const FBlendStateOption InOption) { BlendOption = InOption; }
	void SetBlendState(const std::shared_ptr<FBlendState> InState) { BlendState = InState; }
	void SetMaterialTexture(const std::shared_ptr<FMaterialTexture> InTexture) { MaterialTexture = InTexture; }
	void SetNormalTexture(const std::shared_ptr<FMaterialTexture> InTexture) { NormalTexture = InTexture; }
	void SetEmissiveTexture(const std::shared_ptr<FMaterialTexture> InTexture) { EmissiveTexture = InTexture; }
	void SetPixelTextureBinding(uint32 Slot, ID3D11ShaderResourceView* TextureSRV, ID3D11SamplerState* SamplerState);
	void ClearPixelTextureBinding();

	FVertexShaderHandle* GetVertexShader() const { return VertexShader.get(); }
	FPixelShaderHandle* GetPixelShader() const { return PixelShader.get(); }
	const FMaterialPassShaders* GetPassShaders(EMaterialPassType PassType) const;
	const FRasterizerStateOption& GetRasterizerOption() const { return RasterizerOption; }
	const FDepthStencilStateOption& GetDepthStencilOption() const { return DepthStencilOption; }
	const FBlendStateOption& GetBlendOption() const { return BlendOption; }
	std::shared_ptr<FRasterizerState> GetRasterizerState() const { return RasterizerState; }
	std::shared_ptr<FDepthStencilState> GetDepthStencilState() const { return DepthStencilState; }
	std::shared_ptr<FBlendState> GetBlendState() const { return BlendState; }
	std::shared_ptr<FMaterialTexture> GetMaterialTexture() const { return MaterialTexture; }
	std::shared_ptr<FMaterialTexture> GetNormalTexture() const { return NormalTexture; }
	std::shared_ptr<FMaterialTexture> GetEmissiveTexture() const { return EmissiveTexture; }
	bool HasPixelTextureBinding() const;
	bool HasNormalTexture() const { return NormalTexture && NormalTexture->TextureSRV != nullptr; }
	bool HasEmissiveTexture() const { return EmissiveTexture && EmissiveTexture->TextureSRV != nullptr; }

	// FDynamicMaterial?먯꽌 ?뚮씪誘명꽣 ?ㅼ젙 ???ъ슜
	bool SetParameterData(const FString& ParamName, const void* Data, uint32 DataSize);
	bool GetParameterData(const FString& ParamName, void* OutData, uint32 DataSize) const;
	FVector4 GetVectorParameter(const FString& ParamName) const;
	bool SetLinearColorParameter(const FString& ParamName, const FLinearColor& Value);
	bool SetSRGBColorParameter(const FString& ParamName, const FVector4& Value);

	// ?곸닔 踰꾪띁 ?щ’ 異붽? (b2, b3, ... ?쒖꽌?濡?
	int32 CreateConstantBuffer(ID3D11Device* Device, uint32 InSize);

	// ?щ’ ?몃뜳?ㅻ줈 ?곸닔 踰꾪띁 ?묎렐
	FMaterialConstantBuffer* GetConstantBuffer(int32 Index);

	// ?뚮씪誘명꽣 ?대쫫 ?깅줉 (MaterialManager?먯꽌 JSON 濡쒕뱶 ???몄텧)
	void RegisterParameter(const FString& ParamName, int32 BufferIndex, uint32 Offset, uint32 Size);

	// ?낅┰?곸씤 ?곸닔 踰꾪띁瑜?媛吏?DynamicMaterial 蹂듭젣蹂??앹꽦
	std::unique_ptr<class FDynamicMaterial> CreateDynamicMaterial() const;

	// ?곗씠??諛붿씤??+ Dirty ?곸닔 踰꾪띁 ?낅줈??+ 諛붿씤??
	void Bind(ID3D11DeviceContext* DeviceContext, EMaterialPassType PassType = EMaterialPassType::ForwardOpaque);

	void Release();

protected:
	void EnsureLitMaterialParameters(ID3D11Device* Device);


	// TODO: ShaderId媛 ?ㅼ젣 ?ъ슜?섎뒗 ?먯씠?붾? 諛섏쁺?섎룄濡?蹂寃?
	// NOTE: GetSortId?먯꽌 鍮꾪듃 ?곗궛 ?곕뒗 寃쎌슦 ShaderId媛 32bit瑜??꾨? ?곕㈃ ????
	uint32 ShaderId = 0;
	static inline uint32 NextShaderId = 0;

	FString OriginName;
	FString InstanceName;
	std::shared_ptr<FVertexShaderHandle> VertexShader;
	std::shared_ptr<FPixelShaderHandle> PixelShader;
	// RasterizerState瑜??앹꽦?섍린 ?꾪븳 ?듭뀡, Serialize.
	FRasterizerStateOption RasterizerOption;
	FDepthStencilStateOption DepthStencilOption;
	FBlendStateOption BlendOption;
	// 癒명떚由ъ뼹 濡쒕뱶?쒖뿉 ?앹꽦?섎뒗 RasterizerState ?ъ씤?? No-Serialize.
	std::shared_ptr<FRasterizerState> RasterizerState = nullptr;
	std::shared_ptr<FDepthStencilState> DepthStencilState = nullptr;
	std::shared_ptr<FBlendState> BlendState = nullptr;
	// Texture
	std::shared_ptr<FMaterialTexture> MaterialTexture = nullptr;
	std::shared_ptr<FMaterialTexture> NormalTexture = nullptr;
	std::shared_ptr<FMaterialTexture> EmissiveTexture = nullptr;
	FMaterialPixelTextureBinding PixelTextureBinding = {};
	std::array<FMaterialPassShaders, static_cast<size_t>(EMaterialPassType::Count)> PassShaderMap = {};
	std::array<bool, static_cast<size_t>(EMaterialPassType::Count)> bHasPassShaderMap = {};

	TArray<FMaterialConstantBuffer> ConstantBuffers;
	TMap<FString, FMaterialParameterInfo> ParameterMap;

	static constexpr UINT MaterialCBStartSlot = 2; // b0=Frame, b1=Object, b2+=Material
};

// DynamicMaterial: ?고??꾩뿉 ?뚮씪誘명꽣 媛믪쓣 蹂寃쏀븷 ???덈뒗 Material ?몄뒪?댁뒪
// FMaterial::CreateDynamicMaterial()濡??앹꽦
class ENGINE_API FDynamicMaterial : public FMaterial
{
public:
	FDynamicMaterial() = default;

	// ?대쫫 湲곕컲 ?뚮씪誘명꽣 ?ㅼ젙 (??낅퀎 ?몄쓽 ?⑥닔)
	bool SetScalarParameter(const FString& ParamName, float Value);
	bool SetVectorParameter(const FString& ParamName, const FVector4& Value);
	bool SetVector3Parameter(const FString& ParamName, const FVector& Value);
};

