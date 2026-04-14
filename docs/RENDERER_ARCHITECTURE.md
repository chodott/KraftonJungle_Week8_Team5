# 렌더러 종합 가이드

## 1. 이 문서는 무엇을 위한 문서인가

이 문서는 현재 코드베이스의 렌더러를 **처음 보는 사람도 끝까지 읽으면 실제 흐름이 머릿속에 그려지도록** 정리한 종합 설명서입니다.

단순히 클래스 이름만 나열하는 문서가 아니라, 아래 질문에 실제 코드 기준으로 답하도록 구성했습니다.

1. 한 프레임은 어디서 시작되는가
2. 월드는 어디서 렌더러가 이해할 수 있는 데이터로 바뀌는가
3. 그 데이터는 어디서 실제 GPU 드로우 입력으로 조립되는가
4. 패스는 어떤 순서로 실행되는가
5. 게임 프레임과 에디터 프레임은 무엇이 다른가
6. 새 패스를 추가하려면 정확히 어디를 고쳐야 하는가
7. 새 기능이 화면에 안 나오면 어디부터 확인해야 하는가

이 문서의 목표는 한 가지입니다.

**“이 렌더러가 어떻게 흐르는지 이해하고, 새 기능이나 새 패스를 직접 넣을 수 있게 만드는 것.”**

---

## 2. 먼저 한 줄로 요약하면

이 렌더러는 아래 흐름으로 이해하면 됩니다.

```text
World / Components
 -> ViewportClient
 -> FSceneRenderPacket
 -> FSceneViewData
 -> RenderPipeline(Scene Passes)
 -> Viewport Composite
 -> Screen UI
 -> Present
```

조금 더 풀면 이렇습니다.

- `ViewportClient`가 현재 카메라에서 **무엇이 보이는지** 모읍니다.
- `FScenePacketBuilder`가 그것을 `FSceneRenderPacket`으로 분류합니다.
- `FSceneRenderer`가 이를 `FSceneViewData`로 조립합니다.
- `ScenePipelineBuilder`가 패스 순서를 만들고, 각 패스가 실제 GPU 작업을 실행합니다.
- 게임은 최종 장면을 전체 화면으로 합성합니다.
- 에디터는 여러 뷰포트를 합성한 뒤 화면 UI를 마지막에 덮습니다.

즉 이 구조에서 가장 중요한 구분은 이것입니다.

- `ScenePacket`: **무엇을 그릴지** 설명하는 데이터
- `SceneViewData`: **어떻게 그릴지에 필요한 실행 데이터**
- `RenderPipeline`: **어떤 순서로 그릴지**

---

## 3. 초심자를 위한 핵심 용어 정리

렌더러 문서를 읽다 보면 용어 때문에 막히는 경우가 많습니다. 먼저 이것만 잡고 가면 훨씬 편합니다.

### 3-1. 패스(Pass)

패스는 한 프레임을 여러 단계로 나눈 것이라고 보면 됩니다.

예를 들면:

- 깊이만 먼저 그리는 단계
- GBuffer를 채우는 단계
- 실제 색을 그리는 단계
- 안개를 덮는 단계
- 아웃라인을 덮는 단계
- FXAA를 적용하는 단계

즉 패스는 “렌더링 작업 한 덩어리”입니다.

### 3-2. Render Target

렌더 결과를 그려 넣는 텍스처입니다.

예를 들면:

- `SceneColor`: 장면의 최종 색
- `GBufferA/B/C`: 머티리얼/노멀 같은 중간 정보
- `OutlineMask`: 아웃라인용 마스크
- `SceneColorScratch`: 임시 복사용 텍스처

### 3-3. Depth Buffer

화면의 각 픽셀이 카메라에서 얼마나 가까운지를 저장하는 버퍼입니다.

이 값이 있어야:

- 앞에 있는 물체가 뒤의 물체를 가릴 수 있고
- Fog/Outline/Decal 같은 효과도 깊이를 기준으로 계산할 수 있습니다.

### 3-4. RTV / DSV / SRV

D3D11에서는 같은 텍스처를 어떤 용도로 쓰느냐에 따라 뷰가 달라집니다.

- `RTV`: Render Target View. 여기에 **그려 넣을 때** 사용합니다.
- `DSV`: Depth Stencil View. 깊이 버퍼로 **쓸 때** 사용합니다.
- `SRV`: Shader Resource View. 셰이더에서 **읽을 때** 사용합니다.

같은 텍스처라도 “지금 쓰는 중인지, 읽는 중인지”가 중요합니다.

### 3-5. `FMeshBatch`

렌더러가 메시 하나를 그리기 위해 필요한 최소 단위입니다.

여기에는 보통 아래 정보가 들어갑니다.

- 어떤 메시를 그릴지
- 어떤 머티리얼을 쓸지
- 어떤 월드 변환으로 그릴지
- 어떤 인덱스 범위를 그릴지
- 어떤 패스에서 그릴지 (`PassMask`)

즉 `FMeshBatch`는 “GPU가 그릴 준비가 된 메시 한 건”이라고 보면 됩니다.

---

## 4. 큰 그림에서 어떤 객체들이 있나

## 4-1. `FRenderer`: 전체 프레임의 오케스트레이터

파일:
- `Engine/Source/Renderer/Renderer.h`
- `Engine/Source/Renderer/Renderer.cpp`

`FRenderer`는 렌더러의 얼굴이지만, 모든 기능을 직접 구현하는 클래스는 아닙니다.

핵심 역할은 아래와 같습니다.

1. 프레임 시작/종료를 관리한다.
2. 게임 프레임과 에디터 프레임 요청을 받는다.
3. 내부 서브시스템을 올바른 순서로 호출한다.
4. 공용 디바이스, 컨텍스트, 상태, 피처 접근 지점을 제공한다.

이 클래스를 볼 때 가장 중요한 관점은 이것입니다.

**`FRenderer`는 직접 다 그리는 클래스가 아니라, “전체 흐름을 지휘하는 클래스”다.**

## 4-2. `FRenderDevice`: D3D11 바닥 계층

파일:
- `Engine/Source/Renderer/GraphicsCore/RenderDevice.h`

이 클래스는 D3D11 디바이스와 스왑체인, 백버퍼 관련 책임을 가집니다.

대표 역할:

1. 디바이스/컨텍스트 생성
2. 스왑체인 생성
3. 백버퍼 RTV/DSV 관리
4. 리사이즈 처리
5. `Present`

즉 “화면 출력의 가장 바닥”입니다.

## 4-3. `FSceneRenderer`: 실제 씬 렌더 실행기

파일:
- `Engine/Source/Renderer/Scene/SceneRenderer.h`
- `Engine/Source/Renderer/Scene/SceneRenderer.cpp`

이 클래스는 장면 렌더링의 중심입니다.

대표 역할:

1. `FSceneRenderPacket`을 `FSceneViewData`로 바꾼다.
2. 필요하면 와이어프레임 override를 적용한다.
3. 기본 씬 파이프라인을 구성한다.
4. 각 패스를 실행한다.

즉 “씬이 실제로 GPU에서 그려지는 곳”은 `FSceneRenderer`입니다.

## 4-4. `FSceneTargetManager`: 씬용 렌더 타깃 묶음 관리자

파일:
- `Engine/Source/Renderer/Frame/SceneTargetManager.h`

이 클래스는 게임과 에디터가 서로 다른 렌더 타깃 소유 방식을 갖더라도, 씬 렌더러 입장에서는 비슷한 형태의 `FSceneRenderTargets`를 받도록 맞춰줍니다.

대표 역할:

1. 게임용 내부 SceneColor/SceneDepth/GBuffer 타깃 확보
2. 에디터의 외부 RTV/DSV를 씬 타깃 구조로 래핑
3. 패스들이 공통 구조를 쓰게 정리

## 4-5. `FViewportCompositor`: 최종 뷰포트 합성기

파일:
- `Engine/Source/Renderer/Frame/Viewport/ViewportCompositor.h`
- `Engine/Source/Renderer/Frame/Viewport/ViewportCompositor.cpp`

이 클래스는 여러 뷰포트 결과를 최종 백버퍼에 배치합니다.

대표 역할:

1. 각 뷰포트 텍스처를 백버퍼의 지정된 위치에 배치
2. 컬러/깊이 시각화 모드 처리
3. 합성용 셰이더와 상태 관리

## 4-6. `FScreenUIRenderer`: 화면 UI 렌더러

파일:
- `Engine/Source/Renderer/UI/Screen/ScreenUIRenderer.h`
- `Engine/Source/Renderer/UI/Screen/ScreenUIRenderer.cpp`

UI는 씬 메시 패스에 섞이지 않습니다.

대표 역할:

1. `FUIDrawList`를 GPU 입력으로 변환
2. 화면 UI 패스를 구성
3. 최종 백버퍼 위에 UI를 그림

즉 화면 UI는 **씬 이후의 완전 별도 경로**입니다.

---

## 5. 이 렌더러를 이해할 때 가장 중요한 설계 원칙

이 원칙들을 기억하면 구조가 잘 보입니다.

1. 월드 수집 계층은 D3D11 세부사항을 몰라야 합니다.
2. `ScenePacket`은 “무엇이 보이는지”만 담고, “어떻게 그릴지”는 담지 않습니다.
3. `SceneViewData`는 패스가 바로 소비할 수 있는 실행 데이터입니다.
4. 메시 패스와 후처리 패스는 같은 프레임 안에 있지만 역할이 다릅니다.
5. 에디터는 패스 내부를 직접 건드리기보다 프레임 요청을 조립합니다.
6. UI는 먼저 기록하고 마지막에 렌더링합니다.

가장 많이 헷갈리는 부분을 한 문장으로 정리하면 이렇습니다.

**렌더러는 월드를 직접 그리는 것이 아니라, 월드에서 만든 설명 데이터를 받아 GPU가 이해할 수 있는 형식으로 다시 조립한 뒤 패스 순서대로 실행합니다.**

---

## 6. 한 프레임은 실제로 어떻게 흐르나

이 섹션이 가장 중요합니다.

### 6-1. 정말 단순화한 전체 흐름

```text
카메라/뷰포트 결정
 -> 월드에서 visible primitive 수집
 -> ScenePacket 생성
 -> SceneViewData 생성
 -> 렌더 타깃 준비
 -> 패스 파이프라인 실행
 -> 결과 합성
 -> UI 렌더
 -> Present
```

### 6-2. 게임 프레임 흐름

실제 코드 흐름은 대략 아래와 같습니다.

파일:
- `Engine/Source/Core/ViewportClient.cpp`
- `Engine/Source/Renderer/Frame/GameFrameRenderer.cpp`

```text
IViewportClient::BuildSceneRenderPacket()
 -> FScenePacketBuilder::BuildScenePacket()
 -> FRenderer::RenderGameFrame()
 -> FGameFrameRenderer::Render()
 -> SceneTargetManager::AcquireGameSceneTargets()
 -> FSceneRenderer::BuildSceneViewData()
 -> FSceneRenderer::RenderSceneView()
 -> ViewportCompositePass
 -> Present
```

이걸 사람 말로 풀면 이렇습니다.

1. 현재 카메라 기준으로 무엇이 보이는지 수집합니다.
2. 그것을 `FSceneRenderPacket`으로 정리합니다.
3. 게임용 SceneColor/SceneDepth/GBuffer 타깃을 확보합니다.
4. 패킷을 실제 렌더 입력인 `FSceneViewData`로 바꿉니다.
5. 패스 파이프라인을 실행해서 SceneColor를 만듭니다.
6. SceneColor를 최종 화면 전체에 합성합니다.
7. 화면을 출력합니다.

### 6-3. 에디터 프레임 흐름

파일:
- `Editor/Source/Viewport/Services/EditorViewportRenderService.cpp`
- `Engine/Source/Renderer/Frame/EditorFrameRenderer.cpp`

```text
EditorViewportRenderService::RenderAll()
 -> 뷰포트마다 ScenePacket 생성
 -> 뷰포트마다 Outline/Debug 입력 준비
 -> FRenderer::RenderEditorFrame()
 -> FEditorFrameRenderer::Render()
 -> 각 뷰포트별 SceneRenderer 실행
 -> ViewportCompositePass
 -> ScreenUIPass
 -> Present
```

에디터는 게임보다 한 단계 더 복잡합니다.

왜냐하면:

- 뷰포트가 여러 개일 수 있고
- 각 뷰포트가 별도의 렌더 타깃을 가질 수 있고
- Outline, Gizmo, Grid, DebugLine 같은 에디터 전용 요소가 있고
- 마지막에 Screen UI가 또 별도로 올라가야 하기 때문입니다.

즉 에디터는 이렇게 기억하면 됩니다.

**“여러 개의 작은 장면을 먼저 따로 렌더하고, 마지막에 한 화면으로 합친다.”**

---

## 7. `ScenePacket`은 어디서 만들어지나

## 7-1. 시작점은 `ViewportClient`

파일:
- `Engine/Source/Core/ViewportClient.cpp`

`IViewportClient::BuildSceneRenderPacket()`가 바깥쪽 시작점입니다.

이 함수는 보통 아래 순서로 동작합니다.

1. 월드와 현재 프러스텀을 잡습니다.
2. visible primitive를 찾습니다.
3. `ScenePacketBuilder.BuildScenePacket()`을 호출합니다.
4. Fog, FireBall 같은 추가 입력을 채웁니다.
5. ShowFlags를 보고 FXAA 같은 옵션을 기록합니다.

즉 이 단계는 “월드에서 지금 화면에 보일 것들을 정리하는 단계”입니다.

## 7-2. `FSceneRenderPacket`의 의미

파일:
- `Engine/Source/Level/SceneRenderPacket.h`

`FSceneRenderPacket`은 렌더러 비의존적인 장면 설명 데이터입니다.

대표 버킷:

- `MeshPrimitives`
- `TextPrimitives`
- `SubUVPrimitives`
- `BillboardPrimitives`
- `FogPrimitives`
- `DecalPrimitives`
- `FireBallPrimitives`
- `bApplyFXAA`

여기서 중요한 점은, 이 구조가 아직 GPU 드로우 커맨드가 아니라는 점입니다.

이 단계는 그냥 이렇게 생각하면 됩니다.

**“지금 뷰에서 무엇을 그려야 하는지 종류별로 담아둔 상자.”**

## 7-3. `FScenePacketBuilder`가 하는 일

파일:
- `Engine/Source/Level/ScenePacketBuilder.h`
- `Engine/Source/Level/ScenePacketBuilder.cpp`

이 빌더는 다음 역할만 합니다.

1. ShowFlag 확인
2. 프리미티브 타입 판별
3. 메시/텍스트/SubUV/빌보드/데칼 등으로 분류
4. `FSceneRenderPacket` 채우기

이 단계에서는 아직 머티리얼 바인딩, 셰이더 바인딩, draw 호출이 없습니다.

즉 이 빌더는 **수집기**이지, 아직 **렌더 실행기**가 아닙니다.

---

## 8. `ScenePacket`은 어디서 실제 렌더 입력이 되나

## 8-1. `FSceneViewData`란 무엇인가

파일:
- `Engine/Source/Renderer/Scene/SceneViewData.h`

`FSceneViewData`는 한 뷰를 그리기 위해 필요한 실행 데이터를 모아둔 구조입니다.

핵심 구성:

- `Frame`
- `View`
- `MeshInputs`
- `PostProcessInputs`
- `DebugInputs`

조금 더 풀면:

- `MeshInputs.Batches`: 실제 메시 드로우에 필요한 `FMeshBatch`들
- `PostProcessInputs`: Fog, Decal, Outline, FireBall, FXAA 입력
- `DebugInputs`: 디버그 라인 입력

즉 `SceneViewData`는 패스들이 바로 읽을 수 있는 최종 조립 결과입니다.

## 8-2. `FSceneRenderer::BuildSceneViewData()`

파일:
- `Engine/Source/Renderer/Scene/SceneRenderer.cpp`
- `Engine/Source/Renderer/Scene/Builders/SceneViewAssembler.cpp`

게임과 에디터 모두 결국 이 함수로 들어옵니다.

내부적으로는 `BuildSceneViewDataFromPacket()`이 호출되고, 그 안에서 `FSceneCommandBuilder`가 실제 조립을 담당합니다.

즉 흐름은 이렇습니다.

```text
FSceneRenderPacket
 -> FSceneCommandBuilder
 -> FSceneViewData
```

## 8-3. `FSceneCommandBuilder`의 역할

파일:
- `Engine/Source/Renderer/Scene/Builders/SceneCommandBuilder.h`
- `Engine/Source/Renderer/Scene/Builders/SceneCommandBuilder.cpp`

이 빌더는 타입별 하위 빌더를 조합합니다.

대표 하위 빌더:

- `SceneCommandMeshBuilder`
- `SceneCommandTextBuilder`
- `SceneCommandSpriteBuilder`
- `SceneCommandPostProcessBuilder`

즉 “하나의 거대한 변환 함수”가 아니라, 역할별로 나눠진 조립기입니다.

## 8-4. `FMeshBatch`는 어떻게 만들어지나

파일:
- `Engine/Source/Renderer/Scene/Builders/SceneCommandMeshBuilder.cpp`
- `Engine/Source/Renderer/Mesh/MeshBatch.h`

정적 메시 프리미티브는 `FMeshBatch`로 바뀝니다.

`FMeshBatch`에 들어가는 대표 정보는 아래와 같습니다.

- `Mesh`
- `Material`
- `World`
- `SectionIndex`
- `IndexStart`
- `IndexCount`
- `Domain`
- `PassMask`

여기서 아주 중요한 필드가 `PassMask`입니다.

예를 들어 현재 정적 메시 경로는 대체로 아래처럼 들어갑니다.

- `DepthPrepass`
- `GBuffer`
- `ForwardOpaque`

즉 **같은 메시가 여러 패스에서 다시 사용될 수 있도록 미리 표시를 달아 둡니다.**

## 8-5. 텍스트, SubUV, 빌보드도 결국 메시 배치로 간다

파일:
- `Engine/Source/Renderer/Scene/Builders/SceneCommandTextBuilder.cpp`
- `Engine/Source/Renderer/Scene/Builders/SceneCommandSpriteBuilder.cpp`

특수 오브젝트들도 가능하면 `FMeshBatch`로 흡수합니다.

이 설계의 장점은 큽니다.

1. 정렬 방식을 공통으로 쓸 수 있습니다.
2. 패스 체계를 공유할 수 있습니다.
3. 새 오브젝트 타입을 넣을 때 구조가 덜 흔들립니다.

## 8-6. 후처리 입력은 따로 들어간다

파일:
- `Engine/Source/Renderer/Scene/Builders/SceneCommandPostProcessBuilder.cpp`

Fog, FireBall, Decal은 `PostProcessInputs`에 들어갑니다.

예를 들면:

- `FogItems`
- `DecalItems`
- `FireBallItems`
- `bOutlineEnabled`
- `bApplyFXAA`

즉 메시를 그리는 입력과 후처리 입력은 같은 `SceneViewData` 안에 있지만, **조립 단계부터 서로 다른 통로를 타고 들어갑니다.**

---

## 9. 실제 렌더 패스는 어떤 순서로 실행되나

파일:
- `Engine/Source/Renderer/Scene/Pipeline/ScenePipelineBuilder.cpp`

기본 씬 패스 순서는 아래와 같습니다.

1. `FClearSceneTargetsPass`
2. `FUploadMeshBuffersPass`
3. `FDepthPrepass`
4. `FGBufferPass`
5. `FForwardOpaquePass`
6. `FDecalCompositePass`
7. `FForwardTransparentPass`
8. `FFogPostPass`
9. `FFireBallPass`
10. `FOutlineMaskPass`
11. `FOutlineCompositePass`
12. `FOverlayPass`
13. `FDebugLinePass`
14. `FFXAAPass`

이 순서는 단순한 나열이 아니라 의미가 있습니다.

### 9-1. `FClearSceneTargetsPass`

SceneColor, Depth, GBuffer, OutlineMask를 프레임 시작 상태로 초기화합니다.

### 9-2. `FUploadMeshBuffersPass`

이번 프레임에 필요한 메시들의 버텍스/인덱스 버퍼를 업로드합니다.

### 9-3. `FDepthPrepass`

색은 쓰지 않고 깊이만 먼저 채웁니다.

이 단계가 있으면:

- 뒤에 있는 픽셀을 더 빨리 걸러낼 수 있고
- 후처리와 데칼, 포그가 안정적으로 깊이를 읽을 수 있습니다.

### 9-4. `FGBufferPass`

GBufferA/B/C를 채웁니다.

현재 구조는 완전한 deferred renderer라기보다는 하이브리드에 가깝지만, 이 단계는 앞으로의 확장성과 시각화에 중요합니다.

### 9-5. `FForwardOpaquePass`

불투명 메시의 실제 장면 색을 `SceneColor`에 그립니다.

즉 현재 장면 컬러의 중심은 이 패스입니다.

### 9-6. `FDecalCompositePass`

SceneColor와 Depth를 읽으면서 데칼을 합성합니다.

데칼은 메시 기본 드로우와는 별도 기능으로 빠져 있습니다.

### 9-7. `FForwardTransparentPass`

투명 메시를 그립니다.

투명체는 일반적으로 뒤에서 앞으로 정렬해야 하므로 opaque와 다른 규칙을 가집니다.

### 9-8. `FFogPostPass`

깊이를 읽어 안개를 장면 위에 덮습니다.

### 9-9. `FFireBallPass`

화면 공간 효과 성격의 FireBall 효과를 적용합니다.

### 9-10. `FOutlineMaskPass` / `FOutlineCompositePass`

선택 오브젝트의 마스크를 만들고, 그것을 이용해 실제 아웃라인을 장면 위에 합성합니다.

### 9-11. `FOverlayPass`

기즈모나 특정 오버레이 성격의 메시를 그리는 단계입니다.

### 9-12. `FDebugLinePass`

디버그 라인을 최종 장면 위에 올립니다.

### 9-13. `FFXAAPass`

최종 결과물에 안티앨리어싱을 적용합니다.

보통 FXAA가 마지막에 오는 이유는, 이미 그려진 최종 장면 전체를 대상으로 처리하는 후처리이기 때문입니다.

---

## 10. 이 렌더러는 왜 하이브리드라고 부를 수 있나

완전한 deferred renderer라면 보통:

1. GBuffer를 만든다.
2. 별도의 라이팅 패스에서 그것을 읽는다.
3. 최종 색을 만든다.

그런데 현재 구조는:

1. GBuffer는 만든다.
2. 전통적인 독립 deferred lighting pass는 보이지 않는다.
3. 실제 장면 색은 `FForwardOpaquePass`에서 만들어진다.
4. 그 뒤에 Decal/Fog/Outline/FXAA가 이어진다.

즉 현재 구조는 아래 쪽에 더 가깝습니다.

**Depth + GBuffer + Forward + Screen-space Effects가 결합된 하이브리드 구조**

이렇게 보면 왜 패스 순서가 지금처럼 생겼는지 이해가 쉬워집니다.

---

## 11. 메시 패스는 실제로 어떻게 실행되나

파일:
- `Engine/Source/Renderer/Scene/MeshPassProcessor.cpp`

이 클래스는 메시 패스 실행의 핵심입니다.

핵심 역할:

1. 어떤 배치가 어떤 패스에 들어갈지 결정
2. 패스별 정렬
3. 머티리얼/셰이더 바인딩
4. 렌더 상태 바인딩
5. 메시 바인딩과 draw 호출

## 11-1. `PassMask`로 패스 참여 여부를 결정한다

`ShouldDrawInPass()`는 `PassMask`를 보고 판단합니다.

예를 들면:

- `DepthPrepass`
- `GBuffer`
- `ForwardOpaque`
- `ForwardTransparent`
- `Overlay`

즉 패스를 새로 추가할 때는 “이 패스에 메시가 들어와야 하는가”를 함께 설계해야 합니다.

## 11-2. 패스별 정렬 전략이 다르다

코드를 보면 정렬 전략이 다릅니다.

- `ForwardTransparent`: 거리 기준 뒤에서 앞으로
- `Overlay`: 제출 순서 유지
- `Depth/GBuffer/ForwardOpaque`: 셰이더/상태/메시 기준 정렬

왜 다르냐면 목적이 다르기 때문입니다.

- 투명체는 블렌딩 때문에 순서가 중요합니다.
- 오버레이는 사용자가 넣은 순서가 중요할 수 있습니다.
- 불투명체는 성능상 상태 변경 최소화가 중요합니다.

---

## 12. 게임 프레임과 에디터 프레임은 무엇이 다른가

## 12-1. 게임 프레임

파일:
- `Engine/Source/Renderer/Frame/GameFrameRenderer.cpp`

흐름 요약:

1. 게임용 SceneTargets 확보
2. `BuildSceneViewData()`
3. 데칼 텍스처 배열 해결
4. 디버그 라인 입력 추가
5. `RenderSceneView()` 실행
6. 최종 SceneColor를 전체 화면으로 합성

즉 게임은 상대적으로 단순합니다.

**한 개의 장면을 만들고, 그것을 최종 화면에 붙입니다.**

## 12-2. 에디터 프레임

파일:
- `Engine/Source/Renderer/Frame/EditorFrameRenderer.cpp`

흐름 요약:

1. 각 뷰포트의 외부 렌더 타깃을 씬 타깃 구조로 래핑
2. 뷰포트마다 `BuildSceneViewData()`
3. Outline/Debug 입력을 뷰별로 추가
4. 각 뷰포트에 대해 `RenderSceneView()` 실행
5. 모든 뷰포트 결과를 백버퍼에 합성
6. 그 위에 Screen UI를 그린다

즉 에디터는 이렇게 기억하면 됩니다.

**여러 장면을 먼저 만들고, 마지막에 하나의 창으로 합친다.**

---

## 13. 새 기능을 추가할 때 먼저 던져야 할 질문

무조건 코드를 쓰기 전에 먼저 이 질문을 해야 합니다.

### 질문 1. 이 기능은 메시인가, 후처리인가

- 메시처럼 특정 기하를 그리는 기능인가
- 화면 전체를 덮는 풀스크린 효과인가

### 질문 2. 입력은 어디서 오나

- 월드 컴포넌트에서 오는가
- 에디터 요청에서 오는가
- 뷰포트/프레임 옵션에서 오는가

### 질문 3. 어느 시점의 결과를 읽어야 하나

- Depth가 필요하면 `DepthPrepass` 이후여야 합니다.
- SceneColor가 필요하면 `ForwardOpaque` 이후여야 합니다.
- Transparent까지 반영된 결과가 필요하면 `ForwardTransparent` 이후여야 합니다.
- 화면 전체 최종 결과를 다 보고 싶으면 `FXAA` 직전이나 이후를 고려해야 합니다.

이 세 질문에 답하면 새 패스를 어디에 넣어야 할지 윤곽이 나옵니다.

---

## 14. 새 패스를 추가하는 전체 절차

이 섹션은 실전 작업용입니다.

### 14-1. 새 패스 추가의 공통 흐름

보통 아래 순서입니다.

1. **입력 데이터 구조를 정한다.**
2. 그 입력을 `ScenePacket` 또는 프레임 요청에서 채운다.
3. `SceneViewData`로 옮긴다.
4. 새 패스 클래스를 만든다.
5. `ScenePipelineBuilder.cpp`에 등록한다.
6. 필요한 리소스와 feature를 `FRenderer`가 들고 있게 한다.
7. 실제 입력이 들어오는지 확인한다.

### 14-2. 언제 `ScenePacket`에 넣고, 언제 직접 `SceneViewData`에 넣나

아래 기준이 편합니다.

#### `ScenePacket`에 넣는 편이 맞는 경우

- 월드에 존재하는 새로운 프리미티브 종류
- 예: 커스텀 스프라이트, 월드 마커, 새 데칼류

이 경우 순서는 보통:

```text
SceneRenderPacket.h
 -> ScenePacketBuilder
 -> SceneCommandBuilder / SceneViewAssembler
 -> SceneViewData
 -> Pass Execute
```

#### 프레임 요청이나 에디터 요청에서 직접 넣는 편이 맞는 경우

- 특정 뷰포트나 특정 프레임에만 필요한 기능
- 예: 선택 아웃라인, 디버그 오버레이, 픽킹 마스크

이 경우는 `ScenePacket`보다 `FEditorFrameRequest`, `FViewportScenePassRequest`, `DebugInputs`, `OutlineRequest` 같은 구조가 더 자연스럽습니다.

---

## 15. 예제 1: 풀스크린 후처리 패스 하나 추가하기

여기서는 예시로 **색상을 살짝 톤 조정하는 간단한 후처리 패스**를 추가한다고 가정하겠습니다.

목표는 이렇습니다.

- SceneColor를 읽는다.
- 결과를 다시 SceneColor에 반영한다.
- 옵션이 켜져 있을 때만 돈다.

### 15-1. 먼저 어디에 둬야 하나

이 효과는 SceneColor를 읽어서 처리하므로 최소한 `FForwardOpaquePass` 이후여야 합니다.

또 투명체까지 포함한 최종 화면을 바꾸고 싶다면 `FForwardTransparentPass` 뒤가 더 맞습니다.

예를 들어 “모든 장면 결과를 대상으로 톤 조정”을 원한다면 아래 위치가 자연스럽습니다.

```text
ForwardTransparent 뒤
Fog 앞 또는 뒤
```

어디가 맞는지는 효과 의도에 따라 달라집니다.

- Fog보다 먼저: 안개가 그 위에 다시 덮임
- Fog보다 나중: 안개까지 포함한 최종 결과를 조정

### 15-2. 입력 데이터 추가

예를 들어 `bApplyToneAdjust`, `ToneAdjustStrength` 같은 입력이 필요하다고 하겠습니다.

가장 단순한 방법은 `FScenePostProcessInputs`에 넣는 것입니다.

파일:
- `Engine/Source/Renderer/Scene/SceneViewData.h`

예시:

```cpp
struct FScenePostProcessInputs
{
    ...
    bool bApplyToneAdjust = false;
    float ToneAdjustStrength = 0.0f;
};
```

### 15-3. 입력을 어디서 채울까

선택지는 두 가지입니다.

#### 방법 A. 뷰포트 옵션에서 채운다

`ViewportClient` 단계에서 ShowFlag나 뷰 옵션을 보고 채웁니다.

#### 방법 B. 월드 컴포넌트에서 채운다

특정 컴포넌트가 있으면 `ScenePacket`에 넣고, `SceneCommandPostProcessBuilder`가 `SceneViewData`로 옮깁니다.

초심자 기준으로는 **방법 A가 더 단순**합니다.

### 15-4. 패스 클래스 만들기

파일 추가 예시:

- `Engine/Source/Renderer/Scene/Passes/SceneToneAdjustPass.h`
- `Engine/Source/Renderer/Scene/Passes/SceneToneAdjustPass.cpp`

패턴은 기존 `FFogPostPass`, `FFXAAPass`를 참고하면 됩니다.

헤더 예시:

```cpp
class ENGINE_API FToneAdjustPass : public IRenderPass
{
public:
    bool Execute(FPassContext& Context) override;
};
```

구현 예시 개념:

```cpp
bool FToneAdjustPass::Execute(FPassContext& Context)
{
    if (!Context.SceneViewData.PostProcessInputs.bApplyToneAdjust)
    {
        return true;
    }

    FToneAdjustRenderFeature* Feature = Context.Renderer.GetToneAdjustFeature();
    if (!Feature)
    {
        return true;
    }

    return Feature->Render(
        Context.Renderer,
        Context.SceneViewData.Frame,
        Context.SceneViewData.View,
        Context.Targets,
        Context.SceneViewData.PostProcessInputs.ToneAdjustStrength);
}
```

여기서 중요한 포인트는, 패스 클래스 자체는 보통 얇아야 한다는 점입니다.

즉 패스는 보통:

- 입력이 있는지 검사하고
- feature를 얻고
- feature의 `Render()`를 호출하는 역할

정도만 맡습니다.

### 15-5. 파이프라인에 등록하기

파일:
- `Engine/Source/Renderer/Scene/Pipeline/ScenePipelineBuilder.cpp`

예를 들어 `ForwardTransparent` 뒤, `Fog` 앞에 넣고 싶다면:

```cpp
OutPipeline.AddPass(std::make_unique<FForwardTransparentPass>(MeshPassProcessor));
OutPipeline.AddPass(std::make_unique<FToneAdjustPass>());
OutPipeline.AddPass(std::make_unique<FFogPostPass>());
```

이 단계가 빠지면 패스를 아무리 잘 만들어도 절대 실행되지 않습니다.

### 15-6. 실제 렌더 기능 구현하기

보통은 feature 클래스를 하나 둡니다.

예:

- `Renderer/Features/PostProcess/ToneAdjustRenderFeature.h`
- `Renderer/Features/PostProcess/ToneAdjustRenderFeature.cpp`

이 feature는 보통 아래 일을 합니다.

1. 필요한 셰이더와 상태를 준비
2. `SceneColorSRV`를 읽음
3. 임시 RT에 쓰거나 scratch를 사용
4. 결과를 다시 `SceneColor`에 반영

### 15-7. 초심자가 자주 놓치는 포인트

1. `SceneColor`를 읽으면서 동시에 같은 `SceneColorRTV`에 쓰면 안 됩니다.
	- 보통 scratch texture가 필요합니다.
2. 패스를 만들고 `ScenePipelineBuilder.cpp`에 등록하지 않으면 실행되지 않습니다.
3. 입력 값이 기본값인 채로 남아 있으면 패스가 항상 스킵됩니다.
4. feature를 `FRenderer`가 소유하지 않으면 `GetToneAdjustFeature()`가 실패합니다.

---

## 16. 예제 2: 메시 패스 하나 추가하기

이번에는 예시로 **“특정 메시를 별도의 강조 패스에서 한 번 더 그리는 기능”**을 생각해보겠습니다.

이 예제는 새 메시 패스가 어떤 식으로 들어가는지 이해하는 데 좋습니다.

### 16-1. 먼저 판단해야 할 것

이 기능은 풀스크린이 아니라 메시를 다시 그리는 기능입니다.

따라서 고민 포인트는 이것입니다.

1. 기존 `PassMask` 체계에 새 비트를 추가할까
2. 기존 `Overlay`를 재활용할까
3. 새 `EMeshPassType` 자체를 만들까

기능이 명확히 별도의 의미를 갖고, 앞으로도 독립성이 필요하다면 새 패스를 만드는 편이 낫습니다.

### 16-2. enum 확장

파일:
- `Engine/Source/Renderer/Mesh/MeshBatch.h`

예를 들어 `Highlight` 패스를 추가한다면:

```cpp
enum class EMeshPassType : uint32
{
    DepthPrepass = 0,
    GBuffer,
    ForwardOpaque,
    ForwardTransparent,
    Overlay,
    Highlight,
    Count,
};

enum class EMeshPassMask : uint32
{
    None               = 0,
    DepthPrepass       = 1u << 0,
    GBuffer            = 1u << 1,
    ForwardOpaque      = 1u << 2,
    ForwardTransparent = 1u << 3,
    Overlay            = 1u << 4,
    Highlight          = 1u << 5,
};
```

### 16-3. `FMeshPassProcessor`가 새 패스를 이해하도록 만들기

파일:
- `Engine/Source/Renderer/Scene/MeshPassProcessor.cpp`

적어도 아래 세 군데를 함께 봐야 합니다.

1. `ToMaterialPassType()`
2. `ShouldDrawInPass()`
3. 정렬 전략 분기

예를 들어 새 머티리얼 패스 타입이 필요하면 `EMaterialPassType`과 머티리얼 셰이더 바인딩도 함께 확장해야 합니다.

반대로 기존 `ForwardOpaque` 셰이더를 그대로 쓰는 강조 드로우라면, 새로운 머티리얼 패스 타입 없이도 설계할 수 있습니다.

### 16-4. 패스 클래스 추가

파일 예시:

- `Engine/Source/Renderer/Scene/Passes/SceneHighlightPass.h`
- `Engine/Source/Renderer/Scene/Passes/SceneHighlightPass.cpp`

형태는 `FForwardOpaquePass`나 `FOverlayPass`와 비슷합니다.

예시:

```cpp
class ENGINE_API FHighlightPass : public IRenderPass
{
public:
    explicit FHighlightPass(const FMeshPassProcessor& InProcessor)
        : Processor(InProcessor)
    {
    }

    bool Execute(FPassContext& Context) override;

private:
    const FMeshPassProcessor& Processor;
};
```

구현은 보통 이런 형태가 됩니다.

```cpp
bool FHighlightPass::Execute(FPassContext& Context)
{
    return ExecuteMeshScenePass(
        Context.Renderer,
        Context.Targets,
        Context.SceneViewData,
        Processor,
        EMeshPassType::Highlight);
}
```

### 16-5. 어떤 메시가 이 패스에 들어올지 정하기

이 단계가 핵심입니다.

패스만 만드는 것으로 끝나지 않습니다. 어떤 배치가 `Highlight`에 참여할지 표시해야 합니다.

예를 들어 `SceneCommandMeshBuilder.cpp`에서 조건에 따라:

```cpp
Batch.PassMask =
    static_cast<uint32>(EMeshPassMask::DepthPrepass) |
    static_cast<uint32>(EMeshPassMask::GBuffer) |
    static_cast<uint32>(EMeshPassMask::ForwardOpaque) |
    static_cast<uint32>(EMeshPassMask::Highlight);
```

또는 에디터 전용 선택 메시만 넣고 싶다면, `AdditionalMeshBatches`를 만드는 쪽에서 `Highlight` 마스크를 부여할 수도 있습니다.

### 16-6. 파이프라인에 위치시키기

예를 들어 opaque 뒤, transparent 앞에 강조 패스를 두고 싶다면:

```cpp
OutPipeline.AddPass(std::make_unique<FForwardOpaquePass>(MeshPassProcessor));
OutPipeline.AddPass(std::make_unique<FHighlightPass>(MeshPassProcessor));
OutPipeline.AddPass(std::make_unique<FDecalCompositePass>());
```

왜 이 위치냐면, 강조 대상이 불투명 메시 위에 한 번 더 그려지되, 이후의 transparent나 후처리와도 자연스럽게 이어지기 때문입니다.

### 16-7. 초심자가 자주 하는 실수

1. 새 패스 enum만 추가하고 `ShouldDrawInPass()`를 고치지 않는 경우
2. `PassMask`를 아무도 안 붙여서 배치가 0개인 경우
3. 새 패스를 만들었지만 파이프라인에 등록하지 않은 경우
4. 강조 패스가 기존 깊이 상태 때문에 전혀 안 보이는 경우

특히 4번은 자주 나옵니다.

예를 들어 highlight를 항상 보이게 하고 싶다면 머티리얼이나 상태에서 depth test/write 정책을 다시 생각해야 합니다.

---

## 17. 새 월드 프리미티브 종류를 추가하는 경우

예를 들어 “월드 마커” 같은 새 오브젝트 타입을 추가한다고 해보겠습니다.

이 경우 보통 흐름은 이렇습니다.

### 단계 1. `SceneRenderPacket`에 새 버킷을 추가한다

파일:
- `Engine/Source/Level/SceneRenderPacket.h`

예:

```cpp
TArray<FSceneWorldMarkerPrimitive> WorldMarkerPrimitives;
```

### 단계 2. `ScenePacketBuilder`가 그것을 수집하게 만든다

파일:
- `Engine/Source/Level/ScenePacketBuilder.cpp`

### 단계 3. `SceneCommandBuilder` 하위 빌더가 그것을 `FMeshBatch`나 `PostProcessInputs`로 바꾼다

파일:
- `SceneCommandMeshBuilder.cpp`
- 또는 새 빌더 파일

### 단계 4. 새 패스 또는 기존 패스로 흘려보낸다

- 메시라면 `FMeshBatch` + `PassMask`
- 풀스크린/후처리라면 `PostProcessInputs`

이 흐름을 기억하면 새로운 기능도 대부분 같은 패턴으로 넣을 수 있습니다.

---

## 18. 패스 위치를 고르는 감각

새 패스를 넣을 때 가장 어려운 부분이 “그래서 어디에 꽂아야 하지?”입니다.

아래 기준을 쓰면 판단이 쉬워집니다.

### 18-1. Depth만 필요하다

최소한 `DepthPrepass` 이후면 됩니다.

예:

- 깊이 기반 마스크
- 깊이 시각화

### 18-2. 불투명 SceneColor가 필요하다

`ForwardOpaque` 이후가 좋습니다.

예:

- 불투명 장면 기준 색 보정
- Opaque 기반 데칼/마스크

### 18-3. 투명체까지 포함된 SceneColor가 필요하다

`ForwardTransparent` 이후가 좋습니다.

예:

- 최종 화면에 가까운 후처리
- 전체 화면 톤 조정

### 18-4. Overlay나 DebugLine보다 먼저 와야 한다

기즈모, 디버그, UI는 보통 가장 위에 보이길 원합니다.

따라서 그 위를 덮어쓰는 패스를 너무 뒤에 두면 의도와 어긋납니다.

### 18-5. 안티앨리어싱은 대개 마지막

FXAA 같은 화면 기반 AA는 이미 나온 최종 결과 전체를 대상으로 처리하므로 보통 마지막에 둡니다.

---

## 19. 디버깅할 때는 어디부터 보면 되나

무언가 화면에 안 나오면, 무작정 셰이더부터 보지 말고 아래 순서대로 좁혀가면 빠릅니다.

### 19-1. 수집 단계 확인

질문:

- 애초에 이 오브젝트가 `ScenePacket`에 들어갔는가
- `ViewportClient`가 그것을 수집했는가
- `ShowFlags` 때문에 걸러진 것은 아닌가

### 19-2. 조립 단계 확인

질문:

- `SceneViewData`에 실제 입력이 들어갔는가
- `MeshInputs.Batches`에 배치가 생겼는가
- `PostProcessInputs`가 비어 있지는 않은가

### 19-3. 패스 참여 여부 확인

질문:

- `PassMask`가 올바르게 붙었는가
- `ShouldDrawInPass()`가 true가 되는가
- 새 패스가 파이프라인에 등록되었는가

### 19-4. 상태/자원 충돌 확인

질문:

- 읽는 텍스처를 동시에 쓰고 있지는 않은가
- Depth test 때문에 안 보이는 것은 아닌가
- 머티리얼의 pass shader가 없는 것은 아닌가

### 19-5. 최종 합성 확인

질문:

- 게임이면 composite mode가 이상하지 않은가
- 에디터면 뷰포트 합성 단계에서 사각형 배치가 틀리지 않았는가
- UI나 다른 overlay가 덮어쓴 것은 아닌가

이 순서를 한 줄로 줄이면 이렇습니다.

```text
수집 -> 조립 -> 패스 참여 -> 상태/자원 -> 최종 합성
```

---

## 20. 초심자가 자주 헷갈리는 포인트 정리

### 20-1. 패스를 만들었는데 왜 실행이 안 되나

가장 흔한 원인은 세 가지입니다.

1. `ScenePipelineBuilder.cpp`에 등록하지 않음
2. 입력 데이터가 비어 있음
3. 스킵 조건이 항상 true로 걸림

### 20-2. 메시를 추가했는데 왜 안 보이나

가장 흔한 원인은 세 가지입니다.

1. `PassMask`가 맞지 않음
2. 머티리얼에 해당 pass shader가 없음
3. 깊이 상태나 블렌드 상태 때문에 묻힘

### 20-3. 후처리를 추가했는데 화면이 깨진다

자주 있는 원인은 이것입니다.

- 같은 텍스처를 읽으면서 동시에 쓰는 경우

이 경우는 scratch texture나 임시 RTV/SRV가 필요합니다.

### 20-4. 에디터에서는 보이는데 게임에서는 안 보인다

원인 후보:

1. 에디터 쪽 `AdditionalMeshBatches`에만 들어감
2. 에디터 전용 `OutlineRequest`, `DebugInputs`에만 들어감
3. 게임 경로의 frame request에는 빠져 있음

### 20-5. 게임에서는 보이는데 에디터에서는 안 보인다

원인 후보:

1. 에디터 뷰포트별 scene pass request에 입력이 안 들어감
2. 뷰포트 합성 단계에서 안 보이는 영역에 배치됨
3. Screen UI나 gizmo가 위를 덮음

---

## 21. 파일 지도로 다시 정리하면

처음 분석할 때는 아래 순서가 가장 좋습니다.

### 21-1. 프레임 시작점

- `Engine/Source/Renderer/Renderer.h`
- `Engine/Source/Renderer/Renderer.cpp`

### 21-2. 게임/에디터 프레임 흐름

- `Engine/Source/Renderer/Frame/GameFrameRenderer.cpp`
- `Engine/Source/Renderer/Frame/EditorFrameRenderer.cpp`
- `Editor/Source/Viewport/Services/EditorViewportRenderService.cpp`

### 21-3. 씬 수집

- `Engine/Source/Core/ViewportClient.cpp`
- `Engine/Source/Level/ScenePacketBuilder.cpp`
- `Engine/Source/Level/SceneRenderPacket.h`

### 21-4. 씬 조립

- `Engine/Source/Renderer/Scene/SceneRenderer.cpp`
- `Engine/Source/Renderer/Scene/SceneViewData.h`
- `Engine/Source/Renderer/Scene/Builders/SceneViewAssembler.cpp`
- `Engine/Source/Renderer/Scene/Builders/SceneCommandBuilder.cpp`
- `SceneCommandMeshBuilder.cpp`
- `SceneCommandTextBuilder.cpp`
- `SceneCommandSpriteBuilder.cpp`
- `SceneCommandPostProcessBuilder.cpp`

### 21-5. 패스 실행

- `Engine/Source/Renderer/Scene/Pipeline/ScenePipelineBuilder.cpp`
- `Engine/Source/Renderer/Scene/Passes/ScenePasses.h`
- `Engine/Source/Renderer/Scene/Passes/SceneBasePasses.cpp`
- `Engine/Source/Renderer/Scene/Passes/SceneEffectPasses.cpp`
- `Engine/Source/Renderer/Scene/MeshPassProcessor.cpp`

### 21-6. 최종 합성과 UI

- `Engine/Source/Renderer/Frame/Viewport/ViewportCompositor.cpp`
- `Engine/Source/Renderer/UI/Screen/ScreenUIRenderer.cpp`

---

## 22. 마지막으로 머릿속에 남겨야 할 모델

이 렌더러는 아래 문장으로 기억하면 됩니다.

**`ViewportClient`가 장면을 모으고, `SceneRenderer`가 그것을 패스용 데이터로 조립한 뒤, 패스 파이프라인이 실제로 그리며, 마지막에 `ViewportCompositor`와 `ScreenUIRenderer`가 화면을 완성한다.**

그리고 새 기능을 넣을 때는 항상 아래 순서로 생각하면 됩니다.

```text
입력은 어디서 오나
 -> ScenePacket에 넣을까 / FrameRequest에 넣을까
 -> SceneViewData에 어떻게 옮길까
 -> 어떤 패스에서 실행할까
 -> 그 패스를 파이프라인 어디에 둘까
 -> 읽기/쓰기 충돌은 없는가
```

이 흐름만 몸에 익으면, 렌더러가 더 이상 거대한 블랙박스처럼 보이지 않습니다.
새 패스를 추가하는 일도 “막연한 수정”이 아니라 “정해진 경로를 따라가는 작업”으로 바뀝니다.
