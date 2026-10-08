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
    if (n.role == "merchant" && n.food >= 3 && minute >= l.nextTrade && l.fatigue < 60)
    {
        int source = -1, destination = -1;
        float best = -1e9f;
        for (const auto& route : tradeRoutes)
        {
            const auto& from = places[route.first];
            const auto& to = places[route.second];
            if (from.region != n.region)
            {
                continue;
            }
            int owner = regions[to.region].owner;
            if (owner >= 0 && diplomacy[n.faction][owner] < rules.hostility)
            {
                continue;
            }
            const auto* reserve = world.FindRegion(regions[from.region].id);
            if (!reserve || reserve->food < .08)
            {
                continue;
            }
            float demand = .35f, risk = .25f;
            for (const auto& memory : n.memories)
            {
                if (memory.region == to.region && minute <= memory.learned + 720)
                {
                    demand = 1 - memory.food;
                    risk = memory.infection;
                }
            }
            float score = demand + n.traits[1] * .3f + n.traits[5] * .2f -
                          risk * (1 - n.traits[0]) -
                          std::hypot(to.position.x - n.x, to.position.y - n.y) * .002f;
            if (score > best)
            {
                best = score;
                source = route.first;
                destination = route.second;
            }
        }
        l.nextTrade = minute + 360;
        if (source >= 0 &&
            plan(LifeTask::Collect, source, "알고 있는 교역로와 수요 보고를 근거로 시장 방문"))
        {
            l.delivery = destination;
            l.reason = places[destination].name + "에서 식량을 거래할 계획";
            return;
        }
    }
    if (n.role == "merchant" && n.food >= 3 && minute >= l.nextSocial)
    {
        // Merchants visit the market and look for actual customers there.
        if (plan(LifeTask::Work, LifeSite(index, "market"), "시장에 나가 교역 상대와 소식을 찾음"))
        {
            return;
        }
    }
    std::array<float, 10> motivation = rules.weights;
    StoryInfluence(index, motivation);
    motivation[3] += n.traits[2] + (n.goal == "aid" || n.goal == "connect" ? 1.f : 0.f);
    motivation[4] += n.traits[0] + (n.role == "guard" ? 2.f : 0.f);
    motivation[5] += n.traits[5] + (n.goal == "research" ? 2.f : 0.f);
    const std::pair<const char*, float> candidates[] = {
        {"farm", 1.f + (n.goal == "survive" ? 1.f : 0.f)},
        {"clinic", motivation[3] + (n.role == "healer" ? 2.f : -1.f)},
        {"watch", motivation[4]},
        {"archive", motivation[5]}};
    int preferred = l.work;
    float preference = 1.5f;
    for (const auto& candidate : candidates)
    {
        if (candidate.second <= preference)
        {
            continue;
        }
        const int knownPlace = LifeSite(index, candidate.first);
        if (knownPlace >= 0)
        {
            preferred = knownPlace;
            preference = candidate.second;
        }
    }
    int site = preferred;
    int count = 0;
    for (const auto& other : lives)
    {
        if (other.place == site && other.task != LifeTask::None)
        {
            ++count;
        }
    }
    if (site >= 0 && count >= places[site].capacity)
    {
        site = LifeSite(index, places[site].kind);
    }
    if (site >= 0)
    {
        const bool research = places[site].kind == "archive";
        if (plan(research            ? LifeTask::Research
                 : n.role == "guard" ? LifeTask::Patrol
                                     : LifeTask::Work,
                 site, research ? "기록과 현장 정보를 대조" : "직책과 생활 필요에 맞춰 작업"))
        {
            return;
        }
    }
    plan(LifeTask::Rest, l.home, "빈 작업 자리가 생길 때까지 휴식");
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
                const float price = n.traits[2] >= .7f ? 0.f : l.cargo * .15f;
                if (factions[receiver.faction].supplies < price)
                {
                    l.partner = -1;
                    return true;
                }
                factions[receiver.faction].supplies -= price;
                factions[n.faction].supplies += price;
                receiver.food += l.cargo;
                receiver.yaw = std::atan2(receiver.x - n.x, receiver.y - n.y);
                Relate(l.partner, index, 5, -1, "직접 운송한 식량을 받음", l.cause);
                Record("life_delivery_received", n.id, receiver.id,
                       "cargo=" + std::to_string(l.cargo), "transferred",
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
