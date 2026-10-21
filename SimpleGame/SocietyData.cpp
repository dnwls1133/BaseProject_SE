#include "stdafx.h"
#include "WorldSociety.h"
#include "ContentData.h"
#include "LevelData.h"
#include "RandomLevelMap.h"
#include <algorithm>
#include <cmath>
#include <set>

int WorldSociety::NpcIndex(const std::string& id) const
{
    for (std::size_t i = 0; i < npcs.size(); ++i)
    {
        if (npcs[i].id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int WorldSociety::FactionIndex(const std::string& id) const
{
    for (std::size_t i = 0; i < factions.size(); ++i)
    {
        if (factions[i].id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int WorldSociety::RegionIndex(const std::string& id) const
{
    for (std::size_t i = 0; i < regions.size(); ++i)
    {
        if (regions[i].id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void WorldSociety::Load(const std::string& root, const std::string& file, const LevelData& level,
                        const ModelCache& models, std::uint32_t seed, std::uint64_t time)
{
    PROFILE_SCOPE(SocietyLoad);
    if (file.empty())
    {
        *this = WorldSociety();
        return;
    }
    WorldSociety loaded;
    std::string storyFile, lifeFile, economyFile, conflictFile;
    loaded.source = ContentData::Path(root, file);
    const std::string text = ContentData::Read(loaded.source);
    loaded.signature = level.contentHash;
    // Reject incompatible saves instead of silently remapping persistent identities.
    const std::string authored = text + ContentData::Read(level.sourceFile) +
                                 ContentData::Read(ContentData::Path(root, level.simulationFile));
    for (unsigned char byte : authored)
    {
        loaded.signature = (loaded.signature ^ byte) * 16777619u;
    }
    loaded.randomState = seed ? seed : 1;
    loaded.minute = loaded.lastFactionTick = time;
    for (const auto& definition : level.regions)
    {
        Region region;
        region.id = definition.id;
        region.name = definition.name;
        region.x = definition.x + .5f;
        region.y = definition.y + .5f;
        loaded.regions.push_back(region);
    }
    std::set<std::string> seen;
    using ContentData::Require;
    ContentData::Rows(
        loaded.source, text,
        [&](const std::string& command, ContentData::Row& row)
        {
            if (command == "rules")
            {
                Require(seen.insert(command).second, "duplicate society rules");
                auto& r = loaded.rules;
                r.decisionPeriod = row.Integer(2, 120);
                r.factionPeriod = row.Integer(10, 1440);
                r.messageBudget = row.Integer(1, 64);
                r.messageLifetime = row.Integer(10, 1440);
                r.questLifetime = row.Integer(30, 10080);
                r.maxFactions = row.Integer(5, 64);
                r.talkRange = row.Number(2, 24);
                r.foodPerDay = row.Number(0, 20);
                r.combatRange = row.Number(.5f, 2);
                r.attackWindup = row.Number(.3f, 3);
                r.attackCooldown = row.Number(.5f, 8);
                r.hostility = row.Number(-100, -10);
            }
            else if (command == "ecology")
            {
                const std::string faction = row.Word(), species = row.Word();
                Require(loaded.FactionIndex(faction) >= 0, "unknown ecology faction");
                level.Enemy(species);
                Require(
                    loaded.ecology.emplace(std::make_pair(faction, species), row.Number(-100, 100))
                        .second,
                    "duplicate ecology relation");
            }
            else if (command == "war_rules")
            {
                Require(seen.insert(command).second, "duplicate war rules");
                loaded.warThreshold = row.Number(-100, -20);
                loaded.warReserve = row.Number(1, 100);
                loaded.warCost = row.Number(.1f, 10);
                loaded.truceMinutes = row.Unsigned(10080);
                Require(loaded.truceMinutes >= 60, "truce too short");
            }
            else if (command == "frontier")
            {
                Frontier front;
                front.first = loaded.FactionIndex(row.Word());
                front.second = loaded.FactionIndex(row.Word());
                front.region = loaded.RegionIndex(row.Word());
                Require(front.first >= 0 && front.second >= 0 && front.first != front.second &&
                            front.region >= 0,
                        "invalid frontier references");
                Require(
                    seen.insert("frontier:" + std::to_string(std::min(front.first, front.second)) +
                                ":" + std::to_string(std::max(front.first, front.second)))
                        .second,
                    "duplicate faction frontier");
                front.firstRally = {row.Number(1, level.width - 2.f),
                                    row.Number(1, level.height - 2.f)};
                front.secondRally = {row.Number(1, level.width - 2.f),
                                     row.Number(1, level.height - 2.f)};
                Require(loaded.regions[front.region].owner == front.first ||
                            loaded.regions[front.region].owner == front.second,
                        "frontier objective must belong to a claimant");
                loaded.frontiers.push_back(front);
            }
            else if (command == "conflict")
            {
                Require(conflictFile.empty(), "duplicate conflict file");
                conflictFile = row.Word();
            }
            else if (command == "economy")
            {
                Require(economyFile.empty(), "duplicate economy definition");
                economyFile = row.Word();
            }
            else if (command == "life")
            {
                Require(seen.insert(command).second, "duplicate life file");
                lifeFile = row.Word();
            }
            else if (command == "story")
            {
                Require(seen.insert(command).second, "duplicate story file");
                storyFile = row.Word();
            }
            else if (command == "weights")
            {
                Require(seen.insert(command).second, "duplicate action weights");
                for (auto& weight : loaded.rules.weights)
                {
                    weight = row.Number(0, 5);
                }
            }
            else if (command == "faction")
            {
                Faction faction;
                faction.id = row.Word();
                Require(loaded.FactionIndex(faction.id) < 0, "duplicate faction ID");
                faction.name = row.Word();
                faction.motto = row.Word();
                faction.home = loaded.RegionIndex(row.Word());
                Require(faction.home >= 0, "unknown faction home");
                faction.emblem = models.Find(row.Word());
                faction.color.r = row.Number(0, 1);
                faction.color.g = row.Number(0, 1);
                faction.color.b = row.Number(0, 1);
                faction.ideology = row.Number(-1, 1);
                faction.supplies = row.Number(0, 100);
                loaded.factions.push_back(faction);
                Require(loaded.factions.size() <= 32, "too many initial factions");
            }
            else if (command == "territory")
            {
                const int region = loaded.RegionIndex(row.Word());
                Require(region >= 0 && seen.insert("territory:" + std::to_string(region)).second,
                        "unknown or duplicate territory");
                auto& r = loaded.regions[region];
                r.owner = loaded.FactionIndex(row.Word());
                Require(r.owner >= 0, "unknown territory owner");
                r.security = row.Number(0, 1);
                r.blood = row.Number(0, 1);
                r.hive = row.Number(0, 1);
                r.fear = row.Number(0, 1);
                r.unrest = row.Number(0, 1);
            }
            else if (command == "npc")
            {
                Npc npc;
                npc.id = row.Word();
                Require(loaded.NpcIndex(npc.id) < 0, "duplicate NPC ID");
                npc.name = row.Word();
                npc.role = row.Word();
                npc.goal = row.Word();
                Require(npc.goal == "rule" || npc.goal == "protect" || npc.goal == "profit" ||
                            npc.goal == "aid" || npc.goal == "research" || npc.goal == "purify" ||
                            npc.goal == "connect" || npc.goal == "survive",
                        "unknown NPC goal");
                Require(npc.role == "leader" || npc.role == "guard" || npc.role == "merchant" ||
                            npc.role == "healer" || npc.role == "scholar" || npc.role == "refugee",
                        "unknown NPC role");
                npc.faction = loaded.FactionIndex(row.Word());
                npc.home = loaded.RegionIndex(row.Word());
                Require(npc.faction >= 0 && npc.home >= 0, "unknown NPC faction/home");
                npc.region = npc.destination = npc.home;
                npc.model = models.Find(row.Word());
                npc.x = row.Number(2, static_cast<float>(level.width - 2));
                npc.y = row.Number(2, static_cast<float>(level.height - 2));
                for (auto& trait : npc.traits)
                {
                    trait = row.Number(0, 1);
                }
                npc.food = row.Number(0, 30);
                npc.infection = row.Number(0, 1);
                npc.attack = row.Number(1, 25);
                npc.speed = row.Number(.5f, 3.5f);
                npc.nextDecision = time + loaded.npcs.size() % loaded.rules.decisionPeriod;
                npc.nextFaction = time + 180;
                const int index = static_cast<int>(loaded.npcs.size());
                if (npc.role == "leader")
                {
                    Require(loaded.factions[npc.faction].leader < 0, "duplicate initial leader");
                    loaded.factions[npc.faction].leader = index;
                }
                loaded.npcs.push_back(npc);
                Require(loaded.npcs.size() <= 128, "NPC limit is 128");
            }
            else if (command == "diplomacy")
            {
                if (loaded.diplomacy.empty())
                {
                    loaded.diplomacy.assign(loaded.factions.size(),
                                            std::vector<float>(loaded.factions.size()));
                }
                const int a = loaded.FactionIndex(row.Word()), b = loaded.FactionIndex(row.Word());
                Require(a >= 0 && b >= 0 && a != b, "invalid diplomacy pair");
                Require(seen.insert("d:" + std::to_string(std::min(a, b)) + ":" +
                                    std::to_string(std::max(a, b)))
                            .second,
                        "duplicate diplomacy pair");
                Require(loaded.diplomacy.size() == loaded.factions.size(),
                        "define factions before diplomacy");
                loaded.diplomacy[a][b] = loaded.diplomacy[b][a] = row.Number(-100, 100);
            }
            else if (command == "relationship")
            {
                Relation relation;
                relation.from = loaded.NpcIndex(row.Word());
                relation.to = loaded.NpcIndex(row.Word());
                Require(relation.from >= 0 && relation.to >= 0 && relation.from != relation.to,
                        "invalid NPC relationship");
                Require(seen.insert("r:" + std::to_string(relation.from) + ":" +
                                    std::to_string(relation.to))
                            .second,
                        "duplicate NPC relationship");
                for (auto& value : relation.values)
                {
                    value = row.Number(-100, 100);
                }
                loaded.relations.push_back(relation);
            }
            else
            {
                Require(false, "unknown society record: " + command);
            }
        });
    Require(seen.count("rules") && seen.count("weights"),
            loaded.source + ": rules/weights required");
    Require(!loaded.npcs.empty() && !loaded.factions.empty(),
            loaded.source + ": NPCs/factions required");
    Require(loaded.factions.size() <= static_cast<std::size_t>(loaded.rules.maxFactions),
            "faction limit");
    if (loaded.diplomacy.empty())
    {
        loaded.diplomacy.assign(loaded.factions.size(), std::vector<float>(loaded.factions.size()));
    }
    Require(loaded.diplomacy.size() == loaded.factions.size(), "incomplete diplomacy table");
    for (std::size_t i = 0; i < loaded.factions.size(); ++i)
    {
        Require(loaded.factions[i].leader >= 0, loaded.source + ": faction needs a leader");
        loaded.diplomacy[i][i] = 100;
    }
    for (const auto& r : loaded.regions)
    {
        Require(r.owner >= 0, loaded.source + ": missing territory " + r.id);
    }
    loaded.LoadStory(root, storyFile);
    loaded.LoadLife(root, lifeFile);
    loaded.LoadEconomy(root, economyFile);
    loaded.LoadConflict(root, conflictFile, level);
    if (source == loaded.source)
    {
        Require(signature == loaded.signature,
                "Society/world definitions changed: restart required");
        return;
    }
    *this = std::move(loaded);
}

void WorldSociety::ReconcilePositions(const RandomLevelMap& map)
{
    auto approach = [&](Point& point)
    {
        if (map.CanStand(point.x, point.y))
        {
            return;
        }
        float best = 1e9f;
        Point result = point;
        for (const auto& cell : map.reachable)
        {
            const Point candidate{cell.x + .5f, cell.y + .5f};
            const float d = std::hypot(candidate.x - point.x, candidate.y - point.y);
            if (d < best && map.CanStand(candidate.x, candidate.y))
            {
                best = d;
                result = candidate;
            }
        }
        ContentData::Require(best < 8, "frontier has no reachable muster point");
        point = result;
    };
    for (auto& front : frontiers)
    {
        approach(front.firstRally);
        approach(front.secondRally);
    }
    for (auto& campaign : campaigns)
    {
        approach(campaign.attackRally);
        approach(campaign.defendRally);
    }
    for (auto& place : places)
    {
        if (!map.CanStand(place.position.x, place.position.y))
        {
            float best = 1e9f;
            Point target = place.position;
            for (const auto& cell : map.reachable)
            {
                float d =
                    std::hypot(cell.x + .5f - place.position.x, cell.y + .5f - place.position.y);
                if (d < best)
                {
                    best = d;
                    target = {cell.x + .5f, cell.y + .5f};
                }
            }
            ContentData::Require(best < 10,
                                 "activity place has no reachable approach: " + place.id);
            place.position = target;
        }
    }
    for (auto& place : places)
    {
        place.slots.clear();
        for (int slot = 0; slot < place.capacity; ++slot)
        {
            const float angle = slot * 6.2831853f / place.capacity;
            const float radius = place.kind == "rest" ? 2.2f : 1.4f;
            const Point desired{place.position.x + std::cos(angle) * radius,
                                place.position.y + std::sin(angle) * radius};
            Point best = place.position;
            float score = 1e9f;
            for (int y = -4; y <= 4; ++y)
            {
                for (int x = -4; x <= 4; ++x)
                {
                    Point candidate{std::floor(desired.x) + x + .5f,
                                    std::floor(desired.y) + y + .5f};
                    if (!map.CanTravel(place.position.x, place.position.y, candidate.x,
                                       candidate.y))
                    {
                        continue;
                    }
                    bool occupied = false;
                    for (const auto& used : place.slots)
                    {
                        occupied |= std::hypot(used.x - candidate.x, used.y - candidate.y) < .9f;
                    }
                    const float d = std::hypot(desired.x - candidate.x, desired.y - candidate.y);
                    if (!occupied && d < score)
                    {
                        best = candidate;
                        score = d;
                    }
                }
            }
            ContentData::Require(score < 1e8f, "activity slot is inaccessible: " + place.id);
            place.slots.push_back(best);
        }
    }
    for (auto& r : regions)
    {
        r.population = 0;
    }
    for (auto& npc : npcs)
    {
        if (!map.CanStand(npc.x, npc.y))
        {
            float nearest = 1e30f;
            Point best{npc.x, npc.y};
            for (auto cell : map.reachable)
            {
                const float distance = std::hypot(cell.x + .5f - npc.x, cell.y + .5f - npc.y);
                if (distance < nearest && map.CanStand(cell.x + .5f, cell.y + .5f))
                {
                    nearest = distance;
                    best = {cell.x + .5f, cell.y + .5f};
                }
            }
            ContentData::Require(nearest < 1e29f, "No walkable position for NPC " + npc.id);
            npc.x = best.x;
            npc.y = best.y;
        }
        npc.region =
            RegionIndex(map.DistrictAt(static_cast<int>(npc.x), static_cast<int>(npc.y)).id);
        if (npc.Alive())
        {
            ++regions[npc.region].population;
        }
        npc.path.clear();
        npc.pathCell = -1;
        npc.waypoint = 0;
    }
}

const char* WorldSociety::ActionName(Action value)
{
    static const char* names[] = {"휴식", "이동", "교역", "지원", "순찰",
                                  "조사", "습격", "후퇴", "교전", "동행"};
    return names[static_cast<int>(value)];
}

const char* WorldSociety::QuestName(QuestKind value)
{
    static const char* names[] = {"식량 전달", "부상자 치료", "피난 호위",
                                  "기록 조사", "휴전 협상",   "지역 방어"};
    return names[static_cast<int>(value)];
}

const char* WorldSociety::QuestStatus(QuestState value)
{
    static const char* names[] = {"제안", "수락", "진행", "보류", "완료", "실패", "만료", "대체됨"};
    return names[static_cast<int>(value)];
}

const char* WorldSociety::RoleName(const std::string& role)
{
    if (role == "leader")
    {
        return "지도자";
    }
    if (role == "guard")
    {
        return "경비대원";
    }
    if (role == "merchant")
    {
        return "상인";
    }
    if (role == "healer")
    {
        return "치료사";
    }
    if (role == "scholar")
    {
        return "학자";
    }
    return "피난민";
}
