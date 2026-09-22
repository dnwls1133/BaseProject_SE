#include "stdafx.h"
#include "Game.h"
#include "Scene.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>
#include <cctype>

Game::Game()
{
    player.SetPosition(8, 7);
    player.update = [this](float dt)
    {
        UpdatePlayer(dt);
    };
    player.draw = [this](Renderer& r, ActorPass pass)
    {
        if (pass == ActorPass::World)
        {
            Person(r, player.x, player.y, true);
        }
    };
    terrain.draw = [this](Renderer& r, ActorPass pass)
    {
        if (pass == ActorPass::Ground)
        {
            DrawGround(r);
        }
    };
    atmosphere.draw = [this](Renderer& r, ActorPass pass)
    {
        if (pass == ActorPass::Effects)
        {
            DrawAtmosphere(r);
        }
    };
    hud.draw = [this](Renderer& r, ActorPass pass)
    {
        if (pass == ActorPass::UI)
        {
            DrawUI(r);
        }
    };
}

float Game::Dist(float x, float y, float a, float b)
{
    return std::sqrt((x - a) * (x - a) + (y - b) * (y - b));
}

float Game::SegmentDistance(float x, float y, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy;
    float t =
        length > 0 ? std::max(0.f, std::min(1.f, ((x - ax) * dx + (y - ay) * dy) / length)) : 0;
    return Dist(x, y, ax + t * dx, ay + t * dy);
}

bool Game::Solid(const Object& o) const
{
    return o.IsActive() && (o.type == "wall" || o.type == "well" || o.type == "altar" ||
                            o.type == "wagon" || o.type == "pillar" || o.type == "grave");
}

bool Game::Blocked(float x, float y) const
{
    if (x < 1 || x > mapWidth - 1 || y < 1 || y > mapHeight - 1)
    {
        return true;
    }

    for (const auto& o : objects)
    {
        if (Solid(o) && std::abs(x - o.x) < o.w * .5f + .24f &&
            std::abs(y - o.y) < o.h * .5f + .24f)
        {
            return true;
        }

        if (o.IsActive() && (o.type == "arch" || o.type == "exit"))
        {
            for (int side = -1; side <= 1; side += 2)
            {
                if (std::abs(x - o.x - side * 1.2f) < .515f && std::abs(y - o.y) < .615f)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

bool Game::Reach(const Object& o) const
{
    if (!o.IsActive() || !player.IsActive() || Dist(player.x, player.y, o.x, o.y) > 2.f)
    {
        return false;
    }

    for (int i = 1; i < 10; ++i)
    {
        if (Blocked(player.x + (o.x - player.x) * i / 10, player.y + (o.y - player.y) * i / 10))
        {
            return false;
        }
    }

    return true;
}

const Game::Object* Game::Find(const std::string& type) const
{
    for (const auto& o : objects)
    {
        if (o.IsActive() && o.type == type)
        {
            return &o;
        }
    }

    return nullptr;
}

void Game::ClearInput()
{
    levelOne.ClearInput();
    for (bool& k : keys)
    {
        k = false;
    }
}

void Game::Reset()
{
    scene.Clear();
    footprintGroup = nullptr;
    player.x = cx = spawnX;
    player.y = cy = spawnY;
    time = elapsed = walkPhase = motion = reaction = stepTime = 0;
    accepted = blood = given = choice = done = paused = false;
    answer = 0;
    line = 0;
    target = -1;
    dialogue.clear();
    footsteps.clear();
    notice.clear();
    noticeTime = 0;
    ClearInput();
    BuildScene();
}

bool Game::Load(const std::string& path)
{
    scene.Clear();
    footprintGroup = nullptr;
    objects.clear();
    conversations.clear();
    std::ifstream file(path);
    if (!file)
    {
        return false;
    }
    const std::string allowed = " wall well altar wagon pillar arch banner rubble body corpse npc "
                                "chest exit lamp root grave road spawn map lore ";
    std::string text;
    int npc = 0, chest = 0, exit = 0;
    while (std::getline(file, text))
    {
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        {
            text.erase(0, 3);
        }

        if (text.find_first_not_of(" \t\r") == std::string::npos || text[0] == '#')
        {
            continue;
        }
        std::istringstream row(text);
        Object o;
        if (!(row >> o.type >> o.x >> o.y >> o.w >> o.h))
        {
            return false;
        }

        if (!std::isfinite(o.x) || !std::isfinite(o.y) || !std::isfinite(o.w) ||
            !std::isfinite(o.h))
        {
            return false;
        }

        if (allowed.find(" " + o.type + " ") == std::string::npos)
        {
            return false;
        }

        if (o.type == "map")
        {
            mapWidth = o.x;
            mapHeight = o.y;
            if (mapWidth < 10 || mapHeight < 10 || mapWidth > 128 || mapHeight > 128)
            {
                return false;
            }
            continue;
        }

        if (o.type == "spawn")
        {
            spawnX = o.x;
            spawnY = o.y;
            continue;
        }

        if (row >> o.height)
        {
            if (!std::isfinite(o.height) || o.height < 0 || o.height > 300)
            {
                return false;
            }
            row >> o.variant;
        }

        if (o.x < 0 || o.x > mapWidth || o.y < 0 || o.y > mapHeight || o.w <= 0 || o.h <= 0)
        {
            return false;
        }

        if (o.type == "road" && (o.w > mapWidth || o.h > mapHeight))
        {
            return false;
        }
        npc += o.type == "npc";
        chest += o.type == "chest";
        exit += o.type == "exit";
        o.owner = this;
        objects.push_back(o);
    }

    if (npc != 1 || chest != 1 || exit != 1 || Blocked(spawnX, spawnY))
    {
        return false;
    }
    size_t slash = path.find_last_of("/\\");
    std::string base = slash == std::string::npos ? "" : path.substr(0, slash + 1);
    std::ifstream speech(base + "dialogue.ko.txt");
    if (!speech)
    {
        return false;
    }

    while (std::getline(speech, text))
    {
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        {
            text.erase(0, 3);
        }

        if (text.empty() || text[0] == '#')
        {
            continue;
        }
        size_t a = text.find('|'), b = a == std::string::npos ? a : text.find('|', a + 1);
        if (a == std::string::npos || b == std::string::npos)
        {
            return false;
        }
        conversations[text.substr(0, a)].push_back(
            {text.substr(a + 1, b - a - 1), text.substr(b + 1)});
    }

    for (auto id : {"request", "request_have", "remind", "blood", "empty", "administer", "remember",
                    "testimony", "silent", "corpse", "lore0", "lore1", "lore2"})
    {
        if (conversations.find(id) == conversations.end())
        {
            return false;
        }
    }
    dataDirectory = base;
    Reset();
    return true;
}

void Game::Say(const std::string& id)
{
    auto found = conversations.find(id);
    if (found != conversations.end())
    {
        dialogue = found->second;
        line = 0;
    }
}

void Game::RefreshTarget()
{
    target = -1;
    float best = 100;
    for (int i = 0; i < (int)objects.size(); ++i)
    {
        const auto& o = objects[i];
        if (o.type != "npc" && o.type != "chest" && o.type != "exit" && o.type != "corpse" &&
            o.type != "lore")
        {
            continue;
        }

        if (Reach(o))
        {
            float distance = Dist(player.x, player.y, o.x, o.y);
            if (distance < best)
            {
                target = i;
                best = distance;
            }
        }
    }
}

void Game::Key(unsigned char k, bool down)
{
    if (inFirstLevel)
    {
        levelOne.Key(k, down);
        return;
    }
    if (k >= 'A' && k <= 'Z')
    {
        k += 32;
    }
    bool previous = keys[k];
    keys[k] = down;
    if (!down || previous)
    {
        return;
    }

    if (k == 27)
    {
        paused = !paused;
        ClearInput();
        return;
    }

    if (paused || !player.IsActive())
    {
        return;
    }

    if (done)
    {
        if (k == 'e')
        {
            EnterFirstLevel();
            return;
        }

        if (k == 'r')
        {
            Reset();
        }

        return;
    }

    if (k == '1' && !accepted && dialogue.empty())
    {
        EnterFirstLevel();
        return;
    }

    if (choice)
    {
        if (k == '1' || k == '2')
        {
            answer = k - '0';
            choice = false;
            Say(answer == 1 ? "remember" : "testimony");
            reaction = 1;
        }

        return;
    }

    if (k != 'e')
    {
        return;
    }

    if (!dialogue.empty())
    {
        if (++line >= (int)dialogue.size())
        {
            dialogue.clear();
            if (given && !answer)
            {
                choice = true;
            }
        }

        return;
    }
    RefreshTarget();
    if (target < 0)
    {
        return;
    }
    const auto& o = objects[target];
    if (o.type == "npc")
    {
        if (!accepted)
        {
            accepted = true;
            Say(blood ? "request_have" : "request");
        }
        else if (!blood)
        {
            Say("remind");
        }
        else if (!given)
        {
            given = true;
            reaction = 1;
            Say("administer");
        }
        else
        {
            Say("silent");
        }
    }
    else if (o.type == "chest")
    {
        if (!blood)
        {
            blood = true;
            Say("blood");
        }
        else
        {
            Say("empty");
        }
    }
    else if (o.type == "corpse")
    {
        Say("corpse");
    }
    else if (o.type == "lore")
    {
        Say("lore" + std::to_string(o.variant % 3));
    }
    else if (o.type == "exit")
    {
        if (answer)
        {
            done = true;
        }
        else
        {
            notice = "아직 그를 두고 떠날 수 없다. 광장으로 돌아가자.";
            noticeTime = 4;
        }
    }
}

void Game::Update(float dt)
{
    if (inFirstLevel)
    {
        levelOne.Update(dt);
        return;
    }
    if (paused || done)
    {
        return;
    }
    time += dt;
    elapsed += dt;
    noticeTime -= dt;
    reaction = std::max(0.f, reaction - dt * .15f);
    scene.Update(dt);
    footsteps.remove_if(
        [](const Footprint& footprint)
        {
            return footprint.IsDestroyed();
        });
    cx += (player.x - cx) * (1 - std::exp(-dt * 5));
    cy += (player.y - cy) * (1 - std::exp(-dt * 5));
    RefreshTarget();
}

Point Game::Project(float x, float y, float z, Renderer& r) const
{
    return {r.Width() * .5f + (x - y - cx + cy) * 32,
            r.Height() * .49f + (x + y - cx - cy) * 16 - z};
}

void Game::Render(Renderer& r)
{
    if (inFirstLevel)
    {
        levelOne.Render(r);
        return;
    }

    scene.Render(r, time, {(cx - cy) * 32 - r.Width() * .5f, (cx + cy) * 16 - r.Height() * .49f});
}

void Game::DrawUI(Renderer& r)
{
    const Color gold(.78f, .66f, .46f), ink(.87f, .86f, .8f), muted(.58f, .65f, .64f);
    float hudWidth = std::min(550.f, r.Width() - 36.f);
    r.Panel(18, 16, hudWidth, 104);
    r.Rect(18, 29, 3, 34, Color(.58f, .12f, .15f));
    r.Text(34, 43, "성혈의 잔향  /  마지막 이름", gold, 20);
    std::string goal = !accepted ? "광장에서 숨소리의 주인을 찾으세요"
                       : !blood  ? "약제소에서 마지막 성혈을 찾으세요"
                       : !given  ? "에른에게 돌아가 성혈을 건네세요"
                       : !answer ? "에른의 마지막 말을 들으세요"
                                 : "동쪽 성문으로 마을을 떠나세요";
    r.Text(34, 74, goal, ink, 19);
    int stage = !accepted ? 0 : !blood ? 1 : !given ? 2 : !answer ? 3 : 4;
    for (int i = 0; i < 5; ++i)
    {
        r.Rect(35 + i * 28.f, 96, 21, 3, i <= stage ? gold : Color(.22f, .25f, .25f));
    }
    r.Text(200, 106, "WASD 이동   E 상호작용   ESC 정지", muted, 15);
    std::string region = player.x < 13   ? "무너진 남문"
                         : player.x > 36 ? "성도로 향하는 길"
                         : player.y > 27 ? "침묵의 묘역"
                         : player.x > 27 ? "성혈 약제소"
                         : player.y < 12 ? "잿빛 예배당"
                                         : "순교자의 광장";
    if (r.Width() > 850)
    {
        r.Text(r.Width() - 235.f, 42, region, gold, 18);
        r.Text(r.Width() - 235.f, 67, "살아 있는 것은 둘뿐이다", muted, 15);
    }

    if (target >= 0 && dialogue.empty() && !choice && !done && !paused)
    {
        const auto& o = objects[target];
        Point p = Project(o.x, o.y, 65, r);
        std::string action = o.type == "npc"               ? "대화"
                             : o.type == "exit"            ? "떠나기"
                             : o.type == "chest" && !blood ? "성혈 획득"
                                                           : "조사";
        float x = std::max(8.f, std::min(r.Width() - 158.f, p.x - 70));
        float y = std::max(128.f, std::min(r.Height() - 52.f, p.y - 22));
        r.Panel(x, y, 148, 36);
        r.Text(x + 12, y + 26, "[E] " + action, gold, 18);
    }

    if (dialogue.empty() && !choice && !done && !paused)
    {
        const Object* objective = Find(answer ? "exit" : accepted && !blood ? "chest" : "npc");
        if (objective)
        {
            Point p = Project(objective->x, objective->y, 0, r);
            float dx = p.x - r.Width() * .5f, dy = p.y - r.Height() * .49f,
                  len = std::sqrt(dx * dx + dy * dy);
            if (len > 80)
            {
                dx /= len;
                dy /= len;
                float x = r.Width() - 65.f, y = 156;
                r.SoftEllipse(x, y, 50, 50, Color(0, 0, 0, .6f));
                r.Ellipse(x, y, 22, 22, Color(.025f, .04f, .04f, .9f));
                r.Triangle({x + dx * 17, y + dy * 17}, {x - dx * 8 - dy * 7, y - dy * 8 + dx * 7},
                           {x - dx * 8 + dy * 7, y - dy * 8 - dx * 7}, gold);
                r.Text(x - 31, y + 48, "목표 방향", muted, 15);
            }
        }
        r.Text(24, r.Height() - 24.f,
               blood && !given ? "소지품  ·  마지막 성혈 한 병"
               : answer        ? "소지품  ·  순례자의 통행패"
                               : "오염된 마을을 조사하세요 · 처음에는 [1] 첫 레벨 바로가기",
               muted, 16);
    }

    if (noticeTime > 0)
    {
        r.Text(28, 150, notice, Color(.92f, .72f, .52f), 17);
    }

    if (!dialogue.empty() || choice)
    {
        float panelWidth = std::min(940.f, r.Width() - 36.f), x = (r.Width() - panelWidth) * .5f,
              y = r.Height() - 212.f;
        r.Panel(x, y, panelWidth, 194);
        r.Rect(x, y + 20, 3, 42, Color(.62f, .16f, .17f));
        if (choice)
        {
            r.Text(x + 24, y + 35, "그에게 무엇을 남겨 주겠습니까?", gold, 20);
            r.Rect(x + 20, y + 53, panelWidth - 40, 45, Color(.09f, .11f, .11f, .9f));
            r.Rect(x + 20, y + 106, panelWidth - 40, 45, Color(.09f, .11f, .11f, .9f));
            r.Text(x + 34, y + 83, "[1] 당신의 이름을 기억하겠습니다.", ink, 20);
            r.Text(x + 34, y + 136, "[2] 당신이 들은 것을 말해 주세요.", ink, 20);
            r.Text(x + 24, y + 176, "어느 선택으로도 여정은 계속됩니다.", muted, 15);
        }
        else
        {
            const auto& d = dialogue[line];
            r.Text(x + 24, y + 34, d.speaker, gold, 19);
            r.WrappedText(x + 24, y + 77, panelWidth - 48, d.text, ink, 21);
            r.Text(x + 24, y + 174, "[E] 다음", muted, 16);
            r.Text(x + panelWidth - 100, y + 174,
                   std::to_string(line + 1) + " / " + std::to_string(dialogue.size()), muted, 16);
        }
    }

    if (paused || done)
    {
        r.Rect(0, 0, (float)r.Width(), (float)r.Height(), Color(.006f, .01f, .014f, .82f));
        float w = std::min(680.f, r.Width() - 40.f), x = (r.Width() - w) / 2,
              y = std::max(30.f, r.Height() * .5f - 150);
        r.Panel(x, y, w, 300);
        r.Text(x + 28, y + 54, done ? "마지막 이름  —  여정의 시작" : "잠시 멈춘 시간", gold, 25);
        r.WrappedText(x + 28, y + 106, w - 56,
                      done
                          ? (answer == 1 ? "당신은 한 사람의 이름과 순례자의 통행패를 품었다."
                                         : "당신은 이해할 수 없는 증언과 순례자의 통행패를 품었다.")
                          : "WASD 이동 · E 대화와 조사 · 1 / 2 선택",
                      ink, 20);
        r.Text(x + 28, y + 200,
               done ? "[E] 첫 레벨 입장   [R] 튜토리얼 다시 시작" : "[ESC] 마을로 돌아가기", muted,
               18);
        if (done)
        {
            r.Text(x + 28, y + 246,
                   "머문 시간  " + std::to_string((int)elapsed / 60) + "분 " +
                       std::to_string((int)elapsed % 60) + "초",
                   muted, 17);
        }
    }
}

void Game::EnterFirstLevel()
{
    ClearInput();
    levelOne.Start(dataDirectory);
    inFirstLevel = true;
}

void Game::UpdatePlayer(float dt)
{
    float oldX = player.x, oldY = player.y;
    if (dialogue.empty() && !choice)
    {
        float sx = (keys['d'] ? 1.f : 0) - (keys['a'] ? 1.f : 0),
              sy = (keys['s'] ? 1.f : 0) - (keys['w'] ? 1.f : 0);
        float length = std::sqrt(sx * sx + sy * sy);
        if (length > 0)
        {
            sx /= length;
            sy /= length;
            if (std::abs(sx) > .1f)
            {
                facing = sx > 0 ? 1.f : -1.f;
            }
            // Inverse isometric projection: W/S remain vertical on screen.
            float dx = sx + 2 * sy, dy = -sx + 2 * sy, n = std::sqrt(dx * dx + dy * dy);
            dx = dx / n * 3.8f * dt;
            dy = dy / n * 3.8f * dt;
            if (!Blocked(player.x + dx, player.y))
            {
                player.x += dx;
            }

            if (!Blocked(player.x, player.y + dy))
            {
                player.y += dy;
            }
        }
    }
    float moved = Dist(player.x, player.y, oldX, oldY);
    motion += (float(moved > .0001f) - motion) * (1 - std::exp(-dt * 14));
    walkPhase += moved * 3.8f;
    if (moved > .001f)
    {
        stepTime += moved;
        if (stepTime > .55f)
        {
            stepTime = 0;
            SpawnFootprint(player.x, player.y);
        }
    }
}

void Game::BuildScene()
{
    auto& world = scene.CreateGroup(scene.Root(), "World");
    auto& ground = scene.CreateGroup(world, "Terrain");
    auto& footprints = scene.CreateGroup(world, "Footprints");
    footprintGroup = &footprints;
    auto& statics = scene.CreateGroup(world, "Village sectors");
    auto& characters = scene.CreateGroup(world, "Characters");
    auto& effects = scene.CreateGroup(world, "Atmosphere");
    auto& ui = scene.CreateGroup(scene.Root(), "UI", true);

    terrain.SetTickEnabled(false);
    atmosphere.SetTickEnabled(false);
    hud.SetTickEnabled(false);
    terrain.SetRenderPasses(ActorPassBit(ActorPass::Ground));
    atmosphere.SetRenderPasses(ActorPassBit(ActorPass::Effects));
    hud.SetRenderPasses(ActorPassBit(ActorPass::UI));
    player.SetRenderPasses(ActorPassBit(ActorPass::World));
    player.SetRenderBounds({-130, -180, 130, 80});
    scene.Attach(terrain, ground);
    scene.Attach(player, characters);
    scene.Attach(atmosphere, effects);
    scene.Attach(hud, ui);

    std::unordered_map<int, Scene::Node*> sectors;
    for (auto& object : objects)
    {
        int key = static_cast<int>(object.x / 8) + static_cast<int>(object.y / 8) * 128;
        auto& sector = sectors[key];
        if (!sector)
        {
            sector = &scene.CreateGroup(statics, "Sector " + std::to_string(key));
        }
        unsigned passes = ActorPassBit(ActorPass::Shadow) | ActorPassBit(ActorPass::World);
        if (object.type == "road" || object.type == "lore")
        {
            passes = 0;
        }
        if (object.type == "lamp")
        {
            passes |= ActorPassBit(ActorPass::Ground) | ActorPassBit(ActorPass::Effects);
        }
        object.SetTickEnabled(false);
        object.SetRenderPasses(passes);
        float margin = std::max(300.f, 180 + (object.w + object.h) * 32 + object.height);
        object.SetRenderBounds({-margin, -margin, margin, margin});
        scene.Attach(object, *sector);
    }
}

void Game::SpawnFootprint(float x, float y)
{
    footsteps.emplace_back(this, x, y, 0);
    scene.Attach(footsteps.back(), *footprintGroup);
}
