#include "stdafx.h"
#include "LevelOne.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <queue>

void LevelOne::BuildEcologyIndex()
{
    PROFILE_SCOPE(NpcPerception);
    ecologyIndex.clear();
    ecologyPathBudget = 2;
    for (std::size_t i = 0; i < enemies.size(); ++i)
    {
        const auto& e = enemies[i];
        if (!e.dead && e.IsActive())
        {
            ecologyIndex[int(e.x / 16) + int(e.y / 16) * 1024].push_back({3, int(i)});
        }
    }
    for (std::size_t i = 0; i < society.Npcs().size(); ++i)
    {
        const auto& n = society.Npcs()[i];
        if (n.Alive())
        {
            ecologyIndex[int(n.x / 16) + int(n.y / 16) * 1024].push_back({2, int(i)});
        }
    }
}

std::vector<std::pair<int, int>> LevelOne::EcologyNearby(float x, float y) const
{
    std::vector<std::pair<int, int>> result;
    // Two cells cover the maximum authored perception radius (24 tiles).
    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            const auto found = ecologyIndex.find(int(x / 16) + dx + (int(y / 16) + dy) * 1024);
            if (found != ecologyIndex.end())
            {
                result.insert(result.end(), found->second.begin(), found->second.end());
            }
        }
    }
    Profile::Add(Profile::Metric::EcologyCandidates, static_cast<double>(result.size()));
    return result;
}

Point LevelOne::EcologyStep(float x, float y, Point goal)
{
    if (map.CanTravel(x, y, goal.x, goal.y))
    {
        return goal;
    }
    if (ecologyPathBudget <= 0)
    {
        Profile::Add(Profile::Metric::EcologyPathDeferred);
        return {x, y};
    }
    PROFILE_SCOPE(NpcPath);
    --ecologyPathBudget;
    Profile::Add(Profile::Metric::NpcPathBuilds);
    // Local routes are bounded; they never flood the expanded world per creature.
    constexpr int width = 49, center = 24 * width + 24;
    const int ox = int(x) - 24, oy = int(y) - 24;
    const int gx = int(goal.x) - ox, gy = int(goal.y) - oy;
    if (gx < 0 || gy < 0 || gx >= width || gy >= width)
    {
        return {x, y};
    }
    std::vector<int> parent(width * width, -1);
    std::queue<int> open;
    open.push(center);
    parent[center] = center;
    const int destination = gy * width + gx;
    constexpr int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!open.empty() && parent[destination] < 0)
    {
        const int at = open.front();
        open.pop();
        for (const auto& offset : offsets)
        {
            const int nx = at % width + offset[0], ny = at / width + offset[1];
            if (nx < 0 || ny < 0 || nx >= width || ny >= width)
            {
                continue;
            }
            const int next = ny * width + nx;
            if (parent[next] >= 0 || !map.Walkable(nx + ox, ny + oy))
            {
                continue;
            }
            parent[next] = at;
            open.push(next);
        }
    }
    if (parent[destination] < 0 || destination == center)
    {
        return {int(x) + .5f, int(y) + .5f};
    }
    int next = destination;
    while (parent[next] != center)
    {
        next = parent[next];
    }
    Point step{ox + next % width + .5f, oy + next / width + .5f};
    return map.CanTravel(x, y, step.x, step.y) ? step : Point{int(x) + .5f, int(y) + .5f};
}

bool LevelOne::EnemyTarget(const Enemy& e, Point& point) const
{
    if (e.targetKind == 1 && player.IsActive() && stats.health > 0)
    {
        point = {player.x, player.y};
        return true;
    }
    if (e.targetKind == 2 && e.targetIndex >= 0 &&
        e.targetIndex < static_cast<int>(society.Npcs().size()))
    {
        const auto& n = society.Npcs()[e.targetIndex];
        if (n.Alive() && society.WildlifeAffinity(n.faction, e.type.id) < 0)
        {
            point = {n.x, n.y};
            return true;
        }
    }
    if (e.targetKind == 3 && e.targetIndex >= 0 && e.targetIndex < int(enemies.size()))
    {
        const auto& other = enemies[e.targetIndex];
        if (!other.dead && other.IsActive() && &e != &other)
        {
            point = {other.x, other.y};
            return true;
        }
    }
    return false;
}

void LevelOne::SelectEnemyTarget(Enemy& e)
{
    PROFILE_SCOPE(NpcPerception);
    const int oldKind = e.targetKind, oldIndex = e.targetIndex;
    e.targetKind = 0;
    e.targetIndex = -1;
    float best = static_cast<float>(e.type.aggro);
    auto consider = [&](int kind, int index, float x, float y)
    {
        const float distance = std::hypot(x - e.x, y - e.y);
        const float score = distance - (kind == oldKind && index == oldIndex ? .75f : 0);
        if (distance <= e.type.aggro && score < best && map.Visible(e.x, e.y, x, y))
        {
            e.targetKind = kind;
            e.targetIndex = index;
            best = score;
        }
    };
    if (player.IsActive() && stats.health > 0)
    {
        consider(1, -1, player.x, player.y);
    }
    for (const auto& candidate : EcologyNearby(e.x, e.y))
    {
        if (candidate.first == 2)
        {
            const auto& n = society.Npcs()[candidate.second];
            if (n.Alive() && society.WildlifeAffinity(n.faction, e.type.id) < 0)
            {
                consider(2, candidate.second, n.x, n.y);
            }
        }
        else
        {
            const auto& other = enemies[candidate.second];
            if (&other != &e && !other.dead &&
                std::find(e.type.rivals.begin(), e.type.rivals.end(), other.type.id) !=
                    e.type.rivals.end())
            {
                consider(3, candidate.second, other.x, other.y);
            }
        }
    }
    if (oldKind != e.targetKind || oldIndex != e.targetIndex)
    {
        Profile::Event("wildlife_target",
                       "actor_id=monster_" + std::to_string(&e - enemies.data()) +
                           " target_kind=" + std::to_string(e.targetKind) +
                           " target_index=" + std::to_string(e.targetIndex) +
                           " world_minute=" + std::to_string(simulation.Minute()) +
                           " reason=visible_hostile_or_territorial_rival");
    }
}

void LevelOne::UpdateEnemy(Enemy& e, float dt)
{
    PROFILE_SCOPE(Enemies);
    Profile::Add(Profile::Metric::EnemyUpdates);
    if (e.dead)
    {
        e.collapse = std::min(1.f, e.collapse + dt / 1.6f);
        if (e.collapse >= 1)
        {
            e.SetRenderPasses(ActorPassBit(ActorPass::Ground));
            e.SetTickEnabled(false);
        }
        return;
    }
    if (!e.IsActive())
    {
        return;
    }
    e.movement *= std::exp(-dt * 10);
    e.phase += dt * .9f;
    e.flash = std::max(0.f, e.flash - dt);
    e.strikeTime = std::max(0.f, e.strikeTime - dt);
    e.cooldown = std::max(0.f, e.cooldown - dt);
    e.decisionTime = std::max(-.5f, e.decisionTime - dt);
    if (e.windup <= 0 && e.decisionTime <= 0)
    {
        SelectEnemyTarget(e);
        e.decisionTime = .5f;
        Point goal;
        if (!EnemyTarget(e, goal))
        {
            const float angle = time * .06f + float(&e - enemies.data()) * 2.4f;
            goal = {e.homeX + std::cos(angle) * 4, e.homeY + std::sin(angle) * 4};
        }
        e.route = EcologyStep(e.x, e.y, goal);
    }
    Point target;
    const bool valid = EnemyTarget(e, target);
    if (e.windup > 0)
    {
        e.windup = std::max(0.f, e.windup - dt);
        if (e.windup == 0)
        {
            if (valid && std::hypot(target.x - e.attackX, target.y - e.attackY) < e.type.range &&
                std::hypot(target.x - e.x, target.y - e.y) < e.type.range + .4f &&
                map.Visible(e.x, e.y, target.x, target.y))
            {
                Profile::Add(Profile::Metric::WildlifeStrikes);
                const std::string source = "monster_" + std::to_string(&e - enemies.data());
                if (e.targetKind == 1 && hurtTime <= 0)
                {
                    const int damage = std::max(1, e.type.attack - stats.Defense());
                    stats.health = std::max(0, stats.health - damage);
                    hurtTime = stats.rules.invulnerability;
                    SpawnNumber(player.x, player.y, .8f, "-" + std::to_string(damage), false);
                }
                else if (e.targetKind == 2)
                {
                    society.WildlifeDamage(source, e.targetIndex, float(e.type.attack), simulation);
                }
                else if (e.targetKind == 3)
                {
                    society.ObserveBroodConflict({e.x, e.y}, map, {player.x, player.y});
                    auto& victim = enemies[e.targetIndex];
                    const int before = victim.health;
                    victim.health = std::max(
                        0, victim.health - std::max(1, e.type.attack - victim.type.defense));
                    victim.flash = .18f;
                    society.WildlifeRecord(source, "monster_" + std::to_string(e.targetIndex),
                                           "먹이 영역을 둘러싼 군집 충돌 · 체력 " +
                                               std::to_string(before) + " → " +
                                               std::to_string(victim.health));
                    if (!victim.health)
                    {
                        Kill(victim, false);
                        society.WildlifeRecord(source, "monster_" + std::to_string(e.targetIndex),
                                               "군집 충돌로 변이체 사망");
                    }
                }
            }
            e.cooldown = e.type.cooldown;
            e.strikeTime = .24f;
        }
        return;
    }
    if (valid && std::hypot(target.x - e.x, target.y - e.y) < e.type.range - .1f &&
        e.cooldown <= 0 && map.Visible(e.x, e.y, target.x, target.y))
    {
        e.attackX = target.x;
        e.attackY = target.y;
        e.windup = e.WindupDuration();
        return;
    }
    Point goal = valid && map.CanTravel(e.x, e.y, target.x, target.y) ? target : e.route;
    const float dx = goal.x - e.x, dy = goal.y - e.y, length = std::hypot(dx, dy);
    if (length > .05f && e.flash <= 0)
    {
        const float step = std::min(length, e.type.speed * dt);
        const float oldX = e.x, oldY = e.y;
        Move(e.x, e.y, dx / length * step, dy / length * step);
        const float moved = std::hypot(e.x - oldX, e.y - oldY);
        e.facing = dx - dy < 0 ? -1.f : 1.f;
        e.yaw = std::atan2(-dx, -dy);
        e.phase += moved * e.type.gait;
        e.movement = moved > .001f ? 1.f : 0.f;
    }
}

bool LevelOne::UpdateCitizenWildlife(WorldSociety::Npc& n, float dt)
{
    PROFILE_SCOPE(NpcCombat);
    n.wildDecision = std::max(-.5f, n.wildDecision - dt);
    if (n.wildDecision <= 0 && n.windup <= 0)
    {
        const int previous = n.wildTarget;
        n.wildTarget = -1;
        float best = n.action == WorldSociety::Action::Combat && n.target >= 0 ? 3.f : 7.f;
        int woundedAlly = -1;
        for (const auto& candidate : EcologyNearby(n.x, n.y))
        {
            if (candidate.first != 3)
            {
                continue;
            }
            const auto& e = enemies[candidate.second];
            const float distance = std::hypot(n.x - e.x, n.y - e.y);
            if (!e.dead && e.health < e.maximumHealth && distance < 2 &&
                society.WildlifeAffinity(n.faction, e.type.id) >= 0 && e.targetKind == 0 &&
                map.Visible(n.x, n.y, e.x, e.y))
            {
                woundedAlly = candidate.second;
            }
            if (!e.dead && distance < best && society.WildlifeAffinity(n.faction, e.type.id) < 0 &&
                map.Visible(n.x, n.y, e.x, e.y))
            {
                best = distance;
                n.wildTarget = candidate.second;
            }
        }
        n.wildDecision = .5f;
        if (n.wildTarget < 0 && woundedAlly >= 0 && n.cooldown <= 0 && n.food > 2 &&
            n.traits[2] >= .45f)
        {
            auto& ally = enemies[woundedAlly];
            n.food -= 1;
            ally.health = std::min(ally.maximumHealth, ally.health + 12);
            n.cooldown = 12;
            n.speech = "이 생명도 굶주리고 있소.";
            n.speechTime = 5;
            society.WildlifeRecord(n.id, "monster_" + std::to_string(woundedAlly),
                                   "식량 1을 나누어 공존하는 변이체를 돌봄");
        }
        if (previous != n.wildTarget && n.wildTarget >= 0)
        {
            society.WildlifeRecord(n.id, "monster_" + std::to_string(n.wildTarget),
                                   "주변 변이체 위협을 인지함");
        }
    }
    if (n.wildTarget < 0 || n.wildTarget >= int(enemies.size()))
    {
        return false;
    }
    auto& e = enemies[n.wildTarget];
    if (e.dead || !e.IsActive() || society.WildlifeAffinity(n.faction, e.type.id) >= 0 ||
        std::hypot(n.x - e.x, n.y - e.y) > 16)
    {
        n.wildTarget = -1;
        n.windup = 0;
        n.nextDecision = 0;
        return false;
    }
    n.target = -1;
    if (n.health < 30 || n.traits[0] < .5f)
    {
        n.action = WorldSociety::Action::Flee;
        n.windup = 0;
        const float dx = n.x - e.x, dy = n.y - e.y, length = std::max(.01f, std::hypot(dx, dy));
        society.WildlifeMove(n, {n.x + dx / length * 6, n.y + dy / length * 6}, dt, map);
        return true;
    }
    n.action = WorldSociety::Action::Combat;
    n.facing = e.x - e.y < n.x - n.y ? -1.f : 1.f;
    n.yaw = std::atan2(n.x - e.x, n.y - e.y);
    if (n.windup <= 0 && (std::hypot(n.x - e.x, n.y - e.y) > society.CombatRange() ||
                          !map.Visible(n.x, n.y, e.x, e.y)))
    {
        society.WildlifeMove(n, {e.x, e.y}, dt, map);
        return true;
    }
    if (n.windup > 0)
    {
        n.windup = std::max(0.f, n.windup - dt);
        if (n.windup == 0)
        {
            ++n.attackAttempts;
            n.cooldown = society.AttackCooldownDuration();
            n.strikeTime = .3f;
            if (std::hypot(n.x - e.x, n.y - e.y) > society.CombatRange() + .35f ||
                !map.Visible(n.x, n.y, e.x, e.y))
            {
                ++n.attackMisses;
                n.lastAttack =
                    "monster_" + std::to_string(n.wildTarget) + ": miss (range or line of sight)";
                society.WildlifeRecord(n.id, "monster_" + std::to_string(n.wildTarget),
                                       n.lastAttack);
                return true;
            }
            const int before = e.health;
            e.health = std::max(0, e.health - std::max(1, int(n.attack) - e.type.defense));
            e.flash = .18f;
            ++n.attackHits;
            n.damageDealt += before - e.health;
            n.lastAttack = "monster_" + std::to_string(n.wildTarget) + ": hit " +
                           std::to_string(before - e.health);
            SpawnNumber(e.x, e.y, .8f, "-" + std::to_string(before - e.health), false);
            n.cooldown = society.AttackCooldownDuration();
            Profile::Add(Profile::Metric::NpcStrikes);
            society.WildlifeRecord(n.id, "monster_" + std::to_string(n.wildTarget),
                                   "변이체와 교전 · 체력 " + std::to_string(before) + " → " +
                                       std::to_string(e.health));
            if (!e.health)
            {
                Kill(e, false);
                society.WildlifeRecord(n.id, "monster_" + std::to_string(n.wildTarget),
                                       "주민이 변이체를 처치함");
            }
        }
    }
    else if (n.cooldown <= 0)
    {
        n.windup = society.AttackWindupDuration();
    }
    return true;
}
