#include "stdafx.h"
#include "WorldSociety.h"
#include "ContentData.h"
#include <algorithm>
#include <cmath>

void WorldSociety::LoadLife(const std::string& root, const std::string& file)
{
    if (file.empty())
    {
        return;
    }
    using ContentData::Require;
    const auto path = ContentData::Path(root, file), text = ContentData::Read(path);
    for (unsigned char byte : text)
    {
        signature = (signature ^ byte) * 16777619u;
    }
    lives.resize(npcs.size());
    std::set<std::string> seen;
    auto site = [&](const std::string& id)
    {
        for (std::size_t i = 0; i < places.size(); ++i)
        {
            if (places[i].id == id)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    ContentData::Rows(path, text,
                      [&](const std::string& cmd, ContentData::Row& row)
                      {
                          if (cmd == "rules")
                          {
                              Require(seen.insert("rules").second, "duplicate life rules");
                              lifeWorkSeconds = row.Number(3, 120);
                              lifeRestSeconds = row.Number(5, 180);
                              lifeFoodPerDay = row.Number(1, 30);
                              lifeSocialMinutes = row.Integer(30, 720);
                          }
                          else if (cmd == "planning")
                          {
                              Require(seen.insert(cmd).second, "duplicate planning policy");
                              planningRules.hunger = row.Number(10, 60);
                              planningRules.fatigue = row.Number(40, 95);
                              planningRules.stock = row.Number(1, 6);
                              planningRules.serviceRange = row.Number(3, 30);
                              planningRules.retryMinutes = row.Integer(3, 120);
                              planningRules.avoidMinutes = row.Integer(30, 720);
                              planningRules.goalMinutes = row.Integer(120, 2880);
                              planningRules.handoffSeconds = row.Number(10, 300);
                          }
                          else if (cmd == "place")
                          {
                              LifePlace p;
                              p.id = row.Word();
                              p.name = row.Word();
                              p.kind = row.Word();
                              p.model = row.Word();
                              p.region = RegionIndex(row.Word());
                              p.position.x = row.Number(0, 512);
                              p.position.y = row.Number(0, 512);
                              p.capacity = row.Integer(1, 12);
                              p.medicine = row.Number(0, 100);
                              Require(p.region >= 0 && site(p.id) < 0 && places.size() < 128,
                                      "invalid/duplicate life place");
                              Require(p.kind == "rest" || p.kind == "warehouse" ||
                                          p.kind == "clinic" || p.kind == "farm" ||
                                          p.kind == "market" || p.kind == "watch" ||
                                          p.kind == "archive",
                                      "unknown place activity");
                              places.push_back(p);
                          }
                          else if (cmd == "resident")
                          {
                              const int n = NpcIndex(row.Word());
                              Require(n >= 0, "unknown resident");
                              Require(seen.insert(npcs[n].id).second, "duplicate resident");
                              auto& l = lives[n];
                              l.home = site(row.Word());
                              l.work = site(row.Word());
                              Require(l.home >= 0 && l.work >= 0, "unknown home/workplace");
                              l.fatigue = row.Number(0, 100);
                              npcs[n].food = row.Number(0, 12);
                              l.social = row.Number(0, 100);
                              l.medicine = npcs[n].role == "healer" ? 2 : 0;
                              l.nextSocial = minute + (n % 5) * 4;
                              l.nextTrade = minute + 80 + (n % 3) * 15;
                              l.satiation = 20 + (n * 19) % 65;
                          }
                          else if (cmd == "route")
                          {
                              int a = site(row.Word()), b = site(row.Word());
                              Require(a >= 0 && b >= 0 && a != b && places[a].kind == "warehouse" &&
                                          places[b].kind == "market",
                                      "route requires warehouse and market");
                              Require(tradeRoutes.size() < 64 &&
                                          std::find(tradeRoutes.begin(), tradeRoutes.end(),
                                                    std::make_pair(a, b)) == tradeRoutes.end(),
                                      "duplicate/too many trade routes");
                              tradeRoutes.push_back({a, b});
                          }
                          else if (cmd == "line")
                          {
                              const std::string key = row.Word();
                              lifeLines[key].push_back(row.Word());
                              Require(lifeLines[key].size() <= 12, "too many line variants");
                          }
                          else
                          {
                              Require(false, "unknown life record");
                          }
                      });
    for (const auto& n : npcs)
    {
        Require(seen.count(n.id) != 0, "missing resident: " + n.id);
    }
    for (const std::string key :
         {"request_food", "request_care", "request_news", "counter", "accept", "refuse",
          "delivered", "thanks", "cancel", "work", "rest", "collect", "research", "news", "wait"})
    {
        Require(lifeLines.count(key) != 0, "missing life dialogue: " + key);
    }
    rules.foodPerDay = lifeFoodPerDay;
}

void WorldSociety::SayLife(int index, const std::string& key, int other, int place,
                           const std::string& detail)
{
    auto found = lifeLines.find(key);
    if (found == lifeLines.end() || !npcs[index].Alive())
    {
        return;
    }
    auto& n = npcs[index];
    std::string text = found->second[Random() % found->second.size()];
    auto replace = [&](const std::string& token, const std::string& value)
    {
        std::size_t at = 0;
        while ((at = text.find(token, at)) != std::string::npos)
        {
            text.replace(at, token.size(), value);
            at += value.size();
        }
    };
    replace("{self}", n.name);
    replace("{other}", other >= 0 ? npcs[other].name : "이웃");
    replace("{place}", place >= 0 ? places[place].name : regions[n.region].name);
    replace("{detail}", detail);
    n.speech = text;
    n.speechTime = 7;
    Record("npc_conversation", n.id, other >= 0 ? npcs[other].id : "local", "intent", key, text,
           lives[index].agreement ? lives[index].agreement : lives[index].cause);
}

int WorldSociety::LifeSlot(int npc, int place) const
{
    if (place < 0)
    {
        return -1;
    }
    for (int slot = 0; slot < places[place].capacity; ++slot)
    {
        bool used = false;
        for (std::size_t i = 0; i < lives.size(); ++i)
        {
            used = used || (int(i) != npc && npcs[i].Alive() && lives[i].task != LifeTask::None &&
                            lives[i].place == place && lives[i].slot == slot);
        }
        if (!used)
        {
            return slot;
        }
    }
    return -1;
}

int WorldSociety::LifeSite(int index, const std::string& kind) const
{
    int best = -1;
    float distance = 1e9f;
    const auto& n = npcs[index];
    for (std::size_t i = 0; i < places.size(); ++i)
    {
        const auto& p = places[i];
        if (p.kind != kind)
        {
            continue;
        }
        int count = 0;
        for (std::size_t j = 0; j < lives.size(); ++j)
        {
            if (int(j) != index && npcs[j].Alive() && lives[j].task != LifeTask::None &&
                lives[j].place == int(i))
            {
                ++count;
            }
        }
        if (count >= p.capacity)
        {
            continue;
        }
        // Only familiar home/current regions or actually observed regions are destinations.
        bool known = p.region == n.home || p.region == n.region ||
                     (lives[index].work >= 0 && p.region == places[lives[index].work].region);
        for (const auto& memory : n.memories)
        {
            known = known || memory.region == p.region;
        }
        if (!known)
        {
            continue;
        }
        const int owner = regions[p.region].owner;
        if (owner >= 0 && diplomacy[n.faction][owner] < rules.hostility)
        {
            continue;
        }
        float d = std::hypot(n.x - p.position.x, n.y - p.position.y);
        if (d < distance)
        {
            distance = d;
            best = int(i);
        }
    }
    return best;
}

int WorldSociety::LifeActivity(int npc) const
{
    if (!HasLife() || !npcs[npc].Alive() || npcs[npc].action == Action::Combat ||
        npcs[npc].action == Action::Flee || npcs[npc].action == Action::Raid ||
        npcs[npc].wildTarget >= 0 || npcs[npc].playerThreat)
    {
        return 0;
    }
    const auto& l = lives[npc];
    for (const auto& a : agreements)
    {
        if (a.supplier == npc && a.stage == 2 && a.cargo > 0)
        {
            return a.kind == 1 ? 3 : 2;
        }
    }
    if (l.cargo > 0)
    {
        return 2;
    }
    if (l.task == LifeTask::Meet)
    {
        return npcs[npc].role == "healer" ? 3 : 4;
    }
    if (l.progress <= 0)
    {
        return 0;
    }
    if (l.task == LifeTask::Eat)
    {
        return 3;
    }
    if (l.task == LifeTask::Work || l.task == LifeTask::Research)
    {
        return 1;
    }
    if (l.task == LifeTask::Rest)
    {
        return 5;
    }
    return 0;
}

std::string WorldSociety::LifeLabel(int npc) const
{
    if (IsCourier(npc))
    {
        return std::string("소식 운반 · ") + CourierDestination(npc);
    }
    if (!HasLife() || npcs[npc].action == Action::Combat || npcs[npc].action == Action::Flee)
    {
        return ActionName(npcs[npc].action);
    }
    Point goal;
    if (CampaignGoal(npc, goal))
    {
        if (npcs[npc].health < 35 || npcs[npc].food < 1)
        {
            return "부상·보급 부족으로 집결지 후퇴";
        }
        for (const auto& campaign : campaigns)
        {
            if (campaign.active &&
                (npcs[npc].faction == campaign.attacker || npcs[npc].faction == campaign.defender))
            {
                static const char* phases[] = {"병력 집결", "전선 이동", "전선 교전", "거점 진격",
                                               "방어군 반격"};
                return std::string(phases[campaign.stage]) + " · " + regions[campaign.region].name;
            }
        }
    }
    const auto& l = lives[npc];
    static const char* names[] = {"다음 일 결정", "휴식",      "식사", "작업",     "물자 수령",
                                  "운송",         "약속 이행", "순찰", "기록 조사"};
    if (HasEconomy())
    {
        return std::string(GoalName(l.plan.goal)) + " · " + PlanStageName(l.plan.stage) +
               (l.partner >= 0 ? " · " + npcs[l.partner].name : "");
    }
    return std::string(names[static_cast<int>(l.task)]) +
           (l.partner >= 0 ? " · " + npcs[l.partner].name
            : l.place >= 0 ? " · " + places[l.place].name
                           : "");
}

std::vector<std::string> WorldSociety::LifeJournal() const
{
    std::vector<std::string> lines{"주민의 현재 계획 · 현장 행동과 같은 상태를 표시합니다."};
    for (std::size_t i = 0; i < lives.size(); ++i)
    {
        const auto& n = npcs[i];
        const auto& l = lives[i];
        lines.push_back(n.name + " · " + (n.Alive() ? LifeLabel(int(i)) : "사망"));
        lines.push_back("식량 " + std::to_string(int(n.food)) + " · 포만감 " +
                        std::to_string(int(l.satiation)) + " · 피로 " +
                        std::to_string(int(l.fatigue)) + " · 약품 " +
                        std::to_string(int(l.medicine)) + " · " + l.reason);
        if (HasEconomy() && !l.plan.blocked.empty())
        {
            lines.push_back("중단/대기: " + l.plan.blocked);
        }
        if (l.agreement)
        {
            lines.push_back("진행 중인 약속 #" + std::to_string(l.agreement) + " · 상대 " +
                            npcs[l.partner].name);
        }
        if (!l.lastMemory.empty())
        {
            lines.push_back("기억: " + l.lastMemory);
        }
    }
    return lines;
}
