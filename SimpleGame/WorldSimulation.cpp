#include "stdafx.h"
#include "WorldSimulation.h"
#include "ContentData.h"
#include "LevelData.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <iomanip>
#include <ostream>

const WorldSimulation::RegionState* WorldSimulation::FindRegion(const std::string& id) const
{
    for (const auto& region : regions)
    {
        if (region.id == id)
        {
            return &region;
        }
    }
    return nullptr;
}

void WorldSimulation::Load(const std::string& root, const std::string& relative,
                           const std::vector<RegionDefinition>& definitions,
                           const std::string& world)
{
    WorldSimulation loaded;
    if (relative.empty())
    {
        *this = std::move(loaded);
        return;
    }

    loaded.sourceFile = ContentData::Path(root, relative);
    loaded.worldFile = world;
    std::set<std::string> records;
    ContentData::Load(
        loaded.sourceFile,
        [&](const std::string& command, ContentData::Row& row)
        {
            if (command == "clock")
            {
                ContentData::Require(records.insert(command).second, "duplicate clock");
                loaded.minute = row.Integer(0, 1439);
                loaded.minutesPerSecond = row.Number(.01f, 60);
            }
            else if (command == "region")
            {
                RegionState state;
                state.id = row.Word();
                ContentData::Require(records.insert("region:" + state.id).second,
                                     "duplicate simulation region: " + state.id);
                auto definition = std::find_if(definitions.begin(), definitions.end(),
                                               [&](const RegionDefinition& item)
                                               {
                                                   return item.id == state.id;
                                               });
                ContentData::Require(definition != definitions.end(),
                                     "unknown simulation region: " + state.id);
                state.name = definition->name;
                state.food = row.Number(0, 1);
                state.infection = row.Number(0, 1);
                state.foodPerHour = row.Number(-1, 1);
                state.infectionPerHour = row.Number(-1, 1);
                loaded.regions.push_back(state);
            }
            else if (command == "event")
            {
                ContentData::Require(records.insert(command).second, "only one event supported");
                loaded.eventId = row.Word();
                loaded.foodThreshold = row.Number(0, 1);
                loaded.infectionThreshold = row.Number(0, 1);
                loaded.cooldownMinutes = row.Integer(1, 10080);
                loaded.foodEffect = row.Number(-1, 1);
                loaded.infectionEffect = row.Number(-1, 1);
                loaded.eventMessage = row.Word();
                ContentData::Require(loaded.eventMessage.size() <= 180, "event message too long");
            }
            else
            {
                ContentData::Require(false, "unknown simulation record: " + command);
            }
        });
    ContentData::Require(records.count("clock") && records.count("event"),
                         loaded.sourceFile + ": clock and event required");
    ContentData::Require(loaded.regions.size() == definitions.size(),
                         loaded.sourceFile + ": every world region needs simulation state");

    // Successful reloads retain runtime values by stable ID; new IDs use file defaults.
    if (sourceFile == loaded.sourceFile && worldFile == loaded.worldFile)
    {
        loaded.minute = minute;
        loaded.remainder = remainder;
        if (eventId == loaded.eventId)
        {
            loaded.lastNews = lastNews;
            loaded.newsTime = newsTime;
        }
        for (auto& region : loaded.regions)
        {
            if (const auto* previous = FindRegion(region.id))
            {
                region.food = previous->food;
                region.infection = previous->infection;
                if (eventId == loaded.eventId)
                {
                    region.nextEventMinute = previous->nextEventMinute;
                }
            }
        }
    }
    *this = std::move(loaded);
}

void WorldSimulation::Update(float dt)
{
    if (regions.empty() || !std::isfinite(dt) || dt <= 0)
    {
        return;
    }
    PROFILE_SCOPE(WorldSimulation);
    newsTime = std::max(0.f, newsTime - dt);
    // Keep fractional minutes. The caller supplies the existing bounded gameplay delta.
    remainder += static_cast<double>(dt) * minutesPerSecond;
    // Bound catch-up cost; retain any backlog rather than discarding simulated time.
    int budget = 120;
    while (remainder >= 1 && budget-- > 0)
    {
        remainder -= 1;
        AdvanceMinute();
    }
}

void WorldSimulation::AdvanceMinute()
{
    ++minute;
    Profile::Add(Profile::Metric::WorldMinuteTicks);
    for (auto& region : regions)
    {
        ChangeRegion(region.id, region.foodPerHour / 60, region.infectionPerHour / 60,
                     "environment", "시간에 따른 자원 변화");
        if (minute % 60 == 0)
        {
            Profile::Event("world_region_state",
                           "region_id=" + region.id + " world_minute=" + std::to_string(minute) +
                               " food_ratio=" + std::to_string(region.food) +
                               " infection_ratio=" + std::to_string(region.infection));
        }
        if (region.food > foodThreshold || region.infection < infectionThreshold ||
            minute < region.nextEventMinute)
        {
            continue;
        }

        const double oldFood = region.food, oldInfection = region.infection;
        ChangeRegion(region.id, foodEffect, infectionEffect, eventId, "조건부 지역 사건");
        region.nextEventMinute = minute + cooldownMinutes;
        lastNews = region.name + " · " + eventMessage;
        newsTime = 18;
        Profile::Add(Profile::Metric::WorldEvents);
        Profile::Event("world_event",
                       "event_id=" + eventId + " region_id=" + region.id +
                           " world_minute=" + std::to_string(minute) +
                           " food_before_ratio=" + std::to_string(oldFood) +
                           " food_after_ratio=" + std::to_string(region.food) +
                           " infection_before_ratio=" + std::to_string(oldInfection) +
                           " infection_after_ratio=" + std::to_string(region.infection));
    }
}

void WorldSimulation::RecordMetrics(const std::string& regionId)
{
    Profile::Set(Profile::Metric::WorldMinute, static_cast<double>(minute));
    Profile::Set(Profile::Metric::SimulationRegions, static_cast<double>(regions.size()));
    if (const auto* region = FindRegion(regionId))
    {
        if (loggedRegion != regionId)
        {
            loggedRegion = regionId;
            Profile::Event(
                "world_player_region",
                "region_id=" + regionId + " world_minute=" + std::to_string(minute) +
                    " world_minutes_per_gameplay_second=" + std::to_string(minutesPerSecond));
        }
        Profile::Set(Profile::Metric::RegionFood, region->food);
        Profile::Set(Profile::Metric::RegionInfection, region->infection);
    }
}

const std::string& WorldSimulation::News() const
{
    static const std::string empty;
    return newsTime > 0 ? lastNews : empty;
}

void WorldSimulation::ChangeRegion(const std::string& id, double food, double infection,
                                   const std::string& actor, const std::string& reason,
                                   std::uint64_t cause)
{
    auto it = std::find_if(regions.begin(), regions.end(),
                           [&](const RegionState& region)
                           {
                               return region.id == id;
                           });
    ContentData::Require(it != regions.end(), "unknown changed region: " + id);
    ContentData::Require(std::isfinite(food) && std::isfinite(infection),
                         "invalid resource change");
    const double beforeFood = it->food, beforeInfection = it->infection;
    it->food = std::clamp(it->food + food, 0.0, 1.0);
    it->infection = std::clamp(it->infection + infection, 0.0, 1.0);
    Profile::Event("world_resource_change",
                   "region_id=" + id + " actor_id=" + actor + " cause_id=" + std::to_string(cause) +
                       " world_minute=" + std::to_string(minute) +
                       " food_before_ratio=" + std::to_string(beforeFood) +
                       " food_after_ratio=" + std::to_string(it->food) +
                       " infection_before_ratio=" + std::to_string(beforeInfection) +
                       " infection_after_ratio=" + std::to_string(it->infection) +
                       " reason=" + reason);
}

void WorldSimulation::Write(std::ostream& stream) const
{
    stream << "sim_clock " << minute << ' ' << remainder << ' ' << newsTime << ' '
           << std::quoted(lastNews.empty() ? "-" : lastNews) << '\n';
    for (const auto& r : regions)
    {
        stream << "sim_region " << std::quoted(r.id) << ' ' << r.food << ' ' << r.infection << ' '
               << r.nextEventMinute << '\n';
    }
}

void WorldSimulation::BeginRestore()
{
    restoredRecords.clear();
}

bool WorldSimulation::ReadRow(const std::string& command, ContentData::Row& row)
{
    if (command == "sim_clock")
    {
        ContentData::Require(restoredRecords.insert(command).second, "duplicate saved clock");
        minute = row.Unsigned();
        remainder = row.Decimal(0, 1000000);
        newsTime = row.Number(0, 18);
        lastNews = row.Word();
        if (lastNews == "-")
        {
            lastNews.clear();
        }
        return true;
    }
    if (command == "sim_region")
    {
        const std::string id = row.Word();
        ContentData::Require(restoredRecords.insert(command + id).second, "duplicate saved region");
        auto it = std::find_if(regions.begin(), regions.end(),
                               [&](const RegionState& r)
                               {
                                   return r.id == id;
                               });
        ContentData::Require(it != regions.end(), "unknown saved region");
        it->food = row.Decimal(0, 1);
        it->infection = row.Decimal(0, 1);
        it->nextEventMinute = row.Unsigned();
        return true;
    }
    return false;
}

void WorldSimulation::FinishRestore() const
{
    ContentData::Require(restoredRecords.size() == regions.size() + 1 &&
                             restoredRecords.count("sim_clock"),
                         "incomplete simulation save");
}
