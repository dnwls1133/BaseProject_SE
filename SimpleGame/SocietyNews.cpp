#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "RandomLevelMap.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

bool WorldSociety::NewsContact(int from, int to, const RandomLevelMap& map) const
{
    if (from < 0 || to < 0 || from == to || !npcs[from].Alive() || !npcs[to].Alive())
    {
        return false;
    }
    const auto& a = npcs[from];
    const auto& b = npcs[to];
    return a.faction == b.faction && std::hypot(a.x - b.x, a.y - b.y) <= 2.5f && a.windup <= 0 &&
           b.windup <= 0 && map.Visible(a.x, a.y, b.x, b.y);
}

std::string WorldSociety::CourierDestination(int index) const
{
    const auto route = courierRoutes.find(index);
    if (route == courierRoutes.end())
    {
        return "";
    }
    return regions[route->second[npcs[index].courierStop % route->second.size()]].id;
}

void WorldSociety::ExchangeNews(WorldSimulation& world, const RandomLevelMap& map)
{
    PROFILE_SCOPE(NewsContact);
    // Decision times are staggered; nearby buckets bound comparisons as population grows.
    int budget = rules.messageBudget;
    for (int offset = 0; offset < int(npcs.size()) && budget > 0; ++offset)
    {
        const int index = (int(minute % npcs.size()) + offset) % int(npcs.size());
        auto& npc = npcs[index];
        if (!npc.Alive() || npc.nextMessage > minute)
        {
            continue;
        }
        npc.nextMessage = minute + (IsCourier(index) ? 1 : 8 + index % 5);
        for (int other : Nearby(npc))
        {
            if (!NewsContact(index, other, map))
            {
                continue;
            }
            for (const auto& memory : npc.memories)
            {
                if (memory.learned + rules.messageLifetime < minute)
                {
                    continue;
                }
                const auto& known = npcs[other].memories;
                const bool newer = std::none_of(known.begin(), known.end(),
                                                [&](const Memory& m)
                                                {
                                                    return m.region == memory.region &&
                                                           m.learned >= memory.learned;
                                                });
                if (newer)
                {
                    Send(index, other, 0, memory.region, world);
                    if (--budget <= 0)
                    {
                        return;
                    }
                }
            }
        }
    }
}

bool WorldSociety::TickCourier(int index, float dt, WorldSimulation& world,
                               const RandomLevelMap& map)
{
    const auto found = courierRoutes.find(index);
    if (found == courierRoutes.end())
    {
        return false;
    }
    PROFILE_SCOPE(CourierMotion);
    auto& npc = npcs[index];
    const auto& route = found->second;
    auto& stop = npc.courierStop;
    stop %= int(route.size());
    const auto& region = regions[route[stop]];
    npc.action = Action::Travel;
    npc.destination = route[stop];
    if (npc.courierPause <= 0 && std::hypot(npc.x - region.x, npc.y - region.y) > 1.5f)
    {
        MoveTo(npc, {region.x, region.y}, dt, map);
        return true;
    }
    npc.courierPause = std::max(.001f, npc.courierPause);
    int recipient = -1;
    float nearest = 12;
    for (int other : Nearby(npc))
    {
        if (IsCourier(other) || npcs[other].faction != npc.faction || npcs[other].windup > 0 ||
            npcs[other].role == "guard" || npcs[other].region != route[stop])
        {
            continue;
        }
        const float distance = std::hypot(npc.x - npcs[other].x, npc.y - npcs[other].y);
        if (distance < nearest && map.Visible(npc.x, npc.y, npcs[other].x, npcs[other].y))
        {
            recipient = other;
            nearest = distance;
        }
    }
    if (recipient >= 0 && nearest > 1.5f)
    {
        MoveTo(npc, {npcs[recipient].x, npcs[recipient].y}, dt, map);
        return true;
    }
    npc.courierPause += dt;
    // Stay long enough for the minute-based contact pass, then carry observed memories onward.
    Observe(index, world);
    if (npc.courierPause >= 3)
    {
        Record("courier_arrived", npc.id, region.id, "travelling", "in_person",
               "전령이 실제로 도착 · 대면한 상대에게만 소식 공유");
        stop = (stop + 1) % int(route.size());
        npc.courierPause = 0;
        npc.pathCell = -1;
    }
    return true;
}
