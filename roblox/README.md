# Bodycam for Roblox

Roblox용 1인칭 **바디캠 연출** 패키지 모음입니다. Unrecord 같은 바디캠 FPS 느낌을 목표로 합니다.

- **바디캠:** 시야가 에임을 늦게 따라가고, 걸음·질주·착지에 머리가 움직이고, 무기가 걷기·사격·마우스에 반응합니다.
- **연출:** 생리적 흔들림, 프리 에임, 화면 효과, 1인칭 몸 보이기.
- **조작:** 이동 상태와 무기.

`Bodycam` 패키지는 처음에 X-Ray Monolith Bodycam C++ 소스를 Luau로 옮긴 포팅이었습니다. 그 소스의 라이선스가
상업적 사용을 금지하므로, 지금은 **동작 명세만 보고 새로 설계·구현한 버전**으로 바뀌었습니다
([재설계](#bodycam-패키지-재설계) 참고).

함께 들어 있는 패키지는 다섯 개입니다.

| 패키지 | 내용 |
| --- | --- |
| `Bodycam` | 바디캠 카메라·무기 움직임. 동작 명세로 새로 설계한 버전 ([재설계](#bodycam-패키지-재설계)) |
| `FPSys` | 업로드된 FPSys 모델을 바디캠과 통합해 다시 짠 것: 카메라 효과, 기울이기, 재질 발소리 ([FPSys](#fpsys-통합)) |
| `FPSController` | 이동 상태(질주·앉기·엎드리기·슬라이드)와 무기(연사 속도, 발사 모드, 탄창, 장전, 스코프, 반동) ([FPSController](#fpscontroller)) |
| `Presentation` | 상업 사용 가능한 오픈소스를 포팅한 바디캠 연출: 생리적 무기 흔들림, 프리 에임, 손떨림, 벽 당김, 화면 효과 ([Presentation](#presentation-오픈소스-포팅)) |
| `Realism` | Character-Realism(MPL-2.0) 포팅: 1인칭 몸 보이기, 복제되는 머리·상체 시선 ([Realism](#realism-character-realism-포팅)) |

## Bodycam 패키지 (재설계)

### 왜 다시 만들었나

처음 버전은 X-Ray Monolith Bodycam C++ 코드를 줄 단위로 옮긴 포팅이었습니다. 그 소스는 저장소의
[License.txt](../License.txt)상 상업적 사용이 금지되어 있습니다. 그래서 다음 순서로 다시 만들었습니다.

1. 원본 코드·상수·녹화 데이터·이름을 쓰지 않고, **동작만 적은 명세서**
   [docs/BODYCAM_DESIGN.md](docs/BODYCAM_DESIGN.md)를 작성했습니다. 기본값은 모두 새로 정했습니다.
2. **원본과 이전 포팅을 보지 않는 별도 에이전트**가 명세서와 연결 대상 패키지 코드만 보고 구현했습니다.
3. 원본에서 나온 파일(이전 코어, 녹화 머리 움직임 데이터, C++ 대조 도구와 트레이스, 데이터 변환기)은 삭제했습니다.

명세서를 쓴 쪽은 원본을 본 상태이므로, 법적으로 엄격한 클린룸(원본을 본 사람과 구현자가 완전히 분리된 절차)은 아닙니다.
상업 출시 전에는 법률 검토를 받으세요.

### 기능

| 기능 | 하는 일 | 모듈 |
| --- | --- | --- |
| 룩 래그 | 시야가 스프링으로 에임을 늦게 따라감(힙 5Hz, 조준 11Hz). 0.35° 데드밴드, 최대 7°(조준 1.5°) 콘, 빠르게 돌면 스프링이 단단해짐, 래그·이동 방향으로 시야 위치 이동. 반동(`kickAim`)은 래그 없이 바로 반영 | `LookLag.luau` |
| 머리 움직임 | 보폭에 맞춘 걸음 흔들림(접지마다 아래로, 디딘 발 쪽으로), 좌우 이동·회전 롤, 질주 숙임과 시작 돌진, 착지 충격, 벽 앞에서 옆 흔들림 줄임 | `HeadMotion.luau` |
| 무기 움직임 | 앞으로 걸을 때 무기 내림(사격·조준 중 억제), 안전(홀스터) 자세, 사격 반동 스프링, 빠른 마우스에 손목 롤·피치, 무기별 힙 오프셋 | `WeaponMotion.luau` |
| 이동 관성 | 가속 0.22초, 감속 0.16초, 정지 0.12초, 방향 전환 지연. 목표 속도를 넘지 않음 | `MovementInertia.luau` |
| 보폭 시계 | 속도로 발 위상을 계산(걷기 4.6, 질주 6.8 스터드/보폭). 게임의 애니메이션 마커로 대체 가능 | `StrideClock.luau` |
| 설정·프리셋 | `Off` / `Subtle` / `Default` / `Strong`, 범위 검사 | `Settings.luau` |
| Roblox 어댑터 | 카메라 피드백 방지, 에임·시야·HUD 카메라, 몸·시야·뷰모델 레이어, 발소리 이벤트 | `Controller.luau` |

모든 거리는 **스터드**, 각도는 **라디안**(설정 이름이 `Deg`로 끝나면 도)입니다. Roblox 좌표계를 그대로 씁니다.
## 폴더 구조

```
roblox/
├── default.project.json          Rojo 프로젝트
├── docs/BODYCAM_DESIGN.md        Bodycam 동작 명세서 (재설계의 근거)
├── src/
│   ├── Bodycam/                  ReplicatedStorage.Bodycam: 재설계한 바디캠 (Controller만 Roblox API 사용)
│   ├── FPSys/                    ReplicatedStorage.FPSys: 카메라 효과, 기울이기, 발소리
│   ├── FPSController/            ReplicatedStorage.FPSController: 이동 상태, 무기, 반동
│   ├── Presentation/             ReplicatedStorage.Presentation: 흔들림, 프리 에임, 화면 효과
│   ├── Realism/                  ReplicatedStorage.Realism: 1인칭 몸, 시선 복제 (MPL-2.0)
│   ├── Client/FPSClient.client.luau       예제 LocalScript (모든 패키지를 연결)
│   └── Server/FPSServer.server.luau       태그 지정과 룩 앵글 중계
├── THIRD-PARTY-NOTICES.md        포팅한 오픈소스의 라이선스 전문
├── tests/                        Roblox 밖에서 도는 테스트, 골든 트레이스
└── tools/
    ├── golden/sway_noise.c       SPT-SWAY 노이즈 해시 골든 값 생성기
    ├── luau-runner/              테스트용 최소 Luau 실행기 (Rust + mlua)
    └── rbxm_dump.py              .rbxm 모델에서 스크립트 소스를 꺼내는 도구
```

`Bodycam`의 계산 모듈은 Roblox API를 쓰지 않으므로 어떤 Luau VM에서도 돕니다.
Roblox에 묶이는 파일은 `Controller.luau`(서비스)와 `Aim.luau`(CFrame만)입니다.

## 설치

### Rojo 사용

1. [Rojo](https://rojo.space)를 설치합니다.
2. 이 폴더에서 다음 명령을 실행합니다.

```bash
rojo serve roblox/default.project.json
```

3. Roblox Studio의 Rojo 플러그인에서 Connect를 누릅니다.
   `ReplicatedStorage`에 `Bodycam`, `FPSys`, `FPSController`, `Presentation`, `Realism`이, `ServerScriptService`에
   `FPSServer`가, `StarterPlayerScripts`에 `FPSClient`가 생깁니다.
4. 업로드한 원본 FPSys 모델은 넣지 마세요. 이 프로젝트의 `FPSys`가 그것을 대신하고,
   둘을 같이 넣으면 카메라와 RemoteEvent가 두 번 처리됩니다.

### 수동 복사

1. `ReplicatedStorage`에 ModuleScript `Bodycam`을 만들고 `src/Bodycam/init.luau` 내용을 붙여 넣습니다.
2. 그 아래에 `src/Bodycam`의 나머지 파일마다 같은 이름의 ModuleScript를 만들어 붙여 넣습니다(확장자 제외한 이름).
3. 하위 폴더가 있는 패키지는 같은 구조의 Folder를 만듭니다.
4. `FPSys`, `FPSController`, `Presentation`, `Realism`도 같은 방법으로 만듭니다(`init.luau`가 패키지 ModuleScript, 나머지는 그 자식, 하위 폴더는 Folder).
5. `ServerScriptService`에 Script를 만들고 `src/Server/FPSServer.server.luau` 내용을 붙여 넣습니다.
6. `StarterPlayerScripts`에 LocalScript를 만들고 `src/Client/FPSClient.client.luau` 내용을 붙여 넣습니다.

## 사용법

```lua
local Bodycam = require(game.ReplicatedStorage.Bodycam)

local controller = Bodycam.controller().new({
	walkSpeed = 16,          -- 평상시 WalkSpeed (보폭·흔들림 기준)
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
controller:kickAim(0, math.rad(0.6))   -- 반동: 에임을 위로 0.6° (래그 없이)
controller:setWeaponProfile({ x = 0.05, y = -0.03, z = 0 }, { x = 0, y = 2, z = 0 })  -- 무기별 힙 오프셋(스터드, 도)
controller:applyPreset("Subtle")  -- "Off" | "Subtle" | "Default" | "Strong"
```

**사격 방향은 `controller:getAimCFrame()`을 쓰세요.** 화면(`getViewCFrame()`)은 래그와 흔들림 때문에 조준선과 다릅니다.
무기는 에임 기준 HUD 카메라(`getHudCFrame()`)에 붙으므로 총이 먼저 가고 화면이 뒤따릅니다.

### 설정 바꾸기

설정은 `그룹.이름` 경로로 바꿉니다(`lookLag`, `head`, `weapon`, `inertia`, `stride`).

```lua
local Settings = Bodycam.Settings
Settings.set(controller.config, "lookLag.maxLagDeg", 5)   -- 범위 밖 값은 잘립니다
Settings.set(controller.config, "head.bobEnabled", false)
for _, s in Settings.list() do print(s.path, s.default, s.min, s.max) end  -- 옵션 UI 만들기용
print(controller:featureEnabled("HeadBob"))
```

### 어댑터 없이 코어만 쓰기

자체 카메라 시스템이 있으면 `BodycamCore`를 매 프레임 직접 호출하고 결과 숫자를 적용하면 됩니다.

```lua
local core = Bodycam.BodycamCore.new(Bodycam.Settings.default())
local out = core:update({
	dt = dt, yaw = yaw, pitch = pitch, ads = adsBlend, aiming = aiming,
	grounded = true, sprinting = false, crouching = false,
	velX = v.X, velY = v.Y, velZ = v.Z, walkSpeed = 16, sprintSpeed = 25.6,
})
-- out.viewYaw / viewPitch / lagX..Z (시야), out.headX..Z / headPitch / headRoll (머리),
-- out.weaponX..Z / weaponPitch / weaponYaw / weaponRoll / wristPitch / wristRoll (무기), out.footstep
```

### 카메라 피드백 방지

Roblox 기본 카메라 스크립트는 다음 프레임을 만들 때 `Camera.CFrame`을 다시 읽습니다.
그래서 컨트롤러는 다음 순서로 동작합니다.

1. `RenderPriority.Camera - 1`: 지난 프레임의 **에임**으로 카메라를 되돌립니다. 이때 대기 중인 `kickAim`도 적용합니다.
   카메라가 우리가 쓴 시야 그대로일 때만 되돌리고, 다른 스크립트가 카메라를 옮겼다면 그 값을 새 에임으로 받아들입니다.
2. `RenderPriority.Camera`: 카메라 스크립트가 에임을 돌립니다.
3. `RenderPriority.Camera + 1`: 새 에임을 읽어 바디캠 **시야**와 HUD 카메라를 만들고 뷰모델을 놓습니다.

## FPSys 통합

업로드한 `FPSys.rbxm`에는 클라이언트 모듈(카메라·룩 앵글·발소리 컴포넌트), `Spring`, `Maid`,
서버 스크립트가 들어 있었습니다(`tools/rbxm_dump.py`로 꺼냈습니다). 기능과 설정값은 그대로 두고
바디캠 위에서 함께 돌도록 다시 짰습니다. 설정은 `FPSys/Config.luau` 한 곳에 있습니다.

| 컴포넌트 | 하는 일 | 바디캠과의 연결 |
| --- | --- | --- |
| `CameraEffects` | 걷기 흔들림, 숨쉬기, 좌우 이동 기울기, 마우스 룩 스웨이, 착지 충격, 모션 블러, **사격 카메라 흔들림(새)** | `BodycamController:addViewModifier`로 바디캠 시야 위에 얹힘 |
| `CameraEffects` 기울이기 (새) | 롤 + 벽 앞에서 멈추는 옆 이동 | `addBodyModifier`: 시야와 총이 함께 기울어짐 |
| 시선 회전 | `Realism` 패키지가 담당 ([Realism](#realism-character-realism-포팅)) | 1인칭 판정에 바디캠 에임 사용 |
| `Footsteps` | 바닥 재질별 발소리 | 로컬 캐릭터는 바디캠 보폭의 발 접지 시점에 소리를 냄 |
| `FPSServer` | Humanoid에 `RealismHook` 태그, 룩 앵글 중계 | — |

`Config.Overlap = "Auto"`(기본값)이면 바디캠이 이미 하는 움직임은 FPSys에서 끕니다.
풋스텝 카메라가 켜져 있으면 FPSys 걷기 흔들림을, 바디캠 머리 롤이 켜져 있으면 좌우 기울기를 끕니다.
둘 다 쓰려면 `"All"`로 바꾸세요.

### 원본 FPSys에서 고친 문제

- **룩 앵글이 전혀 동작하지 않았습니다.** 매 프레임 Motor6D를 돌리는 부분이
  `-- Process rotator updates...` 주석만 있는 빈 루프였습니다. 원본 Character-Realism을
  다시 포팅해 대체했습니다([Realism](#realism-character-realism-포팅)).
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
- 서버가 이벤트를 제한 없이 모두에게 중계했습니다. 플레이어당 0.04초 간격 제한과 무한대 값 거부를 넣었습니다.

FPSys는 [Character-Realism](https://github.com/MaximumADHD/Character-Realism)(MPL-2.0)에서 파생된 모델입니다.
설정값, 서버 스크립트, `RealismHook` 태그가 원본과 같습니다. 그래서 그 파생 파일
(`FPSys/Config.luau`, `FPSys/Footsteps.luau`, `Server/FPSServer.server.luau`)에 MPL-2.0 헤더를 달았습니다.
`Spring`과 `Maid`는 Quenty의 Nevermore Engine 모듈(MIT 라이선스)이라 헤더에 출처만 달고 그대로 두었습니다.

## Presentation (오픈소스 포팅)

조사에서 고른, **상업적으로 쓸 수 있는 라이선스**의 오픈소스를 포팅했습니다.
비상업 라이선스(CC BY-NC 계열, Shadertoy 기본 라이선스 등)와 라이선스가 없는 코드는 제외했습니다.
라이선스 전문은 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)에 있습니다.

| 기능 | 원본 | 라이선스 | Luau | 포팅 방식 |
| --- | --- | --- | --- | --- |
| 생리적 무기 흔들림: 호흡(들숨·날숨·멈춤 파형), 심장박동(수축기 + 중복맥), 8~12Hz 손떨림, 자세 드리프트, 관성 진동, 숨 참기와 반동, 스트레스(피격·근접 탄·폭발) | [savannt/SPT-SWAY](https://github.com/savannt/SPT-SWAY) `SwaySources.cs`, `Stress.cs`, `SwayDirector.cs`, `Util/*` | MIT | `Presentation/Sway/` | 1:1. 기본값은 원본 `SwayConfig.cs` 값. EFT 스프링 연동과 인게임 보정은 Roblox에 해당하는 것이 없어 제외 |
| 프리 에임 데드존: 총이 시야보다 먼저 돌고 최대 5°에서 멈춤, 조준 시 중앙 복귀 | [lualeet/sptarkov-deadzone](https://github.com/lualeet/sptarkov-deadzone) `DeadzonePatch.cs` | Unlicense | `Presentation/Deadzone.luau` | 1:1 (Unity 쿼터니언 규칙 포함, `UnityQuaternion.luau`) |
| 손으로 든 카메라 흔들림 | [pmndrs/drei](https://github.com/pmndrs/drei) `CameraShake.tsx` + [three-stdlib](https://github.com/pmndrs/three-stdlib) `SimplexNoise` | MIT | `Presentation/HandheldShake.luau` | 1:1. 바디캠용 작은 값 프리셋 추가 |
| 필름 그레인, 비네팅 | [three.js](https://github.com/mrdoob/three.js) `rand`, `FilmShader`, `VignetteShader` | MIT | `Presentation/ScreenEffects/ShaderMath.luau` | 수식 1:1, 이미지로 구워 오버레이 |
| 스캔라인, 렌즈 왜곡 수식, 눈 적응(자동 노출) | [pmndrs/postprocessing](https://github.com/pmndrs/postprocessing) `scanlines.frag`, `vignette.frag`, `lens-distortion.frag`, `adaptive-luminance.frag` | Zlib | 같은 파일 | 수식 1:1 |
| 벽에 닿으면 총 당기기 | 상업용 원본 없음 | — | `Presentation/WallPullback.luau` | 직접 작성 (Spherecast + 임계 감쇠 스프링) |
| 바디캠 HUD, 색보정, 유리 렌즈(실험), 환경 밝기 측정 | 상업용 원본 없음 | — | `Presentation/ScreenEffects/init.luau` | 직접 작성 |

`WeaponPresenter`가 이것들을 바디캠에 연결합니다.

- 흔들림의 무기 몫과 데드존은 **뷰모델과 사격 방향** 둘 다를 돌립니다. 총알은 화면 중앙이 아니라 총구 방향으로 나갑니다.
- 흔들림의 카메라 몫(원본 기본 10%)과 손떨림은 시야만 움직입니다.
- 총구가 벽에 막히면 총을 뒤로 빼고 들어 올리며, 85% 이상 막히면 사격이 막힙니다.
- 질주하면 지치고(exertion), 쉬면 회복합니다. 심박·호흡이 따라서 변합니다. 원본은 EFT 스태미나를 읽으므로 이 지침 모델은 직접 작성했습니다.

```lua
local Presentation = require(game.ReplicatedStorage.Presentation)
local presenter = Presentation.weaponPresenter().new({ controller = controller })
presenter:start()
presenter:onDamaged(0.2)      -- 피격: 심박·호흡·손떨림이 올라감
presenter:onNearMiss(3)       -- 3 스터드 옆으로 탄이 지나감
local screen = Presentation.screenEffects().new({ getViewCFrame = function() return controller:getViewCFrame() end })
screen:start()
```

### 화면 효과가 원본 셰이더와 다른 점

Roblox에는 커스텀 후처리 셰이더가 없고, 스크립트가 렌더링된 화면을 읽을 수도 없습니다.

- **비네팅:** three.js의 Eskil 비네팅은 `mix(장면, 회색, k)`라서 알파 오버레이와 수학적으로 같습니다. 이미지로 구워 정확히 재현합니다.
- **그레인:** 원본은 장면 색에 비례해 밝게 만듭니다(`base * noise`). 오버레이는 장면 색을 모르므로, 같은 노이즈 패턴을 옅은 알파 오버레이로 씌웁니다. 타일 6장을 24fps로 돌립니다.
- **스캔라인:** 원본은 OVERLAY 블렌드입니다. ImageLabel은 블렌드 모드가 없어 어두운 줄무늬 알파로 근사합니다(기본 꺼짐).
- **자동 노출:** 장면 밝기를 읽을 수 없어, 레이캐스트로 하늘이 보이는 정도와 햇빛 여부를 측정해 밝기로 씁니다. 그 값에 Pattanaik 적응(pmndrs)을 적용해 `ExposureCompensation`을 움직이고, 바디캠 같은 작은 노출 흔들림을 더합니다.
- **렌즈 왜곡:** 불가능합니다. 대신 카메라 앞에 Glass 재질 구를 두는 실험 기능이 있습니다(기본 꺼짐). 그래픽 품질 8 이상에서만 보이고, 반투명 물체·파티클·데칼이 그 뒤로 안 보이는 한계가 있어 Studio에서 값을 조정해야 합니다.
- EditableImage를 만들 수 없는 환경에서는 그 효과만 꺼지고 나머지는 동작합니다.
- **HUD:** 가상의 기기 이름("BODYCAM BC-0451")을 씁니다. 실제 바디캠 제조사의 이름·로고·레이아웃을 흉내 내지 마세요(상표 문제).

## Realism (Character-Realism 포팅)

[MaximumADHD/Character-Realism](https://github.com/MaximumADHD/Character-Realism)(MPL-2.0)을 포팅했습니다.

- **1인칭 몸 보이기:** PlayerModule의 BaseCamera·TransparencyController 함수를 바꿔 끼워, 머리와 모자만 숨기고 몸은 보이게 합니다.
- **시선 회전:** 머리·상체·팔이 보는 방향으로 돌고 RemoteEvent로 복제됩니다. Animator가 쓴 뒤 `Stepped`에서 `Transform`에 곱하므로, **Avatar Joint Upgrade(AnimationConstraint, `C0` 읽기 전용) 리그에서도 동작합니다.** 이전 FPSys 포팅은 `C0`를 써서 이 리그에서 깨졌습니다.
- **원본에서 고친 것:**
  - 도구를 든 팔 규칙이 모든 캐릭터에 로컬 플레이어의 각도를 썼습니다. 이제 각 캐릭터의 각도를 씁니다.
  - "변경 여부" 검사가 캐릭터의 모든 모터에 값 하나를 공유해서, 애니메이션이 재생 중이면 회전이 튀었습니다.
- **바디캠 통합:**
  - 1인칭 판정과 시선 계산은 시야가 아니라 바디캠 에임을 씁니다.
  - 원본의 "카메라를 머리에 붙이는" 마지막 렌더 단계는 바디캠 시야 오프셋을 지워서 기본으로 꺼 두었습니다(`FirstPerson.snapCameraToHead`).
- **MPL-2.0 의무:** 이 폴더의 파일(과 FPSys의 파생 파일)은 수정해도 MPL로 남고 소스를 공개해야 합니다. 게임의 나머지 코드에는 적용되지 않습니다. 원작자는 **게임 설명이나 크레딧에 CloneTrooper1019를 표기**해 달라고 요청합니다.

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

반동은 `controller:kickAim`으로 들어갑니다. 에임을 돌리면서 바디캠 래그도 같은 양만큼 옮기므로,
반동은 래그나 마우스 움직임으로 취급되지 않습니다(시야가 뒤처지거나 손목이 휙 돌지 않음).

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
| Left Alt | 숨 참기 (조준 중 흔들림 감소, 놓으면 반동) |

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

- `tests/bodycam.spec.luau`(재설계 바디캠):
  - 룩 래그: 데드밴드, 콘 제한, 수렴, 조준 시 조여짐, 반동이 즉시 반영되는지.
  - 머리: 걸음 흔들림 주기(보폭당 두 번 내려감)·속도 비례·정지 시 안정, 롤 부호, 질주 숙임, 착지 충격 복귀.
  - 무기: 앞으로 걸을 때만 내림, 사격·조준 중 억제, 반동 방향과 안정, 손목 롤 부호, 상한.
  - 그 밖에: 이동 관성, 설정·프리셋, 목 클라이언트에서의 컨트롤러 프레임 순서·HUD 카메라·발소리 이벤트·`kickAim`.
- `tests/fpscontroller.spec.luau`: 보행 속도 전환, 질주 조건, 앉기·일어서기 공간, 슬라이드, 엎드리기 잠금,
  점프·착지 이벤트, 600 RPM 자동 사격 발수, 단발·점사, 탄창·장전, 스코프 FOV, 반동 상승과 복귀 비율.
- `tests/fpsys.spec.luau`:
  - Realism 시선 계산과 관절 회전.
  - Motor6D와 AnimationConstraint(읽기 전용 `C0`) 리그 모두에서 `Transform`으로 도는지, 보낸 플레이어의 캐릭터에 적용되는지.
  - 1인칭 몸 보이기 규칙과 PlayerModule 함수 교체.
  - 재질 발소리, 숨쉬기·착지 충격·겹침 규칙, 기울이기 벽 정지, 사격 흔들림.
- `tests/presentation.spec.luau`:
  - SwayNoise 해시가 C# 부호 없는 32비트 연산과 일치하는지(C 골든, `tools/golden/sway_noise.c`).
  - 흔들림이 평균 0인지, 피로·스트레스에 따른 심박, 숨 참기와 반동, 관성 지연과 안정.
  - Unity Euler 규칙, 데드존, simplex 노이즈, 손떨림.
- `tests/screen.spec.luau`: three.js·pmndrs 셰이더 식 대조, 목 클라이언트에서 오버레이 픽셀, 노출, HUD, 정리.
- `tests/integration.spec.luau`: 모든 패키지를 함께 띄운 목 클라이언트에서 확인합니다.
  - 걷기 발소리, 조준 FOV·감도, 자동 사격 반동이 에임을 올리고 복귀하는지.
  - 질주 중 사격·조준 금지, 기울이기, 앉기 카메라 높이, 무기 교체.
  - 총알이 총구 방향으로 나가는지, 뷰모델이 그 방향을 가리키는지, 벽에 막히면 사격이 막히는지, 정리 후 남는 연결이 없는지.

현재 전체 80개 테스트가 통과합니다. simplex 노이즈는 원본 TypeScript를 직접 실행해 대조하지
못했습니다(외부 코드 실행이 이 세션에서 허용되지 않음). 대신 성질 테스트로 확인했습니다.

### 테스트 실행

Rust가 필요합니다(Luau VM은 `mlua` 크레이트에 포함되어 빌드됩니다).

```bash
cargo build --release --manifest-path roblox/tools/luau-runner/Cargo.toml
```

```bash
cd roblox && tools/luau-runner/target/release/luau-runner tests/run.luau
```

SwayNoise 골든 값을 다시 만들려면 C 컴파일러가 필요합니다.

```bash
roblox/tools/golden/build.sh
```

## 한계와 주의할 점

- 앉기와 엎드리기는 속도, 점프, 카메라 높이만 바꿉니다. 캐릭터 애니메이션은
  `MovementController:onEvent`(`Crouched`, `ProneStarted`, `SlideStarted` 등)에 연결해 재생하세요.
  Kinemation 프레임워크의 IK 모션 레이어와 애니메이션 블렌딩은 유료 패키지에 있어 포함하지 않았습니다.
- `MovementController`와 바디캠 `applyMovementInertia`는 둘 다 이동을 바꾸므로 하나만 켜세요(예제는 전자).
- **Roblox Studio에서 실행해 보지 않았습니다.** 모든 파일이 Luau 컴파일러를 통과하고,
  어댑터는 목 서비스로 테스트했지만, 실제 Studio 카메라 스크립트와의 상호작용과 체감 튜닝은
  Studio에서 확인해야 합니다. `luau-analyze` 타입 검사도 돌리지 않았습니다.
- 뷰모델은 에임 기준 HUD 카메라(머리 움직임 위치와 롤만 공유)에 붙어서, 총이 먼저 가고 화면이 뒤따릅니다.
  기울이기와 자세 낮추기는 `addBodyModifier`로 시야와 총에 함께 적용됩니다.
- 손목 롤은 그립 피벗(`gripPivot`, 기본 카메라 기준 (0.35, -0.35, -1.1) 스터드)으로 뷰모델 전체를 돌립니다.
  손·팔 본을 따로 움직이려면 `controller.core` 출력(`wristPitch`, `wristRoll`)을 직접 Motor6D나 IKControl에 적용하세요.
- `applyMovementInertia = true`는 ControlModule 뒤에서 `Humanoid:Move`를 덮어씁니다.
  자체 이동 시스템이 있으면 `false`로 두세요.
- 기본값은 Roblox 표준 아바타(걷기 16 스터드/초) 기준으로 정했습니다. 다른 축척이면 프리셋과 설정으로 조정하세요.

### 라이선스

- `Bodycam`: 비상업 라이선스(이 저장소의 [License.txt](../License.txt))가 걸린 X-Ray 소스를 옮긴 이전 포팅을 지우고,
  동작 명세([docs/BODYCAM_DESIGN.md](docs/BODYCAM_DESIGN.md))만으로 새로 구현했습니다. 원본 코드·상수·녹화 데이터는
  들어 있지 않습니다. 다만 명세서를 쓴 쪽이 원본을 본 상태라 엄격한 클린룸은 아니므로, 상업 출시 전 법률 검토를 권합니다.
  이 저장소의 나머지(`src/`, `gamedata/`의 C++ 엔진)는 여전히 License.txt를 따릅니다.

- `FPSys`: 업로드한 모델은 Character-Realism(MPL-2.0)에서 파생되었습니다. 파생 파일은 MPL-2.0이고,
  CloneTrooper1019 크레딧이 필요합니다. 그 안의 `Spring`, `Maid`는 Nevermore Engine(MIT)입니다.
- `Presentation`, `Realism`: MIT, Zlib, Unlicense, MPL-2.0 코드의 포팅입니다. 모두 상업적 사용이 허용되며,
  배포할 때 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)의 고지를 함께 두어야 합니다.
- `FPSController`: 이 프로젝트에서 새로 작성했습니다. Kinemation 코드는 쓰지 않았습니다.
  Kinemation의 실제 애니메이션 시스템이 필요하면 유니티 에셋 스토어 라이선스가 필요하고,
  그 C# 코드를 Roblox용으로 옮겨 배포하려면 Kinemation의 허락이 따로 필요합니다.
