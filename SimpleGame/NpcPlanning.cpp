#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "RandomLevelMap.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

const char* WorldSociety::GoalName(LifeGoal goal)
{
    static const char* names[] = {"상황 관찰",          "식량 확보와 식사",  "휴식과 회복",
                                  "생산으로 생계 확보", "지역 간 물자 공급", "주변 부상자 치료",
                                  "거점 경계",          "새로운 정보 교환"};
    return names[int(goal)];
}

const char* WorldSociety::PlanStageName(PlanStage stage)
{
    static const char* names[] = {"대기",      "방법 선택",       "이동",
                                  "현장 실행", "상대/자리 대기",  "위협으로 일시 중단",
                                  "목표 달성", "방법 실패·재검토"};
    return names[int(stage)];
}

void WorldSociety::SetPlanStage(int index, PlanStage stage, const std::string& reason)
{
    auto& plan = lives[index].plan;
    plan.blocked =
        stage == PlanStage::Waiting || stage == PlanStage::Suspended || stage == PlanStage::Failed
            ? reason
            : "";
    if (plan.stage == stage)
    {
        return;
    }
    const auto before = plan.stage;
    plan.stage = stage;
    Profile::Add(Profile::Metric::NpcPlanTransitions);
    Record("npc_plan_stage", npcs[index].id, "plan", PlanStageName(before), PlanStageName(stage),
           reason, plan.id);
}

void WorldSociety::FinishGoal(int index, bool success, const std::string& reason)
{
    auto& life = lives[index];
    auto& plan = life.plan;
    plan.outcome = reason;
    plan.retryAt = minute + planningRules.retryMinutes;
    if (!success)
    {
        plan.avoidUntil = minute + planningRules.avoidMinutes;
        plan.avoidedPlace = life.place;
    }
    SetPlanStage(index, success ? PlanStage::Complete : PlanStage::Failed, reason);
}

bool WorldSociety::KnownWorkplace(int index, int site) const
{
    const auto& npc = npcs[index];
    const auto& place = places[site];
    const auto& plan = lives[index].plan;
    if (plan.avoidedPlace == site && minute < plan.avoidUntil)
    {
        return false;
    }
    const int owner = regions[place.region].owner;
    if (owner >= 0 && (diplomacy[npc.faction][owner] < rules.hostility ||
                       diplomacy[owner][npc.faction] < rules.hostility))
    {
        return false;
    }
    if (place.region == npc.region || place.region == npc.home)
    {
        return true;
    }
    return std::any_of(npc.memories.begin(), npc.memories.end(),
                       [&](const Memory& memory)
                       {
                           return memory.region == place.region &&
                                  minute <= memory.learned + rules.messageLifetime;
                       });
}

bool WorldSociety::StartPlanStep(int index, LifeTask task, int place, const std::string& reason)
{
    if (place < 0 || !KnownWorkplace(index, place))
    {
        return false;
    }
    const int slot = LifeSlot(index, place);
    if (slot < 0)
    {
        return false;
    }
    auto& life = lives[index];
    life.task = task;
    life.place = place;
    life.slot = slot;
    life.partner = -1;
    life.delivery = -1;
    life.progress = 0;
    life.stalled = 0;
    life.plan.waitingSeconds = 0;
    life.lastDistance = 1e6f;
    life.until = minute + planningRules.goalMinutes;
    life.cause = life.plan.id;
    life.reason = reason;
    npcs[index].action = Action::Travel;
    npcs[index].destination = places[place].region;
    SetPlanStage(index, PlanStage::Moving, reason);
    return true;
}

bool WorldSociety::ChooseGoalPlan(int index, WorldSimulation& world, const RandomLevelMap& map)
{
    if (!HasEconomy())
    {
        return false;
    }
    PROFILE_SCOPE(NpcPlanning);
    auto& npc = npcs[index];
    auto& life = lives[index];
    auto& plan = life.plan;
    auto& wallet = economyWorkers[index];
    if (std::any_of(agreements.begin(), agreements.end(),
                    [&](const Agreement& a)
                    {
                        return a.stage == 0 && a.supplier == index;
                    }))
    {
        return true;
    }
    if ((EconomyParticipantBusy(index) && life.satiation >= 20 && life.fatigue <= 85 &&
         npc.health >= 40) ||
        conversationNpc == index || life.task != LifeTask::None || life.agreement || wallet.order)
    {
        return true;
    }
    if (plan.stage != PlanStage::Failed && plan.stage != PlanStage::Complete &&
        plan.goal != LifeGoal::None)
    {
        const bool done =
            (plan.goal == LifeGoal::Survive && npc.food >= planningRules.stock &&
             life.satiation >= 55) ||
            (plan.goal == LifeGoal::Recover && life.fatigue <= 25 && npc.health >= 80) ||
            (plan.goal == LifeGoal::Earn && wallet.coins > plan.initialCoins + .01f) ||
            (plan.goal == LifeGoal::Patrol && plan.steps > 0);
        if (done)
        {
            FinishGoal(index, true, "자원·허기·회복 또는 경계 작업 결과로 목표 충족");
            return true;
        }
        if (minute >= plan.started + planningRules.goalMinutes)
        {
            FinishGoal(index, false, "목표 기한 경과: 다른 방법과 대상을 다시 평가");
            return true;
        }
    }
    const bool urgent =
        (npc.food < 1 || life.satiation < 20 || life.fatigue > 85 || npc.health < 40);
    if (minute < plan.retryAt)
    {
        return true;
    }
    auto distanceCost = [&](int place)
    {
        return std::hypot(npc.x - places[place].position.x, npc.y - places[place].position.y) *
               .025f;
    };
    int workplace = -1;
    float workValue = -1e9f;
    // Evaluate all known workplaces; the nearest unusable one must not hide viable work.
    for (std::size_t p = 0; p < places.size(); ++p)
    {
        if (!KnownWorkplace(index, int(p)) || LifeSlot(index, int(p)) < 0)
        {
            continue;
        }
        const float score = EconomyWorkScore(index, int(p), world) - distanceCost(int(p));
        if (score > workValue)
        {
            workplace = int(p);
            workValue = score;
        }
    }
    if (workValue < 0)
    {
        workplace = -1;
    }
    bool trade = false;
    for (const auto& order : economyOrders)
    {
        trade =
            trade || (order.stage == 0 && minute >= order.available && minute < order.deadline &&
                      (order.source == npc.region || order.source == npc.home));
    }
    const LifeGoal urgentGoal = (life.fatigue > 85 || npc.health < 40) && life.satiation >= 20
                                    ? LifeGoal::Recover
                                    : LifeGoal::Survive;
    if (urgent && plan.goal != urgentGoal)
    {
        plan.goal = LifeGoal::None;
    }
    if (plan.goal == LifeGoal::None || plan.stage == PlanStage::Complete ||
        plan.stage == PlanStage::Failed)
    {
        std::array<float, 8> values{{0, -100, -100, -100, -100, -100, -100, -100}};
        if (npc.food < planningRules.stock || life.satiation < planningRules.hunger)
        {
            values[1] = 8 + (planningRules.stock - std::min(npc.food, planningRules.stock)) * 2 +
                        std::max(0.f, planningRules.hunger - life.satiation) * .5f;
        }
        if (life.fatigue > planningRules.fatigue || npc.health < 70)
        {
            values[2] = life.fatigue * .14f + (100 - npc.health) * .1f;
        }
        if (urgent)
        {
            values[int(urgentGoal)] = 100;
        }
        if (workplace >= 0)
        {
            values[3] = workValue + (wallet.coins < 3 ? 4 : 1);
        }
        if (trade && npc.role != "guard" && npc.role != "healer" && npc.role != "leader" &&
            npc.food >= 2 && life.fatigue < 65 && minute >= wallet.nextJob)
        {
            values[4] = 4 + npc.traits[1] * 3 + (npc.role == "merchant" ? 4 : 0);
        }
        if (npc.role == "healer" && life.medicine >= 1)
        {
            for (int other : Nearby(npc))
            {
                if (npcs[other].health < 75 && !lives[other].agreement &&
                    minute >= lives[other].nextSocial &&
                    diplomacy[npc.faction][npcs[other].faction] > rules.hostility)
                {
                    values[5] = 8 + (100 - npcs[other].health) * .05f;
                }
            }
        }
        if (npc.role == "guard" && regions[npc.region].security < .8f)
        {
            values[6] = 3;
        }
        if (life.social > 65 && minute >= life.nextSocial)
        {
            values[7] = 1 + npc.traits[5];
        }
        if (plan.stage == PlanStage::Failed && minute < plan.avoidUntil && !urgent)
        {
            values[int(plan.goal)] = -100;
        }
        const int chosen = int(std::max_element(values.begin(), values.end()) - values.begin());
        life.decisionTrace.clear();
        life.choices.clear();
        for (int g = 0; g < int(values.size()); ++g)
        {
            life.decisionTrace +=
                std::string(GoalName(LifeGoal(g))) + "=" + std::to_string(values[g]) + "; ";
        }
        plan.goal = LifeGoal(chosen);
        plan.started = minute;
        plan.initialCoins = wallet.coins;
        plan.steps = 0;
        plan.id = Record("npc_goal_selected", npc.id, "goal", "evaluated", GoalName(plan.goal),
                         life.decisionTrace);
        SetPlanStage(index, PlanStage::Planning, "욕구·알고 있는 일자리·위험·역할 비교");
        if (chosen == 0)
        {
            plan.retryAt = minute + planningRules.retryMinutes;
            SetPlanStage(index, PlanStage::Idle, "현재 실행 가능한 필요나 요청 없음");
            return true;
        }
    }
    auto chooseSite = [&](const std::string& kind, bool food)
    {
        int best = -1;
        float cost = 1e9f;
        for (std::size_t p = 0; p < places.size(); ++p)
        {
            if (places[p].kind != kind || !KnownWorkplace(index, int(p)) ||
                LifeSlot(index, int(p)) < 0)
            {
                continue;
            }
            const auto& market = economyRegions[places[p].region];
            if (food && (EconomyAvailable(places[p].region, 0, world) < 1 ||
                         (npc.food >= 1 && wallet.coins < market.price[0])))
            {
                continue;
            }
            const float value = distanceCost(int(p));
            if (value < cost)
            {
                best = int(p);
                cost = value;
            }
        }
        return best;
    };
    if (plan.goal == LifeGoal::Survive)
    {
        if (npc.food >= 1 && life.satiation < 55 &&
            StartPlanStep(index, LifeTask::Eat, chooseSite("market", false),
                          "확보한 식량으로 식사"))
        {
            return true;
        }
        if (npc.food < planningRules.stock &&
            StartPlanStep(index, LifeTask::Collect, chooseSite("warehouse", true),
                          "식량 확보 목표: 실제 구매 또는 구호 배급"))
        {
            return true;
        }
        if (workplace >= 0 &&
            StartPlanStep(index, LifeTask::Work, workplace, "식량 구입 자금을 벌기 위한 생산"))
        {
            return true;
        }
    }
    if (plan.goal == LifeGoal::Recover &&
        StartPlanStep(index, LifeTask::Rest, chooseSite("rest", false),
                      "회복 목표를 유지하며 휴식"))
    {
        return true;
    }
    if (plan.goal == LifeGoal::Supply && PlanEconomyOrder(index, world))
    {
        return true;
    }
    if (plan.goal == LifeGoal::Earn && workplace >= 0 &&
        StartPlanStep(index, LifeTask::Work, workplace, "원재료·수요·임금이 있는 작업장에서 생산"))
    {
        return true;
    }
    if (plan.goal == LifeGoal::Patrol &&
        StartPlanStep(index, LifeTask::Patrol, chooseSite("watch", false),
                      "실제 거점 경계 후 치안 결과 평가"))
    {
        return true;
    }
    if (minute >= life.nextSocial &&
        (plan.goal == LifeGoal::Care || plan.goal == LifeGoal::Discover ||
         plan.goal == LifeGoal::Survive))
    {
        for (int other : Nearby(npc))
        {
            if (!map.Visible(npc.x, npc.y, npcs[other].x, npcs[other].y))
            {
                continue;
            }
            int requester = index, supplier = other;
            int kind = plan.goal == LifeGoal::Discover ? 2 : 0;
            if (plan.goal == LifeGoal::Care)
            {
                if (npcs[other].health >= 75 || minute < lives[other].nextSocial)
                {
                    continue;
                }
                requester = other;
                supplier = index;
                kind = 1;
            }
            if (kind == 0 && npcs[other].food < 3)
            {
                continue;
            }
            if (kind == 2)
            {
                bool useful = false;
                for (const auto& report : npcs[other].memories)
                {
                    useful = useful || std::none_of(npc.memories.begin(), npc.memories.end(),
                                                    [&](const Memory& own)
                                                    {
                                                        return own.region == report.region &&
                                                               own.learned + 30 >= report.learned;
                                                    });
                }
                if (!useful)
                {
                    continue;
                }
            }
            OfferLife(requester, supplier, kind);
            if (lives[requester].agreement)
            {
                SetPlanStage(index, PlanStage::Waiting, "상대가 제안을 검토 중");
                return true;
            }
        }
        life.nextSocial = minute + planningRules.retryMinutes;
    }
    FinishGoal(index, false,
               "알고 있는 범위에 실행 가능한 방법 없음: 재고·자금·상대·자리 확인 필요");
    return true;
}
