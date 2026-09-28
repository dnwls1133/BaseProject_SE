#include "stdafx.h"
#include "WorldSimulation.h"
#include "ContentData.h"
#include "LevelData.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

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
        region.food = std::clamp(region.food + region.foodPerHour / 60, 0.0, 1.0);
        region.infection = std::clamp(region.infection + region.infectionPerHour / 60, 0.0, 1.0);
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
        region.food = std::clamp(region.food + foodEffect, 0.0, 1.0);
        region.infection = std::clamp(region.infection + infectionEffect, 0.0, 1.0);
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
