#include "stdafx.h"
#include "WorldSociety.h"
#include "ContentData.h"
#include <iomanip>
#include <ostream>
#include <algorithm>

void WorldSociety::WriteStory(std::ostream& out) const
{
    auto id = [&](int npc)
    {
        return npc < 0 ? std::string("player") : npcs[npc].id;
    };
    auto belief = [&](const Belief& b)
    {
        out << std::quoted(storyFacts[b.fact].id) << ' ' << std::quoted(id(b.origin)) << ' '
            << std::quoted(b.subject < 0 ? "-" : npcs[b.subject].id) << ' ' << b.claim << ' '
            << b.confidence << ' ' << b.learned;
    };
    out << "story_clock " << nextStoryTick << ' ' << nextStoryShare << '\n';
    for (int n = -1; n < static_cast<int>(npcs.size()); ++n)
    {
        const auto& knowledge = n < 0 ? playerBeliefs : storyBeliefs[n];
        out << "story_inventory " << std::quoted(id(n)) << ' ' << knowledge.size() << '\n';
        for (const auto& b : knowledge)
        {
            out << "story_belief " << std::quoted(id(n)) << ' ';
            belief(b);
            out << '\n';
        }
    }
    for (const auto& l : storyLetters)
    {
        out << "story_letter " << l.id << ' ' << l.due << ' ' << l.expires << ' '
            << std::quoted(id(l.from)) << ' ' << std::quoted(id(l.to)) << ' ';
        belief(l.evidence);
        out << '\n';
    }
    for (const auto& s : storyIncidents)
    {
        out << "story_incident " << std::quoted(s.id) << ' ' << std::quoted(npcs[s.actor].id) << ' '
            << std::quoted(npcs[s.partner].id) << ' ' << s.stage << ' ' << s.step << ' '
            << std::quoted(s.patient < 0 ? "-" : npcs[s.patient].id) << ' ' << s.deadline << ' '
            << s.cause << ' ' << std::quoted(s.supplier < 0 ? "-" : factions[s.supplier].id) << ' '
            << s.cargo << ' ' << std::quoted(s.outcome.empty() ? "-" : s.outcome) << '\n';
    }
}

bool WorldSociety::ReadStory(const std::string& command, ContentData::Row& row)
{
    if (command.compare(0, 6, "story_") != 0)
    {
        return false;
    }
    using ContentData::Require;
    auto npc = [&](bool player, bool optional)
    {
        const std::string id = row.Word();
        if ((player && id == "player") || (optional && id == "-"))
        {
            return -1;
        }
        const int index = NpcIndex(id);
        Require(index >= 0, "unknown story NPC reference");
        return index;
    };
    auto unique = [&](const std::string& id)
    {
        Require(restoreSeen.insert(command + ":" + id).second, "duplicate narrative state");
    };
    auto belief = [&]()
    {
        Belief b;
        b.fact = StoryFactIndex(row.Word());
        Require(b.fact >= 0, "unknown saved fact");
        b.origin = npc(true, false);
        b.subject = npc(false, true);
        b.claim = row.Integer(-1, 1);
        Require(b.claim != 0, "invalid saved claim");
        b.confidence = row.Number(0, 1);
        b.learned = row.Unsigned();
        return b;
    };
    if (command == "story_clock")
    {
        unique("clock");
        nextStoryTick = row.Unsigned();
        nextStoryShare = row.Unsigned();
    }
    else if (command == "story_inventory")
    {
        const int n = npc(true, false);
        unique(std::to_string(n));
        const int count = row.Integer(0, static_cast<int>(storyFacts.size()));
        restoreSeen.insert("story_count:" + std::to_string(n) + ":" + std::to_string(count));
    }
    else if (command == "story_belief")
    {
        const int n = npc(true, false);
        Belief b = belief();
        unique(std::to_string(n) + ":" + std::to_string(b.fact));
        (n < 0 ? playerBeliefs : storyBeliefs[n]).push_back(b);
    }
    else if (command == "story_letter")
    {
        Require(storyLetters.size() < 128, "story message queue too large");
        StoryLetter l;
        l.id = row.Unsigned();
        unique(std::to_string(l.id));
        l.due = row.Unsigned();
        l.expires = row.Unsigned();
        l.from = npc(false, false);
        l.to = npc(false, false);
        l.evidence = belief();
        Require(l.from != l.to && l.expires >= l.due, "invalid story delivery");
        storyLetters.push_back(l);
    }
    else if (command == "story_incident")
    {
        const std::string id = row.Word();
        unique(id);
        auto found = std::find_if(storyIncidents.begin(), storyIncidents.end(),
                                  [&](const StoryIncident& s)
                                  {
                                      return s.id == id;
                                  });
        Require(found != storyIncidents.end(), "unknown saved story incident");
        auto& s = *found;
        s.actor = npc(false, false);
        s.partner = npc(false, false);
        s.stage = row.Integer(0, 3);
        s.step = row.Integer(0, 3);
        s.patient = npc(false, true);
        s.deadline = row.Unsigned();
        s.cause = row.Unsigned();
        const std::string supplier = row.Word();
        s.supplier = FactionIndex(supplier);
        Require(s.supplier >= 0 || supplier == "-", "unknown shipment supplier");
        s.cargo = row.Number(0, .1f);
        s.outcome = row.Word();
        if (s.outcome == "-")
        {
            s.outcome.clear();
        }
        Require(s.cargo == 0 ||
                    (s.kind == "ration" && s.stage == 1 && s.step == 2 && s.supplier >= 0),
                "invalid cargo escrow");
    }
    else
    {
        Require(false, "unknown narrative checkpoint record");
    }
    return true;
}

void WorldSociety::FinishStoryRestore()
{
    using ContentData::Require;
    Require(restoreSeen.count("story_clock:clock") != 0, "missing narrative clock");
    for (int n = -1; n < static_cast<int>(npcs.size()); ++n)
    {
        const auto& beliefs = n < 0 ? playerBeliefs : storyBeliefs[n];
        Require(restoreSeen.count("story_inventory:" + std::to_string(n)) &&
                    restoreSeen.count("story_count:" + std::to_string(n) + ":" +
                                      std::to_string(beliefs.size())),
                "incomplete narrative knowledge");
        for (const auto& b : beliefs)
        {
            Require(b.learned <= minute, "knowledge from the future");
        }
    }
    for (const auto& s : storyIncidents)
    {
        Require(restoreSeen.count("story_incident:" + s.id) != 0 && s.cause < serial,
                "incomplete narrative incidents");
        Require(s.stage != 1 || (s.cause > 0 && s.deadline > 0), "invalid active narrative");
    }
    for (const auto& l : storyLetters)
    {
        Require(l.id < serial && l.evidence.learned <= minute,
                "invalid narrative message sequence");
    }
}
