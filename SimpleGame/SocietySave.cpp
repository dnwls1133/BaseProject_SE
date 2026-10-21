#include "stdafx.h"
#include "WorldSociety.h"
#include "ContentData.h"
#include "RandomLevelMap.h"
#include <algorithm>
#include <iomanip>
#include <ostream>

void WorldSociety::Write(std::ostream& out) const
{
    auto npcId = [&](int n)
    {
        return n < 0 ? std::string("-") : npcs[n].id;
    };
    auto factionId = [&](int f)
    {
        return f < 0 ? std::string("-") : factions[f].id;
    };
    for (const auto& f : factions)
    {
        out << "soc_faction " << std::quoted(f.id) << ' ' << std::quoted(f.name) << ' '
            << std::quoted(f.motto) << ' ' << std::quoted(regions[f.home].id) << ' '
            << std::quoted(npcId(f.leader)) << ' ' << std::quoted(factionId(f.parent)) << ' '
            << static_cast<unsigned>(f.emblem) << ' ' << f.color.r << ' ' << f.color.g << ' '
            << f.color.b << ' ' << f.ideology << ' ' << f.supplies << ' ' << f.support << ' '
            << f.active << '\n';
    }
    for (const auto& c : campaigns)
    {
        out << "soc_campaign " << std::quoted(factionId(c.attacker)) << ' '
            << std::quoted(factionId(c.defender)) << ' ' << std::quoted(regions[c.region].id) << ' '
            << c.active << ' ' << c.until << ' ' << c.stage << ' ' << c.stageSince << ' '
            << c.attackRally.x << ' ' << c.attackRally.y << ' ' << c.defendRally.x << ' '
            << c.defendRally.y << '\n';
    }
    WriteStory(out);
    WriteLife(out);
    WriteEconomy(out);
    WriteConflict(out);
    out << "soc_state " << minute << ' ' << serial << ' ' << randomState << ' ' << lastFactionTick
        << ' ' << nextQuestTick << ' ' << std::quoted(factionId(playerFaction)) << ' '
        << playerFood;
    for (float value : karma)
    {
        out << ' ' << value;
    }
    out << '\n';
    for (std::size_t i = 0; i < factions.size(); ++i)
    {
        out << "soc_diplomacy " << std::quoted(factions[i].id);
        for (float value : diplomacy[i])
        {
            out << ' ' << value;
        }
        out << '\n';
    }
    for (const auto& r : regions)
    {
        out << "soc_region " << std::quoted(r.id) << ' ' << std::quoted(factionId(r.owner)) << ' '
            << r.population << ' ' << r.security << ' ' << r.blood << ' ' << r.hive << ' ' << r.fear
            << ' ' << r.unrest << ' ' << r.hospital << ' ' << r.storehouse << ' ' << r.wall << '\n';
    }
    for (const auto& n : npcs)
    {
        out << "soc_npc " << std::quoted(n.id) << ' ' << std::quoted(factionId(n.faction)) << ' '
            << std::quoted(regions[n.region].id) << ' ' << n.x << ' ' << n.y << ' ' << n.health
            << ' ' << n.food << ' ' << n.infection << ' ' << static_cast<int>(n.action) << ' '
            << std::quoted(npcId(n.target)) << ' ' << std::quoted(regions[n.destination].id) << ' '
            << n.nextDecision << ' ' << n.nextMessage << ' ' << n.nextFaction << ' ' << n.phase
            << ' ' << n.facing << ' ' << n.windup << ' ' << n.cooldown << ' ' << n.flash << ' '
            << n.collapse << ' ' << n.farTime << ' ' << n.playerThreat << ' ' << n.speechTime << ' '
            << std::quoted(n.speech.empty() ? "-" : n.speech);
        out << ' ' << n.wildTarget << ' ' << n.wildDecision << ' ' << n.courierStop << ' '
            << n.courierPause;
        for (float value : n.playerRelation)
        {
            out << ' ' << value;
        }
        out << '\n';
        for (const auto& m : n.memories)
        {
            out << "soc_memory " << std::quoted(n.id) << ' ' << std::quoted(regions[m.region].id)
                << ' ' << std::quoted(npcId(m.source)) << ' ' << m.food << ' ' << m.infection << ' '
                << m.confidence << ' ' << m.learned << '\n';
        }
        out << "soc_path " << std::quoted(n.id) << ' ' << n.pathCell << ' ' << n.waypoint << ' '
            << n.path.size();
        for (Point p : n.path)
        {
            out << ' ' << p.x << ' ' << p.y;
        }
        out << '\n';
    }
    for (const auto& r : relations)
    {
        out << "soc_relation " << std::quoted(npcId(r.from)) << ' ' << std::quoted(npcId(r.to));
        for (float value : r.values)
        {
            out << ' ' << value;
        }
        out << '\n';
    }
    for (const auto& m : messages)
    {
        out << "soc_message " << m.id << ' ' << m.cause << ' ' << m.due << ' ' << m.expires << ' '
            << m.observed << ' ' << std::quoted(npcId(m.from)) << ' ' << std::quoted(npcId(m.to))
            << ' ' << std::quoted(regions[m.region].id) << ' ' << m.kind << ' ' << m.food << ' '
            << m.infection << ' ' << m.confidence << '\n';
    }
    for (const auto& q : quests)
    {
        out << "soc_quest " << q.id << ' ' << q.cause << ' ' << q.deadline << ' '
            << std::quoted(npcId(q.issuer)) << ' ' << std::quoted(npcId(q.target)) << ' '
            << std::quoted(regions[q.region].id) << ' ' << q.progress << ' '
            << static_cast<int>(q.kind) << ' ' << static_cast<int>(q.state) << '\n';
    }
    for (const auto& h : history)
    {
        out << "soc_history " << h.id << ' ' << h.cause << ' ' << h.minute << ' '
            << std::quoted(h.text) << '\n';
    }
}

void WorldSociety::BeginRestore()
{
    economyOrders.clear();
    restoreSeen.clear();
    restoredState = false;
    restoredNpcs.assign(npcs.size(), false);
    restoredRegions.assign(regions.size(), false);
    campaigns.clear();
    agreements.clear();
    storyLetters.clear();
    playerBeliefs.clear();
    for (auto& knowledge : storyBeliefs)
    {
        knowledge.clear();
    }
    relations.clear();
    messages.clear();
    quests.clear();
    history.clear();
    for (auto& n : npcs)
    {
        n.memories.clear();
        n.path.clear();
    }
}

bool WorldSociety::ReadRow(const std::string& command, ContentData::Row& row)
{
    using ContentData::Require;
    if (ReadConflict(command, row))
    {
        return true;
    }
    if (ReadEconomy(command, row))
    {
        return true;
    }
    if (ReadLife(command, row))
    {
        return true;
    }
    if (ReadStory(command, row))
    {
        return true;
    }
    auto readNpc = [&](bool optional = false)
    {
        const std::string id = row.Word();
        const int n = NpcIndex(id);
        Require(n >= 0 || (optional && id == "-"), "unknown saved NPC: " + id);
        return n;
    };
    auto readFaction = [&](bool optional = false)
    {
        const std::string id = row.Word();
        const int f = FactionIndex(id);
        Require(f >= 0 || (optional && id == "-"), "unknown saved faction: " + id);
        return f;
    };
    auto readRegion = [&]()
    {
        const std::string id = row.Word();
        const int r = RegionIndex(id);
        Require(r >= 0, "unknown saved region: " + id);
        return r;
    };
    auto unique = [&](const std::string& id)
    {
        Require(restoreSeen.insert(command + ":" + id).second,
                "duplicate save record: " + command + ":" + id);
    };
    if (command == "soc_state")
    {
        unique("state");
        restoredState = true;
        minute = row.Unsigned();
        serial = row.Unsigned();
        randomState = static_cast<std::uint32_t>(row.Unsigned(4294967295ULL));
        Require(serial > 0 && randomState > 0, "invalid saved sequence/random state");
        lastFactionTick = row.Unsigned();
        nextQuestTick = row.Unsigned();
        // Factions precede this record when writing a complete checkpoint.
        playerFaction = readFaction(true);
        playerFood = row.Integer(0, 1000000);
        for (float& value : karma)
        {
            value = row.Number(-100, 100);
        }
    }
    else if (command == "soc_faction")
    {
        Faction f;
        f.id = row.Word();
        unique(f.id);
        const int existing = FactionIndex(f.id);
        f.name = row.Word();
        f.motto = row.Word();
        f.home = readRegion();
        f.leader = readNpc(true);
        f.parent = readFaction(true);
        f.emblem = static_cast<ModelId>(row.Integer(0, 10000));
        f.color.r = row.Number(0, 1);
        f.color.g = row.Number(0, 1);
        f.color.b = row.Number(0, 1);
        f.ideology = row.Number(-1, 1);
        f.supplies = row.Number(0, 1000000);
        f.support = row.Number(0, 1);
        f.active = row.Integer(0, 1) != 0;
        if (existing >= 0)
        {
            Require(f.emblem == factions[existing].emblem, "saved faction model mismatch");
            factions[existing] = f;
        }
        else
        {
            Require(f.parent >= 0 && f.emblem == factions[f.parent].emblem,
                    "invalid new faction parent/model");
            Require(factions.size() < static_cast<std::size_t>(rules.maxFactions),
                    "too many saved factions");
            factions.push_back(f);
        }
        diplomacy.resize(factions.size());
        for (auto& values : diplomacy)
        {
            values.resize(factions.size(), 0);
        }
    }
    else if (command == "soc_campaign")
    {
        Campaign c;
        c.attacker = readFaction();
        c.defender = readFaction();
        c.region = readRegion();
        Require(c.attacker != c.defender, "self war is invalid");
        unique(std::to_string(std::min(c.attacker, c.defender)) + ":" +
               std::to_string(std::max(c.attacker, c.defender)));
        c.active = row.Integer(0, 1) != 0;
        c.until = row.Unsigned();
        c.stage = row.Integer(0, 4);
        c.stageSince = row.Unsigned();
        c.attackRally = {row.Number(0, 1024), row.Number(0, 1024)};
        c.defendRally = {row.Number(0, 1024), row.Number(0, 1024)};
        campaigns.push_back(c);
    }
    else if (command == "soc_diplomacy")
    {
        const int f = readFaction();
        unique(factions[f].id);
        for (auto& value : diplomacy[f])
        {
            value = row.Number(-100, 100);
        }
    }
    else if (command == "soc_region")
    {
        int r = readRegion();
        unique(regions[r].id);
        restoredRegions[r] = true;
        auto& region = regions[r];
        region.owner = readFaction(true);
        region.population = row.Integer(0, 128);
        region.security = row.Number(0, 1);
        region.blood = row.Number(0, 1);
        region.hive = row.Number(0, 1);
        region.fear = row.Number(0, 1);
        region.unrest = row.Number(0, 1);
        region.hospital = row.Number(0, 1);
        region.storehouse = row.Number(0, 1);
        region.wall = row.Number(0, 1);
    }
    else if (command == "soc_npc")
    {
        const int index = readNpc();
        auto& n = npcs[index];
        unique(n.id);
        restoredNpcs[index] = true;
        n.faction = readFaction();
        n.region = readRegion();
        n.x = row.Number(0, 256);
        n.y = row.Number(0, 256);
        n.health = row.Number(0, 100);
        n.food = row.Number(0, 1000000);
        n.infection = row.Number(0, 1);
        n.action = static_cast<Action>(row.Integer(0, 9));
        n.target = readNpc(true);
        n.destination = readRegion();
        n.nextDecision = row.Unsigned();
        n.nextMessage = row.Unsigned();
        n.nextFaction = row.Unsigned();
        n.phase = row.Number(0, 1e9f);
        n.facing = row.Number(-1, 1);
        n.windup = row.Number(-1, 5);
        n.cooldown = row.Number(0, 100);
        n.flash = row.Number(0, 1);
        n.collapse = row.Number(0, 1);
        n.farTime = row.Number(0, 1);
        n.playerThreat = row.Integer(0, 1) != 0;
        n.speechTime = row.Number(0, 60);
        n.speech = row.Word();
        if (n.speech == "-")
        {
            n.speech.clear();
        }
        n.wildTarget = row.Integer(-1, 1023);
        n.wildDecision = row.Number(-1, 1);
        n.courierStop = row.Integer(0, 15);
        n.courierPause = row.Number(0, 3);
        for (float& value : n.playerRelation)
        {
            value = row.Number(-100, 100);
        }
    }
    else if (command == "soc_memory")
    {
        int n = readNpc();
        Memory m;
        m.region = readRegion();
        unique(npcs[n].id + ":" + regions[m.region].id);
        m.source = readNpc(true);
        m.food = row.Number(0, 1);
        m.infection = row.Number(0, 1);
        m.confidence = row.Number(0, 1);
        m.learned = row.Unsigned();
        npcs[n].memories.push_back(m);
    }
    else if (command == "soc_path")
    {
        int n = readNpc();
        unique(npcs[n].id);
        npcs[n].pathCell = row.Integer(-1, 65536);
        npcs[n].waypoint = static_cast<std::size_t>(row.Integer(0, 65536));
        const int count = row.Integer(0, 65536);
        Require(npcs[n].waypoint <= static_cast<std::size_t>(count), "invalid waypoint");
        for (int i = 0; i < count; ++i)
        {
            Point point;
            point.x = row.Number(0, 256);
            point.y = row.Number(0, 256);
            npcs[n].path.push_back(point);
        }
    }
    else if (command == "soc_relation")
    {
        Relation relation;
        relation.from = readNpc();
        relation.to = readNpc();
        unique(npcs[relation.from].id + ":" + npcs[relation.to].id);
        Require(relation.from != relation.to, "self relationship is invalid");
        for (float& value : relation.values)
        {
            value = row.Number(-100, 100);
        }
        relations.push_back(relation);
    }
    else if (command == "soc_message")
    {
        Require(messages.size() < 256, "too many saved messages");
        Message m;
        m.id = row.Unsigned();
        unique(std::to_string(m.id));
        m.cause = row.Unsigned();
        m.due = row.Unsigned();
        m.expires = row.Unsigned();
        m.observed = row.Unsigned();
        m.from = readNpc();
        m.to = readNpc();
        m.region = readRegion();
        m.kind = row.Integer(0, 2);
        m.food = row.Number(0, 1);
        m.infection = row.Number(0, 1);
        m.confidence = row.Number(0, 1);
        messages.push_back(m);
    }
    else if (command == "soc_quest")
    {
        Require(quests.size() < 96, "too many saved quests");
        Quest q;
        q.id = row.Unsigned();
        unique(std::to_string(q.id));
        q.cause = row.Unsigned();
        q.deadline = row.Unsigned();
        q.issuer = readNpc();
        q.target = readNpc(true);
        q.region = readRegion();
        q.progress = row.Integer(0, 1000000);
        q.kind = static_cast<QuestKind>(row.Integer(0, 5));
        q.state = static_cast<QuestState>(row.Integer(0, 7));
        Require((q.kind != QuestKind::Heal && q.kind != QuestKind::Escort &&
                 q.kind != QuestKind::Negotiate) ||
                    q.target >= 0,
                "quest target required");
        quests.push_back(q);
    }
    else if (command == "soc_history")
    {
        Require(history.size() < 96, "too much saved history");
        Change h;
        h.id = row.Unsigned();
        unique(std::to_string(h.id));
        h.cause = row.Unsigned();
        h.minute = row.Unsigned();
        h.text = row.Word();
        history.push_back(h);
    }
    else
    {
        return false;
    }
    return true;
}

void WorldSociety::FinishRestore(const RandomLevelMap& map)
{
    using ContentData::Require;
    FinishConflictRestore();
    Require(restoredState &&
                std::all_of(restoredNpcs.begin(), restoredNpcs.end(),
                            [](bool v)
                            {
                                return v;
                            }) &&
                std::all_of(restoredRegions.begin(), restoredRegions.end(),
                            [](bool v)
                            {
                                return v;
                            }),
            "incomplete society save");
    for (const auto& campaign : campaigns)
    {
        Require(campaign.stageSince <= minute, "campaign stage is in the future");
        for (Point rally : {campaign.attackRally, campaign.defendRally})
        {
            Require(rally.x >= 0 && rally.y >= 0 && rally.x < map.Width && rally.y < map.Height,
                    "saved campaign rally outside map");
        }
    }
    for (std::size_t i = 0; i < factions.size(); ++i)
    {
        int ancestor = factions[i].parent;
        std::size_t hops = 0;
        while (ancestor >= 0)
        {
            Require(ancestor < static_cast<int>(factions.size()) &&
                        ancestor != static_cast<int>(i) && ++hops <= factions.size(),
                    "invalid faction ancestry");
            ancestor = factions[ancestor].parent;
        }
    }
    for (const auto& f : factions)
    {
        Require(restoreSeen.count("soc_faction:" + f.id) &&
                    restoreSeen.count("soc_diplomacy:" + f.id),
                "incomplete faction save");
    }
    for (const auto& n : npcs)
    {
        Require(restoreSeen.count("soc_path:" + n.id) && map.CanStand(n.x, n.y),
                "invalid saved NPC placement");
        if (IsCourier(int(&n - npcs.data())))
        {
            Require(n.courierStop < int(courierRoutes.at(int(&n - npcs.data())).size()),
                    "invalid saved courier stop");
        }
        Require(n.pathCell < map.Width * map.Height, "invalid saved path destination");
        for (const Point point : n.path)
        {
            Require(map.CanStand(point.x, point.y), "invalid saved NPC path");
        }
    }
    for (const auto& m : messages)
    {
        Require(m.id < serial && m.cause < serial && m.observed <= minute,
                "invalid saved message sequence or observation time");
    }
    for (const auto& q : quests)
    {
        Require(q.id < serial && q.cause < serial, "invalid saved quest sequence");
    }
    FinishStoryRestore();
    FinishLifeRestore();
    FinishEconomyRestore();
    neighborhood.clear();
}
