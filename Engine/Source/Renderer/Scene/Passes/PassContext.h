#pragma once

#include "CoreMinimal.h"
#include "Renderer/Common/SceneRenderTargets.h"
#include "Renderer/Scene/SceneViewData.h"

class FRenderer;

enum class EPassDomain : uint8
{
	Graphics,
	Compute,
	Copy,
};

// FSceneRenderTargets 의 공유 리소스 슬롯
enum class ESceneTarget : uint8
{
	SceneColor,         // SceneColorRead  (RTV/SRV)
	SceneColorScratch,  // SceneColorWrite (RTV/SRV, ping-pong buffer)
	SceneDepth,         // SceneDepthDSV/SRV
	GBufferA,
	GBufferB,
	GBufferC,
	OutlineMask,
	OverlayColor,
	ShadowMap,          // ShadowDepthArray (DSV write / SRV read)
};

using FPassTargetMask = uint32;

// 사용 예: PassTarget(ESceneTarget::SceneDepth) | PassTarget(ESceneTarget::SceneColor)
constexpr FPassTargetMask PassTarget(ESceneTarget T)
{
	return 1u << static_cast<uint8>(T);
}

enum class EPassCategory : uint8
{
	Setup,         // clear, upload
	Geometry,      // depth prepass, GBuffer, forward opaque
	Lighting,      // light culling, deferred lighting
	Effects,       // decal, fog, transparent, bloom
	EditorOverlay, // grid, outline, line, primitive
};

// 패스가 선언하는 속성 / 의존성 디스크립터
struct FPassDesc
{
	const char*     Name     = "UnnamedPass";
	EPassDomain     Domain   = EPassDomain::Graphics;
	EPassCategory   Category = EPassCategory::Geometry;
	FPassTargetMask Reads    = 0; // 이 패스가 SRV/UAV로 읽는 타깃
	FPassTargetMask Writes   = 0; // 이 패스가 RTV/DSV/UAV로 쓰는 타깃
};

struct ENGINE_API FPassContext
{
	FRenderer&           Renderer;
	FSceneRenderTargets& Targets;
	FSceneViewData&      SceneViewData;
	FVector4             ClearColor = FVector4(0.01f, 0.01f, 0.01f, 1.0f);
};

class ENGINE_API IRenderPass
{
public:
	virtual ~IRenderPass() = default;

	virtual FPassDesc Describe() const { return {}; }

	// Describe() 에 위임하는 편의 접근자 (override 불필요)
	const char* GetName()   const { return Describe().Name; }
	EPassDomain GetDomain() const { return Describe().Domain; }

	virtual bool Execute(FPassContext& Context) = 0;
};
