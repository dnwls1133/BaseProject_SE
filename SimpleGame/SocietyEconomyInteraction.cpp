#include "stdafx.h"
#include "WorldSociety.h"
#include "RandomLevelMap.h"
#include "Profiler.h"
#include <cmath>

bool WorldSociety::EconomyParticipantBusy(int index, std::uint64_t except) const
{
    for (const auto& order : economyOrders)
    {
        if (order.id == except || order.stage < 1 || order.stage > 2)
        {
            continue;
        }
        if (order.carrier == index || order.receiver == index ||
            (order.stage == 1 && order.supplier == index))
        {
            return true;
        }
    }
    return false;
}

int WorldSociety::EconomyRepresentative(int region, int except, std::uint64_t order) const
{
    int chosen = -1;
    float best = -1e9f;
    for (std::size_t i = 0; i < npcs.size(); ++i)
    {
        const auto& npc = npcs[i];
        const auto& life = lives[i];
        if (IsHive(int(i)) || IsCourier(int(i)) || int(i) == except || !npc.Alive() ||
            npc.region != region || npc.faction != regions[region].owner || npc.role == "guard" ||
            npc.action == Action::Combat || npc.action == Action::Flee ||
            npc.action == Action::Escort || npc.wildTarget >= 0 || npc.playerThreat ||
            npc.health < 35 || life.satiation < 20 || life.fatigue > 90 || life.agreement ||
            life.cargo > 0 || economyWorkers[i].order || EconomyParticipantBusy(int(i), order))
        {
            continue;
        }
        const float score = (npc.role == "merchant" ? 5.f
                             : npc.role == "leader" ? 4.f
                                                    : 0.f) -
                            std::hypot(npc.x - regions[region].x, npc.y - regions[region].y) * .05f;
        if (score > best)
        {
            best = score;
            chosen = int(i);
        }
    }
    return chosen;
}

bool WorldSociety::EconomicContact(int index, const EconomyOrder& order,
                                   const RandomLevelMap& map) const
{
    const int partner = order.stage == 1 ? order.supplier : order.receiver;
    if (partner < 0 || !npcs[partner].Alive())
    {
        return false;
    }
    const auto& npc = npcs[index];
    const auto& other = npcs[partner];
    return other.action != Action::Combat && other.action != Action::Flee &&
           other.action != Action::Escort && other.wildTarget < 0 && !other.playerThreat &&
           !lives[partner].agreement && std::hypot(npc.x - other.x, npc.y - other.y) <= 1.8f &&
           map.Visible(npc.x, npc.y, other.x, other.y);
}

bool WorldSociety::EconomicService(int index, float dt, const RandomLevelMap& map)
{
    if (!HasEconomy() || lives[index].agreement || economyWorkers[index].order ||
        lives[index].satiation < 15 || lives[index].fatigue > 95)
    {
        return false;
    }
    PROFILE_SCOPE(EconomyTransfer);
    for (const auto& order : economyOrders)
    {
        if (!((order.stage == 1 && order.supplier == index) ||
              (order.stage == 2 && order.receiver == index)) ||
            order.carrier < 0)
        {
            continue;
        }
        const auto& carrier = npcs[order.carrier];
        const auto& delivery = lives[order.carrier];
        if (!carrier.Alive() || delivery.place < 0)
        {
            continue;
        }
        const auto& site = places[delivery.place];
        if (std::hypot(carrier.x - site.position.x, carrier.y - site.position.y) >
            planningRules.serviceRange)
        {
            continue;
        }
        auto& npc = npcs[index];
        Point goal = delivery.slot >= 0 ? site.slots[delivery.slot] : site.position;
        if (std::hypot(npc.x - goal.x, npc.y - goal.y) > 1.25f ||
            !map.Visible(npc.x, npc.y, goal.x, goal.y))
        {
            SetPlanStage(index, PlanStage::Moving,
                         carrier.name + "와 약속한 물자 인계 장소로 이동");
            MoveTo(npc, goal, dt, map);
        }
        else
        {
            npc.yaw = std::atan2(npc.x - carrier.x, npc.y - carrier.y);
            SetPlanStage(index, PlanStage::Waiting, carrier.name + "와 화물 확인·인계");
        }
        return true;
    }
    return false;
}
