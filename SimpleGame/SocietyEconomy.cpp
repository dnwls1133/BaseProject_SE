#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "ContentData.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <set>

const char* WorldSociety::EconomyGoodName(int good)
{
    static const char* names[] = {"식량", "약품", "자재"};
    return good >= 0 && good < 3 ? names[good] : "물자";
}

void WorldSociety::LoadEconomy(const std::string& root, const std::string& file)
{
    if (file.empty())
    {
        return;
    }
    using ContentData::Require;
    Require(HasLife(), "economy requires life definitions");
    const auto path = ContentData::Path(root, file), text = ContentData::Read(path);
    for (unsigned char byte : text)
    {
        signature = (signature ^ byte) * 16777619u;
    }
    economyRegions.resize(regions.size());
    economyWorkers.resize(npcs.size());
    std::set<std::string> seen;
    auto place = [&](const std::string& id)
    {
        for (std::size_t i = 0; i < places.size(); ++i)
        {
            if (places[i].id == id)
            {
                return int(i);
            }
        }
        Require(false, "unknown economy place: " + id);
        return -1;
    };
    ContentData::Rows(
        path, text,
        [&](const std::string& command, ContentData::Row& row)
        {
            if (command == "rules")
            {
                Require(seen.insert(command).second, "duplicate economy rules");
                economyRules.period = row.Integer(4, 120);
                economyRules.lifetime = row.Integer(120, 2880);
                economyRules.batch = row.Number(1, 3);
                economyRules.freight = row.Number(.1f, 10);
                economyRules.startingCoins = row.Number(0, 100);
                for (auto& price : economyRules.basePrice)
                {
                    price = row.Number(.1f, 20);
                }
            }
            else if (command == "market")
            {
                const int region = RegionIndex(row.Word());
                Require(region >= 0 && seen.insert("region:" + regions[region].id).second,
                        "unknown/duplicate economy region");
                auto& market = economyRegions[region];
                market.warehouse = place(row.Word());
                market.market = place(row.Word());
                market.clinic = place(row.Word());
                Require(places[market.warehouse].kind == "warehouse" &&
                            places[market.market].kind == "market" &&
                            places[market.clinic].kind == "clinic",
                        "invalid economy facilities");
                for (int p : {market.warehouse, market.market, market.clinic})
                {
                    Require(places[p].region == region,
                            "economy facility belongs to another region");
                }
                market.coins = row.Number(0, 10000);
                market.materials = row.Number(0, 100);
                for (auto& target : market.target)
                {
                    target = row.Number(1, 80);
                }
            }
            else if (command == "recipe")
            {
                EconomyRecipe recipe;
                recipe.place = place(row.Word());
                Require(seen.insert("recipe:" + places[recipe.place].id).second,
                        "duplicate workplace recipe");
                recipe.product = row.Integer(0, 2);
                recipe.ingredient = row.Integer(0, 2);
                recipe.input = row.Number(.05f, 10);
                recipe.output = row.Number(.1f, 10);
                recipe.seconds = row.Number(3, 120);
                recipe.wage = row.Number(.01f, 5);
                Require(recipe.product != recipe.ingredient, "recipe needs distinct input/output");
                const auto& kind = places[recipe.place].kind;
                Require(kind == "farm" || kind == "clinic" || kind == "archive",
                        "recipe requires a production workplace");
                economyRecipes.push_back(recipe);
            }
            else
            {
                Require(false, "unknown economy record");
            }
        });
    Require(seen.count("rules") != 0 && !economyRecipes.empty(), "missing economy rules/recipes");
    for (std::size_t i = 0; i < regions.size(); ++i)
    {
        Require(seen.count("region:" + regions[i].id) != 0, "missing economy market");
        economyRegions[i].price = economyRules.basePrice;
    }
    for (auto& worker : economyWorkers)
    {
        worker.coins = economyRules.startingCoins;
    }
}

float WorldSociety::EconomyStock(int region, int good, const WorldSimulation& world) const
{
    if (!HasEconomy() || region < 0 || region >= int(economyRegions.size()))
    {
        return 0;
    }
    if (good == 0)
    {
        const auto* state = world.FindRegion(regions[region].id);
        return state ? float(state->food * 100) : 0;
    }
    const auto& market = economyRegions[region];
    return good == 1 ? places[market.clinic].medicine : market.materials;
}

float WorldSociety::EconomyAvailable(int region, int good, const WorldSimulation& world) const
{
    float stock = EconomyStock(region, good, world);
    for (const auto& order : economyOrders)
    {
        if (order.source == region && order.good == good && order.stage == 1)
        {
            stock -= order.units;
        }
    }
    return std::max(0.f, stock);
}

bool WorldSociety::ChangeEconomyStock(int region, int good, float amount, WorldSimulation& world,
                                      const std::string& actor, std::uint64_t cause)
{
    const float before = EconomyStock(region, good, world);
    if (before + amount < -.001f || before + amount > 100.001f)
    {
        return false;
    }
    if (good == 0)
    {
        world.ChangeRegion(regions[region].id, amount * .01, 0, actor, "경제 재고 이동", cause);
    }
    else if (good == 1)
    {
        places[economyRegions[region].clinic].medicine = std::clamp(before + amount, 0.f, 100.f);
    }
    else
    {
        economyRegions[region].materials = std::clamp(before + amount, 0.f, 100.f);
    }
    Record("economy_stock", actor, regions[region].id, std::to_string(before),
           std::to_string(before + amount), EconomyGoodName(good), cause);
    return true;
}

float WorldSociety::EconomyWorkScore(int npc, int place, const WorldSimulation& world) const
{
    if (!HasEconomy())
    {
        return 0;
    }
    const auto& worker = economyWorkers[npc];
    if (worker.lastWork == place && minute < worker.nextWork)
    {
        return -1000;
    }
    if (places[place].kind == "clinic" && npcs[npc].role == "healer" && lives[npc].medicine < 2 &&
        EconomyAvailable(places[place].region, 1, world) >= 1)
    {
        return 5;
    }
    for (const auto& recipe : economyRecipes)
    {
        if (recipe.place != place)
        {
            continue;
        }
        const int region = places[place].region;
        const auto& market = economyRegions[region];
        const float stock = EconomyStock(region, recipe.product, world);
        // Outstanding export orders constitute real demand, including local reservations.
        float demand = market.target[recipe.product];
        for (const auto& order : economyOrders)
        {
            if (order.source == region && order.good == recipe.product && order.stage <= 1)
            {
                demand += order.units;
            }
        }
        if (stock >= demand || stock + recipe.output > 100 ||
            EconomyAvailable(region, recipe.ingredient, world) < recipe.input ||
            market.coins < recipe.wage)
        {
            return -1000;
        }
        return 1 + (demand - stock) / std::max(1.f, demand) * 6 +
               recipe.wage / std::max(.5f, worker.coins) +
               (npcs[npc].role == "healer" && recipe.product == 1 ? 2.f : 0);
    }
    return -1000;
}

bool WorldSociety::EconomyWork(int npc, WorldSimulation& world)
{
    if (!HasEconomy())
    {
        return false;
    }
    auto& life = lives[npc];
    if (places[life.place].kind == "clinic" && npcs[npc].role == "healer" && life.medicine < 2 &&
        EconomyAvailable(places[life.place].region, 1, world) >= 1)
    {
        if (life.progress >= 3)
        {
            ChangeEconomyStock(places[life.place].region, 1, -1, world, npcs[npc].id, life.cause);
            life.medicine += 1;
            ++life.plan.steps;
            life.task = LifeTask::None;
            life.progress = 0;
        }
        return true;
    }
    auto recipe = std::find_if(economyRecipes.begin(), economyRecipes.end(),
                               [&](const EconomyRecipe& value)
                               {
                                   return value.place == life.place;
                               });
    if (recipe == economyRecipes.end())
    {
        return false;
    }
    PROFILE_SCOPE(EconomyProduction);
    const int region = places[life.place].region;
    if (EconomyWorkScore(npc, life.place, world) < 0)
    {
        life.reason = "수요·재료·임금 조건이 바뀌어 생산 중단";
        economyWorkers[npc].lastWork = life.place;
        economyWorkers[npc].nextWork = minute + 30;
        life.task = LifeTask::None;
        life.progress = 0;
        return true;
    }
    if (life.progress < recipe->seconds)
    {
        return true;
    }
    ChangeEconomyStock(region, recipe->ingredient, -recipe->input, world, npcs[npc].id, life.cause);
    ChangeEconomyStock(region, recipe->product, recipe->output, world, npcs[npc].id, life.cause);
    economyRegions[region].coins -= recipe->wage;
    economyWorkers[npc].coins += recipe->wage;
    economyRegions[region].produced[recipe->product] += recipe->output;
    Record("economy_production", npcs[npc].id, places[life.place].id,
           std::string(EconomyGoodName(recipe->ingredient)) + "=" + std::to_string(recipe->input),
           std::string(EconomyGoodName(recipe->product)) + "=" + std::to_string(recipe->output),
           "현장 생산 완료 · 금고에서 임금 지급 " + std::to_string(recipe->wage), life.cause);
    ++life.plan.steps;
    economyWorkers[npc].lastWork = life.place;
    economyWorkers[npc].nextWork = minute + 24;
    life.task = LifeTask::None;
    life.progress = 0;
    return true;
}

bool WorldSociety::BuyEconomyFood(int npc, WorldSimulation& world)
{
    if (!HasEconomy() || economyWorkers[npc].order != 0)
    {
        return false;
    }
    auto& life = lives[npc];
    const int region = places[life.place].region;
    if (life.progress < 3)
    {
        return true;
    }
    const float price = economyRegions[region].price[0];
    const float wanted = std::min(3.f, std::max(0.f, 6 - npcs[npc].food));
    float units = std::min(wanted, EconomyAvailable(region, 0, world));
    bool relief = economyWorkers[npc].coins < price && npcs[npc].food < 1;
    units = std::min(units, relief ? 1.f : economyWorkers[npc].coins / price);
    if (units > .01f && ChangeEconomyStock(region, 0, -units, world, npcs[npc].id, life.cause))
    {
        const float payment = relief ? 0 : units * price;
        economyWorkers[npc].coins -= payment;
        economyRegions[region].coins += payment;
        npcs[npc].food += units;
        ++life.plan.steps;
        Record("economy_purchase", npcs[npc].id, regions[region].id, "stock",
               "food=" + std::to_string(units) + " coins=" + std::to_string(payment),
               relief ? "굶주린 무자산 주민에게 유한 재고에서 구호 배급" : "소지금으로 식량 구입",
               life.cause);
    }
    life.task = LifeTask::None;
    life.progress = 0;
    return true;
}

int WorldSociety::EconomyCargoKind(int npc) const
{
    if (HasEconomy())
    {
        for (const auto& order : economyOrders)
        {
            if (order.id == economyWorkers[npc].order)
            {
                return order.good;
            }
        }
    }
    return 0;
}

float WorldSociety::EconomyProgress(int place) const
{
    float result = 0;
    for (const auto& recipe : economyRecipes)
    {
        if (recipe.place == place)
        {
            for (std::size_t i = 0; i < lives.size(); ++i)
            {
                if (npcs[i].Alive() && lives[i].place == place &&
                    (lives[i].task == LifeTask::Work || lives[i].task == LifeTask::Research))
                {
                    result = std::max(result, std::min(1.f, lives[i].progress / recipe.seconds));
                }
            }
        }
    }
    return result;
}
