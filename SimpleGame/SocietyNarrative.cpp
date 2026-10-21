#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "RandomLevelMap.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

const WorldSociety::Belief* WorldSociety::KnownStory(int npc, int fact) const
{
    const auto& knowledge = npc < 0 ? playerBeliefs : storyBeliefs[npc];
    for (const auto& b : knowledge)
    {
        if (b.fact == fact)
        {
            return &b;
        }
    }
    return nullptr;
}

bool WorldSociety::LearnStory(int npc, const Belief& evidence, int sender)
{
    if (npc >= 0 && !npcs[npc].Alive())
    {
        return false;
    }
    auto& knowledge = npc < 0 ? playerBeliefs : storyBeliefs[npc];
    auto old = std::find_if(knowledge.begin(), knowledge.end(),
                            [&](const Belief& b)
                            {
                                return b.fact == evidence.fact;
                            });
    const std::string before = old == knowledge.end()
                                   ? "unknown"
                                   : "claim=" + std::to_string(old->claim) +
                                         " confidence=" + std::to_string(old->confidence) +
                                         " observed_minute=" + std::to_string(old->learned);
    if (old != knowledge.end())
    {
        if (old->claim == evidence.claim && evidence.origin == npc && sender == npc &&
            old->subject == evidence.subject)
        {
            if (evidence.learned >= old->learned + 60)
            {
                *old = evidence;
            }
            return false;
        }
        if (evidence.learned < old->learned ||
            (old->claim == evidence.claim && old->confidence >= evidence.confidence &&
             evidence.learned <= old->learned) ||
            (old->claim != evidence.claim && evidence.confidence < .55f))
        {
            return false;
        }
        *old = evidence;
    }
    else
    {
        knowledge.push_back(evidence);
    }
    const std::string receiver = npc < 0 ? "player" : npcs[npc].id;
    Record("story_knowledge", sender < 0 ? "player" : npcs[sender].id, receiver, before,
           storyFacts[evidence.fact].id + " claim=" + std::to_string(evidence.claim) +
               " confidence=" + std::to_string(evidence.confidence) +
               " observed_minute=" + std::to_string(evidence.learned) + " original_source=" +
               (evidence.origin < 0 ? std::string("player") : npcs[evidence.origin].id),
           evidence.claim > 0 ? "출처가 있는 관찰/보고를 받아들임"
                              : "새 관찰에 따라 이전 판단을 정정함");
    if (npc >= 0)
    {
        npcs[npc].nextDecision = minute;
    }
    return true;
}

void WorldSociety::SendStory(int from, int to, int fact)
{
    if (from == to || !npcs[from].Alive() || !npcs[to].Alive() || storyLetters.size() >= 128)
    {
        return;
    }
    const auto* known = KnownStory(from, fact);
    const auto* received = KnownStory(to, fact);
    if (!known ||
        (received && received->claim == known->claim && received->learned >= known->learned))
    {
        return;
    }
    if (std::any_of(storyLetters.begin(), storyLetters.end(),
                    [&](const StoryLetter& l)
                    {
                        return l.from == from && l.to == to && l.evidence.fact == fact;
                    }))
    {
        return;
    }
    const float distance = std::hypot(npcs[from].x - npcs[to].x, npcs[from].y - npcs[to].y);
    if (distance > 2.5f)
    {
        return;
    }
    StoryLetter l;
    l.id = Record("story_dispatch", npcs[from].id, npcs[to].id, "known", storyFacts[fact].id,
                  "출처를 보존한 보고 전달 예약");
    l.from = from;
    l.to = to;
    l.evidence = *known;
    l.due = minute;
    l.expires = l.due + 360;
    storyLetters.push_back(l);
}

void WorldSociety::ObserveBroodConflict(Point point, const RandomLevelMap& map, Point player)
{
    for (std::size_t f = 0; f < storyFacts.size(); ++f)
    {
        if (storyFacts[f].observation != "rivals")
        {
            continue;
        }
        if (std::hypot(player.x - point.x, player.y - point.y) < rules.talkRange &&
            map.Visible(player.x, player.y, point.x, point.y))
        {
            LearnStory(-1, {int(f), -1, -1, 1, .95f, minute}, -1);
        }
        for (std::size_t i = 0; i < npcs.size(); ++i)
        {
            const auto& n = npcs[i];
            if (n.Alive() && std::hypot(n.x - point.x, n.y - point.y) < rules.talkRange &&
                map.Visible(n.x, n.y, point.x, point.y))
            {
                LearnStory(int(i), {int(f), int(i), -1, 1, .95f, minute}, int(i));
            }
        }
    }
}

bool WorldSociety::StoryOrder(int index)
{
    if (HasLife())
    {
        return false;
    }
    auto& n = npcs[index];
    if (!n.Alive() || n.wildTarget >= 0 || n.playerThreat || n.action == Action::Escort ||
        n.action == Action::Combat || n.action == Action::Flee || n.windup > 0 || n.health < 30)
    {
        return false;
    }
    for (const auto& s : storyIncidents)
    {
        if (s.stage != 1)
        {
            continue;
        }
        if (s.kind == "ration" && s.actor == index && s.step <= 3)
        {
            n.action = Action::Travel;
            n.target = -1;
            n.destination = s.step >= 2 ? s.region : npcs[s.partner].home;
            return true;
        }
        if (s.kind == "signal" && (s.actor == index || s.partner == index) && s.patient >= 0 &&
            npcs[s.patient].Alive())
        {
            n.action = s.patient == index ? Action::Rest : Action::Aid;
            n.target = s.patient == index ? -1 : s.patient;
            n.destination = npcs[s.patient].region;
            return true;
        }
        if (s.kind == "ledger" && s.actor == index)
        {
            n.action = Action::Research;
            n.target = -1;
            n.destination = s.region;
            return true;
        }
    }
    return false;
}

void WorldSociety::FinishStory(StoryIncident& s, int stage, const std::string& reason,
                               WorldSimulation& world)
{
    if (s.cause == 0)
    {
        s.cause =
            Record("story_candidate_closed", npcs[s.actor].id, s.id, "waiting", "closed", reason);
    }
    if (s.cargo > 0)
    {
        // Unspent escrow returns to its supplier; a dead carrier loses it at the site.
        if (npcs[s.actor].Alive())
        {
            factions[s.supplier].supplies += s.cargo / .03f;
        }
        Record("story_cargo", npcs[s.actor].id, s.id, std::to_string(s.cargo), "0",
               npcs[s.actor].Alive() ? "남은 운송 식량을 공급자에게 반환"
                                     : "운송자 사망으로 실린 식량 유실",
               s.cause);
        s.cargo = 0;
        s.supplier = -1;
    }
    s.stage = stage;
    s.outcome = reason;
    Record("story_outcome", npcs[s.actor].id, s.id, "active", stage == 2 ? "resolved" : "failed",
           reason, s.cause);
    for (int n : {s.actor, s.partner})
    {
        npcs[n].nextDecision = minute;
        if (npcs[n].Alive() && npcs[n].wildTarget < 0 && npcs[n].action != Action::Combat)
        {
            npcs[n].action = Action::Rest;
            npcs[n].target = -1;
            npcs[n].destination = npcs[n].home;
        }
    }
    for (auto& q : quests)
    {
        if (q.cause == s.cause &&
            static_cast<int>(q.state) < static_cast<int>(QuestState::Completed))
        {
            if (stage != 2 || q.progress == 0)
            {
                ChangeQuest(q, stage == 2 ? QuestState::Replaced : QuestState::Failed, reason);
            }
        }
    }
    if (stage == 3)
    {
        regions[s.region].unrest = std::min(1.f, regions[s.region].unrest + .03f);
    }
    (void)world;
}

void WorldSociety::AdvanceStory(StoryIncident& s, WorldSimulation& world, const RandomLevelMap& map)
{
    auto& actor = npcs[s.actor];
    auto& partner = npcs[s.partner];
    const auto* known = KnownStory(s.actor, s.trigger);
    if (s.stage == 0)
    {
        if (!actor.Alive() || !partner.Alive())
        {
            FinishStory(s, 3, "관련 인물 사망으로 사건 후보 종료", world);
            return;
        }
        if (!known || known->claim < 0 || minute > known->learned + 1440 ||
            known->confidence < .5f ||
            (s.kind == "signal" && (known->subject < 0 || !npcs[known->subject].Alive())))
        {
            return;
        }
        if (std::any_of(storyIncidents.begin(), storyIncidents.end(),
                        [&](const StoryIncident& other)
                        {
                            return &other != &s && other.stage == 1 &&
                                   (other.actor == s.actor || other.partner == s.actor ||
                                    other.actor == s.partner || other.partner == s.partner);
                        }))
        {
            return;
        }
        s.stage = 1;
        s.patient = known->subject;
        s.deadline = minute + s.lifetime;
        s.cause = Record("story_started", actor.id, s.id, "condition", "active", s.title);
        s.outcome = "관찰된 문제를 보고하고 가능한 대응을 찾는 중";
        SendStory(s.actor, s.partner, s.trigger);
        if (quests.size() < 96)
        {
            Quest q;
            q.id = serial++;
            q.cause = s.cause;
            q.issuer = s.actor;
            q.region = s.region;
            q.deadline = s.deadline;
            q.kind = s.kind == "ration"   ? QuestKind::Supply
                     : s.kind == "signal" ? QuestKind::Heal
                                          : QuestKind::Investigate;
            q.target = s.kind == "signal" ? s.patient : -1;
            quests.push_back(q);
        }
    }
    if (s.stage != 1)
    {
        return;
    }
    if (!actor.Alive() || !partner.Alive() || minute >= s.deadline)
    {
        FinishStory(s, 3,
                    !actor.Alive() || !partner.Alive() ? "담당자 사망으로 계획 중단"
                                                       : "기한 내 해결하지 못해 철수",
                    world);
        return;
    }
    Profile::Add(Profile::Metric::StoryDecisions);
    SendStory(s.partner, s.actor, s.proof);
    SendStory(s.actor, s.partner, s.proof);
    const auto* proof = KnownStory(s.actor, s.proof);
    const auto* otherProof = KnownStory(s.partner, s.proof);
    const bool corroborated = proof && otherProof && proof->claim > 0 && otherProof->claim > 0 &&
                              proof->confidence >= .55f && otherProof->confidence >= .55f;
    auto arrived = [&](const Npc& n, int region)
    {
        return std::hypot(n.x - regions[region].x, n.y - regions[region].y) < 5;
    };
    if (s.kind == "ration")
    {
        const auto* region = world.FindRegion(regions[s.region].id);
        if (region && region->food >= .55)
        {
            FinishStory(s, 2, "배급 상황이 회복되어 운송 계획을 정리함", world);
            return;
        }
        if (s.step < 2 && arrived(actor, partner.home) && actor.action != Action::Combat &&
            actor.wildTarget < 0)
        {
            const auto* report = KnownStory(s.partner, s.trigger);
            auto& seller = factions[partner.faction];
            auto& buyer = factions[actor.faction];
            if (!report)
            {
                SendStory(s.actor, s.partner, s.trigger);
                return;
            }
            if (PersonalRegard(s.partner, s.actor) > -25 && seller.supplies >= 3 &&
                buyer.supplies >= .5f)
            {
                seller.supplies -= 3;
                buyer.supplies -= .5f;
                seller.supplies += .5f;
                s.cargo = .09f;
                s.supplier = partner.faction;
                actor.speech = "배급 수레를 북부로 옮깁니다.";
                actor.speechTime = 8;
                s.step = 2;
                s.outcome = "교단 비축 식량을 교환하고 피난지로 운송 중";
                Record("story_exchange", actor.id, partner.id, "seller_food=3 buyer_payment=0.5",
                       "cargo_food=0.09", s.outcome, s.cause);
                Relate(s.actor, s.partner, 3, 0, "실제 물자를 교환함", s.cause);
            }
            else
            {
                s.step = 3;
                s.outcome = "교환 조건이 맞지 않아 피난지 비축분 재배분을 검토";
                actor.destination = s.region;
                actor.action = Action::Travel;
                Record("story_ration_plan", actor.id, s.id, "trade", "ration", s.outcome, s.cause);
            }
        }
        else if (s.step == 2 && arrived(actor, s.region) && actor.action != Action::Combat &&
                 actor.wildTarget < 0)
        {
            const double room = region ? std::max(0.0, 1 - region->food) : 0;
            const float delivered = std::min(s.cargo, static_cast<float>(room));
            world.ChangeRegion(regions[s.region].id, delivered, 0, actor.id, "운송 식량 도착",
                               s.cause);
            s.cargo -= delivered;
            regions[s.region].unrest = std::max(0.f, regions[s.region].unrest - .03f);
            FinishStory(s, 2, "수레 식량이 도착함 · 다음 배급 문제는 자원 상태에 따라 남음", world);
            return;
        }
        else if (s.step == 3 && arrived(actor, s.region) && actor.action != Action::Combat &&
                 actor.wildTarget < 0)
        {
            float units = 0;
            for (auto& n : npcs)
            {
                if (n.Alive() && n.region == s.region && n.food > 3 && n.faction == actor.faction &&
                    n.traits[2] > .4f && units < 3 && region &&
                    region->food + (units + 1) * .03 <= 1)
                {
                    n.food -= 1;
                    units += 1;
                }
            }
            const float accepted = region ? std::min(units * .03f, float(1 - region->food)) : 0;
            world.ChangeRegion(regions[s.region].id, accepted, 0, actor.id,
                               "동의한 주민의 여유 식량 재배분", s.cause);
            regions[s.region].unrest = std::min(1.f, regions[s.region].unrest + .02f);
            FinishStory(s, units > 0 ? 2 : 3,
                        units > 0 ? "비축분을 나눴으나 배급 불만이 남음"
                                  : "나눌 비축분도 부족하여 계획 중단",
                        world);
            return;
        }
    }
    else if (s.kind == "signal")
    {
        if (s.patient < 0 || !npcs[s.patient].Alive())
        {
            FinishStory(s, 3, "부상자가 사망하여 공동 진료 중단", world);
            return;
        }
        auto& patient = npcs[s.patient];
        if (patient.health >= 90 && s.step == 0)
        {
            FinishStory(s, 2, "다른 치료로 부상자가 회복됨 · 신호의 원인은 단정하지 않음", world);
            return;
        }
        const bool together = actor.wildTarget < 0 && partner.wildTarget < 0 &&
                              actor.action != Action::Combat && partner.action != Action::Combat &&
                              std::hypot(actor.x - patient.x, actor.y - patient.y) < 3 &&
                              std::hypot(partner.x - patient.x, partner.y - patient.y) < 3 &&
                              map.Visible(actor.x, actor.y, partner.x, partner.y);
        if (together && actor.food >= 1 && partner.food >= 1 && s.step == 0)
        {
            actor.food -= 1;
            partner.food -= 1;
            actor.speech = "상처와 목격 기록을 비교합시다.";
            actor.speechTime = 8;
            const float before = patient.health;
            patient.health = std::min(100.f, patient.health + 25);
            s.step = 1;
            Record("story_joint_care", actor.id, patient.id, std::to_string(before),
                   std::to_string(patient.health), "양측 의료진이 식량을 소비해 공동 진료",
                   s.cause);
            Relate(s.actor, s.partner, 8, -3, "부상자 진료 기록 비교", s.cause);
            Relate(s.partner, s.actor, 8, -3, "공동 진료를 직접 확인", s.cause);
        }
        if (s.step == 1)
        {
            const int leaderA = factions[actor.faction].leader,
                      leaderB = factions[partner.faction].leader;
            if (leaderA >= 0)
            {
                SendStory(s.actor, leaderA, s.proof);
            }
            if (leaderB >= 0)
            {
                SendStory(s.partner, leaderB, s.proof);
            }
            if (corroborated)
            {
                regions[s.region].fear = std::max(0.f, regions[s.region].fear - .05f);
                FinishStory(s, 2,
                            "경쟁 군집 목격과 공동 진료를 보고함 · 지휘관의 판단은 별도로 이어짐",
                            world);
            }
            else
            {
                FinishStory(s, 2, "부상자는 치료했지만 신호의 원인은 미확인 · 세력 적대는 유지",
                            world);
            }
            return;
        }
    }
    else if (s.kind == "ledger")
    {
        if (actor.wildTarget >= 0 || actor.action == Action::Combat || actor.action == Action::Flee)
        {
            return;
        }
        if (!arrived(actor, s.region))
        {
            StoryOrder(s.actor);
            return;
        }
        if (s.step == 0 && actor.food >= 1)
        {
            actor.food -= 1;
            s.step = 1;
            actor.speech = "빈 페이지의 뜻을 단정할 수 없소.";
            actor.speechTime = 8;
            s.outcome = "불완전한 원문을 보존하고 의료진에게 대조 기록을 요청함";
            Record("story_preservation", actor.id, s.id, "wet_pages", "preserved", s.outcome,
                   s.cause);
        }
        if (s.step == 1 && corroborated)
        {
            // Publish to a finite medical network; others must learn through later reports.
            for (std::size_t i = 0; i < npcs.size(); ++i)
            {
                if (npcs[i].Alive() && npcs[i].role == "healer" &&
                    PersonalRegard(s.actor, int(i)) >= 0)
                {
                    SendStory(s.actor, int(i), s.trigger);
                    SendStory(s.actor, int(i), s.proof);
                }
            }
            Relate(s.actor, s.partner, 6, 0, "투여 중단 위험과 원문을 함께 검토함", s.cause);
            Relate(s.partner, s.actor, 4, 0, "의료진 대상 조건부 공개", s.cause);
            FinishStory(
                s, 2,
                "원문과 임상 기록을 함께 의료진에게 전달 · 모든 감염자의 원인으로 단정하지 않음",
                world);
            return;
        }
        if (s.step == 1 && minute + s.lifetime / 3 >= s.deadline && actor.traits[4] > .7f)
        {
            SendStory(s.actor, s.partner, s.trigger);
            Relate(s.partner, s.actor, -8, 3, "대조 자료 없는 발표를 경계함", s.cause);
            FinishStory(s, 3, "검증 전 발표로 의료진의 신뢰가 낮아짐 · 환자 피해를 확정하지 않음",
                        world);
            return;
        }
    }
    StoryOrder(s.actor);
    if (s.kind == "signal")
    {
        StoryOrder(s.partner);
    }
}

void WorldSociety::StoryTick(WorldSimulation& world, const RandomLevelMap& map, Point player)
{
    PROFILE_SCOPE(StoryUpdate);
    if (storyFacts.empty() || minute < nextStoryTick)
    {
        return;
    }
    nextStoryTick = minute + 6;
    for (int observer = -1; observer < static_cast<int>(npcs.size()); ++observer)
    {
        if (observer >= 0 && !npcs[observer].Alive())
        {
            continue;
        }
        const Point p = observer < 0 ? player : Point{npcs[observer].x, npcs[observer].y};
        for (std::size_t f = 0; f < storyFacts.size(); ++f)
        {
            const auto& fact = storyFacts[f];
            const auto& region = regions[fact.region];
            if (fact.observation == "food" || fact.observation == "site")
            {
                if (std::hypot(p.x - region.x, p.y - region.y) >= 6 ||
                    !map.Visible(p.x, p.y, region.x, region.y))
                {
                    continue;
                }
                const auto* state = world.FindRegion(region.id);
                const int claim =
                    fact.observation == "food" && state && state->food > fact.threshold ? -1 : 1;
                LearnStory(observer, {int(f), observer, -1, claim, .95f, minute}, observer);
            }
            else if (fact.observation == "injury")
            {
                for (std::size_t n = 0; n < npcs.size(); ++n)
                {
                    const auto& patient = npcs[n];
                    if (patient.Alive() && patient.region == fact.region &&
                        patient.health < fact.threshold &&
                        std::hypot(p.x - patient.x, p.y - patient.y) < rules.talkRange &&
                        map.Visible(p.x, p.y, patient.x, patient.y))
                    {
                        LearnStory(observer, {int(f), observer, int(n), 1, .95f, minute}, observer);
                        break;
                    }
                }
            }
        }
    }
    int delivered = 0;
    for (auto it = storyLetters.begin(); it != storyLetters.end();)
    {
        if (minute < it->due || delivered >= rules.messageBudget)
        {
            ++it;
            continue;
        }
        const auto letter = *it;
        it = storyLetters.erase(it);
        ++delivered;
        if (minute > letter.expires || !npcs[letter.to].Alive() || !npcs[letter.from].Alive())
        {
            Record("story_report_cancelled", npcs[letter.from].id, npcs[letter.to].id, "pending",
                   "cancelled", "기한 경과 또는 전달 당사자 사망", letter.id);
            continue;
        }
        if (!NewsContact(letter.from, letter.to, map))
        {
            continue;
        }
        Belief b = letter.evidence;
        b.confidence *= std::clamp(.78f + PersonalRegard(letter.to, letter.from) * .003f -
                                       npcs[letter.to].traits[3] * .1f,
                                   .3f, 1.f);
        LearnStory(letter.to, b, letter.from);
    }
    if (minute >= nextStoryShare)
    {
        nextStoryShare = minute + 30;
        for (std::size_t i = 0; i < npcs.size(); ++i)
        {
            if (!npcs[i].Alive())
            {
                continue;
            }
            const auto nearby = Nearby(npcs[i]);
            for (const auto& b : storyBeliefs[i])
            {
                // One local recipient and one trusted correspondent per evidence per interval.
                for (int other : nearby)
                {
                    const auto* received = KnownStory(other, b.fact);
                    if (PersonalRegard(int(i), other) >= 0 &&
                        (!received || received->claim != b.claim || received->learned < b.learned))
                    {
                        SendStory(int(i), other, b.fact);
                        break;
                    }
                }
                int correspondent = -1;
                float best = 20;
                for (const auto& r : relations)
                {
                    if (r.from == int(i) && PersonalRegard(int(i), r.to) > best &&
                        !KnownStory(r.to, b.fact))
                    {
                        correspondent = r.to;
                        best = PersonalRegard(int(i), r.to);
                    }
                }
                if (correspondent >= 0)
                {
                    SendStory(int(i), correspondent, b.fact);
                }
            }
        }
    }
    for (auto& incident : storyIncidents)
    {
        if (!HasLife())
        {
            AdvanceStory(incident, world, map);
        }
        else if (incident.stage == 1 && minute >= incident.deadline)
        {
            incident.stage = 3;
            incident.outcome = "세계의 문제가 기한 안에 해결되지 않음";
            Record("story_outcome", npcs[incident.actor].id, incident.id, "active", "unresolved",
                   incident.outcome, incident.cause);
        }
    }
    Profile::Set(Profile::Metric::StoryLetters, static_cast<double>(storyLetters.size()));
}
