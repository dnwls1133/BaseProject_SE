#include "stdafx.h"
#include "WorldSociety.h"
#include "ContentData.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>

void WorldSociety::WriteEconomy(std::ostream& out) const
{
    if (!HasEconomy())
    {
        return;
    }
    out << "econ_clock " << nextEconomyTick << '\n';
    for (std::size_t i = 0; i < economyRegions.size(); ++i)
    {
        const auto& r = economyRegions[i];
        out << "econ_region " << std::quoted(regions[i].id) << ' ' << r.coins << ' ' << r.materials;
        for (const auto* values : {&r.price, &r.produced, &r.imported, &r.exported})
        {
            for (float value : *values)
            {
                out << ' ' << value;
            }
        }
        out << '\n';
    }
    for (std::size_t i = 0; i < economyWorkers.size(); ++i)
    {
        const auto& w = economyWorkers[i];
        out << "econ_worker " << std::quoted(npcs[i].id) << ' ' << w.coins << ' ' << w.order << ' '
            << w.nextJob << ' ' << w.nextWork << ' '
            << std::quoted(w.lastWork < 0 ? "-" : places[w.lastWork].id) << '\n';
    }
    for (const auto& o : economyOrders)
    {
        if (o.stage >= 4)
        {
            continue;
        }
        out << "econ_order " << o.id << ' ' << o.available << ' ' << o.deadline << ' '
            << std::quoted(regions[o.source].id) << ' ' << std::quoted(regions[o.destination].id)
            << ' ' << o.good << ' ' << std::quoted(o.carrier < 0 ? "-" : npcs[o.carrier].id) << ' '
            << o.stage << ' ' << o.units << ' ' << o.unitPrice << ' ' << o.freight << ' '
            << o.escrow << ' ' << std::quoted(o.supplier < 0 ? "-" : npcs[o.supplier].id) << ' '
            << std::quoted(npcs[o.receiver].id) << '\n';
    }
}

bool WorldSociety::ReadEconomy(const std::string& command, ContentData::Row& row)
{
    if (command.compare(0, 5, "econ_") != 0)
    {
        return false;
    }
    using ContentData::Require;
    Require(HasEconomy(), "economy state without definitions");
    auto unique = [&](const std::string& id)
    {
        Require(restoreSeen.insert(command + ":" + id).second, "duplicate economy state");
    };
    if (command == "econ_clock")
    {
        unique("clock");
        nextEconomyTick = row.Unsigned();
    }
    else if (command == "econ_region")
    {
        const auto id = row.Word();
        const int index = RegionIndex(id);
        Require(index >= 0, "unknown economy region");
        unique(id);
        auto& r = economyRegions[index];
        r.coins = row.Number(0, 1e7f);
        r.materials = row.Number(0, 100);
        for (auto* values : {&r.price, &r.produced, &r.imported, &r.exported})
        {
            for (auto& value : *values)
            {
                value = row.Number(0, 1e7f);
            }
        }
        for (float price : r.price)
        {
            Require(price > 0, "zero economy price");
        }
    }
    else if (command == "econ_worker")
    {
        const auto id = row.Word();
        const int index = NpcIndex(id);
        Require(index >= 0, "unknown economy worker");
        unique(id);
        auto& w = economyWorkers[index];
        w.coins = row.Number(0, 1e7f);
        w.order = row.Unsigned();
        w.nextJob = row.Unsigned();
        w.nextWork = row.Unsigned();
        const auto site = row.Word();
        w.lastWork = -1;
        for (std::size_t p = 0; p < places.size(); ++p)
        {
            if (places[p].id == site)
            {
                w.lastWork = int(p);
            }
        }
        Require(site == "-" || w.lastWork >= 0, "unknown economy workplace");
    }
    else if (command == "econ_order")
    {
        Require(economyOrders.size() < 64, "too many economy orders");
        EconomyOrder o;
        o.id = row.Unsigned();
        unique(std::to_string(o.id));
        o.available = row.Unsigned();
        o.deadline = row.Unsigned();
        o.source = RegionIndex(row.Word());
        o.destination = RegionIndex(row.Word());
        Require(o.source >= 0 && o.destination >= 0 && o.source != o.destination,
                "invalid economy route");
        o.good = row.Integer(0, 2);
        const auto carrier = row.Word();
        o.carrier = NpcIndex(carrier);
        Require(carrier == "-" || o.carrier >= 0, "unknown economic carrier");
        o.stage = row.Integer(0, 3);
        o.units = row.Number(.001f, 3);
        o.unitPrice = row.Number(.01f, 100);
        o.freight = row.Number(0, 1000);
        o.escrow = row.Number(0, 10000);
        const auto supplier = row.Word();
        o.supplier = NpcIndex(supplier);
        o.receiver = NpcIndex(row.Word());
        Require((supplier == "-" || o.supplier >= 0) && o.receiver >= 0 &&
                    (o.stage == 0 ||
                     (o.supplier >= 0 && o.carrier != o.receiver && o.carrier != o.supplier)),
                "invalid trade participants");
        economyOrders.push_back(o);
    }
    else
    {
        Require(false, "unknown economy save record");
    }
    return true;
}

void WorldSociety::FinishEconomyRestore()
{
    if (!HasEconomy())
    {
        return;
    }
    using ContentData::Require;
    Require(restoreSeen.count("econ_clock:clock") != 0, "missing economy clock");
    for (const auto& region : regions)
    {
        Require(restoreSeen.count("econ_region:" + region.id) != 0, "missing economy region");
    }
    for (std::size_t i = 0; i < npcs.size(); ++i)
    {
        Require(restoreSeen.count("econ_worker:" + npcs[i].id) != 0, "missing economy worker");
        const auto id = economyWorkers[i].order;
        Require(!id || std::count_if(economyOrders.begin(), economyOrders.end(),
                                     [&](const EconomyOrder& o)
                                     {
                                         return o.id == id && o.carrier == int(i);
                                     }) == 1,
                "worker contract reference mismatch");
    }
    for (const auto& o : economyOrders)
    {
        Require(o.id > 0 && o.id < serial && o.deadline >= o.available,
                "invalid economy order timeline");
        const float expected = o.stage == 1 || o.stage == 2 ? o.units * o.unitPrice + o.freight : 0;
        Require(std::abs(expected - o.escrow) < .02f, "economy escrow mismatch");
        if (o.stage == 0)
        {
            Require(o.carrier < 0, "posted order has carrier");
            continue;
        }
        Require(o.carrier >= 0 && economyWorkers[o.carrier].order == o.id,
                "contract worker mismatch");
        const auto& life = lives[o.carrier];
        const int destination = o.stage == 1 || o.stage == 3 ? economyRegions[o.source].warehouse
                                                             : economyRegions[o.destination].market;
        Require(!life.agreement && life.place == destination &&
                    life.task == (o.stage == 1 ? LifeTask::Collect : LifeTask::Deliver) &&
                    std::abs(life.cargo - (o.stage == 1 ? 0.f : o.units)) < .01f,
                "contract cargo or physical plan mismatch");
    }
}
