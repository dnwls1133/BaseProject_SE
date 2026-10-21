#include "stdafx.h"
#include "LevelOne.h"
#include "WorldMonitor.h"
#include "Profiler.h"
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace
{
std::string JsonString(const std::string& text)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out << '\\' << char(c);
        }
        else if (c < 32)
        {
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
        }
        else
        {
            out << char(c);
        }
    }
    out << '"';
    return out.str();
}
} // namespace

void LevelOne::PublishMonitoring(float dt)
{
    if (!started)
    {
        return;
    }
    monitorTime += dt;
    if (monitorTime < 1)
    {
        return;
    }
    monitorTime = 0;
    PROFILE_SCOPE(WorldMonitor);
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    const auto& npcs = society.Npcs();
    const auto& factions = society.Factions();
    const auto& lives = society.LifeStates();
    std::size_t npcAlive = 0, monsterAlive = 0, monsterActive = 0;
    for (const auto& npc : npcs)
    {
        npcAlive += npc.Alive() ? 1 : 0;
    }
    for (const auto& enemy : enemies)
    {
        monsterAlive += !enemy.dead ? 1 : 0;
        monsterActive += !enemy.dead && enemy.IsActive() ? 1 : 0;
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3);
    out << "{\"schema_version\":1,\"session_id\":" << GetCurrentProcessId()
        << ",\"behavior_model\":\"persistent_goals_v1\",\"content_signature\":"
        << society.Signature() << ",\"sequence\":" << ++monitorSequence
        << ",\"sampled_at_unix_ms\":" << timestamp << ",\"generation\":" << expedition
        << ",\"world_minute\":" << simulation.Minute() << ",\"world\":" << JsonString(worldFile)
        << ",\"paused\":" << (paused || statPanel || stats.health <= 0 ? "true" : "false")
        << ",\"map\":{\"width\":" << map.Width << ",\"height\":" << map.Height << "}"
        << ",\"counts\":{\"npc_total\":" << npcs.size() << ",\"npc_alive\":" << npcAlive
        << ",\"npc_dead\":" << npcs.size() - npcAlive << ",\"monster_total\":" << enemies.size()
        << ",\"monster_alive\":" << monsterAlive
        << ",\"monster_dead\":" << enemies.size() - monsterAlive
        << ",\"monster_active\":" << monsterActive << "}"
        << ",\"player\":{\"x\":" << player.x << ",\"y\":" << player.y
        << ",\"health\":" << stats.health << ",\"level\":" << stats.level << "},\"npcs\":[";
    bool comma = false;
    for (std::size_t i = 0; i < npcs.size(); ++i)
    {
        const auto& n = npcs[i];
        if (comma)
        {
            out << ',';
        }
        comma = true;
        out << "{\"id\":" << JsonString(n.id) << ",\"name\":" << JsonString(n.name)
            << ",\"role\":" << JsonString(n.role) << ",\"goal\":" << JsonString(n.goal)
            << ",\"faction\":" << JsonString(factions[n.faction].id)
            << ",\"region\":" << JsonString(society.Regions()[n.region].id)
            << ",\"alive\":" << (n.Alive() ? "true" : "false") << ",\"x\":" << n.x
            << ",\"y\":" << n.y << ",\"health\":" << n.health << ",\"food_units\":" << n.food
            << ",\"infection\":" << n.infection << ",\"action\":" << int(n.action)
            << ",\"activity\":" << JsonString(society.LifeLabel(int(i)))
            << ",\"target_npc\":" << (n.target >= 0 ? JsonString(npcs[n.target].id) : "null")
            << ",\"target_monster\":" << n.wildTarget << ",\"windup_seconds\":" << n.windup
            << ",\"cooldown_seconds\":" << n.cooldown << ",\"attack_attempts\":" << n.attackAttempts
            << ",\"attack_hits\":" << n.attackHits << ",\"attack_misses\":" << n.attackMisses
            << ",\"damage_dealt\":" << n.damageDealt
            << ",\"last_attack\":" << JsonString(n.lastAttack)
            << ",\"speech\":" << JsonString(n.speech) << ",\"known_regions\":" << n.memories.size();
        if (i < lives.size())
        {
            const auto& l = lives[i];
            const auto& plan = l.plan;
            out << ",\"plan\":{\"id\":" << plan.id
                << ",\"goal\":" << JsonString(WorldSociety::GoalName(plan.goal))
                << ",\"stage\":" << JsonString(WorldSociety::PlanStageName(plan.stage))
                << ",\"blocked_reason\":" << JsonString(plan.blocked)
                << ",\"outcome\":" << JsonString(plan.outcome)
                << ",\"waiting_seconds\":" << plan.waitingSeconds
                << ",\"steps_completed\":" << plan.steps << ",\"started_minute\":" << plan.started
                << ",\"retry_minute\":" << plan.retryAt << ",\"avoided_place\":"
                << (plan.avoidedPlace < 0 ? "null"
                                          : JsonString(society.LifePlaces()[plan.avoidedPlace].id))
                << '}';
            out << ",\"life\":{\"task\":" << int(l.task) << ",\"fatigue\":" << l.fatigue
                << ",\"satiation\":" << l.satiation << ",\"social_need\":" << l.social
                << ",\"cargo_units\":" << l.cargo << ",\"medicine_units\":" << l.medicine
                << ",\"partner\":" << (l.partner >= 0 ? JsonString(npcs[l.partner].id) : "null")
                << ",\"place\":"
                << (l.place >= 0 ? JsonString(society.LifePlaces()[l.place].id) : "null")
                << ",\"delivery\":"
                << (l.delivery >= 0 ? JsonString(society.LifePlaces()[l.delivery].id) : "null")
                << ",\"agreement_id\":" << l.agreement << ",\"progress_seconds\":" << l.progress
                << ",\"stalled_seconds\":" << l.stalled << ",\"reason\":" << JsonString(l.reason)
                << ",\"candidate_scores\":" << JsonString(l.decisionTrace)
                << ",\"memory\":" << JsonString(l.lastMemory) << ",\"choices\":[";
            bool choiceComma = false;
            for (const auto& choice : l.choices)
            {
                if (choiceComma)
                {
                    out << ',';
                }
                choiceComma = true;
                out << "{\"task\":" << choice.task
                    << ",\"place\":" << JsonString(society.LifePlaces()[choice.place].id)
                    << ",\"score\":" << choice.score << ",\"reason\":" << JsonString(choice.reason)
                    << ",\"destination\":"
                    << (choice.destination >= 0
                            ? JsonString(society.LifePlaces()[choice.destination].id)
                            : "null")
                    << '}';
            }
            out << "]}";
        }
        if (society.HasEconomy())
        {
            const auto& worker = society.EconomyWorkers()[i];
            out << ",\"coins\":" << worker.coins << ",\"economic_order\":" << worker.order;
        }
        out << '}';
    }
    out << "],\"regions\":[";
    comma = false;
    for (const auto& region : society.Regions())
    {
        if (comma)
        {
            out << ',';
        }
        comma = true;
        const auto* state = simulation.FindRegion(region.id);
        int localNpc = 0, localMonsters = 0;
        for (const auto& npc : npcs)
        {
            localNpc += npc.Alive() && society.Regions()[npc.region].id == region.id ? 1 : 0;
        }
        for (const auto& enemy : enemies)
        {
            localMonsters +=
                !enemy.dead && map.DistrictAt(int(enemy.x), int(enemy.y)).id == region.id ? 1 : 0;
        }
        out << "{\"id\":" << JsonString(region.id) << ",\"name\":" << JsonString(region.name)
            << ",\"x\":" << region.x << ",\"y\":" << region.y
            << ",\"owner\":" << (region.owner >= 0 ? JsonString(factions[region.owner].id) : "null")
            << ",\"npc_alive\":" << localNpc << ",\"monster_alive\":" << localMonsters
            << ",\"food_ratio\":" << (state ? std::to_string(state->food) : "null")
            << ",\"infection_ratio\":" << (state ? std::to_string(state->infection) : "null")
            << ",\"security\":" << region.security << ",\"fear\":" << region.fear
            << ",\"unrest\":" << region.unrest << ",\"blood\":" << region.blood
            << ",\"hive\":" << region.hive << ",\"hospital\":" << region.hospital
            << ",\"storehouse\":" << region.storehouse << ",\"wall\":" << region.wall << '}';
    }
    out << "],\"factions\":[";
    comma = false;
    for (std::size_t i = 0; i < factions.size(); ++i)
    {
        const auto& faction = factions[i];
        if (comma)
        {
            out << ',';
        }
        comma = true;
        int members = 0;
        for (const auto& npc : npcs)
        {
            members += npc.Alive() && npc.faction == int(i) ? 1 : 0;
        }
        out << "{\"id\":" << JsonString(faction.id) << ",\"name\":" << JsonString(faction.name)
            << ",\"active\":" << (faction.active ? "true" : "false")
            << ",\"members_alive\":" << members << ",\"supplies_units\":" << faction.supplies
            << ",\"support\":" << faction.support << ",\"ideology\":" << faction.ideology
            << ",\"relations\":[";
        for (std::size_t other = 0; other < factions.size(); ++other)
        {
            if (other > 0)
            {
                out << ',';
            }
            out << "{\"faction\":" << JsonString(factions[other].id)
                << ",\"attitude\":" << society.FactionAttitude(int(i), int(other)) << '}';
        }
        out << "]}";
    }
    out << "],\"agreements\":[";
    comma = false;
    for (const auto& agreement : society.Agreements())
    {
        if (comma)
        {
            out << ',';
        }
        comma = true;
        out << "{\"id\":" << agreement.id
            << ",\"requester\":" << JsonString(npcs[agreement.requester].id)
            << ",\"supplier\":" << JsonString(npcs[agreement.supplier].id)
            << ",\"kind\":" << agreement.kind << ",\"stage\":" << agreement.stage
            << ",\"cargo_units\":" << agreement.cargo << ",\"payment_units\":" << agreement.payment
            << ",\"deadline_minute\":" << agreement.deadline << '}';
    }
    out << "],\"monsters\":[";
    comma = false;
    for (std::size_t i = 0; i < enemies.size(); ++i)
    {
        const auto& e = enemies[i];
        if (comma)
        {
            out << ',';
        }
        comma = true;
        out << "{\"id\":" << JsonString("monster_" + std::to_string(i))
            << ",\"species\":" << JsonString(e.type.id) << ",\"x\":" << e.x << ",\"y\":" << e.y
            << ",\"health\":" << e.health << ",\"max_health\":" << e.maximumHealth
            << ",\"alive\":" << (!e.dead ? "true" : "false") << ",\"target_kind\":" << e.targetKind
            << ",\"target_index\":" << e.targetIndex << '}';
    }
    out << "],\"campaigns\":[";
    comma = false;
    for (const auto& campaign : society.Campaigns())
    {
        if (!campaign.active)
        {
            continue;
        }
        if (comma)
        {
            out << ',';
        }
        comma = true;
        out << "{\"attacker\":" << JsonString(factions[campaign.attacker].id)
            << ",\"defender\":" << JsonString(factions[campaign.defender].id)
            << ",\"region\":" << JsonString(society.Regions()[campaign.region].id)
            << ",\"stage\":" << campaign.stage << '}';
    }
    out << "],\"economy\":{\"regions\":[";
    for (std::size_t i = 0; i < society.EconomyRegions().size(); ++i)
    {
        if (i)
        {
            out << ',';
        }
        const auto& market = society.EconomyRegions()[i];
        out << "{\"id\":" << JsonString(society.Regions()[i].id) << ",\"coins\":" << market.coins
            << ",\"goods\":[";
        for (int good = 0; good < 3; ++good)
        {
            if (good)
            {
                out << ',';
            }
            out << "{\"name\":" << JsonString(WorldSociety::EconomyGoodName(good))
                << ",\"stock\":" << society.EconomyStock(int(i), good, simulation)
                << ",\"target\":" << market.target[good] << ",\"price\":" << market.price[good]
                << ",\"produced\":" << market.produced[good]
                << ",\"imported\":" << market.imported[good]
                << ",\"exported\":" << market.exported[good] << '}';
        }
        out << "]}";
    }
    out << "],\"orders\":[";
    comma = false;
    for (const auto& order : society.EconomyOrders())
    {
        if (comma)
        {
            out << ',';
        }
        comma = true;
        out << "{\"id\":" << order.id
            << ",\"source\":" << JsonString(society.Regions()[order.source].id)
            << ",\"destination\":" << JsonString(society.Regions()[order.destination].id)
            << ",\"good\":" << JsonString(WorldSociety::EconomyGoodName(order.good))
            << ",\"units\":" << order.units << ",\"stage\":" << order.stage
            << ",\"escrow\":" << order.escrow << ",\"freight\":" << order.freight
            << ",\"carrier\":" << (order.carrier < 0 ? "null" : JsonString(npcs[order.carrier].id))
            << ",\"supplier\":"
            << (order.supplier < 0 ? "null" : JsonString(npcs[order.supplier].id))
            << ",\"receiver\":"
            << (order.receiver < 0 ? "null" : JsonString(npcs[order.receiver].id))
            << ",\"outcome\":" << JsonString(order.outcome) << '}';
    }
    out << "]},\"events\":[";
    comma = false;
    for (const auto& event : society.History())
    {
        if (comma)
        {
            out << ',';
        }
        comma = true;
        out << "{\"id\":" << event.id << ",\"cause\":" << event.cause
            << ",\"minute\":" << event.minute << ",\"text\":" << JsonString(event.text) << '}';
    }
    out << "]}";
    wchar_t executable[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    WorldMonitor::Publish(std::filesystem::path(executable).parent_path() / "Logs" / "WorldMonitor",
                          out.str());
}
