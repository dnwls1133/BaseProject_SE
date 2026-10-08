#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "RandomLevelMap.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <limits>
#include <set>

void WorldSociety::Damage(int attacker, int target, float amount, WorldSimulation& world,
                          const std::string& wildlife)
{
    if (target < 0 || target >= static_cast<int>(npcs.size()))
    {
        return;
    }
    auto& victim = npcs[target];
    if (!victim.Alive())
    {
        return;
    }
    const float before = victim.health;
    victim.health = std::max(0.f, before - amount);
    const bool starvation = wildlife == "굶주림";
    if (!starvation)
    {
        victim.flash = .2f;
    }
    const std::string actor = !wildlife.empty() ? wildlife
                              : attacker < 0    ? "player"
                                                : npcs[attacker].id;
    const auto cause =
        Record("damage", actor, victim.id, std::to_string(before), std::to_string(victim.health),
               starvation ? "지속된 굶주림" : "무력 충돌");
    regions[victim.region].fear =
        std::min(1.f, regions[victim.region].fear + (starvation ? .005f : .03f));
    if (attacker >= 0)
    {
        Relate(target, attacker, -15, 8, "공격을 받음", cause);
        diplomacy[victim.faction][npcs[attacker].faction] =
            std::max(-100.f, diplomacy[victim.faction][npcs[attacker].faction] - 2);
    }
    else if (wildlife.empty())
    {
        victim.playerThreat = true;
        victim.playerRelation[0] = std::max(-100.f, victim.playerRelation[0] - 25);
        victim.playerRelation[3] = std::min(100.f, victim.playerRelation[3] + 15);
        karma[0] = std::max(-100.f, karma[0] - 3);
    }
    if (!victim.Alive())
    {
        victim.action = Action::Rest;
        victim.target = -1;
        victim.windup = 0;
        Record("death", actor, victim.id, "alive", "dead", "사망이 세계 상태에 반영됨", cause);
        auto& damagedRegion = regions[victim.region];
        if (!starvation)
        {
            damagedRegion.wall = std::max(0.f, damagedRegion.wall - .08f);
            damagedRegion.storehouse = std::max(0.f, damagedRegion.storehouse - .03f);
            damagedRegion.hospital = std::max(0.f, damagedRegion.hospital - .02f);
        }
        damagedRegion.security = std::max(0.f, damagedRegion.security - .08f);
        world.ChangeRegion(regions[victim.region].id, -.015, .005, actor, "주민 사망", cause);
        for (auto& quest : quests)
        {
            if ((quest.issuer == target || quest.target == target) &&
                static_cast<int>(quest.state) < static_cast<int>(QuestState::Completed))
            {
                ChangeQuest(quest, QuestState::Failed, "관련 인물이 사망함");
            }
        }
        for (auto& npc : npcs)
        {
            if (npc.target == target)
            {
                npc.target = -1;
                npc.action = Action::Rest;
                npc.windup = 0;
            }
        }
    }
}

void WorldSociety::MoveTo(Npc& npc, Point goal, float dt, const RandomLevelMap& map)
{
    npc.pathRetry = std::max(0.f, npc.pathRetry - dt);
    const float goalX = goal.x - npc.x, goalY = goal.y - npc.y;
    const float goalDistance = std::hypot(goalX, goalY);
    if (goalDistance < .1f)
    {
        return;
    }
    // Finish at the reserved work position rather than the navigation cell center.
    if (goalDistance < 4 && map.CanTravel(npc.x, npc.y, goal.x, goal.y))
    {
        const float stride = std::min(goalDistance, npc.speed * dt);
        npc.x += goalX / goalDistance * stride;
        npc.y += goalY / goalDistance * stride;
        npc.yaw = std::atan2(-goalX, -goalY);
        npc.facing = goalX - goalY < 0 ? -1.f : 1.f;
        npc.phase += stride * 5;
        npc.movement = 1;
        npc.path.clear();
        npc.pathCell = -1;
        npc.waypoint = 0;
        return;
    }
    int gx = std::clamp(static_cast<int>(goal.x), 1, map.Width - 2);
    int gy = std::clamp(static_cast<int>(goal.y), 1, map.Height - 2);
    if (!map.CanStand(gx + .5f, gy + .5f))
    {
        float best = 1e10f;
        const int centerX = gx, centerY = gy;
        for (int y = centerY - 3; y <= centerY + 3; ++y)
        {
            for (int x = centerX - 3; x <= centerX + 3; ++x)
            {
                const float d = std::hypot(x + .5f - goal.x, y + .5f - goal.y);
                if (d < best && map.CanStand(x + .5f, y + .5f))
                {
                    best = d;
                    gx = x;
                    gy = y;
                }
            }
        }
    }
    const int cell = gy * map.Width + gx;
    const bool needsPath = npc.pathCell != cell || npc.waypoint >= npc.path.size();
    if (needsPath && npc.pathRetry <= 0 && pathBudget > 0 && pathNodeBudget > 0)
    {
        PROFILE_SCOPE(NpcPath);
        --pathBudget;
        Profile::Add(Profile::Metric::NpcPathBuilds);
        const int size = map.Width * map.Height;
        if (int(pathParents.size()) != size)
        {
            pathParents.resize(size);
            pathCosts.resize(size);
            pathStamps.assign(size, 0);
        }
        if (++pathStamp == 0)
        {
            std::fill(pathStamps.begin(), pathStamps.end(), 0);
            ++pathStamp;
        }
        const int start = int(npc.y) * map.Width + int(npc.x);
        auto heuristic = [&](int at)
        {
            return std::abs(at % map.Width - gx) + std::abs(at / map.Width - gy);
        };
        using Entry = std::pair<int, int>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
        pathParents[start] = start;
        pathCosts[start] = 0;
        pathStamps[start] = pathStamp;
        open.push({heuristic(start), start});
        constexpr int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        int best = start, expanded = 0;
        bool reached = false;
        const auto started = Profile::Clock::now();
        while (!open.empty() && pathNodeBudget > 0)
        {
            const auto entry = open.top();
            open.pop();
            const int current = entry.second;
            if (entry.first != pathCosts[current] + heuristic(current))
            {
                continue;
            }
            if (heuristic(current) < heuristic(best))
            {
                best = current;
            }
            if (current == cell)
            {
                reached = true;
                break;
            }
            --pathNodeBudget;
            ++expanded;
            for (const auto& offset : offsets)
            {
                const int x = current % map.Width + offset[0];
                const int y = current / map.Width + offset[1];
                if (!map.CanStand(x + .5f, y + .5f))
                {
                    continue;
                }
                const int next = y * map.Width + x;
                const int cost = pathCosts[current] + 1;
                if (pathStamps[next] == pathStamp && pathCosts[next] <= cost)
                {
                    continue;
                }
                pathStamps[next] = pathStamp;
                pathParents[next] = current;
                pathCosts[next] = cost;
                open.push({cost + heuristic(next), next});
            }
            if ((expanded & 31) == 0 &&
                std::chrono::duration<double, std::milli>(Profile::Clock::now() - started)
                        .count() >= 1)
            {
                pathNodeBudget = 0;
                break;
            }
        }
        Profile::Add(Profile::Metric::NpcPathNodes, expanded);
        // A bounded search may return a useful prefix, never claim goal arrival.
        if (reached || (!open.empty() && best != start))
        {
            npc.path.clear();
            npc.waypoint = 0;
            npc.pathCell = cell;
            npc.pathFailures = 0;
            for (int at = reached ? cell : best; at != start; at = pathParents[at])
            {
                npc.path.push_back({at % map.Width + .5f, at / map.Width + .5f});
            }
            std::reverse(npc.path.begin(), npc.path.end());
            if (!reached)
            {
                Profile::Add(Profile::Metric::NpcPathPartial);
            }
        }
        else
        {
            npc.pathRetry = std::min(6.f, .5f * ++npc.pathFailures);
            Profile::Add(open.empty() ? Profile::Metric::NpcPathFailed
                                      : Profile::Metric::NpcPathDeferred);
        }
    }
    else if (needsPath && npc.pathRetry <= 0)
    {
        Profile::Add(Profile::Metric::NpcPathDeferred);
    }
    if (npc.waypoint >= npc.path.size())
    {
        return;
    }
    const Point destination = npc.path[npc.waypoint];
    const float dx = destination.x - npc.x, dy = destination.y - npc.y;
    const float distance = std::hypot(dx, dy);
    const float step = std::min(distance, npc.speed * dt);
    if (distance < .01f)
    {
        ++npc.waypoint;
        return;
    }
    const float x = npc.x + dx / distance * step, y = npc.y + dy / distance * step;
    if (map.CanTravel(npc.x, npc.y, x, y))
    {
        npc.yaw = std::atan2(-dx, -dy);
        npc.x = x;
        npc.y = y;
        npc.movement = 1;
        npc.phase += step * 5;
        if (std::abs(dx - dy) > .01f)
        {
            npc.facing = dx - dy < 0 ? -1.f : 1.f;
        }
        if (step >= distance - .01f)
        {
            ++npc.waypoint;
        }
    }
    else
    {
        npc.pathCell = -1;
        const float centerX = std::floor(npc.x) + .5f, centerY = std::floor(npc.y) + .5f;
        const float offsetX = centerX - npc.x, offsetY = centerY - npc.y;
        const float length = std::hypot(offsetX, offsetY);
        if (length > .001f)
        {
            const float stride = std::min(length, npc.speed * dt);
            const float nextX = npc.x + offsetX / length * stride,
                        nextY = npc.y + offsetY / length * stride;
            if (map.CanTravel(npc.x, npc.y, nextX, nextY))
            {
                npc.x = nextX;
                npc.y = nextY;
                npc.phase += stride * 5;
                npc.movement = 1;
            }
        }
    }
}

void WorldSociety::TickActor(std::size_t index, float dt, WorldSimulation& world,
                             const RandomLevelMap& map, Point player,
                             const std::function<bool(Npc&, float)>& wildlife)
{
    PROFILE_SCOPE(NpcMotion);
    auto& npc = npcs[index];
    npc.speechTime = std::max(0.f, npc.speechTime - dt);
    npc.flash = std::max(0.f, npc.flash - dt);
    npc.movement = 0;
    if (!npc.Alive())
    {
        npc.collapse = std::min(1.f, npc.collapse + dt * 2);
        return;
    }
    const bool near = std::hypot(npc.x - player.x, npc.y - player.y) < 32;
    if (!near)
    {
        npc.farTime += dt;
        if (npc.farTime < .25f)
        {
            return;
        }
        dt = npc.farTime;
        npc.farTime = 0;
    }
    else
    {
        dt += npc.farTime;
        npc.farTime = 0;
    }
    npc.cooldown = std::max(0.f, npc.cooldown - dt);
    if (wildlife(npc, dt))
    {
        if (conversationNpc == int(index))
        {
            EndConversation();
        }
        return;
    }
    if (npc.playerThreat && std::hypot(npc.x - player.x, npc.y - player.y) < 18)
    {
        PROFILE_SCOPE(NpcCombat);
        if (npc.health < 30 && npc.traits[0] < .6f)
        {
            npc.action = Action::Flee;
            npc.target = -1;
            MoveTo(npc, {regions[npc.home].x, regions[npc.home].y}, dt, map);
            return;
        }
        npc.action = Action::Combat;
        if (std::hypot(npc.x - player.x, npc.y - player.y) <= rules.combatRange &&
            map.Visible(npc.x, npc.y, player.x, player.y))
        {
            if (npc.windup > 0)
            {
                npc.windup -= dt;
                if (npc.windup <= 0)
                {
                    playerDamage += static_cast<int>(npc.attack);
                    npc.cooldown = rules.attackCooldown;
                    Profile::Add(Profile::Metric::NpcStrikes);
                    Record("retaliation", npc.id, "player", "windup", "strike",
                           "공격한 플레이어에게 반격");
                }
            }
            else if (npc.cooldown <= 0)
            {
                npc.windup = rules.attackWindup;
            }
        }
        else
        {
            npc.windup = 0;
            MoveTo(npc, player, dt, map);
        }
        return;
    }
    if (HasLife() && npc.action != Action::Combat && npc.action != Action::Flee &&
        npc.action != Action::Raid && npc.action != Action::Escort)
    {
        for (std::size_t other = 0; other < lives.size(); ++other)
        {
            const auto& delivery = lives[other];
            if (delivery.task == LifeTask::Deliver && delivery.partner == int(index) &&
                delivery.cargo > 0 && npcs[other].Alive() && minute < delivery.until &&
                std::hypot(npc.x - npcs[other].x, npc.y - npcs[other].y) < 3 &&
                map.Visible(npc.x, npc.y, npcs[other].x, npcs[other].y))
            {
                npc.yaw = std::atan2(npc.x - npcs[other].x, npc.y - npcs[other].y);
                return;
            }
        }
    }
    if (conversationNpc == int(index))
    {
        if (!IsConversationActive(int(index)) ||
            std::hypot(npc.x - player.x, npc.y - player.y) > 5 ||
            !map.Visible(npc.x, npc.y, player.x, player.y))
        {
            EndConversation();
        }
        else
        {
            npc.yaw = std::atan2(npc.x - player.x, npc.y - player.y);
            return;
        }
    }
    if (HasLife() && npc.action != Action::Combat && npc.action != Action::Raid &&
        npc.action != Action::Flee && npc.action != Action::Escort && !CampaignOrder(int(index)))
    {
        TickLife(int(index), dt, world, map);
        return;
    }
    Point goal{regions[npc.destination].x, regions[npc.destination].y};
    if (npc.action == Action::Patrol)
    {
        CampaignGoal(int(index), goal);
    }
    if (npc.action == Action::Escort)
    {
        goal = player;
    }
    else if (npc.target >= 0 && npc.action != Action::Flee)
    {
        goal = {npcs[npc.target].x, npcs[npc.target].y};
    }
    const float distance = std::hypot(npc.x - goal.x, npc.y - goal.y);
    if ((npc.action == Action::Combat || npc.action == Action::Raid) && npc.target >= 0)
    {
        PROFILE_SCOPE(NpcCombat);
        auto& target = npcs[npc.target];
        if (!target.Alive())
        {
            npc.target = -1;
            npc.action = Action::Rest;
            npc.windup = 0;
            return;
        }
        if (distance <= rules.combatRange && map.Visible(npc.x, npc.y, target.x, target.y))
        {
            if (npc.windup > 0)
            {
                npc.windup -= dt;
                if (npc.windup <= 0)
                {
                    npc.cooldown = rules.attackCooldown;
                    Damage(static_cast<int>(index), npc.target, npc.attack, world);
                    Profile::Add(Profile::Metric::NpcStrikes);
                }
            }
            else if (npc.cooldown <= 0)
            {
                npc.windup = rules.attackWindup;
            }
            return;
        }
        npc.windup = 0;
    }
    if ((npc.action == Action::Aid || npc.action == Action::Trade) && npc.target >= 0 &&
        distance < 2 && map.Visible(npc.x, npc.y, goal.x, goal.y))
    {
        auto& other = npcs[npc.target];
        if (other.Alive() && npc.food > 2 && npc.cooldown <= 0)
        {
            if (Trust(static_cast<int>(index), npc.target) < -30)
            {
                npc.action = Action::Rest;
                return;
            }
            const float before = other.food;
            npc.food -= 1;
            other.food += 1;
            npc.cooldown = 8;
            if (npc.action == Action::Aid)
            {
                other.health = std::min(100.f, other.health + 10);
            }
            else
            {
                // Exchange a real reserve unit; no resources are created by trade.
                auto& buyer = factions[other.faction];
                if (buyer.supplies < .25f)
                {
                    npc.food += 1;
                    other.food -= 1;
                    npc.action = Action::Rest;
                    return;
                }
                buyer.supplies -= .25f;
                factions[npc.faction].supplies += .25f;
            }
            const auto cause = Record(
                "transfer", npc.id, other.id, std::to_string(before), std::to_string(other.food),
                npc.action == Action::Aid ? "식량과 치료 지원" : "식량 거래 완료");
            Relate(npc.target, static_cast<int>(index), 8, 0, "도움을 기억함", cause);
            diplomacy[other.faction][npc.faction] =
                std::min(100.f, diplomacy[other.faction][npc.faction] + 2);
            world.ChangeRegion(regions[other.region].id, .004, 0, npc.id, "주민 식량 지원", cause);
            npc.speech = "함께 살아남읍시다.";
            npc.speechTime = 5;
            npc.action = Action::Rest;
        }
        return;
    }
    if (distance > (npc.action == Action::Escort ? 2.f : .8f))
    {
        MoveTo(npc, goal, dt, map);
    }
    else if (npc.cooldown <= 0)
    {
        if (npc.action == Action::Research)
        {
            Observe(static_cast<int>(index), world);
            const float infectionEffect = factions[npc.faction].ideology < 0 ? .002f : -.002f;
            world.ChangeRegion(regions[npc.region].id, 0, infectionEffect, npc.id,
                               "지역 조사와 치료");
            regions[npc.region].hospital = std::min(1.f, regions[npc.region].hospital + .01f);
            npc.cooldown = 10;
        }
        else if (npc.action == Action::Rest || npc.action == Action::Patrol ||
                 npc.action == Action::Travel)
        {
            auto* state = world.FindRegion(regions[npc.region].id);
            if (npc.food < 6 && state && state->food >= .01)
            {
                npc.food += 1;
                world.ChangeRegion(regions[npc.region].id, -.01, 0, npc.id, "지역 식량 수령");
            }
            if (npc.food > 0)
            {
                npc.health = std::min(100.f, npc.health + 2);
            }
            if (npc.action == Action::Patrol)
            {
                regions[npc.region].security = std::min(1.f, regions[npc.region].security + .01f);
            }
            npc.cooldown = 12;
        }
    }
}

void WorldSociety::PlayerAttack(int npc, int damage, WorldSimulation& world)
{
    Damage(-1, npc, static_cast<float>(damage), world);
}

float WorldSociety::WildlifeAffinity(int faction, const std::string& species) const
{
    if (faction < 0 || faction >= static_cast<int>(factions.size()))
    {
        return -60;
    }
    const auto found = ecology.find({factions[faction].id, species});
    if (found != ecology.end())
    {
        return found->second;
    }
    return factions[faction].parent >= 0 ? WildlifeAffinity(factions[faction].parent, species)
                                         : -60;
}

void WorldSociety::WildlifeDamage(const std::string& source, int target, float amount,
                                  WorldSimulation& world)
{
    Damage(-1, target, amount, world, source);
}

void WorldSociety::WildlifeRecord(const std::string& source, const std::string& target,
                                  const std::string& reason)
{
    Record("wildlife_interaction", source, target, "encounter", "resolved", reason);
}

void WorldSociety::WildlifeMove(Npc& npc, Point goal, float dt, const RandomLevelMap& map)
{
    MoveTo(npc, goal, dt, map);
}

void WorldSociety::ResetWildlifeTargets()
{
    for (auto& n : npcs)
    {
        if (n.wildTarget >= 0)
        {
            n.windup = 0;
            n.nextDecision = 0;
        }
        n.wildTarget = -1;
        n.wildDecision = 0;
    }
}
