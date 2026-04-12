# 렌더러 구조 안내서

## 1. 이 문서는 무엇을 설명하나

이 문서는 현재 코드베이스의 실제 렌더러 구조를 코드 기준으로 설명한다.

예전 문서에는 이미 제거된 개념이 섞여 있었다.
대표적으로:

1. `FRenderCommandQueue`
2. callback 기반 outline/debug/UI 연결
3. `FSceneRenderer`가 버킷형 command queue를 직접 실행한다는 설명

현재 구조의 핵심은 다르다.

1. 월드 수집은 `FSceneRenderPacket`에서 멈춘다.
2. 렌더러는 `FSceneViewData`를 만든 뒤 scene/frame pass pipeline으로 실행한다.
3. debug/UI/outline/decal/fog는 각각 명시적 pass input 또는 feature request로 편입된다.

## 2. 한 줄 요약

현재 렌더러는 아래처럼 이해하면 된다.

`FRenderer`가 프레임을 조립하고, `FSceneRenderer`가 scene pipeline을 실행하고, `FViewportCompositor`와 `FScreenUIRenderer`가 frame pipeline을 마무리한다.

즉 지금 구조는 "거대한 단일 렌더러"가 아니라:

1. 프레임 오케스트레이션
2. scene view 데이터 준비
3. scene pass 실행
4. frame pass 실행

으로 나뉜 pass 기반 구조다.

## 3. 가장 중요한 설계 원칙

현재 구조를 읽을 때는 아래 원칙을 기준으로 보면 된다.

1. 월드/에디터 수집 계층은 renderer feature 타입이나 D3D11 세부를 몰라야 한다.
2. `FSceneRenderPacket`은 "무엇이 보이는가"만 담고 "어떻게 그리는가"는 담지 않는다.
3. `FSceneViewData`는 실제 렌더 실행 직전의 frame-local 입력이다.
4. scene pass와 frame pass는 서로 다른 context를 쓰지만 같은 pass-sequence 철학을 공유한다.
5. UI와 debug는 더 이상 독립 미니 렌더러처럼 움직이지 않고 pass input으로 정리된다.

## 4. 큰 그림의 주요 객체

### 4-1. `FRenderer`

파일: `Engine/Source/Renderer/Renderer.h`

`FRenderer`는 여전히 렌더러의 진입점이지만, 직접 모든 드로우를 구현하지는 않는다.

현재 역할은 아래와 같다.

1. `FRenderDevice`와 공용 GPU 자원 owner
2. game/editor frame request 진입점
3. scene targets 준비
4. `FSceneRenderer` 호출
5. decal texture array 같은 frame preparation helper 실행
6. frame pipeline용 viewport composite / screen UI 실행 연결

즉 "프레임을 설명된 순서대로 돌리는 오케스트레이터"에 가깝다.

### 4-2. `FRenderDevice`

파일: `Engine/Source/Renderer/RenderDevice.h`

`FRenderDevice`는 D3D11 디바이스와 swap chain, 백버퍼, resize/present 책임을 맡는다.

핵심 역할:

1. device/context/swap chain 생성
2. backbuffer RTV/DSV 관리
3. viewport 관리
4. frame begin/end
5. resize / occlusion 처리

### 4-3. `FSceneRenderer`

파일: `Engine/Source/Renderer/SceneRenderer.h`

`FSceneRenderer`는 scene view 하나를 그리는 실행기다.

핵심 역할:

1. `FSceneRenderPacket` + 추가 배치 입력을 `FSceneViewData`로 변환
2. 와이어프레임 override 적용
3. scene pass pipeline 구성
4. pass 순서대로 실행

중요한 점은 `FSceneRenderer`가 더 이상 "render command queue executor"가 아니라는 점이다.
지금은 `FSceneViewData`와 `FMeshPassProcessor`를 중심으로 scene pipeline을 수행한다.

### 4-4. `FViewportCompositor`

파일: `Engine/Source/Renderer/ViewportCompositor.h`

`FViewportCompositor`는 scene 결과를 최종 render target에 배치하는 frame pass 성격의 컴포넌트다.

핵심 역할:

1. `FViewportCompositePassInputs` 해석
2. scene color / depth view 합성
3. scissor + fullscreen triangle 기반 viewport 배치

### 4-5. `FScreenUIRenderer`

파일: `Engine/Source/Renderer/ScreenUIRenderer.h`

`FScreenUIRenderer`는 화면 UI를 2단계로 처리한다.

1. `BuildPassInputs(...)`
2. `Render(...)`

즉 예전처럼 draw list를 바로 GPU에 그리는 흐름이 아니라:

1. `FUIDrawList`
2. `FScreenUIPassInputs`
3. frame pass 실행

순서로 분리된다.

### 4-6. feature 계층

파일: `Engine/Source/Renderer/Feature/*`

feature는 공통 mesh pass에 바로 녹지 않는 특수 경로를 담당한다.

현재 주요 feature:

1. `FTextRenderFeature`
2. `FSubUVRenderFeature`
3. `FBillboardRenderFeature`
4. `FFogRenderFeature`
5. `FOutlineRenderFeature`
6. `FDebugLineRenderFeature`
7. `FDecalRenderFeature`

이 중 fog / outline / decal / viewport composition은 공통 fullscreen pass 실행 모델 위에 올라가 있다.

## 5. scene 프런트엔드: 월드에서 packet까지

### 5-1. `FSceneRenderPacket`

파일: `Engine/Source/Level/SceneRenderPacket.h`

현재 `FSceneRenderPacket`은 renderer-neutral한 scene 설명 구조다.

포함하는 primitive 그룹:

1. `MeshPrimitives`
2. `TextPrimitives`
3. `SubUVPrimitives`
4. `BillboardPrimitives`
5. `FogPrimitives`
6. `DecalPrimitives`

즉 예전 문서처럼 메시/텍스트/SubUV만 있는 구조가 아니다.

### 5-2. `FScenePacketBuilder`

파일: `Engine/Source/Level/ScenePacketBuilder.h`

이 계층은 월드와 show flag, 가시성 정보를 바탕으로 packet만 채운다.

여기서 멈춰야 하는 이유는 명확하다.

1. 수집 계층은 GPU 타입을 몰라야 한다.
2. sorting/material binding/pass scheduling은 renderer 책임이다.
3. debug/UI/outline 같은 feature-specific 실행 모델과 분리되어야 한다.

## 6. packet에서 scene view 데이터로

### 6-1. `FSceneCommandBuilder`

파일: `Engine/Source/Renderer/SceneCommandBuilder.h`

`FSceneCommandBuilder`는 packet을 renderer가 바로 소비할 `FSceneViewData`로 확장한다.

현재 역할:

1. mesh primitive -> `FMeshBatch`
2. text primitive -> text mesh + material
3. subUV primitive -> sprite mesh + material
4. billboard primitive -> billboard mesh + material
5. fog primitive -> `FFogRenderItem`
6. decal primitive -> `FDecalRenderItem`

즉 "packet -> pass input" 변환기다.

### 6-2. `FSceneCommandResourceCache`

파일: `Engine/Source/Renderer/SceneCommandBuilder.h`

최근 구조에서 중요한 변화는 cache 책임 분리다.

이 클래스는 아래 캐시를 관리한다.

1. 텍스트 색상별 dynamic material
2. `USubUVComponent*` 기반 dynamic material

즉 `FSceneCommandBuilder`는 extraction 중심이고, resource/cache는 `FSceneCommandResourceCache`로 빠졌다.

## 7. `FSceneViewData`가 의미하는 것

파일: `Engine/Source/Renderer/SceneViewData.h`

`FSceneViewData`는 한 scene view를 그리기 직전의 frame-local 렌더 입력이다.

현재는 세 덩어리로 나뉜다.

1. `MeshInputs`
2. `PostProcessInputs`
3. `DebugInputs`

### 7-1. `MeshInputs`

`FMeshBatch` 배열을 가진다.
scene geometry와 추가 editor 배치가 여기로 모인다.

### 7-2. `PostProcessInputs`

후처리성 데이터가 들어간다.

1. fog items
2. decal items
3. decal texture array SRV
4. outline items
5. outline enabled flag

### 7-3. `DebugInputs`

현재는 `FDebugLinePassInputs`를 가진다.

중요한 점은 debug가 더 이상 engine 쪽에서 `FDebugLineRenderFeature`를 직접 호출하지 않는다는 점이다.
engine/debug 계층은 먼저 renderer-neutral primitive를 모으고, renderer가 그것을 debug line pass input으로 바꾼다.

## 8. scene pipeline

### 8-1. 공통 기반

파일:

1. `Engine/Source/Renderer/PassPipeline.h`
2. `Engine/Source/Renderer/RenderPipeline.h`
3. `Engine/Source/Renderer/PassContext.h`

scene pipeline은 `TPassPipeline<IRenderPass, FPassContext>` 위에서 동작한다.

`FPassContext`는 아래를 묶는다.

1. `FRenderer`
2. `FSceneRenderTargets`
3. `FSceneViewData`
4. clear color

### 8-2. 실제 scene pass 순서

파일: `Engine/Source/Renderer/SceneRenderer.cpp`

현재 pass 순서는 아래와 같다.

1. `FClearSceneTargetsPass`
2. `FUploadMeshBuffersPass`
3. `FDepthPrepass`
4. `FGBufferPass`
5. `FForwardOpaquePass`
6. `FDecalCompositePass`
7. `FForwardTransparentPass`
8. `FFogPostPass`
9. `FOutlineMaskPass`
10. `FOutlineCompositePass`
11. `FOverlayPass`
12. `FDebugLinePass`

즉 현재 렌더러는 명시적인 ordered pass list다.

### 8-3. `FMeshPassProcessor`

파일: `Engine/Source/Renderer/MeshPassProcessor.h`

mesh pass 공통 실행은 `FMeshPassProcessor`가 맡는다.

핵심 역할:

1. mesh buffer upload
2. pass별 필터링
3. pass별 정렬
4. material pass type 선택
5. draw submission

정리하면:

1. `FSceneCommandBuilder`는 batch를 준비하고
2. `FMeshPassProcessor`는 batch를 pass별로 실제 draw로 실행한다

## 9. frame pipeline

### 9-1. 공통 기반

파일:

1. `Engine/Source/Renderer/PassPipeline.h`
2. `Engine/Source/Renderer/FramePipeline.h`
3. `Engine/Source/Renderer/FramePassContext.h`

frame pipeline도 scene pipeline과 같은 `TPassPipeline`을 쓴다.

차이는 context다.

`FFramePassContext`는 아래를 담는다.

1. `FRenderer`
2. `FFrameContext`
3. `FViewContext`
4. 최종 render target/depth target
5. `FViewportCompositePassInputs`
6. `FScreenUIPassInputs`

즉 frame pipeline은 scene 결과를 최종 화면에 배치하는 단계다.

### 9-2. 실제 frame pass

파일: `Engine/Source/Renderer/FramePasses.cpp`

현재 frame pass는 아래 두 개다.

1. `FViewportCompositePass`
2. `FScreenUIPass`

게임 프레임은 보통 viewport composite까지만 사용하고,
에디터 프레임은 composite 뒤에 screen UI pass를 이어서 사용한다.

## 10. debug 경로

### 10-1. engine 쪽 수집

파일: `Engine/Source/Debug/DebugDrawManager.h`

`FDebugDrawManager`는 이제 renderer-neutral collector다.

현재는:

1. `FDebugLine`
2. `FDebugCube`
3. `FDebugPrimitiveList`

를 다루고, `BuildPrimitiveList(...)`에서 멈춘다.

즉 engine 계층은 `FDebugLineRenderFeature::*`를 더 이상 직접 호출하지 않는다.

### 10-2. renderer 쪽 변환

파일: `Engine/Source/Renderer/Renderer.cpp`

renderer 내부 helper가:

1. `FDebugPrimitiveList`
2. actor mesh BVH debug primitive
3. `FDebugLinePassInputs`

를 연결한다.

### 10-3. feature 실행

파일: `Engine/Source/Renderer/Feature/DebugLineRenderFeature.h`

`FDebugLineRenderFeature`는 이제 prepared input 소비자다.

현재 역할:

1. `FDebugLinePassInputs`의 line mesh 소비
2. material/state bind
3. scene color/depth 위 line draw

즉 feature-local "요청 축적기"가 아니라 pass executor 쪽에 가깝다.

## 11. UI 경로

### 11-1. 기록 단계

파일:

1. `Editor/Source/Slate/Widget/Painter.h`
2. `Engine/Source/Renderer/UIDrawList.h`

Slate 계층은 여전히 `FUIDrawList`를 기록한다.
여기서는 "무엇을 그릴지"만 정리한다.

### 11-2. pass input 준비

파일: `Engine/Source/Renderer/ScreenUIRenderer.h`

`FScreenUIRenderer::BuildPassInputs(...)`가 `FUIDrawList`를 `FScreenUIPassInputs`로 바꾼다.

여기서 하는 일:

1. orthographic view/projection 준비
2. filled rect / outline / text를 dynamic mesh로 변환
3. batching
4. font/color material 준비

즉 UI도 이제 "기록 -> prepared input -> pass 실행" 3단계다.

### 11-3. frame pass 실행

`FScreenUIPass`는 prepared input만 소비해서 최종 target 위에 UI를 그린다.

이 구조 덕분에:

1. frame pass context가 필요한 정보를 직접 갖고
2. UI 업로드와 draw 경계가 명확해지고
3. UI가 독립 렌더러처럼 보이지 않게 되었다

## 12. fullscreen pass 공통화

파일: `Engine/Source/Renderer/FullscreenPass.h`

fullscreen/post 계열은 공통 helper 위에 올라간다.

핵심 요소:

1. `FFullscreenPassShaderSet`
2. `FFullscreenPassPipelineState`
3. `FFullscreenPassBindings`
4. `ExecuteFullscreenPass(...)`

현재 이 모델을 쓰는 대표 경로:

1. fog
2. outline mask/composite
3. decal composite
4. viewport compositor

즉 raw D3D11 boilerplate가 feature마다 따로 흩어지지 않는다.

## 13. game frame 흐름

가장 단순한 흐름은 아래와 같다.

1. `ViewportClient`가 `FSceneRenderPacket`을 만든다.
2. `FRenderer::RenderGameFrame()`이 scene targets를 확보한다.
3. `BuildFrameContext` / `BuildViewContext`를 만든다.
4. `FSceneRenderer::BuildSceneViewData()`가 `FSceneViewData`를 만든다.
5. renderer가 decal texture array와 debug line pass input을 보강한다.
6. `FSceneRenderer::RenderSceneView()`가 scene pipeline을 실행한다.
7. `FViewportCompositePassInputs`를 만든다.
8. `FFrameRenderPipeline`이 viewport composite pass를 실행한다.
9. `FRenderDevice`가 present한다.

도식으로 보면:

```text
World
 -> ScenePacketBuilder
 -> FSceneRenderPacket
 -> FRenderer::RenderGameFrame
 -> FSceneViewData
 -> Scene Pass Pipeline
 -> Viewport Composite Pass
 -> Present
```

## 14. editor frame 흐름

에디터는 여러 scene view와 final frame composition이 있다.

흐름은 아래와 같다.

1. editor 서비스가 viewport별 `FViewportScenePassRequest`를 만든다.
2. 각 viewport request는:
   - `FSceneRenderPacket`
   - `FOutlineRenderRequest`
   - `FDebugSceneBuildInputs`
   - 추가 mesh batch
   를 담는다.
3. `FRenderer::RenderEditorFrame()`이 viewport별로 scene pipeline을 실행한다.
4. 모든 viewport scene 결과가 준비되면 `FViewportCompositePassInputs`를 만든다.
5. `FScreenUIRenderer::BuildPassInputs()`로 최종 screen UI pass input을 만든다.
6. `FFrameRenderPipeline`이:
   - `FViewportCompositePass`
   - `FScreenUIPass`
   순서로 실행한다.
7. present한다.

도식:

```text
EditorViewportRenderService
 -> ViewportScenePassRequest[]
 -> per-viewport Scene Pass Pipeline
 -> ViewportCompositePassInputs
 -> ScreenUIPassInputs
 -> Frame Pass Pipeline
 -> Present
```

## 15. `FRenderer`가 아직 소유하는 것

현재 `FRenderer`는 여전히 많은 영구 owner다.
다만 예전처럼 모든 실행 구현을 품고 있지는 않다.

현재 permanent owner에 가까운 것들:

1. `FRenderDevice`
2. `FRenderStateManager`
3. default material / default texture material / samplers
4. text, subUV, billboard, fog, outline, debug line, decal feature
5. `FViewportCompositor`
6. `FScreenUIRenderer`
7. scene core targets
8. supplemental targets

### 15-1. target ownership

현재 targets는 크게 두 그룹으로 볼 수 있다.

1. scene core targets
   - scene color
   - scene depth
2. supplemental targets
   - GBuffer A/B/C
   - scene color scratch
   - outline mask

현재는 여전히 `FRenderer`가 소유하지만, 구조적으로는 이미 구분된 상태다.

## 16. 예전 문서와 달라진 점

이번 갱신에서 특히 바로잡은 내용은 아래와 같다.

1. `FRenderCommandQueue` / `RenderCommand.h` 기반 설명 제거
2. `FSceneRenderer`가 command bucket을 직접 실행한다는 설명 제거
3. `FSceneRenderPacket` primitive 종류를 최신 코드 기준으로 확장
4. `FSceneViewData`의 `MeshInputs` / `PostProcessInputs` / `DebugInputs` 구조 반영
5. debug path의 `FDebugPrimitiveList -> FDebugLinePassInputs` 흐름 반영
6. UI path의 `FUIDrawList -> FScreenUIPassInputs -> FScreenUIPass` 흐름 반영
7. scene/frame pipeline의 공통 `TPassPipeline` 기반 반영
8. fullscreen pass 공통 executor 반영

## 17. 새 기능을 넣을 때 어디를 수정해야 하나

기능 종류별로 보는 게 가장 빠르다.

### 17-1. 새 scene primitive

예: 새 world-space component

보통 순서는 아래다.

1. `FSceneRenderPacket`에 primitive bucket 추가
2. `FScenePacketBuilder`에 수집 추가
3. `FSceneCommandBuilder`에 `FSceneViewData` 변환 추가
4. mesh pass로 처리 가능하면 `FMeshBatch`로 편입
5. 별도 GPU 경로가 필요하면 feature 추가

### 17-2. 새 post/fullscreen effect

예: 색보정, 디버그 depth overlay, custom composite

보통 순서는 아래다.

1. 필요한 view/post input을 `FSceneViewData` 또는 frame pass input에 추가
2. feature 또는 pass 클래스 추가
3. 가능하면 `ExecuteFullscreenPass(...)` 재사용
4. `SceneRenderer::BuildRenderPipeline()` 또는 frame pipeline에 pass 삽입

### 17-3. 새 screen UI 요소

보통 순서는 아래다.

1. `FUIDrawList` element 타입 추가
2. Slate/Painter 기록 단계 추가
3. `FScreenUIRenderer::BuildPassInputs(...)` 변환 추가

## 18. 디버깅은 어디부터 보면 되나

문제가 생기면 아래 순서가 가장 빠르다.

1. `ScenePacketBuilder`가 packet을 제대로 만들었는가
2. `SceneCommandBuilder`가 `FSceneViewData`를 제대로 채웠는가
3. `SceneViewData`의 어느 input 그룹에 있어야 하는 데이터인지 맞는가
4. scene/frame pipeline에 해당 pass가 실제로 들어갔는가
5. feature request 또는 pass input이 비어 있지 않은가
6. compositor 또는 screen UI pass에서 최종 target으로 올라갔는가

즉 지금 구조는:

`수집 -> scene view 데이터 준비 -> pass 실행 -> frame 합성`

순서로 좁혀가면 된다.

## 19. 함께 봐야 할 문서

더 짧은 구조 변경 메모는 아래 문서를 같이 보면 된다.

파일: `docs/RendererRefactorNotes.md`

이 문서는 설계 판단과 ownership 메모 중심이고,
현재 문서는 실제 코드 구조 walkthrough 중심이다.
