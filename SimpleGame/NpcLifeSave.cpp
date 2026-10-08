#include "stdafx.h"
#include "WorldSociety.h"
#include "ContentData.h"
#include <algorithm>
#include <iomanip>
#include <ostream>
#include <set>

void WorldSociety::WriteLife(std::ostream& out) const
{
    auto site = [&](int i)
    {
        return i < 0 ? std::string("-") : places[i].id;
    };
    auto npc = [&](int i)
    {
        return i < 0 ? std::string("-") : npcs[i].id;
    };
    for (std::size_t i = 0; i < lives.size(); ++i)
    {
        const auto& l = lives[i];
        out << "life_npc " << std::quoted(npcs[i].id) << ' ' << int(l.task) << ' '
            << std::quoted(site(l.home)) << ' ' << std::quoted(site(l.work)) << ' '
            << std::quoted(site(l.place)) << ' ' << std::quoted(npc(l.partner)) << ' '
            << std::quoted(site(l.delivery)) << ' ' << l.agreement << ' ' << l.until << ' '
            << l.nextSocial << ' ' << l.cause << ' ' << l.nextTrade << ' ' << l.slot << ' '
            << l.satiation << ' ' << l.fatigue << ' ' << l.social << ' ' << l.progress << ' '
            << l.cargo << ' ' << l.medicine << ' ' << l.lastDistance << ' ' << l.stalled << ' '
            << l.helped << ' ' << l.broken << ' ' << npcs[i].yaw << ' '
            << std::quoted(l.reason.empty() ? "-" : l.reason) << ' '
            << std::quoted(l.lastMemory.empty() ? "-" : l.lastMemory) << '\n';
    }
    for (const auto& p : places)
    {
        out << "life_place " << std::quoted(p.id) << ' ' << p.medicine << '\n';
    }
    for (const auto& a : agreements)
    {
        auto faction = [&](int i)
        {
            return i < 0 ? std::string("-") : factions[i].id;
        };
        out << "life_agreement " << a.id << ' ' << a.due << ' ' << a.deadline << ' '
            << std::quoted(npc(a.requester)) << ' ' << std::quoted(npc(a.supplier)) << ' ' << a.kind
            << ' ' << a.stage << ' ' << std::quoted(faction(a.payer)) << ' '
            << std::quoted(faction(a.payee)) << ' ' << a.cargo << ' ' << a.payment << '\n';
    }
}

bool WorldSociety::ReadLife(const std::string& command, ContentData::Row& row)
{
    if (command.compare(0, 5, "life_") != 0)
    {
        return false;
    }
    using ContentData::Require;
    Require(HasLife(), "life state without definitions");
    auto unique = [&](const std::string& id)
    {
        Require(restoreSeen.insert(command + ":" + id).second, "duplicate life state");
    };
    auto npc = [&](bool optional)
    {
        const auto id = row.Word();
        int n = NpcIndex(id);
        Require(n >= 0 || (optional && id == "-"), "unknown life NPC");
        return n;
    };
    auto site = [&](bool optional)
    {
        const auto id = row.Word();
        if (optional && id == "-")
        {
            return -1;
        }
        for (std::size_t i = 0; i < places.size(); ++i)
        {
            if (places[i].id == id)
            {
                return int(i);
            }
        }
        Require(false, "unknown saved activity place");
        return -1;
    };
    if (command == "life_npc")
    {
        const int n = npc(false);
        unique(npcs[n].id);
        auto& l = lives[n];
        l.task = static_cast<LifeTask>(row.Integer(0, 8));
        l.home = site(false);
        l.work = site(false);
        l.place = site(true);
        l.partner = npc(true);
        l.delivery = site(true);
        l.agreement = row.Unsigned();
        l.until = row.Unsigned();
        l.nextSocial = row.Unsigned();
        l.cause = row.Unsigned();
        l.nextTrade = row.Unsigned();
        l.slot = row.Integer(-1, 11);
        l.satiation = row.Number(0, 100);
        Require(l.place < 0 || l.slot < places[l.place].capacity, "invalid reserved activity slot");
        l.fatigue = row.Number(0, 100);
        l.social = row.Number(0, 100);
        l.progress = row.Number(0, 1000);
        l.cargo = row.Number(0, 3);
        l.medicine = row.Number(0, 10);
        l.lastDistance = row.Number(0, 1e6f);
        l.stalled = row.Number(0, 1000);
        l.helped = row.Integer(0, 1000000);
        l.broken = row.Integer(0, 1000000);
        npcs[n].yaw = row.Number(-3.142f, 3.142f);
        l.reason = row.Word();
        l.lastMemory = row.Word();
        if (l.reason == "-")
        {
            l.reason.clear();
        }
        if (l.lastMemory == "-")
        {
            l.lastMemory.clear();
        }
        Require(l.task == LifeTask::None || l.task == LifeTask::Meet || l.place >= 0,
                "active plan needs place");
        Require(l.cargo == 0 || l.task == LifeTask::Deliver, "unowned life cargo");
    }
    else if (command == "life_place")
    {
        const int p = site(false);
        unique(places[p].id);
        places[p].medicine = row.Number(0, 100);
    }
    else if (command == "life_agreement")
    {
        Require(agreements.size() < 64, "too many saved agreements");
        Agreement a;
        a.id = row.Unsigned();
        unique(std::to_string(a.id));
        a.due = row.Unsigned();
        a.deadline = row.Unsigned();
        a.requester = npc(false);
        a.supplier = npc(false);
        a.kind = row.Integer(0, 2);
        a.stage = row.Integer(0, 2);
        auto faction = [&]()
        {
            auto id = row.Word();
            int f = FactionIndex(id);
            Require(f >= 0 || id == "-", "unknown escrow faction");
            return f;
        };
        a.payer = faction();
        a.payee = faction();
        a.cargo = row.Number(0, 1);
        a.payment = row.Number(0, .5f);
        Require(a.requester != a.supplier && a.deadline >= a.due,
                "invalid agreement participants/timing");
        Require(a.stage != 2 ||
                    (a.payer >= 0 && a.payee >= 0 && a.cargo == (a.kind == 2 ? 0.f : 1.f)),
                "invalid escrow");
        Require(a.stage == 2 || a.cargo == 0, "unreserved cargo");
        agreements.push_back(a);
    }
    else
    {
        Require(false, "unknown life checkpoint record");
    }
    return true;
}

void WorldSociety::FinishLifeRestore()
{
    using ContentData::Require;
    if (!HasLife())
    {
        return;
    }
    for (const auto& p : places)
    {
        Require(restoreSeen.count("life_place:" + p.id) != 0, "incomplete life places");
    }
    std::set<std::pair<int, int>> occupied;
    for (std::size_t i = 0; i < npcs.size(); ++i)
    {
        const auto& l = lives[i];
        Require(restoreSeen.count("life_npc:" + npcs[i].id) != 0 && l.cause < serial,
                "incomplete life plans");
        if (npcs[i].Alive() && l.task != LifeTask::None && l.place >= 0)
        {
            Require(l.slot >= 0 && occupied.insert({l.place, l.slot}).second,
                    "missing or duplicate reserved activity slot");
        }
        if (l.agreement)
        {
            auto a = std::find_if(agreements.begin(), agreements.end(),
                                  [&](const Agreement& a)
                                  {
                                      return a.id == l.agreement;
                                  });
            Require(a != agreements.end() && l.task == LifeTask::Meet &&
                        ((a->requester == int(i) && a->supplier == l.partner) ||
                         (a->supplier == int(i) && a->requester == l.partner)),
                    "dangling agreement");
        }
        else
        {
            Require(l.task != LifeTask::Meet, "meeting without agreement");
        }
    }
    for (const auto& a : agreements)
    {
        Require(a.id > 0 && a.id < serial && lives[a.requester].agreement == a.id &&
                    lives[a.supplier].agreement == a.id,
                "inconsistent bilateral agreement");
    }
}
