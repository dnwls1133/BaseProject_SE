#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "RandomLevelMap.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <limits>
#include <set>

float WorldSociety::Trust(int from, int to) const
{
    for (const auto& r : relations)
    {
        if (r.from == from && r.to == to)
        {
            return r.values[0];
        }
    }
    return npcs[from].faction == npcs[to].faction ? 20.f : 0.f;
}

void WorldSociety::Relate(int from, int to, float trust, float fear, const std::string& reason,
                          std::uint64_t cause)
{
    if (from == to || from < 0 || to < 0)
    {
        return;
    }
    auto found = std::find_if(relations.begin(), relations.end(),
                              [&](const Relation& r)
                              {
                                  return r.from == from && r.to == to;
                              });
    if (found == relations.end())
    {
        Relation r;
        r.from = from;
        r.to = to;
        r.values[0] = Trust(from, to);
        relations.push_back(r);
        found = relations.end() - 1;
    }
    const float before = found->values[0];
    found->values[0] = std::clamp(before + trust, -100.f, 100.f);
    found->values[1] = std::clamp(found->values[1] + trust * .5f, -100.f, 100.f);
    found->values[2] = std::clamp(found->values[2] + trust * .25f, -100.f, 100.f);
    found->values[3] = std::clamp(found->values[3] + fear, 0.f, 100.f);
    found->values[4] = std::clamp(found->values[4] + (trust > 0 ? trust * .2f : 0), 0.f, 100.f);
    found->values[5] =
        100 *
        (1 - std::abs(factions[npcs[from].faction].ideology - factions[npcs[to].faction].ideology));
    Record("relationship", npcs[from].id, npcs[to].id, std::to_string(before),
           std::to_string(found->values[0]), reason, cause);
}

std::vector<int> WorldSociety::Nearby(const Npc& npc) const
{
    std::vector<int> result;
    const int bx = static_cast<int>(npc.x / 24), by = static_cast<int>(npc.y / 24);
    for (int y = by - 1; y <= by + 1; ++y)
    {
        for (int x = bx - 1; x <= bx + 1; ++x)
        {
            const auto bucket = neighborhood.find(x + y * 1024);
            if (bucket == neighborhood.end())
            {
                continue;
            }
            for (int i : bucket->second)
            {
                if (&npcs[i] != &npc && npcs[i].Alive() &&
                    std::hypot(npcs[i].x - npc.x, npcs[i].y - npc.y) <= rules.talkRange)
                {
                    result.push_back(i);
                }
            }
        }
    }
    return result;
}

void WorldSociety::Observe(int index, WorldSimulation& world)
{
    auto& npc = npcs[index];
    const auto* state = world.FindRegion(regions[npc.region].id);
    if (!state)
    {
        return;
    }
    auto it = std::find_if(npc.memories.begin(), npc.memories.end(),
                           [&](const Memory& m)
                           {
                               return m.region == npc.region;
                           });
    if (it != npc.memories.end() && it->source != index && minute >= it->learned &&
        minute - it->learned < 180 && std::abs(it->food - static_cast<float>(state->food)) > .3f)
    {
        if (it->source >= 0)
        {
            Relate(index, it->source, -8, 0, "전달받은 정보가 관찰과 다름");
        }
        else
        {
            npc.playerRelation[0] = std::max(-100.f, npc.playerRelation[0] - 15);
            Record("rumor_disproved", npc.id, "player", std::to_string(it->food),
                   std::to_string(state->food), "거짓 소식 발견으로 신뢰 하락");
        }
    }
    Memory memory{
        npc.region, index, static_cast<float>(state->food), static_cast<float>(state->infection),
        1,          minute};
    if (it == npc.memories.end())
    {
        npc.memories.push_back(memory);
    }
    else
    {
        *it = memory;
    }
}

void WorldSociety::Send(int from, int to, int kind, int region, WorldSimulation& world,
                        std::uint64_t cause)
{
    if (from == to || !npcs[from].Alive() || !npcs[to].Alive() ||
        std::hypot(npcs[from].x - npcs[to].x, npcs[from].y - npcs[to].y) > 2.5f)
    {
        return;
    }
    if (messages.size() >= 256)
    {
        Profile::Add(Profile::Metric::SocialMessageDropped);
        return;
    }
    // Only known facts travel. A messenger is not an omniscient world query.
    const auto& sender = npcs[from];
    auto known = std::find_if(sender.memories.begin(), sender.memories.end(),
                              [&](const Memory& m)
                              {
                                  return m.region == region;
                              });
    if (known == sender.memories.end())
    {
        return;
    }
    for (const auto& pending : messages)
    {
        if (pending.from == from && pending.to == to && pending.kind == kind &&
            pending.region == region)
        {
            return;
        }
    }
    Message message;
    message.id = serial++;
    message.cause = cause;
    message.from = from;
    message.to = to;
    message.region = region;
    message.kind = kind;
    message.due = minute;
    message.expires = minute + rules.messageLifetime;
    message.observed = known->learned;
    message.food = known->food;
    message.infection = known->infection;
    message.confidence = known->confidence * .9f;
    messages.push_back(message);
    Profile::Add(Profile::Metric::SocialMessageSent);
    Record("message_sent", sender.id, npcs[to].id, "none", std::to_string(message.id),
           kind == 1   ? "식량 지원 요청 전달"
           : kind == 2 ? "교역 제안 전달"
                       : "지역 소식 전달",
           cause);
    (void)world;
}

void WorldSociety::Deliver(WorldSimulation& world, const RandomLevelMap& map)
{
    PROFILE_SCOPE(SocialMessages);
    int budget = rules.messageBudget;
    for (auto it = messages.begin(); it != messages.end() && budget > 0;)
    {
        if (it->due > minute)
        {
            ++it;
            continue;
        }
        --budget;
        const Message message = *it;
        it = messages.erase(it);
        if (message.expires < minute || !npcs[message.from].Alive() || !npcs[message.to].Alive())
        {
            Record("message_cancel", npcs[message.from].id, npcs[message.to].id,
                   std::to_string(message.id), "cancelled", "전달 기한 만료 또는 당사자 사망",
                   message.id);
            continue;
        }
        if (!NewsContact(message.from, message.to, map))
        {
            Record("message_cancel", npcs[message.from].id, npcs[message.to].id,
                   std::to_string(message.id), "cancelled", "실제 대면 없이 소식 전달 불가");
            continue;
        }
        Profile::Add(Profile::Metric::NewsDelivered);
        auto& receiver = npcs[message.to];
        const float credibility =
            message.confidence + Trust(message.to, message.from) / 200 - receiver.traits[3] * .4f;
        if (credibility < .25f)
        {
            if (HasLife())
            {
                continue;
            }
            receiver.speech = "그 소식은 직접 확인하겠소.";
            receiver.speechTime = 5;
            receiver.destination = message.region;
            receiver.action = Action::Research;
            continue;
        }
        Memory memory{message.region,
                      message.from,
                      message.food,
                      message.infection,
                      std::clamp(credibility, 0.f, 1.f),
                      message.observed};
        auto old = std::find_if(receiver.memories.begin(), receiver.memories.end(),
                                [&](const Memory& m)
                                {
                                    return m.region == memory.region;
                                });
        if (old == receiver.memories.end())
        {
            receiver.memories.push_back(memory);
        }
        else if (old->learned < message.observed)
        {
            *old = memory;
        }
        Relate(message.to, message.from, 1, 0, "정보를 전달받음", message.id);
        if (HasLife())
        {
            lives[message.to].lastMemory = npcs[message.from].name + "에게서 " +
                                           regions[message.region].name + "의 소식을 전달받음";
            continue;
        }
        if (receiver.action == Action::Combat || receiver.wildTarget >= 0)
        {
            continue;
        }
        if (message.kind == 1 && receiver.food > 2 &&
            receiver.traits[2] + Trust(message.to, message.from) / 100 > receiver.traits[1])
        {
            receiver.action = Action::Aid;
            receiver.target = message.from;
            receiver.destination = message.region;
            receiver.nextDecision = minute + rules.decisionPeriod * 4;
            receiver.speech = "식량을 나누겠소. 기다리시오.";
        }
        else if (message.kind == 2 && receiver.food < 5)
        {
            receiver.action = Action::Trade;
            receiver.target = message.from;
            receiver.destination = message.region;
            receiver.nextDecision = minute + rules.decisionPeriod * 4;
            receiver.speech = "조건이 맞으면 거래하겠소.";
        }
        else
        {
            receiver.speech = "소식을 기억하겠소.";
        }
        receiver.speechTime = 5;
        Record("message_received", npcs[message.from].id, receiver.id, "unknown",
               ActionName(receiver.action), "정보가 기억과 판단에 반영됨", message.id);
    }
    (void)world;
}

void WorldSociety::Decide(int index, WorldSimulation& world, const RandomLevelMap& map)
{
    if (conflictEnabled)
    {
        return;
    }

    PROFILE_SCOPE(NpcDecision);
    Profile::Add(Profile::Metric::NpcDecisions);
    auto& npc = npcs[index];
    if (!npc.Alive() || npc.action == Action::Escort || npc.windup > 0 || npc.wildTarget >= 0)
    {
        return;
    }
    Observe(index, world);
    const auto nearby = Nearby(npc);
    int threat = -1, partner = -1;
    float danger = 0, threatScore = -1;
    for (int other : nearby)
    {
        const auto& person = npcs[other];
        const float hostility =
            diplomacy[npc.faction][person.faction] + PersonalRegard(index, other) * .4f;
        const float distance = std::hypot(person.x - npc.x, person.y - npc.y);
        const float urgency =
            20.f / (distance + 1) +
            (person.target == index && person.action == Action::Combat ? 10.f : 0.f);
        const bool assault = person.action == Action::Combat && person.target >= 0 &&
                             (person.target == index ||
                              (npc.role == "guard" && npcs[person.target].faction == npc.faction));
        const bool retaliation = npc.action == Action::Combat && npc.target == other;
        if ((hostility < rules.hostility || assault || retaliation) && urgency > threatScore &&
            map.Visible(npc.x, npc.y, person.x, person.y))
        {
            threat = other;
            danger = person.health;
            threatScore = urgency;
        }
        if (partner < 0 || PersonalRegard(index, other) > PersonalRegard(index, partner))
        {
            partner = other;
        }
    }
    if (HasLife() && threat < 0)
    {
        if (!CampaignOrder(index))
        {
            npc.action = lives[index].task == LifeTask::None ? Action::Rest : Action::Travel;
            npc.target = -1;
        }
        return;
    }
    if (threat < 0 && (npc.action == Action::Aid || npc.action == Action::Trade) &&
        npc.target >= 0 && npcs[npc.target].Alive() && npc.food > 2 &&
        std::hypot(npc.x - npcs[npc.target].x, npc.y - npcs[npc.target].y) > 2)
    {
        return;
    }
    if (npc.nextMessage <= minute && partner >= 0)
    {
        Send(index, partner, npc.food < 2 ? 1 : npc.role == "merchant" ? 2 : 0, npc.region, world);
        const int leader = factions[npc.faction].leader;
        if (leader >= 0 && leader != index && npcs[leader].Alive())
        {
            Send(index, leader, 0, npc.region, world);
        }
        npc.nextMessage = minute + 30;
    }
    if (threat < 0 && (StoryOrder(index) || CampaignOrder(index)))
    {
        return;
    }
    std::array<float, 10> score = rules.weights;
    score[0] += (100 - npc.health) * .015f;
    score[1] += npc.food < 2 ? .9f : .1f;
    score[2] += npc.traits[1] + (npc.role == "merchant" ? .8f : 0);
    score[3] += npc.traits[2] + (npc.role == "healer" ? .7f : 0);
    score[4] += npc.traits[0] * .7f + (npc.role == "guard" ? .8f : 0);
    score[5] += npc.traits[5] + (npc.role == "scholar" ? .8f : 0);
    score[6] += npc.traits[1] + npc.traits[4] - npc.traits[2];
    score[7] += (1 - npc.traits[0]) + (100 - npc.health) * .03f;
    score[8] += npc.traits[0] + npc.traits[4] - npc.traits[2] * .5f;
    if (npc.goal == "rule")
    {
        score[4] += 1.2f;
        score[3] += .4f;
    }
    if (npc.goal == "protect")
    {
        score[4] += 1;
        score[3] += .3f;
        score[8] += .3f;
    }
    if (npc.goal == "profit")
    {
        score[2] += 1;
    }
    if (npc.goal == "aid")
    {
        score[3] += 1;
        score[0] += .8f;
    }
    if (npc.goal == "research")
    {
        score[5] += 1;
    }
    if (npc.goal == "purify")
    {
        score[4] += .8f;
        score[8] += .8f;
    }
    if (npc.goal == "connect")
    {
        score[3] += .7f;
        score[5] += .5f;
    }
    if (npc.goal == "survive")
    {
        score[1] += .5f;
        score[7] += .6f;
    }
    StoryInfluence(index, score);
    score[9] = -100;
    if (partner < 0 ||
        (partner >= 0 && diplomacy[npc.faction][npcs[partner].faction] < rules.hostility &&
         !(npc.role == "healer" && PersonalRegard(index, partner) >= 15)))
    {
        score[2] = score[3] = -100;
    }
    else
    {
        if (npcs[partner].health >= 90 && npcs[partner].food >= 3)
        {
            score[3] = -100;
        }
        if (npc.food <= 2 || npcs[partner].food >= 8)
        {
            score[2] = -100;
        }
    }
    if (threat < 0)
    {
        score[6] = score[7] = score[8] = -100;
    }
    else
    {
        for (int action = 0; action < 7; ++action)
        {
            score[action] = -100;
        }
        score[8] += npc.role == "guard" ? .8f : 0;
        score[7] += 2 + danger / std::max(1.f, npc.health) - 1;
        score[8] += 2 + npc.health / std::max(1.f, danger) - 1;
    }
    for (auto& value : score)
    {
        value += static_cast<float>(Random() % 100) * .00001f;
    }
    Action next = static_cast<Action>(std::max_element(score.begin(), score.end()) - score.begin());
    if (next == Action::Flee && npc.health < 25 && npc.traits[0] < .4f && threat >= 0)
    {
        // Surrender changes relations; it does not erase personal memories or automatically join.
        Relate(index, threat, -2, 20, "항복을 요청함");
        if (npcs[threat].traits[2] > .55f)
        {
            diplomacy[npc.faction][npcs[threat].faction] = -20;
            diplomacy[npcs[threat].faction][npc.faction] = -20;
            npc.speech = "무기를 내려놓겠소.";
            npc.speechTime = 6;
        }
    }
    if (next != npc.action)
    {
        const auto decision =
            Record("npc_decision", npc.id, factions[npc.faction].id, ActionName(npc.action),
                   ActionName(next), "성향·직책·욕구·관계·인지한 위협 평가");
        std::string detail = "npc_id=" + npc.id + " long_term_goal=" + npc.goal +
                             " cause_id=" + std::to_string(decision) +
                             " world_minute=" + std::to_string(minute) +
                             " selected_action_id=" + std::to_string(static_cast<int>(next)) +
                             " food_units=" + std::to_string(npc.food) +
                             " health_points=" + std::to_string(npc.health) +
                             " known_threat_id=" + (threat >= 0 ? npcs[threat].id : "none");
        for (std::size_t i = 0; i < score.size(); ++i)
        {
            detail += " action_" + std::to_string(i) + "_score=" + std::to_string(score[i]);
        }
        Profile::Event("npc_decision_scores", detail);
    }
    npc.action = next;
    npc.target = next == Action::Combat || next == Action::Raid || next == Action::Flee ? threat
                 : next == Action::Aid || next == Action::Trade                         ? partner
                                                                                        : -1;
    if (next == Action::Research || next == Action::Travel)
    {
        npc.destination = npc.home;
        float best = -1;
        for (const auto& memory : npc.memories)
        {
            if (minute > memory.learned + 720)
            {
                continue;
            }
            const float value =
                next == Action::Research ? memory.infection * npc.traits[5] : memory.food;
            if (value > best)
            {
                best = value;
                npc.destination = memory.region;
            }
        }
        if (next == Action::Research && npc.destination == npc.region)
        {
            npc.destination = static_cast<int>((npc.region + 1) % regions.size());
        }
    }
    if (next == Action::Flee || next == Action::Rest)
    {
        npc.destination = npc.home;
    }
    if (next == Action::Patrol)
    {
        npc.destination = minute / 60 % 2 ? npc.home : factions[npc.faction].home;
    }
}
