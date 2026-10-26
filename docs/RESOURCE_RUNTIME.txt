# Resource 자산의 게임 런타임 적용 — 2026-10-21

## 변경 범위
현재 3D 화면에서 사용하는 캐릭터·몬스터·건물·환경·생활 소품의 모델 경로를
Resource의 GLB로부터 준비한 메시로 교체했다. 튜토리얼도 같은 메시 라이브러리를 사용한다.
원본 Resource의 Blender/GLB/PNG 파일은 보존했다. 게임용 데이터는 SimpleGame/Data/Resources에 둔다.
이번 작업에서 실행한 것은 로컬 자산 변환 도구이며 게임 빌드·실행·테스트·화면 검증은 하지 않았다.

## 모델 대응
| 기존 용도 | Resource 적용 |
|---|---|
| 플레이어 | Pilgrim 프리셋 |
| 기사단 경비·지도자 | Warden |
| 의무관 | Healer |
| 전령·상인 | Scavenger |
| 일반 주민·기록관 | Body_7H / Pilgrim |
| 튜토리얼 감염 생존자 | InfectedWanderer |
| 야외 3종 | Weak_HollowThrall / Danger_RibBrute / Weak_AshWeaver |
| 군체 돌격·포자·파종·지도자 | ThornHound / ThornWeaver / BroodMatriarch / CrownedAbomination |
| 성벽·문·기둥·폐허 | Wall_Ruined_2m / Arch_Ruined_2m / Post_2m4 등 |
| 건물 | Floor + Wall/Doorway/Window + Roof + Chimney 조립 |
| 군체 구조물 | Colony_Large + Mycelium_Roots + Egg_Cluster |
| 침상·의무실·작업 | Bed_Straw / Table / BloodVial / Lantern |
| 창고·운송 화물 | Crate/Barrel/Sack 조합 |
| 나무·풀·늪 | Tree_Dead_A / Tree_Infected / Grass_Dry_Clump / Grass_Reeds |
| 잔해·묘역·시체 | Rubble_Cluster / Rock_Spire / Death 마지막 자세 |

Cart·Grave 등 기존 ID는 호환성을 위한 이름이며 제공 자산 중 용도에 가까운 조합으로
대응했다. 도형 갑옷용 5개 기존 ID는 빈 프리팹으로 유지해 완성 의상과 중복 렌더링하지 않는다.
기존 2D 카탈로그/원본과 Models3D/*.mesh는 삭제하지 않았지만 새 3D 로더는 도형 파일을 읽지 않는다.
models3d.catalog는 기존 모델 ID 순서 확인에 사용한다.

## 배치와 지형
- 기존 마을 배치와 이동 판정을 유지하고 건물 프리팹을 외부 모듈 조합으로 교체했다.
- 기사단 거점에는 보급 상자·통과 주변 마른 풀을, 군체/습지에는 알 군집·균사·갈대를 추가했다.
- 작업대·약품·시장 적치물·운반 상자도 실제 리소스 메시를 사용한다.
- 12개 침상을 작은 도형 기준 원형으로 겹쳐 놓지 않도록 활동 자리를 격자와 침상 간격에 맞췄다.
- 새 모델의 범위에 맞춰 NPC·적·시설·소품의 3D 컬링 경계를 계산한다.
- 이동 지형의 연결성과 높이는 기존 Terrain3D로 유지하고 석재·묘토·오염토의 UV 재질을 적용한다.
- 물결·불꽃·입자·공격 예고·오염 지대는 기존 셰이더/효과 기하를 유지한다.
  이들은 콘텐츠 캐릭터나 건물의 대체 모델이 아니다.
- 건물 문은 시각적 개구부이며 이번 작업이 실내 탐색·정밀 삼각형 충돌 구현을 의미하지 않는다.

## 데이터 파이프라인
```
Resource/*.glb + Materials/*.png
  -> tools/prepare_resource_models.py
  -> Data/Resources/Meshes/*.rmesh + Textures/*.png + models.catalog
  -> ResourceLibrary3D -> MeshCache3D -> Actor/Scene -> WorldRenderer3D
```

변환 결과는 메시 79개와 게임용 BaseColor/Normal/Roughness PNG 66장이다.
Height 원본은 보존하며 현재 런타임 변위에는 사용하지 않는다.
변환은 Python 표준 라이브러리로 GLB의 접근자·노드 변환·스킨·애니메이션을 읽는다.
Source SHA256이 같은 메시의 재변환은 건너뛴다. 게임에는 Python/Blender/LLM 연결이 필요 없다.
Source가 바뀌면 다음 제작 명령으로 외부 데이터를 갱신한다.

```
python tools/prepare_resource_models.py
```

Resources/models.catalog 레코드:
```
asset 원본ID Resources/Meshes/파일.rmesh
prefab 기존모델ID 기본Yaw도 균일스케일
part 프리팹ID 원본ID X Y Z Yaw도 ScaleX ScaleY ScaleZ
freeze 프리팹ID 클립명 정규화시각
bind 배치소품ID 프리팹ID
```

빌드의 기존 CopyGameContent가 Data/Resources 전체를 실행 파일 옆 Data로 복사한다.
이번에는 새 C++ 로더·렌더러 변경이 있으므로 사용자 빌드와 프로그램 재시작이 필요하다.
모델/텍스처만 예전 실행 파일의 Data에 복사하는 것으로 새 렌더링 경로가 활성화되지는 않는다.

## 렌더링과 애니메이션
- UV·본 인덱스·가중치·재질 응답을 포함한 정점을 GPU에 한 번 올린다.
- 원본 이동/회전/스케일 키를 24Hz로 준비하며 쿼터니언 키 샘플에는 SLERP를 사용한다.
- 런타임은 공유 본 행렬 팔레트 두 프레임을 GPU에서 선형 보간해 스키닝한다.
  완전한 런타임 로컬 TRS 블렌드 트리나 IK는 이번 범위가 아니다.
- Idle/Walk/Run/Attack/Jump/Hit/Death와 Wind/Pulse를 상태에 연결한다.
  기타 원본 클립도 데이터에 보존한다. 완성된 모든 전환·발 접지를 검증한 것은 아니다.
- 이동량과 원본 리그의 보행 기준 속도·루트 스케일을 사용해 주기를 계산한다.
  소품/프리팹 크기와 실제 개체 스케일도 반영하며 전환·경사면의 완전한 발 접지는 미검증이다.
- 사망의 시각적 쓰러짐은 원본 클립에 맞춰 약 1.6초로 연결한다. 체력·처치 판정은 유지한다.
- 공격 준비와 타격 이후 진행을 원본 Attack의 예고/접촉/복귀 구간으로 매핑한다.
  전투 판정과 데미지 규칙은 기존 게임 상태가 담당한다.
- CPU에서 개체마다 변형된 정점 배열을 다시 만들지 않는다. 같은 메시·본 데이터는
  공유하고 변환·색·클립 위치를 인스턴스별로 전달한다.
- 기존 모델별 제출 루프를 메시별 인스턴스 배치로 바꾸어 같은 벽/소품/NPC 모델을 묶는다.
- BaseColor는 sRGB, Normal/Roughness는 선형 배열 텍스처다. 밉맵과 지원 시 이방성 필터를 사용한다.
- 그림자 패스도 동일한 본 팔레트로 변형하며 기존 PCF·후처리·클립 규칙을 유지한다.
- 게임/첫 레벨의 메시 라이브러리는 공유하며 기존 CacheRead/ModelDraw/World3D/GPU 계측을 재사용한다.

## 사용자 확인 항목
빌드·실행·테스트·FPS 측정은 수행하지 않았다. 사용자는 다음을 확인한다.
1. 시작/첫 맵 진입에서 Resources 파일 누락 또는 셰이더/텍스처 오류가 없는지.
2. 플레이어·직책별 주민·군체의 크기/정면/무기 위치와 이동·공격·피격·사망 자세.
3. 건물·상자·침상·군체 소품이 통로와 상호작용을 가리지 않는지.
4. UV 재질·명암·그림자·한글 UI와 기존 후처리의 화면 가독성.
5. 같은 해상도/실행 구성에서 프로파일을 비교해 GPU 스키닝·그림자·AI 부하 확인.
새 배치 데이터의 콘텐츠 서명 변경으로 이전 저장과 맞지 않을 수 있으며 기존 저장을 삭제하지 않았다.
