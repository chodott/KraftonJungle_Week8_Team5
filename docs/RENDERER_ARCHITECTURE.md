# 렌더러 구조 안내서

## 1. 이 문서는 무엇을 설명하나요

이 문서는 **현재 코드베이스의 렌더러 구조만** 설명합니다.

목표는 세 가지입니다.

1. 처음 코드를 보는 분이 **전체 구조를 한 번에 이해**할 수 있게 하는 것
2. 각 클래스와 파일이 **무엇을 담당하는지 바로 찾을 수 있게** 하는 것
3. 새로운 기능이나 패스를 넣을 때 **어디를 수정해야 하는지 바로 판단**할 수 있게 하는 것

이 문서는 클래스 목록이 아니라, 실제 작업할 때 기준이 되는 구조 문서입니다.

---

## 2. 먼저 전체 구조를 한 문장으로 요약하면

현재 렌더러는 다음 흐름으로 움직입니다.

**뷰포트 쪽이 프레임 요청을 만들고 → `FRenderer`가 그 요청을 받아 → 씬 패스를 실행하고 → 마지막에 뷰포트 합성과 Screen UI를 처리합니다.**

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
  뷰포트 결과를 최종 백버퍼에 배치하는 쪽
- `FScreenUIRenderer`:
  Slate가 기록한 화면 UI를 GPU 드로우로 바꾸는 쪽

---

## 3. 전체 파일 지도

처음 코드를 읽을 때는 아래 순서로 보면 가장 빠릅니다.

### 3-1. 프레임 진입점

- `Engine/Source/Renderer/Renderer.h`
- `Engine/Source/Renderer/Renderer.cpp`
- `Engine/Source/Renderer/Frame/FrameRequests.h`

여기서 렌더러의 큰 얼굴과 프레임 요청 구조를 봅니다.

### 3-2. 게임 프레임 / 에디터 프레임 조립

- `Engine/Source/Core/ViewportClient.cpp`
- `Editor/Source/Viewport/Services/EditorViewportRenderService.cpp`
- `Engine/Source/Renderer/Frame/GameFrameRenderer.cpp`
- `Engine/Source/Renderer/Frame/EditorFrameRenderer.cpp`

여기서 프레임 단위 실행 순서를 봅니다.

### 3-3. 월드 수집과 씬 입력 조립

- `Engine/Source/Level/SceneRenderPacket.h`
- `Engine/Source/Level/ScenePacketBuilder.cpp`
- `Engine/Source/Renderer/Scene/SceneViewData.h`
- `Engine/Source/Renderer/Scene/Builders/SceneViewAssembler.cpp`
- `Engine/Source/Renderer/Scene/Builders/SceneCommandBuilder.cpp`
- `Engine/Source/Renderer/Scene/Builders/SceneCommandLightingBuilder.cpp`

여기서 월드 정보가 실제 렌더 입력으로 바뀌는 과정을 봅니다.

### 3-4. 씬 패스와 메시 드로우

- `Engine/Source/Renderer/Scene/SceneRenderer.cpp`
- `Engine/Source/Renderer/Scene/Pipeline/ScenePipelineBuilder.cpp`
- `Engine/Source/Renderer/Scene/Passes/ScenePasses.h`
- `Engine/Source/Renderer/Scene/MeshPassProcessor.cpp`

여기서 장면 패스 순서와 실제 드로우 실행을 봅니다.

### 3-5. 타깃 관리, 합성, Screen UI

- `Engine/Source/Renderer/Common/SceneRenderTargets.h`
- `Engine/Source/Renderer/Frame/SceneTargetManager.cpp`
- `Engine/Source/Renderer/Frame/Viewport/ViewportCompositor.cpp`
- `Engine/Source/Renderer/Frame/UI/FramePasses.cpp`
- `Engine/Source/Renderer/UI/Screen/ScreenUIRenderer.cpp`
- `Engine/Source/Renderer/UI/Screen/ScreenUIPassBuilder.cpp`
- `Engine/Source/Renderer/UI/Screen/UIDrawList.h`
- `Editor/Source/Slate/Widget/Painter.cpp`
- `Editor/Source/Slate/SlateApplication.cpp`

여기서 뷰포트 결과가 화면에 붙는 단계와 Screen UI 단계를 봅니다.

---

## 4. 가장 먼저 머릿속에 넣어야 할 핵심 데이터

렌더러를 이해할 때는 아래 네 가지를 먼저 구분하시면 됩니다.

### 4-1. `FSceneRenderPacket`

파일: `Engine/Source/Level/SceneRenderPacket.h`

이 구조는 **월드에서 수집한 렌더 대상 목록**입니다.

주요 내용은 아래와 같습니다.

- 메시 프리미티브
- 텍스트 프리미티브
- SubUV 프리미티브
- 빌보드 프리미티브
- 포그 프리미티브
- 데칼 / 메시 데칼 프리미티브
- 파이어볼 프리미티브
- FXAA 적용 여부

중요한 점은 이것이 아직 **GPU 드로우 명령 목록은 아니라는 것**입니다.

또 한 가지 중요한 점은, 현재 코드는 아래처럼 입력을 나눠서 채웁니다.

- `FScenePacketBuilder`:
  메시, 텍스트, SubUV, 빌보드, 데칼, 메시 데칼 같은 가시 프리미티브 수집
- `IViewportClient::BuildSceneRenderPacket()`:
  포그, 파이어볼, FXAA 플래그 추가

구조체에는 조명 배열도 있지만, **현재 조명 입력은 이 패킷을 주 소스로 쓰지 않습니다.** 실제 조명은 `FSceneCommandLightingBuilder`가 `World->GetAllActors()`를 직접 훑어서 만듭니다.

### 4-2. `FMeshBatch`

파일: `Engine/Source/Renderer/Mesh/MeshBatch.h`

이 구조는 실제 드로우에 훨씬 가까운 단위입니다.

들어 있는 것은 다음과 같습니다.

- 어떤 메시를 그릴지
- 어떤 머티리얼을 쓸지
- 월드 행렬이 무엇인지
- 어떤 패스에서 그릴지 (`PassMask`)
- 어떤 도메인인지 (`Domain`)
- 깊이 테스트, 깊이 쓰기, 컬링 정책
- 투명 정렬용 거리
- 제출 순서

즉 `FMeshBatch`는 **실제 패스가 소비하는 장면 드로우 단위**라고 생각하시면 됩니다.

### 4-3. `FSceneViewData`

파일: `Engine/Source/Renderer/Scene/SceneViewData.h`

이 구조는 **한 뷰의 실제 렌더 입력 전체**입니다.

안에는 크게 네 묶음이 있습니다.

- `MeshInputs`: 메시 배치 목록
- `LightingInputs`: ambient, local light, directional light, object light list
- `PostProcessInputs`: 포그, 데칼, 메시 데칼, 아웃라인, 파이어볼, FXAA 같은 후반 입력
- `DebugInputs`: 디버그 라인과 월드 포인터

즉 `FSceneViewData`는 “한 카메라 시점의 씬을 렌더하기 위한 완성본”입니다.

추가로 기억할 점은 아래와 같습니다.

- 에디터의 그리드, 월드축, 기즈모 같은 보조 메시도 최종적으로는 `MeshInputs.Batches`에 합쳐집니다.
- 아웃라인 입력과 FXAA 여부는 `PostProcessInputs` 안에 들어갑니다.

### 4-4. `FSceneRenderTargets`

파일: `Engine/Source/Renderer/Common/SceneRenderTargets.h`

이 구조는 한 씬 렌더링이 사용하는 타깃 묶음입니다.

핵심 필드는 아래와 같습니다.

- `FinalSceneColor`
- `SceneColorRead`
- `SceneColorWrite`
- `OverlayColor`
- `SceneDepth`
- `GBufferA/B/C`
- `OutlineMask`

현재 구조에서 가장 중요한 점은 **SceneColor가 ping-pong 구조**라는 것입니다.

- 씬 패스는 주로 `SceneColorRead`를 현재 출력으로 씁니다.
- 톤매핑과 후반 resolve 단계는 `SceneColorWrite`를 쓰고 필요할 때 `SwapSceneColor()`를 호출합니다.
- 에디터에서는 `FinalSceneColor`가 외부 뷰포트 타깃을 가리키고, 게임에서는 `FinalSceneColor`가 비어 있을 수 있습니다.

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
10. 데칼 텍스처 배열을 준비하고 디버그 입력을 붙입니다.
11. `FSceneRenderer::RenderSceneView()`가 씬 패스와 resolve 단계를 실행합니다.
12. 마지막에 `FViewportCompositePass`가 SceneColor 결과를 백버퍼에 붙입니다.

텍스트로 그리면 아래와 같습니다.

```text
FGameViewportClient
 -> FGameFrameRequest
 -> FRenderer::RenderGameFrame
 -> FGameFrameRenderer
 -> AcquireGameSceneTargets
 -> FSceneRenderer::BuildSceneViewData
 -> FSceneRenderer::RenderSceneView
 -> FViewportCompositePass
```

게임 프레임에서 기억할 점은 아래와 같습니다.

- 게임은 기본적으로 하나의 씬 뷰를 렌더합니다.
- 게임용 `FSceneRenderTargets`는 내부 HDR SceneColor를 쓰고, 마지막 백버퍼 복사는 프레임 합성 단계에서 처리합니다.

### 5-2. 에디터 프레임 흐름

에디터 쪽은 `FEditorViewportRenderService::RenderAll()`이 핵심입니다.

파일:
- `Editor/Source/Viewport/Services/EditorViewportRenderService.cpp`
- `Engine/Source/Renderer/Frame/EditorFrameRenderer.cpp`

흐름은 아래와 같습니다.

1. 활성 에디터 뷰포트 엔트리들을 순회합니다.
2. 각 뷰포트의 RTV / DSV / SRV를 확인합니다.
3. 각 뷰포트의 View / Projection / Frustum을 계산합니다.
4. 각 뷰포트마다 `FSceneRenderPacket`을 만듭니다.
5. 기즈모, 그리드, 월드축은 `AdditionalMeshBatches`로 따로 만듭니다.
6. 선택 아웃라인이 필요하면 `OutlineRequest`를 만듭니다.
7. 각 뷰포트 요청을 `FViewportScenePassRequest`로 묶습니다.
8. 동시에 최종 화면 배치를 위한 `CompositeItems`를 준비합니다.
9. `FSlateApplication::BuildDrawList()`로 `ScreenDrawList`를 만듭니다.
10. 이를 `FEditorFrameRequest`로 묶어 `FRenderer::RenderEditorFrame()`에 넘깁니다.
11. 내부에서 각 뷰포트 씬을 먼저 렌더합니다.
12. 각 뷰포트의 overlay SRV를 합성 정보에 연결합니다.
13. `FViewportCompositePass`가 여러 뷰포트 결과를 백버퍼에 붙입니다.
14. `FScreenUIPass`가 Screen UI를 최종 결과 위에 그립니다.

텍스트로 그리면 아래와 같습니다.

```text
FEditorViewportRenderService
 -> many FViewportScenePassRequest
 -> FEditorFrameRequest
 -> FRenderer::RenderEditorFrame
 -> per-viewport FSceneRenderer::RenderSceneView
 -> FViewportCompositePass
 -> FScreenUIPass
```

에디터 프레임에서 가장 중요한 점은 아래 두 가지입니다.

- 씬 렌더링은 뷰포트마다 따로 돌고, 화면 합성은 나중에 한 번 더 합니다.
- Screen UI는 씬 패스가 아니라 **뷰포트 합성 뒤**에 실행되는 별도 프레임 패스입니다.

---

## 6. 각 계층은 무엇을 담당하나요

## 6-1. `FRenderer`

파일: `Engine/Source/Renderer/Renderer.h`

이 클래스는 렌더러의 최상위 소유자입니다.

주요 역할은 아래와 같습니다.

- `FSceneRenderer` 소유
- `FViewportCompositor` 소유
- `FScreenUIRenderer` 소유
- `FSceneTargetManager` 소유
- `FDecalTextureCache` 소유
- Text / SubUV / Billboard / Fog / Outline / Decal / VolumeDecal / FireBall / FXAA / Lighting / Bloom / DebugLine feature 소유
- 게임 프레임 진입점 제공
- 에디터 프레임 진입점 제공
- 최종 tone mapping / FXAA / final blit 처리

작업자가 기억해야 할 점은 아래와 같습니다.

- 전역 feature나 공용 후반 처리의 소유 위치는 대개 여기입니다.
- 씬 패스 실행이 끝난 뒤 출력 정리는 `FRenderer::ResolveSceneColorTargets()`에서 합니다.

## 6-2. `FGameFrameRenderer` / `FEditorFrameRenderer`

파일:
- `Engine/Source/Renderer/Frame/GameFrameRenderer.cpp`
- `Engine/Source/Renderer/Frame/EditorFrameRenderer.cpp`

이 둘은 **프레임 단위 실행 순서**를 담당합니다.

차이는 아래처럼 이해하시면 됩니다.

- `FGameFrameRenderer`:
  단일 씬 뷰 렌더 + 최종 화면 합성
- `FEditorFrameRenderer`:
  뷰포트별 씬 렌더 반복 + 최종 화면 합성 + Screen UI

새 프레임 후반 패스를 넣고 싶다면, 가장 먼저 여기서 순서를 판단하면 됩니다.

## 6-3. `FSceneRenderer`

파일: `Engine/Source/Renderer/Scene/SceneRenderer.cpp`

이 클래스는 **한 개의 씬 뷰를 실제로 렌더하는 중심 클래스**입니다.

주요 역할은 아래와 같습니다.

- `FSceneRenderPacket`을 `FSceneViewData`로 조립
- 렌더 모드에 따라 조명 모델 선택
- 필요하면 wireframe override 적용
- 기본 씬 파이프라인 구성
- 패스 순서대로 실행
- 마지막에 scene color resolve 수행

즉 “한 뷰를 어떤 패스 순서로 그리는가”의 중심은 여기입니다.

## 6-4. `FSceneCommandBuilder`와 하위 빌더들

파일:
- `Engine/Source/Renderer/Scene/Builders/SceneCommandBuilder.cpp`
- `SceneCommandMeshBuilder.cpp`
- `SceneCommandTextBuilder.cpp`
- `SceneCommandSpriteBuilder.cpp`
- `SceneCommandPostProcessBuilder.cpp`
- `SceneCommandLightingBuilder.cpp`

이 묶음은 **장면 패킷을 실제 렌더 입력으로 조립**합니다.

현재 조립 순서는 아래와 같습니다.

1. 메시 입력 생성
2. 조명 입력 생성
3. 텍스트 입력 생성
4. SubUV 입력 생성
5. 빌보드 입력 생성
6. 포그 입력 생성
7. 파이어볼 입력 생성
8. 데칼 입력 생성
9. 메시 데칼 입력 생성
10. 필요하면 object light list 생성

중요한 점은 아래와 같습니다.

- 메시, 텍스트, 스프라이트, 후반 효과 입력은 대부분 패킷에서 옵니다.
- 조명은 현재 패킷보다 `BuildContext.World`를 직접 스캔해서 만듭니다.
- `AdditionalMeshBatches`는 `SceneViewAssembler` 단계에서 마지막에 붙습니다.

## 6-5. `FMeshPassProcessor`

파일: `Engine/Source/Renderer/Scene/MeshPassProcessor.cpp`

이 클래스는 메시 배치를 패스별로 필터링하고 정렬해서 그립니다.

주요 역할은 아래와 같습니다.

- 프레임당 한 번 메시 버퍼 업로드
- `PassMask` 기준으로 패스별 배치 필터링
- 패스별 정렬 정책 적용
- 머티리얼과 렌더 상태 바인딩
- 불투명 / 메시 데칼 패스에서 조명 feature 연동
- Draw / DrawIndexed 실행

현재 정렬 규칙은 아래처럼 요약할 수 있습니다.

- `ForwardTransparent`: 카메라에서 먼 것부터
- `EditorGrid`, `EditorPrimitive`: 제출 순서 유지
- `DepthPrepass`, `GBuffer`, `ForwardOpaque`, `ForwardMeshDecal`: 상태와 셰이더 중심 정렬

## 6-6. `FSceneTargetManager`

파일: `Engine/Source/Renderer/Frame/SceneTargetManager.cpp`

이 클래스는 씬 렌더링에 필요한 오프스크린 타깃 묶음을 준비합니다.

현재 구조에서 중요한 점은 아래와 같습니다.

- 내부 SceneColor는 `R16G16B16A16_FLOAT` 두 장을 ping-pong으로 유지
- GBufferA / B / C, OverlayColor, OutlineMask도 별도 유지
- 게임은 내부 SceneColor + 게임용 깊이 타깃을 사용
- 에디터는 외부 뷰포트 색/깊이 타깃을 감싸고, 내부 SceneColor는 그대로 유지
- 에디터 overlay는 뷰포트별 외부 타깃 조합 기준으로 따로 캐시

새 렌더 타깃이 필요하면 거의 항상 여기까지 수정해야 합니다.

## 6-7. `FViewportCompositor`

파일: `Engine/Source/Renderer/Frame/Viewport/ViewportCompositor.cpp`

이 클래스는 최종 백버퍼에 뷰포트 결과를 붙입니다.

주요 역할은 아래와 같습니다.

- 합성할 뷰포트 사각형 설정
- source SRV 선택
- full-screen triangle로 복사
- 필요하면 overlay SRV를 알파 블렌드로 덧그리기

`EViewportCompositeMode`에는 여러 시각화 모드 슬롯이 정의돼 있지만, 현재 코드 경로에서 실제로 적극 사용되는 것은 `SceneColor`와 `DepthView`입니다.

## 6-8. `FScreenUIRenderer`와 Slate 기록 단계

파일:
- `Engine/Source/Renderer/UI/Screen/ScreenUIRenderer.cpp`
- `Engine/Source/Renderer/UI/Screen/ScreenUIPassBuilder.cpp`
- `Engine/Source/Renderer/UI/Screen/UIDrawList.h`
- `Editor/Source/Slate/Widget/Painter.cpp`
- `Editor/Source/Slate/SlateApplication.cpp`

이쪽은 두 단계로 나뉩니다.

### Slate 기록 단계

- `Painter`가 `FilledRect`, `RectOutline`, `Text`를 `FUIDrawList`에 기록합니다.
- `SlateApplication`은 위젯 트리를 순회해 draw list를 만듭니다.

### GPU 렌더 단계

- `FScreenUIPassBuilder`가 draw list를 화면 공간 메시로 바꿉니다.
- UI는 직교 투영과 depth off 상태로 그려집니다.
- 색은 display-space 기준으로 처리됩니다.

즉 Screen UI는 씬 패스 일부가 아니라 **최종 화면에 덧그리는 별도 레이어**입니다.

---

## 7. 현재 씬 패스 순서

현재 기본 씬 패스 순서는 아래 파일에서 정의됩니다.

파일: `Engine/Source/Renderer/Scene/Pipeline/ScenePipelineBuilder.cpp`

현재 순서는 아래와 같습니다.

1. `FClearSceneTargetsPass`
2. `FUploadMeshBuffersPass`
3. `FDepthPrepass`
4. `FLightCullingComputePass`
5. `FForwardOpaquePass`
6. `FMeshDecalPass`
7. `FDecalCompositePass`
8. `FFogPostPass`
9. `FFireBallPass`
10. `FForwardTransparentPass`
11. `FBloomPass`
12. `FEditorGridPass`
13. `FOutlineMaskPass`
14. `FOutlineCompositePass`
15. `FEditorLinePass`
16. `FEditorPrimitivePass`

초심자 기준으로 각 패스를 짧게 설명하면 아래와 같습니다.

### 7-1. `FClearSceneTargetsPass`

- SceneColor, Depth, GBuffer, OutlineMask를 초기화합니다.

### 7-2. `FUploadMeshBuffersPass`

- 이번 프레임에 필요한 메시 버퍼를 GPU에 올립니다.

### 7-3. `FDepthPrepass`

- 깊이만 먼저 채웁니다.

### 7-4. `FLightCullingComputePass`

- 조명 계산용 culling 데이터를 준비합니다.

### 7-5. `FForwardOpaquePass`

- 불투명 메시를 그립니다.

### 7-6. `FMeshDecalPass`

- 메시 데칼 수신자와 메시 데칼 드로우를 처리합니다.

### 7-7. `FDecalCompositePass`

- 일반 데칼 결과를 SceneColor에 합성합니다.

### 7-8. `FFogPostPass`

- 포그를 장면 결과 위에 적용합니다.

### 7-9. `FFireBallPass`

- 파이어볼 후반 효과를 적용합니다.

### 7-10. `FForwardTransparentPass`

- 투명 오브젝트를 뒤에서 앞으로 정렬해서 그립니다.

### 7-11. `FBloomPass`

- bloom 관련 후반 처리를 수행합니다.

### 7-12. `FEditorGridPass`

- 에디터 그리드를 그립니다.

### 7-13. `FOutlineMaskPass` / `FOutlineCompositePass`

- 선택 아웃라인용 마스크를 만들고
- 그 마스크를 장면 위에 합성합니다.

### 7-14. `FEditorLinePass`

- 디버그 라인과 에디터 라인 오버레이를 그립니다.

### 7-15. `FEditorPrimitivePass`

- 월드축, 기즈모 같은 에디터 보조 프리미티브를 그립니다.

추가로 꼭 기억해야 할 점은 아래와 같습니다.

- `FGBufferPass` 코드는 존재하지만, 현재 기본 파이프라인에서는 실행하지 않습니다.
- 현재 기본 파이프라인은 **forward 중심 구조**입니다.

### 7-16. 씬 패스 뒤의 출력 정리 단계

씬 패스가 끝나면 `FRenderer::ResolveSceneColorTargets()`가 아래 순서로 실행됩니다.

1. tone mapping + `LinearToSRGB`
2. `SceneColorWrite`로 출력
3. `SwapSceneColor()`
4. 옵션이면 FXAA 수행
5. `FinalSceneColor`가 있을 때만 최종 blit 수행

즉 FXAA와 final blit은 `ScenePipelineBuilder.cpp` 안이 아니라 **씬 패스 실행 이후의 resolve 단계**에 있습니다.

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

- 게임에서는 씬 결과를 전체 화면에 붙입니다.
- 에디터에서는 여러 뷰포트 결과를 백버퍼에 배치합니다.
- overlay SRV가 있으면 SceneColor 위에 알파 블렌드로 덧그립니다.

### `FScreenUIPass`

- `FUIDrawList`를 기반으로 Screen UI를 최종 화면 위에 그립니다.

즉 **씬 패스**와 **프레임 후반 패스**는 완전히 다른 계층입니다.

판단 기준은 아래처럼 잡으시면 됩니다.

- 개별 씬 뷰 안에서 돌아야 하면 씬 패스
- 최종 화면이나 백버퍼 기준으로 돌아야 하면 프레임 패스

---

## 9. “무엇을 하려면 어디를 수정하나요?”

실무에서는 이 섹션이 가장 중요합니다.

## 9-1. 새 월드 프리미티브 타입을 추가하고 싶을 때

예시:
- `ULaserBeamComponent`
- `UWorldMarkerComponent`
- `UHeatDistortionComponent`

수정 순서는 보통 아래와 같습니다.

1. `SceneRenderPacket.h`에 새 프리미티브 구조와 배열을 추가합니다.
2. `ScenePacketBuilder.cpp`에서 타입 판별과 ShowFlag 처리를 추가합니다.
3. 프러스텀 외 별도 수집이 필요하면 `ViewportClient.cpp`나 `EditorViewportRenderService.cpp`를 수정합니다.
4. `SceneCommandBuilder` 하위 빌더에서 `FSceneViewData` 입력으로 변환합니다.
5. 메시인지 후반 효과인지에 따라 소비 패스를 연결합니다.

판단 기준은 간단합니다.

- 메시처럼 그리면 `FMeshBatch`
- 풀스크린이나 후반 효과면 `PostProcessInputs`

## 9-2. 기존 메시를 다른 패스로 보내고 싶을 때

예시:
- 어떤 메시를 투명 패스로만 보내기
- 에디터 전용 보조 메시로 보내기
- 새 메시 패스를 추가하기

주로 수정할 곳은 아래와 같습니다.

- `Renderer/Mesh/MeshBatch.h`
- `SceneCommandMeshBuilder.cpp` 또는 관련 빌더
- `Scene/MeshPassProcessor.cpp`
- `Scene/Pipeline/ScenePipelineBuilder.cpp`

핵심은 아래 세 가지입니다.

- `PassMask`를 무엇으로 줄 것인가
- 새 `EMeshPassType`가 필요한가
- 정렬 규칙을 어떻게 할 것인가

## 9-3. 새 후반 효과나 풀스크린 패스를 추가하고 싶을 때

예시:
- SSAO
- custom highlight
- scene color distortion
- 추가 bloom 단계

수정 순서는 보통 아래와 같습니다.

1. `SceneViewData.h`의 `FScenePostProcessInputs`에 필요한 입력을 추가합니다.
2. 필요하면 `SceneRenderPacket.h`와 수집 코드를 수정합니다.
3. `SceneCommandPostProcessBuilder.cpp`에서 입력을 조립합니다.
4. 새 패스를 구현합니다.
5. `ScenePipelineBuilder.cpp`에 순서를 추가합니다.
6. 새 텍스처가 필요하면 `SceneRenderTargets.h`와 `SceneTargetManager.cpp`를 수정합니다.

## 9-4. 새 렌더 타깃이 필요할 때

예시:
- 추가 mask 텍스처
- blur ping-pong 텍스처
- custom accumulation 텍스처

수정할 곳은 아래와 같습니다.

1. `SceneRenderTargets.h`에 필드를 추가합니다.
2. `SceneTargetManager.cpp`에 생성과 해제 로직을 추가합니다.
3. `AcquireGameSceneTargets()`와 `WrapExternalSceneTargets()`에서 연결합니다.
4. 새 패스가 SRV / RTV / UAV / DSV 중 무엇을 쓰는지 맞춥니다.

새 타깃 작업은 구조체와 매니저를 항상 같이 수정해야 안전합니다.

## 9-5. 에디터 전용 보조 메시를 추가하고 싶을 때

예시:
- 그리드 변형
- 선택 보조 메시
- 기즈모 확장

주로 수정할 곳:

- `Editor/Source/Viewport/Services/EditorViewportRenderService.cpp`

이 파일이 `AdditionalMeshBatches`를 만드는 위치이므로, 에디터 월드 보조 드로우는 여기서 시작하는 경우가 많습니다.

## 9-6. 최종 화면 합성 단계를 추가하고 싶을 때

예시:
- 뷰포트 합성 뒤 전체 화면 디버그 시각화
- Screen UI 전에 들어가는 전체 화면 색보정

이 경우는 씬 패스가 아니라 **프레임 패스**입니다.

수정할 곳은 아래와 같습니다.

1. `FramePassContext.h`에 필요한 입력을 추가합니다.
2. `FramePasses.h/cpp`에 새 `IFrameRenderPass` 구현을 추가합니다.
3. `GameFrameRenderer.cpp` 또는 `EditorFrameRenderer.cpp`에서 실행 순서를 조정합니다.

## 9-7. Screen UI 요소 타입을 추가하고 싶을 때

예시:
- 이미지
- 선
- 아이콘
- 둥근 사각형

수정 위치는 두 갈래입니다.

- 기록 단계:
  `UIDrawList.h`, `Painter.cpp`, 필요하면 `SlateApplication.cpp`
- GPU 변환 단계:
  `ScreenUIPassBuilder.cpp`, 필요하면 `ScreenUIBatchRenderer.cpp`

기억할 점은 Screen UI가 **씬 패스가 아니라는 것**입니다. SceneColor나 depth를 직접 만지는 기능이라면 Screen UI가 아니라 다른 계층일 가능성이 큽니다.

## 9-8. 뷰포트 시각화 모드를 늘리고 싶을 때

예시:
- Normal 보기
- GBuffer 보기
- OutlineMask 보기

주로 수정할 곳은 아래와 같습니다.

- `Frame/Viewport/ViewportCompositor.h`
- `Frame/Viewport/ViewportCompositor.cpp`
- `EditorViewportRenderService.cpp`

현재 enum에는 여러 모드가 정의돼 있지만, compositor의 실제 분기와 셰이더 연결은 필요한 만큼 직접 확장해야 합니다.

---

## 10. 작업 전에 먼저 판단해야 할 기준

새 기능을 넣기 전에 아래 질문을 먼저 정리하면 수정 범위를 빠르게 줄일 수 있습니다.

1. 이 기능은 월드 오브젝트인가, 화면 UI인가?
2. 이 기능은 메시 드로우인가, 풀스크린 후반 효과인가?
3. 이 기능은 개별 씬 뷰 안에서 돌아야 하는가, 최종 화면에서 돌아야 하는가?
4. 이 기능은 새 타깃이 필요한가, 기존 SceneColor / Depth / Overlay로 충분한가?

대부분의 경우 판단은 아래처럼 귀결됩니다.

- 월드에서 수집되는 렌더 대상이면 `FSceneRenderPacket`부터 시작
- 실제 드로우 메시라면 `FMeshBatch`와 `FMeshPassProcessor`까지 연결
- 후반 효과라면 `FScenePostProcessInputs`와 씬 패스로 연결
- 최종 화면 작업이라면 프레임 패스로 연결
- 에디터 화면 UI라면 `FUIDrawList`와 `FScreenUIRenderer`로 연결

이 기준만 분명하면, 현재 렌더러 구조에서는 수정 위치를 꽤 빠르게 좁힐 수 있습니다.
