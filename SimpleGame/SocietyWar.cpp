#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <set>

bool WorldSociety::CampaignGoal(int index, Point& goal) const
{
    const auto& n = npcs[index];
    if (!n.Alive() || n.role != "guard")
    {
        return false;
    }
    for (const auto& c : campaigns)
    {
        if (!c.active || (n.faction != c.attacker && n.faction != c.defender))
        {
            continue;
        }
        if (n.health < 35 || n.food < 1)
        {
            goal = n.faction == c.attacker ? c.attackRally : c.defendRally;
        }
        else if (c.stage == 0)
        {
            goal = n.faction == c.attacker ? c.attackRally : c.defendRally;
        }
        else if (c.stage < 3)
        {
            goal = {(c.attackRally.x + c.defendRally.x) * .5f,
                    (c.attackRally.y + c.defendRally.y) * .5f};
        }
        else
        {
            const int target = c.stage == 4 ? factions[c.attacker].home : c.region;
            goal = {regions[target].x, regions[target].y};
        }
        // Stable formation slots prevent all soldiers walking onto one point.
        goal.x += float(index % 3 - 1) * 1.1f;
        goal.y += float(index % 2) * 1.1f;
        return true;
    }
    return false;
}

bool WorldSociety::CampaignOrder(int index)
{
    auto& n = npcs[index];
    Point goal;
    if (!CampaignGoal(index, goal))
    {
        return false;
    }
    // Querying an order must not erase a live combat target.
    if (n.action == Action::Combat || n.action == Action::Raid || n.action == Action::Flee ||
        n.wildTarget >= 0)
    {
        return true;
    }
    for (const auto& c : campaigns)
    {
        if (!c.active || (n.faction != c.attacker && n.faction != c.defender))
        {
            continue;
        }
        if (n.action != Action::Patrol || n.destination != c.region)
        {
            Record("campaign_order", factions[n.faction].id, n.id, "local", regions[c.region].id,
                   "병력이 집결지와 전선으로 이동");
        }
        n.action = Action::Patrol;
        n.destination = c.region;
        n.target = -1;
        return true;
    }
    return false;
}

void WorldSociety::CampaignTick(WorldSimulation& world)
{
    if (conflictEnabled)
    {
        return;
    }

    PROFILE_SCOPE(WarDecision);
    auto strength = [&](int faction)
    {
        float result = 0;
        for (const auto& n : npcs)
        {
            if (n.Alive() && n.faction == faction && n.role == "guard")
            {
                result += n.health / 100.f;
            }
        }
        return result;
    };
    for (auto& c : campaigns)
    {
        if (!c.active)
        {
            continue;
        }
        auto& a = factions[c.attacker];
        auto& b = factions[c.defender];
        const bool captured = c.stage == 4 ? regions[a.home].owner == c.defender
                                           : regions[c.region].owner == c.attacker;
        const bool exhausted = a.supplies < warCost || b.supplies < warCost ||
                               strength(c.attacker) < 1 || strength(c.defender) < 1;
        const bool settled =
            diplomacy[c.attacker][c.defender] >= -20 || diplomacy[c.defender][c.attacker] >= -20;
        if (!a.active || !b.active || captured || exhausted || settled)
        {
            c.active = false;
            c.until = minute + truceMinutes;
            diplomacy[c.attacker][c.defender] = -20;
            diplomacy[c.defender][c.attacker] = -20;
            Record("armistice", a.id, b.id, "war", "truce",
                   captured    ? "거점 통제권 변경 후 휴전"
                   : exhausted ? "병력·보급 손실로 철수와 휴전"
                               : "협상 또는 세력 변화로 교전 종료");
            for (auto& n : npcs)
            {
                if (n.role == "guard" && (n.faction == c.attacker || n.faction == c.defender))
                {
                    n.action = Action::Travel;
                    n.destination = n.home;
                    n.target = -1;
                    n.windup = 0;
                    n.nextDecision = minute + rules.decisionPeriod * 3;
                }
            }
            continue;
        }
        const Point front{(c.attackRally.x + c.defendRally.x) * .5f,
                          (c.attackRally.y + c.defendRally.y) * .5f};
        int readyA = 0, readyB = 0, fieldA = 0, fieldB = 0;
        for (const auto& n : npcs)
        {
            if (!n.Alive() || n.role != "guard" || n.health < 35)
            {
                continue;
            }
            if (n.faction == c.attacker)
            {
                readyA += std::hypot(n.x - c.attackRally.x, n.y - c.attackRally.y) < 12;
                fieldA += std::hypot(n.x - front.x, n.y - front.y) < 14;
            }
            if (n.faction == c.defender)
            {
                readyB += std::hypot(n.x - c.defendRally.x, n.y - c.defendRally.y) < 12;
                fieldB += std::hypot(n.x - front.x, n.y - front.y) < 14;
            }
        }
        const int previousStage = c.stage;
        if (c.stage == 0 && ((readyA >= 2 && readyB >= 1) || minute >= c.stageSince + 120))
        {
            c.stage = 1;
        }
        else if (c.stage == 1 && fieldA > 0 && fieldB > 0)
        {
            c.stage = 2;
        }
        else if (c.stage == 2 && fieldA >= 2 && fieldB == 0)
        {
            c.stage = 3;
        }
        else if (c.stage == 2 && fieldB >= 2 && fieldA == 0)
        {
            c.stage = 4;
        }
        if (c.stage != previousStage)
        {
            c.stageSince = minute;
            Record("campaign_stage", a.id, b.id, std::to_string(previousStage),
                   std::to_string(c.stage),
                   c.stage == 1   ? "집결 후 전선으로 이동"
                   : c.stage == 2 ? "양측 병력 전선 접촉"
                   : c.stage == 4 ? "수비대가 전선을 확보하고 공격 측 거점으로 반격"
                                  : "수비선 이탈을 확인하고 거점으로 진격");
        }
        const float beforeA = a.supplies, beforeB = b.supplies;
        a.supplies = std::max(0.f, a.supplies - warCost);
        b.supplies = std::max(0.f, b.supplies - warCost * .5f);
        const auto cause = Record("war_supply", a.id, b.id,
                                  std::to_string(beforeA) + "/" + std::to_string(beforeB),
                                  std::to_string(a.supplies) + "/" + std::to_string(b.supplies),
                                  "원정대와 수비대의 식량 보급 소비");
        world.ChangeRegion(regions[c.region].id, -.003, 0, a.id, "분쟁으로 지역 식량 소비", cause);
    }

    // A declaration requires local contact, not knowledge of distant live positions.
    std::set<std::pair<int, int>> contact;
    for (const auto& n : npcs)
    {
        if (!n.Alive())
        {
            continue;
        }
        for (int other : Nearby(n))
        {
            const int a = n.faction, b = npcs[other].faction;
            if (a != b)
            {
                contact.emplace(std::min(a, b), std::max(a, b));
            }
        }
    }
    for (const auto& pair : contact)
    {
        int a = pair.first, b = pair.second;
        if (!factions[a].active || !factions[b].active ||
            std::min(diplomacy[a][b], diplomacy[b][a]) > warThreshold)
        {
            continue;
        }
        auto found = std::find_if(campaigns.begin(), campaigns.end(),
                                  [&](const Campaign& c)
                                  {
                                      return std::min(c.attacker, c.defender) == a &&
                                             std::max(c.attacker, c.defender) == b;
                                  });
        if (found != campaigns.end() && (found->active || minute < found->until))
        {
            continue;
        }
        // Avoid giving one small faction conflicting simultaneous expedition orders.
        if (std::any_of(campaigns.begin(), campaigns.end(),
                        [&](const Campaign& c)
                        {
                            return c.active && (c.attacker == a || c.defender == a ||
                                                c.attacker == b || c.defender == b);
                        }))
        {
            continue;
        }
        const int la = factions[a].leader, lb = factions[b].leader;
        if (la < 0 || lb < 0)
        {
            continue;
        }
        if (npcs[lb].traits[4] > npcs[la].traits[4])
        {
            std::swap(a, b);
        }
        const auto& leader = npcs[factions[a].leader];
        const auto* food = world.FindRegion(regions[factions[a].home].id);
        if (leader.traits[4] < .6f && (!food || food->food > .35))
        {
            continue;
        }
        const bool defendingWounded = std::any_of(npcs.begin(), npcs.end(),
                                                  [&](const Npc& n)
                                                  {
                                                      return n.Alive() && n.faction == a &&
                                                             n.health < 45 &&
                                                             n.region == factions[a].home;
                                                  });
        if (StoryRestraint(factions[a].leader) >= .5f && !defendingWounded)
        {
            Profile::Event("story_war_reconsidered", "leader_id=" + leader.id +
                                                         " world_minute=" + std::to_string(minute) +
                                                         " reason=credible_counterevidence");
            continue;
        }
        if (strength(a) < 2 || strength(b) < 1 || factions[a].supplies < warReserve)
        {
            continue;
        }
        int destination = -1;
        float nearest = 1e9f;
        for (std::size_t r = 0; r < regions.size(); ++r)
        {
            const float distance = std::hypot(regions[r].x - regions[factions[a].home].x,
                                              regions[r].y - regions[factions[a].home].y);
            if (regions[r].owner == b && distance < nearest)
            {
                nearest = distance;
                destination = static_cast<int>(r);
            }
        }
        if (destination < 0)
        {
            continue;
        }
        Campaign c{a, b, destination, true, minute};
        c.stageSince = minute;
        c.attackRally = {regions[factions[a].home].x, regions[factions[a].home].y};
        c.defendRally = {regions[destination].x, regions[destination].y};
        for (const auto& front : frontiers)
        {
            if (regions[front.region].owner == b && ((front.first == a && front.second == b) ||
                                                     (front.first == b && front.second == a)))
            {
                c.region = destination = front.region;
                c.attackRally = front.first == a ? front.firstRally : front.secondRally;
                c.defendRally = front.first == b ? front.firstRally : front.secondRally;
                break;
            }
        }
        if (found == campaigns.end())
        {
            campaigns.push_back(c);
        }
        else
        {
            *found = c;
        }
        diplomacy[a][b] = std::min(diplomacy[a][b], warThreshold);
        diplomacy[b][a] = std::min(diplomacy[b][a], warThreshold);
        Record("war_declared", factions[a].id, factions[b].id, "dispute", regions[destination].id,
               "현지 접촉과 적대 관계·이념·자원 판단으로 원정 결정");
    }
    for (std::size_t i = 0; i < npcs.size(); ++i)
    {
        if (npcs[i].Alive() && npcs[i].action != Action::Combat && npcs[i].action != Action::Flee &&
            npcs[i].wildTarget < 0)
        {
            CampaignOrder(static_cast<int>(i));
        }
    }
    Profile::Set(Profile::Metric::ActiveWars,
                 static_cast<double>(std::count_if(campaigns.begin(), campaigns.end(),
                                                   [](const Campaign& c)
                                                   {
                                                       return c.active;
                                                   })));
}
