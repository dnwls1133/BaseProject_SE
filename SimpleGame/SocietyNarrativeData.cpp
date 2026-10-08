#include "stdafx.h"
#include "WorldSociety.h"
#include "ContentData.h"
#include <algorithm>
#include <set>

int WorldSociety::StoryFactIndex(const std::string& id) const
{
    for (std::size_t i = 0; i < storyFacts.size(); ++i)
    {
        if (storyFacts[i].id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void WorldSociety::LoadStory(const std::string& root, const std::string& file)
{
    using ContentData::Require;
    storyProfiles.resize(npcs.size());
    storyBeliefs.resize(npcs.size());
    if (file.empty())
    {
        return;
    }
    const std::string path = ContentData::Path(root, file), text = ContentData::Read(path);
    for (unsigned char byte : text)
    {
        signature = (signature ^ byte) * 16777619u;
    }
    std::set<std::string> seen;
    ContentData::Rows(
        path, text,
        [&](const std::string& command, ContentData::Row& row)
        {
            auto npc = [&]()
            {
                const int index = NpcIndex(row.Word());
                Require(index >= 0, "unknown story NPC");
                return index;
            };
            auto fact = [&]()
            {
                const int index = StoryFactIndex(row.Word());
                Require(index >= 0, "unknown story evidence");
                return index;
            };
            if (command == "profile")
            {
                const int index = npc();
                Require(seen.insert("profile:" + npcs[index].id).second, "duplicate profile");
                auto& p = storyProfiles[index];
                p.past = row.Word();
                p.motive = row.Word();
                p.boundary = row.Word();
                p.belief = row.Word();
                p.unknown = row.Word();
                for (auto& value : p.weights)
                {
                    value = row.Number(-2, 2);
                }
            }
            else if (command == "bond")
            {
                const int a = npc(), b = npc();
                Require(a != b && seen.insert("bond:" + npcs[a].id + ":" + npcs[b].id).second,
                        "duplicate/self story bond");
                Relation r;
                r.from = a;
                r.to = b;
                for (auto& value : r.values)
                {
                    value = row.Number(-100, 100);
                }
                auto existing = std::find_if(relations.begin(), relations.end(),
                                             [&](const Relation& v)
                                             {
                                                 return v.from == a && v.to == b;
                                             });
                if (existing == relations.end())
                {
                    relations.push_back(r);
                }
                else
                {
                    *existing = r;
                }
            }
            else if (command == "fact")
            {
                StoryFact f;
                f.id = row.Word();
                Require(StoryFactIndex(f.id) < 0 && storyFacts.size() < 32,
                        "duplicate/too many facts");
                f.text = row.Word();
                f.correction = row.Word();
                f.region = RegionIndex(row.Word());
                f.observation = row.Word();
                f.threshold = row.Number(0, 100);
                Require(f.region >= 0, "unknown fact region");
                Require(f.observation != "food" || f.threshold <= 1,
                        "food threshold must be a ratio");
                Require(f.observation == "food" || f.observation == "injury" ||
                            f.observation == "site" || f.observation == "rivals" ||
                            f.observation == "testimony",
                        "unknown observation rule");
                storyFacts.push_back(f);
            }
            else if (command == "influence")
            {
                const int f = fact();
                Require(seen.insert("influence:" + storyFacts[f].id).second,
                        "duplicate fact influence");
                storyFacts[f].restraint = row.Number(0, 1);
                for (auto& value : storyFacts[f].influence)
                {
                    value = row.Number(-2, 2);
                }
            }
            else if (command == "knowledge")
            {
                const int receiver = npc();
                Belief b;
                b.fact = fact();
                b.origin = npc();
                b.claim = row.Integer(-1, 1);
                b.confidence = row.Number(.1f, 1);
                b.learned = minute;
                Require(b.claim != 0 && !KnownStory(receiver, b.fact),
                        "invalid/duplicate knowledge");
                storyBeliefs[receiver].push_back(b);
            }
            else if (command == "incident")
            {
                StoryIncident s;
                s.id = row.Word();
                Require(seen.insert("incident:" + s.id).second && storyIncidents.size() < 16,
                        "duplicate/too many incidents");
                s.title = row.Word();
                s.kind = row.Word();
                Require(s.kind == "ration" || s.kind == "signal" || s.kind == "ledger",
                        "unknown story incident kind");
                s.region = RegionIndex(row.Word());
                Require(s.region >= 0, "unknown incident region");
                s.actor = npc();
                s.partner = npc();
                Require(s.actor != s.partner, "incident needs distinct participants");
                s.trigger = fact();
                s.proof = fact();
                s.lifetime = row.Unsigned(10080);
                Require(s.lifetime >= 60, "incident deadline too short");
                storyIncidents.push_back(s);
            }
            else
            {
                Require(false, "unknown story record: " + command);
            }
        });
}

float WorldSociety::PersonalRegard(int from, int to) const
{
    for (const auto& r : relations)
    {
        if (r.from == from && r.to == to)
        {
            return r.values[0] + r.values[2] * .25f + r.values[4] * .15f - r.values[3] * .1f;
        }
    }
    return Trust(from, to);
}

std::string WorldSociety::StoryNews(int npc)
{
    if (storyBeliefs[npc].empty())
    {
        return {};
    }
    const Belief* chosen = nullptr;
    for (const auto& b : storyBeliefs[npc])
    {
        const auto* known = KnownStory(-1, b.fact);
        if (!known || known->learned < b.learned || known->claim != b.claim)
        {
            chosen = &b;
            break;
        }
    }
    if (!chosen)
    {
        chosen = &storyBeliefs[npc].back();
    }
    Belief report = *chosen;
    report.confidence *= .9f;
    LearnStory(-1, report, npc);
    return (report.claim > 0 ? storyFacts[report.fact].text : storyFacts[report.fact].correction) +
           " · 출처: " + (report.origin >= 0 ? npcs[report.origin].name : "직접 관찰") +
           " · 신뢰도 " + std::to_string(int(report.confidence * 100)) + "%";
}

std::string WorldSociety::GiveStoryEvidence(int npc)
{
    int delivered = 0;
    for (auto b : playerBeliefs)
    {
        b.confidence *= std::clamp(.8f + npcs[npc].playerRelation[0] * .002f, .4f, 1.f);
        if (LearnStory(npc, b, -1))
        {
            ++delivered;
        }
    }
    return delivered ? "출처가 있는 관찰 기록을 전달했습니다. 상대의 다음 판단에 반영됩니다."
                     : "새롭게 받아들일 기록이 없습니다. 현장을 관찰하거나 다른 주민의 소식을 "
                       "들어보세요.";
}

std::vector<std::string> WorldSociety::StoryJournal(int tab) const
{
    std::vector<std::string> lines;
    if (tab == 5)
    {
        lines.push_back("대표 인물의 배경 · 행동 결과와 미정인 진실을 구분합니다.");
        for (std::size_t i = 0; i < storyProfiles.size(); ++i)
        {
            const auto& p = storyProfiles[i];
            if (p.past.empty())
            {
                continue;
            }
            lines.push_back(npcs[i].name + " · " + p.motive);
            lines.push_back("과거: " + p.past);
            lines.push_back("지키려는 선: " + p.boundary);
            lines.push_back("믿는 것: " + p.belief);
            lines.push_back("모르는 것: " + p.unknown);
        }
        return lines;
    }
    lines.push_back("세계 서사 · 대화나 요청 수락 없이도 진행됩니다.");
    static const char* states[] = {"조건 대기", "진행 중", "해결", "실패/철수"};
    for (const auto& s : storyIncidents)
    {
        lines.push_back(s.title + " · " + states[s.stage]);
        lines.push_back("  " + npcs[s.actor].name + " / " + npcs[s.partner].name + " · " +
                        regions[s.region].name);
        if (!s.outcome.empty())
        {
            lines.push_back("  " + s.outcome);
        }
        if (s.stage == 1)
        {
            lines.push_back("  남은 세계 분 " +
                            std::to_string(s.deadline > minute ? s.deadline - minute : 0));
        }
    }
    lines.push_back("내가 접한 증거 · 사실의 전모와 같은 뜻은 아닙니다.");
    for (const auto& b : playerBeliefs)
    {
        lines.push_back((b.claim > 0 ? storyFacts[b.fact].text : storyFacts[b.fact].correction) +
                        " · " + std::to_string(int(b.confidence * 100)) + "%");
    }
    lines.push_back("현장 관찰 / F→7 소식 듣기 / F→T 기록 전달 · 전달 중 보고 " +
                    std::to_string(storyLetters.size()));
    return lines;
}

void WorldSociety::StoryInfluence(int index, std::array<float, 10>& score) const
{
    for (std::size_t i = 0; i < score.size(); ++i)
    {
        score[i] += storyProfiles[index].weights[i];
        for (const auto& b : storyBeliefs[index])
        {
            if (b.confidence >= .5f && minute <= b.learned + 1440)
            {
                score[i] += storyFacts[b.fact].influence[i] * b.confidence * b.claim;
            }
        }
    }
}

float WorldSociety::StoryRestraint(int index) const
{
    float value = 0;
    for (const auto& b : storyBeliefs[index])
    {
        if (b.claim > 0 && b.confidence >= .55f && minute <= b.learned + 1440)
        {
            value += storyFacts[b.fact].restraint * b.confidence;
        }
    }
    return value;
}

float WorldSociety::StoryCargo(int npc) const
{
    for (const auto& s : storyIncidents)
    {
        if (s.actor == npc && s.stage == 1)
        {
            return s.cargo;
        }
    }
    return 0;
}
