#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "RpgStats.h"
#include <algorithm>
#include <cmath>

void WorldSociety::ChangeQuest(Quest& quest, QuestState state, const std::string& reason)
{
    Record("quest", npcs[quest.issuer].id, std::to_string(quest.id), QuestStatus(quest.state),
           QuestStatus(state), reason, quest.cause);
    quest.state = state;
    if (quest.kind == QuestKind::Escort && quest.target >= 0)
    {
        npcs[quest.target].action = (state == QuestState::Accepted || state == QuestState::Progress)
                                        ? Action::Escort
                                        : Action::Rest;
    }
    if (static_cast<int>(state) >= static_cast<int>(QuestState::Completed) &&
        quest.kind == QuestKind::Escort && quest.target >= 0 &&
        npcs[quest.target].action == Action::Escort)
    {
        npcs[quest.target].action = Action::Rest;
    }
}

void WorldSociety::QuestTick(WorldSimulation& world, Point player)
{
    PROFILE_SCOPE(SocialQuests);
    for (auto& quest : quests)
    {
        if (static_cast<int>(quest.state) >= static_cast<int>(QuestState::Completed))
        {
            continue;
        }
        if (!npcs[quest.issuer].Alive() || (quest.target >= 0 && !npcs[quest.target].Alive()))
        {
            ChangeQuest(quest, QuestState::Failed, "관련 인물 사망");
            continue;
        }
        if (minute >= quest.deadline)
        {
            if (quest.state != QuestState::Proposed)
            {
                auto& trust = npcs[quest.issuer].playerRelation[0];
                trust = std::max(-100.f, trust - 8);
                karma[1] = std::max(-100.f, karma[1] - 2);
            }
            ChangeQuest(quest, QuestState::Expired, "문제 해결 기한 경과");
            regions[quest.region].unrest = std::min(1.f, regions[quest.region].unrest + .03f);
            continue;
        }
        const auto* state = world.FindRegion(regions[quest.region].id);
        bool resolved = quest.kind == QuestKind::Supply && state && state->food > .55;
        resolved |=
            quest.kind == QuestKind::Heal && quest.target >= 0 && npcs[quest.target].health >= 90;
        resolved |= quest.kind == QuestKind::Negotiate && quest.target >= 0 &&
                    diplomacy[npcs[quest.issuer].faction][npcs[quest.target].faction] > -15;
        if (resolved && quest.progress == 0)
        {
            ChangeQuest(quest, QuestState::Replaced, "NPC 활동으로 문제가 해소됨");
            continue;
        }
        if (quest.state == QuestState::Accepted || quest.state == QuestState::Progress)
        {
            bool atGoal = std::hypot(player.x - regions[quest.region].x,
                                     player.y - regions[quest.region].y) < 5;
            if (quest.kind == QuestKind::Investigate && atGoal)
            {
                quest.progress = 1;
            }
            if (quest.kind == QuestKind::Escort && quest.target >= 0 && atGoal &&
                std::hypot(npcs[quest.target].x - player.x, npcs[quest.target].y - player.y) < 4)
            {
                quest.progress = 1;
            }
            if (quest.progress > 0 && quest.state == QuestState::Accepted)
            {
                ChangeQuest(quest, QuestState::Progress, "목표 진행 기록");
            }
        }
    }
    // Keep completed records bounded; active requests are never silently removed.
    if (quests.size() >= 96)
    {
        auto old = std::find_if(quests.begin(), quests.end(),
                                [](const Quest& q)
                                {
                                    return static_cast<int>(q.state) >=
                                           static_cast<int>(QuestState::Completed);
                                });
        if (old != quests.end())
        {
            quests.erase(old);
        }
    }
    if (quests.size() >= 96)
    {
        return;
    }
    if (minute < nextQuestTick)
    {
        return;
    }
    nextQuestTick = minute + 30;
    for (std::size_t i = 0; i < npcs.size(); ++i)
    {
        const auto& npc = npcs[i];
        if (!npc.Alive())
        {
            continue;
        }
        bool active = false;
        for (const auto& q : quests)
        {
            if (q.issuer == static_cast<int>(i) &&
                (static_cast<int>(q.state) < static_cast<int>(QuestState::Completed) ||
                 minute < q.deadline))
            {
                active = true;
                break;
            }
        }
        if (active)
        {
            continue;
        }
        const auto* state = world.FindRegion(regions[npc.region].id);
        if (!state)
        {
            continue;
        }
        Quest q;
        q.issuer = static_cast<int>(i);
        q.region = npc.region;
        q.deadline = minute + rules.questLifetime;
        bool needed = false;
        if (state->food < .45)
        {
            q.kind = QuestKind::Supply;
            needed = true;
        }
        for (int other : Nearby(npc))
        {
            if (npcs[other].health < 65 && npc.traits[2] > .5f)
            {
                q.kind = QuestKind::Heal;
                q.target = other;
                needed = true;
                break;
            }
            if (diplomacy[npc.faction][npcs[other].faction] < rules.hostility &&
                npc.traits[2] > .4f)
            {
                q.kind = QuestKind::Negotiate;
                q.target = other;
                needed = true;
            }
        }
        if (!needed && npc.role == "scholar" && state->infection > .35)
        {
            q.kind = QuestKind::Investigate;
            q.region = (npc.region + 1) % static_cast<int>(regions.size());
            needed = true;
        }
        if (!needed && npc.role == "refugee" && regions[npc.region].security < .65f)
        {
            q.kind = QuestKind::Escort;
            q.target = static_cast<int>(i);
            q.region = npc.home;
            needed = true;
        }
        if (!needed && npc.role == "guard" && regions[npc.region].security < .6f)
        {
            q.kind = QuestKind::Defend;
            needed = true;
        }
        if (!needed)
        {
            continue;
        }
        q.id = serial++;
        q.cause = Record("quest_created", npc.id, regions[q.region].id, "none", QuestName(q.kind),
                         "지역 문제·직책·주변 인물에서 요청 생성");
        quests.push_back(q);
        Profile::Add(Profile::Metric::SocialQuestCreated);
    }
}

void WorldSociety::MonsterKilled(const std::string& region, bool playerCredit)
{
    int r = RegionIndex(region);
    if (r < 0)
    {
        return;
    }
    regions[r].security = std::min(1.f, regions[r].security + .03f);
    if (!playerCredit)
    {
        return;
    }
    karma[1] = std::min(100.f, karma[1] + 1);
    for (auto& q : quests)
    {
        if (q.region == r && q.kind == QuestKind::Defend &&
            (q.state == QuestState::Accepted || q.state == QuestState::Progress))
        {
            ++q.progress;
            ChangeQuest(q, QuestState::Progress, "지역의 변이체 위협 제거");
        }
    }
}

void WorldSociety::ToggleQuestHold()
{
    for (auto& q : quests)
    {
        if (q.state == QuestState::Held)
        {
            ChangeQuest(q, QuestState::Accepted, "보류한 요청 재개");
            return;
        }
    }
    for (auto& q : quests)
    {
        if (q.state == QuestState::Accepted || q.state == QuestState::Progress)
        {
            ChangeQuest(q, QuestState::Held, "플레이어가 요청 보류");
            return;
        }
    }
}

std::string WorldSociety::Interact(int index, int option, WorldSimulation& world, RpgStats& stats)
{
    if (index < 0 || index >= static_cast<int>(npcs.size()) || !npcs[index].Alive())
    {
        return "더 이상 대화할 수 없습니다.";
    }
    auto& npc = npcs[index];
    auto reputation = [&](float amount)
    {
        const float before = npc.playerRelation[0];
        npc.playerRelation[0] = std::clamp(before + amount, -100.f, 100.f);
        npc.playerRelation[1] = std::clamp(npc.playerRelation[1] + amount * .5f, -100.f, 100.f);
        npc.playerRelation[2] = std::clamp(npc.playerRelation[2] + amount * .25f, -100.f, 100.f);
        Record("player_relationship", "player", npc.id, std::to_string(before),
               std::to_string(npc.playerRelation[0]), "상호작용 결과 누적");
    };
    if (option == 13)
    {
        return GiveStoryEvidence(index);
    }
    if (option == 1)
    {
        if (playerFood < 2)
        {
            return "식량 2개가 필요합니다. 상인에게 거래할 수 있습니다.";
        }
        playerFood -= 2;
        world.ChangeRegion(regions[npc.region].id, .06, 0, "player", "식량 기부");
        reputation(6);
        karma[0] = std::min(100.f, karma[0] + 2);
        return "지역에 식량을 전달했습니다. 부족 사건의 조건이 달라집니다.";
    }
    if (option == 2)
    {
        if (npc.playerRelation[0] < -30)
        {
            return "당신을 믿을 수 없어 거래를 거절합니다.";
        }
        if (stats.fragments < 1 || npc.food < 3)
        {
            return "파편 1개와 상대의 여유 식량이 필요합니다.";
        }
        --stats.fragments;
        npc.food -= 2;
        playerFood += 2;
        factions[npc.faction].supplies += 1;
        reputation(2);
        Record("player_trade", "player", npc.id, "fragment=1", "food=2", "파편과 식량 교환");
        return "파편 1개로 식량 2개를 받았습니다.";
    }
    if (option == 3)
    {
        if (playerFaction == npc.faction)
        {
            playerFaction = -1;
            reputation(-5);
            karma[2] = std::min(100.f, karma[2] + 2);
            return "세력을 떠났습니다. 개인 관계와 기억은 남습니다.";
        }
        if (npc.playerRelation[0] + npc.playerRelation[2] * .2f + karma[0] * npc.traits[2] * .1f <
            10)
        {
            return "함께할 신뢰가 부족합니다. 지원이나 요청 해결로 관계를 쌓으세요.";
        }
        const int before = playerFaction;
        playerFaction = npc.faction;
        Record("player_membership", "player", factions[npc.faction].id, std::to_string(before),
               factions[npc.faction].id, "세력 가입");
        return factions[npc.faction].name + "에 합류했습니다.";
    }
    if (option == 4)
    {
        for (auto& q : quests)
        {
            if (q.issuer == index && q.state == QuestState::Proposed)
            {
                if (npc.playerRelation[0] < -40)
                {
                    return "당신에게 맡길 수 없다고 합니다.";
                }
                ChangeQuest(q, QuestState::Accepted, "플레이어가 요청 수락");
                if (q.kind == QuestKind::Escort)
                {
                    npcs[q.target].action = Action::Escort;
                }
                return std::string(QuestName(q.kind)) + " 수락 · J에서 목표와 기한을 확인하세요.";
            }
        }
        return "현재 제안할 요청이 없습니다. 세계 상황에 따라 새 요청이 생깁니다.";
    }
    if (option == 5)
    {
        for (auto& q : quests)
        {
            if (q.issuer != index ||
                (q.state != QuestState::Accepted && q.state != QuestState::Progress))
            {
                continue;
            }
            bool done = false;
            if (q.kind == QuestKind::Supply && playerFood >= 3)
            {
                playerFood -= 3;
                world.ChangeRegion(regions[q.region].id, .12, 0, "player", "식량 요청 해결",
                                   q.cause);
                done = true;
            }
            if (q.kind == QuestKind::Heal && q.target >= 0 && npcs[q.target].health >= 90)
            {
                done = true;
            }
            if ((q.kind == QuestKind::Escort || q.kind == QuestKind::Investigate ||
                 q.kind == QuestKind::Negotiate) &&
                q.progress >= 1)
            {
                done = true;
            }
            if (q.kind == QuestKind::Defend && q.progress >= 2)
            {
                done = true;
            }
            if (!done)
            {
                return "목표가 충족되지 않았습니다. J의 요청 내용을 확인하세요.";
            }
            ChangeQuest(q, QuestState::Completed, "플레이어가 결과 보고");
            stats.AddExperience(35);
            stats.fragments += 2;
            reputation(10);
            karma[0] = std::min(100.f, karma[0] + 3);
            return "요청 완료 · 경험치 35, 파편 2개. 세계와 관계에 결과가 남습니다.";
        }
        return "보고할 요청이 없습니다.";
    }
    if (option == 6)
    {
        const float persuasion = npc.playerRelation[0] + npc.playerRelation[2] * .5f +
                                 karma[0] * .3f + karma[1] * .2f - karma[5] * npc.traits[3] * .3f +
                                 (karma[3] - karma[4]) * factions[npc.faction].ideology * .1f;
        if (persuasion < 8 + npc.traits[4] * 12)
        {
            return "신뢰와 평판이 부족하여 협상을 거절합니다.";
        }
        if (npc.playerThreat)
        {
            npc.playerThreat = false;
            npc.windup = 0;
            npc.action = Action::Rest;
            reputation(3);
            Record("personal_truce", "player", npc.id, "hostile", "truce", "개인 휴전 합의");
            return "당신에 대한 공격을 멈추기로 합의했습니다.";
        }
        for (auto& q : quests)
        {
            if (q.kind == QuestKind::Negotiate && q.target == index &&
                (q.state == QuestState::Accepted || q.state == QuestState::Progress))
            {
                const int a = npcs[q.issuer].faction, b = npc.faction;
                const float old = diplomacy[a][b];
                diplomacy[a][b] = diplomacy[b][a] = std::max(-10.f, old + 35);
                q.progress = 1;
                Record("diplomacy", "player", factions[a].id + ":" + factions[b].id,
                       std::to_string(old), std::to_string(diplomacy[a][b]), "휴전 협상", q.cause);
                return "휴전에 합의했습니다. 의뢰인에게 돌아가 보고하세요.";
            }
        }
        return "진행 중인 협상 요청의 상대가 아닙니다.";
    }
    if (option == 7)
    {
        const std::string report = StoryNews(index);
        if (!report.empty())
        {
            return report;
        }
        Observe(index, world);
        npc.speech = "내가 직접 본 것을 말해주겠소.";
        npc.speechTime = 6;
        const auto* r = world.FindRegion(regions[npc.region].id);
        return regions[npc.region].name + " · 식량 " +
               std::to_string(static_cast<int>(r->food * 100)) + "% · " +
               factions[npc.faction].motto;
    }
    if (option == 8)
    {
        if (stats.potions < 1 || npc.health >= 100)
        {
            return "회복약이 없거나 치료가 필요하지 않습니다.";
        }
        --stats.potions;
        npc.health = std::min(100.f, npc.health + 45);
        npc.infection = std::min(1.f, npc.infection + .03f);
        reputation(8);
        karma[0] = std::min(100.f, karma[0] + 2);
        karma[4] = std::min(100.f, karma[4] + 1);
        for (auto& q : quests)
        {
            if (q.target == index && q.kind == QuestKind::Heal &&
                (q.state == QuestState::Accepted || q.state == QuestState::Progress))
            {
                q.progress = 1;
            }
        }
        Record("healing", "player", npc.id, "injured", "treated", "성혈 치료로 감염 의존 증가");
        return "성혈로 치료했습니다. 생존과 감염 의존이 함께 늘었습니다.";
    }
    if (option == 10)
    {
        const int destination = (npc.region + 1) % static_cast<int>(regions.size());
        Memory rumor;
        rumor.region = destination;
        rumor.source = -1;
        rumor.food = .95f;
        rumor.infection = .05f;
        rumor.confidence = .6f;
        rumor.learned = minute;
        auto found = std::find_if(npc.memories.begin(), npc.memories.end(),
                                  [&](const Memory& m)
                                  {
                                      return m.region == destination;
                                  });
        if (npc.traits[3] > .65f || npc.playerRelation[0] < -10)
        {
            reputation(-4);
            return "근거 없는 소식이라며 믿지 않습니다.";
        }
        if (found == npc.memories.end())
        {
            npc.memories.push_back(rumor);
        }
        else
        {
            *found = rumor;
        }
        npc.destination = destination;
        npc.target = -1;
        npc.action = Action::Travel;
        npc.nextDecision = minute + 60;
        karma[5] = std::min(100.f, karma[5] + 3);
        Record("rumor", "player", npc.id, "unknown", "belief_changed",
               "풍족한 식량이 있다는 거짓 소문 전달");
        return "상대가 소문을 확인하러 갑니다. 거짓이 드러나면 신뢰를 잃습니다.";
    }
    if (option == 11)
    {
        auto& region = regions[npc.region];
        if (stats.fragments < 2 || region.blood < .05f)
        {
            return "정화에는 파편 2개와 지역의 성혈 공급이 필요합니다.";
        }
        stats.fragments -= 2;
        region.blood -= .05f;
        world.ChangeRegion(region.id, -.02, -.04, "player", "균사 식량 일부를 포기하는 정화");
        karma[3] = std::min(100.f, karma[3] + 3);
        reputation(factions[npc.faction].ideology > 0 ? 4.f : -6.f);
        return "지역 감염을 줄였지만 균사 식량과 성혈 공급도 줄었습니다.";
    }
    if (option == 12)
    {
        if (npc.traits[6] > .8f && npc.playerRelation[0] < 15)
        {
            return "자아의 경계를 지키고 싶다며 연결을 거절합니다.";
        }
        const auto* state = world.FindRegion(regions[npc.region].id);
        npc.infection = std::min(1.f, npc.infection + .02f);
        npc.playerRelation[4] = std::min(100.f, npc.playerRelation[4] + 4);
        npc.playerRelation[5] =
            std::clamp(npc.playerRelation[5] - factions[npc.faction].ideology * 3, -100.f, 100.f);
        reputation(3);
        karma[4] = std::min(100.f, karma[4] + 3);
        world.ChangeRegion(regions[npc.region].id, .01, .01, "player",
                           "동의한 주민과 군집 기억 공유");
        Record("memory_sharing", "player", npc.id, "separate", "connected",
               "동의에 따른 기억과 의존의 공유");
        return "타인의 기억과 굶주림이 전해집니다. 지역 감염은 " +
               std::to_string(static_cast<int>(state->infection * 100)) + "%입니다.";
    }
    if (option == 9)
    {
        for (auto& q : quests)
        {
            if (q.issuer == index &&
                static_cast<int>(q.state) < static_cast<int>(QuestState::Completed))
            {
                ChangeQuest(q, QuestState::Held, "요청 거절/보류 · 세계는 계속 진행");
                return "요청을 보류했습니다. 기한과 NPC 활동은 계속 진행됩니다.";
            }
        }
    }
    return "...";
}

std::vector<std::string> WorldSociety::Dialogue(int index) const
{
    if (index < 0 || index >= static_cast<int>(npcs.size()))
    {
        return {};
    }
    const auto& n = npcs[index];
    const auto& f = factions[n.faction];
    return {n.name + " · " + f.name,
            HasLife() ? (n.speechTime > 0 ? n.speech : lives[index].reason)
            : index < static_cast<int>(storyProfiles.size()) && !storyProfiles[index].motive.empty()
                ? storyProfiles[index].motive
                : f.motto,
            std::string(RoleName(n.role)) + " · 현재 행동: " + LifeLabel(index) + " · 신뢰 " +
                std::to_string(static_cast<int>(n.playerRelation[0])),
            "대화 중에도 다른 주민과 세계는 계속 움직입니다."};
}

bool WorldSociety::BeginConversation(int index)
{
    EndConversation();
    if (index < 0 || index >= int(npcs.size()))
    {
        return false;
    }
    conversationNpc = index;
    conversationTime = 45;
    if (!IsConversationActive(index))
    {
        EndConversation();
        return false;
    }
    return true;
}

void WorldSociety::EndConversation()
{
    conversationNpc = -1;
    conversationTime = 0;
}

bool WorldSociety::IsConversationActive(int index) const
{
    if (index < 0 || index >= int(npcs.size()) || conversationNpc != index || conversationTime <= 0)
    {
        return false;
    }
    const auto& n = npcs[index];
    if (!n.Alive() || n.playerThreat || n.wildTarget >= 0 || n.flash > 0 ||
        n.action == Action::Combat || n.action == Action::Raid || n.action == Action::Flee)
    {
        return false;
    }
    if (HasLife() && lives[index].agreement)
    {
        return false;
    }
    for (const auto& campaign : campaigns)
    {
        if (campaign.active && n.role == "guard" &&
            (n.faction == campaign.attacker || n.faction == campaign.defender))
        {
            return false;
        }
    }
    return true;
}

std::vector<std::pair<int, std::string>> WorldSociety::ConversationChoices(int index) const
{
    std::vector<std::pair<int, std::string>> choices;
    if (index < 0 || index >= int(npcs.size()))
    {
        return choices;
    }
    const auto& n = npcs[index];
    choices.push_back({7, "직접 본 소식과 지금 하는 일을 묻는다"});
    if (n.role == "merchant" || n.food >= 3)
    {
        choices.push_back({2, "식량 거래 조건을 묻는다"});
    }
    if (n.health < 90)
    {
        choices.push_back({8, "부상을 치료해도 되는지 묻는다"});
    }
    bool proposed = false, ongoing = false, negotiation = false;
    for (const auto& q : quests)
    {
        proposed |= q.issuer == index && q.state == QuestState::Proposed;
        ongoing |= q.issuer == index &&
                   (q.state == QuestState::Accepted || q.state == QuestState::Progress);
        negotiation |= q.target == index && q.kind == QuestKind::Negotiate &&
                       (q.state == QuestState::Accepted || q.state == QuestState::Progress);
    }
    if (proposed)
    {
        choices.push_back({4, "도움이 필요한 일을 묻는다"});
    }
    if (ongoing)
    {
        choices.push_back({5, "맡은 일의 결과를 보고한다"});
    }
    if (negotiation)
    {
        choices.push_back({6, "휴전 조건을 논의한다"});
    }
    if (n.role == "leader" || n.role == "merchant")
    {
        choices.push_back({1, "지역에 식량을 지원한다"});
    }
    if (n.role == "leader")
    {
        choices.push_back({3, playerFaction == n.faction ? "세력을 떠나겠다고 말한다"
                                                         : "세력에 합류하고 싶다고 말한다"});
    }
    if (!playerBeliefs.empty())
    {
        choices.push_back({13, "알게 된 증거를 전달한다"});
    }
    if (n.role == "healer" || n.role == "scholar")
    {
        choices.push_back({12, "군집의 기억을 함께 들여다본다"});
    }
    if (factions[n.faction].ideology > .5f && (n.role == "leader" || n.role == "healer"))
    {
        choices.push_back({11, "이 지역의 정화를 의논한다"});
    }
    return choices;
}

std::string WorldSociety::ConversationTerms(int index, int option) const
{
    const auto& n = npcs[index];
    if (option == 2)
    {
        return n.playerRelation[0] < -30 ? "지금은 당신을 믿을 수 없어 거래하지 않겠소."
               : n.food < 3              ? "나와 이웃이 먹을 식량도 부족하오. 지금은 내줄 수 없소."
                            : "파편 1개를 주면 내 식량 2개를 건네겠소. 이 조건으로 거래하겠소?";
    }
    if (option == 8)
    {
        return "회복약 1개로 체력을 최대 45 회복하지만 감염 의존이 조금 높아집니다. "
               "치료하시겠습니까?";
    }
    if (option == 1)
    {
        return "소지 식량 2개를 이 지역에 기부합니다. 전달하시겠습니까?";
    }
    if (option == 3)
    {
        return playerFaction == n.faction ? "떠나더라도 우리 사이의 기억은 남을 것이오. 탈퇴하겠소?"
                                          : "함께하려면 신뢰가 필요하오. 합류를 요청하겠소?";
    }
    if (option == 4)
    {
        for (const auto& q : quests)
        {
            if (q.issuer == index && q.state == QuestState::Proposed)
            {
                return std::string(QuestName(q.kind)) + " · 목적지: " + regions[q.region].name +
                       " · 남은 세계 시간 " +
                       std::to_string(q.deadline > minute ? q.deadline - minute : 0) +
                       "분. 완료 시 경험치 35와 파편 2개를 받습니다. 맡겠습니까?";
            }
        }
    }
    if (option == 5)
    {
        return "현재 요청의 충족 여부를 확인합니다. 식량 전달 요청이라면 식량 3개를 인계합니다. "
               "보고하시겠습니까?";
    }
    if (option == 6)
    {
        return "서로의 신뢰와 평판을 바탕으로 휴전을 제안합니다. 상대가 거절할 수도 있습니다. "
               "제안하시겠습니까?";
    }
    if (option == 11)
    {
        return "파편 2개와 성혈 공급을 소비해 감염을 줄입니다. 균사 식량도 줄어듭니다. "
               "진행하시겠습니까?";
    }
    if (option == 12)
    {
        return "동의를 얻어 기억을 공유합니다. 감염과 군집 의존이 함께 높아집니다. "
               "요청하시겠습니까?";
    }
    return "현재 조건으로 실행하시겠습니까?";
}

std::vector<std::string> WorldSociety::Journal(int tab) const
{
    if (tab == 6)
    {
        return LifeJournal();
    }
    if (tab >= 4)
    {
        return StoryJournal(tab);
    }
    std::vector<std::string> lines;
    if (tab == 0)
    {
        lines.push_back("소지 식량 " + std::to_string(playerFood) + " · 소속 " +
                        (playerFaction < 0 ? std::string("무소속") : factions[playerFaction].name));
        for (std::size_t i = 0; i < factions.size(); ++i)
        {
            const auto& f = factions[i];
            int count = 0;
            for (const auto& n : npcs)
            {
                if (n.Alive() && n.faction == static_cast<int>(i))
                {
                    ++count;
                }
            }
            lines.push_back(f.name + " · 생존 " + std::to_string(count) + "명 · 지도자 " +
                            (f.leader >= 0 ? npcs[f.leader].name : "없음") +
                            (f.active ? "" : " · 해산"));
            lines.push_back("  " + f.motto);
        }
        for (const auto& c : campaigns)
        {
            lines.push_back(factions[c.attacker].name + " ↔ " + factions[c.defender].name +
                            (c.active ? " · 전쟁 · " : " · 휴전 · ") + regions[c.region].name);
            lines.push_back("  보급 " + std::to_string(int(factions[c.attacker].supplies)) + " / " +
                            std::to_string(int(factions[c.defender].supplies)));
        }
        for (std::size_t a = 0; a < factions.size(); ++a)
        {
            for (std::size_t b = a + 1; b < factions.size(); ++b)
            {
                if (!factions[a].active || !factions[b].active)
                {
                    continue;
                }
                lines.push_back(factions[a].name + " ↔ " + factions[b].name + " · " +
                                (diplomacy[a][b] >= 55               ? "동맹"
                                 : diplomacy[a][b] < rules.hostility ? "적대"
                                                                     : "교섭") +
                                " " + std::to_string(static_cast<int>(diplomacy[a][b])));
            }
        }
        static const char* titles[] = {"자비", "질서", "자유", "정화", "연결", "기만"};
        std::string values;
        for (int i = 0; i < 6; ++i)
        {
            values +=
                std::string(titles[i]) + " " + std::to_string(static_cast<int>(karma[i])) + "  ";
        }
        lines.push_back(values);
        for (const auto& r : regions)
        {
            lines.push_back(r.name + " · 주민 " + std::to_string(r.population) + " · 통제 " +
                            (r.owner < 0 ? "공백" : factions[r.owner].name));
        }
    }
    else if (tab == 1)
    {
        for (const auto& n : npcs)
        {
            lines.push_back(n.name + " / " + factions[n.faction].name + " / " +
                            (n.Alive() ? ActionName(n.action) : "사망") + " / " +
                            regions[n.region].name);
        }
    }
    else if (tab == 2)
    {
        for (const auto& q : quests)
        {
            std::string title = QuestName(q.kind);
            for (const auto& story : storyIncidents)
            {
                if (story.cause != 0 && story.cause == q.cause)
                {
                    title = story.title;
                    break;
                }
            }
            lines.push_back(title + " · " + QuestStatus(q.state) + " · " + npcs[q.issuer].name);
            std::string goal = q.kind == QuestKind::Supply      ? "식량 3개 전달"
                               : q.kind == QuestKind::Heal      ? "대상 체력 90 이상"
                               : q.kind == QuestKind::Defend    ? "해당 지역 변이체 2마리 처치"
                               : q.kind == QuestKind::Negotiate ? "상대에게 신뢰를 얻고 휴전 협상"
                               : q.kind == QuestKind::Escort    ? "동행 대상과 목적지 중심 도착"
                                                                : "목적지 중심의 기록 조사";
            lines.push_back("  " + regions[q.region].name + " · " + goal +
                            (q.target >= 0 ? " · " + npcs[q.target].name : "") +
                            " · 남은 세계 분 " +
                            std::to_string(q.deadline > minute ? q.deadline - minute : 0));
        }
        if (quests.empty())
        {
            lines.push_back("아직 요청이 없습니다. 지역 문제와 NPC 활동에 따라 생성됩니다.");
        }
        lines.push_back("목표를 달성하면 의뢰인에게 F → 5로 보고 · H: 첫 진행 요청 보류/재개");
    }
    else
    {
        for (auto it = history.rbegin(); it != history.rend(); ++it)
        {
            lines.push_back(std::to_string(it->minute) + "분 · " + it->text);
        }
    }
    return lines;
}
