#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "RandomLevelMap.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

void WorldSociety::CancelEconomyOrder(EconomyOrder& order, WorldSimulation& world,
                                      const std::string& reason)
{
    if (order.stage >= 4)
    {
        return;
    }
    economyRegions[order.destination].coins += order.escrow;
    order.escrow = 0;
    order.outcome = reason;
    const bool alive = order.carrier >= 0 && npcs[order.carrier].Alive();
    if (order.stage == 2 && alive)
    {
        order.stage = 3;
        order.deadline = minute + economyRules.lifetime;
        auto& life = lives[order.carrier];
        life.task = LifeTask::Deliver;
        life.place = economyRegions[order.source].warehouse;
        life.delivery = life.place;
        life.slot = LifeSlot(order.carrier, life.place);
        life.progress = 0;
        life.stalled = 0;
        life.plan.waitingSeconds = 0;
        life.until = order.deadline;
        life.reason = "거래 중단 · 화물을 출발 창고로 실제 반송";
        npcs[order.carrier].pathCell = -1;
        Record("economy_return", npcs[order.carrier].id, regions[order.source].id, "delivery",
               "return", reason, order.id);
        return;
    }
    if (order.carrier >= 0)
    {
        auto& worker = economyWorkers[order.carrier];
        auto& life = lives[order.carrier];
        if (life.cargo > 0)
        {
            Record("economy_cargo_lost", npcs[order.carrier].id, regions[order.source].id,
                   std::to_string(life.cargo), "0", reason, order.id);
        }
        if (order.supplier >= 0 && order.stage >= 1)
        {
            Relate(order.supplier, order.carrier, -2, 1, reason, order.id);
        }
        FinishGoal(order.carrier, false, reason);
        worker.order = 0;
        worker.nextJob = minute + 60;
        life.cargo = 0;
        life.task = LifeTask::None;
        life.progress = 0;
    }
    order.stage = 5;
    Record("economy_order_cancelled", regions[order.source].id, regions[order.destination].id,
           "pending", "cancelled", reason, order.id);
    (void)world;
}

void WorldSociety::EconomyTick(WorldSimulation& world)
{
    if (!HasEconomy())
    {
        return;
    }
    PROFILE_SCOPE(EconomyDecision);
    for (auto& order : economyOrders)
    {
        if (order.stage >= 4)
        {
            continue;
        }
        const int seller = regions[order.source].owner, buyer = regions[order.destination].owner;
        const bool closed = seller < 0 || buyer < 0 || diplomacy[seller][buyer] < rules.hostility ||
                            diplomacy[buyer][seller] < rules.hostility;
        const bool lostPartner =
            order.stage != 3 &&
            (order.receiver < 0 || !npcs[order.receiver].Alive() ||
             (order.stage == 1 && (order.supplier < 0 || !npcs[order.supplier].Alive())));
        const bool blocked = order.carrier >= 0 && (lives[order.carrier].stalled > 25 ||
                                                    lives[order.carrier].plan.waitingSeconds >
                                                        planningRules.handoffSeconds);
        if (lostPartner || blocked || (order.carrier >= 0 && !npcs[order.carrier].Alive()) ||
            minute >= order.deadline || (order.stage != 3 && closed))
        {
            CancelEconomyOrder(order, world,
                               lostPartner ? "거래 담당자 사망 또는 부재"
                               : blocked   ? "이동 또는 현장 인계 대기 한도 초과"
                               : closed    ? "교역 상대와 교전하여 계약 중단"
                               : order.carrier >= 0 && !npcs[order.carrier].Alive()
                                   ? "운송자 사망으로 화물 손실"
                                   : "운송 기한 경과");
        }
    }
    if (minute < nextEconomyTick)
    {
        return;
    }
    nextEconomyTick = minute + economyRules.period;
    // Bound completed history while retaining every live contract.
    if (economyOrders.size() > 48)
    {
        economyOrders.erase(std::remove_if(economyOrders.begin(), economyOrders.end(),
                                           [](const EconomyOrder& order)
                                           {
                                               return order.stage >= 4;
                                           }),
                            economyOrders.end());
    }
    for (std::size_t r = 0; r < economyRegions.size(); ++r)
    {
        auto& market = economyRegions[r];
        for (int good = 0; good < 3; ++good)
        {
            const float stock = EconomyStock(int(r), good, world);
            market.price[good] =
                economyRules.basePrice[good] *
                std::clamp(1 + (market.target[good] - stock) / market.target[good], .5f, 2.5f);
        }
    }
    auto exportable = [&](int region, int good)
    {
        const bool producer =
            std::any_of(economyRecipes.begin(), economyRecipes.end(),
                        [&](const EconomyRecipe& recipe)
                        {
                            return places[recipe.place].region == region && recipe.product == good;
                        });
        // Consumers keep their target stock; producers retain a smaller emergency reserve.
        return std::max(0.f, EconomyAvailable(region, good, world) -
                                 economyRegions[region].target[good] * (producer ? .6f : 1.f));
    };
    // Buyers publish concrete deficits on authored trade routes. Merchants receive notices
    // after a distance delay and still need to visit the supplier and customer in person.
    for (std::size_t buyer = 0; buyer < economyRegions.size(); ++buyer)
    {
        for (int good = 0; good < 3; ++good)
        {
            if (economyOrders.size() >= 64)
            {
                return;
            }
            const float deficit =
                economyRegions[buyer].target[good] - EconomyStock(int(buyer), good, world);
            if (deficit < 1 || std::any_of(
                                   economyOrders.begin(), economyOrders.end(),
                                   [&](const EconomyOrder& order)
                                   {
                                       return order.destination == int(buyer) &&
                                              order.good == good && order.stage < 4;
                                   }))
            {
                continue;
            }
            int source = -1;
            float best = -1e9f;
            for (const auto& route : tradeRoutes)
            {
                const int seller = places[route.first].region;
                if (places[route.second].region != int(buyer) || seller == int(buyer))
                {
                    continue;
                }
                const int a = regions[seller].owner, b = regions[buyer].owner;
                if (a < 0 || b < 0 || diplomacy[a][b] < rules.hostility ||
                    diplomacy[b][a] < rules.hostility)
                {
                    continue;
                }
                const float spare = exportable(seller, good);
                const float distance = std::hypot(regions[seller].x - regions[buyer].x,
                                                  regions[seller].y - regions[buyer].y);
                const float score = spare - distance * .02f;
                if (spare >= 1 && score > best)
                {
                    best = score;
                    source = seller;
                }
            }
            if (source < 0)
            {
                continue;
            }
            const int requester = EconomyRepresentative(int(buyer), -1);
            if (requester < 0)
            {
                continue;
            }
            EconomyOrder order;
            order.receiver = requester;
            order.source = source;
            order.destination = int(buyer);
            order.good = good;
            order.units = std::min({economyRules.batch, deficit, exportable(source, good)});
            order.unitPrice =
                (economyRegions[source].price[good] + economyRegions[buyer].price[good]) * .5f;
            const float distance = std::hypot(regions[source].x - regions[buyer].x,
                                              regions[source].y - regions[buyer].y);
            order.freight = economyRules.freight + distance * .015f;
            if (economyRegions[buyer].coins < order.units * order.unitPrice + order.freight)
            {
                continue;
            }
            order.available = minute + std::uint64_t(distance / 4);
            order.deadline = minute + economyRules.lifetime;
            order.id = Record("economy_order_posted", npcs[requester].id, regions[source].id,
                              "shortage", "units=" + std::to_string(order.units),
                              std::string(EconomyGoodName(good)) +
                                  " 구매 주문 · 창고 수령과 시장 납품 필요");
            economyOrders.push_back(order);
        }
    }
}

bool WorldSociety::PlanEconomyOrder(int index, WorldSimulation& world)
{
    if (!HasEconomy())
    {
        return false;
    }
    auto& npc = npcs[index];
    auto& worker = economyWorkers[index];
    auto& life = lives[index];
    if (IsHive(index) || IsCourier(index) || EconomyParticipantBusy(index) || worker.order ||
        minute < worker.nextJob || life.agreement || life.cargo > 0 || life.fatigue > 65 ||
        npc.food < 2 || npc.health < 50 || npc.role == "guard" || npc.role == "healer" ||
        npc.role == "leader")
    {
        return false;
    }
    EconomyOrder* choice = nullptr;
    float best = 0;
    for (auto& order : economyOrders)
    {
        // A buyer must brief a carrier in person; orders are not a global radio channel.
        if (npc.region != order.destination || order.receiver < 0 ||
            std::hypot(npc.x - npcs[order.receiver].x, npc.y - npcs[order.receiver].y) > 2.5f)
        {
            continue;
        }
        if (order.stage != 0 || minute < order.available || minute >= order.deadline)
        {
            continue;
        }
        if (order.receiver == index || !npcs[order.receiver].Alive() ||
            EconomyParticipantBusy(order.receiver, order.id) ||
            EconomyRepresentative(order.source, index, order.id) < 0)
        {
            continue;
        }
        const int source = economyRegions[order.source].warehouse;
        const int destination = economyRegions[order.destination].market;
        if (minute < life.plan.avoidUntil &&
            (life.plan.avoidedPlace == source || life.plan.avoidedPlace == destination))
        {
            continue;
        }
        // The buyer just briefed this carrier in person. The authored route gives the
        // supplier location, not remote knowledge of the supplier's actual stock.
        const int seller = regions[order.source].owner;
        if (seller < 0 || diplomacy[npc.faction][seller] < rules.hostility ||
            diplomacy[seller][npc.faction] < rules.hostility)
        {
            continue;
        }
        const int owner = regions[order.destination].owner;
        if (owner < 0 || diplomacy[npc.faction][owner] < rules.hostility ||
            diplomacy[owner][npc.faction] < rules.hostility || LifeSlot(index, source) < 0 ||
            EconomyAvailable(order.source, order.good, world) < order.units)
        {
            continue;
        }
        const bool producer = std::any_of(economyRecipes.begin(), economyRecipes.end(),
                                          [&](const EconomyRecipe& recipe)
                                          {
                                              return places[recipe.place].region == order.source &&
                                                     recipe.product == order.good;
                                          });
        if (EconomyAvailable(order.source, order.good, world) - order.units <
            economyRegions[order.source].target[order.good] * (producer ? .6f : 1.f))
        {
            continue;
        }
        const float distance =
            std::hypot(npc.x - places[source].position.x, npc.y - places[source].position.y) +
            std::hypot(places[source].position.x - places[destination].position.x,
                       places[source].position.y - places[destination].position.y);
        const float value = order.freight * (1 + npc.traits[1]) +
                            (npc.role == "merchant" ? 3.f : 0) + (worker.coins < 2 ? 2.f : 0) -
                            distance * .025f - life.fatigue * .02f;
        if (value > best)
        {
            choice = &order;
            best = value;
        }
    }
    if (!choice)
    {
        worker.nextJob = minute + 12;
        return false;
    }
    const float payment = choice->units * choice->unitPrice + choice->freight;
    if (economyRegions[choice->destination].coins < payment)
    {
        return false;
    }
    const int supplier = EconomyRepresentative(choice->source, index, choice->id);
    const int receiver = choice->receiver;
    // Both parties can reject a carrier based on personal experience, not only team colors.
    if (supplier < 0 || PersonalRegard(supplier, index) < -25 ||
        PersonalRegard(receiver, index) < -25 || lives[receiver].agreement ||
        economyWorkers[receiver].order || npcs[receiver].health < 35)
    {
        worker.nextJob = minute + planningRules.avoidMinutes;
        return false;
    }
    choice->supplier = supplier;
    economyRegions[choice->destination].coins -= payment;
    choice->escrow = payment;
    choice->carrier = index;
    choice->stage = 1;
    worker.order = choice->id;
    life.task = LifeTask::Collect;
    life.place = economyRegions[choice->source].warehouse;
    life.delivery = economyRegions[choice->destination].market;
    life.slot = LifeSlot(index, life.place);
    life.partner = -1;
    life.progress = 0;
    life.until = choice->deadline;
    life.stalled = 0;
    life.plan.waitingSeconds = 0;
    life.cause = choice->id;
    life.reason = regions[choice->destination].name + "의 " + EconomyGoodName(choice->good) +
                  " 부족 주문 수락 · 재고 예약과 대금 예치";
    life.choices.clear();
    life.decisionTrace = "실제 주문 운송료=" + std::to_string(choice->freight) +
                         " 운송 평가=" + std::to_string(best);
    npc.action = Action::Travel;
    npc.destination = choice->source;
    Record("economy_contract_reserved", npc.id, regions[choice->destination].id, "posted",
           "escrow=" + std::to_string(payment), life.reason, choice->id);
    return true;
}

bool WorldSociety::ExecuteEconomyOrder(int index, float dt, WorldSimulation& world,
                                       const RandomLevelMap& map)
{
    if (!HasEconomy() || !economyWorkers[index].order)
    {
        return false;
    }
    PROFILE_SCOPE(EconomyTransfer);
    auto found = std::find_if(economyOrders.begin(), economyOrders.end(),
                              [&](const EconomyOrder& order)
                              {
                                  return order.id == economyWorkers[index].order;
                              });
    if (found == economyOrders.end())
    {
        return false;
    }
    auto& order = *found;
    auto& life = lives[index];
    if (order.stage != 3 && !EconomicContact(index, order, map))
    {
        life.progress = 0;
        life.plan.waitingSeconds += dt;
        const int partner = order.stage == 1 ? order.supplier : order.receiver;
        SetPlanStage(index, PlanStage::Waiting,
                     (partner >= 0 ? npcs[partner].name : std::string("거래 담당자")) +
                         "의 현장 도착/인계 대기");
        return true;
    }
    life.plan.waitingSeconds = 0;
    SetPlanStage(index, PlanStage::Working, "거래 당사자와 현장에서 화물 인계");
    if (life.progress < 3)
    {
        return true;
    }
    if (order.stage == 1)
    {
        if (!ChangeEconomyStock(order.source, order.good, -order.units, world, npcs[index].id,
                                order.id))
        {
            CancelEconomyOrder(order, world, "예약 이후 창고 재고 부족");
            return true;
        }
        Relate(index, order.supplier, 2, 0, "직접 만나 예약된 화물을 인수", order.id);
        life.cargo = order.units;
        ++life.plan.steps;
        order.stage = 2;
        life.task = LifeTask::Deliver;
        life.place = economyRegions[order.destination].market;
        life.slot = LifeSlot(index, life.place);
        life.progress = 0;
        life.stalled = 0;
        life.reason = std::string(EconomyGoodName(order.good)) + " " + std::to_string(order.units) +
                      "개를 " + regions[order.destination].name + "에 운송";
        npcs[index].destination = order.destination;
        Record("economy_loaded", npcs[index].id, regions[order.source].id, "warehouse",
               "cargo=" + std::to_string(life.cargo), life.reason, order.id);
        return true;
    }
    const int destination = order.stage == 3 ? order.source : order.destination;
    if (!ChangeEconomyStock(destination, order.good, life.cargo, world, npcs[index].id, order.id))
    {
        if (order.stage == 2)
        {
            CancelEconomyOrder(order, world, "목적지 수용량 부족 · 화물 반송");
        }
        return true;
    }
    if (order.stage == 2)
    {
        const float goodsPayment = order.escrow - order.freight;
        economyRegions[order.source].coins += goodsPayment;
        economyWorkers[index].coins += order.freight;
        economyRegions[order.source].exported[order.good] += life.cargo;
        economyRegions[order.destination].imported[order.good] += life.cargo;
        const int a = regions[order.source].owner, b = regions[order.destination].owner;
        if (a >= 0 && b >= 0 && a != b)
        {
            diplomacy[a][b] = std::min(100.f, diplomacy[a][b] + .75f);
            diplomacy[b][a] = std::min(100.f, diplomacy[b][a] + .75f);
        }
        regions[order.destination].unrest = std::max(0.f, regions[order.destination].unrest - .02f);
        Relate(index, order.receiver, 5, -1, "현장에서 화물을 인계하고 대금을 받음", order.id);
        Relate(order.receiver, index, 6, -1, "운송 약속을 이행함", order.id);
        order.stage = 4;
        order.outcome = "현장 납품 완료 · 판매 대금과 운송료 정산";
        Profile::Add(Profile::Metric::EconomyDeliveries);
    }
    else
    {
        order.stage = 5;
        order.outcome = "출발 창고에 화물 반송 완료";
    }
    Record("economy_delivery", npcs[index].id, regions[destination].id,
           "cargo=" + std::to_string(life.cargo), "settled=" + std::to_string(order.escrow),
           order.outcome, order.id);
    order.escrow = 0;
    economyWorkers[index].order = 0;
    economyWorkers[index].nextJob = minute + 24;
    life.cargo = 0;
    life.task = LifeTask::None;
    life.progress = 0;
    life.reason = order.outcome;
    ++life.plan.steps;
    FinishGoal(index, order.stage == 4, order.outcome);
    return true;
}
