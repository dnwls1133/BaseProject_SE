# 프로파일 기반 성능·전투·교역 수정 — 2026-11-11

상태: 코드 반영, 빌드·실행·테스트 및 수정 후 성능 측정 전. 사용자 직접 검증 원칙을 유지한다.
근거는 profile-2026-11-11-7-42-13-19048-0.jsonl과 같은 실행에서 저장된 WorldMonitor 스냅샷이다.
수정 전 Debug/Intel UHD/1280×800/MSAA 4배에서 약 28초 구간 평균 46.1 FPS였으며,
오염 갱신은 한 번에 174~217ms를 사용했다. 해당 수치는 수정 후 결과가 아니다.

## 1. 오염 확산

- SocietyConflict::PrepareConflictTopology에서 셀 중심의 CanStand와 인접 셀 사이 CanTravel을 기존 함수로 한 번 계산한다.
- 지역별 반경 15 이내 셀 인덱스도 생성한다. 반복 틱은 연결 마스크와 인덱스를 읽고 기존 밀도·화염·소각 흔적 규칙을 적용한다.
- ReconcilePositions에서 새 맵/재생성/복원마다 파생 캐시를 다시 만든다. 반복 틱의 복사 버퍼도 재사용한다.
- 현재 통행 지형은 맵 생성 후 고정이다. 향후 통행 타일·충돌 구조를 변경하면 같은 캐시 재생성 경로를 호출해야 한다.
- 캐시는 세이브 상태가 아니다. 오염 밀도·흔적·화염은 기존 저장 원본을 유지한다.
- 새 지표: cpu_contamination_topology_build_ms, contamination_topology_build_count.
  반복 비용은 기존 cpu_contamination_update_ms로 비교한다. 최초 연결 계산 비용은 로딩으로 이동한다.

## 2. GPU 렌더링

- WorldShaders3D의 ShadowVertex는 기존과 같은 스킨 위치·월드 변환·변위·알파/재질 판정을 사용한다.
  그림자 깊이 계산에 필요 없는 스킨/모델 법선 역행렬, 조명용 varying 계산을 제외한다.
- WorldRenderer3D는 불투명 리소스 인스턴스와 메시 배치를 카메라 가까운 순서로 제출한다.
  기존 깊이 검사와 블렌딩 비활성 상태를 유지해 가려지는 픽셀의 처리를 줄이는 방향이다.
- 원본 메시·재질·MSAA 4배 상한·2048 그림자 맵·PCF·양면 천/풀 렌더링을 유지한다.
- 인스턴싱은 기존부터 사용한다. 이번 변경이 draw call 감소나 60 FPS 달성을 보장하는 것은 아니다.
- 기존 gpu_world_ms, gpu_shadow_ms, gpu_frame_ms와 cpu_world_3d_ms를 같은 카메라/해상도/빌드 구성에서 비교한다.

## 3. 전투 이동과 교역 실행

### 경로 요청의 공정한 처리

- 기존에는 Scene에서 앞선 NPC가 매번 먼저 공통 경로 예산을 사용할 수 있었다.
- MoveTo는 NPC별 중복 없는 요청을 FIFO 큐에 넣고 대기 중에는 최신 목표로 갱신한다.
- WorldSociety::Update가 먼저 온 요청부터 BuildNpcPath에 전달한다. 프레임당 최대 2회/2048 노드와 기존 시간 중단 규칙을 유지한다.
- 경로 생성은 사회 업데이트, 실제 NPC 이동은 기존 Actor/Scene 업데이트에서 한 번만 처리한다.
- 신규 요청은 다음 월드 업데이트부터 처리된다. 큐는 맵 재구성/복원 시 비우는 파생 상태다.
- npc_path_request_queue_count로 대기량을 관측한다. npc_path_budget_deferred_count는 이제 비동기 요청 대기도 포함한다.
- cpu_npc_path_ms는 그대로 유지하지만 호출 위치가 바뀌어 cpu_scene_update_ms에서 cpu_society_update_ms 쪽으로 포함 관계가 이동한다.

### 실제 거점으로 진격

- 전장 목표를 모든 적대 지역 중심에서 .world의 village로 정의된 적대 거점으로 한정한다.
  마을 정의가 없는 구형 맵은 기존 세력 본거지를 사용한다. 원격 NPC 위치나 체력을 새로 공유하지 않는다.
- 빈 적대 습지에 도착한 뒤 영구 대기하는 경우를 줄인다. 기사단 일반 병과는 진격하며 정화 병과(combatClass 3)가 주변 오염을 소각한다.
- 체력 25 미만 피격자의 후퇴 상태가 전장 갱신에서 즉시 공격으로 덮이지 않도록 한다.
- 진군은 생활 계획보다 우선하며, 원정 병력도 실제 소지 식량을 소비해 허기를 해소한다.
- faction_marching_unit_count와 faction_engaged_unit_count를 추가한다.
  active_war_count는 기존 캠페인 수이므로 두 세력 전장의 모든 교전을 나타내는 수치로 해석하지 않는다.

### 구매 담당자 접근 → 대면 수락 → 실제 운송

- 기존 목표 선택은 source/home, 수락은 destination과 구매자 근접을 요구해 조건이 불일치했다.
- NpcPlanning과 PlanEconomyOrder는 도착 마을의 현지 주문·담당자를 기준으로 후보를 평가한다.
- 인지 범위와 시야 안의 담당자에게 실제 이동하고 2.5타일 이내에서 설명을 들은 뒤 계약을 수락한다.
  접근만으로 재고 예약·금고 차감·화물 생성은 하지 않는다.
- 수락 후 기존 출고 담당자 대면 → 화물 인수 → 목적지 이동 → 인수 담당자 대면 → 입고/정산을 유지한다.
- 새 영속 필드를 만들지 않고 Supply 목표·기존 partner/cause/단계를 사용한다. 진행 계약·저장 스키마 9 형식을 유지한다.
- 후보가 없을 때 decisionTrace에 local, available, stock_partner_slot_eligible, positive_score를 남긴다.
- economy_briefing_approach_count, economy_contract_reserved_count, 기존 economy_delivery_count와 이벤트를 함께 비교한다.
- JSON NPC 상태에 combat_class, attack_pattern, path_remaining_points, path_retry_seconds, path_failure_count를 추가했다.
  기존 HTML/API가 읽는 필드는 보존한다.

## 4. 첫 프레임 폰트·텍스처 준비

- Renderer는 Nanum Gothic 파일을 크기별로 재독하지 않고 바이너리 일괄 읽기 1회와 공유 fontinfo를 사용한다.
  크기별 배율/측정값, 기존 라벨 텍스처 캐시와 글꼴은 유지한다.
- MeshCache3D는 같은 Data 경로의 끝 구분자·상대 경로 표기를 정규화해 리소스 라이브러리 공유 키로 사용한다.
- WorldMaterials3D는 PNG를 원본으로 유지하고 Data/Cache/Textures에 디코딩된 RGBA를 저장한다.
  캐시 헤더 버전 1, PNG 내용 해시, 픽셀 검증값, 정확한 크기를 검사하며 손상/불일치 시 원본에서 다시 만든다.
- 캐시 쓰기는 임시 파일과 원자 교체를 사용한다. 읽기 전용 환경의 캐시 쓰기 실패는 PNG 로딩 성공을 취소하지 않는다.
- 캐시는 배포/커밋/세이브 대상이 아니다. 최초 실행은 디코딩·캐시 생성 비용이 있고 이후 유효한 캐시를 재사용한다.
  일반 이미지 내용 변경은 자동 해시 갱신이며 디코딩/바이너리 해석을 바꾸면 TextureCacheVersion을 올린다.
- 기존 CacheRead 계측이 PNG 디코딩·GPU 업로드까지 포함해 첫 프레임 1.86초를 모델 캐시 비용으로 오해할 수 있었다.
  새 경로는 cpu_resource_texture_prepare_ms 아래 decode/cache_read/cache_write/upload를 분리한다.
- font_source_load_count는 정상 세션에서 1회를 기대한다. cpu_font_source_load_ms,
  texture_cache_hit_count/miss_count와 cpu_resource_texture_decode_ms를 함께 확인한다.

## 사용자가 확인할 순서

1. 같은 Debug/해상도에서 반복 오염 틱의 최대 지연과 collision 조회 수를 이전 로그와 비교한다.
2. 동일 화면에서 그림자·포즈·천/풀·가림·색을 확인하고 gpu_world_ms/gpu_shadow_ms를 비교한다.
3. 모니터에서 군체와 기사단 양측의 위치·경로·진군 대상이 바뀌는지, 접촉 후 실제 피해와 faction_attack_resolved_count가 발생하는지 확인한다.
4. 주문의 approach → reserved → loaded → delivery 흐름과 실제 재고·금고·운송료 변화가 연결되는지 확인한다.
   긴 이동·현장 인계가 있어 짧은 28초 실행만으로 모든 계약 완료를 요구하지 않는다.
5. 첫 실행과 재실행의 텍스처 cache hit/miss, 폰트 로딩 횟수를 비교한다.
6. 운송 중 저장/복원 후 물자·예치금의 중복 지급이 없는지 확인한다.

기존 saved content signature mismatch 파일은 삭제/변환하지 않았다. 저장 호환 오류는 성능 수정과 별도로 보존한다.
