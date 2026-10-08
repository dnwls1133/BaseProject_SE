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

void WorldSociety::SetFaction(int index, int faction, const std::string& reason)
{
    auto& npc = npcs[index];
    const int previous = npc.faction;
    if (previous == faction)
    {
        return;
    }
    npc.faction = faction;
    npc.nextFaction = minute + 360;
    npc.target = -1;
    npc.action = Action::Travel;
    npc.destination = factions[faction].home;
    Record("membership", npc.id, factions[faction].id, factions[previous].id, factions[faction].id,
           reason);
}

void WorldSociety::FactionTick(WorldSimulation& world)
{
    PROFILE_SCOPE(FactionDecision);
    Profile::Add(Profile::Metric::FactionDecisions);
    // Mutations occur here, outside Actor/Scene iteration. No references survive an append.
    for (std::size_t f = 0; f < factions.size(); ++f)
    {
        auto& faction = factions[f];
        if (!faction.active)
        {
            continue;
        }
        int members = 0, successor = -1;
        float support = 0, best = -1;
        for (std::size_t i = 0; i < npcs.size(); ++i)
        {
            const auto& n = npcs[i];
            if (!n.Alive() || n.faction != static_cast<int>(f))
            {
                continue;
            }
            ++members;
            support += n.food > 1 ? .8f : .2f;
            const float suitability = n.traits[0] + n.traits[2] + n.health / 100;
            if (suitability > best)
            {
                best = suitability;
                successor = static_cast<int>(i);
            }
        }
        if (!members)
        {
            faction.active = false;
            Record("faction_dissolved", faction.id, faction.id, "active", "empty",
                   "생존 구성원 없음");
            for (auto& r : regions)
            {
                if (r.owner == static_cast<int>(f))
                {
                    r.owner = -1;
                }
            }
            if (playerFaction == static_cast<int>(f))
            {
                playerFaction = -1;
            }
            continue;
        }
        faction.support = support / members;
        if (faction.leader < 0 || !npcs[faction.leader].Alive() ||
            npcs[faction.leader].faction != static_cast<int>(f))
        {
            const int old = faction.leader;
            faction.leader = successor;
            Record("leadership", faction.id, npcs[successor].id, std::to_string(old),
                   npcs[successor].id, "구성원이 후계 지도자를 선택함");
        }
        const auto* resource = world.FindRegion(regions[faction.home].id);
        if (resource && resource->food < .35 && faction.supplies > 1)
        {
            faction.supplies -= 1;
            world.ChangeRegion(regions[faction.home].id, .025, 0, faction.id, "비축 식량 배급");
        }
    }
    for (std::size_t i = 0; i < npcs.size(); ++i)
    {
        auto& npc = npcs[i];
        if (!npc.Alive() || minute < npc.nextFaction)
        {
            continue;
        }
        const int leader = factions[npc.faction].leader;
        bool dissatisfied =
            leader >= 0 && leader != static_cast<int>(i) &&
            (Trust(static_cast<int>(i), leader) < -25 ||
             (npc.food < 1 && npc.traits[6] > .65f && factions[npc.faction].support < .5f));
        if (!dissatisfied)
        {
            continue;
        }
        int willing = -1, join = -1;
        for (int other : Nearby(npc))
        {
            if (Trust(static_cast<int>(i), other) < 10)
            {
                continue;
            }
            if (npcs[other].faction == npc.faction && npcs[other].traits[6] > .5f)
            {
                willing = other;
            }
            else if (factions[npcs[other].faction].active &&
                     std::abs(factions[npc.faction].ideology -
                              factions[npcs[other].faction].ideology) < .7f)
            {
                join = npcs[other].faction;
            }
        }
        if (willing >= 0 && factions.size() < static_cast<std::size_t>(rules.maxFactions))
        {
            Faction founded = factions[npc.faction];
            founded.parent = npc.faction;
            founded.id = "community_" + std::to_string(serial);
            founded.name = npc.name + "의 공동체";
            founded.leader = static_cast<int>(i);
            founded.home = npc.region;
            founded.supplies = 0;
            founded.support = .7f;
            factions.push_back(founded);
            for (auto& row : diplomacy)
            {
                row.push_back(0);
            }
            diplomacy.emplace_back(factions.size(), 0);
            const int f = static_cast<int>(factions.size() - 1);
            diplomacy[f][f] = 100;
            SetFaction(static_cast<int>(i), f, "독립 공동체 결성");
            SetFaction(willing, f, "공동체에 자발적으로 합류");
        }
        else if (join >= 0)
        {
            SetFaction(static_cast<int>(i), join, "신뢰하는 이웃 세력으로 이적");
        }
    }
    // Delegates can negotiate only after local contact. Contact pairs are region-indexed.
    std::set<std::pair<int, int>> contacts;
    for (const auto& npc : npcs)
    {
        if (!npc.Alive())
        {
            continue;
        }
        for (int other : Nearby(npc))
        {
            const int a = npc.faction, b = npcs[other].faction;
            if (a != b)
            {
                contacts.emplace(std::min(a, b), std::max(a, b));
            }
        }
    }
    for (const auto& pair : contacts)
    {
        const int a = pair.first, b = pair.second;
        if (!factions[a].active || !factions[b].active)
        {
            continue;
        }
        const int la = factions[a].leader, lb = factions[b].leader;
        if (la < 0 || lb < 0 || !npcs[la].Alive() || !npcs[lb].Alive())
        {
            continue;
        }
        const float compatibility = 1 - std::abs(factions[a].ideology - factions[b].ideology);
        const float diplomacyScore = compatibility + npcs[la].traits[2] + npcs[lb].traits[2] -
                                     (npcs[la].traits[4] + npcs[lb].traits[4]) * .4f;
        const float before = diplomacy[a][b];
        const float delta = diplomacyScore > .7f ? 4.f : diplomacyScore < -.3f ? -3.f : 0.f;
        diplomacy[a][b] = std::clamp(diplomacy[a][b] + delta, -100.f, 100.f);
        diplomacy[b][a] = std::clamp(diplomacy[b][a] + delta, -100.f, 100.f);
        if (delta != 0)
        {
            Record("faction_negotiation", factions[a].id, factions[b].id, std::to_string(before),
                   std::to_string(diplomacy[a][b]),
                   delta > 0 ? "접촉한 대표단의 협상" : "이념 충돌로 관계 악화");
        }
        if (before < 55 && diplomacy[a][b] >= 55 && diplomacy[b][a] >= 55)
        {
            Record("alliance", factions[a].id, factions[b].id, "neutral", "allied",
                   "상호 이익에 따른 동맹");
        }
        if (diplomacy[a][b] < 80 || diplomacy[b][a] < 80 || compatibility < .6f)
        {
            continue;
        }
        std::array<int, 2> members{};
        for (const auto& n : npcs)
        {
            if (n.Alive() && n.faction == a)
            {
                ++members[0];
            }
            if (n.Alive() && n.faction == b)
            {
                ++members[1];
            }
        }
        const int small = members[0] < members[1] ? a : b, large = small == a ? b : a;
        const int count = small == a ? members[0] : members[1];
        if (count > 2 || factions[small].supplies >= 2)
        {
            continue;
        }
        for (std::size_t i = 0; i < npcs.size(); ++i)
        {
            if (npcs[i].Alive() && npcs[i].faction == small)
            {
                SetFaction(static_cast<int>(i), large, "보호와 자원을 위한 세력 통합");
            }
        }
        factions[large].supplies += factions[small].supplies;
        factions[small].supplies = 0;
        factions[small].active = false;
        for (auto& r : regions)
        {
            if (r.owner == small)
            {
                r.owner = large;
            }
        }
        if (playerFaction == small)
        {
            playerFaction = large;
        }
        Record("faction_merge", factions[small].id, factions[large].id, "separate", "merged",
               "구성원과 자원 통합");
    }
    // Local presence, leadership and relations determine control; no scripted conquest order.
    for (std::size_t r = 0; r < regions.size(); ++r)
    {
        std::vector<int> presence(factions.size());
        auto& region = regions[r];
        region.population = 0;
        for (const auto& npc : npcs)
        {
            if (npc.Alive() && npc.region == static_cast<int>(r))
            {
                ++region.population;
                if (std::hypot(npc.x - region.x, npc.y - region.y) <= 8)
                {
                    ++presence[npc.faction];
                }
            }
        }
        const int strongest =
            static_cast<int>(std::max_element(presence.begin(), presence.end()) - presence.begin());
        const bool contested = std::any_of(campaigns.begin(), campaigns.end(),
                                           [&](const Campaign& c)
                                           {
                                               return c.active && c.region == static_cast<int>(r) &&
                                                      c.attacker == strongest;
                                           });
        if (presence[strongest] >= 3 &&
            (region.owner < 0 ||
             (contested && strongest != region.owner && presence[region.owner] == 0 &&
              diplomacy[strongest][region.owner] < rules.hostility)))
        {
            const int old = region.owner;
            region.owner = strongest;
            Record("territory", factions[strongest].id, region.id, std::to_string(old),
                   factions[strongest].id, "지역 통제권 변경");
        }
        const auto* state = world.FindRegion(region.id);
        if (state)
        {
            region.unrest =
                std::clamp(region.unrest + (state->food < .3 ? .025f : -.015f), 0.f, 1.f);
            region.hive = static_cast<float>(state->infection) * (1 - region.security * .3f);
            const float beforeBlood = region.blood;
            region.blood = std::clamp(
                region.blood +
                    (region.owner >= 0 && factions[region.owner].ideology > 0 ? .015f : -.005f),
                0.f, 1.f);
            if (beforeBlood != region.blood)
            {
                Record("blood_supply",
                       region.owner >= 0 ? factions[region.owner].id : "environment", region.id,
                       std::to_string(beforeBlood), std::to_string(region.blood),
                       "교단 공급과 지역 소비");
            }
            region.fear = std::clamp(region.fear - .01f, 0.f, 1.f);
        }
    }
    CampaignTick(world);
}
