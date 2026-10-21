#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "RandomLevelMap.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

bool WorldSociety::TickConflictActor(int index, float dt, WorldSimulation& world,
                                     const RandomLevelMap& map, Point player)
{
    if (!conflictEnabled)
    {
        return false;
    }
    PROFILE_SCOPE(ConflictCombat);
    auto& n = npcs[index];
    const bool hive = IsHive(index);
    n.effectTime = std::max(0.f, n.effectTime - dt);
    if (n.windup > 0)
    {
        const auto& attack = conflictAttacks[n.attackPattern];
        n.windup = std::max(0.f, n.windup - dt);
        // Leap movement is collision checked and goes to the telegraphed position.
        if (n.attackPattern == 4 && n.windup < n.attackDuration * .4f)
        {
            const float speed = n.speed;
            n.speed *= 5;
            MoveTo(n, n.attackPoint, dt, map);
            n.speed = speed;
        }
        if (n.windup > 0)
        {
            return true;
        }
        n.strikeTime = .3f;
        n.effectTime = .55f;
        n.cooldown = attack.cooldown;
        int hits = 0;
        const Point origin = n.attackOrigin;
        const float dx = n.attackPoint.x - origin.x, dy = n.attackPoint.y - origin.y;
        const float length = std::max(.01f, std::hypot(dx, dy));
        auto inAttack = [&](Point target)
        {
            if (!map.Visible(origin.x, origin.y, target.x, target.y))
            {
                return false;
            }
            const float tx = target.x - origin.x, ty = target.y - origin.y;
            const float forward = (tx * dx + ty * dy) / length;
            const float side = std::abs(tx * dy - ty * dx) / length;
            if (n.attackPattern == 2)
            {
                return forward >= -.2f && forward <= attack.range && side <= attack.radius;
            }
            if (n.attackPattern == 1 || n.attackPattern == 3)
            {
                return forward >= -.2f && std::hypot(tx, ty) <= attack.range &&
                       side <= attack.radius + forward * .4f;
            }
            if (n.attackPattern == 4 &&
                std::hypot(n.x - n.attackPoint.x, n.y - n.attackPoint.y) > 1.5f)
            {
                return false;
            }
            return std::hypot(target.x - n.attackPoint.x, target.y - n.attackPoint.y) <=
                   attack.radius;
        };
        // Bounded perception buckets provide victims; no view-dependent combat authority.
        for (int other : Nearby(n))
        {
            if (npcs[other].faction == n.faction || !inAttack({npcs[other].x, npcs[other].y}))
            {
                continue;
            }
            const float before = npcs[other].health;
            Damage(index, other, n.attack * attack.damage, world);
            n.damageDealt += before - npcs[other].health;
            ++hits;
        }
        if ((hive || n.playerThreat) && inAttack(player))
        {
            playerDamage += std::max(1, int(n.attack * attack.damage));
            ++hits;
        }
        if (n.attackPattern == 3)
        {
            BurnContamination(n.attackPoint, attack.radius + 1, index);
        }
        if (n.attackPattern == 7)
        {
            const int cell = int(n.attackPoint.y / conflictCell) * contaminationWidth +
                             int(n.attackPoint.x / conflictCell);
            if (cell >= 0 && cell < int(contamination.size()) && contamination[cell].scar <= 0 &&
                map.CanStand(n.attackPoint.x, n.attackPoint.y))
            {
                contamination[cell].density = std::min(1.f, contamination[cell].density + .5f);
            }
        }
        n.attackHits += hits;
        n.attackMisses += hits == 0;
        n.lastAttack =
            std::string(AttackPatternName(n.attackPattern)) + (hits ? " · 명중" : " · 회피/빗나감");
        Record("faction_attack_resolved", n.id, "area", AttackPatternName(n.attackPattern),
               std::to_string(hits), "고정된 예고 위치 · 거리/시야/진영으로 실제 피해 판정");
        Profile::Add(Profile::Metric::ConflictStrikes);
        return true;
    }
    int target = -1;
    float nearest = 14;
    Point goal{n.x, n.y};
    for (int other : Nearby(n))
    {
        const auto& enemy = npcs[other];
        const float distance = std::hypot(enemy.x - n.x, enemy.y - n.y);
        if (enemy.faction != n.faction && distance < nearest &&
            map.Visible(n.x, n.y, enemy.x, enemy.y))
        {
            target = other;
            nearest = distance;
            goal = {enemy.x, enemy.y};
        }
    }
    const float playerDistance = std::hypot(player.x - n.x, player.y - n.y);
    const bool againstPlayer = (hive || n.playerThreat) && playerDistance < nearest &&
                               map.Visible(n.x, n.y, player.x, player.y);
    if (againstPlayer)
    {
        nearest = playerDistance;
        goal = player;
    }
    bool engagement = target >= 0 || againstPlayer;
    n.target = againstPlayer ? -1 : target;
    if (!engagement && n.combatClass == 0)
    {
        n.action = Action::Rest;
        return false;
    }
    int pattern = 1;
    const bool alternate = (n.attackSequence % 2) != 0;
    switch (n.combatClass)
    {
    case 2:
        pattern = alternate ? 1 : 2;
        break;
    case 3:
        pattern = alternate ? 2 : 3;
        break;
    case 4:
        pattern = alternate ? 1 : 4;
        break;
    case 5:
        pattern = alternate ? 7 : 5;
        break;
    case 6:
        pattern = alternate ? 1 : 6;
        break;
    case 7:
        pattern = alternate ? 5 : 7;
        break;
    default:
        pattern = alternate ? 2 : 1;
        break;
    }
    if (!engagement)
    {
        // Regional enemy strongholds are authored strategic knowledge, not remote NPC sensing.
        float best = 1e10f;
        for (const auto& region : regions)
        {
            if (region.owner == n.faction)
            {
                continue;
            }
            const float distance = std::hypot(region.x - n.x, region.y - n.y);
            if (distance < best)
            {
                best = distance;
                goal = {region.x, region.y};
            }
        }
        if (!hive)
        {
            float dirtyDistance = 18;
            for (int i = 0; i < int(contamination.size()); ++i)
            {
                if (contamination[i].density < .08f)
                {
                    continue;
                }
                Point center{(i % contaminationWidth + .5f) * conflictCell,
                             (i / contaminationWidth + .5f) * conflictCell};
                const float distance = std::hypot(center.x - n.x, center.y - n.y);
                if (distance < dirtyDistance && map.Visible(n.x, n.y, center.x, center.y))
                {
                    dirtyDistance = distance;
                    goal = center;
                    pattern = 3;
                    engagement = true;
                }
            }
        }
        // Soldiers return to ordinary needs when they are not under immediate threat.
        if (!engagement && !hive && HasLife() &&
            (lives[index].satiation < planningRules.hunger || lives[index].fatigue > 85))
        {
            n.action = Action::Rest;
            return false;
        }
    }
    n.action = engagement ? Action::Combat : Action::Raid;
    const auto& attack = conflictAttacks[pattern];
    const float distance = std::hypot(goal.x - n.x, goal.y - n.y);
    if (engagement && distance <= attack.range && n.cooldown <= 0 &&
        map.Visible(n.x, n.y, goal.x, goal.y))
    {
        n.attackPattern = pattern;
        n.attackOrigin = {n.x, n.y};
        n.attackPoint = goal;
        n.attackDuration = n.windup = attack.windup;
        n.yaw = std::atan2(n.x - goal.x, n.y - goal.y);
        ++n.attackSequence;
        ++n.attackAttempts;
        n.lastAttack = std::string(AttackPatternName(pattern)) + " · 예고";
        if (conversationNpc == index)
        {
            EndConversation();
        }
        return true;
    }
    if (distance > (engagement ? attack.range * .8f : 1.f))
    {
        MoveTo(n, goal, dt, map);
    }
    return true;
}
