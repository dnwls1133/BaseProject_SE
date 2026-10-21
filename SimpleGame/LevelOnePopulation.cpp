#include "stdafx.h"
#include "LevelOne.h"
#include "ContentData.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <random>

bool LevelOne::PlaceWildernessEnemy(Enemy& enemy, bool relocating)
{
    if (wildernessSpawns.empty())
    {
        return false;
    }
    // Candidate attempts and respawns are bounded; never flood-fill the map per death.
    const std::size_t start = PopulationRandom() % wildernessSpawns.size();
    const int attempts = relocating ? 160 : int(wildernessSpawns.size());
    for (int i = 0; i < attempts; ++i)
    {
        const Point point = wildernessSpawns[(start + i) % wildernessSpawns.size()];
        if (std::hypot(point.x - player.x, point.y - player.y) < content.spawnPlayerClearance ||
            (relocating &&
             std::hypot(point.x - enemy.x, point.y - enemy.y) < content.respawnRelocation))
        {
            continue;
        }
        bool occupied = false;
        for (const auto& other : enemies)
        {
            if (&other != &enemy && !other.dead &&
                std::hypot(point.x - other.x, point.y - other.y) < 2.5f)
            {
                occupied = true;
                break;
            }
        }
        if (!occupied)
        {
            for (const auto& npc : society.Npcs())
            {
                if (npc.Alive() && std::hypot(point.x - npc.x, point.y - npc.y) < 4)
                {
                    occupied = true;
                    break;
                }
            }
        }
        if (occupied)
        {
            continue;
        }
        enemy.SetPosition(point.x, point.y);
        enemy.homeX = point.x;
        enemy.homeY = point.y;
        enemy.route = point;
        enemy.dead = false;
        enemy.health = enemy.maximumHealth = enemy.type.health;
        enemy.respawnRemaining = enemy.collapse = enemy.windup = enemy.flash = 0;
        enemy.movement = enemy.strikeTime = enemy.yaw = 0;
        enemy.targetKind = 0;
        enemy.targetIndex = -1;
        enemy.decisionTime = float(PopulationRandom() % 10) * .05f;
        enemy.cooldown = .5f;
        enemy.SetActive(true);
        enemy.SetVisible(true);
        enemy.SetTickEnabled(true);
        enemy.SetRenderPasses(ActorPassBit(ActorPass::Shadow) | ActorPassBit(ActorPass::World) |
                              ActorPassBit(ActorPass::Overlay));
        return true;
    }
    return false;
}

std::vector<Point> LevelOne::PrepareWildernessSpawns(const LevelData& definition,
                                                     const RandomLevelMap& terrain,
                                                     const WorldSociety& population,
                                                     std::uint32_t seed) const
{
    std::vector<Point> candidates;
    if (definition.wildernessCount == 0)
    {
        return candidates;
    }
    for (const auto& cell : terrain.reachable)
    {
        // Uniformly sparse navigation candidates reduce repeated collision work.
        if ((cell.x % 2) || (cell.y % 2))
        {
            continue;
        }
        Point point{cell.x + .5f, cell.y + .5f};
        bool village = false;
        for (const auto& region : definition.regions)
        {
            if (region.villageRadius > 0 &&
                std::hypot(point.x - region.x, point.y - region.y) < region.villageRadius + 4)
            {
                village = true;
                break;
            }
        }
        bool betweenVillages = false;
        for (const auto& road : definition.roads)
        {
            const auto endpoint = [&](int x, int y)
            {
                return std::any_of(definition.regions.begin(), definition.regions.end(),
                                   [&](const RegionDefinition& r)
                                   {
                                       return r.villageRadius > 0 && r.x == x && r.y == y;
                                   });
            };
            if (!endpoint(road.ax, road.ay) || !endpoint(road.bx, road.by))
            {
                continue;
            }
            const float dx = float(road.bx - road.ax), dy = float(road.by - road.ay);
            const float length2 = dx * dx + dy * dy;
            if (length2 <= 0)
            {
                continue;
            }
            const float t = std::clamp(
                ((point.x - road.ax) * dx + (point.y - road.ay) * dy) / length2, 0.f, 1.f);
            if (std::hypot(point.x - road.ax - t * dx, point.y - road.ay - t * dy) <= 24)
            {
                betweenVillages = true;
                break;
            }
        }
        if (!village && betweenVillages && terrain.CanStand(point.x, point.y, .4f))
        {
            candidates.push_back(point);
        }
    }
    std::mt19937 candidateRandom(seed ^ 0x53A92C17u);
    std::shuffle(candidates.begin(), candidates.end(), candidateRandom);
    std::vector<Point> selected;
    const Point start{definition.startX + .5f, definition.startY + .5f};
    for (Point point : candidates)
    {
        if (std::hypot(point.x - start.x, point.y - start.y) < definition.spawnPlayerClearance)
        {
            continue;
        }
        const bool npcNearby =
            std::any_of(population.Npcs().begin(), population.Npcs().end(),
                        [&](const WorldSociety::Npc& npc)
                        {
                            return npc.Alive() && std::hypot(point.x - npc.x, point.y - npc.y) < 4;
                        });
        const bool overlapping =
            std::any_of(selected.begin(), selected.end(),
                        [&](Point other)
                        {
                            return std::hypot(point.x - other.x, point.y - other.y) < 2.5f;
                        });
        if (!npcNearby && !overlapping)
        {
            selected.push_back(point);
            if (int(selected.size()) == definition.wildernessCount)
            {
                break;
            }
        }
    }
    ContentData::Require(int(selected.size()) == definition.wildernessCount,
                         "not enough valid inter-village monster positions");
    // Prefix is validated initial placement. The remainder is the reusable respawn pool.
    selected.insert(selected.end(), candidates.begin(), candidates.end());
    return selected;
}

void LevelOne::BuildWildernessPopulation()
{
    for (int i = 0; i < content.wildernessCount; ++i)
    {
        Enemy enemy(this);
        enemy.wilderness = true;
        enemy.type = content.Enemy(content.wildernessSpecies[i % content.wildernessSpecies.size()]);
        const Point position = wildernessSpawns[i];
        enemy.SetPosition(position.x, position.y);
        enemy.homeX = position.x;
        enemy.homeY = position.y;
        enemy.route = position;
        enemy.health = enemy.maximumHealth = enemy.type.health;
        enemy.phase = float(PopulationRandom() % 100);
        enemy.decisionTime = float(i % 10) * .05f;
        enemies.push_back(enemy);
    }
}

void LevelOne::UpdateRespawns(float dt)
{
    PROFILE_SCOPE(WildernessRespawn);
    int budget = 4;
    for (auto& enemy : enemies)
    {
        if (!enemy.wilderness || !enemy.dead)
        {
            continue;
        }
        enemy.respawnRemaining = std::max(0.f, enemy.respawnRemaining - dt);
        if (enemy.respawnRemaining > 0 || budget <= 0)
        {
            continue;
        }
        --budget;
        const Point previous{enemy.x, enemy.y};
        if (PlaceWildernessEnemy(enemy, true))
        {
            Profile::Add(Profile::Metric::WildernessRespawnCount);
            Profile::Event("wilderness_respawn", "species=" + enemy.type.id +
                                                     " previous_x=" + std::to_string(previous.x) +
                                                     " previous_y=" + std::to_string(previous.y) +
                                                     " x=" + std::to_string(enemy.x) +
                                                     " y=" + std::to_string(enemy.y));
        }
        else
        {
            enemy.respawnRemaining = 1;
        }
    }
}
