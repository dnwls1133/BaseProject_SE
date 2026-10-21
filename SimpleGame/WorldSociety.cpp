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

std::uint32_t WorldSociety::Random()
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}

std::uint64_t WorldSociety::Record(const std::string& kind, const std::string& actor,
                                   const std::string& target, const std::string& before,
                                   const std::string& after, const std::string& reason,
                                   std::uint64_t cause)
{
    const auto id = serial++;
    Profile::Event("society_change",
                   "change_id=" + std::to_string(id) + " cause_id=" + std::to_string(cause) +
                       " world_minute=" + std::to_string(minute) + " kind=" + kind +
                       " actor=" + actor + " target=" + target + " before=" + before +
                       " after=" + after + " reason=" + reason);
    auto displayName = [&](const std::string& value)
    {
        if (value == "player")
        {
            return std::string("플레이어");
        }
        if (value == "environment")
        {
            return std::string("환경");
        }
        const int n = NpcIndex(value), f = FactionIndex(value), r = RegionIndex(value);
        if (n >= 0)
        {
            return npcs[n].name;
        }
        if (f >= 0)
        {
            return factions[f].name;
        }
        if (r >= 0)
        {
            return regions[r].name;
        }
        return std::string("세계 기록");
    };
    history.push_back(
        {id, cause, minute, displayName(actor) + " → " + displayName(target) + " · " + reason});
    if (history.size() > 96)
    {
        history.pop_front();
    }
    return id;
}

void WorldSociety::Update(float dt, WorldSimulation& world, const RandomLevelMap& map, Point player)
{
    PROFILE_SCOPE(SocietyUpdate);
    if (npcs.empty())
    {
        return;
    }
    conversationTime = std::max(0.f, conversationTime - dt);
    if (conversationTime <= 0)
    {
        EndConversation();
    }
    pathBudget = 2;
    pathNodeBudget = 2048;
    neighborhood.clear();
    for (std::size_t i = 0; i < npcs.size(); ++i)
    {
        auto& npc = npcs[i];
        npc.region =
            RegionIndex(map.DistrictAt(static_cast<int>(npc.x), static_cast<int>(npc.y)).id);
        if (npc.Alive())
        {
            neighborhood[static_cast<int>(npc.x / 24) + static_cast<int>(npc.y / 24) * 1024]
                .push_back(static_cast<int>(i));
        }
    }
    TickContamination(dt, world, map);
    const auto previous = minute;
    minute = world.Minute();
    if (minute > previous)
    {
        for (std::size_t i = 0; i < npcs.size(); ++i)
        {
            auto& npc = npcs[i];
            if (!npc.Alive())
            {
                continue;
            }
            if (!HasLife())
            {
                npc.food =
                    std::max(0.f, npc.food - rules.foodPerDay *
                                                 static_cast<float>(minute - previous) / 1440);
            }
            if (npc.nextDecision <= minute)
            {
                Decide(static_cast<int>(i), world, map);
                npc.nextDecision = minute + rules.decisionPeriod;
            }
        }
        Deliver(world);
        StoryTick(world, map, player);
        EconomyTick(world);
        LifeTick(minute - previous, world, map);
        if (minute >= lastFactionTick + rules.factionPeriod)
        {
            FactionTick(world);
            lastFactionTick = minute;
        }
        QuestTick(world, player);
    }
    Profile::Set(Profile::Metric::SocialNpcCount, static_cast<double>(npcs.size()));
    Profile::Set(Profile::Metric::SocialMessageQueue, static_cast<double>(messages.size()));
    Profile::Set(Profile::Metric::SocialQuestCount, static_cast<double>(quests.size()));
    std::size_t activePlans = 0;
    for (std::size_t i = 0; i < lives.size(); ++i)
    {
        if (npcs[i].Alive() && lives[i].task != LifeTask::None)
        {
            ++activePlans;
        }
    }
    Profile::Set(Profile::Metric::EconomyOrders,
                 double(std::count_if(economyOrders.begin(), economyOrders.end(),
                                      [](const EconomyOrder& order)
                                      {
                                          return order.stage < 4;
                                      })));
    // Emit gauges even on frames without a world-minute decision tick.
    Profile::Set(Profile::Metric::NpcPlans, double(activePlans));
    Profile::Set(Profile::Metric::NpcAgreements, double(agreements.size()));
    (void)dt;
}
