# 0. 이 렌더러의 가장 큰 설계 의도

이 렌더러의 핵심 설계는 이것입니다.

```cpp
렌더 패스는 자원을 직접 만들지 않는다.
렌더 패스는 FSceneRenderTargets를 통해 이미 준비된 자원을 읽고 쓴다.
```

즉, 각 패스가 직접 `CreateTexture2D`, `CreateRenderTargetView`, `CreateDepthStencilView`를 호출하지 않습니다.

대신 큰 흐름은 이렇습니다.

```cpp
FSceneTargetManager
    ↓
렌더 타깃 생성 또는 외부 타깃 wrap
    ↓
FSceneRenderTargets
    ↓
FPassContext
    ↓
각 RenderPass
    ↓
SRV / RTV / DSV / UAV로 읽기/쓰기
    ↓
ResolveSceneColorTargets
    ↓
ViewportCompositor
    ↓
BackBuffer
```

이 구조의 목적은 분명합니다.

```cpp
게임 프레임이든 에디터 뷰포트든,
각 패스는 항상 같은 방식으로 Targets만 보고 렌더링하게 만든다.
```

그래서 `FSceneTargetManager`, `FSceneRenderTargets`, `FGPUTexture2D`, `WrapExternalColorTarget`, `SceneColorRead/Write`, `SwapSceneColor()` 같은 구조가 존재합니다.

---

# 1. 전체 파일 구조에서 먼저 봐야 할 축

이 렌더러를 읽을 때는 파일을 기능별로 나누어 보시는 것이 좋습니다.

```cpp
Frame/
    GameFrameRenderer.cpp
    EditorFrameRenderer.cpp
    SceneTargetManager.cpp
    FrameRequests.h
    Viewport/ViewportCompositor.cpp
    UI/FramePasses.cpp

Common/
    SceneRenderTargets.h

Scene/
    SceneRenderer.cpp
    Pipeline/ScenePipelineBuilder.cpp
    Pipeline/RenderPipeline.cpp
    Passes/PassContext.h
    Passes/SceneGeometryPasses.*
    Passes/SceneEffectsPasses.*
    Passes/SceneLightingPasses.*
    Passes/EditorWorldOverlayPasses.*
    Passes/SelectionHighlightPasses.*
    Passes/EditorScreenOverlayPasses.*

Features/
    Decal/
    Fog/
    Lighting/
    FireBall/
    Outline/
    Debug/
    PostProcess/

GraphicsCore/
    RenderDevice.h
    FullscreenPass.h
    RenderStateManager.*
```

각 폴더의 역할은 이렇게 보시면 됩니다.

| 위치                            | 역할                                              |
| ----------------------------- | ----------------------------------------------- |
| `Frame`                       | 프레임 단위 진입점, 게임/에디터 분기, 최종 합성                    |
| `Common/SceneRenderTargets.h` | 렌더 타깃 구조체 정의                                    |
| `Scene`                       | 씬 뷰 구성, 패스 파이프라인, 패스 실행                         |
| `Features`                    | 실제 효과 구현, 셰이더 바인딩, feature 내부 자원 관리             |
| `GraphicsCore`                | D3D11 device/context, fullscreen pass, state 관리 |

처음 읽을 때는 `Features`부터 들어가면 어렵습니다.
먼저 `Frame → SceneTargetManager → FSceneRenderTargets → SceneRenderer → Pipeline → Pass` 순서로 보는 것이 좋습니다.

---

# 2. 자원 관리의 최상위는 `FRenderer`

`FRenderer`는 전체 렌더링 시스템의 중심 객체입니다.

`Renderer.h`를 보면 다음을 가지고 있습니다.

```cpp
FRenderDevice RenderDevice;
std::unique_ptr<FSceneRenderer> SceneRenderer;
std::unique_ptr<FSceneTargetManager> SceneTargetManager;
std::unique_ptr<FViewportCompositor> ViewportCompositor;
std::unique_ptr<FScreenUIRenderer> ScreenUIRenderer;

std::unique_ptr<FFogRenderFeature> FogFeature;
std::unique_ptr<FOutlineRenderFeature> OutlineFeature;
std::unique_ptr<FDecalRenderFeature> DecalFeature;
std::unique_ptr<FVolumeDecalRenderFeature> VolumeDecalFeature;
std::unique_ptr<FFireBallRenderFeature> FireBallFeature;
std::unique_ptr<FLightRenderFeature> LightFeature;
std::unique_ptr<FShadowRenderFeature> ShadowFeature;
std::unique_ptr<FBloomRenderFeature> BloomFeature;
std::unique_ptr<FFXAARenderFeature> FXAAFeature;
```

여기서 역할을 나누면 이렇습니다.

```cpp
FRenderer
    = 전체 시스템 소유자

FRenderDevice
    = D3D11 Device / Context / SwapChain / BackBuffer 관리

FSceneTargetManager
    = SceneColor, Depth, GBuffer, Overlay 등 scene render target 관리

FSceneRenderer
    = scene view data 구성 + scene pass pipeline 실행

Feature들
    = fog, decal, bloom, outline 같은 실제 효과 구현

ViewportCompositor
    = 최종 SRV를 backbuffer에 합성
```

즉, `FRenderer`가 모든 것을 직접 그리는 것이 아니라, 각 모듈에게 책임을 나누어 줍니다.

---

# 3. 백버퍼와 scene render target은 다릅니다

초심자가 자주 헷갈리는 부분입니다.

이 렌더러에는 크게 두 종류의 렌더 타깃이 있습니다.

```cpp
1. BackBuffer
2. Scene Render Targets
```

## 3.1 BackBuffer

BackBuffer는 swap chain에 붙어 있는 최종 화면용 render target입니다.

`GraphicsCore/RenderDevice.h`의 `CreateRenderTargetAndDepthStencil()`에서 만들어집니다.

```cpp
SwapChain->GetBuffer(...)
Device->CreateRenderTargetView(BackBuffer, nullptr, &RenderTargetView)
```

그리고 backbuffer용 depth도 만듭니다.

```cpp
DepthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

Device->CreateDepthStencilView(...)
Device->CreateShaderResourceView(...)
```

BackBuffer는 최종적으로 화면에 present될 대상입니다.

## 3.2 Scene Render Targets

Scene render target은 scene을 그리기 위한 내부 작업용 target입니다.

대표적으로:

```cpp
InternalSceneColorA
InternalSceneColorB
GameSceneDepth
GBufferA/B/C
OverlayColor
OutlineMask
```

이 있습니다.

이것들은 `FSceneTargetManager`가 관리합니다.

중요한 차이는 이것입니다.

```cpp
SceneColor A/B에서 scene과 후처리를 만든다.
마지막에 ViewportCompositor가 그 결과를 BackBuffer에 그린다.
```

즉, 대부분의 scene pass는 backbuffer에 직접 그리지 않습니다.

---

# 4. `FGPUTexture2D`: 자원 하나의 최소 단위

파일:

```cpp
Common/SceneRenderTargets.h
```

핵심 구조체입니다.

```cpp
struct FGPUTexture2D
{
    FGPUTextureDesc Desc;

    ID3D11Texture2D* Texture = nullptr;

    ID3D11RenderTargetView*    RTV = nullptr;
    ID3D11ShaderResourceView*  SRV = nullptr;
    ID3D11DepthStencilView*    DSV = nullptr;
    ID3D11UnorderedAccessView* UAV = nullptr;

    std::vector<ID3D11ShaderResourceView*>  MipSRVs;
    std::vector<ID3D11UnorderedAccessView*> MipUAVs;
};
```

이 구조체는 “GPU 텍스처 하나와 그 텍스처를 사용하는 여러 입구”를 묶은 것입니다.

| 멤버        | 의미                         |
| --------- | -------------------------- |
| `Texture` | 실제 GPU 메모리                 |
| `RTV`     | 컬러 render target으로 쓰는 view |
| `SRV`     | shader에서 읽는 view           |
| `DSV`     | depth/stencil로 쓰는 view     |
| `UAV`     | compute/random write용 view |
| `MipSRVs` | mip 단위 읽기 view             |
| `MipUAVs` | mip 단위 쓰기 view             |

가장 중요한 개념은 이것입니다.

```cpp
Texture 자체가 SRV/RTV/DSV/UAV인 것이 아닙니다.
Texture는 실제 데이터이고,
SRV/RTV/DSV/UAV는 그 데이터를 어떤 방식으로 사용할지 나타내는 view입니다.
```

---

# 5. `FGPUTextureDesc`: 이 자원이 어떤 용도인지 기록

같은 파일에 `FGPUTextureDesc`가 있습니다.

```cpp
struct FGPUTextureDesc
{
    uint32 Width     = 0;
    uint32 Height    = 0;
    uint32 MipLevels = 1;

    DXGI_FORMAT TextureFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT SRVFormat     = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT RTVFormat     = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT DSVFormat     = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT UAVFormat     = DXGI_FORMAT_UNKNOWN;

    ETextureBindFlags BindFlags = ETextureBindFlags::None;
    bool bExternalWrapped = false;
};
```

이 구조체의 역할은 **이 텍스처가 어떤 목적으로 만들어졌는지 설명하는 메타데이터**입니다.

예를 들어 컬러 타깃은 보통:

```cpp
BindFlags = SRV | RTV
```

compute 출력까지 필요하면:

```cpp
BindFlags = SRV | RTV | UAV
```

Depth 타깃은:

```cpp
BindFlags = SRV | DSV
```

입니다.

특히 중요한 값은 이것입니다.

```cpp
bool bExternalWrapped;
```

이 값이 `false`면:

```cpp
FSceneTargetManager가 직접 만든 내부 리소스
ReleaseTexture()가 실제 COM 객체를 Release함
```

이 값이 `true`면:

```cpp
외부에서 빌려온 리소스
ReleaseTexture()가 실제 COM 객체를 Release하지 않음
포인터만 nullptr로 비움
```

이것이 `WrapExternalColorTarget()`과 `WrapExternalDepthTarget()`의 핵심입니다.

---

# 6. `FSceneRenderTargets`: 패스들이 보는 공통 계약서

파일:

```cpp
Common/SceneRenderTargets.h
```

핵심 구조체입니다.

```cpp
struct FSceneRenderTargets
{
    uint32 Width  = 0;
    uint32 Height = 0;

    FGPUTexture2D* FinalSceneColor = nullptr;
    FGPUTexture2D* SceneColorRead  = nullptr;
    FGPUTexture2D* SceneColorWrite = nullptr;
    FGPUTexture2D* OverlayColor    = nullptr;
    FGPUTexture2D* SceneDepth      = nullptr;
    FGPUTexture2D* GBufferA        = nullptr;
    FGPUTexture2D* GBufferB        = nullptr;
    FGPUTexture2D* GBufferC        = nullptr;
    FGPUTexture2D* OutlineMask     = nullptr;
};
```

이 구조체의 의도는 이것입니다.

```cpp
이번 scene render에서 사용할 모든 공통 render target을 하나로 묶어서
모든 pass에게 같은 형태로 전달한다.
```

패스는 `FPassContext`를 통해 이 구조체를 받습니다.

```cpp
struct FPassContext
{
    FRenderer&           Renderer;
    FSceneRenderTargets& Targets;
    FSceneViewData&      SceneViewData;
    FVector4             ClearColor;
};
```

따라서 패스는 보통 이렇게 씁니다.

```cpp
bool Execute(FPassContext& Context)
{
    FSceneRenderTargets& Targets = Context.Targets;

    // Targets.SceneColorRTV
    // Targets.SceneColorSRV
    // Targets.SceneDepthDSV
    // Targets.SceneDepthSRV
}
```

즉, 패스 입장에서는:

```cpp
이 target이 게임용 내부 target인지,
에디터 viewport에서 온 external target인지,
새로 만들어진 건지,
wrap된 건지
```

몰라도 됩니다.

항상 `Targets`만 보면 됩니다.

---

# 7. `SceneColorRead`와 `SceneColorWrite`의 정확한 의미

이 둘은 단순히 “읽기 버퍼”, “쓰기 버퍼”가 아닙니다.

정확한 의미는 다음입니다.

```cpp
SceneColorRead
    = 현재까지의 최신 scene color 결과가 들어 있는 버퍼

SceneColorWrite
    = 후처리 결과를 쓸 다음 버퍼
```

처음 게임 프레임에서는 이렇게 시작합니다.

```cpp
SceneColorRead  = InternalSceneColorA
SceneColorWrite = InternalSceneColorB
```

초반 geometry pass는 `SceneColorRead` 쪽에 씁니다.
즉, `ForwardOpaquePass`가 쓰는 `Targets.SceneColorRTV`는 초기에는 `InternalSceneColorA.RTV`입니다.

이후 후처리 패스가 실행되면 패턴이 바뀝니다.

```cpp
Fog:
    SceneColorRead.SRV를 읽음
    SceneColorWrite.RTV에 씀
    SwapSceneColor()
```

그 결과:

```cpp
SceneColorRead  = InternalSceneColorB
SceneColorWrite = InternalSceneColorA
```

가 됩니다.

따라서 가장 안전한 이해는 이것입니다.

```cpp
SceneColorRead는 "최신 결과".
SceneColorWrite는 "다음 결과를 쓸 작업지".
```

---

# 8. Compatibility view들이 있는 이유

`FSceneRenderTargets` 안에는 이런 포인터들도 있습니다.

```cpp
ID3D11RenderTargetView*   SceneColorRTV;
ID3D11ShaderResourceView* SceneColorSRV;

ID3D11RenderTargetView*   SceneColorScratchRTV;
ID3D11ShaderResourceView* SceneColorScratchSRV;

ID3D11DepthStencilView*   SceneDepthDSV;
ID3D11ShaderResourceView* SceneDepthSRV;
```

이들은 실제 소유자가 아닙니다.
`SceneColorRead`, `SceneColorWrite`, `SceneDepth`에서 꺼낸 **편의용 alias**입니다.

이 alias를 갱신하는 함수가:

```cpp
RefreshCompatibilityViews()
```

입니다.

핵심은 다음입니다.

```cpp
SceneColorRTV = SceneColorRead ? SceneColorRead->RTV : nullptr;
SceneColorSRV = SceneColorRead ? SceneColorRead->SRV : nullptr;

SceneColorScratchRTV = SceneColorWrite ? SceneColorWrite->RTV : nullptr;
SceneColorScratchSRV = SceneColorWrite ? SceneColorWrite->SRV : nullptr;

SceneDepthDSV = SceneDepth ? SceneDepth->DSV : nullptr;
SceneDepthSRV = SceneDepth ? SceneDepth->SRV : nullptr;
```

그래서 `SwapSceneColor()`는 단순히 포인터만 바꾸는 것이 아니라 alias도 다시 맞춥니다.

```cpp
void SwapSceneColor()
{
    if (!SceneColorRead || !SceneColorWrite)
    {
        return;
    }

    std::swap(SceneColorRead, SceneColorWrite);
    RefreshCompatibilityViews();
}
```

초심자에게 가장 중요한 경고는 이것입니다.

```cpp
SceneColorRTV가 항상 InternalSceneColorA.RTV인 것이 아닙니다.
SceneColorScratchRTV가 항상 InternalSceneColorB.RTV인 것도 아닙니다.

swap이 일어나면 둘의 실제 대상은 바뀝니다.
```

---

# 9. `FSceneTargetManager`: scene target의 실제 관리자

파일:

```cpp
Frame/SceneTargetManager.h
Frame/SceneTargetManager.cpp
```

이 클래스가 scene render target의 생성, 재사용, wrap, release를 담당합니다.

멤버는 다음과 같습니다.

```cpp
FGPUTexture2D InternalSceneColorA;
FGPUTexture2D InternalSceneColorB;
FGPUTexture2D GameSceneDepth;

FGPUTexture2D GBufferASurface;
FGPUTexture2D GBufferBSurface;
FGPUTexture2D GBufferCSurface;

FGPUTexture2D OverlayColorSurface;
FGPUTexture2D OutlineMaskSurface;

FGPUTexture2D WrappedFinalSceneColor;
FGPUTexture2D WrappedSceneDepth;
```

각각의 역할은 다음입니다.

| 자원                       | 역할                            |
| ------------------------ | ----------------------------- |
| `InternalSceneColorA`    | ping-pong용 SceneColor A       |
| `InternalSceneColorB`    | ping-pong용 SceneColor B       |
| `GameSceneDepth`         | 게임 프레임용 내부 depth              |
| `GBufferA/B/C`           | GBuffer용 타깃, 현재 pass는 비활성화 상태 |
| `OverlayColorSurface`    | 기본 overlay color              |
| `OutlineMaskSurface`     | outline mask 저장               |
| `WrappedFinalSceneColor` | 외부 viewport color target 포장   |
| `WrappedSceneDepth`      | 외부 viewport depth target 포장   |

여기서 이름에 `Wrapped`가 붙은 것은 “새로 만든 텍스처”가 아닙니다.
외부에서 받은 view pointer를 `FGPUTexture2D`처럼 보이도록 포장하는 구조입니다.

---

# 10. 컬러 텍스처 생성: `CreateColorTexture()`

파일:

```cpp
Frame/SceneTargetManager.cpp
```

함수:

```cpp
FSceneTargetManager::CreateColorTexture()
```

생성 흐름은 다음입니다.

```cpp
ReleaseTexture(OutTexture);

OutTexture.Desc.Width = Width;
OutTexture.Desc.Height = Height;
OutTexture.Desc.TextureFormat = Format;
OutTexture.Desc.SRVFormat = Format;
OutTexture.Desc.RTVFormat = Format;
OutTexture.Desc.UAVFormat = Format;
OutTexture.Desc.BindFlags = SRV | RTV;

if (bCreateUAV)
{
    OutTexture.Desc.BindFlags |= UAV;
}
```

그다음 실제 D3D 리소스를 만듭니다.

```cpp
Device->CreateTexture2D(...)
Device->CreateRenderTargetView(...)
Device->CreateShaderResourceView(...)

if (bCreateUAV)
{
    Device->CreateUnorderedAccessView(...)
}
```

즉, 컬러 텍스처는 보통 다음을 가집니다.

```cpp
Texture 있음
RTV 있음
SRV 있음
UAV 있을 수 있음
DSV 없음
```

이 렌더러에서 내부 scene color는 `R16G16B16A16_FLOAT`입니다.

```cpp
InternalSceneColorA  = R16G16B16A16_FLOAT
InternalSceneColorB  = R16G16B16A16_FLOAT
```

이것은 scene pass와 후처리를 HDR/linear 공간에서 처리하기 위한 선택입니다.

그리고 `bCreateUAV = true`로 생성됩니다.
따라서 scene color는 다음 세 방식으로 사용 가능합니다.

```cpp
RTV로 렌더링 출력
SRV로 후처리 입력
UAV로 compute 출력
```

Bloom composite가 `GetSceneColorWriteUAV()`를 쓸 수 있는 이유도 여기에 있습니다.

---

# 11. Depth 텍스처 생성: `CreateDepthTexture()`

함수:

```cpp
FSceneTargetManager::CreateDepthTexture()
```

핵심 포맷은 다음입니다.

```cpp
TextureFormat = DXGI_FORMAT_R24G8_TYPELESS;
SRVFormat     = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
DSVFormat     = DXGI_FORMAT_D24_UNORM_S8_UINT;
BindFlags     = SRV | DSV;
```

생성 순서는 다음입니다.

```cpp
Device->CreateTexture2D(...)
Device->CreateDepthStencilView(...)
Device->CreateShaderResourceView(...)
```

즉, depth texture는 보통 다음을 가집니다.

```cpp
Texture 있음
DSV 있음
SRV 있음
RTV 없음
UAV 없음
```

왜 depth에 SRV가 필요하냐면, depth는 나중에 shader에서 읽기 때문입니다.

```cpp
DepthPrepass:
    SceneDepthDSV에 depth를 씀

LightCulling:
    SceneDepthSRV를 읽음

Fog:
    SceneDepthSRV를 읽음

FireBall:
    SceneDepthSRV를 읽음

Decal:
    SceneDepthSRV를 읽음
```

즉, depth는:

```cpp
먼저 DSV로 쓰고,
나중에 SRV로 읽습니다.
```

---

# 12. 내부 target 생성과 캐싱

`FSceneTargetManager`는 매 프레임마다 무조건 texture를 새로 만들지 않습니다.

크기가 같으면 기존 자원을 재사용합니다.

## 12.1 `EnsureGameSceneTargets()`

게임 프레임에서 호출됩니다.

```cpp
EnsureGameSceneTargets(Device, Width, Height)
```

역할은:

```cpp
GameSceneDepth 준비
SupplementalTargets 준비
```

입니다.

만약 기존 `GameSceneDepth`가 있고 크기가 같으면:

```cpp
return EnsureSupplementalTargets(Device, Width, Height);
```

크기가 다르거나 없으면:

```cpp
Release();
CreateDepthTexture(GameSceneDepth);
EnsureSupplementalTargets();
```

를 합니다.

여기서 주의할 점이 있습니다.

```cpp
EnsureGameSceneTargets()는 크기가 달라지면 Release()를 호출합니다.
```

`Release()`는 `GameSceneDepth`뿐 아니라 supplemental target, wrapped target, external overlay target map까지 정리합니다.
즉, resize 상황에서는 관련 target들이 모두 재생성될 수 있습니다.

## 12.2 `EnsureSupplementalTargets()`

공통 보조 타깃들을 준비합니다.

```cpp
InternalSceneColorA
InternalSceneColorB
GBufferASurface
GBufferBSurface
GBufferCSurface
OverlayColorSurface
OutlineMaskSurface
```

이미 모두 있고 크기가 같으면 재사용합니다.

```cpp
if (InternalSceneColorA.RTV
    && InternalSceneColorB.RTV
    && GBufferASurface.RTV
    && GBufferBSurface.RTV
    && GBufferCSurface.RTV
    && OverlayColorSurface.RTV
    && OutlineMaskSurface.RTV
    && SupplementalTargetCacheWidth == Width
    && SupplementalTargetCacheHeight == Height)
{
    return true;
}
```

없거나 크기가 바뀌면:

```cpp
ReleaseSupplementalTargets();
CreateColorTexture(...);
```

를 합니다.

즉, 이 함수의 목적은:

```cpp
viewport 크기에 맞는 공통 scene target pool을 유지하는 것
```

입니다.

---

# 13. 게임 프레임의 자원 연결: `AcquireGameSceneTargets()`

게임 프레임은 `FGameFrameRenderer::Render()`에서 시작합니다.

```cpp
FSceneRenderTargets Targets;

Renderer.SceneTargetManager->AcquireGameSceneTargets(
    Renderer.GetDevice(),
    Renderer.GetRenderDevice().GetViewport(),
    Targets);
```

`AcquireGameSceneTargets()`는 내부 target들을 `FSceneRenderTargets`에 연결합니다.

```cpp
OutTargets.FinalSceneColor = nullptr;

OutTargets.SceneColorRead  = &InternalSceneColorA;
OutTargets.SceneColorWrite = &InternalSceneColorB;

OutTargets.OverlayColor    = &OverlayColorSurface;
OutTargets.SceneDepth      = &GameSceneDepth;

OutTargets.GBufferA        = &GBufferASurface;
OutTargets.GBufferB        = &GBufferBSurface;
OutTargets.GBufferC        = &GBufferCSurface;

OutTargets.OutlineMask     = &OutlineMaskSurface;

OutTargets.RefreshCompatibilityViews();
```

게임 프레임의 초기 상태를 그림으로 보면 이렇습니다.

```cpp
GameFrame 시작

SceneColorRead  ──> InternalSceneColorA
SceneColorWrite ──> InternalSceneColorB
SceneDepth      ──> GameSceneDepth
OverlayColor    ──> OverlayColorSurface
OutlineMask     ──> OutlineMaskSurface
FinalSceneColor ──> nullptr
```

여기서 `FinalSceneColor = nullptr`인 이유가 중요합니다.

게임 프레임은 최종 결과를 외부 viewport render target에 복사하지 않습니다.
그 대신 최종 `Targets.SceneColorSRV`를 `ViewportCompositor`가 바로 backbuffer에 그립니다.

---

# 14. 에디터 프레임의 자원 연결: `WrapExternalSceneTargets()`

에디터 프레임은 `FEditorFrameRenderer::Render()`에서 시작합니다.

에디터는 viewport마다 이미 만들어진 target을 넘깁니다.

요청 구조체는:

```cpp
FViewportScenePassRequest
```

입니다.

그 안에는 다음이 있습니다.

```cpp
ID3D11RenderTargetView*   RenderTargetView;
ID3D11ShaderResourceView* RenderTargetShaderResourceView;

ID3D11DepthStencilView*   DepthStencilView;
ID3D11ShaderResourceView* DepthShaderResourceView;

D3D11_VIEWPORT Viewport;
```

즉, 에디터 viewport는 이미 color target과 depth target을 갖고 있습니다.

그런데 scene pass들은 `FViewportScenePassRequest`를 모릅니다.
패스들은 오직 `FSceneRenderTargets`만 봅니다.

그래서 호출되는 함수가 이것입니다.

```cpp
WrapExternalSceneTargets()
```

이 함수는 외부 target을 렌더러의 표준 target 구조에 끼워 넣습니다.

핵심 연결은 다음입니다.

```cpp
WrapExternalColorTarget(
    Width,
    Height,
    RenderTargetView,
    RenderTargetShaderResourceView,
    WrappedFinalSceneColor);

WrapExternalDepthTarget(
    Width,
    Height,
    DepthStencilView,
    DepthShaderResourceView,
    WrappedSceneDepth);

OutTargets.FinalSceneColor = &WrappedFinalSceneColor;
OutTargets.SceneColorRead  = &InternalSceneColorA;
OutTargets.SceneColorWrite = &InternalSceneColorB;
OutTargets.OverlayColor    = &ExternalOverlayTargets->OverlayColor;
OutTargets.SceneDepth      = &WrappedSceneDepth;
OutTargets.GBufferA        = &GBufferASurface;
OutTargets.GBufferB        = &GBufferBSurface;
OutTargets.GBufferC        = &GBufferCSurface;
OutTargets.OutlineMask     = &OutlineMaskSurface;
```

에디터 프레임의 target 상태는 이렇습니다.

```cpp
Editor viewport 하나

FinalSceneColor ──> 외부 viewport color target
SceneColorRead  ──> InternalSceneColorA
SceneColorWrite ──> InternalSceneColorB
SceneDepth      ──> 외부 viewport depth target
OverlayColor    ──> viewport별 external overlay target
```

핵심은 이것입니다.

```cpp
에디터의 외부 RenderTargetView는 SceneColorRead가 아닙니다.
외부 RenderTargetView는 FinalSceneColor입니다.

중간 scene rendering과 후처리는 여전히 내부 SceneColor A/B에서 합니다.
마지막에 SceneColorRead를 FinalSceneColor로 복사합니다.
```

---

# 15. `WrapExternalColorTarget()` 자세히 보기

함수는 다음과 같습니다.

```cpp
void FSceneTargetManager::WrapExternalColorTarget(
    uint32 Width,
    uint32 Height,
    ID3D11RenderTargetView* RenderTargetView,
    ID3D11ShaderResourceView* ShaderResourceView,
    FGPUTexture2D& OutTexture)
{
    ReleaseTexture(OutTexture);

    OutTexture.Desc = {};
    OutTexture.Desc.Width = Width;
    OutTexture.Desc.Height = Height;
    OutTexture.Desc.BindFlags = ETextureBindFlags::SRV | ETextureBindFlags::RTV;
    OutTexture.Desc.bExternalWrapped = true;

    OutTexture.RTV = RenderTargetView;
    OutTexture.SRV = ShaderResourceView;
}
```

이 함수가 **하는 일**은 이것입니다.

```cpp
외부에서 받은 RTV/SRV 포인터를 FGPUTexture2D 안에 넣는다.
bExternalWrapped = true로 표시한다.
```

이 함수가 **하지 않는 일**이 더 중요합니다.

```cpp
CreateTexture2D 하지 않음
CreateRenderTargetView 하지 않음
CreateShaderResourceView 하지 않음
AddRef 하지 않음
OutTexture.Texture를 채우지 않음
```

즉, 이것은 생성 함수가 아닙니다.

정확한 의미는 다음입니다.

```cpp
이미 외부에서 만들어진 color render target을
렌더러 내부 코드가 FGPUTexture2D처럼 다룰 수 있게 포장하는 함수
```

그래서 `FinalSceneColor->RTV`처럼 접근할 수 있게 됩니다.

---

# 16. `WrapExternalDepthTarget()` 자세히 보기

Depth도 비슷합니다.

```cpp
void FSceneTargetManager::WrapExternalDepthTarget(
    uint32 Width,
    uint32 Height,
    ID3D11DepthStencilView* DepthStencilView,
    ID3D11ShaderResourceView* ShaderResourceView,
    FGPUTexture2D& OutTexture)
{
    ReleaseTexture(OutTexture);

    OutTexture.Desc = {};
    OutTexture.Desc.Width = Width;
    OutTexture.Desc.Height = Height;
    OutTexture.Desc.BindFlags = ETextureBindFlags::SRV | ETextureBindFlags::DSV;
    OutTexture.Desc.bExternalWrapped = true;

    OutTexture.DSV = DepthStencilView;
    OutTexture.SRV = ShaderResourceView;
}
```

이 함수도 새 depth texture를 만들지 않습니다.

그냥 외부에서 받은:

```cpp
DepthStencilView
DepthShaderResourceView
```

를 `FGPUTexture2D` 안에 넣습니다.

그래서 패스들은 에디터 외부 depth인지 게임 내부 depth인지 몰라도 됩니다.

```cpp
Targets.SceneDepthDSV
Targets.SceneDepthSRV
```

로 동일하게 접근합니다.

---

# 17. wrap의 소유권 규칙

`ReleaseTexture()`를 보면 wrap의 의도가 명확합니다.

```cpp
if (!Texture.IsExternal())
{
    ReleaseCOM(Texture.UAV);
    ReleaseCOM(Texture.DSV);
    ReleaseCOM(Texture.SRV);
    ReleaseCOM(Texture.RTV);
    ReleaseCOM(Texture.Texture);
}
else
{
    Texture.UAV = nullptr;
    Texture.DSV = nullptr;
    Texture.SRV = nullptr;
    Texture.RTV = nullptr;
    Texture.Texture = nullptr;
}
```

즉:

```cpp
내부 생성 리소스:
    FSceneTargetManager가 소유하고 Release한다.

외부 wrapped 리소스:
    FSceneTargetManager가 소유하지 않는다.
    Release하지 않고 포인터만 비운다.
```

중요한 점은 `WrapExternalColorTarget()`이 `AddRef()`도 하지 않는다는 것입니다.

따라서 외부 render target의 lifetime은 외부 소유자가 보장해야 합니다.

정리하면:

```cpp
CreateColorTexture()
    = 자원을 새로 만들고 FSceneTargetManager가 소유

WrapExternalColorTarget()
    = 외부 자원을 빌려와 FGPUTexture2D처럼 보이게 포장
```

---

# 18. wrap이 없으면 구조가 어떻게 나빠지는가

wrap이 없다면 에디터 렌더링 패스마다 이런 분기가 필요해집니다.

```cpp
if (Editor)
{
    ScenePass.RenderTargetView 사용
    ScenePass.DepthStencilView 사용
}
else
{
    Targets.SceneColorRTV 사용
    Targets.SceneDepthDSV 사용
}
```

이렇게 되면 모든 패스가 게임/에디터 차이를 알아야 합니다.

현재 구조는 반대로 되어 있습니다.

```cpp
게임:
    내부 target을 FSceneRenderTargets에 연결

에디터:
    외부 target을 wrap해서 FSceneRenderTargets에 연결

패스:
    항상 FSceneRenderTargets만 사용
```

그래서 wrap은 단순한 편의 함수가 아닙니다.

```cpp
wrap은 외부 viewport target을 기존 scene pipeline에 태우기 위한 adapter입니다.
```

---

# 19. 외부 overlay target 관리

에디터에서는 viewport마다 overlay가 따로 필요합니다.

예를 들어:

```cpp
A viewport에는 grid가 보이고
B viewport에는 outline이 다르고
C viewport에는 debug line이 다를 수 있습니다.
```

그래서 `FSceneTargetManager`는 외부 viewport별 overlay target을 map으로 관리합니다.

```cpp
std::unordered_map<uint64, FExternalOverlayTargets> ExternalOverlayTargetMap;
```

key는 외부 color RTV와 depth DSV 포인터를 섞어서 만듭니다.

```cpp
MakeExternalOverlayKey(RenderTargetView, DepthStencilView)
```

`EnsureExternalOverlayTargets()`는 다음을 합니다.

```cpp
1. RenderTargetView / DepthStencilView / Width / Height 확인
2. key 생성
3. map에서 기존 overlay target 검색
4. 크기가 같고 기존 RTV가 있으면 재사용
5. 없거나 크기가 다르면 새 OverlayColor 생성
```

즉, 에디터 viewport마다 별도 overlay color를 유지합니다.

`WrapExternalSceneTargets()`에서는 이것을:

```cpp
OutTargets.OverlayColor = &ExternalOverlayTargets->OverlayColor;
```

로 연결합니다.

---

# 20. `FSceneRenderer`: scene view data와 pass pipeline 연결

게임/에디터 프레임 모두 결국 이것을 호출합니다.

```cpp
Renderer.GetSceneRenderer().RenderSceneView(...)
```

`SceneRenderer.cpp`의 흐름은 다음입니다.

```cpp
RenderSceneView()
    ↓
Targets.IsValid() 확인
    ↓
RenderMode에 따라 lighting model 설정
    ↓
wireframe override 적용
    ↓
FPassContext 구성
    ↓
BuildDefaultSceneRenderPipeline()
    ↓
Pipeline.Execute()
    ↓
ResolveSceneColorTargets()
```

핵심 부분은 이것입니다.

```cpp
FPassContext PassContext
{
    Renderer,
    Targets,
    SceneViewData,
    FVector4(ClearColor[0], ClearColor[1], ClearColor[2], ClearColor[3])
};

FRenderPipeline Pipeline;
BuildDefaultSceneRenderPipeline(Pipeline, *MeshPassProcessor);

Pipeline.Execute(PassContext);

Renderer.ResolveSceneColorTargets(...);
```

즉, `FSceneRenderer`는 직접 fog나 bloom을 그리는 것이 아닙니다.

```cpp
FSceneRenderer는 pass pipeline을 만들고 실행하는 조율자입니다.
```

---

# 21. `FRenderPipeline`: 패스 실행기

파일:

```cpp
Scene/Pipeline/RenderPipeline.cpp
```

실행은 단순합니다.

```cpp
for (const std::unique_ptr<IRenderPass>& Pass : PassSequence.GetPasses())
{
    Context.Renderer.PreparePassDomain(Pass->GetDomain(), Context.Targets);

    if (!Pass->Execute(Context))
    {
        return false;
    }
}
```

각 pass 실행 전에:

```cpp
PreparePassDomain()
```

이 호출됩니다.

이 함수는 `Graphics`, `Compute`, `Copy` 도메인에 따라 바인딩 상태를 정리합니다.

---

# 22. `PreparePassDomain()`의 의도

파일:

```cpp
Renderer.cpp
```

핵심은 다음입니다.

```cpp
case Compute:
    ClearAllGraphicsState();
    ClearAllComputeState();
    UnbindResourceEverywhere(SceneColorRead.Texture);
    UnbindResourceEverywhere(SceneColorWrite.Texture);
    break;

case Copy:
    ClearAllGraphicsState();
    ClearAllComputeState();
    break;

case Graphics:
    ClearAllComputeState();
    break;
```

이 함수의 목적은 D3D11 resource binding hazard를 줄이는 것입니다.

예를 들어 어떤 texture가 이전 pass에서 `SRV`로 묶여 있는데, 다음 pass에서 `UAV`나 `RTV`로 쓰려고 하면 충돌이 날 수 있습니다.

그래서 compute pass 전에는 특히 강하게 상태를 정리합니다.

여기서 중요한 세부사항이 있습니다.

```cpp
UnbindResourceEverywhere()는 Texture 포인터가 있어야 가능합니다.
```

내부 `InternalSceneColorA/B`는 `Texture` 포인터가 있습니다.

하지만 `WrapExternalColorTarget()`으로 감싼 `WrappedFinalSceneColor`는 `Texture` 포인터가 없습니다.
현재 구조에서는 `WrappedFinalSceneColor`를 중간 compute 대상으로 쓰지 않기 때문에 큰 문제가 없습니다.

다만 나중에 external wrapped target 자체를 적극적으로 SRV/RTV/UAV 전환하며 쓰고 싶다면, `RTV->GetResource()` 또는 `SRV->GetResource()`로 실제 resource pointer를 보관하는 설계를 고려할 수 있습니다.

---

# 23. 기본 Scene Pipeline 순서

파일:

```cpp
Scene/Pipeline/ScenePipelineBuilder.cpp
```

현재 기본 순서는 다음입니다.

```cpp
ClearSceneTargets
UploadMeshBuffers
DepthPrepass
LightCullingCompute
ShadowMap
ForwardOpaque

MeshDecal
DecalComposite
FogPost
FireBall
ForwardTransparent
Bloom

EditorGrid

OutlineMask
OutlineComposite

EditorLine
EditorPrimitive
```

이 순서는 중요합니다.

왜냐하면 각 패스가 이전 패스의 결과를 읽기 때문입니다.

예를 들어:

```cpp
DepthPrepass
    ↓
LightCullingCompute가 SceneDepthSRV를 읽음

ForwardOpaque
    ↓
DecalComposite / Fog / Bloom이 SceneColorSRV를 읽음

OutlineMask
    ↓
OutlineComposite가 OutlineMaskSRV를 읽음

Scene pass 전체
    ↓
ResolveSceneColorTargets가 SceneColorRead를 읽음
```

현재 `GBufferPass`는 코드가 있지만 꺼져 있습니다.

```cpp
// OutPipeline.AddPass(std::make_unique<FGBufferPass>(MeshPassProcessor));
```

따라서 현재 렌더러는 구조상 GBuffer target을 준비하지만, 실제로는 forward 중심입니다.

---

# 24. `FPassDesc`: 현재는 의도 선언에 가깝다

패스는 `Describe()`로 자신이 무엇을 읽고 쓰는지 선언합니다.

```cpp
struct FPassDesc
{
    const char*     Name;
    EPassDomain     Domain;
    EPassCategory   Category;
    FPassTargetMask Reads;
    FPassTargetMask Writes;
};
```

예를 들어 Fog pass는 이렇게 선언합니다.

```cpp
.Reads  = SceneColor | SceneDepth
.Writes = SceneColorScratch
```

의미는:

```cpp
SceneColor를 SRV로 읽고
SceneDepth를 SRV로 읽고
SceneColorScratch, 즉 SceneColorWrite에 씁니다.
```

다만 현재 `RenderPipeline::Execute()`에서는 `Reads/Writes`를 기반으로 자동 barrier나 dependency scheduling을 하지는 않습니다.

실제로 실행에 쓰이는 것은 주로:

```cpp
Pass->GetDomain()
```

입니다.

즉, 현재 `FPassDesc`의 역할은:

```cpp
패스 의도 문서화
향후 frame graph / validation / resource hazard detection 기반
debug UI 표시 가능성
```

에 가깝습니다.

나중에 자동 resource tracking을 붙인다면 `Reads/Writes`를 매우 정확히 맞춰야 합니다.

예를 들어 `ClearSceneTargetsPass`는 실제로 `OverlayColorRTV`도 clear하지만 `Describe()`의 `Writes`에는 `OverlayColor`가 빠져 있습니다.
현재는 실행 제어에 사용하지 않으므로 당장 큰 문제는 아니지만, 향후 자동 검증 시스템에서는 수정해야 할 지점입니다.

---

# 25. 패스별 자원 읽기/쓰기 흐름

이제 실제로 어떤 패스가 무엇을 읽고 쓰는지 전체 흐름으로 보겠습니다.

---

## 25.1 `FClearSceneTargetsPass`

파일:

```cpp
Scene/Passes/SceneGeometryPasses.cpp
```

읽기:

```cpp
없음
```

쓰기:

```cpp
SceneColorRTV
SceneColorScratchRTV
SceneDepthDSV
GBufferARTV
GBufferBRTV
GBufferCRTV
OverlayColorRTV
OutlineMaskRTV
```

실제 clear 대상:

```cpp
ClearRenderTargetView(SceneColorRTV)
ClearRenderTargetView(SceneColorScratchRTV)
ClearDepthStencilView(SceneDepthDSV)
ClearRenderTargetView(GBufferARTV)
ClearRenderTargetView(GBufferBRTV)
ClearRenderTargetView(GBufferCRTV)
ClearRenderTargetView(OverlayColorRTV)
ClearRenderTargetView(OutlineMaskRTV)
```

이 패스는 이번 scene view에서 사용할 주요 target들을 초기화합니다.

중요한 점:

```cpp
에디터에서도 FinalSceneColor는 여기서 clear하지 않습니다.
내부 SceneColor A/B와 overlay 등이 clear됩니다.
FinalSceneColor는 마지막 resolve에서 덮어씁니다.
```

---

## 25.2 `FUploadMeshBuffersPass`

읽기/쓰기 target 관점에서는 scene render target을 직접 다루지 않습니다.

역할은 mesh buffer 업로드입니다.

```cpp
Processor.UploadMeshBuffers(...)
```

도메인은 `Copy`입니다.

---

## 25.3 `FDepthPrepass`

읽기:

```cpp
없음
```

쓰기:

```cpp
SceneDepthDSV
```

코드 흐름:

```cpp
BeginPass(
    0,
    nullptr,
    SceneDepthDSV,
    ...);

Processor.ExecutePass(..., EMeshPassType::DepthPrepass);
```

즉, color target 없이 depth만 씁니다.

이 depth는 이후 다음 패스들에서 SRV로 읽힙니다.

```cpp
LightCullingCompute
DecalComposite
FogPost
FireBall
EditorGrid
```

---

## 25.4 `FLightCullingComputePass`

읽기:

```cpp
SceneDepthSRV
```

쓰기:

```cpp
Light feature 내부 UAV buffer
```

`LightRenderFeature` 쪽에서:

```cpp
ID3D11ShaderResourceView* TileDepthSRV[1] = { Targets.SceneDepthSRV };
```

로 depth를 읽습니다.

이 패스는 `FSceneRenderTargets` 내부 color target에 직접 쓰지는 않습니다.
대신 light culling 결과를 feature 내부 buffer에 씁니다.

즉, 이 구조는 다음을 보여줍니다.

```cpp
모든 GPU 자원이 FSceneTargetManager에 있는 것은 아닙니다.
feature 내부 전용 자원은 feature가 직접 관리할 수 있습니다.
```

---

## 25.5 `FShadowMapPass`

쓰기:

```cpp
Shadow feature 내부 shadow map
```

`FSceneRenderTargets`에는 `ShadowMap` 포인터가 직접 없습니다.
`ESceneTarget::ShadowMap` enum은 있지만 실제 shadow map resource는 `FShadowRenderFeature` 내부에서 관리됩니다.

이것도 feature-owned resource의 예입니다.

---

## 25.6 `FForwardOpaquePass`

읽기:

```cpp
SceneDepthDSV를 depth test에 사용
ShadowMap
Material textures
Light buffers
```

쓰기:

```cpp
SceneColorRTV
```

코드 흐름:

```cpp
BeginPass(
    SceneColorRTV,
    SceneDepthDSV,
    ...);

Processor.ExecutePass(..., EMeshPassType::ForwardOpaque);
```

초기 상태에서는:

```cpp
SceneColorRTV = InternalSceneColorA.RTV
```

이므로 opaque geometry 결과는 A에 쌓입니다.

이 패스는 후처리 패스가 아닙니다.
따라서 `SwapSceneColor()`를 하지 않습니다.

---

## 25.7 `FMeshDecalPass`

읽기:

```cpp
SceneDepth
```

쓰기:

```cpp
SceneColor
```

이 패스는 mesh pass processor를 통해 decal mesh를 그립니다.

개념상:

```cpp
depth를 참고하면서
현재 SceneColor 위에 decal mesh를 그림
```

입니다.

이것도 전체 화면 후처리 ping-pong이라기보다는 geometry/blend 계열입니다.

---

## 25.8 `FDecalCompositePass`

이 패스는 decal mode에 따라 두 경로가 있습니다.

### 일반 `FDecalRenderFeature`

읽기:

```cpp
Targets.GetSceneColorShaderResource()
Targets.SceneDepthSRV
Decal buffer / texture array
```

쓰기:

```cpp
Targets.GetSceneColorWriteRenderTarget()
```

끝:

```cpp
Targets.SwapSceneColor()
```

즉, 전형적인 ping-pong 후처리 패스입니다.

```cpp
현재 SceneColor 읽기
Depth 읽기
Decal projection 계산
SceneColorWrite에 새 결과 출력
SwapSceneColor()
```

### `FVolumeDecalRenderFeature`

읽기:

```cpp
SceneDepthSRV
```

쓰기:

```cpp
SceneColorRTV
```

이 경로는 volume decal geometry를 현재 SceneColor에 직접 blend하는 방식입니다.

```cpp
OMSetRenderTargets(1, &Targets.SceneColorRTV, nullptr);
```

즉, 이 경우는 `SceneColorWrite`를 쓰지 않으므로 일반적인 ping-pong swap을 하지 않습니다.

---

## 25.9 `FFogPostPass`

읽기:

```cpp
Targets.GetSceneColorShaderResource()
Targets.SceneDepthSRV
Fog structured buffer
```

쓰기:

```cpp
Targets.GetSceneColorWriteRenderTarget()
```

끝:

```cpp
Targets.SwapSceneColor()
```

Fog는 대표적인 후처리 패스입니다.

```cpp
현재 화면 색 + depth + fog volume 정보
    ↓
fog가 적용된 새 화면
```

이므로 반드시 `SceneColorRead → SceneColorWrite → Swap` 패턴입니다.

---

## 25.10 `FFireBallPass`

읽기:

```cpp
SceneDepthSRV
```

쓰기:

```cpp
SceneColorRTV
```

`FireBallRenderFeature`는 fullscreen pass로 depth를 읽고 현재 SceneColor에 그립니다.

```cpp
SRV:
    Targets.SceneDepthSRV

RTV:
    Targets.SceneColorRTV
```

이 패스는 현재 scene color 위에 효과를 덧그리는 성격입니다.

따라서 `SceneColorWrite`를 쓰지 않고, `SwapSceneColor()`도 없습니다.

---

## 25.11 `FForwardTransparentPass`

읽기:

```cpp
SceneDepthDSV를 depth test에 사용
```

쓰기:

```cpp
SceneColorRTV
```

투명 오브젝트를 현재 scene color 위에 blend합니다.

이 패스도 ping-pong 후처리가 아닙니다.

```cpp
현재 SceneColor에 직접 그린다.
swap 없음.
```

---

## 25.12 `FBloomPass`

Bloom은 두 단계의 ping-pong이 섞여 있습니다.

### Bloom feature 내부 ping-pong

Bloom 내부에는 feature-owned texture가 있습니다.

```cpp
BloomBrightness
BloomScratch
```

blur 과정에서 둘을 번갈아 씁니다.

```cpp
read  = BloomBrightnessSRV 또는 BloomScratchSRV
write = BloomScratchUAV 또는 BloomBrightnessUAV
```

이건 `FSceneRenderTargets`의 SceneColor ping-pong이 아니라 Bloom feature 내부 ping-pong입니다.

### SceneColor composite ping-pong

마지막 composite 단계에서는 scene color를 갱신합니다.

읽기:

```cpp
Targets.SceneColorSRV
finalBloomSRV
```

쓰기:

```cpp
Targets.GetSceneColorWriteUAV()
```

끝:

```cpp
Targets.SwapSceneColor()
```

즉, Bloom composite는 SceneColor ping-pong 패스입니다.

---

## 25.13 `FEditorGridPass`

읽기:

```cpp
SceneDepth
```

쓰기:

```cpp
OverlayColorRTV가 있으면 OverlayColorRTV
없으면 SceneColorRTV
```

코드:

```cpp
ID3D11RenderTargetView* OverlayRenderTarget =
    Context.Targets.OverlayColorRTV
        ? Context.Targets.OverlayColorRTV
        : Context.Targets.SceneColorRTV;
```

의도는 다음입니다.

```cpp
에디터 overlay target이 있으면 scene color와 분리해서 overlay에 그림.
없으면 fallback으로 scene color에 직접 그림.
```

swap은 없습니다.

---

## 25.14 `FOutlineMaskPass`

읽기:

```cpp
선택된 outline item 정보
```

쓰기:

```cpp
SceneDepthDSV의 stencil
OutlineMaskRTV
```

Outline feature는 먼저 stencil을 사용해서 선택된 mesh 영역을 표시하고, 그 결과를 바탕으로 `OutlineMaskRTV`에 mask를 씁니다.

즉:

```cpp
SceneDepth의 stencil 이용
    ↓
OutlineMaskRTV에 선택 영역 mask 작성
```

입니다.

---

## 25.15 `FOutlineCompositePass`

읽기:

```cpp
OutlineMaskSRV
```

쓰기:

```cpp
OverlayColorRTV가 있으면 OverlayColorRTV
없으면 SceneColorRTV
```

코드:

```cpp
ID3D11RenderTargetView* CompositeRTV =
    Targets.OverlayColorRTV ? Targets.OverlayColorRTV : Targets.SceneColorRTV;
```

즉, outline은 가능하면 overlay target에 합성됩니다.

이 패스도 scene color 전체를 새로 만드는 후처리가 아니므로 `SwapSceneColor()`가 없습니다.

---

## 25.16 `FEditorLinePass`, `FEditorPrimitivePass`

이 둘도 overlay 계열입니다.

쓰기:

```cpp
OverlayColorRTV 또는 SceneColorRTV fallback
```

`DebugLineRenderFeature`도 다음 패턴을 사용합니다.

```cpp
ID3D11RenderTargetView* OverlayRenderTarget =
    Targets.OverlayColorRTV
        ? Targets.OverlayColorRTV
        : Targets.SceneColorRTV;
```

즉, 에디터용 시각화는 scene color와 분리된 overlay color에 그리는 것이 기본 의도입니다.

---

# 26. Fullscreen pass 구조

후처리 패스 대부분은 `ExecuteFullscreenPass()`를 사용합니다.

파일:

```cpp
GraphicsCore/FullscreenPass.h
```

흐름은 다음입니다.

```cpp
BeginFullscreenPass()
    ↓
BindFullscreenPassResources()
    ↓
Draw()
    ↓
ClearFullscreenPassResources()
    ↓
EndFullscreenPass()
```

핵심은 `ClearFullscreenPassResources()`입니다.

```cpp
PSSetConstantBuffers(slot, nullptr)
PSSetShaderResources(slot, nullptr)
PSSetSamplers(slot, nullptr)
```

즉, fullscreen pass에서 사용한 SRV, constant buffer, sampler는 pass 뒤에 null로 풀립니다.

이것은 다음 pass에서 같은 resource를 RTV/UAV로 쓸 때 충돌을 줄이는 데 중요합니다.

---

# 27. Resolve 단계: scene pass 이후의 최종 후처리

`FSceneRenderer::RenderSceneView()`는 scene pipeline 실행 후 마지막에 이 함수를 호출합니다.

```cpp
Renderer.ResolveSceneColorTargets(...)
```

파일:

```cpp
Renderer.cpp
```

이 함수는 세 단계입니다.

```cpp
1. ToneMapping
2. Optional FXAA
3. FinalSceneColor가 있으면 final blit
```

---

## 27.1 ToneMapping

읽기:

```cpp
Targets.SceneColorRead->SRV
```

쓰기:

```cpp
Targets.SceneColorWrite->RTV
```

끝:

```cpp
Targets.SwapSceneColor()
```

즉, tone mapping도 ping-pong 후처리입니다.

---

## 27.2 FXAA

옵션이 켜져 있으면:

```cpp
FXAAFeature->Render(..., Targets)
```

가 호출됩니다.

FXAA도 다음 패턴입니다.

```cpp
SceneColorRead.SRV 읽기
SceneColorWrite.RTV 쓰기
SwapSceneColor()
```

---

## 27.3 FinalSceneColor로 복사

마지막으로 이것을 확인합니다.

```cpp
Targets.NeedsSceneColorResolve()
```

구현은 다음입니다.

```cpp
return FinalSceneColor != nullptr
    && SceneColorRead != nullptr
    && FinalSceneColor != SceneColorRead;
```

게임 프레임은:

```cpp
FinalSceneColor = nullptr
```

이므로 final blit이 없습니다.

에디터 프레임은:

```cpp
FinalSceneColor = WrappedFinalSceneColor
```

이므로 final blit이 있습니다.

읽기:

```cpp
Targets.SceneColorRead->SRV
```

쓰기:

```cpp
Targets.FinalSceneColor->RTV
```

즉, 에디터에서는 이 단계에서 내부 SceneColor A/B의 최종 결과가 외부 viewport render target으로 복사됩니다.

---

# 28. GameFrame 전체 흐름

`FGameFrameRenderer::Render()` 기준으로 보면 다음입니다.

```cpp
FGameFrameRenderer::Render()
    ↓
AcquireGameSceneTargets()
    ↓
BuildRenderFrameContext()
BuildRenderViewContext()
    ↓
SceneRenderer.BuildSceneViewData()
    ↓
DecalTextureCache.ResolveTextureArray()
    ↓
SceneRenderer.RenderSceneView()
        ↓
        BuildDefaultSceneRenderPipeline()
        ↓
        Pipeline.Execute()
        ↓
        ResolveSceneColorTargets()
    ↓
FViewportCompositeItem 구성
    SceneColorSRV = Targets.SceneColorSRV
    SceneDepthSRV = Targets.SceneDepthSRV
    OverlayColorSRV = Targets.OverlayColorSRV
    ↓
FramePipeline
    FViewportCompositePass
    ↓
BackBuffer RTV
```

게임 프레임은 이렇게 이해하시면 됩니다.

```cpp
내부 SceneColor A/B에서 scene을 만들고,
최종 SceneColorSRV를 바로 backbuffer에 합성합니다.
```

그림으로 보면:

```cpp
InternalSceneColorA/B
    ↓
Scene passes + post process + tone mapping
    ↓
Targets.SceneColorSRV
    ↓
ViewportCompositor
    ↓
BackBuffer
```

---

# 29. EditorFrame 전체 흐름

`FEditorFrameRenderer::Render()` 기준입니다.

```cpp
FEditorFrameRenderer::Render()
    ↓
CompositeItems = Request.CompositeItems 복사
    ↓
for each ScenePass:
    WrapExternalSceneTargets()
        ↓
    BuildRenderFrameContext()
    BuildRenderViewContext()
        ↓
    SceneRenderer.BuildSceneViewData()
        ↓
    DecalTextureCache.ResolveTextureArray()
        ↓
    SceneRenderer.RenderSceneView()
        ↓
        내부 SceneColor A/B에서 scene render
        ResolveSceneColorTargets()
            ↓
            SceneColorRead → FinalSceneColor
    ↓
    OverlayBindings에 OverlayColorSRV 저장
    ↓
CompositeItems와 OverlayBindings 매칭
    ↓
FramePipeline
    FViewportCompositePass
    FScreenUIPass
    ↓
BackBuffer
```

에디터 프레임은 이렇게 이해하시면 됩니다.

```cpp
외부 viewport target을 wrap한다.
하지만 중간 렌더링은 내부 SceneColor A/B에서 한다.
마지막에 내부 결과를 외부 viewport target으로 복사한다.
그 외부 viewport SRV를 backbuffer에 합성한다.
그 위에 UI를 그린다.
```

그림:

```cpp
External viewport RTV/SRV/DSV/SRV
    ↓
WrapExternalSceneTargets()
    ↓
InternalSceneColorA/B에서 렌더링
    ↓
ResolveSceneColorTargets()
    ↓
FinalSceneColor, 즉 external viewport RTV에 blit
    ↓
external viewport SRV
    ↓
ViewportCompositor
    ↓
BackBuffer
    ↓
Screen UI
```

---

# 30. `ViewportCompositor`: 최종 합성

파일:

```cpp
Frame/Viewport/ViewportCompositor.cpp
```

입력은:

```cpp
FViewportCompositeItem
```

입니다.

주요 멤버:

```cpp
SceneColorSRV
SceneDepthSRV
OverlayColorSRV
Rect
Mode
```

`ResolveSourceSRV()`는 composite mode에 따라 source를 고릅니다.

```cpp
DepthView:
    SceneDepthSRV

SceneColor:
    SceneColorSRV
```

그리고 backbuffer에 fullscreen triangle을 그립니다.

그 후 overlay가 있으면 한 번 더 그립니다.

```cpp
if (Item.OverlayColorSRV)
{
    OverlayColorSRV를 alpha blend로 backbuffer 위에 그림
}
```

즉, 최종 화면은:

```cpp
SceneColorSRV
    ↓
BackBuffer

OverlayColorSRV
    ↓
BackBuffer 위에 alpha blend
```

입니다.

---

# 31. 자원 소유권 분류

이 렌더러의 자원을 소유권 기준으로 나누면 이해가 쉬워집니다.

## 31.1 `FRenderDevice` 소유

```cpp
Device
DeviceContext
SwapChain
BackBuffer RTV
BackBuffer Depth Texture
BackBuffer Depth DSV
BackBuffer Depth SRV
```

최종 화면 출력용입니다.

## 31.2 `FSceneTargetManager` 소유

```cpp
InternalSceneColorA/B
GameSceneDepth
GBufferA/B/C
OverlayColorSurface
OutlineMaskSurface
ExternalOverlayTargetMap 안의 OverlayColor
```

scene render 공통 target입니다.

## 31.3 외부 소유, `FSceneTargetManager`가 wrap만 함

```cpp
WrappedFinalSceneColor
WrappedSceneDepth
```

실제 외부 자원은 editor viewport 쪽에서 소유합니다.
`FSceneTargetManager`는 포인터만 빌려 씁니다.

## 31.4 Feature 내부 소유

```cpp
BloomBrightness
BloomScratch
Light culling buffers
Shadow maps
Fog buffers
Decal buffers
Texture arrays
```

특정 feature 내부 구현용 자원입니다.

기준은 다음입니다.

```cpp
여러 pass가 공통으로 공유하고 scene size와 함께 가면 SceneTargetManager.
특정 feature 내부에서만 쓰면 feature 내부 소유.
```

---

# 32. ping-pong의 전체 의미

`SceneColorRead/Write` ping-pong은 이 렌더러의 후처리 핵심입니다.

왜 필요하냐면 D3D11에서는 같은 texture를 동시에:

```cpp
SRV로 읽고
RTV/UAV로 쓰는 것
```

을 피해야 하기 때문입니다.

후처리 패스는 보통 이렇게 하고 싶습니다.

```cpp
현재 화면을 읽는다.
효과를 적용한다.
새 화면을 만든다.
```

잘못된 방식:

```cpp
SceneColorA를 읽고
SceneColorA에 다시 쓴다.
```

좋은 방식:

```cpp
SceneColorA를 읽고
SceneColorB에 쓴다.
끝나면 A/B를 교체한다.
```

이게 ping-pong입니다.

패턴은 다음입니다.

```cpp
Input  = Targets.GetSceneColorShaderResource();
Output = Targets.GetSceneColorWriteRenderTarget();

// Input 읽기
// Output 쓰기

Targets.SwapSceneColor();
```

compute라면:

```cpp
Output = Targets.GetSceneColorWriteUAV();
```

를 씁니다.

---

# 33. swap이 필요한 패스와 필요 없는 패스

## swap이 필요한 경우

조건:

```cpp
SceneColorRead를 SRV로 읽고
SceneColorWrite에 RTV/UAV로 쓴다.
```

예:

```cpp
DecalComposite 일반 경로
FogPost
Bloom composite
ToneMapping
FXAA
```

반드시 끝에:

```cpp
Targets.SwapSceneColor();
```

가 있어야 합니다.

## swap이 필요 없는 경우

조건:

```cpp
현재 SceneColorRTV 또는 OverlayColorRTV에 직접 덧그린다.
```

예:

```cpp
ForwardOpaque
MeshDecal
VolumeDecal
FireBall
ForwardTransparent
EditorGrid
OutlineComposite
EditorLine
EditorPrimitive
```

이들은 새로운 full scene color를 만드는 것이 아니라 기존 target에 직접 그리는 성격입니다.

---

# 34. 새 후처리 패스를 추가하는 방법

예를 들어 `ColorGradingPass`를 추가한다고 하겠습니다.

## 34.1 pass class 추가

```cpp
class FColorGradingPass : public IRenderPass
{
public:
    FPassDesc Describe() const override
    {
        return {
            .Name     = "Color Grading Pass",
            .Domain   = EPassDomain::Graphics,
            .Category = EPassCategory::Effects,
            .Reads    = PassTarget(ESceneTarget::SceneColor),
            .Writes   = PassTarget(ESceneTarget::SceneColorScratch),
        };
    }

    bool Execute(FPassContext& Context) override;
};
```

## 34.2 Execute 구현

```cpp
bool FColorGradingPass::Execute(FPassContext& Context)
{
    FSceneRenderTargets& Targets = Context.Targets;

    ID3D11ShaderResourceView* InputSRV =
        Targets.GetSceneColorShaderResource();

    ID3D11RenderTargetView* OutputRTV =
        Targets.GetSceneColorWriteRenderTarget();

    if (!InputSRV || !OutputRTV)
    {
        return true;
    }

    // fullscreen pass:
    // InputSRV를 pixel shader slot에 바인딩
    // OutputRTV를 render target으로 바인딩
    // Draw(3, 0)

    Targets.SwapSceneColor();
    return true;
}
```

핵심은 이 세 줄입니다.

```cpp
읽기: Targets.GetSceneColorShaderResource()
쓰기: Targets.GetSceneColorWriteRenderTarget()
끝: Targets.SwapSceneColor()
```

## 34.3 pipeline에 추가

`ScenePipelineBuilder.cpp`에 넣습니다.

```cpp
OutPipeline.AddPass(std::make_unique<FColorGradingPass>());
```

위치는 효과 순서에 따라 달라집니다.

예:

```cpp
Fog 뒤, Bloom 앞
Bloom 뒤, ToneMapping 전
```

현재 tone mapping은 `ResolveSceneColorTargets()`에서 수행되므로, scene pipeline 안에 넣는 후처리는 기본적으로 tone mapping 전 HDR/linear 공간에서 동작합니다.

---

# 35. 새 overlay pass를 추가하는 방법

예를 들어 editor helper shape를 그리고 싶다면 overlay에 쓰는 것이 좋습니다.

```cpp
ID3D11RenderTargetView* TargetRTV =
    Targets.OverlayColorRTV
        ? Targets.OverlayColorRTV
        : Targets.SceneColorRTV;
```

패스 선언:

```cpp
FPassDesc Describe() const override
{
    return {
        .Name     = "My Editor Overlay Pass",
        .Domain   = EPassDomain::Graphics,
        .Category = EPassCategory::EditorOverlay,
        .Reads    = PassTarget(ESceneTarget::SceneDepth),
        .Writes   = PassTarget(ESceneTarget::OverlayColor),
    };
}
```

실행:

```cpp
BeginPass(
    Context.Renderer,
    TargetRTV,
    Context.Targets.SceneDepthDSV,
    Context.SceneViewData.View.Viewport,
    Context.SceneViewData.Frame,
    Context.SceneViewData.View);

// draw overlay

EndPass(
    Context.Renderer,
    Context.Targets.SceneColorRTV,
    Context.Targets.SceneDepthDSV,
    Context.SceneViewData.View.Viewport,
    Context.SceneViewData.Frame,
    Context.SceneViewData.View);
```

swap은 하지 않습니다.

---

# 36. 새 scene-wide render target을 추가하는 방법

예를 들어 `VelocityBuffer`를 추가한다고 하겠습니다.

## 36.1 `FSceneRenderTargets`에 추가

```cpp
FGPUTexture2D* VelocityBuffer = nullptr;
```

alias도 필요하면 추가합니다.

```cpp
ID3D11Texture2D*          VelocityTexture = nullptr;
ID3D11RenderTargetView*   VelocityRTV = nullptr;
ID3D11ShaderResourceView* VelocitySRV = nullptr;
ID3D11UnorderedAccessView* VelocityUAV = nullptr;
```

`RefreshCompatibilityViews()`에 추가합니다.

```cpp
VelocityTexture = VelocityBuffer ? VelocityBuffer->Texture : nullptr;
VelocityRTV     = VelocityBuffer ? VelocityBuffer->RTV : nullptr;
VelocitySRV     = VelocityBuffer ? VelocityBuffer->SRV : nullptr;
VelocityUAV     = VelocityBuffer ? VelocityBuffer->UAV : nullptr;
```

## 36.2 `ESceneTarget`에 추가

파일:

```cpp
Scene/Passes/PassContext.h
```

```cpp
enum class ESceneTarget : uint8
{
    SceneColor,
    SceneColorScratch,
    SceneDepth,
    GBufferA,
    GBufferB,
    GBufferC,
    OutlineMask,
    OverlayColor,
    ShadowMap,
    VelocityBuffer,
};
```

## 36.3 `FSceneTargetManager` 멤버 추가

```cpp
FGPUTexture2D VelocitySurface;
```

## 36.4 `EnsureSupplementalTargets()`에서 생성

```cpp
CreateColorTexture(
    Device,
    Width,
    Height,
    DXGI_FORMAT_R16G16_FLOAT,
    VelocitySurface,
    true);
```

`bCreateUAV`는 compute로 쓸 필요가 있으면 `true`, 아니면 `false`로 둡니다.

## 36.5 release 추가

```cpp
ReleaseTexture(VelocitySurface);
```

## 36.6 `AcquireGameSceneTargets()`와 `WrapExternalSceneTargets()`에 연결

```cpp
OutTargets.VelocityBuffer = &VelocitySurface;
```

게임/에디터 둘 다 내부 공통 target으로 쓴다면 두 함수에 모두 넣습니다.

## 36.7 clear pass에 추가

```cpp
if (Context.Targets.VelocityRTV)
{
    DeviceContext->ClearRenderTargetView(Context.Targets.VelocityRTV, ZeroColor);
}
```

## 36.8 pass에서 사용

쓰기:

```cpp
Targets.VelocityRTV
```

읽기:

```cpp
Targets.VelocitySRV
```

descriptor:

```cpp
.Writes = PassTarget(ESceneTarget::VelocityBuffer)
```

또는:

```cpp
.Reads = PassTarget(ESceneTarget::VelocityBuffer)
```

---

# 37. 새 external target을 추가하는 방법

만약 에디터 viewport에서 외부 target을 하나 더 받아야 한다고 하겠습니다.

예:

```cpp
ExternalVelocityRTV
ExternalVelocitySRV
```

이 경우에는 단순히 `EnsureSupplementalTargets()`에 내부 target을 추가하는 것과 다릅니다.

수정 지점은 다음입니다.

## 37.1 `FViewportScenePassRequest`에 외부 포인터 추가

```cpp
ID3D11RenderTargetView*   VelocityRenderTargetView = nullptr;
ID3D11ShaderResourceView* VelocityShaderResourceView = nullptr;
```

## 37.2 `FSceneTargetManager`에 wrapped 멤버 추가

```cpp
FGPUTexture2D WrappedVelocity;
```

## 37.3 wrap 함수 추가 또는 color wrap 재사용

```cpp
WrapExternalColorTarget(
    Width,
    Height,
    VelocityRenderTargetView,
    VelocityShaderResourceView,
    WrappedVelocity);
```

## 37.4 `WrapExternalSceneTargets()`에서 연결

```cpp
OutTargets.VelocityBuffer = &WrappedVelocity;
```

## 37.5 release 처리

`ReleaseWrappedExternalTargets()`에 추가합니다.

```cpp
ReleaseTexture(WrappedVelocity);
```

주의할 점:

```cpp
wrapped target은 FSceneTargetManager가 소유하지 않습니다.
ReleaseTexture()는 external이면 실제 COM Release를 하지 않습니다.
```

---

# 38. Feature 내부 자원으로 둘지, SceneTargetManager에 둘지 판단법

새 자원이 필요할 때 항상 이 질문을 하시면 됩니다.

```cpp
이 자원을 여러 패스가 공통으로 쓰는가?
프레임/viewport 크기에 맞춰 관리되어야 하는가?
디버깅/패스 의존성 관점에서 scene target으로 드러나야 하는가?
```

그렇다면 `FSceneTargetManager`에 두는 것이 좋습니다.

예:

```cpp
VelocityBuffer
SSAOBuffer
CustomDepth
NormalBuffer
ObjectIDBuffer
```

반대로:

```cpp
특정 feature 내부에서만 쓰는 임시 buffer인가?
알고리즘 구현 세부사항인가?
다른 패스가 몰라도 되는가?
```

그렇다면 feature 내부 자원으로 두는 것이 좋습니다.

예:

```cpp
BloomBrightness
BloomScratch
LightCullingBuffer
FogVolumeBuffer
DecalStructuredBuffer
ShadowMapArray
```

Bloom이 좋은 예입니다.

```cpp
Bloom 내부 blur ping-pong은 BloomFeature가 관리합니다.
SceneColor 최종 composite만 FSceneRenderTargets의 SceneColorWrite를 사용합니다.
```

---

# 39. 자원 흐름 전체 요약표

| 단계                   | 읽는 자원                               | 쓰는 자원                                                | swap |
| -------------------- | ----------------------------------- | ---------------------------------------------------- | ---- |
| Clear                | 없음                                  | SceneColor A/B, Depth, GBuffer, Overlay, OutlineMask | 없음   |
| UploadMeshBuffers    | CPU/mesh data                       | GPU mesh buffers                                     | 없음   |
| DepthPrepass         | mesh data                           | SceneDepthDSV                                        | 없음   |
| LightCullingCompute  | SceneDepthSRV                       | Light feature UAV buffers                            | 없음   |
| ShadowMap            | scene mesh                          | Shadow feature shadow map                            | 없음   |
| ForwardOpaque        | Depth, Shadow, Material, Light      | SceneColorRTV                                        | 없음   |
| MeshDecal            | Depth                               | SceneColorRTV                                        | 없음   |
| DecalComposite 일반    | SceneColorSRV, DepthSRV             | SceneColorWriteRTV                                   | 있음   |
| VolumeDecal          | DepthSRV                            | SceneColorRTV                                        | 없음   |
| FogPost              | SceneColorSRV, DepthSRV             | SceneColorWriteRTV                                   | 있음   |
| FireBall             | DepthSRV                            | SceneColorRTV                                        | 없음   |
| ForwardTransparent   | Depth                               | SceneColorRTV                                        | 없음   |
| Bloom threshold/blur | SceneColorSRV, Bloom SRV            | Bloom UAV                                            | 내부   |
| Bloom composite      | SceneColorSRV, BloomSRV             | SceneColorWriteUAV                                   | 있음   |
| EditorGrid           | Depth                               | OverlayColorRTV 또는 SceneColorRTV                     | 없음   |
| OutlineMask          | selection/stencil                   | OutlineMaskRTV, SceneDepth stencil                   | 없음   |
| OutlineComposite     | OutlineMaskSRV                      | OverlayColorRTV 또는 SceneColorRTV                     | 없음   |
| EditorLine           | debug line data                     | OverlayColorRTV 또는 SceneColorRTV                     | 없음   |
| EditorPrimitive      | debug primitive data                | OverlayColorRTV 또는 SceneColorRTV                     | 없음   |
| ToneMapping          | SceneColorRead.SRV                  | SceneColorWrite.RTV                                  | 있음   |
| FXAA                 | SceneColorRead.SRV                  | SceneColorWrite.RTV                                  | 있음   |
| Final blit           | SceneColorRead.SRV                  | FinalSceneColor.RTV                                  | 없음   |
| ViewportComposite    | SceneColorSRV, DepthSRV, OverlaySRV | BackBuffer RTV                                       | 없음   |
| ScreenUI             | UI draw list                        | BackBuffer RTV                                       | 없음   |

---

# 40. 초심자가 가장 많이 헷갈리는 지점 정리

## 40.1 `SceneColorRTV`는 항상 A가 아닙니다

초기에는 A일 수 있습니다.

```cpp
SceneColorRead = A
SceneColorRTV = A.RTV
```

하지만 swap 후에는 B가 됩니다.

```cpp
SceneColorRead = B
SceneColorRTV = B.RTV
```

따라서 `SceneColorRTV`는 특정 texture 이름이 아니라:

```cpp
현재 최신 SceneColor의 RTV
```

입니다.

---

## 40.2 `SceneColorScratchRTV`도 항상 B가 아닙니다

`SceneColorScratchRTV`는:

```cpp
현재 SceneColorWrite의 RTV
```

입니다.

swap 후에는 A/B가 바뀝니다.

---

## 40.3 `SceneColorWrite`에 썼으면 swap해야 합니다

이 패턴이면:

```cpp
SceneColorRead.SRV를 읽고
SceneColorWrite.RTV 또는 UAV에 쓴다
```

끝에 반드시:

```cpp
Targets.SwapSceneColor();
```

가 필요합니다.

---

## 40.4 `FinalSceneColor`는 중간 작업용이 아닙니다

`FinalSceneColor`는 주로 에디터 viewport의 외부 color target입니다.

정상 흐름:

```cpp
내부 SceneColor A/B에서 후처리
    ↓
최신 결과가 SceneColorRead에 남음
    ↓
ResolveSceneColorTargets()
    ↓
SceneColorRead → FinalSceneColor
```

일반 패스에서 `FinalSceneColor->RTV`에 직접 그리는 것은 구조를 깨뜨릴 가능성이 큽니다.

---

## 40.5 wrapped target은 소유하지 않습니다

`WrapExternalColorTarget()`은 외부 포인터를 `AddRef()`하지 않습니다.
`ReleaseTexture()`도 external이면 `Release()`하지 않습니다.

따라서 외부 target의 lifetime은 외부 소유자가 보장해야 합니다.

---

## 40.6 wrapped target은 `Texture`가 null입니다

`WrapExternalColorTarget()`은 다음만 채웁니다.

```cpp
OutTexture.RTV = RenderTargetView;
OutTexture.SRV = ShaderResourceView;
```

`OutTexture.Texture`는 채우지 않습니다.

현재는 `FinalSceneColor->RTV`로 마지막 blit하는 정도라 괜찮습니다.
하지만 나중에 wrapped target을 state unbind나 resource hazard tracking까지 포함해 다루려면 실제 `ID3D11Resource*` 저장을 고려해야 합니다.

---

# 41. 이 구조를 설명할 때 가장 좋은 비유

이 렌더러를 비유하면 다음과 같습니다.

```cpp
FSceneTargetManager
    = 렌더링 도구 창고 관리자

FGPUTexture2D
    = 종이 한 장 + 그 종이를 읽고/쓰는 여러 입구

FSceneRenderTargets
    = 이번 작업에 쓸 도구 상자

RenderPass
    = 도구 상자를 받아 작업하는 작업자

SceneColorRead
    = 현재 완성본으로 인정되는 종이

SceneColorWrite
    = 다음 버전을 그릴 빈 종이

SwapSceneColor()
    = 방금 그린 종이를 이제 최신 완성본으로 인정하는 행동

WrapExternalColorTarget()
    = 남이 만든 종이를 우리 도구 상자 규격에 맞게 끼워 넣는 어댑터
```

---

# 42. 최종 구조 요약

이 렌더러를 전체적으로 한 문장으로 정리하면 다음입니다.

```cpp
FSceneTargetManager가 내부 render target을 만들거나 외부 target을 wrap하고,
FSceneRenderTargets가 그 target들을 scene pass에 전달하며,
각 pass는 SRV/RTV/DSV/UAV를 통해 읽고 쓴 뒤,
후처리는 SceneColorRead/Write ping-pong으로 최신 SceneColor를 갱신하고,
ResolveSceneColorTargets가 tone mapping/FXAA/final blit을 수행하며,
ViewportCompositor가 최종 SRV를 backbuffer에 합성합니다.
```

그리고 확장할 때의 기준은 이것입니다.

```cpp
화면 전체 후처리:
    SceneColorRead.SRV 읽기
    SceneColorWrite.RTV/UAV 쓰기
    SwapSceneColor()

현재 화면 위에 덧그리기:
    SceneColorRTV 또는 OverlayColorRTV에 직접 쓰기
    Swap 없음

Depth 기반 효과:
    DepthPrepass가 만든 SceneDepthSRV 읽기

새 공통 target:
    FSceneRenderTargets
    ESceneTarget
    FSceneTargetManager
    Clear pass
    Acquire/Wrap 연결
    사용하는 pass
    순서로 추가

외부 viewport target:
    FViewportScenePassRequest
    WrapExternalColorTarget / WrapExternalDepthTarget
    FSceneRenderTargets 연결
    소유권은 외부 유지
```

이 관점으로 보시면 이 렌더러의 `srv`, `dsv`, `wrap`, `ping-pong`, `FinalSceneColor`, `SceneColorRead/Write`가 각각 따로 노는 개념이 아니라, **게임 프레임과 에디터 프레임을 하나의 scene pipeline으로 통일하기 위한 설계 요소**라는 점이 명확해집니다.
