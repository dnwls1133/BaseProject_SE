# 지속적 성능 프로파일링

2026-09-23. 구현 소스 기준 안내이며 빌드·실행·테스트·실측은 수행하지 않았다. 최신 구현에는 아래의 실측 기반 1차 최적화도 포함한다. 지형 GPU 캐싱, 텍스트 아틀라스, 인스턴싱, 타이머 주기 변경, 후처리 품질 변경은 아직 적용하지 않았다. 향후 이러한 변경의 전후 효과를 같은 지표로 비교한다.

## 사용 방법

- 기본적으로 실행 시 자동 활성화한다. 실행 파일 옆 `Logs/Performance/profile-<UTC 시각>-<PID>-0.jsonl`에 저장한다. 작업 디렉터리가 바뀌어도 로그 위치는 유지된다. 콘솔 시작 메시지에서 실제 경로를 확인한다.
- 첫 로딩은 startup 레코드로 분리한다. 플레이 중 모든 프레임을 집계하여 약 1초마다 window 레코드와 콘솔 요약을 만든다. 게임이 한 프레임에서 오래 멈추면 그 프레임이 끝난 뒤 지연이 포함된 기록을 남긴다. 크래시로 끝나지 않은 프레임은 기록할 수 없다.
- GPU 결과는 준비된 과거 프레임만 회수한다. GPU 미지원·대기 중에는 GPU 통계가 null이다. 이를 0ms로 해석하지 않는다.
- 환경변수 `HB_PROFILE=0`으로 시작하면 프로파일링을 비활성화할 수 있다. 별도 키 조작이나 설정 파일은 필요 없다. 일반 FPS/draw call의 매 프레임 콘솔 출력은 제거했으며, 활성 상태에서 1초 요약으로 대체한다.
- 정상 종료 시 남은 CPU 구간을 기록하고 출력 큐를 비운다. 미완료 GPU 결과를 기다리지 않으며 session_end 이벤트에 그 수를 남긴다. 마지막 GPU 샘플 일부는 종료 시 생략될 수 있다.
- 로그는 세션당 8MiB 파일 4개를 순환 사용한다. 파일이 가득 차면 같은 세션의 오래된 파트를 덮어쓴다. 전체 세션이 항상 보존되는 것은 아니며 과거 실행의 로그는 자동 삭제하지 않는다. 모든 파트의 session_id와 elapsed_ms로 시간순으로 정렬한다.
- AI 분석 시 같은 세션의 `.jsonl` 파일과 이 문서를 함께 제공한다. 파일은 Git에서 제외한다. 상태 비교에는 실행 구성, 화면 크기, 데이터, 맵 시드, 카메라 위치와 행동을 가능한 한 맞춘다.

## 기록 구조

JSONL은 한 줄마다 완전한 JSON 객체 하나이다. 영문 snake_case 키, 명시적 단위, schema_version=1을 사용한다.

- `session`: UTC 기반 세션 ID, Debug/Release, 프로파일러 컴파일 시각, 프로세스 비트 수, 논리 프로세서 수, GPU 제조사·렌더러·OpenGL 버전, GPU 타이머 지원 여부, 출력 제한과 측정 방식. swap_interval은 조회하지 않으므로 null이다. build_stamp는 Git 커밋이나 전체 콘텐츠 버전이 아니다.
- `startup`: 게임 로딩이 끝난 시점까지의 원시 누적 측정값. 플레이 FPS 통계에 로딩 시간을 섞지 않는다.
- `event`: viewport_resize, model_catalog(내용 해시·모델 수), model_cache(hit/miss_rebuild), world_generate(레벨 파일·시드), content_error, session_end. event.frame_id는 이벤트 발생 시점의 최근 BeginFrame ID다.
- `window`: 최근 구간의 프레임 범위, 실제 구간 길이, FPS, 프레임당 통계, 회수한 GPU 통계, 모델 종류별 합계, 최악의 완료 프레임, 프로세스 메모리·CPU, 로그 출력 상태.

`per_frame`의 각 항목은 samples/mean/p95/max/sum을 가진다. 함수 호출마다 따로 표본을 만들지 않고, 이전 프레임 종료 후 현재 프레임 종료까지의 호출 비용·횟수를 합산해 프레임 표본 하나로 만든다. 실행되지 않은 경로는 해당 프레임에서 0이다. 따라서 p95가 0이어도 드물게 실행되는 경로가 없다는 뜻은 아니다. max와 sum도 함께 확인한다.

p95는 관측 표본의 nearest-rank 95백분위다. 약 1초 표본 수로 계산하므로 긴 구간의 1% low나 엄밀한 최소 FPS가 아니다. 구간별 p95들을 단순 평균해서 전체 p95를 만들지 않는다. FPS는 완료 프레임 사이의 간격으로 계산하며 첫 프레임은 이전 완료 시각이 없으므로 간격 표본에서 제외한다.

`worst_present_frame`은 구간에서 프레임 간격이 가장 길었던 프레임 ID와 같은 시점의 전체 CPU/카운터 스냅샷이다. 모든 프레임 원본을 파일에 남기는 구조는 아니다. `over_budget_frame_count`는 1000/60ms 초과, `over_33ms_frame_count`는 1000/30ms 초과 횟수다.

## 주요 지표와 수정 위치

| 영역 | 주요 키 | 담당 코드 |
|---|---|---|
| 프레임·예약 | frame_present_interval_ms, cpu_update_ms, cpu_render_submit_ms, cpu_present_wait_ms, timer_lateness_ms, tick_interval_ms, update_tick_count, update_dt_clamp_count | SimpleGame.cpp |
| Scene Graph | cpu_scene_update_ms, cpu_scene_prepare_ms, cpu_scene_sort_ms, actor_updated_count, actor_visible_count, actor_culled_count, group_culled_count, actor_render_callback_count | Scene.cpp |
| 렌더 패스 | cpu_ground_pass_ms, cpu_shadow_pass_ms, cpu_world_pass_ms, cpu_effects_pass_ms, cpu_overlay_pass_ms, cpu_ui_pass_ms | Scene.cpp |
| 지형·지도 | cpu_ground_build_ms, ground_tile_scanned_count, ground_tile_visible_count, cpu_minimap_build_ms, minimap_tile_scanned_count | LevelOne.cpp |
| AI·게임 | cpu_player_update_ms, cpu_enemy_update_ms, enemy_update_count, cpu_navigation_ms, navigation_build_count, navigation_visited_tile_count, cpu_combat_ms | LevelOne.cpp, RandomLevelMap.cpp |
| 충돌·시야 | cpu_collision_ms, collision_query_count, cpu_line_of_sight_ms, line_of_sight_query_count | RandomLevelMap.cpp |
| 모델 | cpu_model_draw_ms, cpu_static_model_draw_ms, model_draw_count, model_transformed_vertex_count, static_model_transformed_vertex_count | ModelCache.cpp |
| 제출·전송 | cpu_batch_submit_ms, cpu_vertex_upload_ms, draw_call_count, geometry_draw_count, fullscreen_draw_count, submitted_vertex_count, vertex_upload_bytes, largest_batch_vertex_count | Renderer.cpp |
| 배치 분리 | flush_text_count, flush_clip_count, flush_postprocess_count, flush_frame_end_count, flush_label_eviction_count, flush_other_count | Renderer::Flush |
| 텍스트 | cpu_text_submit_ms, text_draw_count, cpu_label_rasterize_ms, label_cache_hit_count, label_cache_miss_count, label_cache_eviction_count, label_texture_upload_bytes, label_cache_estimated_bytes | Renderer::Text/GetLabel |
| 효과·후처리 | cpu_atmosphere_build_ms, cpu_msaa_resolve_submit_ms, cpu_postprocess_submit_ms, effect_quad_count, effect_bbox_clipped_estimated_pixels, mist_bbox_clipped_estimated_pixels, soft_light_bbox_clipped_estimated_pixels, flame_bbox_clipped_estimated_pixels | LevelOne.cpp, Renderer.cpp |
| GPU 구간 | gpu_world_ms, gpu_msaa_resolve_ms, gpu_bloom_horizontal_ms, gpu_bloom_vertical_ms, gpu_postprocess_ms, gpu_ui_ms, gpu_frame_ms | Renderer.cpp, Profiler.cpp |
| 로딩 | cpu_content_read_ms, content_read_bytes, cpu_level_definition_load_ms, cpu_world_generate_ms, cpu_terrain_generate_ms, cpu_model_cache_read_ms, cpu_model_cache_build_ms, cpu_model_cache_write_ms, model_cache_hit_count, model_cache_miss_count | ContentData, LevelData, LevelOne, ModelCache |
| 실행 조건 | viewport_width_pixels, viewport_height_pixels, msaa_sample_count, map_width_tiles, map_height_tiles, enemy_total_count, prop_total_count, drop_total_count, floating_text_count, game_paused_flag, level_one_flag | Renderer.cpp, LevelOne.cpp, Game.cpp |
| 메모리·CPU | process_working_set_bytes, process_private_commit_bytes, process_cpu_core_equivalents, render_target_estimated_bytes, cpu_vertex_buffer_capacity_bytes | Profiler.cpp, Renderer.cpp |
| 측정 상태 | cpu_profiler_collect_report_ms, gpu_query_skipped_frame_count, gpu_result_max_age_frames, log_dropped_record_count, log_writer_last_record_ms, log_file_failed | Profiler.cpp |

전체 per_frame 키의 원본은 Profiler.h의 PROFILE_METRICS 목록이다. 신규 필드도 단위를 키에 포함한다. `models_window_totals`는 모델 이름별 cpu_draw_ms/draw_count/transformed_vertex_count/static의 구간 합계다. 모델 draw_count는 모델 표시 함수 호출 수이며 OpenGL draw call 수가 아니다.

## 반드시 지킬 해석 규칙

1. CPU 타이머는 steady_clock으로 잰 경과 시간이다. OS 스케줄링·드라이버 대기가 포함될 수 있으며 CPU 명령 실행 시간이나 CPU 점유율과 다르다. 부모 범위는 자식 범위를 포함한다. render_submit + model_draw + batch_submit 등을 더해 전체 프레임 시간으로 만들지 않는다. 정적 모델 시간은 전체 모델 시간의 부분집합이다.
2. GPU 타이머는 드라이버의 타임스탬프 사이 경과 시간이다. GPU 이용률·셰이더 단독 실행 시간이 아니다. 첫 월드 draw 직전부터 기록하여 앞선 CPU 지오메트리 준비를 가능한 한 제외하지만, 구간 중 명령 공급이 늦어지면 GPU 타임라인의 대기도 포함될 수 있다. 특히 UI 구간은 CPU UI 구성과 텍스트 준비 지연의 영향을 받을 수 있다. GPU 시간이 크다는 이유만으로 GPU 연산 병목이라고 단정하지 않는다.
3. GPU 결과는 수 프레임 뒤 도착한다. gpu_completed_samples는 보고 구간에 회수된 표본이다. gpu_source_frame_first/last와 gpu_worst_source_frame을 사용하며 같은 window의 CPU 최악 프레임과 동일하다고 가정하지 않는다. gpu_frame_ms는 6개 GPU 구간의 합계라서 이 값에 각 구간을 다시 더하지 않는다.
4. draw_call_count = geometry_draw_count + fullscreen_draw_count이다. flush 원인별 합계는 실제 제출된 geometry_draw_count와 대응한다. 빈 Flush는 호출 횟수에 넣지 않는다. CPU 프레임별 집계는 유지하고 콘솔만 1초 요약으로 바꾸었다. glBlitFramebuffer, 텍스처·버퍼 업로드는 draw call이 아니다.
5. submitted_vertex_count와 vertex_upload_bytes는 지오메트리 배치 기준이다. 후처리의 gl_VertexID 기반 전면 삼각형은 이 업로드 수에 포함되지 않는다. 라벨 텍스처 업로드 바이트는 별도다. cpu_vertex_upload_ms는 glBufferData 호출의 CPU 경과 시간이며 실제 비동기 GPU 전송 완료 시간이 아니다.
6. effect bbox 지표는 Effect 사각형과 화면의 교집합 면적을 합산한 추정치다. 알파, 정확한 scissor, MSAA 샘플, 실제 셰이더 실행 픽셀을 세지 않는다. 겹침 때문에 화면 면적보다 클 수 있다. 타일 Water 셰이더나 모든 도형의 오버드로를 측정한 값은 아니다.
7. 렌더 타깃·텍스처 바이트는 형식과 크기로 계산한 추정치이며 드라이버 실제 VRAM 사용량이 아니다. working_set은 프로세스 상주 메모리, private_commit은 프로세스 전용 커밋이다. CPU core_equivalents의 1.0은 구간 평균으로 코어 한 개 분량의 CPU 시간을 소비했다는 의미다. 모든 게임·로그·드라이버 작업 스레드가 포함되며 첫 구간은 null이다.
8. tick_interval_ms·timer_lateness_ms는 해당 렌더 프레임 구간에 발생한 tick들의 합계다. update_tick_count가 0 또는 여러 번이면 이를 함께 해석한다. timer_lateness는 요청한 16ms 기한 이후 콜백 시작까지의 지연이며 정확한 OS 대기 원인을 밝히지는 않는다. 현재 예약 방식은 변경하지 않았다.
9. cpu_profiler_collect_report_ms는 게임 스레드의 GPU 결과 회수와 프레임 표본 수집·스냅샷 전달 비용이다. 백분위 계산과 JSON 구성은 작업 스레드 비용으로 분리했다. 개별 Scope 시계 읽기, 카운터 증가, 모델별 기록의 전체 오버헤드를 직접 재는 것은 아니다. 집계 종료 후 비용은 다음 프레임에 귀속한다. 성능 비교에는 HB_PROFILE=0 기준도 필요하다.
10. p95/max만으로 해결책이나 60 FPS 달성을 보장하지 않는다. 해상도·Debug/Release·정지/전투 상태·모델 수·맵 시드·GPU 지원 여부를 확인한다. 관측값과 원인 후보를 구분한다.

## 오버헤드와 실패 처리

- 게임 스레드의 일반 지표는 고정 배열에 누적한다. 정점마다 타이머를 생성하지 않는다. 모델별 통계는 모델 이름당 누적 레코드를 재사용한다.
- GPU 쿼리는 8프레임 링으로 관리한다. 마지막 타임스탬프가 준비된 뒤 결과를 읽으며 glFinish를 호출하지 않는다. 링이 차면 해당 프레임 GPU 측정을 생략하고 카운터를 기록한다. CPU 측정과 게임 진행은 유지한다.
- 약 1초마다 게임 스레드에서 통계 스냅샷을 전달하고, 백분위 계산·JSON 구성·파일·콘솔 I/O는 별도 스레드에서 수행한다. 큐는 8레코드로 제한하여 저장장치·콘솔이 느려도 무제한 적체되지 않는다. 큐가 가득 차면 새 레코드를 생략하고 이후 log_dropped_record_count에 반영한다.
- 파일 생성/쓰기 실패는 콘솔에 알리고 콘솔 요약을 유지한다. 이후 window의 log_file_failed에도 표시한다. 정상 종료는 출력 큐가 비워질 때까지 대기할 수 있다.
- 계측을 추가한 주요 엔진 구간을 추적하는 기능이다. GPU 드라이버 내부, 모든 힙 할당 호출, 시스템 전체 작업, 개별 셰이더 명령, 실제 VRAM 상주량까지 자동 추적하는 도구는 아니다. 필요한 새 공통 기능은 아래 방식으로 범위를 추가한다.

## 앞으로 새 기능을 만들 때

1. 주요 업데이트·제작·렌더 준비·I/O 함수에 PROFILE_SCOPE를 추가한다. Profiler.h에 의미와 단위가 명확한 Metric과 키를 등록한다.
2. 루프는 전체 구간 시간과 처리 개수를 함께 기록한다. 타일·정점마다 고해상도 시계를 읽지 않는다. 배치 내부 도형을 독립 draw call로 세지 않는다.
3. 자식 시간과 겹치는 부모 시간을 문서화한다. 함수별 호출 수가 필요한 경우 별도 _count 지표를 추가한다. 프레임마다 갱신되는 상태는 Set, 누적 처리량은 Add, 프레임 최댓값은 Maximum을 사용한다.
4. GPU 패스 추가 시 고정 타임스탬프 경계와 GPU 이름/문서를 함께 확장한다. GPU 결과를 즉시 기다려 FPS를 왜곡하지 않는다.
5. 상시 로그에 모든 프레임·타일·Actor 원문을 무제한 출력하지 않는다. 재현이 필요한 상태 변경은 Event로 이름과 근거를 기록한다. 게임 상태의 정상 동작이나 품질을 측정 편의 때문에 바꾸지 않는다.

## AI 분석 요청 예시

이 JSONL 성능 로그와 PERFORMANCE_PROFILING 문서를 기준으로 병목을 분석해 주세요. Debug/Release와 해상도·맵·GPU 결과 가용성을 먼저 확인하고, CPU 프레임별 mean/p95/max, GPU 비동기 구간, draw call 분리 원인, 정점 업로드, 모델 종류별 비용, 텍스트 캐시, 메모리 추이를 비교해 주세요. 부모·자식 시간과 GPU 합계를 중복 합산하지 말고 측정 사실·원인 후보·추가로 필요한 측정을 구분해 주세요. 수정 제안은 파일·함수·예상 효과·품질 위험·비교할 지표로 정리하고 코드 수정은 별도 요청 전까지 하지 마세요.


## 실측 로그 기반 1차 최적화 — 2026-09-23

사용자가 제공한 Debug 1280×800 Intel UHD 로그(1,120프레임)는 평균 38.02 FPS, CPU 렌더 24.66ms, 정적 모델 13.89ms, 미니맵 3.77ms를 기록했다. 프로파일러의 보고 처리도 일부 프레임에서 6~12ms였다. 아래 변경 이후 성능은 아직 실측하지 않았다.

- `Profiler::Report`: 게임 스레드는 프레임 통계 버퍼와 모델별 합계를 소유권 교환으로 스냅샷에 전달한다. 작업 스레드가 백분위 계산·JSON 구성·프로세스 통계 조회·파일/콘솔 출력을 처리한다. 표본 버퍼는 재사용하고 기존 8개 큐 상한과 종료 시 배출 정책을 유지한다. CPU/GPU 표본과 최악 프레임 값은 독립된 스냅샷으로 전달하므로 작업 스레드가 게임의 실시간 누적 배열에 접근하지 않는다.
- `ModelCache::Initialize/Draw`, `Renderer::AppendMesh`: 정적 모델은 로딩 시 렌더러 정점 형식으로 한 번 포장한다. 정지 상태에서는 관절 행렬·삼각형별 도형 호출을 건너뛰고 기존 배치에 연속 복사한 뒤 위치·좌우 반전·크기·불투명도를 적용한다. 이동/공격/피격/쓰러짐 값이 필요한 경우 기존 경로를 사용한다. 원본 형상·색·재질과 Actor 깊이 순서·가림 처리를 유지한다. GPU 상주 캐시나 인스턴싱을 구현한 것은 아니며 정점 업로드는 계속 발생한다. 디스크 모델 캐시 형식은 바뀌지 않는다.
- `LevelOne::BuildMinimapBackground/DrawMinimap`: 맵 생성 시 같은 타일의 가로 연속 구간을 사각형으로 합쳐 배경 정점을 캐싱한다. 프레임마다 전체 타일을 스캔하지 않고 캐시를 배치에 추가한다. 플레이어·적·제단 표시는 기존대로 갱신하며 창 크기 변경 시 위치도 따라간다. R 맵 재생성 성공 시 캐시를 다시 만든다.
- 비교 지표: cpu_static_model_draw_ms, cpu_minimap_build_ms, cpu_profiler_collect_report_ms, submitted_vertex_count, vertex_upload_bytes, frame_present_interval_ms의 mean/p95/max. static_model_fast_path_count로 빠른 경로 사용 횟수를 기록한다. minimap_cache_build_count는 맵별 재생성 횟수, minimap_cached_vertex_count는 현재 캐시 크기다. minimap_tile_scanned_count는 이제 캐시 생성 때만 증가한다. 단순 위치 변환은 여전히 수행하므로 static_model_transformed_vertex_count가 0이 될 것으로 기대하지 않는다.
- 프로세스 CPU/메모리 샘플은 작업 스레드의 조회 시점 값이다. process_sample_elapsed_ms로 원본 프레임 구간 종료 시각과 구분한다. log_writer_last_record_ms에는 이제 통계·JSON 준비 비용도 포함된다. session.report_processing은 background_snapshot_v2로 기록한다.
- 텍스트 아틀라스, 고정 UI 라벨 사전 준비, GPU 정적 배칭/인스턴싱, 지형 캐싱, 타이머 변경, 후처리 변경은 이번 범위에 포함하지 않았다. draw call 감소나 60 FPS 달성을 실측 없이 주장하지 않는다.
- 빌드·실행·테스트는 하지 않았다. 사용자는 정적 소품의 형태·가림, 미니맵의 이동 표식·창 크기 변경·R 재생성, 새 JSONL 출력을 확인한다.

## 월드 시뮬레이션 계측 — 2026-09-28
cpu_world_simulation_ms, world_minute_tick_count, world_event_count를 추가했다.
world_absolute_minute, world_simulated_region_count, player_region_food_ratio, player_region_infection_ratio는 게이지다.
world_player_region, world_region_state, world_event 이벤트의 detail에 지역/사건 ID와 단위가 명시된 상태를 기록한다.
전체 규약은 docs/WORLD_SIMULATION.md를 따른다. 게이지 sum을 누적 자원으로 보지 않으며 상위 update 시간과 중복 합산하지 않는다.
새 계측의 실행 로그·성능 수치는 아직 확인하지 않았다.

## 자율 NPC·저장 프로파일링 — 2026-09-28 후속
NPC 판단/소통/세력/요청/경로/이동/전투와 월드 저장·복원에 CPU 타이머, 처리 횟수, 메시지 대기/탈락 카운터를 추가했다. 키 전체와 기록 예시는 AUTONOMOUS_WORLD.md를 따른다.
society_change의 change_id/cause_id와 주체·대상·사유로 사건 연결을 추적하고 world_resource_change의 변경 전후를 비교한다. 게이지와 건수, 포함 CPU 시간을 구분한다. 저장 I/O도 프레임 부하를 만들 수 있으므로 cpu_world_save_ms와 p95를 함께 본다.
맵 확대 후 지형은 화면 주변만 순회하며 NPC는 지역 후보와 분산 주기, 경로 생성 예산을 사용한다. 실측 없이 60 FPS나 최적화 효과를 확정하지 않는다.

## 생태·세력 전쟁 계측 — 2026-09-30
- cpu_ecology_perception_ms: 프레임 공간 버킷 구성과 몬스터 대상 선택 CPU. ecology_candidate_count는 몹/주민이 조회한 누적 후보 수다.
- ecology_path_budget_deferred_count: 몬스터 우회 경로의 프레임 예산(2건) 소진으로 미룬 요청 수다. 기존 npc_path_build_count/cpu_npc_path_ms에는 주민과 몬스터 경로가 함께 포함된다.
- wildlife_strike_count: 몬스터의 유효 타격 조건 통과 횟수. 플레이어 무적 중 실제 피해량과는 다를 수 있다. npc_strike_count는 주민 타격을 집계한다.
- cpu_faction_war_ms/active_war_count: 캠페인 판단 CPU와 활성 전쟁 수. 부모 cpu_faction_decision_ms에 포함되므로 중복 합산하지 않는다.
- wildlife_target 사건: actor_id, target_kind(0 없음/1 플레이어/2 주민/3 몬스터), target_index, world_minute, reason. society_change에는 wildlife_interaction/war_declared/campaign_order/war_supply/armistice와 원인·시각을 기록한다.
맵 256×200·NPC 40명으로 부하 조건이 달라졌다. 플레이어 전용 전맵 거리장 반복 갱신은 중지했지만 목표 FPS 달성을 측정한 것은 아니다. 빌드·실행·실측은 사용자가 담당한다.

## 서사 계측 — 2026-10-07
- `cpu_narrative_update_ms`: 관찰·정보 전달·사건 진행의 포괄 CPU 시간. 부모인 사회/월드 CPU와 중복 합산하지 않는다.
- `narrative_message_queue_count`: 전달 대기 중인 서사 보고 수. 최대 128개. 기존 일반 사회 메시지 큐와 별도다.
- `narrative_decision_count`: 진행 중 사건의 판단 횟수. 완료 수나 NPC 전체 판단 수가 아니다.
- 관찰/전달/사건 판단은 세계 6분 간격, 자발적 정보 공유는 세계 30분 간격이다. 서사 전달 처리 예산은 기존 messageBudget 값(현재 16)을 재사용하며 일반 사회 전달 예산과 별도 적용한다. 매 프레임 모든 인물의 서사 계획을 재평가하지 않는다.
- `story_started`, `story_knowledge`, `story_dispatch`, `story_report_cancelled`, `story_exchange`, `story_cargo`, `story_joint_care`, `story_preservation`, `story_outcome`, `story_war_reconsidered` 등 변경 기록을 남긴다. 지식 수용에는 출처·관찰 시각·주장·신뢰도, 사건 변화에는 원인 ID를 포함한다.
- 동일한 직접 관찰은 매 틱 변경 로그를 반복하지 않고 60세계분 이후 신선도를 갱신한다. 출처가 같은 정보가 여러 번 전달됐다고 독립 증거 여러 개로 누적하지 않는다.
- 기존 사회 관계 검색/소규모 관찰 루프를 재사용한다. 현재 40명 규모의 구현이며 대규모 인구에 대한 확장성이나 60 FPS 달성을 실측한 것은 아니다. 빌드·실행·테스트·프로파일 비교는 사용자 담당이다.

## 실제 3D와 생활 계획 계측 — 2026-10-07 후속
- cpu_world_3d_ms: 인스턴스 업로드·그림자/본 패스 GL 제출·점광원 선택 CPU 시간. GPU 완료 시간이 아니다. 현재 FinishScene 안에서 호출되어 cpu_postprocess_submit_ms에도 포함된다.
- cpu_shadow_3d_submit_ms: 위 시간에 포함된 그림자 draw 제출 CPU. 상위 시간과 합산하지 않는다. 기존 GPU geometry 구간에는 3D 그림자/본 패스가 함께 들어가며 별도 그림자 GPU 타이머는 없다.
- world_3d_instance_count: 프레임의 동적 도형 인스턴스 수. 별도 정적 지형 청크는 제외한다. world_3d_submitted_triangle_count는 그림자와 본 패스에 제출한 삼각형을 합산하며 유일 정점/보이는 픽셀 수가 아니다.
- cpu_model_draw_ms와 model_draw_count는 3D 모델 포즈·인스턴스 제출에도 사용한다. 기존 2D ModelScope의 모델별 상세 정점 집계와 새 3D 인스턴스 집계를 동일하게 해석하지 않는다.
- cpu_npc_life_ms: 세계 분 틱의 약속/생활 계획 판단 CPU. 실제 이동·현장 작업은 기존 cpu_npc_motion_ms에 포함된다. npc_active_plan_count/npc_active_agreement_count는 사회 갱신 프레임마다 기록하는 현재 상태 게이지다. 판단 틱이 없는 프레임에도 유지한다.
- society_change의 life_plan/life_path_blocked/life_plan_cancelled/life_production/life_meal/life_delivery/life_cargo_lost/npc_conversation/ agreement_proposed·reserved·fulfilled·cancelled로 원인 ID와 결과를 추적한다. world_resource_change로 지역 자원 이동을 함께 본다.
기본 도형별 GPU 인스턴싱과 정적 지형 청크 캐시를 추가했으므로 이전 2D CPU 배칭만 있는 단계와 구분한다. 그림자 패스가 실제 draw call에 포함되어 이전 19회보다 늘 수 있다. 60 FPS 달성/회귀 여부는 같은 시드·해상도·구성의 실제 로그로 판단한다. 빌드·실행·실측은 하지 않았다.

## 2026-10-08 계측 보정과 최적화

3D glDrawArraysInstanced를 draw_call_count/geometry_draw_count에 포함했다. 과거 로그의 호출 수는 3D를 제외했으므로 새 로그와 단순 비교하면 안 된다. GPU 타임스탬프는 8개로 확장해 gpu_shadow_ms, gpu_world_ms(본 렌더), resolve, bloom 2구간, postprocess, UI, 전체 프레임을 기록한다. 이전 gpu_world_ms와 비교할 때 새 그림자+본 렌더 구간을 본다.

A* 탐색 노드/예산 대기/실패/부분 경로, 그림자 인스턴스, 작은 도형 LOD, Scene 경계 재계산, 정적 부품 재사용 카운터를 추가했다. 정적 부품 수는 GPU 영구 캐시 적중 수와 다르며, 그림자 인스턴스 카운터는 동적 도형 배치만 집계한다. 자세한 키는 PLAYABILITY_RENDER_IMPROVEMENTS.md를 따른다.

절대 타이머 주기, 경로 탐색 예산, 정적 소품 행렬/Scene 경계 재사용, 글꼴 측정 분리/LRU, 그림자 제외, 작은 구체 LOD, 후면 컬링을 적용했다. 실측/60 FPS 달성은 미확인이다. 사용자 실행 로그를 받은 후 같은 조건의 프레임 p95/max, cpu_npc_path_ms, cpu_scene_prepare_ms, cpu_model_draw_ms, cpu_label_rasterize_ms 및 GPU 구간으로 평가한다.

## 월드 관측 — 2026-10-12
- cpu_world_monitor_snapshot_ms: 첫 레벨에서 약 1초마다 상태 JSON을 구성하고 작업 스레드에 제출하는 게임 스레드 시간. 별도 스레드의 파일 I/O는 포함하지 않는다. 대기 스냅샷은 최신 하나만 유지한다.
- snapshot.json의 attack_attempts/hits/misses/damage_dealt와 last_attack은 현재 세션 진단이다. 준비 취소는 완료 시도 수에 포함하지 않으며 damage_dealt는 NPC/몬스터 체력 감소량만 포함한다. 영속 사건 전체 기록은 기존 JSONL을 사용한다. 모니터 최근 사건은 제한된 History다.
- 새 궤적/파편도 기존 3D 배치를 사용하나 실제 draw call/인스턴스/GPU 비용은 사용자 실측으로 판단한다. 빌드·실행·테스트·성능 확인은 수행하지 않았다.
