#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "RandomLevelMap.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

void WorldSociety::OfferLife(int requester, int supplier, int kind)
{
    if (conversationNpc == requester || conversationNpc == supplier || agreements.size() >= 64 ||
        lives[requester].agreement || lives[supplier].agreement || lives[requester].cargo > 0 ||
        lives[supplier].cargo > 0 || !npcs[requester].Alive() || !npcs[supplier].Alive() ||
        npcs[requester].action == Action::Combat || npcs[supplier].action == Action::Combat ||
        npcs[requester].action == Action::Flee || npcs[supplier].action == Action::Flee ||
        npcs[requester].action == Action::Escort || npcs[supplier].action == Action::Escort ||
        npcs[requester].wildTarget >= 0 || npcs[supplier].wildTarget >= 0 ||
        npcs[requester].playerThreat || npcs[supplier].playerThreat)
    {
        return;
    }
    Agreement a;
    a.requester = requester;
    a.supplier = supplier;
    a.kind = kind;
    a.id = Record("agreement_proposed", npcs[requester].id, npcs[supplier].id, "none",
                  kind == 0   ? "food"
                  : kind == 1 ? "care"
                              : "information",
                  "욕구와 알고 있는 상대를 근거로 요청");
    a.due = minute + 6;
    a.deadline = minute + 180;
    agreements.push_back(a);
    for (int index : {requester, supplier})
    {
        auto& l = lives[index];
        // A carried delivery must finish before accepting a second obligation.
        l.agreement = a.id;
        l.task = LifeTask::Meet;
        l.partner = index == requester ? supplier : requester;
        l.progress = 0;
        l.place = -1;
        l.until = a.deadline;
        l.reason = "상대의 응답과 약속 이행을 기다림";
        npcs[index].action = Action::Rest;
    }
    SayLife(requester,
            kind == 0   ? "request_food"
            : kind == 1 ? "request_care"
                        : "request_news",
            supplier);
}

void WorldSociety::EndAgreement(Agreement& a, bool success, const std::string& reason)
{
    if (a.stage >= 3)
    {
        return;
    }
    if (!success)
    {
        if (npcs[a.supplier].Alive())
        {
            if (a.kind == 0)
            {
                npcs[a.supplier].food += a.cargo;
            }
            if (a.kind == 1)
            {
                lives[a.supplier].medicine += a.cargo;
            }
        }
        if (a.payer >= 0)
        {
            factions[a.payer].supplies += a.payment;
        }
        Relate(a.requester, a.supplier, -3, 1, reason, a.id);
        SayLife(a.requester, "cancel", a.supplier, -1, reason);
    }
    a.cargo = 0;
    a.payment = 0;
    a.stage = success ? 3 : 4;
    Record(success ? "agreement_fulfilled" : "agreement_cancelled", npcs[a.supplier].id,
           npcs[a.requester].id, "pending", success ? "fulfilled" : "cancelled", reason, a.id);
    for (int index : {a.requester, a.supplier})
    {
        auto& l = lives[index];
        l.lastMemory = npcs[index == a.requester ? a.supplier : a.requester].name + " · " + reason;
        if (success)
        {
            ++l.helped;
            l.social = std::max(0.f, l.social - 45);
        }
        else
        {
            ++l.broken;
        }
        if (l.agreement == a.id)
        {
            l.agreement = 0;
            l.task = LifeTask::None;
            l.partner = -1;
            l.progress = 0;
            l.nextSocial = minute + lifeSocialMinutes;
            npcs[index].nextDecision = minute + rules.decisionPeriod;
        }
    }
}

void WorldSociety::ChooseLife(int index, WorldSimulation& world, const RandomLevelMap& map)
{
    auto& n = npcs[index];
    auto& l = lives[index];
    if (conversationNpc == index || l.task != LifeTask::None || l.agreement)
    {
        return;
    }
    l.decisionTrace = "생존·회복·약속 우선 조건 확인";
    l.choices.clear();
    auto plan = [&](LifeTask task, int place, const std::string& reason)
    {
        if (place < 0 || LifeSlot(index, place) < 0)
        {
            return false;
        }
        l.task = task;
        l.place = place;
        l.slot = LifeSlot(index, place);
        l.partner = -1;
        l.delivery = -1;
        l.progress = 0;
        l.lastDistance = 1e6f;
        l.stalled = 0;
        l.until = minute + 600;
        l.reason = reason;
        l.cause =
            Record("life_plan", n.id, places[place].id, "idle", std::to_string(int(task)), reason);
        n.action = task == LifeTask::Research ? Action::Research : Action::Travel;
        n.destination = places[place].region;
        return true;
    };
    if (l.satiation < 35 && n.food >= 1 &&
        plan(LifeTask::Eat, LifeSite(index, "market"), "먹을 것을 구했으므로 식사"))
    {
        return;
    }
    if (l.fatigue > 75 || (minute % 1440 > 1320 && l.fatigue > 35))
    {
        if (plan(LifeTask::Rest, l.home, "피로를 해소한 뒤 일을 계속하기로 함"))
        {
            return;
        }
    }
    if (minute >= l.nextSocial && (n.food < 2 || n.health < 75 || l.social > 45))
    {
        const int kind = n.health < 75 ? 1 : n.food < 2 ? 0 : 2;
        int best = -1;
        float value = -1e9f;
        for (int other : Nearby(n))
        {
            const auto& b = npcs[other];
            const auto& bl = lives[other];
            if (bl.agreement || bl.cargo > 0 || b.action == Action::Combat ||
                b.action == Action::Flee || b.action == Action::Escort || b.wildTarget >= 0 ||
                b.playerThreat || !map.Visible(n.x, n.y, b.x, b.y))
            {
                continue;
            }
            if (kind == 0 && b.food < 3)
            {
                continue;
            }
            if (kind == 1 && (b.role != "healer" || bl.medicine < 1))
            {
                continue;
            }
            if (kind == 2)
            {
                bool useful = false;
                for (const auto& report : b.memories)
                {
                    const auto known = std::find_if(n.memories.begin(), n.memories.end(),
                                                    [&](const Memory& memory)
                                                    {
                                                        return memory.region == report.region;
                                                    });
                    useful =
                        useful || known == n.memories.end() || report.learned > known->learned + 30;
                }
                if (other < int(storyBeliefs.size()) && index < int(storyBeliefs.size()))
                {
                    for (const auto& belief : storyBeliefs[other])
                    {
                        const auto known = std::find_if(
                            storyBeliefs[index].begin(), storyBeliefs[index].end(),
                            [&](const Belief& own)
                            {
                                return own.fact == belief.fact && own.claim == belief.claim;
                            });
                        useful = useful || known == storyBeliefs[index].end();
                    }
                }
                if (!useful)
                {
                    continue;
                }
            }
            float score = PersonalRegard(index, other) + b.traits[2] * 20 -
                          std::hypot(n.x - b.x, n.y - b.y) * 2;
            if (score > value)
            {
                best = other;
                value = score;
            }
        }
        if (best >= 0 && value > -35)
        {
            OfferLife(index, best, kind);
            return;
        }
        l.nextSocial = minute + 20;
    }
    if (n.food < 3)
    {
        int site = LifeSite(index, "warehouse");
        if (site >= 0)
        {
            const auto* stock = world.FindRegion(regions[places[site].region].id);
            if (stock && stock->food >= .015 &&
                plan(LifeTask::Collect, site, "배급소에서 실제 비축 식량을 수령"))
            {
                return;
            }
        }
        if (plan(LifeTask::Work, LifeSite(index, "farm"), "비축이 부족해 식량 생산에 참여"))
        {
            return;
        }
    }
    if (n.food >= 3 && l.fatigue > 45 && plan(LifeTask::Rest, l.home, "일을 마치고 휴식"))
    {
        return;
    }

    struct Candidate
    {
        LifeTask task;
        int place;
        int destination;
        float score;
        std::string reason;
    };

    std::vector<Candidate> options;
    std::array<float, 10> motivation = rules.weights;
    StoryInfluence(index, motivation);
    auto consider =
        [&](LifeTask task, int place, float value, const std::string& reason, int destination = -1)
    {
        if (place < 0 || LifeSlot(index, place) < 0)
        {
            return;
        }
        const auto& site = places[place];
        const float travel =
            std::hypot(site.position.x - n.x, site.position.y - n.y) / std::max(.1f, n.speed);
        float knownRisk = 0;
        for (const auto& memory : n.memories)
        {
            if (memory.region == site.region && minute <= memory.learned + 720)
            {
                knownRisk = std::max(knownRisk, memory.infection * memory.confidence);
            }
        }
        const int owner = regions[site.region].owner;
        if (owner >= 0 && diplomacy[n.faction][owner] < rules.hostility)
        {
            return;
        }
        value -= travel * .045f + knownRisk * (1 - n.traits[0]) * 2;
        // Recent exhaustion and broken promises change the cost of another expedition.
        if (destination >= 0)
        {
            value -= l.fatigue * .025f + std::min(3.f, l.broken * .12f);
        }
        options.push_back({task, place, destination, value, reason});
    };
    consider(LifeTask::Rest, l.home, l.fatigue * .055f + (100 - n.health) * .015f,
             "피로와 부상 회복의 이득을 다른 일과 비교");
    if (n.food >= 1)
    {
        consider(LifeTask::Eat, LifeSite(index, "market"), (100 - l.satiation) * .07f,
                 "허기 해소와 남은 이동 비용을 비교");
    }
    const int warehouse = LifeSite(index, "warehouse");
    if (warehouse >= 0)
    {
        const auto* stock = world.FindRegion(regions[places[warehouse].region].id);
        if (stock && stock->food >= .015)
        {
            consider(LifeTask::Collect, warehouse, std::max(0.f, 5 - n.food) * .8f,
                     "개인 식량 부족을 가까운 실제 재고로 보충");
        }
    }
    const char* kinds[] = {"farm", "clinic", "watch", "archive", "market"};
    for (const char* kind : kinds)
    {
        const int place = LifeSite(index, kind);
        if (place < 0)
        {
            continue;
        }
        float need = 0;
        // Only local observations and already received reports inform demand.
        if (places[place].region == n.region)
        {
            const auto* state = world.FindRegion(regions[n.region].id);
            need = state ? 1 - float(state->food) : 0;
        }
        else
        {
            for (const auto& memory : n.memories)
            {
                if (memory.region == places[place].region && minute <= memory.learned + 720)
                {
                    need = (1 - memory.food) * memory.confidence;
                }
            }
        }
        const std::string type = kind;
        float value = .6f;
        LifeTask task = LifeTask::Work;
        if (type == "farm")
        {
            value += need * 3 + std::max(0.f, 4 - n.food) * .4f + n.traits[2];
        }
        else if (type == "clinic")
        {
            float injury = 0;
            for (int other : Nearby(n))
            {
                if (npcs[other].Alive() && npcs[other].health < 75)
                {
                    injury += (100 - npcs[other].health) / 100;
                }
            }
            value += motivation[3] + n.traits[2] * injury * 2 + (n.role == "healer" ? 1.3f : -.8f);
        }
        else if (type == "watch")
        {
            value += motivation[4] + n.traits[0] + (n.role == "guard" ? 1.4f : 0);
            task = LifeTask::Patrol;
        }
        else if (type == "archive")
        {
            value += motivation[5] + n.traits[5] + (n.role == "scholar" ? 1.4f : 0);
            task = LifeTask::Research;
        }
        else
        {
            value += motivation[2] + l.social * .025f + (n.role == "merchant" ? .9f : -.8f);
        }
        value -= l.fatigue * .025f;
        consider(task, place, value, type + " : 알고 있는 수요·직책·성향·피로 비교");
    }
    if (n.role == "merchant" && n.food >= 3 && minute >= l.nextTrade && l.fatigue < 70)
    {
        for (const auto& route : tradeRoutes)
        {
            const auto& from = places[route.first];
            const auto& to = places[route.second];
            bool sourceKnown = from.region == n.region || from.region == n.home;
            float demand = -1, risk = 0, confidence = 0;
            for (const auto& memory : n.memories)
            {
                sourceKnown = sourceKnown || memory.region == from.region;
                if (memory.region == to.region && minute <= memory.learned + 720)
                {
                    demand = 1 - memory.food;
                    risk = memory.infection;
                    confidence = memory.confidence;
                }
            }
            const auto* reserve = world.FindRegion(regions[from.region].id);
            const int owner = regions[to.region].owner;
            if (!sourceKnown || demand < 0 || !reserve || reserve->food < .08 ||
                LifeSlot(index, route.second) < 0 ||
                (owner >= 0 && diplomacy[n.faction][owner] < rules.hostility))
            {
                continue;
            }
            const float travel =
                std::hypot(to.position.x - from.position.x, to.position.y - from.position.y) /
                std::max(.1f, n.speed);
            const float value = motivation[2] + demand * confidence * (3 + n.traits[1] * 2) +
                                n.traits[2] * demand - risk * (1 - n.traits[0]) * 3 -
                                travel * .035f;
            consider(LifeTask::Collect, route.first, value,
                     to.name + " : 전달받은 수요·정보 신뢰·위험·운송 비용 비교", route.second);
        }
    }
    std::stable_sort(options.begin(), options.end(),
                     [](const Candidate& a, const Candidate& b)
                     {
                         return a.score > b.score;
                     });
    l.decisionTrace.clear();
    for (const auto& option : options)
    {
        l.choices.push_back(
            {int(option.task), option.place, option.destination, option.score, option.reason});
        l.decisionTrace += places[option.place].id + "=" + std::to_string(option.score) + " (" +
                           option.reason + "); ";
    }
    for (const auto& option : options)
    {
        if (plan(option.task, option.place, option.reason))
        {
            l.delivery = option.destination;
            if (option.destination >= 0)
            {
                l.nextTrade = minute + 90;
            }
            return;
        }
    }
    plan(LifeTask::Rest, l.home, "실행 가능한 선택이 없어 휴식");
}

void WorldSociety::LifeTick(std::uint64_t elapsed, WorldSimulation& world,
                            const RandomLevelMap& map)
{
    PROFILE_SCOPE(NpcLife);
    if (!HasLife())
    {
        return;
    }
    const float minutes = float(std::min<std::uint64_t>(elapsed, 30));
    auto threatened = [&](int i)
    {
        return !npcs[i].Alive() || npcs[i].wildTarget >= 0 || npcs[i].playerThreat ||
               npcs[i].action == Action::Combat || npcs[i].action == Action::Flee ||
               npcs[i].action == Action::Escort;
    };
    for (auto& a : agreements)
    {
        if (a.stage >= 3)
        {
            continue;
        }
        auto& receiver = npcs[a.requester];
        auto& supplier = npcs[a.supplier];
        if (threatened(a.requester) || threatened(a.supplier) || minute >= a.deadline ||
            CampaignOrder(a.requester) || CampaignOrder(a.supplier))
        {
            EndAgreement(a, false, "위협·사망·기한 경과로 약속 중단");
            continue;
        }
        if (minute < a.due)
        {
            continue;
        }
        const float trust = PersonalRegard(a.supplier, a.requester);
        if (a.stage == 0)
        {
            bool consent =
                trust > -25 && diplomacy[supplier.faction][receiver.faction] > rules.hostility;
            // Personal medical trust can cross faction hostility.
            if (a.kind == 1 && trust > 15 && supplier.traits[2] > .6f)
            {
                consent = true;
            }
            consent = consent && (a.kind != 0 || supplier.food >= 3) &&
                      (a.kind != 1 || lives[a.supplier].medicine >= 1);
            if (!consent)
            {
                SayLife(a.supplier, "refuse", a.requester, -1, "안전과 비축을 먼저 지켜야 합니다");
                EndAgreement(a, false, "요청을 거절함");
                continue;
            }
            a.payment =
                a.kind == 0 && supplier.traits[2] < .75f ? .2f + supplier.traits[1] * .3f : 0;
            a.stage = 1;
            a.due = minute + 6;
            SayLife(a.supplier, a.payment > 0 ? "counter" : "accept", a.requester, -1,
                    a.payment > 0 ? "공동 보급 " + std::to_string(a.payment).substr(0, 4)
                                  : "대가 없이 돕겠습니다");
        }
        else if (a.stage == 1)
        {
            if (factions[receiver.faction].supplies < a.payment ||
                (a.kind == 0 && supplier.food < 3) ||
                (a.kind == 1 && lives[a.supplier].medicine < 1))
            {
                EndAgreement(a, false, "합의할 물자나 지불 여력이 부족함");
                continue;
            }
            a.payer = receiver.faction;
            a.payee = supplier.faction;
            factions[a.payer].supplies -= a.payment;
            a.cargo = a.kind == 2 ? 0 : 1;
            if (a.kind == 0)
            {
                supplier.food -= 1;
            }
            if (a.kind == 1)
            {
                lives[a.supplier].medicine -= 1;
            }
            a.stage = 2;
            a.due = minute + 8;
            SayLife(a.requester, "accept", a.supplier);
            Record("agreement_reserved", receiver.id, supplier.id, "available",
                   "cargo=" + std::to_string(a.cargo) + " payment=" + std::to_string(a.payment),
                   "양측 수락 후 실제 자원을 예약", a.id);
        }
        else if (a.stage == 2 && std::hypot(receiver.x - supplier.x, receiver.y - supplier.y) < 2 &&
                 map.Visible(receiver.x, receiver.y, supplier.x, supplier.y))
        {
            if (a.kind == 0)
            {
                receiver.food += a.cargo;
            }
            if (a.kind == 1)
            {
                receiver.health = std::min(100.f, receiver.health + 30);
            }
            factions[a.payee].supplies += a.payment;
            Relate(a.requester, a.supplier, 8, -2, "합의한 도움을 실제로 받음", a.id);
            Relate(a.supplier, a.requester, 4, 0, "약속을 이행함", a.id);
            // Exchange known regional observations as well as authored narrative evidence.
            for (int sender : {a.supplier, a.requester})
            {
                const int recipient = sender == a.supplier ? a.requester : a.supplier;
                for (const auto& memory : npcs[sender].memories)
                {
                    if (minute <= memory.learned + 720)
                    {
                        Send(sender, recipient, 0, memory.region, world, a.id);
                    }
                }
                if (sender < static_cast<int>(storyBeliefs.size()))
                {
                    for (const auto& b : storyBeliefs[sender])
                    {
                        SendStory(sender, recipient, b.fact);
                    }
                }
            }
            std::string detail = a.kind == 0   ? "식량 1인분"
                                 : a.kind == 1 ? "약품을 사용한 진료"
                                               : "서로 알고 있는 소식";
            if (a.kind == 2 && !supplier.memories.empty())
            {
                const auto& memory = supplier.memories.back();
                detail = regions[memory.region].name + "에서 확인한 식량 상황 " +
                         std::to_string(int(memory.food * 100)) + "% · 감염 " +
                         std::to_string(int(memory.infection * 100)) + "%";
            }
            if (a.kind == 2 && a.supplier < static_cast<int>(storyBeliefs.size()) &&
                !storyBeliefs[a.supplier].empty())
            {
                const auto& b = storyBeliefs[a.supplier].front();
                detail = (b.claim > 0 ? storyFacts[b.fact].text : storyFacts[b.fact].correction);
            }
            SayLife(a.supplier, a.kind == 2 ? "news" : "delivered", a.requester, -1, detail);
            SayLife(a.requester, "thanks", a.supplier, -1, detail);
            LifeEvent(a.supplier,
                      a.kind == 0   ? "supply"
                      : a.kind == 1 ? "care"
                                    : "report",
                      receiver.region, world, a.requester);
            EndAgreement(a, true, detail + " · 약속 이행");
        }
    }
    agreements.erase(std::remove_if(agreements.begin(), agreements.end(),
                                    [](const Agreement& a)
                                    {
                                        return a.stage >= 3;
                                    }),
                     agreements.end());
    for (std::size_t i = 0; i < npcs.size(); ++i)
    {
        auto& n = npcs[i];
        auto& l = lives[i];
        if (!n.Alive())
        {
            if (l.cargo > 0)
            {
                Record("life_cargo_lost", n.id, "ground", std::to_string(l.cargo), "0",
                       "운송자 사망", l.cause);
                l.cargo = 0;
            }
            l.task = LifeTask::None;
            continue;
        }
        l.social = std::min(100.f, l.social + minutes * .10f);
        l.satiation = std::max(0.f, l.satiation - lifeFoodPerDay * minutes * 50 / 1440);
        Point campaignGoal;
        if (l.satiation < 35 && n.food >= 1 && CampaignGoal(int(i), campaignGoal))
        {
            n.food -= 1;
            l.satiation = std::min(100.f, l.satiation + 55);
            Record("campaign_ration", n.id, factions[n.faction].id, "food=1", "consumed",
                   "출정 중 소지 식량으로 허기 해소");
        }
        if (l.satiation == 0 && minute / 60 != (minute - elapsed) / 60)
        {
            Damage(-1, int(i), 2.f, world, "굶주림");
            if (!n.Alive())
            {
                continue;
            }
        }
        if (l.task != LifeTask::Rest)
        {
            l.fatigue = std::min(100.f, l.fatigue + minutes * .05f);
        }
        if (threatened(int(i)) || CampaignOrder(int(i)))
        {
            if (!l.agreement && l.cargo == 0 && l.task != LifeTask::None)
            {
                l.task = LifeTask::None;
                l.progress = 0;
                l.place = -1;
                l.slot = -1;
                l.reason = "위협 또는 집단 명령으로 생활 계획 중단";
            }
            continue;
        }
        if (!l.agreement && l.cargo == 0 && l.task != LifeTask::None && l.task != LifeTask::Rest &&
            l.task != LifeTask::Eat && (l.fatigue > 85 || l.satiation < 20 || n.health < 40))
        {
            Record("life_plan_reconsidered", n.id, l.place >= 0 ? places[l.place].id : "none",
                   "active", "urgent_need", "몸 상태가 달라져 회복과 생존을 다시 평가", l.cause);
            l.task = LifeTask::None;
            l.progress = 0;
            l.place = -1;
            l.slot = -1;
        }
        if (l.task != LifeTask::None && minute >= l.until && !l.agreement)
        {
            Record("life_plan_cancelled", n.id, l.place >= 0 ? places[l.place].id : "none",
                   "active", "expired", "작업 기한 경과", l.cause);
            if (l.cargo > 0)
            {
                world.ChangeRegion(regions[n.region].id, l.cargo * .01, 0, n.id,
                                   "중단한 운송 물자 반환", l.cause);
                l.cargo = 0;
            }
            l.task = LifeTask::None;
        }
        ChooseLife(int(i), world, map);
    }
}

bool WorldSociety::TickLife(int index, float dt, WorldSimulation& world, const RandomLevelMap& map)
{
    if (!HasLife())
    {
        return false;
    }
    auto& n = npcs[index];
    auto& l = lives[index];
    if (l.task == LifeTask::None)
    {
        return true;
    }
    Point goal{n.x, n.y};
    float stop = .3f;
    if (l.task == LifeTask::Meet && l.partner >= 0)
    {
        goal = {npcs[l.partner].x, npcs[l.partner].y};
        stop = 1.55f;
    }
    else if (l.task == LifeTask::Deliver && l.partner >= 0 && npcs[l.partner].Alive())
    {
        goal = {npcs[l.partner].x, npcs[l.partner].y};
        stop = 1.5f;
    }
    else if (l.place >= 0)
    {
        goal = places[l.place].position;
        if (l.slot >= 0 && l.slot < int(places[l.place].slots.size()))
        {
            goal = places[l.place].slots[l.slot];
        }
    }
    const float distance = std::hypot(n.x - goal.x, n.y - goal.y);
    if (distance > stop)
    {
        const Point before{n.x, n.y};
        MoveTo(n, goal, dt, map);
        if (std::hypot(n.x - before.x, n.y - before.y) > .001f)
        {
            l.stalled = 0;
            l.lastDistance = distance;
        }
        else
        {
            l.stalled += dt;
        }
        if (l.stalled > 25 && !l.agreement && l.until > minute)
        {
            Record("life_path_blocked", n.id, l.place >= 0 ? places[l.place].id : "none", "moving",
                   "retry", "접근 불가로 계획 재검토", l.cause);
            l.until = minute;
        }
        return true;
    }
    if (l.task == LifeTask::Meet)
    {
        n.yaw = std::atan2(n.x - goal.x, n.y - goal.y);
        return true;
    }
    if (l.task == LifeTask::Deliver && l.partner >= 0)
    {
        n.yaw = std::atan2(n.x - goal.x, n.y - goal.y);
    }
    else if (l.place >= 0)
    {
        const auto center = places[l.place].position;
        n.yaw = std::atan2(n.x - center.x, n.y - center.y);
    }
    l.progress += dt;
    if (l.progress <= dt + .001f)
    {
        SayLife(index,
                l.task == LifeTask::Rest       ? "rest"
                : l.task == LifeTask::Collect  ? "collect"
                : l.task == LifeTask::Research ? "research"
                                               : "work",
                -1, l.place);
    }
    if (l.task == LifeTask::Rest)
    {
        l.fatigue = std::max(0.f, l.fatigue - dt * 3);
        n.health = std::min(100.f, n.health + dt * .12f);
        if (l.progress < lifeRestSeconds)
        {
            return true;
        }
    }
    else if (l.task == LifeTask::Eat)
    {
        if (l.progress < 4)
        {
            return true;
        }
        if (n.food >= 1)
        {
            n.food -= 1;
            l.satiation = std::min(100.f, l.satiation + 55);
            Record("life_meal", n.id, places[l.place].id, "food=1", "consumed",
                   "식사로 허기를 해소", l.cause);
        }
    }
    else if (l.task == LifeTask::Collect)
    {
        if (l.progress < 3)
        {
            return true;
        }
        const auto* stock = world.FindRegion(regions[places[l.place].region].id);
        float units = stock ? std::min(3.f, float(stock->food * 100)) : 0;
        const int destination = l.delivery < 0 ? l.home : l.delivery;
        const int slot = LifeSlot(index, destination);
        if (slot < 0)
        {
            return true;
        }
        if (units > .5f)
        {
            world.ChangeRegion(regions[places[l.place].region].id, -units * .01, 0, n.id,
                               "창고에서 운반할 식량 수령", l.cause);
            l.cargo = units;
            if (l.delivery < 0)
            {
                l.delivery = l.home;
            }
            l.place = l.delivery;
            l.slot = slot;
            l.task = LifeTask::Deliver;
            l.progress = 0;
            l.lastDistance = 1e6f;
            l.until = minute + 600;
            l.reason = places[l.place].kind == "market" ? "식량을 싣고 교역 상대를 찾아 이동"
                                                        : "받은 배급을 거처로 운반";
            return true;
        }
        l.reason = "창고가 비어 다른 공급 방법을 찾음";
    }
    else if (l.task == LifeTask::Deliver)
    {
        if (l.progress < 3)
        {
            return true;
        }
        auto quote = [&](int receiver)
        {
            const float scarcity = std::max(0.f, 6 - npcs[receiver].food) / 6;
            const float trustDiscount =
                std::max(-.04f, std::min(.04f, PersonalRegard(index, receiver) * .0005f));
            return n.traits[2] >= .7f ? 0.f
                                      : std::max(.05f, .10f + n.traits[1] * .12f + scarcity * .08f -
                                                           trustDiscount);
        };
        if (places[l.place].kind == "market")
        {
            if (l.partner >= 0 &&
                (!npcs[l.partner].Alive() || lives[l.partner].agreement ||
                 npcs[l.partner].wildTarget >= 0 || npcs[l.partner].action == Action::Combat ||
                 npcs[l.partner].action == Action::Flee ||
                 diplomacy[n.faction][npcs[l.partner].faction] < rules.hostility))
            {
                l.partner = -1;
            }
            if (l.partner < 0)
            {
                float best = -1e9f;
                for (int other : Nearby(n))
                {
                    const auto& receiver = npcs[other];
                    if (lives[other].agreement || other == conversationNpc || receiver.food >= 6 ||
                        receiver.action == Action::Combat || receiver.action == Action::Flee ||
                        receiver.wildTarget >= 0 ||
                        diplomacy[n.faction][receiver.faction] < rules.hostility)
                    {
                        continue;
                    }
                    const float amount = std::min(l.cargo, std::max(0.f, 6 - receiver.food));
                    const float price = quote(other) * amount;
                    if (factions[receiver.faction].supplies < price)
                    {
                        continue;
                    }
                    const float score = (6 - receiver.food) * 4 +
                                        (n.faction != receiver.faction ? 5.f : 0.f) +
                                        PersonalRegard(index, other) * .1f -
                                        std::hypot(n.x - receiver.x, n.y - receiver.y);
                    if (score > best)
                    {
                        best = score;
                        l.partner = other;
                    }
                }
                if (l.partner < 0)
                {
                    // Deposit unsold cargo into the destination stock, never duplicate it.
                    world.ChangeRegion(regions[places[l.place].region].id, l.cargo * .01, 0, n.id,
                                       "시장 창고에 운송 물자 인계", l.cause);
                }
                else
                {
                    l.progress = 0;
                    l.reason = npcs[l.partner].name + "에게 물자를 직접 전달";
                    return true;
                }
            }
            if (l.partner >= 0)
            {
                auto& receiver = npcs[l.partner];
                if (std::hypot(n.x - receiver.x, n.y - receiver.y) > 1.6f ||
                    !map.Visible(n.x, n.y, receiver.x, receiver.y))
                {
                    return true;
                }
                const float amount = std::min(l.cargo, std::max(0.f, 6 - receiver.food));
                const float price = quote(l.partner) * amount;
                if (factions[receiver.faction].supplies < price)
                {
                    l.partner = -1;
                    return true;
                }
                factions[receiver.faction].supplies -= price;
                factions[n.faction].supplies += price;
                receiver.food += amount;
                const float remainder = l.cargo - amount;
                if (remainder > 0)
                {
                    world.ChangeRegion(regions[places[l.place].region].id, remainder * .01, 0, n.id,
                                       "판매 후 남은 화물을 시장 창고에 보관", l.cause);
                }
                receiver.yaw = std::atan2(receiver.x - n.x, receiver.y - n.y);
                Relate(l.partner, index, 5, -1, "직접 운송한 식량을 받음", l.cause);
                Record("life_delivery_received", n.id, receiver.id,
                       "cargo=" + std::to_string(l.cargo),
                       "food=" + std::to_string(amount) + "; payment=" + std::to_string(price),
                       "거리와 시야 확인 후 물자·공동 보급 교환", l.cause);
            }
        }
        else
        {
            n.food += l.cargo;
        }
        Record("life_delivery", n.id, places[l.place].id, std::to_string(l.cargo), "0",
               "운송 물자를 수신자 또는 목적지 창고에 인계", l.cause);
        l.partner = -1;
        l.cargo = 0;
        SayLife(index, "delivered", -1, l.place, "운반한 식량");
        l.social = std::max(l.social, 70.f);
        l.nextSocial = minute;
        if (places[l.place].kind == "market")
        {
            l.work = l.place;
        }
        LifeEvent(index, "supply", n.region, world);
    }
    else
    {
        l.fatigue = std::min(100.f, l.fatigue + dt * .5f);
        if (l.progress < lifeWorkSeconds)
        {
            return true;
        }
        auto& p = places[l.place];
        if (p.kind == "farm")
        {
            world.ChangeRegion(regions[p.region].id, .012, 0, n.id, "작업장에서 식량을 생산",
                               l.cause);
            Record("life_production", n.id, p.id, "labor", "food=1.2",
                   "실제 작업 시간과 피로를 소비", l.cause);
        }
        else if (p.kind == "clinic")
        {
            if (p.medicine >= 1 && l.medicine < 3)
            {
                p.medicine -= 1;
                l.medicine += 1;
            }
            else if (n.food >= 1 && p.medicine < 8)
            {
                n.food -= 1;
                p.medicine += 1;
                Record("life_medicine_prepared", n.id, p.id, "food=1", "medicine=1",
                       "진료소 조제 작업", l.cause);
            }
            l.social = std::max(l.social, 65.f);
        }
        else if (p.kind == "archive")
        {
            Observe(index, world);
            for (std::size_t f = 0; f < storyFacts.size(); ++f)
            {
                if (storyFacts[f].region == p.region && storyFacts[f].observation == "site")
                {
                    LearnStory(index, {int(f), index, -1, 1, .95f, minute}, index);
                }
            }
            l.social = std::max(l.social, 70.f);
            LifeEvent(index, "research", p.region, world);
        }
        else if (p.kind == "watch")
        {
            regions[p.region].security = std::min(1.f, regions[p.region].security + .01f);
            l.work = LifeSite(index, "market");
            if (l.work < 0)
            {
                l.work = l.place;
            }
        }
        else if (p.kind == "market")
        {
            l.social = std::max(l.social, 75.f);
            for (int other : Nearby(n))
            {
                if (lives[other].agreement || lives[other].cargo > 0 ||
                    !map.Visible(n.x, n.y, npcs[other].x, npcs[other].y))
                {
                    continue;
                }
                if (npcs[other].food < 2 && n.food >= 3)
                {
                    l.task = LifeTask::None;
                    OfferLife(other, index, 0);
                    return true;
                }
                if (npcs[other].health < 80 && n.role == "healer" && l.medicine >= 1)
                {
                    l.task = LifeTask::None;
                    OfferLife(other, index, 1);
                    return true;
                }
            }
        }
        if (n.role == "guard" && p.kind == "market")
        {
            int watch = LifeSite(index, "watch");
            if (watch >= 0)
            {
                l.work = watch;
            }
        }
    }
    l.task = LifeTask::None;
    l.progress = 0;
    n.nextDecision = minute;
    return true;
}

void WorldSociety::LifeEvent(int index, const std::string& kind, int region, WorldSimulation& world,
                             int other)
{
    for (auto& s : storyIncidents)
    {
        if (s.stage >= 2 || s.region != region)
        {
            continue;
        }
        if (s.stage == 0)
        {
            const auto* evidence = KnownStory(index, s.trigger);
            if (!evidence || evidence->claim < 0 || evidence->confidence < .5f)
            {
                continue;
            }
            s.stage = 1;
            s.actor = index;
            s.patient = evidence->subject;
            s.deadline = minute + s.lifetime;
            s.cause =
                Record("story_emerged", npcs[index].id, s.id, "world_problem", "observed", s.title);
        }
        if ((s.kind == "ration" && kind == "supply") || (s.kind == "signal" && kind == "care"))
        {
            s.stage = 2;
            s.outcome = npcs[index].name + "의 자율 행동으로 실제 지원이 이루어짐";
            if (other >= 0)
            {
                s.partner = other;
            }
            Record("story_outcome", npcs[index].id, s.id, "active", "relief", s.outcome, s.cause);
        }
        else if (s.kind == "ledger" && (kind == "research" || kind == "report"))
        {
            const auto* proof = KnownStory(index, s.proof);
            if (!proof || proof->claim < 0 || proof->confidence < .55f)
            {
                s.outcome = "기록을 조사했으나 임상 보고가 부족해 판단을 유보";
                continue;
            }
            for (int n : Nearby(npcs[index]))
            {
                if (npcs[n].role == "healer" && PersonalRegard(index, n) >= 0)
                {
                    SendStory(index, n, s.trigger);
                    SendStory(index, n, s.proof);
                }
            }
            s.stage = 2;
            s.outcome = "조사자가 실제로 얻은 원문과 임상 보고를 대조해 공유";
            Record("story_outcome", npcs[index].id, s.id, "active", "corroborated", s.outcome,
                   s.cause);
        }
    }
    (void)world;
}
