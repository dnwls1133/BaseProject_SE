#include "stdafx.h"
#include "LevelOne.h"
#include "ContentData.h"
#include <algorithm>
#include <cmath>

namespace
{
std::vector<std::string> WrapJournal(Renderer& renderer, const std::vector<std::string>& input,
                                     float width, int size = 18)
{
    std::vector<std::string> output;
    for (const auto& text : input)
    {
        std::string line;
        float advance = 0;
        for (std::size_t at = 0; at < text.size();)
        {
            const auto lead = static_cast<unsigned char>(text[at]);
            const std::size_t bytes =
                std::min(text.size() - at, static_cast<std::size_t>(lead < 128   ? 1
                                                                    : lead < 224 ? 2
                                                                    : lead < 240 ? 3
                                                                                 : 4));
            const float next = renderer.TextWidth(text.substr(at, bytes), size) - 4;
            if (advance + next > width && !line.empty())
            {
                output.push_back(line);
                line.clear();
                advance = 0;
            }
            line.append(text, at, bytes);
            at += bytes;
            advance += next;
        }
        output.push_back(line);
    }
    return output;
}
} // namespace

void LevelOne::CreateCitizens()
{
    activityPlaces.clear();
    for (std::size_t i = 0; i < society.LifePlaces().size(); ++i)
    {
        const auto& place = society.LifePlaces()[i];
        const int model = meshCache3D.Find(place.model);
        ContentData::Require(model >= 0, "unknown activity 3D model: " + place.model);
        activityPlaces.emplace_back(this, i, model);
        activityPlaces.back().SetPosition(place.position.x, place.position.y);
    }
    citizens.clear();
    citizens.reserve(society.Npcs().size());
    for (std::size_t i = 0; i < society.Npcs().size(); ++i)
    {
        citizens.emplace_back(this, i);
        citizens.back().SetPosition(society.Npcs()[i].x, society.Npcs()[i].y);
    }
}

void LevelOne::UpdateCitizen(Citizen& citizen, float dt)
{
    society.TickActor(citizen.index, dt, simulation, map, {player.x, player.y},
                      [this](WorldSociety::Npc& npc, float step)
                      {
                          return UpdateCitizenWildlife(npc, step);
                      });
    const auto& npc = society.Npcs()[citizen.index];
    citizen.SetPosition(npc.x, npc.y);
    if (!npc.Alive() && npc.collapse >= 1)
    {
        citizen.SetRenderPasses(ActorPassBit(ActorPass::Ground));
        citizen.SetTickEnabled(false);
    }
}

void LevelOne::DrawCitizen(Renderer& r, const Citizen& citizen, ActorPass pass)
{
    const auto& npc = society.Npcs()[citizen.index];
    const auto& faction = society.Factions()[npc.faction];
    Point p = Project(npc.x, npc.y, 0, r);
    if (pass == ActorPass::Shadow)
    {
        return;
    }
    if (pass == ActorPass::World || pass == ActorPass::Ground)
    {
        Pose3D pose;
        pose.phase = npc.phase;
        pose.movement = npc.movement;
        pose.time = time + static_cast<float>(citizen.index) * .73f;
        pose.flash = npc.flash * 3;
        pose.collapse = npc.collapse;
        pose.attack =
            npc.windup > 0
                ? .25f + .65f * (1 - std::min(1.f, npc.windup / society.AttackWindupDuration()))
                : 0;
        pose.activity = society.LifeActivity(static_cast<int>(citizen.index));
        Vec3 position{npc.x, Terrain3D::Height(npc.x, npc.y) + npc.collapse * .25f, npc.y};
        meshCache3D.Draw(r, static_cast<int>(npc.model), position, npc.yaw, .94f, pose);
        meshCache3D.Draw(r, static_cast<int>(faction.emblem), position, npc.yaw, .94f, pose);
        if (npc.Alive() &&
            (society.StoryCargo(static_cast<int>(citizen.index)) > 0 || pose.activity == 2))
        {
            r.Submit3D(0,
                       Mat4::Translation({npc.x - std::sin(npc.yaw) * .45f, position.y + .95f,
                                          npc.y - std::cos(npc.yaw) * .45f}) *
                           Mat4::Rotation({0, 1, 0}, npc.yaw) * Mat4::Scale({.55f, .45f, .4f}),
                       Color(.43f, .30f, .16f), Material::Wood);
        }
    }
    else if (pass == ActorPass::Overlay && npc.Alive())
    {
        const float distance = std::hypot(npc.x - player.x, npc.y - player.y);
        const bool selected = int(citizen.index) == talkingNpc;
        const bool fighting = npc.action == WorldSociety::Action::Combat || npc.wildTarget >= 0;
        const bool focused = int(citizen.index) == focusedNpc && talkingNpc < 0 && !journalOpen;
        if (distance > 12 || (!selected && !focused && !fighting))
        {
            return;
        }
        r.Rect(p.x - 25, p.y - 92, 50, 5, Color(.08f, .07f, .06f));
        r.Rect(p.x - 24, p.y - 91, 48 * npc.health / 100, 3, faction.color);
        if (focused || selected)
        {
            const std::string label = npc.name + " · " + faction.name;
            const float width = r.TextWidth(label, 17);
            r.Rect(p.x - width * .5f - 8, p.y - 123, width + 16, 27,
                   Color(.035f, .04f, .045f, .93f));
            r.Text(p.x - width * .5f, p.y - 102, label, Color(.94f, .9f, .78f), 17);
            if (focused)
            {
                r.Text(p.x - 36, p.y + 28, "[F] 대화", Color(.96f, .86f, .6f), 17);
            }
        }
    }
}

void LevelOne::CloseConversation()
{
    society.EndConversation();
    talkingNpc = -1;
    pendingDialogueOption = 0;
    dialogueResponse.clear();
    dialogueChoices.clear();
    dialoguePage = dialogueTextPage = 0;
    ClearInput();
}

bool LevelOne::SocietyKey(unsigned char key)
{
    if (key == 'k' || key == 'l')
    {
        CloseConversation();
        if (key == 'k')
        {
            SaveWorld(true);
        }
        else
        {
            RestoreWorld(false);
        }
        return true;
    }
    if (talkingNpc >= 0 && (key == 'f' || key == '0' || key == 27))
    {
        CloseConversation();
        return true;
    }
    if (journalOpen && (key == 'j' || key == 27))
    {
        journalOpen = false;
        ClearInput();
        return true;
    }
    if (paused || statPanel || stats.health <= 0)
    {
        return false;
    }
    if (key == 'j')
    {
        CloseConversation();
        journalOpen = true;
        journalPage = 0;
        return true;
    }
    if (journalOpen)
    {
        if (key >= '1' && key <= '7')
        {
            journalTab = key - '1';
            journalPage = 0;
        }
        if (key == '[')
        {
            journalPage = std::max(0, journalPage - 1);
        }
        if (key == ']')
        {
            ++journalPage;
        }
        if (key == 'h')
        {
            society.ToggleQuestHold();
        }
        ClearInput();
        return true;
    }
    if (key == 'f')
    {
        float nearest = 3.5f;
        int target = -1;
        for (std::size_t i = 0; i < society.Npcs().size(); ++i)
        {
            const auto& npc = society.Npcs()[i];
            const float distance = std::hypot(npc.x - player.x, npc.y - player.y);
            if (npc.Alive() && distance < nearest && map.Visible(player.x, player.y, npc.x, npc.y))
            {
                target = int(i);
                nearest = distance;
            }
        }
        if (target >= 0 && society.BeginConversation(target))
        {
            talkingNpc = target;
            dialoguePage = dialogueTextPage = pendingDialogueOption = 0;
            dialogueResponse = society.Dialogue(target)[1];
            dialogueChoices = society.ConversationChoices(target);
            noticeTime = 0;
        }
        else
        {
            Notify(target < 0 ? "대화할 주민 가까이에서 F를 누르세요."
                              : "상대는 지금 전투나 다른 약속을 처리하고 있습니다.");
        }
        ClearInput();
        return true;
    }
    if (talkingNpc < 0)
    {
        return false;
    }
    if (!society.IsConversationActive(talkingNpc))
    {
        CloseConversation();
        return true;
    }
    if (key == 'u')
    {
        dialogueTextPage = std::max(0, dialogueTextPage - 1);
    }
    if (key == 'o')
    {
        ++dialogueTextPage;
    }
    if (!pendingDialogueOption)
    {
        if (key == '[')
        {
            dialoguePage = std::max(0, dialoguePage - 1);
        }
        if (key == ']')
        {
            dialoguePage = std::min(int((dialogueChoices.size() - 1) / 3), dialoguePage + 1);
        }
        if (key >= '1' && key <= '3')
        {
            const int choice = dialoguePage * 3 + key - '1';
            if (choice < int(dialogueChoices.size()))
            {
                const int option = dialogueChoices[choice].first;
                dialogueTextPage = 0;
                if (option == 7 || option == 13)
                {
                    dialogueResponse = society.Interact(talkingNpc, option, simulation, stats);
                }
                else
                {
                    const auto& npc = society.Npcs()[talkingNpc];
                    const bool refusedTrade =
                        option == 2 && (npc.food < 3 || npc.playerRelation[0] < -30);
                    pendingDialogueOption = refusedTrade ? 0 : option;
                    dialogueResponse = society.ConversationTerms(talkingNpc, option);
                    if (option == 2 && !refusedTrade && stats.fragments < 1)
                    {
                        pendingDialogueOption = 0;
                        dialogueResponse += " 하지만 지금은 파편이 부족합니다.";
                    }
                    if (option == 8 && stats.potions < 1)
                    {
                        pendingDialogueOption = 0;
                        dialogueResponse =
                            "치료하려면 회복약 1개가 필요합니다. 지금은 회복약이 없습니다.";
                    }
                }
            }
        }
    }
    else if (key == '1' || key == '2')
    {
        if (key == '1')
        {
            // Commit once. Interact rechecks inventory and the live world conditions.
            dialogueResponse =
                society.Interact(talkingNpc, pendingDialogueOption, simulation, stats);
        }
        else
        {
            dialogueResponse = "지금은 보류하겠습니다.";
        }
        pendingDialogueOption = 0;
        dialoguePage = dialogueTextPage = 0;
        dialogueChoices = society.ConversationChoices(talkingNpc);
    }
    ClearInput();
    return true;
}

void LevelOne::DrawSocietyUI(Renderer& r)
{
    if ((!journalOpen && talkingNpc < 0) || paused || statPanel || stats.health <= 0)
    {
        return;
    }
    const float width = std::min(820.f, r.Width() - 40.f);
    const float height = std::min(570.f, r.Height() - 60.f);
    const float x = (r.Width() - width) * .5f, y = (r.Height() - height) * .5f;
    const Color ink(.94f, .92f, .85f), gold(.92f, .77f, .49f);
    r.Rect(x - 2, y - 2, width + 4, height + 4, Color(.035f, .04f, .045f, .98f));
    r.Panel(x, y, width, height);
    if (talkingNpc >= 0)
    {
        const auto& npc = society.Npcs()[talkingNpc];
        const auto& faction = society.Factions()[npc.faction];
        r.Text(x + 24, y + 36, npc.name + " · " + faction.name, gold, 22);
        r.Text(x + 24, y + 64, "현재: " + society.LifeLabel(talkingNpc), ink, 17);
        auto lines = WrapJournal(r, {dialogueResponse}, width - 48);
        const int perPage = std::max(1, int((height - 290) / 28));
        const int pages = std::max(1, (int(lines.size()) + perPage - 1) / perPage);
        dialogueTextPage = std::clamp(dialogueTextPage, 0, pages - 1);
        for (int i = 0; i < perPage && dialogueTextPage * perPage + i < int(lines.size()); ++i)
        {
            r.Text(x + 24, y + 104 + i * 28.f, lines[dialogueTextPage * perPage + i], ink, 18);
        }
        if (pages > 1)
        {
            r.Text(x + 24, y + height - 202,
                   "U/O 대사 페이지 " + std::to_string(dialogueTextPage + 1) + "/" +
                       std::to_string(pages),
                   gold, 16);
        }
        const float optionsTop = y + height - 174;
        if (pendingDialogueOption)
        {
            r.Text(x + 24, optionsTop, "[1] 이 조건으로 진행한다", gold, 18);
            r.Text(x + 24, optionsTop + 42, "[2] 보류하고 다른 이야기를 한다", ink, 18);
        }
        else
        {
            for (int i = 0; i < 3 && dialoguePage * 3 + i < int(dialogueChoices.size()); ++i)
            {
                r.Text(x + 24, optionsTop + i * 42.f,
                       "[" + std::to_string(i + 1) + "] " +
                           dialogueChoices[dialoguePage * 3 + i].second,
                       ink, 18);
            }
        }
        r.Text(x + 24, y + height - 24,
               "F / Esc 닫기" +
                   std::string(!pendingDialogueOption && dialogueChoices.size() > 3
                                   ? " · [ / ] 다른 화제"
                                   : "") +
                   " · 세계는 계속 진행",
               gold, 16);
        return;
    }
    r.Text(x + 22, y + 35, "[1] 세력 [2] 주민 [3] 요청 [4] 기록 [5] 서사 [6] 인물 [7] 생활", gold,
           18);
    auto lines = WrapJournal(r, society.Journal(journalTab), width - 48);
    const int perPage = std::max(1, int((height - 112) / 28));
    const int pages = std::max(1, (int(lines.size()) + perPage - 1) / perPage);
    journalPage = std::clamp(journalPage, 0, pages - 1);
    for (int i = 0; i < perPage && journalPage * perPage + i < int(lines.size()); ++i)
    {
        r.Text(x + 24, y + 76 + i * 28.f, lines[journalPage * perPage + i], ink, 18);
    }
    r.Text(x + 24, y + height - 24,
           "[ / ] " + std::to_string(journalPage + 1) + " / " + std::to_string(pages) +
               " · H 요청 보류/재개 · J 닫기",
           gold, 17);
}

void LevelOne::DrawActivityPlace(Renderer& r, const ActivityPlace& actor, ActorPass pass)
{
    const auto& place = society.LifePlaces()[actor.index];
    if (pass == ActorPass::World)
    {
        Pose3D pose;
        pose.time = time;
        const int copies = place.kind == "rest" ? place.capacity : 1;
        for (int i = 0; i < copies; ++i)
        {
            Point position{actor.x, actor.y};
            if (copies > 1 && i < int(place.slots.size()))
            {
                position = place.slots[i];
            }
            const float yaw =
                copies > 1 ? std::atan2(position.x - actor.x, position.y - actor.y) : 0;
            meshCache3D.Draw(r, actor.model,
                             {position.x, Terrain3D::Height(position.x, position.y), position.y},
                             yaw, 1, pose);
        }
        if (place.kind == "clinic")
        {
            int bottles = std::min(4, static_cast<int>(place.medicine));
            for (int i = 0; i < bottles; ++i)
            {
                meshCache3D.Draw(r, static_cast<int>(ModelId::Potion),
                                 {actor.x + .5f, Terrain3D::Height(actor.x, actor.y) + .15f,
                                  actor.y - .45f + i * .3f},
                                 0, .65f);
            }
        }
    }
    else if (talkingNpc < 0 && !journalOpen &&
             std::hypot(actor.x - player.x, actor.y - player.y) < 2)
    {
        Point p = Project(actor.x, actor.y, 68, r);
        r.Text(p.x - r.TextWidth(place.name, 12) * .5f, p.y, place.name, Color(.72f, .67f, .48f),
               12);
    }
}
