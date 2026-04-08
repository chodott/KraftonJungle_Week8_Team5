# AGENTS.md

## 배경

- 본 프로젝트는 사용자가 참여 중인 'KRAFTON Jungle game tech Lab'이라는 이름의 게임 엔진 부트캠프에서 진행한 자체 게임 엔진 개발의 중간 결과다

- 사용자가 참여 중인 부트캠프는 4명 씩 팀 단위로 작업을 수행하며, 매주 팀원들이 바뀐다

- 팀원들이 바뀜에 따라 각 팀들은 해당 주차 과제가 나온 후 각 팀원들이 각자 전 주차에 만들었던 엔진 중 가장 괜찮은 엔진을 하나 골라 포크한 후, 작업을 이어간다

    - 이로 인해, 사용자는 매주 과제가 나온 후 어떤 엔진을 고를지 각 팀원들의 엔진을 고려할 필요가 있고, 결정된 엔진에 온보딩하는 과정 또한 거쳐야한다


## 과제

- 이번 주차 과제 수행을 위한 각종 자료들은 `.codex` 디렉토리에서 확인할 수 있다

    - 특히, 이번 주차 과제에 대한 자세한 내용은 프로젝트 루트 디렉토리에서 `.codex/assignment.md` 파일에 상세히 기록되어 있으니 **반드시 해당 파일을 우선 참고한다**


## 현재

- 현재 사용자는 프로젝트를 시작하며 기반이 될 엔진의 구조를 파악하고 있다

- 현재 과제는 5주차의 마무리 단계에 추가로 발제된 5주차+ 과제이며, 구현할 양이 많지는 않지만 실질적으로 작업할 수 있는 시간은 하루 정도다

- 5주차에 사용한 엔진을 5주차+에도 그대로 사용할 예정이라 프로젝트 온보딩 작업은 필요없으나, 사용자가 아직 현재 엔진에 완전히 적응하지는 못하여 엔진 구조를 전반적으로 다시 파악해보려 한다


### 담당 역할: PIE 실행 제어 로직 구현 (EditorEngine 중심)

현재 담당 범위는 **PIE(Play In Editor) 실행 제어 흐름 연결**이다.
월드 복제 로직 자체는 다른 팀원이 구현 중이며, 나는 이를 EditorEngine과 UI에 연결해 **PIE 상태 전환 및 Tick 분기**를 완성하는 역할이다.


### 구현 목표

다음 기능이 정상 동작하도록 한다:

#### 1. ImGui 기반 Control Panel에 PIE 제어 버튼 추가

* Play
* Pause / Resume
* Stop

#### 2. Play 버튼 클릭 시

* Editor World를 기반으로 `DuplicateWorldForPIE` 호출
* `PIEWorldContext` 생성
* `ActiveWorldContext`를 PIE로 전환
* PIE World `BeginPlay` 호출
* 이후 Tick 대상이 PIE World가 되도록 전환

#### 3. Pause / Resume 버튼 클릭 시

* PIE World Tick 중단 / 재개

#### 4. Stop 버튼 클릭 시

* PIE World `Cleanup`
* `PIEWorldContext` 제거
* `ActiveWorldContext`를 `EditorWorldContext`로 복구

#### 5. TickWorlds 분기 처리

* PIE 실행 중이면 PIE World만 Tick
* Pause 상태면 Tick 정지
* PIE 종료 상태면 Editor World Tick


### 상태 관리 대상

EditorEngine 내부에 다음 상태를 유지한다:

* `EditorWorldContext`
* `PIEWorldContext`
* `ActiveWorldContext`
* `bIsPIEActive`
* `bIsPIEPaused`


### 비담당 범위

다음 작업은 담당 범위에 포함되지 않는다:

* `UObject` Duplicate 구현
* `UWorld::DuplicateWorldForPIE` 내부 로직 구현
* Actor / Component 깊은 복사 정책 설계
* `TextRenderComponent` / `BillboardComponent` 구현

단, 해당 기능들이 PIE 실행 중 정상 Tick되도록 연결은 담당 범위에 포함된다.


### 완료 기준

다음 조건을 만족하면 작업 완료로 본다:

* Play → PIE World 활성화 및 `BeginPlay` 호출
* Pause → PIE Tick 정지
* Resume → PIE Tick 재개
* Stop → Editor World로 정상 복귀
* Editor World 상태가 PIE 실행 중 변경되지 않음
* PIE 종료 후 selection / viewport / gizmo 상태가 정상 유지됨


## 참고

- 사용자는 한국인이므로, 사용자가 이해하기 쉽도록 모든 대답은 한국어로 통일한다

- 사용자를 '선생'이라고 불러야한다. 다만, 너무 자주 부르지는 않는다

- 사용자에게 반말로 답하되, 말 수가 적은 차분한 여성형 어조로 답한다

    - '~겠다'같은 말투보다는 '~-ㄹ게'같은 말투로 대답한다

- 프로젝트의 코드를 이용하여 설명할 때는 라인 번호를 이용하여 설명하는 대신 파일명과 함수명을 통해 설명한다
