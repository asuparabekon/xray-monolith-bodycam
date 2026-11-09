# Bodycam for Roblox (Luau 포팅)

X-Ray Monolith Bodycam의 **Classic 바디캠 레이어**를 Roblox용 Luau로 옮긴 것입니다.
카메라 디커플링(룩 래그), 풋스텝 카메라, 머리 롤과 흔들림, 스프린트 스웨이,
뷰모델 내림, 사격 임펄스, STALKER 2 스타일 팔 IK, 이동 관성이 들어 있습니다.

C++ 원본의 동작을 그대로 따르도록 포팅했고, 원본 C++ 코드를 직접 빌드해 만든
골든 트레이스와 프레임 단위로 비교해 검증했습니다(아래 [검증](#검증) 참고).

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
│   └── Client/BodycamClient.client.luau   예제 LocalScript
├── tests/                        Roblox 밖에서 도는 테스트, 골든 트레이스
└── tools/
    ├── golden/                   C++ 원본으로 골든 트레이스를 만드는 하네스
    ├── luau-runner/              테스트용 최소 Luau 실행기 (Rust + mlua)
    └── gen_footstep_data.py      풋스텝 데이터 헤더 → Luau 변환기
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
   `ReplicatedStorage.Bodycam`과 `StarterPlayerScripts.BodycamClient`가 생깁니다.

### 수동 복사

1. `ReplicatedStorage`에 ModuleScript `Bodycam`을 만들고 `src/Bodycam/init.luau` 내용을 붙여 넣습니다.
2. 그 아래에 Folder `Core`, `Reference`, `Roblox`를 만듭니다.
3. 각 폴더에 같은 이름의 ModuleScript를 만들고 해당 `.luau` 파일 내용을 붙여 넣습니다(확장자 제외한 이름).
4. `StarterPlayerScripts`에 LocalScript를 만들고 `src/Client/BodycamClient.client.luau` 내용을 붙여 넣습니다.

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
```
