# 코드베이스 구조 정리

## 목적

이 문서는 현재 엔진의 디렉토리 구조와 주요 클래스 계층을 빠르게 파악하기 위한 온보딩 메모다.  
특히 5주차+ 과제인 PIE(Play In Editor) 구현 관점에서, `FEngine -> UWorld/UScene -> AActor/UActorComponent -> Viewport/Editor` 흐름을 중심으로 정리했다.

---

## 1. 최상위 디렉토리 구조

```text
jungle-gtl-week05+
├─ .codex/      Codex 작업 메모, 과제 정리, 구조 문서
├─ Assets/      런타임 에셋 원본/직렬화 데이터
├─ Client/      게임 실행용 엔트리 프로젝트
├─ Content/     에디터 UI용 폰트/텍스처 같은 리소스
├─ docs/        Doxygen 기반으로 생성된 문서 산출물
├─ Editor/      에디터 전용 코드
├─ Engine/      공용 런타임 엔진 코드
├─ ObjViewer/   메시/머티리얼 확인용 별도 툴 프로젝트
└─ Scripts/     프로젝트 파일 생성 등 보조 스크립트
```

### 디렉토리별 역할

- `.codex/`
  - 과제 설명, 에이전트 지침, 조사 문서 등을 모아두는 작업 디렉토리다.
- `Assets/`
  - `Scenes/`, `Meshes/`, `Materials/`, `Textures/`처럼 실제 엔진이 읽는 에셋 데이터를 둔다.
  - `FSceneSerializer`, `FObjManager`, `FMaterialManager`가 주로 이 디렉토리와 연결된다.
- `Client/`
  - 게임 모드 실행용 진입점이다.
  - `main.cpp`에서 게임 엔진을 부팅하는 최소 래퍼 역할을 한다.
- `Content/`
  - 에디터 UI에서 쓰는 폰트와 아이콘성 리소스를 둔다.
  - `FEditorUI`, Slate 계층, Content Browser 쪽이 주로 사용한다.
- `docs/`
  - 자동 생성된 API 문서, 그래프 이미지, latex/html 산출물이다.
  - 코드 수정 대상이라기보다 참고 자료 성격이 강하다.
- `Editor/`
  - 에디터 엔진, 에디터 뷰포트, Gizmo, Picking, Slate UI, Selection/Camera 서브시스템이 있다.
- `Engine/`
  - 런타임 핵심. 오브젝트 시스템, 월드/씬, 액터-컴포넌트, 렌더러, 입력, 수학, 물리, 직렬화가 모여 있다.
- `ObjViewer/`
  - 단독 뷰어 성격의 보조 프로그램이다.
  - 엔진 공용 모듈을 재사용해 메시/머티리얼을 확인하는 도구로 보인다.
- `Scripts/`
  - `GenerateProjectFiles.py` 같은 빌드/프로젝트 생성 보조 스크립트와 임베디드 파이썬 런타임이 있다.

---

## 2. 소스 디렉토리 구조

### 2.1 `Engine/Source`

```text
Engine/Source
├─ Actor/       월드에 배치되는 액터 클래스
├─ Asset/       메시/모델 파일 로더
├─ Camera/      카메라 데이터와 뷰/투영 계산
├─ Component/   액터에 부착되는 컴포넌트 계층
├─ Core/        엔진 루프, 뷰포트 클라이언트, 타이머, show flags
├─ Debug/       로그, 디버그 드로우
├─ Input/       Raw Input + Enhanced Input
├─ Math/        벡터/행렬/쿼터니언/프러스텀/트랜스폼
├─ Memory/      메모리 베이스 유틸리티
├─ Object/      UObject RTTI, 팩토리, 오브젝트 매니저
├─ Physics/     간단한 라인트레이스 중심 물리 유틸리티
├─ Platform/    Windows 앱/윈도우/런처
├─ Primitive/   기즈모용 절차적 메시 생성
├─ Renderer/    D3D11 렌더러, 셰이더, 머티리얼, 메시 버퍼
├─ Scene/       씬, 컬링, BVH, 렌더 수집
├─ Serializer/  씬 저장/로드 아카이브
├─ Types/       커스텀 컨테이너/문자열/이름/스마트 포인터
└─ World/       월드와 월드 컨텍스트
```

### 2.2 `Editor/Source`

```text
Editor/Source
├─ Axis/        축 렌더러
├─ Controller/  에디터 카메라 조작 컨트롤러
├─ Gizmo/       이동/회전/스케일 기즈모
├─ Pawn/        에디터 카메라 폰
├─ Picking/     레이캐스트 기반 액터 선택
├─ Slate/       커스텀 경량 UI 프레임워크
├─ Subsystem/   Selection, Editor Camera 같은 상태성 서브시스템
├─ UI/          아웃라이너, 프로퍼티, 콘솔, 콘텐츠 브라우저 등 에디터 창
└─ Viewport/    에디터 뷰포트, 레지스트리, 렌더/입력/에셋 상호작용 서비스
```

### 2.3 그 외 프로젝트 디렉토리

- `Client/Source`
  - 게임 실행용 `main.cpp`만 있는 얇은 엔트리 포인트다.
- `ObjViewer/Source`
  - `ObjViewerEngine`, `ObjViewerViewportClient`, `ObjViewerShell`로 이루어진 별도 미리보기 툴이다.

---

## 3. 런타임 핵심 구조

### 3.1 큰 흐름

```text
FEngine
 ├─ FRenderer
 ├─ FInputManager / FEnhancedInputManager
 ├─ FPhysicsManager
 ├─ FWorldContext[]
 │   └─ UWorld
 │       ├─ PersistentLevel(UScene)
 │       ├─ StreamingLevels(UScene[])
 │       └─ ActiveCameraComponent(UCameraComponent)
 └─ IViewportClient
```

### 3.2 구조 해설

- `FEngine`
  - 런타임의 최상위 오케스트레이터다.
  - `Initialize`, `Tick`, `Shutdown`에서 입력, 물리, 월드 진행, 렌더링 순서를 관리한다.
  - 여러 `FWorldContext`를 들고 있어서 게임/에디터/프리뷰/PIE 같은 월드 분리가 가능한 구조다.
- `FWorldContext`
  - `ContextName`, `WorldType`, `UWorld*`를 묶는 얇은 핸들이다.
  - 엔진이 여러 월드를 동시에 관리할 수 있게 해주는 슬롯 역할이다.
- `UWorld`
  - 월드 단위 컨테이너다.
  - `PersistentLevel`과 `StreamingLevels`를 소유하고, 활성 카메라와 월드 시간도 관리한다.
  - 이미 `DuplicateWorldForPIE` 선언이 있어서 PIE 확장 지점이 분명하다.
- `UScene`
  - 현재 구조에서는 사실상 레벨/씬 역할을 겸한다.
  - 액터 등록, 파괴 예약, `BeginPlay`, `Tick`, BVH 기반 공간 질의를 맡는다.

---

## 4. 클래스 계층도

## 4.1 오브젝트/월드/씬 계층

```text
UObject
├─ UWorld
├─ UScene
├─ UStaticMesh
├─ AActor
└─ UActorComponent
```

### 역할

- `UObject`
  - 엔진 공통 베이스 클래스다.
  - RTTI, `Outer` 체인, 이름, UUID, 플래그, 복제 훅(`Duplicate`, `DuplicateSubObjects`)을 제공한다.
- `UWorld`
  - 씬 묶음과 활성 카메라를 가진 월드 루트다.
  - PIE 시점에는 Editor World와 분리된 런타임 월드를 담는 대상이 된다.
- `UScene`
  - 액터 집합과 공간 가속 구조를 가진 레벨 컨테이너다.
- `UStaticMesh`
  - 렌더용 정적 메시 데이터와 기본 머티리얼, 메시 BVH를 가진 에셋 오브젝트다.
- `AActor`
  - 월드에 배치되는 엔티티의 베이스다.
  - 루트 컴포넌트와 Owned Component 집합을 가진다.
- `UActorComponent`
  - 액터 기능 조각의 베이스다.
  - 등록 상태, BeginPlay 상태, Tick 여부, Owner 연결을 관리한다.

## 4.2 액터 계층

```text
UObject
└─ AActor
   ├─ ACubeActor
   ├─ APlaneActor
   ├─ ASphereActor
   ├─ ASkySphereActor
   ├─ AStaticMeshActor
   ├─ ASubUVActor
   ├─ ATextActor
   └─ AEditorCameraPawn
```

### 역할

- `AActor`
  - `PostSpawnInitialize`, `BeginPlay`, `Tick`, `EndPlay`, `Destroy`, `Serialize`의 공통 수명주기를 제공한다.
- `ACubeActor`, `APlaneActor`, `ASphereActor`
  - 기본 프리미티브 메시를 가진 액터들이다.
  - 보통 루트에 `UStaticMeshComponent`를 두는 샘플/기본 액터로 보인다.
- `ASkySphereActor`
  - `USkyComponent` 기반의 하늘 렌더링용 액터다.
- `AStaticMeshActor`
  - 일반 정적 메시 배치용 액터다.
  - `UStaticMeshComponent`를 중심으로 구성된다.
- `ASubUVActor`
  - 스프라이트 시트 애니메이션용 `USubUVComponent`를 가지는 액터다.
- `ATextActor`
  - `UTextComponent` 기반 텍스트 렌더링 액터다.
- `AEditorCameraPawn`
  - 에디터 카메라를 월드 안 액터 형태로 다루기 위한 폰이다.
  - `FEditorCameraSubsystem`이 이 액터를 관리한다.

## 4.3 컴포넌트 계층

```text
UObject
└─ UActorComponent
   ├─ USceneComponent
   │  ├─ UCameraComponent
   │  └─ UPrimitiveComponent
   │     ├─ ULineBatchComponent
   │     ├─ UMeshComponent
   │     │  └─ UStaticMeshComponent
   │     │     └─ USkyComponent
   │     ├─ USubUVComponent
   │     └─ UTextComponent
   │        └─ UUUIDBillboardComponent
   └─ URandomColorComponent
```

### 역할

- `UActorComponent`
  - 액터에 부착되는 모든 기능 단위의 공통 부모다.
- `USceneComponent`
  - 상대/월드 트랜스폼, 부모-자식 Attach 트리를 담당한다.
  - 액터의 위치 개념은 사실상 이 클래스에서 시작된다.
- `UCameraComponent`
  - `FCamera`를 소유하고 이동/회전/투영 설정을 제공한다.
- `UPrimitiveComponent`
  - 렌더링 가능한 씬 컴포넌트의 베이스다.
  - 바운즈 계산, 피킹 여부, 레이 교차 훅, 렌더 메시 반환 인터페이스를 가진다.
- `ULineBatchComponent`
  - 디버그 라인/배치형 선 렌더링용 컴포넌트다.
- `UMeshComponent`
  - 머티리얼 슬롯 배열을 가지는 메시 계층 공통 베이스다.
- `UStaticMeshComponent`
  - `UStaticMesh` 에셋을 씬에 배치하는 핵심 렌더 컴포넌트다.
- `USkyComponent`
  - 스카이 메시 전용 정적 메시 컴포넌트다.
- `USubUVComponent`
  - 스프라이트 시트 기반 애니메이션 렌더링용 컴포넌트다.
- `UTextComponent`
  - 텍스트 메시를 동적으로 생성해 렌더링하는 컴포넌트다.
- `UUUIDBillboardComponent`
  - 텍스트를 빌보드처럼 띄워 UUID 라벨을 보여주는 에디터 보조 컴포넌트다.
  - 이번 과제의 `BillboardComponent`, `TextRenderComponent` 설계 참고점으로 보기 좋다.
- `URandomColorComponent`
  - 메시 머티리얼을 런타임에 변조하는 보조 로직 컴포넌트다.

## 4.4 엔진/뷰포트 계층

```text
FEngine
├─ FGameEngine
└─ FEditorEngine

IViewportClient
├─ FGameViewportClient
├─ FEditorViewportClient
└─ FPreviewViewportClient
```

### 역할

- `FGameEngine`
  - 게임 월드 하나를 기준으로 실행하는 단순 런타임 엔진이다.
- `FEditorEngine`
  - 에디터 월드, 프리뷰 월드, 선택 상태, 카메라, Slate UI를 함께 관리하는 확장 엔진이다.
  - PIE 구현 시 가장 먼저 수정하게 될 가능성이 높다.
- `IViewportClient`
  - 입력/렌더링/파일 드롭 처리 같은 뷰포트 행동의 인터페이스다.
- `FGameViewportClient`
  - 게임 모드 기본 뷰포트다.
- `FEditorViewportClient`
  - 에디터의 중심 뷰포트 로직이다.
  - 내부적으로 입력/렌더/에셋 상호작용을 별도 서비스에 위임한다.
- `FPreviewViewportClient`
  - 프리뷰 월드를 그리기 위한 전용 뷰포트다.

## 4.5 렌더링 계층

```text
FRenderer
├─ FRenderStateManager
├─ FShaderManager
├─ FTextMeshBuilder
├─ FSubUVRenderer
├─ FMaterial
│  └─ FDynamicMaterial
├─ FVertexShader
├─ FPixelShader
└─ FRenderMesh
   ├─ FStaticMesh
   └─ FDynamicMesh
```

### 역할

- `FRenderer`
  - D3D11 디바이스, 스왑체인, 렌더 타깃, 상수 버퍼를 관리하는 메인 렌더러다.
  - 커맨드 큐를 받아 레이어별 패스를 실행한다.
- `FRenderStateManager`
  - 래스터/깊이/블렌드 상태를 생성하고 캐시하는 매니저다.
- `FShaderManager`
  - 셰이더 리소스와 생성된 VS/PS 객체를 관리한다.
- `FMaterial`
  - 셰이더 조합, 렌더 스테이트, 텍스처, 머티리얼 상수 버퍼를 묶는 렌더 단위다.
- `FDynamicMaterial`
  - 런타임 파라미터 변경이 가능한 `FMaterial` 파생형이다.
- `FVertexShader`, `FPixelShader`
  - D3D11 셰이더 래퍼다.
- `FRenderMesh`
  - GPU 버퍼로 올릴 렌더 가능한 메시 데이터의 베이스다.
- `FStaticMesh`
  - 정적 메시 버퍼 데이터다.
- `FDynamicMesh`
  - 텍스트, SubUV, Gizmo처럼 런타임 생성/수정이 필요한 메시다.

## 4.6 에디터 보조 구조

```text
FEditorEngine
├─ FEditorUI
├─ FEditorSelectionSubsystem
├─ FEditorCameraSubsystem
├─ FEditorViewportRegistry
├─ FEditorViewportClient
│  ├─ FEditorViewportInputService
│  ├─ FEditorViewportAssetInteractionService
│  └─ FEditorViewportRenderService
├─ FGizmo
├─ FPicker
└─ FSlateApplication
```

### 역할

- `FEditorUI`
  - 아웃라이너, 프로퍼티, 콘텐츠 브라우저, 통계창 등 에디터 창 묶음이다.
- `FEditorSelectionSubsystem`
  - 현재 선택 액터를 관리한다.
- `FEditorCameraSubsystem`
  - 에디터 카메라 폰과 입력 컨트롤러를 관리한다.
- `FEditorViewportRegistry`
  - 여러 에디터 뷰포트의 레이아웃/상태를 보관한다.
- `FEditorViewportInputService`
  - 카메라 내비게이션, 마우스 입력, 선택/기즈모 조작 분기를 담당한다.
- `FEditorViewportAssetInteractionService`
  - 파일 더블클릭/드래그 드롭 같은 에셋 상호작용을 담당한다.
- `FEditorViewportRenderService`
  - 멀티 뷰포트 렌더링과 와이어프레임/그리드/오버레이 처리를 맡는다.
- `FGizmo`
  - 이동/회전/스케일 편집 도구다.
- `FPicker`
  - 화면 좌표를 레이로 바꿔 액터를 선택한다.
- `FSlateApplication`
  - 에디터 중앙 뷰포트 영역의 레이아웃/입력/오버레이를 관리하는 매니저

---

## 5. 기타 시스템 정리

### 입력

- `FInputManager`
  - Windows 메시지를 받아 키/마우스 현재 상태와 pressed/released를 계산한다.
- `FEnhancedInputManager`
  - 액션, 매핑 컨텍스트, 트리거/모디파이어를 얹은 상위 입력 계층이다.

### 에셋/직렬화

- `FObjManager`
  - `.obj`, `.mtl`, `.Model` 파일을 읽어 `UStaticMesh`를 생성하고 캐시한다.
- `FMaterialManager`
  - 머티리얼 JSON 로드와 캐시를 맡는다.
- `FSceneSerializer`
  - `UScene` 저장/로드를 담당한다.

### 공간 질의/피킹/물리

- `BVH`, `FMeshBVH`
  - 씬/메시 레벨 공간 가속 구조다.
- `FSceneRenderCollector`
  - 액터/프리미티브를 순회해서 프러스텀 컬링 후 렌더 커맨드로 바꾼다.
- `FPhysicsManager`
  - 현재는 간단한 `Linetrace` 중심 구조다.

### 플랫폼

- `FWindowsApplication`
  - Windows 메시지 루프 및 애플리케이션 레벨 진입점이다.
- `FWindowsWindow`
  - Win32 창 래퍼다.
- `FWindowsEngineLaunch`, `FEngineLoop`
  - 실제 실행 파일에서 엔진 부팅을 감싸는 플랫폼/런처 계층이다.

---

## 6. PIE 과제 관점에서 중요하게 볼 곳

### 이미 준비된 지점

- `UObject`
  - `Duplicate`, `DuplicateSubObjects` 훅이 이미 있다.
- `UWorld`
  - `DuplicateWorldForPIE` 선언이 이미 있다.
- `FEngine`
  - 여러 `FWorldContext`를 관리할 수 있다.
- `EWorldType`
  - `Game`, `Editor`, `PIE`, `Preview`, `Inactive`가 정의돼 있다.
- `FEditorEngine`
  - 에디터 월드와 프리뷰 월드를 따로 관리한다.

### 앞으로 확인/구현이 필요해 보이는 포인트

- `UWorld::DuplicateWorldForPIE`
  - 실제 복제 범위가 어디까지인지
  - 액터/컴포넌트 깊은 복사 규칙을 어떻게 정할지
- `AActor::BeginPlay`, `AActor::EndPlay`, `AActor::Tick`
  - Editor World와 PIE World에서 호출 정책을 어떻게 나눌지
- `FEditorEngine::TickWorlds`
  - 현재 어떤 월드를 Tick하는지, PIE 시작 후 활성 월드 전환을 어떻게 할지
- `IViewportClient::ResolveWorld`, `ResolveScene`
  - 에디터 뷰포트가 PIE 중 어느 월드를 바라보게 할지
- `FEditorSelectionSubsystem`, `FEditorUI`
  - PIE 중 선택/프로퍼티 대상이 Editor World 기준인지 PIE World 기준인지 정책 결정이 필요하다
- `UUUIDBillboardComponent`, `UTextComponent`
  - `TextRenderComponent`, `BillboardComponent` 설계 시 재사용/분리 후보가 된다

---

## 7. 한 줄 요약

현재 구조는 이미 `UObject`, `UWorld`, `UScene`, `AActor`, `UActorComponent`, `FWorldContext`, `EWorldType`가 갖춰져 있어서,  
5주차+ 과제는 새 시스템을 완전히 처음 만드는 작업이라기보다 기존 에디터/월드 구조 위에 PIE 복제와 플레이 수명주기를 연결하는 작업에 가깝다.
