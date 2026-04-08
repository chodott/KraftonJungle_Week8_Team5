---
title: "5주차+. It's PIE time!"
---

## 0. 기간 및 일정

2026년 4월 7일 오후 4:00 ~ 2026년 4월 9일 오전 10:00

- [ ] 4월 7일: 발제 내용 정리 & 엔진 구조 파악, 이론 공부

- [ ] 4월 8일: 구현, 디버깅


## 1. 학습 내용

### 1.1. 필수 학습 내용

- PIE(Play In Editor)를 구현

  ```
  class UObject
  {
  public:
      // 공유 가능한 오브젝트(얕은 복사)
      UObject* ObjectA;
      UObject* ObjectB;

      // 공유 불가능한 서브 오브젝트(깊은 복사 필요)
      UObject* SubObjectA;
      UObject* SubObjectB;

      virtual ~UObject() {}

      // 서브 오브젝트를 복제하는 함수(하위 클래스에서 재정의 가능)
      virtual void DuplicateSubObjects()
      {
          if (SubObjectA)
              SubObjectA = SubObjectA->Duplicate();

          if (SubObjectB)
              SubObjectB = SubObjectB->Duplicate();
      }

      // 현재 오브젝트를 복제하는 함수
      virtual UObject* Duplicate()
      {
          // 새 객체 생성
          UObject* NewObject = new UObject(*this); // 얕은 복사 수행

          // 서브 오브젝트는 깊은 복사로 별도 처리
          NewObject->DuplicateSubObjects();

          return NewObject;
      }
  };

  class UEditorEngine: public UObject
  {
      TArray<FWorldContext> WorldContexts;

      virtual void Tick(float DeltaSeconds);
  };

  void UEditorEngine::Tick(float DeltaSeconds)
  {
      // Editor 전용 액터 Tick 처리
      for (FWorldContext& WorldContext : WorldContexts)
      {
          UWorld* EditorWorld = WorldContext.World();
          if (EditorWorld && EditorWorld->WorldType == EWorldType::Editor)
          {
              ULevel* Level = EditorWorld->Level;
              {
                  for (AActor* Actor: Level->Actors)
                  {
                      if (Actor && Actor->bTickInEditor)
                      {
                          Actor->Tick(DeltaSeconds);
                      }
                  }
              }
          }
          else if (EditorWorld && EditorWorld->WorldType == EWorldType::PIE)
          {
              ULevel* Level = EditorWorld->Level;
              {
                  for (AActor* Actor: Level->Actors)
                  {
                      if (Actor)
                      {
                          Actor->Tick(DeltaSeconds);
                      }
                  }
              }
          }
      }
  }

  void StartPIE()
  {
      UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();

      UWorld* PIEWorld = UWorld::DuplicateWorldForPIE(EditorWorld, ...);

      GWorld = PIEWorld;

      PIEWorld->InitializeActorsForPlay();
  }

  void EndPIE()
  {
      if (GWorld && GWorld->IsPIEWorld())
      {
          GWorld->CleanupWorld();
          delete GWorld;
      }

      GWorld = GEditor->GetEditorWorldContext().World();
  }
  ```

- 월드에 actor를 배치하고 다양한 component를 추가

- TextRenderComponent 클래스를 구현

    - 기존 UUID 텍스트 렌더링 로직을 component 클래스화

- BillboardComponent 클래스를 구현

    - 멤버 변수 sprite에 drop list UI를 통해 선택한 `UTexture`를 assign

    - UE에서 아래 이미지들을 가져와 `Editor\Icon` 폴더에 복사 합니다.

        - `SpotLight_64x.png`, `PointLight_64x.png`, `Pawn_64x.png`

- `UWorld` 클래스를 구현

  ```
  enum EWorldType
  {
      Editor,
      EditorPreview,
      PIE,
      Game
  };

  class UWorld: public UObject
  {
      // Sub level은 고려하지 않음
      ULevel* Level;
      EWorldType WorldType;

      void Tick(float DeltaTime)
      {
          for (AActor* Actor: Level->Actors)
          {
              if (Actor && Actor->IsActorTickEnabled())
              {
                Actor->Tick(DeltaTime);
              }
          }
      }
  };
  ```

- `ULevel` 클래스를 구현

  ```
  class ULevel: public UObject
  {
      TArray<AActor*> Actors;
  };
  ```

- `AActor` 클래스를 구현

  ```
  class AActor: public UObject
  {
      TSet<UActorComponent*> OwnedComponents;
      USceneComponent* RootComponent;

      bool bTickInEditor;

      virtual void BeginPlay();
      virtual void EndPlay(const EEndPlayReason::Type EndPlayReason);

      virtual void Tick(float DeltaTime)
      {
          for (UActorComponent* Component: OwnedComponents)
          {
              if (Component && Component->IsComponentTickEnabled())
              {
                  Component->TickComponent(DeltaTime, ...);
              }
          }
      }
  };
  ```


## 2. 핵심 키워드

- PIE(Play In Editor)

- Tick

- Begin / End Play

- Object Duplication

- Deep / Shallow Copy

- Sprite

- Component Pattern
