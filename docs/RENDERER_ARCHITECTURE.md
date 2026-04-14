# 렌더러 구조 안내서

## 1. 이 문서는 무엇을 설명하나요

이 문서는 **현재 코드베이스의 렌더러 구조만** 설명합니다.

목표는 세 가지입니다.

1. 처음 코드를 보는 분이 **전체 구조를 한 번에 이해**할 수 있게 하는 것
2. 각 클래스와 파일이 **무엇을 담당하는지 바로 찾을 수 있게** 하는 것
3. 새로운 기능이나 패스를 넣을 때 **어디를 수정해야 하는지 바로 판단**할 수 있게 하는 것

이 문서는 “클래스 이름 모음”이 아니라, 실제로 작업할 때 필요한 기준서를 목표로 작성했습니다.

---

## 2. 먼저 전체 구조를 한 문장으로 요약하면

현재 렌더러는 다음 흐름으로 움직입니다.

**뷰포트가 프레임 요청을 만들고 → `FRenderer`가 그 요청을 받아 → 씬 렌더링과 프레임 후반 합성을 순서대로 실행합니다.**

초심자 기준으로는 아래처럼 이해하시면 됩니다.

- `IViewportClient` / `FEditorViewportRenderService`:
  이번 프레임에 무엇을 그릴지 정리하는 쪽
- `FRenderer`:
  렌더러 전체 서브시스템을 소유하고 프레임 진입점을 제공하는 쪽
- `FGameFrameRenderer` / `FEditorFrameRenderer`:
  게임 프레임과 에디터 프레임의 실제 실행 순서를 잡는 쪽
- `FSceneRenderer`:
  한 개의 씬 뷰를 실제 장면 패스로 렌더링하는 쪽
- `FViewportCompositor`:
  여러 뷰포트 결과를 최종 백버퍼에 붙이는 쪽
- `FScreenUIRenderer`:
  화면 UI 드로우 리스트를 실제 GPU 드로우로 바꾸는 쪽

---

## 3. 전체 파일 지도

처음 코드를 읽을 때는 아래 순서로 보면 가장 빠릅니다.

### 3-1. 프레임 진입점

- `Engine/Source/Renderer/Renderer.h`
- `Engine/Source/Renderer/Renderer.cpp`

여기서 렌더러의 큰 얼굴을 봅니다.

### 3-2. 게임 프레임 / 에디터 프레임 조립

- `Engine/Source/Renderer/Frame/FrameRequests.h`
- `Engine/Source/Renderer/Frame/GameFrameRenderer.cpp`
- `Engine/Source/Renderer/Frame/EditorFrameRenderer.cpp`

여기서 “프레임 단위로 어떤 순서로 실행되는지”를 봅니다.

### 3-3. 씬 렌더링 중심

- `Engine/Source/Renderer/Scene/SceneRenderer.h`
- `Engine/Source/Renderer/Scene/SceneRenderer.cpp`
- `Engine/Source/Renderer/Scene/SceneViewData.h`

여기서 “씬 하나가 어떻게 렌더되는지”를 봅니다.

### 3-4. 씬 패스 순서 정의

- `Engine/Source/Renderer/Scene/Pipeline/ScenePipelineBuilder.cpp`
- `Engine/Source/Renderer/Scene/Passes/ScenePasses.h`
- `Engine/Source/Renderer/Scene/Passes/SceneBasePasses.cpp`
- `Engine/Source/Renderer/Scene/Passes/SceneEffectPasses.cpp`
- `Engine/Source/Renderer/Scene/Passes/SceneOverlayPasses.cpp`

새 패스를 넣을 때 가장 많이 보게 되는 파일입니다.

### 3-5. 월드 수집과 씬 데이터 조립

- `Engine/Source/Level/SceneRenderPacket.h`
- `Engine/Source/Level/ScenePacketBuilder.cpp`
- `Engine/Source/Core/ViewportClient.cpp`
- `Engine/Source/Renderer/Scene/Builders/SceneCommandBuilder.cpp`
- `Engine/Source/Renderer/Scene/Builders/SceneCommandMeshBuilder.cpp`
- `Engine/Source/Renderer/Scene/Builders/SceneCommandTextBuilder.cpp`
- `Engine/Source/Renderer/Scene/Builders/SceneCommandSpriteBuilder.cpp`
- `Engine/Source/Renderer/Scene/Builders/SceneCommandPostProcessBuilder.cpp`
- `Engine/Source/Renderer/Scene/Builders/SceneViewAssembler.cpp`

여기서 “월드 정보가 렌더 입력으로 바뀌는 과정”을 봅니다.

### 3-6. 최종 합성과 UI

- `Engine/Source/Renderer/Frame/Viewport/ViewportCompositor.cpp`
- `Engine/Source/Renderer/Frame/UI/FramePasses.cpp`
- `Engine/Source/Renderer/UI/Screen/ScreenUIRenderer.cpp`
- `Engine/Source/Renderer/UI/Screen/ScreenUIPassBuilder.cpp`
- `Engine/Source/Renderer/UI/Screen/ScreenUIBatchRenderer.cpp`
- `Editor/Source/Slate/Widget/Painter.cpp`
- `Editor/Source/Slate/SlateApplication.cpp`

여기서 “뷰포트를 화면에 합치는 단계”와 “UI를 올리는 단계”를 봅니다.

### 3-7. 렌더 타깃 관리

- `Engine/Source/Renderer/Common/SceneRenderTargets.h`
- `Engine/Source/Renderer/Frame/SceneTargetManager.cpp`

새 패스가 새 텍스처를 필요로 하면 반드시 보게 됩니다.

---

## 4. 가장 먼저 머릿속에 넣어야 할 핵심 데이터

렌더러를 이해할 때는 아래 네 가지를 먼저 구분하시면 됩니다.

### 4-1. `FSceneRenderPacket`

파일: `Engine/Source/Level/SceneRenderPacket.h`

이 구조는 **월드에서 수집한 렌더 대상 목록**입니다.

들어 있는 것은 다음과 같습니다.

- 메시 프리미티브
- 텍스트 프리미티브
- SubUV 프리미티브
- 빌보드 프리미티브
- 포그 프리미티브
- 데칼 프리미티브
- 파이어볼 프리미티브
- FXAA 적용 여부

중요한 점은 이것이 아직 **GPU 드로우 명령 목록은 아니라는 것**입니다.

이 구조는 “이 뷰에서 어떤 종류의 오브젝트를 렌더해야 하는가”를 담는 단계입니다.

### 4-2. `FMeshBatch`

파일: `Engine/Source/Renderer/Mesh/MeshBatch.h`

이 구조는 실제 드로우에 훨씬 가까운 단위입니다.

들어 있는 것은 다음과 같습니다.

- 어떤 메시를 그릴지
- 어떤 머티리얼을 쓸지
- 월드 행렬이 무엇인지
- 어떤 패스에서 그릴지 (`PassMask`)
- 깊이 테스트/깊이 쓰기/컬링을 어떻게 할지
- 투명 정렬에 필요한 거리
- 제출 순서

즉, `FMeshBatch`는 **실제 패스가 소비하는 장면 드로우 단위**라고 생각하시면 됩니다.

### 4-3. `FSceneViewData`

파일: `Engine/Source/Renderer/Scene/SceneViewData.h`

이 구조는 **한 뷰의 실제 렌더 입력 전체**입니다.

안에는 크게 세 묶음이 있습니다.

- `MeshInputs`: 메시 배치 목록
- `PostProcessInputs`: 포그, 데칼, 아웃라인, 파이어볼, FXAA 같은 후반 입력
- `DebugInputs`: 디버그 라인 입력

즉, `FSceneViewData`는 “한 카메라 시점의 씬을 렌더하기 위한 완성본”입니다.

### 4-4. `FSceneRenderTargets`

파일: `Engine/Source/Renderer/Common/SceneRenderTargets.h`

이 구조는 한 씬 렌더링이 사용하는 타깃 묶음입니다.

들어 있는 것은 다음과 같습니다.

- SceneColor
- SceneColorScratch
- SceneDepth
- GBuffer A/B/C
- OutlineMask

새 패스가 어떤 입력/출력을 필요로 하는지 따질 때 이 구조가 매우 중요합니다.

---

## 5. 현재 프레임 흐름

## 5-1. 게임 프레임 흐름

게임 쪽은 `FGameViewportClient::Render()`에서 시작합니다.

파일:
- `Engine/Source/Core/ViewportClient.cpp`
- `Engine/Source/Renderer/Frame/GameFrameRenderer.cpp`

흐름은 아래와 같습니다.

1. 활성 월드와 활성 카메라를 찾습니다.
2. 카메라의 View / Projection을 구합니다.
3. 프러스텀을 만듭니다.
4. `BuildSceneRenderPacket()`으로 현재 뷰에 보이는 프리미티브를 수집합니다.
5. `FGameFrameRequest`를 채웁니다.
6. `FRenderer::RenderGameFrame()`을 호출합니다.
7. 내부에서 `FGameFrameRenderer::Render()`가 실행됩니다.
8. 게임용 씬 타깃을 확보합니다.
9. `FSceneRenderer::BuildSceneViewData()`로 패킷을 실제 렌더 입력으로 조립합니다.
10. `FSceneRenderer::RenderSceneView()`가 씬 패이프라인을 실행합니다.
11. 렌더된 SceneColor/Depth를 `FViewportCompositePass`로 최종 백버퍼에 붙입니다.

텍스트로 그리면 아래와 같습니다.

```text
FGameViewportClient
 -> FGameFrameRequest
 -> FRenderer::RenderGameFrame
 -> FGameFrameRenderer
 -> FSceneRenderer::BuildSceneViewData
 -> FSceneRenderer::RenderSceneView
 -> Scene Pass Pipeline
 -> Viewport Composite Pass
```

### 5-2. 에디터 프레임 흐름

에디터 쪽은 `FEditorViewportRenderService::RenderAll()`이 핵심입니다.

파일:
- `Editor/Source/Viewport/Services/EditorViewportRenderService.cpp`
- `Engine/Source/Renderer/Frame/EditorFrameRenderer.cpp`

흐름은 아래와 같습니다.

1. 활성 에디터 뷰포트 엔트리들을 순회합니다.
2. 각 뷰포트의 렌더 타깃과 깊이 타깃을 확인합니다.
3. 각 뷰포트의 View / Projection / Frustum을 계산합니다.
4. 각 뷰포트마다 `FSceneRenderPacket`을 만듭니다.
5. 기즈모와 그리드는 `AdditionalMeshBatches`로 따로 넣습니다.
6. 선택된 액터가 있으면 `OutlineRequest`를 만듭니다.
7. 디버그 드로우 입력을 채웁니다.
8. 이를 `FViewportScenePassRequest`로 묶습니다.
9. 모든 뷰포트의 결과를 `CompositeItems`로 정리합니다.
10. `FSlateApplication::BuildDrawList()`로 화면 UI 드로우 리스트를 만듭니다.
11. 이를 `FEditorFrameRequest`로 묶어 `FRenderer::RenderEditorFrame()`에 넘깁니다.
12. 내부에서 각 뷰포트 씬을 먼저 렌더합니다.
13. 그 뒤 여러 뷰포트 결과를 백버퍼에 합성합니다.
14. 마지막에 화면 UI를 그립니다.

텍스트로 그리면 아래와 같습니다.

```text
FEditorViewportRenderService
 -> many FViewportScenePassRequest
 -> FEditorFrameRequest
 -> FRenderer::RenderEditorFrame
 -> per-viewport SceneRenderer
 -> ViewportCompositePass
 -> ScreenUIPass
```

에디터 프레임은 **씬 렌더링이 여러 번 돌고**, 그 결과를 **나중에 한 화면으로 모아 붙인다**는 점을 기억하시면 됩니다.

---

## 6. 각 클래스는 무엇을 담당하나요

## 6-1. `FRenderer`

파일: `Engine/Source/Renderer/Renderer.h`

이 클래스는 렌더러의 최상위 소유자입니다.

담당 업무:

- `FRenderDevice` 소유
- `FSceneRenderer` 소유
- `FViewportCompositor` 소유
- `FScreenUIRenderer` 소유
- Text / SubUV / Billboard / Fog / Outline / Decal / FireBall / FXAA / DebugLine feature 소유
- 게임 프레임 진입점 제공
- 에디터 프레임 진입점 제공
- 공용 constant buffer와 sampler 관리

작업자가 기억해야 할 점:

- 새 기능이 렌더러 전역 서브시스템이라면 여기서 소유하거나 getter를 추가할 가능성이 큽니다.
- 새 feature를 초기화하려면 `RendererResourceBootstrap.cpp`도 함께 봐야 합니다.

## 6-2. `FRenderDevice`

파일: `Engine/Source/Renderer/GraphicsCore/RenderDevice.h`

이 클래스는 D3D11 디바이스/컨텍스트/스왑체인/백버퍼를 다룹니다.

담당 업무:

- 디바이스 생성
- 스왑체인 생성
- 백버퍼 RTV/DSV 관리
- 리사이즈 처리
- `BeginFrame`, `EndFrame`, `Present`

작업자가 기억해야 할 점:

- 씬 패스를 추가할 때는 보통 여기까지 건드리지 않습니다.
- 창 크기나 백버퍼 자체 정책을 바꿀 때 봅니다.

## 6-3. `FGameFrameRenderer`

파일: `Engine/Source/Renderer/Frame/GameFrameRenderer.cpp`

이 클래스는 **게임 프레임 전체 실행 순서**를 정합니다.

담당 업무:

- 게임용 씬 타깃 확보
- `FrameContext`, `ViewContext` 구성
- `SceneViewData` 생성 요청
- 데칼 텍스처 배열 준비
- 디버그 라인 입력 구성
- 한 뷰의 씬 렌더 실행
- 최종 화면 합성 패스 실행

작업자가 기억해야 할 점:

- 게임 화면 전용 후처리나 프레임 마지막 단계가 필요하면 여기서 순서를 손볼 수 있습니다.

## 6-4. `FEditorFrameRenderer`

파일: `Engine/Source/Renderer/Frame/EditorFrameRenderer.cpp`

이 클래스는 **에디터 프레임 전체 실행 순서**를 정합니다.

담당 업무:

- 뷰포트별 외부 타깃 래핑
- 각 뷰포트별 `SceneViewData` 구성
- 아웃라인 입력 연결
- 디버그 입력 연결
- 뷰포트별 씬 렌더 실행
- 최종 뷰포트 합성
- 화면 UI 패스 실행

작업자가 기억해야 할 점:

- 에디터 전용 후반 패스를 넣으려면 가장 먼저 여기서 위치를 생각하셔야 합니다.
- “씬 렌더 전에 필요한가, 뷰포트 합성 뒤에 필요한가, UI 뒤에 필요한가”를 먼저 정해야 합니다.

## 6-5. `FSceneTargetManager`

파일: `Engine/Source/Renderer/Frame/SceneTargetManager.cpp`

이 클래스는 씬 렌더링에 필요한 오프스크린 타깃 묶음을 준비합니다.

담당 업무:

- 게임 씬용 SceneColor / SceneDepth 생성
- 보조 타깃(GBuffer, Scratch, OutlineMask) 생성
- 에디터 뷰포트가 가진 외부 RTV/DSV/SRV를 `FSceneRenderTargets` 형태로 감싸기

작업자가 기억해야 할 점:

- 새 패스가 **새 텍스처**를 필요로 하면 여기까지 수정해야 합니다.
- `SceneRenderTargets.h` 구조체와 `SceneTargetManager.cpp` 생성/해제 로직은 항상 같이 움직입니다.

## 6-6. `FSceneRenderer`

파일: `Engine/Source/Renderer/Scene/SceneRenderer.cpp`

이 클래스는 **한 개의 씬 뷰를 실제로 렌더하는 중심 클래스**입니다.

담당 업무:

- `FSceneRenderPacket`을 `FSceneViewData`로 조립
- wireframe override 적용
- 씬 패이프라인 구성
- 패스 순서대로 실행

작업자가 기억해야 할 점:

- 새 씬 패스를 넣더라도 보통 `FSceneRenderer` 자체는 크게 바뀌지 않습니다.
- 대부분은 `BuildSceneViewData` 쪽이나 `ScenePipelineBuilder.cpp` 쪽을 수정합니다.

## 6-7. `FSceneCommandBuilder`와 하위 빌더들

파일:
- `Engine/Source/Renderer/Scene/Builders/SceneCommandBuilder.cpp`
- `SceneCommandMeshBuilder.cpp`
- `SceneCommandTextBuilder.cpp`
- `SceneCommandSpriteBuilder.cpp`
- `SceneCommandPostProcessBuilder.cpp`

이 묶음은 **장면 패킷을 실제 렌더 입력으로 조립**합니다.

각자의 역할은 아래와 같습니다.

### `FSceneCommandMeshBuilder`

- `MeshPrimitives`를 `FMeshBatch`들로 바꿉니다.
- 기본 메시는 `DepthPrepass + GBuffer + ForwardOpaque` 마스크로 들어갑니다.

### `FSceneCommandTextBuilder`

- `TextPrimitives`를 텍스트 메시 배치로 바꿉니다.
- UUID billboard 텍스트는 Overlay 도메인으로 들어갑니다.

### `FSceneCommandSpriteBuilder`

- `SubUVPrimitives`, `BillboardPrimitives`를 투명 배치로 바꿉니다.
- 빌보드 변환, 카메라 거리 계산도 여기서 합니다.

### `FSceneCommandPostProcessBuilder`

- `FogPrimitives`, `DecalPrimitives`, `FireBallPrimitives`를 후반 패스 입력으로 바꿉니다.

작업자가 기억해야 할 점:

- **새 프리미티브 타입**을 추가하려면 거의 항상 여기까지 옵니다.
- “패킷에는 들어갔는데 실제 렌더링이 안 된다”면 이 구간부터 확인하시면 됩니다.

## 6-8. `FMeshPassProcessor`

파일: `Engine/Source/Renderer/Scene/MeshPassProcessor.cpp`

이 클래스는 메시 배치를 패스별로 필터링하고 정렬해서 그립니다.

담당 업무:

- 배치의 `PassMask` 검사
- 패스별 정렬 정책 적용
- 머티리얼/렌더 상태 바인딩
- 메시 바인딩
- Draw / DrawIndexed 실행

현재 정렬 규칙:

- `ForwardTransparent`: 카메라에서 먼 것부터
- `Overlay`: 제출 순서 유지
- `DepthPrepass`, `GBuffer`, `ForwardOpaque`: 상태/셰이더 중심 정렬

작업자가 기억해야 할 점:

- 새 **메시 패스**를 추가하려면 `EMeshPassType`, `EMeshPassMask`, `ShouldDrawInPass`, `ToMaterialPassType`까지 같이 봐야 합니다.

## 6-9. `FViewportCompositor`

파일: `Engine/Source/Renderer/Frame/Viewport/ViewportCompositor.cpp`

이 클래스는 최종 백버퍼에 뷰포트 결과를 붙입니다.

담당 업무:

- SceneColor 또는 SceneDepth를 소스로 선택
- 뷰포트 사각형에 맞게 전체화면 삼각형으로 복사
- DepthView 같은 시각화 모드 지원

작업자가 기억해야 할 점:

- “에디터 뷰포트 결과를 화면에 어떻게 배치하는가”는 여기서 결정됩니다.
- 뷰포트 시각화 모드를 늘리려면 여기와 `FViewportCompositeItem` 해석 쪽을 봅니다.

## 6-10. `FScreenUIRenderer`

파일: `Engine/Source/Renderer/UI/Screen/ScreenUIRenderer.cpp`

이 클래스는 최종 UI 드로우 리스트를 실제 GPU 드로우로 바꿉니다.

역할은 내부적으로 둘로 나뉩니다.

### `FScreenUIPassBuilder`

- `FUIDrawList`를 읽습니다.
- 사각형/외곽선/텍스트를 메시로 만듭니다.
- 레이어, 깊이, 순서 기준으로 정렬합니다.
- 직교 투영을 설정합니다.

### `FScreenUIBatchRenderer`

- 메시 버퍼를 업로드합니다.
- UI 머티리얼과 상태를 바인딩합니다.
- 실제 Draw를 실행합니다.

작업자가 기억해야 할 점:

- 화면 UI 요소 타입을 늘리려면 `UIDrawList.h`와 `ScreenUIPassBuilder.cpp`를 같이 수정하면 됩니다.

## 6-11. `FSlatePaintContext`와 `FSlateApplication`

파일:
- `Editor/Source/Slate/Widget/Painter.cpp`
- `Editor/Source/Slate/SlateApplication.cpp`

이쪽은 화면 UI를 **즉시 렌더링하지 않고 기록만 하는 계층**입니다.

`FSlatePaintContext`가 하는 일:

- FilledRect 기록
- RectOutline 기록
- Text 기록
- ClipRect 기록
- Layer / Depth / Order 기록

`FSlateApplication`이 하는 일:

- 위젯 트리 배치
- 뷰포트 및 크롬 레이아웃 계산
- paint 호출
- 최종 `FUIDrawList` 빌드

작업자가 기억해야 할 점:

- UI 쪽 기능을 추가할 때는 “Slate 기록 단계”와 “실제 GPU UI 렌더 단계”를 따로 생각해야 합니다.

---

## 7. 현재 씬 패스 순서

현재 기본 씬 패스 순서는 아래 파일에서 정의됩니다.

파일: `Engine/Source/Renderer/Scene/Pipeline/ScenePipelineBuilder.cpp`

현재 순서:

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

초심자 기준으로 각 패스를 간단히 설명하면 아래와 같습니다.

### 7-1. `FClearSceneTargetsPass`

- SceneColor, SceneDepth, GBuffer, OutlineMask를 초기화합니다.
- 프레임마다 새 캔버스를 만드는 단계라고 생각하시면 됩니다.

### 7-2. `FUploadMeshBuffersPass`

- 이번 프레임에 필요한 메시 버퍼를 GPU에 올립니다.
- 실제 그리기 전에 준비만 하는 단계입니다.

### 7-3. `FDepthPrepass`

- 깊이만 먼저 채웁니다.
- 이후 패스가 깊이 정보를 활용할 수 있게 합니다.

### 7-4. `FGBufferPass`

- GBuffer A/B/C를 채웁니다.
- 후처리나 데칼 같은 기능이 씬 표면 정보를 사용할 수 있게 합니다.

### 7-5. `FForwardOpaquePass`

- 불투명 메시를 SceneColor에 그립니다.

### 7-6. `FDecalCompositePass`

- 데칼 입력이 있으면 데칼을 합성합니다.
- 현재는 일반 데칼 feature 또는 volume decal feature 중 하나가 실행됩니다.

### 7-7. `FForwardTransparentPass`

- 투명 오브젝트를 뒤에서 앞으로 정렬하여 그립니다.

### 7-8. `FFogPostPass`

- 포그가 있으면 씬 결과 위에 포그를 적용합니다.

### 7-9. `FFireBallPass`

- 파이어볼 관련 효과를 적용합니다.

### 7-10. `FOutlineMaskPass` / `FOutlineCompositePass`

- 선택된 오브젝트 윤곽선용 마스크를 만들고
- 그 마스크를 이용해 최종 씬에 외곽선을 합성합니다.

### 7-11. `FOverlayPass`

- 오버레이 성격의 메시를 그립니다.
- 기즈모 텍스트나 특정 화면 상부 요소가 여기에 들어갈 수 있습니다.

### 7-12. `FDebugLinePass`

- 디버그 라인을 그립니다.

### 7-13. `FFXAAPass`

- `bApplyFXAA`가 켜져 있으면 마지막에 FXAA를 적용합니다.

---

## 8. 현재 프레임 후반 패스 순서

씬 렌더링이 끝나면 프레임 후반 패스가 실행됩니다.

관련 파일:
- `Engine/Source/Renderer/Frame/UI/FramePasses.h`
- `Engine/Source/Renderer/Frame/UI/FramePasses.cpp`
- `Engine/Source/Renderer/Frame/UI/FramePipeline.h`

현재 사용되는 프레임 패스는 두 개입니다.

1. `FViewportCompositePass`
2. `FScreenUIPass`

### `FViewportCompositePass`

- 에디터: 여러 뷰포트 결과를 최종 백버퍼에 배치합니다.
- 게임: 단일 SceneColor 결과를 전체 화면에 붙입니다.

### `FScreenUIPass`

- 화면 UI를 최종 결과 위에 그립니다.

즉, **씬 패스**와 **프레임 후반 패스**는 별도 계층입니다.

이 구분이 매우 중요합니다.

- 씬 내부에 들어갈 패스인가?
- 최종 화면 합성 뒤에 들어갈 패스인가?

이 두 질문에 따라 수정 위치가 완전히 달라집니다.

---

## 9. “무엇을 하려면 어디를 수정하나요?”

실무에서는 이 섹션이 가장 중요합니다.

## 9-1. 새 월드 프리미티브 타입을 추가하고 싶을 때

예시:
- `ULaserBeamComponent`
- `UWorldMarkerComponent`
- `UHeatDistortionComponent`

수정 순서:

1. `SceneRenderPacket.h`에 새 프리미티브 구조와 배열을 추가합니다.
2. `ScenePacketBuilder.cpp`에서 ShowFlag와 타입 판별을 추가합니다.
3. 필요하면 `ViewportClient.cpp`에서 프러스텀 외 수집 로직을 추가합니다.
4. `SceneCommandBuilder` 하위 빌더 중 알맞은 곳에서 `FSceneViewData` 입력으로 바꿉니다.
5. 메시로 렌더할 것인지, 후처리 입력으로 넣을 것인지 결정합니다.
6. 실제 패스가 그 입력을 소비하도록 연결합니다.

판단 기준:

- 메시처럼 그릴 수 있으면 `FMeshBatch`로 만듭니다.
- 후처리 성격이면 `PostProcessInputs` 쪽으로 넣습니다.

## 9-2. 기존 메시를 특정 패스에만 보내고 싶을 때

예시:
- 어떤 메시를 Overlay 패스에만 보내기
- 투명 전용 패스로 보내기

주로 수정할 곳:

- `MeshBatch.h`: `EMeshPassMask`, 필요하면 `EMeshPassType`
- `SceneCommandMeshBuilder.cpp` 또는 관련 빌더: `Batch.PassMask`
- `MeshPassProcessor.cpp`: `ShouldDrawInPass`, 정렬 규칙

핵심은 **배치를 만들 때 어떤 `PassMask`를 주는가**입니다.

## 9-3. 새 후처리 입력을 추가하고 싶을 때

예시:
- Bloom 입력
- SSAO 입력
- Custom Highlight 입력

수정 순서:

1. `SceneViewData.h`의 `FScenePostProcessInputs`에 새 입력 필드를 추가합니다.
2. `SceneRenderPacket.h`에 필요한 프리미티브나 설정을 추가합니다.
3. `ScenePacketBuilder.cpp` 또는 `ViewportClient.cpp`에서 패킷에 입력을 채웁니다.
4. `SceneCommandPostProcessBuilder.cpp`에서 `PostProcessInputs`로 변환합니다.
5. 새 패스를 만들어 이 입력을 소비하게 합니다.
6. `ScenePipelineBuilder.cpp`에 새 패스를 추가합니다.

## 9-4. 씬 패스를 하나 추가하고 싶을 때

이 경우가 가장 자주 나옵니다.

예시:
- SSAO 패스
- Bloom threshold 패스
- Custom edge detect 패스
- SceneColor를 변형하는 풀스크린 패스

수정 순서:

1. 새 패스 클래스를 `ScenePasses.h`에 선언합니다.
2. 구현을 `SceneBasePasses.cpp`, `SceneEffectPasses.cpp`, `SceneOverlayPasses.cpp` 중 성격에 맞는 파일에 넣습니다.
3. 필요하면 새 feature 클래스를 만듭니다.
4. `ScenePipelineBuilder.cpp`에 실행 순서를 추가합니다.
5. 그 패스가 읽을 입력을 `FSceneViewData`에 넣습니다.
6. 새 타깃이 필요하면 `SceneRenderTargets.h`와 `SceneTargetManager.cpp`를 수정합니다.

가장 중요한 질문:

- 이 패스는 **SceneColor를 읽기만 하는가**
- 아니면 **새 텍스처에 써야 하는가**
- 아니면 **깊이 / GBuffer가 필요한가**

이 질문에 따라 필요한 수정 범위가 달라집니다.

## 9-5. 새 렌더 타깃이 필요할 때

예시:
- SSAO 결과 텍스처
- Blur ping-pong 텍스처
- Custom mask 텍스처

수정할 곳:

1. `SceneRenderTargets.h`에 텍스처 / RTV / SRV 필드를 추가합니다.
2. `SceneTargetManager.h`에 멤버를 추가합니다.
3. `SceneTargetManager.cpp`의 생성 함수에서 타깃을 만듭니다.
4. `ReleaseSupplementalTargets()`와 `Release()`에 해제를 추가합니다.
5. `AcquireGameSceneTargets()`와 `WrapExternalSceneTargets()`에서 `OutTargets`에 연결합니다.

초심자 체크포인트:

- 새 패스가 읽으려면 SRV가 필요합니다.
- 새 패스가 쓰려면 RTV 또는 DSV가 필요합니다.
- 둘 다 필요하면 texture 생성 시 bind flag도 맞아야 합니다.

## 9-6. 에디터 전용 기즈모/그리드/보조 메시를 추가하고 싶을 때

수정할 곳:

- `Editor/Source/Viewport/Services/EditorViewportRenderService.cpp`

이 파일에서 `AdditionalMeshBatches`를 만들고 있으므로,
에디터 전용 보조 메시를 넣으려면 여기서 `FMeshBatch`를 추가하시면 됩니다.

대표 사례:

- 그리드
- 기즈모 메시
- 특정 에디터 보조 메쉬

## 9-7. 최종 화면 합성 단계를 추가하고 싶을 때

예시:
- 뷰포트 합성 후 전체 화면 톤매핑
- UI 전에 최종 색보정
- 에디터 전체 화면 디버그 시각화

이 경우는 씬 패스가 아니라 **프레임 패스**입니다.

수정할 곳:

1. `FramePassContext.h`에 필요한 입력을 추가합니다.
2. `FramePasses.h/cpp`에 새 `IFrameRenderPass` 구현을 추가합니다.
3. `GameFrameRenderer.cpp` 또는 `EditorFrameRenderer.cpp`에서 `FramePipeline.AddPass(...)` 순서를 조정합니다.

판단 기준:

- 개별 뷰포트 씬 안에서 돌아야 하면 씬 패스
- 최종 백버퍼 단계에서 돌아야 하면 프레임 패스

## 9-8. 새로운 UI 도형이나 UI 요소 타입을 추가하고 싶을 때

예시:
- 선 그리기
- 이미지 드로우
- 아이콘 드로우
- 둥근 사각형

수정할 곳:

1. `UIDrawList.h`에 `EUIDrawElementType`와 데이터 필드를 추가합니다.
2. `Painter.cpp`에 기록 API를 추가합니다.
3. `ScreenUIPassBuilder.cpp`에 해당 타입을 메시로 바꾸는 코드를 추가합니다.
4. 필요하면 전용 머티리얼이나 텍스처 바인딩을 넣습니다.

## 9-9. 새 렌더 feature 클래스를 붙이고 싶을 때

예시:
- `FBloomRenderFeature`
- `FCustomAOFeature`

수정할 곳:

1. feature 클래스 작성
2. `Renderer.h`에 멤버 포인터 / getter 추가
3. `RendererResourceBootstrap.cpp`에서 생성 및 초기화
4. 패스에서 `Renderer.Get...Feature()`로 호출

이 패턴은 Fog, Outline, DebugLine, Decal, FireBall, FXAA가 이미 보여주고 있습니다.

---

## 10. 패스를 추가하려면 정확히 어디를 건드리면 되나요

이 섹션은 **새 패스 추가 절차**만 따로 정리한 체크리스트입니다.

## 10-1. 케이스 A: 씬 패이프라인 안에 새 패스를 추가하는 경우

예시:
- SSAO
- Bloom prefilter
- Custom composite
- Object ID mask 생성

### 1단계. 패스가 사용할 입력을 정합니다

먼저 아래 중 어디에서 입력을 받을지 정합니다.

- `FSceneViewData::MeshInputs`
- `FSceneViewData::PostProcessInputs`
- `FSceneViewData::DebugInputs`
- `FSceneRenderTargets`

질문 예시:

- 메시 목록이 필요한가?
- SceneColor가 필요한가?
- Depth가 필요한가?
- GBuffer가 필요한가?
- 별도 텍스처 출력이 필요한가?

### 2단계. 새 출력 타깃이 필요하면 렌더 타깃을 추가합니다

수정 파일:

- `SceneRenderTargets.h`
- `SceneTargetManager.h`
- `SceneTargetManager.cpp`

여기서 새 텍스처 / RTV / SRV를 만들고 `OutTargets`에 연결합니다.

### 3단계. 패스 클래스를 선언합니다

수정 파일:

- `ScenePasses.h`

예시 형태:

```cpp
class ENGINE_API FMyCustomPass : public IRenderPass
{
public:
    bool Execute(FPassContext& Context) override;
};
```

### 4단계. 패스 구현을 작성합니다

수정 파일:

- 메시 성격이면 `SceneBasePasses.cpp`
- 효과 성격이면 `SceneEffectPasses.cpp`
- 오버레이/디버그 성격이면 `SceneOverlayPasses.cpp`

보통 `Execute()` 안에서 아래 중 하나를 합니다.

- `FMeshPassProcessor`로 메시 패스를 돌린다.
- 특정 feature의 `Render()`를 호출한다.
- 전체화면 패스를 실행한다.

### 5단계. 실행 순서를 파이프라인에 넣습니다

수정 파일:

- `ScenePipelineBuilder.cpp`

예시:

```cpp
OutPipeline.AddPass(std::make_unique<FMyCustomPass>());
```

여기서 순서가 매우 중요합니다.

예를 들어:

- Depth 이후가 필요한지
- GBuffer 이후가 필요한지
- 투명체 전에 해야 하는지
- 아웃라인 전에 해야 하는지
- FXAA 전에 해야 하는지

를 먼저 정해야 합니다.

### 6단계. 패스 입력을 채웁니다

수정 파일 후보:

- `SceneRenderPacket.h`
- `ScenePacketBuilder.cpp`
- `ViewportClient.cpp`
- `SceneCommandPostProcessBuilder.cpp`
- `SceneCommandMeshBuilder.cpp`
- `SceneCommandSpriteBuilder.cpp`
- `SceneCommandTextBuilder.cpp`

즉, 패스 자체만 만들어서는 동작하지 않고,
**그 패스가 읽을 데이터를 `FSceneViewData`에 넣어야** 합니다.

### 7단계. feature가 필요하면 렌더러에 연결합니다

수정 파일:

- `Renderer.h`
- `RendererResourceBootstrap.cpp`

새 feature를 렌더러가 소유하도록 연결한 뒤,
패스에서 getter를 통해 호출하게 합니다.

---

## 10-2. 케이스 B: 메시 패스 자체를 하나 더 추가하는 경우

예시:
- `EMeshPassType::CustomMask`
- `EMeshPassType::Velocity`

이 경우는 일반 후처리 패스보다 수정 범위가 조금 더 넓습니다.

### 수정 체크리스트

1. `MeshBatch.h`
   - `EMeshPassType`에 새 값 추가
   - `EMeshPassMask`에 새 비트 추가

2. `MeshPassProcessor.cpp`
   - `ToMaterialPassType()` 확장
   - `ShouldDrawInPass()` 확장
   - 정렬 정책 필요 시 추가

3. 메시를 만드는 빌더
   - 해당 배치에 새 `PassMask`를 주도록 수정

4. 새 패스 클래스 추가
   - `ScenePasses.h/cpp`

5. 파이프라인 순서 추가
   - `ScenePipelineBuilder.cpp`

이 케이스는 “새 패스”이면서 동시에 “새 메시 필터 기준”이 추가되는 것이므로,
`PassMask`와 `ScenePipelineBuilder`를 둘 다 수정해야 합니다.

---

## 10-3. 케이스 C: 최종 프레임 단계에 새 패스를 추가하는 경우

예시:
- 백버퍼 전체 색보정
- 뷰포트 합성 후 디버그 오버레이
- UI 전용 블러 배경

수정 체크리스트:

1. `FramePassContext.h`
   - 새 패스가 읽어야 할 입력 추가

2. `FramePasses.h`
   - 새 `IFrameRenderPass` 선언

3. `FramePasses.cpp`
   - `Execute()` 구현

4. `GameFrameRenderer.cpp` 또는 `EditorFrameRenderer.cpp`
   - `FramePipeline.AddPass(...)` 위치 추가

예를 들어,

- UI 전에 넣고 싶으면 `FScreenUIPass` 앞
- UI 뒤에 넣고 싶으면 `FScreenUIPass` 뒤

에 넣으면 됩니다.

---

## 11. 초심자가 가장 자주 헷갈리는 구분

## 11-1. `SceneRenderPacket`과 `SceneViewData`의 차이

- `SceneRenderPacket`:
  월드에서 수집한 원재료 목록
- `SceneViewData`:
  실제 렌더러가 사용할 가공 완료본

즉,

- 수집 단계는 `ScenePacketBuilder`
- 조립 단계는 `SceneCommandBuilder`

입니다.

## 11-2. 씬 패스와 프레임 패스의 차이

- 씬 패스:
  한 개의 뷰포트 SceneColor/Depth/GBuffer를 만드는 과정
- 프레임 패스:
  최종 백버퍼에서 뷰포트 결과를 합치고 UI를 올리는 과정

## 11-3. `AdditionalMeshBatches`의 의미

이것은 월드에서 수집된 정규 프리미티브가 아니라,
**프레임 구성 단계에서 따로 넣는 보조 배치**입니다.

대표 예:

- 기즈모
- 그리드
- 에디터 전용 보조 메시

## 11-4. 아웃라인은 왜 `ScenePacketBuilder`가 아니라 프레임 요청 쪽에 있나

현재 구조에서 아웃라인은 일반 월드 수집 결과가 아니라,
**에디터가 특정 선택 상태를 해석해서 넣는 별도 요청**입니다.

그래서 `FViewportScenePassRequest`의 `OutlineRequest`를 통해 전달됩니다.

---

## 12. 실제 작업 시 추천 확인 순서

화면에 뭔가 안 보일 때는 아래 순서로 확인하시면 가장 빠릅니다.

### 경우 1. 월드 오브젝트가 아예 안 보인다

1. `ViewportClient.cpp`에서 패킷이 채워졌는지
2. `ScenePacketBuilder.cpp`에서 타입이 수집되는지
3. `SceneCommandBuilder` 하위 빌더에서 `FMeshBatch` 또는 후처리 입력이 만들어졌는지
4. `FSceneViewData`에 실제로 들어갔는지
5. `ScenePipelineBuilder.cpp`에 필요한 패스가 들어 있는지
6. 해당 패스 `Execute()`가 호출되는지

### 경우 2. 패스는 돌았는데 화면 결과가 이상하다

1. `SceneRenderTargets`에서 올바른 타깃을 쓰는지
2. 패스 전후로 RTV/DSV/SRV가 맞는지
3. `SceneColorScratch` 같은 중간 타깃이 필요한데 빠진 것은 아닌지
4. 순서가 맞는지

### 경우 3. 에디터 화면에는 안 보이는데 씬 렌더는 된 것 같다

1. `CompositeItems`가 올바른지
2. `FViewportCompositor`가 원하는 SRV를 읽는지
3. `Rect`가 올바른지
4. 최종 백버퍼에 실제로 합성됐는지

### 경우 4. UI가 안 보인다

1. `FSlateApplication::BuildDrawList()`가 실제로 엘리먼트를 기록했는지
2. `FUIDrawList`의 `ScreenWidth`, `ScreenHeight`가 맞는지
3. `ScreenUIPassBuilder.cpp`가 메시를 만들었는지
4. `FScreenUIPass`가 실행됐는지

---

## 13. 작업 유형별 바로가기

### 작업: 새 씬 패스 추가

먼저 볼 파일:

- `ScenePipelineBuilder.cpp`
- `ScenePasses.h`
- `SceneEffectPasses.cpp`
- `SceneViewData.h`
- `SceneTargetManager.cpp`

### 작업: 새 프리미티브 수집

먼저 볼 파일:

- `SceneRenderPacket.h`
- `ScenePacketBuilder.cpp`
- `ViewportClient.cpp`
- `SceneCommandBuilder.cpp`

### 작업: 새 메시 패스 마스크 추가

먼저 볼 파일:

- `MeshBatch.h`
- `MeshPassProcessor.cpp`
- 관련 SceneCommand builder

### 작업: 에디터 전용 기즈모/그리드/오버레이 추가

먼저 볼 파일:

- `EditorViewportRenderService.cpp`
- `FrameRequests.h`
- 필요 시 `ScenePasses.cpp`

### 작업: 화면 UI 요소 추가

먼저 볼 파일:

- `UIDrawList.h`
- `Painter.cpp`
- `ScreenUIPassBuilder.cpp`
- `ScreenUIBatchRenderer.cpp`

### 작업: 새 렌더 타깃 추가

먼저 볼 파일:

- `SceneRenderTargets.h`
- `SceneTargetManager.h`
- `SceneTargetManager.cpp`

---

## 14. 마지막으로 이 구조를 기억하는 가장 쉬운 방법

현재 구조는 아래 한 줄로 기억하시면 됩니다.

**뷰포트/에디터 서비스가 프레임 요청을 만들고, `FRenderer`가 이를 받아 씬 패스와 프레임 패스를 순서대로 실행합니다.**

조금 더 풀면 이렇게 됩니다.

1. `ScenePacketBuilder`가 월드에서 렌더 대상을 모읍니다.
2. `SceneCommandBuilder`가 그것을 실제 렌더 입력으로 바꿉니다.
3. `SceneRenderer`가 씬 패이프라인을 돌립니다.
4. `ViewportCompositor`가 결과를 최종 화면에 붙입니다.
5. `ScreenUIRenderer`가 마지막 UI를 올립니다.

새 작업을 시작할 때는 항상 먼저 이 질문부터 하시면 됩니다.

- 이건 **월드 수집 단계**인가?
- 이건 **씬 패스 단계**인가?
- 이건 **최종 프레임 합성 단계**인가?
- 이건 **UI 기록/렌더 단계**인가?

이 질문만 정확히 잡으면, 수정 위치를 훨씬 빠르게 찾을 수 있습니다.
