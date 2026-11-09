# Bodycam for Roblox (Luau 포팅)

X-Ray Monolith Bodycam의 **Classic 바디캠 레이어**를 Roblox용 Luau로 옮긴 것입니다.
카메라 디커플링(룩 래그), 풋스텝 카메라, 머리 롤과 흔들림, 스프린트 스웨이,
뷰모델 내림, 사격 임펄스, STALKER 2 스타일 팔 IK, 이동 관성이 들어 있습니다.

C++ 원본의 동작을 그대로 따르도록 포팅했고, 원본 C++ 코드를 직접 빌드해 만든
골든 트레이스와 프레임 단위로 비교해 검증했습니다(아래 [검증](#검증) 참고).

함께 들어 있는 패키지는 세 개입니다.

| 패키지 | 내용 |
| --- | --- |
| `Bodycam` | X-Ray Bodycam Classic 레이어의 Luau 포팅 (이 문서의 대부분) |
| `FPSys` | 업로드된 FPSys 모델을 바디캠과 통합해 다시 짠 것: 카메라 효과, 룩 앵글 복제, 재질 발소리 ([FPSys](#fpsys-통합)) |
| `FPSController` | 이동 상태(질주·앉기·엎드리기·슬라이드)와 무기(연사 속도, 발사 모드, 탄창, 장전, 스코프, 반동) ([FPSController](#fpscontroller)) |

## 무엇이 포팅되었나

| 기능 (원본 설정 이름) | C++ 원본 | Luau |
| --- | --- | --- |
| 공통 입력 신호(마우스 속도, 이동 의도) | `bodycam_simulation.cpp` | `Core/Simulation.luau` |
| Sprint Transition 카메라 스웨이 | `bodycam_simulation.cpp` | `Core/Simulation.luau` |
| 뷰모델 내림 (`vm_lowering_*`) | `bodycam_simulation.cpp` | `Core/Simulation.luau` |
| 사격 임펄스 (`fire_impulse_*`) | `bodycam_simulation.cpp` | `Core/Simulation.luau` |
| STALKER 2 스타일 팔 IK (`stalker2_arm_ik_*`) | `bodycam_simulation.cpp` | `Core/Simulation.luau` |
| 카메라 디커플링: 데드존 래그 | `bodycam_camera_rig.cpp` | `Core/CameraRig.luau` |
| 카메라 디커플링: 건-퍼스트 팔로우 래그 | `bodycam_reference/classic_follow.h`, `camera.cpp` | `Reference/ClassicFollow.luau`, `Reference/CameraMotion.luau` |
| 머리 롤·흔들림 (`camera_reference_*`) | `classic_footstep_camera.h`, `movement_vibration.cpp`, `mouse_shake.cpp`, `camera_effects.cpp` | `Reference/ReferenceHead.luau`, `Reference/CameraShake.luau` |
| 카메라 회전 델타 | `classic_regrip.h` (`ClassicRegripCameraDelta`) | `Reference/CameraDelta.luau` |
| 풋스텝 카메라 (`footstep_camera_*`) | `classic_footstep_camera.h`, `footstep_camera_data.h` | `Reference/FootstepCamera.luau`, `Reference/FootstepCameraData.luau`(생성됨) |
| 마우스 에임 누적 | `bodycam_mouse_input.cpp` | `Core/MouseAim.luau` |
| 이동 관성 (`movement_*`) | `bodycam_movement_response.cpp` | `Core/MovementInertia.luau` |
| 무기별 뷰모델 프로필 | `bodycam_viewmodel_profile.cpp` | `Core/ViewmodelProfile.luau` |
| 게이트 속도 앵커 | `bodycam_reference/speed_curve.h` | `Core/GaitSpeed.luau` |
| 설정 레지스트리·프리셋 | `bodycam_settings.cpp` | `Core/Settings.luau` |
| 캐릭터별 호스트 (`CBodycam`) | `bodycam_camera.cpp` | `Core/BodycamCore.luau` |
| 엔진 어댑터 | `bodycam_actor_camera.cpp`, `bodycam_actor_movement.cpp` | `Roblox/BodycamController.luau`, `Roblox/CoordinateSpace.luau` |

### 포팅하지 않은 것

- **Reference 모드** (녹화된 레퍼런스 리그 전체 구동, `bodycam_reference/` 대부분):
  X-Ray 스켈레톤과 녹화 캐릭터 패킷에 묶여 있어 Roblox 리그로 옮기려면 리타깃 작업이 따로 필요합니다.
- **HUD 팔 솔버** (`bodycam_hud_arms.cpp`): X-Ray HUD 본 구조 전용입니다. 대신 어댑터가
  팔 IK 회전을 뷰모델 전체에 그립 피벗 기준으로 적용합니다(근사).
- **리그립, Reference 리코일, 핸들링** (`classic_regrip`, `classic_recoil`, `handling`): Reference 애셋 뱅크가 필요합니다.
- **스프린트 시작 연출, 스프린트 건 밥** (`bodycam_sprint_start.h`, `classic_sprint_bob.h`): X-Ray 애니메이션 페이싱 전용입니다.
- **PiP 스코프, 벡터 레티클, 잔디 렌더러**: 렌더러(DX11/셰이더) 기능이라 Luau로 옮길 대상이 아닙니다.
- ImGui/MCM 메뉴, 콘솔 명령, 진단 로그.

## 폴더 구조

```
roblox/
├── default.project.json          Rojo 프로젝트
├── src/
│   ├── Bodycam/                  ReplicatedStorage.Bodycam (ModuleScript)
│   │   ├── init.luau             공개 API
│   │   ├── Core/                 엔진 독립 로직 (Roblox 타입 없음)
│   │   ├── Reference/            Reference 라이브러리에서 가져온 카메라 부품 (Roblox 타입 없음)
│   │   └── Roblox/               Roblox 어댑터 (Camera, Humanoid, Raycast 사용)
│   ├── FPSys/                    ReplicatedStorage.FPSys: 카메라 효과, 룩 앵글, 발소리
│   ├── FPSController/            ReplicatedStorage.FPSController: 이동 상태, 무기, 반동
│   ├── Client/FPSClient.client.luau       예제 LocalScript (세 패키지를 모두 연결)
│   └── Server/FPSServer.server.luau       태그 지정과 룩 앵글 중계
├── tests/                        Roblox 밖에서 도는 테스트, 골든 트레이스
└── tools/
    ├── golden/                   C++ 원본으로 골든 트레이스를 만드는 하네스
    ├── luau-runner/              테스트용 최소 Luau 실행기 (Rust + mlua)
    ├── gen_footstep_data.py      풋스텝 데이터 헤더 → Luau 변환기
    └── rbxm_dump.py              .rbxm 모델에서 스크립트 소스를 꺼내는 도구
```

`Core/`와 `Reference/`는 Roblox API를 쓰지 않으므로 어떤 Luau VM에서도 돕니다.
Roblox에 묶이는 코드는 `Roblox/` 두 파일뿐입니다.

## 설치

### Rojo 사용

1. [Rojo](https://rojo.space)를 설치합니다.
2. 이 폴더에서 다음 명령을 실행합니다.

```bash
rojo serve roblox/default.project.json
```

3. Roblox Studio의 Rojo 플러그인에서 Connect를 누릅니다.
   `ReplicatedStorage`에 `Bodycam`, `FPSys`, `FPSController`가, `ServerScriptService`에
   `FPSServer`가, `StarterPlayerScripts`에 `FPSClient`가 생깁니다.
4. 업로드한 원본 FPSys 모델은 넣지 마세요. 이 프로젝트의 `FPSys`가 그것을 대신하고,
   둘을 같이 넣으면 카메라와 RemoteEvent가 두 번 처리됩니다.

### 수동 복사

1. `ReplicatedStorage`에 ModuleScript `Bodycam`을 만들고 `src/Bodycam/init.luau` 내용을 붙여 넣습니다.
2. 그 아래에 Folder `Core`, `Reference`, `Roblox`를 만듭니다.
3. 각 폴더에 같은 이름의 ModuleScript를 만들고 해당 `.luau` 파일 내용을 붙여 넣습니다(확장자 제외한 이름).
4. `FPSys`와 `FPSController`도 같은 방법으로 만듭니다(`init.luau`가 패키지 ModuleScript, 나머지는 그 자식).
5. `ServerScriptService`에 Script를 만들고 `src/Server/FPSServer.server.luau` 내용을 붙여 넣습니다.
6. `StarterPlayerScripts`에 LocalScript를 만들고 `src/Client/FPSClient.client.luau` 내용을 붙여 넣습니다.

## 사용법

```lua
local Bodycam = require(game.ReplicatedStorage.Bodycam)

local controller = Bodycam.controller().new({
	walkSpeed = 16,          -- 평상시 WalkSpeed (게이트 앵커)
	sprintSpeed = 25.6,      -- 전력 질주 속도
	isSprinting = function() return sprinting end,
	isCrouching = function() return crouching end,
	viewmodel = viewmodelModel,             -- PrimaryPart가 있는 Model (선택)
	viewmodelOffset = CFrame.new(0.6, -0.6, -1.2),
	applyMovementInertia = false,           -- true면 Humanoid:Move로 이동 관성을 직접 적용
})
controller:start()

controller:setAiming(true)        -- 조준 (ADS 블렌드 자동)
controller:notifyShot(1)          -- 발사 1회 (1 = 일반 소총)
controller:setWeaponProfile({ x = 0.02, y = -0.01, z = 0 }, { x = 0, y = 2, z = 0 })  -- 무기별 힙 오프셋
controller:applyPreset(1)         -- 0 끄기, 1 Balanced, 2 기본값
```

**사격 방향은 `controller:getAimCFrame()`을 쓰세요.** 화면 카메라(`getViewCFrame()`)는
룩 래그와 흔들림 때문에 조준선과 다릅니다. 원본에서도 탄도는 에임을 따르고 화면만 지연됩니다.

### 설정 바꾸기

설정 이름은 원본 콘솔 명령에서 `bodycam_` 접두어를 뺀 것입니다. 그래서
[docs/CLASSIC.md](../docs/CLASSIC.md)의 설명이 그대로 적용됩니다.

```lua
local Settings = Bodycam.Settings
Settings.set(controller.config, "hip_camera_max_yaw_offset", 9)   -- 범위 밖 값은 잘립니다
Settings.set(controller.config, "footstep_camera_enable", false)
for _, s in Settings.list() do print(s.name, s.default, s.min, s.max) end  -- 옵션 UI 만들기용
```

### 엔진 없이 코어만 쓰기

자체 카메라 시스템이 있으면 어댑터 없이 `BodycamCore`를 직접 돌릴 수 있습니다.
입력과 출력은 X-Ray 관례(라디안, 미터, 카메라 로컬 x 오른쪽·y 위·z 앞)를 따르고,
`Roblox/CoordinateSpace.luau`가 Roblox CFrame으로 바꿉니다.

```lua
local core = Bodycam.BodycamCore.new()
local frame = { camera = Bodycam.CameraRig.newInput(), move_flags = Bodycam.MoveFlags.Forward }
-- 매 프레임: frame.camera.aim_yaw / aim_pitch / dt / velocity / foot_phase ... 를 채운 뒤
local composition = core:update(frame, { horizontalClearance = function(right, forward, maxDistance) return nil end })
local view = Bodycam.coordinateSpace().composeView(aimCFrame, yaw, pitch, composition, studsPerMetre)
```

## 좌표계와 단위

| | X-Ray (코어 내부) | Roblox |
| --- | --- | --- |
| 손 방향 | 왼손 좌표계 | 오른손 좌표계 |
| 앞 | +Z | -Z (`LookVector`) |
| 단위 | 미터 | 스터드 |

- Z축을 뒤집으면 두 좌표계가 일치합니다. X-Ray 헤딩 `h`와 피치 `p`는
  `CFrame.fromOrientation(p, h, 0)`과 같습니다.
- 회전 행렬은 `CoordinateSpace.deltaToCFrame`이 Z 반전으로 기저 변환합니다. 부호를 손으로 뒤집지 마세요.
- 기본 축척은 Roblox 아바타 기준 1 스터드 = 0.28 m입니다. 게임 비율이 다르면 `studsPerMetre`를 넘기세요.
- Roblox에는 X-Ray의 발소리 스케줄이 없어서, 어댑터가 평면 속도로 보폭 시계(`GaitSpeed.StrideClock`)를 돌립니다.
  애니메이션 발자국 마커가 있으면 `footPhase` 옵션으로 넘기는 편이 더 정확합니다(왼발 0, 오른발 0.5).

### 카메라 피드백 방지

Roblox 기본 카메라 스크립트는 다음 프레임을 만들 때 `Camera.CFrame`을 다시 읽습니다.
바디캠 시야를 그대로 두면 래그가 에임에 누적됩니다. 그래서 컨트롤러는

1. `RenderPriority.Camera - 1`에서 지난 프레임의 **에임** CFrame을 되돌려 놓고,
2. `RenderPriority.Camera + 1`에서 카메라 스크립트가 만든 에임을 읽어 바디캠 **시야**를 덮어씁니다.

## FPSys 통합

업로드한 `FPSys.rbxm`에는 클라이언트 모듈(카메라·룩 앵글·발소리 컴포넌트), `Spring`, `Maid`,
서버 스크립트가 들어 있었습니다(`tools/rbxm_dump.py`로 꺼냈습니다). 기능과 설정값은 그대로 두고
바디캠 위에서 함께 돌도록 다시 짰습니다. 설정은 `FPSys/Config.luau` 한 곳에 있습니다.

| 컴포넌트 | 하는 일 | 바디캠과의 연결 |
| --- | --- | --- |
| `CameraEffects` | 걷기 흔들림, 숨쉬기, 좌우 이동 기울기, 마우스 룩 스웨이, 착지 충격, 모션 블러, **기울이기(새)**, **사격 카메라 흔들림(새)** | `BodycamController:addViewModifier`로 바디캠 시야 위에 얹힘 |
| `LookAngles` | 다른 플레이어가 보는 방향으로 머리·상체·팔 Motor6D를 돌림, RemoteEvent로 복제 | 1인칭 판정에 바디캠 에임 사용 |
| `Footsteps` | 바닥 재질별 발소리 | 로컬 캐릭터는 바디캠 보폭의 발 접지 시점에 소리를 냄 |
| `FPSServer` | Humanoid에 `RealismHook` 태그, 룩 앵글 중계 | — |

`Config.Overlap = "Auto"`(기본값)이면 바디캠이 이미 하는 움직임은 FPSys에서 끕니다.
풋스텝 카메라가 켜져 있으면 FPSys 걷기 흔들림을, 바디캠 머리 롤이 켜져 있으면 좌우 기울기를 끕니다.
둘 다 쓰려면 `"All"`로 바꾸세요.

### 원본 FPSys에서 고친 문제

- **룩 앵글이 전혀 동작하지 않았습니다.** 매 프레임 Motor6D를 돌리는 부분이
  `-- Process rotator updates...` 주석만 있는 빈 루프였습니다. 이제 실제로 모터를 돌립니다.
- **복제 인자 순서가 틀렸습니다.** 서버는 `(player, pitch, yaw)`를 보내는데 클라이언트는
  `(pitch, yaw)`로 받아 플레이어 객체를 각도로 썼고, 모든 업데이트를 자기 캐릭터에 적용했습니다.
  이제 보낸 플레이어의 캐릭터에 적용합니다.
- **카메라 효과가 에임에 누적되었습니다.** RenderStepped에서 `Camera.CFrame`을 직접 곱해서
  다음 프레임 카메라 스크립트가 그 값을 다시 읽었습니다. 이제 시야에만 적용됩니다.
- 걷기 흔들림이 `Humanoid.CameraOffset`이라 사격 위치까지 흔들렸습니다. 이제 시야 오프셋입니다.
- 착지 충격이 프레임 시간(`dt * 60`)에 비례해 프레임레이트마다 세기가 달랐습니다.
- 리스폰하면 착지 감지(`StateChanged`)가 끊겼습니다.
- 발소리가 상태가 바뀔 때마다 `while` 루프를 새로 띄워 루프가 쌓였고, 멈춰 있을 때 0으로 나눴습니다.
- 없는 함수 `GetBaseSubjectPosition`을 부르는 코드가 있었습니다(호출되지 않아 드러나지 않았음). 제거했습니다.
- 서버가 이벤트를 제한 없이 모두에게 중계했습니다. 플레이어당 0.1초 간격 제한과 무한대 값 거부를 넣었습니다.

`Spring`과 `Maid`는 Quenty의 Nevermore Engine 모듈(MIT 라이선스)이라 헤더에 출처만 달고 그대로 두었습니다.

## FPSController

요청하신 두 Kinemation 저장소(`fps-animation-framework`, `scriptable-animation-system`)를 확인한 결과:

- 두 저장소에는 **Unity 데모 스크립트만** 있습니다(C# 8개, 14개). 실제 프레임워크
  (`KINEMATION.FPSAnimationFramework` 등: 반동 애니메이션, 반동 패턴, IK 모션 레이어, 룩 레이어,
  카메라 흔들림)는 유니티 에셋 스토어 유료 패키지라 저장소에 없습니다. README도 패키지를 따로
  가져오라고 안내합니다.
- 두 저장소 모두 **라이선스 파일이 없습니다.** 라이선스가 없는 코드는 기본적으로 저작권자 외에 쓸 권리가 없습니다.

그래서 C# 코드를 줄 단위로 옮기지 않았습니다. 데모가 보여 주는 **기능**을 기준으로 Luau 모듈을 새로 작성했습니다.
게임 메커니즘 자체는 저작권 대상이 아니지만, 코드 표현은 대상입니다.

| 모듈 | 기능 | 종류 |
| --- | --- | --- |
| `MovementStateMachine` | 대기·걷기·질주·공중·슬라이드, 서기·앉기·엎드리기, 보행 속도 간 부드러운 전환, 슬라이드 감속, 엎드리기 전환 중 입력 잠금, 앉은 채 점프 → 일어서기, 자세별 카메라 높이 | 순수 로직 |
| `Weapon` | RPM 연사 속도, 자동·단발·점사, 탄창, 장전·꺼내기·넣기 시간, 스코프별 FOV 배율과 감도 | 순수 로직 |
| `RecoilPattern` | 발당 위·옆 반동, 연사 누적, 첫 발 배율, 조준 배율, 사격 후 일부 복귀 | 순수 로직 |
| `MovementController` | 상태 기계를 Humanoid에 적용(WalkSpeed, 슬라이드 이동, 앉은 상태 점프 금지, 일어설 공간 검사, 카메라 높이) | Roblox |
| `WeaponController` | 사격 → 바디캠 사격 임펄스 + FPSys 흔들림 + 반동, 반동 → `kickAim`, 조준 FOV·감도, 무기 교체, 기울이기, 질주 중 사격·조준 금지 | Roblox |

반동은 엔진의 스크립트 카메라 델타와 같은 방식으로 들어갑니다. `BodycamController:kickAim`이
에임을 돌리고 바디캠 래그를 `rebase`하므로, 반동은 마우스 움직임으로 취급되지 않습니다(팔이 휙 돌지 않음).

### 키 조작 (예제 `FPSClient`)

| 키 | 동작 |
| --- | --- |
| WASD | 이동 |
| Left Shift | 질주 |
| C | 앉기 토글 |
| Z | 엎드리기 토글 |
| Left Ctrl | 슬라이드 (질주 중) |
| Space | 점프 (앉음·엎드림에서는 일어서기) |
| 마우스 오른쪽 | 조준 |
| 마우스 왼쪽 | 사격 |
| R | 장전 |
| V | 발사 모드 변경 |
| T | 다음 스코프 (조준 중) |
| 1, 2 | 무기 선택 |
| Q, E | 왼쪽·오른쪽 기울이기 |

### 무기 정의

```lua
local Weapon = require(game.ReplicatedStorage.FPSController).Weapon
local smg = Weapon.defaultConfig()
smg.name = "SMG"
smg.rpm = 850
smg.modes = { "Auto", "Semi" }
smg.magazine = 32
smg.scopes = { { name = "Holo", zoom = 0.9, sensitivity = 0.9 } }
smg.recoil.pitch = { 0.35, 0.55 }   -- 발당 위쪽 반동(도)
smg.recoil.recovery = 0.6           -- 사격 후 되돌아오는 비율
smg.profile = { pos = { x = 0.01, y = -0.01, z = 0 }, rot = { x = 0, y = 1, z = 0 } }  -- 바디캠 힙 오프셋
```

`onFire(fireCFrame, weapon)`이 발마다 불립니다. 명중 판정은 게임이 하고, **서버에서 검증해야 합니다.**
`fireCFrame`은 에임(기울이기 포함)이라 화면 흔들림과 무관합니다.

## 검증

`tools/golden/golden.cpp`가 원본 C++ 코드(`bodycam_simulation.cpp`, `bodycam_mouse_input.cpp`,
`bodycam_viewmodel_profile.cpp`, Reference 라이브러리)를 그대로 링크해 정해진 입력 시나리오
(프레임 시간 흔들림, 144/30 fps 구간, 히치, 플릭, 각도 랩, 걷기·질주·앉기·점프, ADS, 사격)를
돌리고, 프레임마다 입력과 출력을 `tests/golden/*.luau`에 씁니다.
`tests/golden.spec.luau`가 같은 입력을 Luau 모듈에 넣고 결과를 비교합니다.

| 트레이스 | 프레임 | 비교 항목 |
| --- | --- | --- |
| simulation | 1200 | 마우스 에임, 마우스 속도, 이동 의도, 스웨이, 뷰모델 위치·회전, 내림, 팔 IK |
| follow | 900 | 팔로우 카메라 각도, 팔 오프셋, 룩 시프트 |
| footstep | 1200 | 오프셋, 위상, 잠금, 제동 발걸음 |
| head | 900 | 롤, 흔들림 피치·요·롤 |
| camera_delta | 7 | 회전 행렬 |
| viewmodel_profile | 300 | 위치, 회전, 활성 |

원본은 단정밀도(float)를 섞어 쓰고 Luau는 배정밀도라 비트 단위로 같지는 않습니다.
결과가 눈에 띄게 달라지는 세 곳은 원본의 float 동작을 그대로 따라 했습니다:
팔 스프링 서브스텝 수(60 fps에서 `dt * 120`이 float로 정확히 2), Reference 프레임 델타의 float 반올림,
흔들림 오실레이터 위상의 float 누적. 마지막 측정에서 가장 큰 오차는 흔들림 피치 약 6e-5°였습니다.

나머지(데드존 래그, 리그 합성, 이동 관성, 설정, 게이트 속도, 좌표 변환, 어댑터)는
`tests/unit.spec.luau`와 `tests/controller.spec.luau`의 단위 테스트로 확인합니다.
어댑터 테스트는 Roblox 서비스(카메라, Humanoid, RunService, Raycast)를 목으로 대신합니다.

FPSys와 FPSController는 다음으로 확인합니다.

- `tests/fpscontroller.spec.luau`: 보행 속도 전환, 질주 조건, 앉기·일어서기 공간, 슬라이드, 엎드리기 잠금,
  점프·착지 이벤트, 600 RPM 자동 사격 발수, 단발·점사, 탄창·장전, 스코프 FOV, 반동 상승과 복귀 비율.
- `tests/fpsys.spec.luau`: 룩 앵글 계산과 모터 회전, 보낸 플레이어 캐릭터에 적용되는지, 재질 발소리,
  숨쉬기·착지 충격·겹침 규칙, 기울이기 벽 정지, 사격 흔들림.
- `tests/integration.spec.luau`: 세 패키지를 함께 띄운 목 클라이언트에서 걷기 발소리, 조준 FOV·감도,
  자동 사격 반동이 에임을 실제로 올리고 복귀하는지, 질주 중 사격·조준 금지와 해제 후 조준 재개,
  기울이기 사격 위치, 앉기 카메라 높이, 무기 교체 시간, 정리 후 남는 연결이 없는지.

현재 전체 46개 테스트가 통과합니다.

### 테스트 실행

Rust가 필요합니다(Luau VM은 `mlua` 크레이트에 포함되어 빌드됩니다).

```bash
cargo build --release --manifest-path roblox/tools/luau-runner/Cargo.toml
```

```bash
cd roblox && tools/luau-runner/target/release/luau-runner tests/run.luau
```

골든 트레이스를 다시 만들려면 CMake와 C++17 컴파일러가 필요합니다.

```bash
roblox/tools/golden/build.sh
```

풋스텝 데이터 헤더가 바뀌면 Luau 데이터 파일을 다시 생성하세요.

```bash
python3 roblox/tools/gen_footstep_data.py
```

## 한계와 주의할 점

- 앉기와 엎드리기는 속도, 점프, 카메라 높이만 바꿉니다. 캐릭터 애니메이션은
  `MovementController:onEvent`(`Crouched`, `ProneStarted`, `SlideStarted` 등)에 연결해 재생하세요.
  Kinemation 프레임워크의 IK 모션 레이어와 애니메이션 블렌딩은 유료 패키지에 있어 포함하지 않았습니다.
- `MovementController`와 바디캠 `applyMovementInertia`는 둘 다 이동을 바꾸므로 하나만 켜세요(예제는 전자).
- **Roblox Studio에서 실행해 보지 않았습니다.** 모든 파일이 Luau 컴파일러를 통과하고,
  어댑터는 목 서비스로 테스트했지만, 실제 Studio 카메라 스크립트와의 상호작용과 체감 튜닝은
  Studio에서 확인해야 합니다. `luau-analyze` 타입 검사도 돌리지 않았습니다.
- 뷰모델 배치는 원본 HUD 솔버의 근사입니다. 팔 IK 회전은 그립 피벗(`gripPivot`, 기본
  오른쪽 0, 위 -0.12, 앞 0.35 m) 기준으로 뷰모델 전체를 돌립니다. 손·팔 본을 따로 움직이려면
  `controller.core:getArmPose()`를 읽어 직접 Motor6D에 적용하세요.
- `applyMovementInertia = true`는 ControlModule 뒤에서 `Humanoid:Move`를 덮어씁니다.
  자체 이동 시스템이 있으면 `false`로 두고 `core:updateMovement()` 결과를 그 시스템에 넣으세요.
- 원본의 시간 상수와 거리는 X-Ray 캐릭터 비율에 맞춰져 있습니다. 축척이 다른 게임은
  `studsPerMetre`와 프리셋으로 먼저 맞추고 세부 값을 조정하세요.

### 라이선스

이 저장소의 [License.txt](../License.txt)는 소스 코드가 GSC Game World 소유이며
**상업적 목적으로 쓸 수 없다**고 정합니다. 이 Luau 코드는 그 소스를 옮긴 파생물이고,
`FootstepCameraData.luau`는 원본 데이터 헤더에서 생성한 것입니다
([docs/PROVENANCE.md](../docs/PROVENANCE.md) 참고). Robux 수익이 나는 경험(게임 패스,
개발자 상품, 프리미엄 페이아웃 등)에 넣기 전에 라이선스 적용 범위를 직접 확인하세요.

- `FPSys`: 업로드한 모델을 바탕으로 했습니다. 모델 자체의 사용 권리는 직접 확인하세요.
  그 안의 `Spring`, `Maid`는 Nevermore Engine(MIT)입니다.
- `FPSController`: 이 프로젝트에서 새로 작성했습니다. Kinemation 코드는 쓰지 않았습니다.
  Kinemation의 실제 애니메이션 시스템이 필요하면 유니티 에셋 스토어 라이선스가 필요하고,
  그 C# 코드를 Roblox용으로 옮겨 배포하려면 Kinemation의 허락이 따로 필요합니다.
