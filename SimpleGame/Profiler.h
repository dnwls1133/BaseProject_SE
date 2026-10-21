#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

// Stable keys are the public JSONL schema. Timers are inclusive wall times, not CPU cycles.
#define PROFILE_METRICS(X)                                                                         \
    X(ConflictField, cpu_contamination_update_ms)                                                  \
    X(ConflictCombat, cpu_faction_combat_ms)                                                       \
    X(ContaminatedCells, contamination_cell_count)                                                 \
    X(BurningCells, burning_cell_count)                                                            \
    X(ConflictStrikes, faction_attack_resolved_count)                                              \
    X(FrameInterval, frame_present_interval_ms)                                                    \
    X(Update, cpu_update_ms)                                                                       \
    X(Render, cpu_render_submit_ms)                                                                \
    X(Swap, cpu_present_wait_ms)                                                                   \
    X(TickInterval, tick_interval_ms)                                                              \
    X(TimerLate, timer_lateness_ms)                                                                \
    X(Ticks, update_tick_count)                                                                    \
    X(DtClamps, update_dt_clamp_count)                                                             \
    X(SceneUpdate, cpu_scene_update_ms)                                                            \
    X(ScenePrepare, cpu_scene_prepare_ms)                                                          \
    X(SceneSort, cpu_scene_sort_ms)                                                                \
    X(GroundPass, cpu_ground_pass_ms)                                                              \
    X(ShadowPass, cpu_shadow_pass_ms)                                                              \
    X(WorldPass, cpu_world_pass_ms)                                                                \
    X(EffectsPass, cpu_effects_pass_ms)                                                            \
    X(OverlayPass, cpu_overlay_pass_ms)                                                            \
    X(UiPass, cpu_ui_pass_ms)                                                                      \
    X(ActorsUpdated, actor_updated_count)                                                          \
    X(ActorsVisible, actor_visible_count)                                                          \
    X(ActorsCulled, actor_culled_count)                                                            \
    X(GroupsCulled, group_culled_count)                                                            \
    X(ActorCallbacks, actor_render_callback_count)                                                 \
    X(Ground, cpu_ground_build_ms)                                                                 \
    X(Minimap, cpu_minimap_build_ms)                                                               \
    X(Atmosphere, cpu_atmosphere_build_ms)                                                         \
    X(Player, cpu_player_update_ms)                                                                \
    X(Enemies, cpu_enemy_update_ms)                                                                \
    X(EnemyUpdates, enemy_update_count)                                                            \
    X(Navigation, cpu_navigation_ms)                                                               \
    X(NavigationBuilds, navigation_build_count)                                                    \
    X(Collision, cpu_collision_ms)                                                                 \
    X(CollisionCalls, collision_query_count)                                                       \
    X(Visibility, cpu_line_of_sight_ms)                                                            \
    X(VisibilityCalls, line_of_sight_query_count)                                                  \
    X(NavigationVisited, navigation_visited_tile_count)                                            \
    X(Combat, cpu_combat_ms)                                                                       \
    X(GroundTilesScanned, ground_tile_scanned_count)                                               \
    X(GroundTilesVisible, ground_tile_visible_count)                                               \
    X(MinimapTiles, minimap_tile_scanned_count)                                                    \
    X(MinimapCacheBuild, minimap_cache_build_count)                                                \
    X(MinimapCacheVertices, minimap_cached_vertex_count)                                           \
    X(ModelDraw, cpu_model_draw_ms)                                                                \
    X(StaticModelDraw, cpu_static_model_draw_ms)                                                   \
    X(ModelCalls, model_draw_count)                                                                \
    X(ModelVertices, model_transformed_vertex_count)                                               \
    X(StaticVertices, static_model_transformed_vertex_count)                                       \
    X(StaticFastPath, static_model_fast_path_count)                                                \
    X(Flush, cpu_batch_submit_ms)                                                                  \
    X(Upload, cpu_vertex_upload_ms)                                                                \
    X(DrawCalls, draw_call_count)                                                                  \
    X(GeometryDraws, geometry_draw_count)                                                          \
    X(FullscreenDraws, fullscreen_draw_count)                                                      \
    X(Vertices, submitted_vertex_count)                                                            \
    X(UploadBytes, vertex_upload_bytes)                                                            \
    X(MaxBatchVertices, largest_batch_vertex_count)                                                \
    X(FlushOther, flush_other_count)                                                               \
    X(FlushText, flush_text_count)                                                                 \
    X(FlushClip, flush_clip_count)                                                                 \
    X(FlushPost, flush_postprocess_count)                                                          \
    X(FlushFrameEnd, flush_frame_end_count)                                                        \
    X(FlushLabelEviction, flush_label_eviction_count)                                              \
    X(ClipChanges, clip_change_count)                                                              \
    X(Text, cpu_text_submit_ms)                                                                    \
    X(TextCalls, text_draw_count)                                                                  \
    X(LabelBuild, cpu_label_rasterize_ms)                                                          \
    X(LabelHits, label_cache_hit_count)                                                            \
    X(LabelMisses, label_cache_miss_count)                                                         \
    X(LabelEvictions, label_cache_eviction_count)                                                  \
    X(LabelUploadBytes, label_texture_upload_bytes)                                                \
    X(LabelCacheBytes, label_cache_estimated_bytes)                                                \
    X(LabelCacheEntries, label_cache_entry_count)                                                  \
    X(Resolve, cpu_msaa_resolve_submit_ms)                                                         \
    X(Postprocess, cpu_postprocess_submit_ms)                                                      \
    X(Effects, effect_quad_count)                                                                  \
    X(EffectPixels, effect_bbox_clipped_estimated_pixels)                                          \
    X(MistPixels, mist_bbox_clipped_estimated_pixels)                                              \
    X(LightPixels, soft_light_bbox_clipped_estimated_pixels)                                       \
    X(FlamePixels, flame_bbox_clipped_estimated_pixels)                                            \
    X(ViewWidth, viewport_width_pixels)                                                            \
    X(ViewHeight, viewport_height_pixels)                                                          \
    X(MsaaSamples, msaa_sample_count)                                                              \
    X(TargetBytes, render_target_estimated_bytes)                                                  \
    X(VertexCapacity, cpu_vertex_buffer_capacity_bytes)                                            \
    X(MapWidth, map_width_tiles)                                                                   \
    X(MapHeight, map_height_tiles)                                                                 \
    X(EnemyCount, enemy_total_count)                                                               \
    X(PropCount, prop_total_count)                                                                 \
    X(DropCount, drop_total_count)                                                                 \
    X(NumberCount, floating_text_count)                                                            \
    X(Paused, game_paused_flag)                                                                    \
    X(LevelOne, level_one_flag)                                                                    \
    X(LevelLoad, cpu_level_definition_load_ms)                                                     \
    X(TerrainGenerate, cpu_terrain_generate_ms)                                                    \
    X(Load, cpu_content_read_ms)                                                                   \
    X(ContentBytes, content_read_bytes)                                                            \
    X(WorldSimulation, cpu_world_simulation_ms)                                                    \
    X(WorldMinuteTicks, world_minute_tick_count)                                                   \
    X(WorldEvents, world_event_count)                                                              \
    X(WorldMinute, world_absolute_minute)                                                          \
    X(SimulationRegions, world_simulated_region_count)                                             \
    X(RegionFood, player_region_food_ratio)                                                        \
    X(RegionInfection, player_region_infection_ratio)                                              \
    X(SocietyLoad, cpu_society_load_ms)                                                            \
    X(SocietyUpdate, cpu_society_update_ms)                                                        \
    X(NpcDecision, cpu_npc_decision_ms)                                                            \
    X(NpcDecisions, npc_decision_count)                                                            \
    X(SocialMessages, cpu_npc_messages_ms)                                                         \
    X(SocialMessageSent, npc_message_sent_count)                                                   \
    X(SocialMessageDropped, npc_message_dropped_count)                                             \
    X(FactionDecision, cpu_faction_decision_ms)                                                    \
    X(FactionDecisions, faction_decision_count)                                                    \
    X(SocialQuests, cpu_dynamic_quests_ms)                                                         \
    X(SocialQuestCreated, dynamic_quest_created_count)                                             \
    X(SocialNpcCount, social_npc_count)                                                            \
    X(SocialMessageQueue, npc_message_queue_count)                                                 \
    X(SocialQuestCount, world_quest_record_count)                                                  \
    X(NpcPath, cpu_npc_path_ms)                                                                    \
    X(NpcPathBuilds, npc_path_build_count)                                                         \
    X(NpcPathNodes, npc_path_expanded_nodes)                                                       \
    X(NpcPathDeferred, npc_path_budget_deferred_count)                                             \
    X(NpcPathFailed, npc_path_unreachable_count)                                                   \
    X(NpcPathPartial, npc_path_partial_route_count)                                                \
    X(NpcMotion, cpu_npc_motion_ms)                                                                \
    X(EcologyCandidates, ecology_candidate_count)                                                  \
    X(EcologyPathDeferred, ecology_path_budget_deferred_count)                                     \
    X(WildlifeStrikes, wildlife_strike_count)                                                      \
    X(StoryUpdate, cpu_narrative_update_ms)                                                        \
    X(StoryLetters, narrative_message_queue_count)                                                 \
    X(StoryDecisions, narrative_decision_count)                                                    \
    X(World3D, cpu_world_3d_ms)                                                                    \
    X(Shadow3D, cpu_shadow_3d_submit_ms)                                                           \
    X(World3DInstances, world_3d_instance_count)                                                   \
    X(World3DTriangles, world_3d_submitted_triangle_count)                                         \
    X(World3DShadowInstances, world_3d_shadow_instance_count)                                      \
    X(World3DSmallLod, world_3d_small_lod_count)                                                   \
    X(SceneBoundsRebuild, scene_3d_bounds_rebuild_count)                                           \
    X(StaticModelParts, static_model_cached_part_count)                                            \
    X(EconomyDecision, cpu_economy_decision_ms)                                                    \
    X(EconomyProduction, cpu_economy_production_ms)                                                \
    X(EconomyTransfer, cpu_economy_transfer_ms)                                                    \
    X(EconomyOrders, economy_active_order_count)                                                   \
    X(EconomyDeliveries, economy_delivery_count)                                                   \
    X(NpcPlanning, cpu_npc_goal_planning_ms)                                                       \
    X(NpcPlanTransitions, npc_plan_transition_count)                                               \
    X(NpcLife, cpu_npc_life_ms)                                                                    \
    X(NpcPlans, npc_active_plan_count)                                                             \
    X(NpcAgreements, npc_active_agreement_count)                                                   \
    X(WarDecision, cpu_faction_war_ms)                                                             \
    X(ActiveWars, active_war_count)                                                                \
    X(NpcPerception, cpu_ecology_perception_ms)                                                    \
    X(NpcCombat, cpu_npc_combat_ms)                                                                \
    X(NpcStrikes, npc_strike_count)                                                                \
    X(WorldSave, cpu_world_save_ms)                                                                \
    X(WorldMonitor, cpu_world_monitor_snapshot_ms)                                                 \
    X(WorldRestore, cpu_world_restore_ms)                                                          \
    X(WorldGenerate, cpu_world_generate_ms)                                                        \
    X(CacheRead, cpu_model_cache_read_ms)                                                          \
    X(CacheBuild, cpu_model_cache_build_ms)                                                        \
    X(CacheWrite, cpu_model_cache_write_ms)                                                        \
    X(CacheHit, model_cache_hit_count)                                                             \
    X(CacheMiss, model_cache_miss_count)                                                           \
    X(Profiler, cpu_profiler_collect_report_ms)                                                    \
    X(GpuSkipped, gpu_query_skipped_frame_count)

namespace Profile
{
enum class Metric
{
#define PROFILE_ENUM(id, key) id,
    PROFILE_METRICS(PROFILE_ENUM)
#undef PROFILE_ENUM
    Count
};

using Clock = std::chrono::steady_clock;
bool Enabled();
void Initialize();
void Shutdown();
void StartGameplay();
void BeginFrame();
void Presented();
void EndFrame();
void Add(Metric metric, double value = 1);
void Set(Metric metric, double value);
void Maximum(Metric metric, double value);
void Event(const std::string& name, const std::string& detail);
void RecordModel(const std::string& name, std::size_t vertices, bool isStatic, double elapsedMs);
void BeginGpuFrame();
void GpuMarker(unsigned index);
void EndGpuFrame();

class Scope
{
  public:
    explicit Scope(Metric metric, bool measure = true)
        : metric(metric), enabled(measure && Enabled())
    {
        if (enabled)
        {
            start = Clock::now();
        }
    }

    ~Scope()
    {
        if (enabled)
        {
            Add(metric, std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        }
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

  private:
    Metric metric;
    bool enabled;
    Clock::time_point start;
};

class ModelScope
{
  public:
    ModelScope(const std::string& name, std::size_t vertices, bool isStatic)
        : name(name), vertices(vertices), isStatic(isStatic), enabled(Enabled())
    {
        if (enabled)
        {
            start = Clock::now();
        }
    }

    ~ModelScope()
    {
        if (enabled)
        {
            RecordModel(name, vertices, isStatic,
                        std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        }
    }

    ModelScope(const ModelScope&) = delete;
    ModelScope& operator=(const ModelScope&) = delete;

  private:
    const std::string& name;
    std::size_t vertices;
    bool isStatic, enabled;
    Clock::time_point start;
};
} // namespace Profile

#define PROFILE_JOIN_INNER(a, b) a##b
#define PROFILE_JOIN(a, b) PROFILE_JOIN_INNER(a, b)
#define PROFILE_SCOPE(metric)                                                                      \
    Profile::Scope PROFILE_JOIN(profileScope, __LINE__)(Profile::Metric::metric)
