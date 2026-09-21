#include "stdafx.h"
#include "LevelOne.h"
#include "Scene.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace
{
constexpr float AttackDuration = .36f;
constexpr float AttackRange = 2.15f;
constexpr float EnemyRange = 1.45f;
constexpr float WindupDuration = .65f;
} // namespace

LevelOne::LevelOne()
{
    player.update = [this](float dt)
    {
        UpdatePlayer(dt);
    };
    player.draw = [this](Renderer& r, ActorPass pass)
    {
        if (pass == ActorPass::World)
        {
            DrawPlayer(r);
        }
    };
    terrain.draw = [this](Renderer& r, ActorPass pass)
    {
        if (pass == ActorPass::Ground)
        {
            DrawGround(r);
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

void LevelOne::ClearInput()
{
    for (bool& key : keys)
    {
        key = false;
    }
}

void LevelOne::Notify(const std::string& text)
{
    notice = text;
    noticeTime = 4;
}

void LevelOne::Start(const std::string& dataDirectory)
{
    if (started)
    {
        return;
    }
    modelCache.Initialize(dataDirectory);
    started = true;
    Generate();
    Notify("첫 레벨 · 변이체를 처치하고 경험치로 능력치를 성장시키세요.");
}

void LevelOne::Generate()
{
    auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto seed = static_cast<std::uint32_t>(ticks) ^ static_cast<std::uint32_t>(++expedition * 7919);
    map.Generate(seed);
    std::mt19937 random(seed ^ 0xBAADF00Du);
    auto start = map.rooms.front();
    player.x = cx = start.x + .5f;
    player.y = cy = start.y + .5f;
    enemies.clear();
    drops.clear();
    props.clear();
    numbers.clear();
    attackTime = attackCooldown = hurtTime = navigationTime = 0;
    phase = movement = 0;
    paused = statPanel = false;
    stats.health = stats.MaximumHealth();
    stats.potions = std::max(stats.potions, 3);
    ClearInput();

    std::vector<RandomLevelMap::Cell> candidates = map.reachable;
    std::shuffle(candidates.begin(), candidates.end(), random);
    for (const auto& cell : candidates)
    {
        float x = cell.x + .5f, y = cell.y + .5f;
        if (std::hypot(x - player.x, y - player.y) < 7 || !map.CanStand(x, y, .35f))
        {
            continue;
        }
        bool free = true;
        for (const auto& other : enemies)
        {
            if (std::hypot(x - other.x, y - other.y) < 3)
            {
                free = false;
                break;
            }
        }
        if (!free)
        {
            continue;
        }
        Enemy enemy(this);
        enemy.x = x;
        enemy.y = y;
        enemy.brute = enemies.size() % 4 == 3;
        enemy.maximumHealth = enemy.health = enemy.brute ? 88 : 48;
        enemy.phase = static_cast<float>(random() % 100);
        enemies.push_back(enemy);
        if (enemies.size() == 14)
        {
            break;
        }
    }

    // A reachable starter item teaches E before the first fight.
    drops.push_back({this, player.x + 1, player.y, true, false});
    for (std::size_t i = 0; i < map.rooms.size(); ++i)
    {
        const auto& room = map.rooms[i];
        props.push_back(
            {this, room.x + 1.5f, room.y + .5f, i == 0 ? ModelId::Shrine : ModelId::Brazier});
    }

    // Blocking decoration is restricted to non-walkable terrain, never inside corridors.
    for (int y = 1; y < RandomLevelMap::Height - 1; ++y)
    {
        for (int x = 1; x < RandomLevelMap::Width - 1; ++x)
        {
            if (!map.Walkable(x, y) && (map.Walkable(x - 1, y) || map.Walkable(x, y - 1)) &&
                random() % 4 == 0)
            {
                props.push_back(
                    {this, x + .5f, y + .5f, random() % 2 == 0 ? ModelId::Pillar : ModelId::Roots});
            }
        }
    }
    distances = map.Distances(static_cast<int>(player.x), static_cast<int>(player.y));
}

void LevelOne::Key(unsigned char key, bool down)
{
    if (key >= 'A' && key <= 'Z')
    {
        key += 32;
    }
    bool previous = keys[key];
    keys[key] = down;
    if (!down || previous)
    {
        return;
    }

    if (stats.health <= 0)
    {
        if (key == 'r')
        {
            Generate();
            Notify("다시 일어섰습니다. 획득한 경험치와 능력치는 유지됩니다.");
        }
        return;
    }
    if (key == 27)
    {
        if (statPanel)
        {
            statPanel = false;
        }
        else
        {
            paused = !paused;
        }
        ClearInput();
        return;
    }
    if (paused || !player.IsActive())
    {
        return;
    }

    if (key == '\t')
    {
        statPanel = !statPanel;
        ClearInput();
        return;
    }
    if (statPanel)
    {
        if (key >= '1' && key <= '3')
        {
            if (stats.Allocate(key - '0'))
            {
                Notify("능력치를 올렸습니다. 전투 수치에 즉시 적용됩니다.");
            }
            else
            {
                Notify("남은 능력치 포인트가 없습니다. 경험치를 더 모으세요.");
            }
        }
        return;
    }
    if (key == 'e')
    {
        Interact();
    }
    else if (key == 'q')
    {
        if (stats.Heal())
        {
            Notify("성혈 회복약 사용 · 체력 40 회복");
        }
        else
        {
            Notify(stats.potions == 0 ? "회복약이 없습니다." : "체력이 가득 차 있습니다.");
        }
    }
    else if (key == 'r')
    {
        const Prop* shrine = FindShrine();
        bool atShrine = shrine && std::hypot(player.x - shrine->x, player.y - shrine->y) <= 3;
        bool cleared = std::all_of(enemies.begin(), enemies.end(),
                                   [](const Enemy& e)
                                   {
                                       return !e.IsActive() || e.dead;
                                   });
        if (atShrine || cleared)
        {
            Generate();
            Notify("새 지형이 생성되었습니다. 경험치·능력치·소지품은 유지됩니다.");
        }
        else
        {
            Notify("새 사냥터는 시작 제단 근처 또는 모든 적 처치 후에 생성할 수 있습니다.");
        }
    }
}

void LevelOne::Move(float& x, float& y, float dx, float dy)
{
    if (map.CanStand(x + dx, y))
    {
        x += dx;
    }
    if (map.CanStand(x, y + dy))
    {
        y += dy;
    }
}

void LevelOne::Kill(Enemy& enemy)
{
    if (!enemy.IsActive() || enemy.dead)
    {
        return;
    }
    enemy.dead = true;
    ++kills;
    int reward = enemy.brute ? 45 : 28;
    int levels = stats.AddExperience(reward);
    drops.push_back({this, enemy.x, enemy.y, kills % 3 == 1, false});
    numbers.push_back(
        {this, enemy.x, enemy.y, 1.6f, "+" + std::to_string(reward) + " 경험치", true});
    if (levels > 0)
    {
        Notify("레벨 " + std::to_string(stats.level) + " 달성! [Tab]에서 능력치를 배분하세요.");
    }
}

void LevelOne::Attack()
{
    for (auto& enemy : enemies)
    {
        if (!enemy.IsActive() || enemy.dead)
        {
            continue;
        }
        float dx = enemy.x - player.x, dy = enemy.y - player.y;
        float distance = std::hypot(dx, dy);
        float alignment = distance > .01f ? (dx * attackAimX + dy * attackAimY) / distance : 1;
        if (distance <= AttackRange && alignment >= .15f &&
            map.Visible(player.x, player.y, enemy.x, enemy.y))
        {
            int damage = std::max(1, stats.Attack() - (enemy.brute ? 5 : 1));
            enemy.health = std::max(0, enemy.health - damage);
            enemy.flash = .18f;
            enemy.cooldown = std::max(enemy.cooldown, .4f);
            numbers.push_back({this, enemy.x, enemy.y, .8f, std::to_string(damage), false});
            if (enemy.health == 0)
            {
                Kill(enemy);
            }
        }
    }
}

void LevelOne::Interact()
{
    Drop* nearest = nullptr;
    float best = 1.8f;
    for (auto& drop : drops)
    {
        float distance = std::hypot(drop.x - player.x, drop.y - player.y);
        if (drop.IsActive() && !drop.collected && distance < best &&
            map.Visible(player.x, player.y, drop.x, drop.y))
        {
            best = distance;
            nearest = &drop;
        }
    }
    if (nearest)
    {
        nearest->collected = true;
        nearest->Destroy();
        ++pickups;
        if (nearest->potion)
        {
            ++stats.potions;
            Notify("성혈 회복약 획득 · [Q]로 사용");
        }
        else
        {
            ++stats.fragments;
            Notify("성혈 파편 획득 · 시작 제단에서 3개로 회복약을 교환할 수 있습니다.");
        }
        return;
    }
    const Prop* shrine = FindShrine();
    if (shrine && std::hypot(player.x - shrine->x, player.y - shrine->y) <= 3)
    {
        stats.health = stats.MaximumHealth();
        if (stats.fragments >= 3)
        {
            stats.fragments -= 3;
            ++stats.potions;
            Notify("제단에서 휴식 · 파편 3개를 회복약 1개로 교환했습니다.");
        }
        else
        {
            Notify("제단에서 체력을 회복했습니다. [R] 새 사냥터 생성");
        }
    }
}

const LevelOne::Prop* LevelOne::FindShrine() const
{
    for (const auto& prop : props)
    {
        if (prop.IsActive() && prop.model == ModelId::Shrine)
        {
            return &prop;
        }
    }
    return nullptr;
}

void LevelOne::UpdateEnemies(float dt)
{
    navigationTime -= dt;
    if (navigationTime <= 0)
    {
        distances = map.Distances(static_cast<int>(player.x), static_cast<int>(player.y));
        navigationTime = .2f;
    }

    for (auto& enemy : enemies)
    {
        enemy.Update(dt);
    }
}

void LevelOne::UpdateEnemy(Enemy& enemy, float dt)
{
    constexpr int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    if (!enemy.IsActive() || enemy.dead || !player.IsActive())
    {
        return;
    }
    enemy.flash = std::max(0.f, enemy.flash - dt);
    enemy.cooldown = std::max(0.f, enemy.cooldown - dt);
    float distance = std::hypot(player.x - enemy.x, player.y - enemy.y);
    if (enemy.windup > 0)
    {
        enemy.windup = std::max(0.f, enemy.windup - dt);
        if (enemy.windup == 0)
        {
            bool hit =
                std::hypot(player.x - enemy.attackX, player.y - enemy.attackY) < EnemyRange &&
                map.Visible(enemy.x, enemy.y, player.x, player.y);
            if (hit && hurtTime <= 0)
            {
                int damage = std::max(1, (enemy.brute ? 22 : 13) - stats.Defense());
                stats.health = std::max(0, stats.health - damage);
                hurtTime = .65f;
                numbers.push_back(
                    {this, player.x, player.y, .8f, "-" + std::to_string(damage), false});
            }
            enemy.cooldown = 1.25f;
        }
        return;
    }
    if (distance < 1.35f && enemy.cooldown <= 0 &&
        map.Visible(player.x, player.y, enemy.x, enemy.y))
    {
        enemy.attackX = player.x;
        enemy.attackY = player.y;
        enemy.windup = WindupDuration;
        return;
    }
    int ex = static_cast<int>(enemy.x), ey = static_cast<int>(enemy.y);
    int cost = distances[ey * RandomLevelMap::Width + ex];
    if (cost < 0 || cost > 11 || distance < 1 || enemy.flash > 0)
    {
        return;
    }
    float tx = player.x, ty = player.y;
    if (!map.CanTravel(enemy.x, enemy.y, player.x, player.y))
    {
        // Center first if the actor's radius cannot safely cut the next corner.
        tx = ex + .5f;
        ty = ey + .5f;
        int best = cost;
        for (const auto& direction : directions)
        {
            int nx = ex + direction[0], ny = ey + direction[1];
            if (map.Walkable(nx, ny) && distances[ny * RandomLevelMap::Width + nx] < best &&
                distances[ny * RandomLevelMap::Width + nx] >= 0 &&
                map.CanTravel(enemy.x, enemy.y, nx + .5f, ny + .5f))
            {
                best = distances[ny * RandomLevelMap::Width + nx];
                tx = nx + .5f;
                ty = ny + .5f;
            }
        }
    }
    float dx = tx - enemy.x, dy = ty - enemy.y, length = std::hypot(dx, dy);
    if (length > .05f)
    {
        float speed = enemy.brute ? 1.15f : 1.65f;
        float step = std::min(length, speed * dt);
        Move(enemy.x, enemy.y, dx / length * step, dy / length * step);
        enemy.phase += dt * 7;
    }
}

void LevelOne::Update(float dt)
{
    if (paused || statPanel || stats.health <= 0)
    {
        return;
    }
    time += dt;
    noticeTime -= dt;
    terrain.Update(dt);
    hud.Update(dt);
    player.Update(dt);
    UpdateEnemies(dt);
    for (auto& prop : props)
    {
        prop.Update(dt);
    }
    for (auto& drop : drops)
    {
        drop.Update(dt);
    }
    for (auto& number : numbers)
    {
        number.Update(dt);
    }
    numbers.erase(std::remove_if(numbers.begin(), numbers.end(),
                                 [](const FloatingText& item)
                                 {
                                     return item.IsDestroyed();
                                 }),
                  numbers.end());
    drops.erase(std::remove_if(drops.begin(), drops.end(),
                               [](const Drop& drop)
                               {
                                   return drop.IsDestroyed();
                               }),
                drops.end());

    cx += (player.x - cx) * (1 - std::exp(-dt * 6));
    cy += (player.y - cy) * (1 - std::exp(-dt * 6));

    if (!completed && stats.level >= 3 && stats.allocated > 0 && pickups > 0)
    {
        completed = true;
        Notify("첫 레벨 목표 달성! 전투와 성장을 계속하거나 제단에서 새 사냥터를 만드세요.");
    }
}

Point LevelOne::Project(float x, float y, float z, Renderer& r) const
{
    return {r.Width() * .5f + (x - y - cx + cy) * 32,
            r.Height() * .5f + (x + y - cx - cy) * 16 - z};
}

void LevelOne::Render(Renderer& r)
{
    Scene scene;
    scene.Add(terrain);
    for (const auto& prop : props)
    {
        scene.Add(prop);
    }
    for (const auto& enemy : enemies)
    {
        scene.Add(enemy);
    }
    for (const auto& drop : drops)
    {
        scene.Add(drop);
    }
    scene.Add(player);
    for (const auto& number : numbers)
    {
        scene.Add(number);
    }
    scene.Add(hud);
    scene.Render(r, time, {(cx - cy) * 32 - r.Width() * .5f, (cx + cy) * 16 - r.Height() * .5f});
}

void LevelOne::DrawGround(Renderer& r)
{
    for (int y = 0; y < RandomLevelMap::Height; ++y)
    {
        for (int x = 0; x < RandomLevelMap::Width; ++x)
        {
            Point p = Project(x + .5f, y + .5f, 0, r);
            if (p.x < -70 || p.x > r.Width() + 70 || p.y < -50 || p.y > r.Height() + 50)
            {
                continue;
            }
            int tile = map.Tile(x, y);
            r.SetMaterial(tile == 2   ? Material::Water
                          : tile == 1 ? Material::Paving
                                      : Material::Earth);
            Color color = tile == 2   ? Color(.12f, .24f, .24f)
                          : tile == 1 ? Color(.23f, .25f, .22f)
                                      : Color(.055f, .08f, .07f);
            r.Quad(Project(static_cast<float>(x), static_cast<float>(y), 0, r),
                   Project(x + 1.f, static_cast<float>(y), 0, r), Project(x + 1.f, y + 1.f, 0, r),
                   Project(static_cast<float>(x), y + 1.f, 0, r), color);
        }
    }
}

void LevelOne::DrawPlayer(Renderer& r)
{
    Point p = Project(player.x, player.y, 0, r);
    r.SoftEllipse(p.x + 5, p.y + 4, 30, 12, Color(0, 0, 0, .7f));
    float attack = attackTime > 0 ? 1 - attackTime / AttackDuration : 0;
    modelCache.Draw(r, ModelId::Pilgrim, p, phase, movement, facing, hurtTime * .4f, attack);
    if (attackTime > 0)
    {
        float angle = std::atan2(attackAimY, attackAimX);
        float end = angle - 1.35f + attack * 2.7f;
        for (int i = 0; i < 12; ++i)
        {
            float a = end - i * .08f, b = a - .08f;
            r.Line(Project(player.x + std::cos(a) * AttackRange,
                           player.y + std::sin(a) * AttackRange, 22, r),
                   Project(player.x + std::cos(b) * AttackRange,
                           player.y + std::sin(b) * AttackRange, 22, r),
                   3, Color(.95f, .80f, .55f, (1 - i / 12.f) * .7f));
        }
    }
}

void LevelOne::DrawEnemy(Renderer& r, const Enemy& enemy, ActorPass pass)
{
    if (pass == ActorPass::Shadow)
    {
        Point p = Project(enemy.x, enemy.y, 0, r);
        r.SoftEllipse(p.x + 8, p.y + 4, 32, 12, Color(0, 0, 0, .65f));
        if (enemy.windup > 0 && !enemy.dead)
        {
            Point warning = Project(enemy.attackX, enemy.attackY, 0, r);
            float progress = 1 - enemy.windup / WindupDuration;
            r.Ellipse(warning.x, warning.y, EnemyRange * 45, EnemyRange * 23,
                      Color(.65f, .03f, .055f, .15f + progress * .2f));
        }
    }
    else if (pass == ActorPass::World)
    {
        Point p = Project(enemy.x, enemy.y, 0, r);
        if (enemy.dead)
        {
            r.Ellipse(p.x, p.y - 2, 19, 6, Color(.24f, .055f, .06f, .75f));
        }
        else
        {
            modelCache.Draw(r, enemy.brute ? ModelId::Brute : ModelId::Infected, p, enemy.phase,
                            enemy.windup > 0 ? .12f : .6f,
                            player.x - player.y > enemy.x - enemy.y ? 1.f : -1.f, enemy.flash * 4);
        }
    }
    else if (pass == ActorPass::Overlay)
    {
        if (enemy.dead || std::hypot(enemy.x - player.x, enemy.y - player.y) > 8)
        {
            return;
        }
        Point p = Project(enemy.x, enemy.y, enemy.brute ? 92.f : 84.f, r);
        r.Rect(p.x - 23, p.y, 46, 4, Color(.06f, .07f, .07f));
        r.Rect(p.x - 23, p.y, 46.f * enemy.health / enemy.maximumHealth, 4,
               Color(.72f, .16f, .17f));
    }
}

void LevelOne::DrawProp(Renderer& r, const Prop& prop, ActorPass pass)
{
    if (pass == ActorPass::Shadow)
    {
        Point p = Project(prop.x, prop.y, 0, r);
        if (p.x > -160 && p.x < r.Width() + 160 && p.y > -100 && p.y < r.Height() + 180)
        {
            r.SoftEllipse(p.x + 17, p.y + 10, 46, 18, Color(0, 0, 0, .50f));
        }
    }
    else if (pass == ActorPass::World)
    {
        Point p = Project(prop.x, prop.y, 0, r);
        if (p.x < -160 || p.x > r.Width() + 160 || p.y < -100 || p.y > r.Height() + 180)
        {
            return;
        }
        modelCache.Draw(r, prop.model, p);
        if (prop.model == ModelId::Brazier)
        {
            r.Effect(p.x - 23, p.y - 87, 46, 60, Material::Flame, Color(1, .5f, .18f));
        }
    }
}

void LevelOne::DrawDrop(Renderer& r, const Drop& drop)
{
    Point p = Project(drop.x, drop.y, 2 + std::sin(time * 2 + drop.x) * 2, r);
    r.SoftEllipse(p.x, p.y, 30, 13, Color(.65f, .40f, .17f, .22f));
    modelCache.Draw(r, drop.potion ? ModelId::Potion : ModelId::Fragment, p);
}

void LevelOne::DrawNumber(Renderer& r, const FloatingText& number)
{
    Point p = Project(number.x, number.y, 65 + (1.6f - number.remaining) * 15, r);
    r.Text(p.x - 20, p.y, number.text,
           number.reward ? Color(.83f, .72f, .39f) : Color(.95f, .72f, .62f), 17);
}

void LevelOne::DrawUI(Renderer& r)
{
    const Color ink(.88f, .87f, .79f), muted(.60f, .65f, .61f), gold(.78f, .64f, .39f);
    r.Panel(18, 16, 475, 138);
    r.Text(34, 44, "첫 레벨 · 성혈의 사냥터", gold, 22);
    r.Text(34, 72,
           "레벨 " + std::to_string(stats.level) + "   체력 " + std::to_string(stats.health) +
               " / " + std::to_string(stats.MaximumHealth()),
           ink, 18);
    r.Rect(34, 82, 280, 9, Color(.13f, .08f, .08f));
    r.Rect(34, 82, 280.f * stats.health / stats.MaximumHealth(), 9, Color(.65f, .13f, .16f));
    r.Text(34, 118,
           stats.level >= RpgStats::MaximumLevel
               ? "최대 레벨"
               : "경험치 " + std::to_string(stats.experience) + " / " +
                     std::to_string(stats.RequiredExperience()),
           muted, 17);
    r.Rect(34, 130, 280, 5, Color(.12f, .14f, .12f));
    r.Rect(34, 130,
           stats.level >= RpgStats::MaximumLevel
               ? 280.f
               : 280.f * stats.experience / stats.RequiredExperience(),
           5, gold);
    r.Text(332, 89, "공격 " + std::to_string(stats.Attack()), ink, 17);
    r.Text(332, 116, "방어 " + std::to_string(stats.Defense()), ink, 17);
    r.Text(24, 183,
           completed ? "성장 목표 달성 · 자유롭게 경험치를 모으세요"
                     : "목표: 레벨 3 달성 · 아이템 획득 · 능력치 배분",
           gold, 18);
    if (noticeTime > 0)
    {
        r.WrappedText(24, 217, r.Width() - 48.f, notice, ink, 17);
    }
    r.Text(24, r.Height() - 64.f, "WASD 이동 · Space 공격 · E 획득/제단 · Q 회복 · Tab 능력치", ink,
           17);
    r.Text(24, r.Height() - 35.f,
           "회복약 " + std::to_string(stats.potions) + "   파편 " +
               std::to_string(stats.fragments) + "   처치 " + std::to_string(kills) +
               "   남은 포인트 " + std::to_string(stats.points),
           gold, 17);

    // The minimap exposes the connected floor network and the nearest combat opportunities.
    if (r.Width() >= 900)
    {
        float mx = r.Width() - 172.f, my = 24;
        r.Panel(mx - 12, my - 8, 158, 176);
        for (int y = 0; y < RandomLevelMap::Height; ++y)
        {
            for (int x = 0; x < RandomLevelMap::Width; ++x)
            {
                if (map.Walkable(x, y))
                {
                    r.Rect(mx + x * 3.4f, my + y * 3.4f, 3.3f, 3.3f,
                           map.Tile(x, y) == 2 ? Color(.19f, .35f, .36f) : Color(.28f, .31f, .26f));
                }
            }
        }
        for (const auto& enemy : enemies)
        {
            if (enemy.IsVisible() && !enemy.dead)
            {
                r.Rect(mx + enemy.x * 3.4f - 1, my + enemy.y * 3.4f - 1, 3, 3,
                       Color(.8f, .16f, .16f));
            }
        }
        if (player.IsVisible())
        {
            r.Rect(mx + player.x * 3.4f - 2, my + player.y * 3.4f - 2, 4, 4, gold);
        }
        r.Text(mx, my + 145, "노랑: 나  빨강: 적", muted, 14);
    }
    if (statPanel)
    {
        r.Rect(0, 0, static_cast<float>(r.Width()), static_cast<float>(r.Height()),
               Color(0, 0, 0, .75f));
        float width = std::min(700.f, r.Width() - 40.f), x = (r.Width() - width) / 2, y = 160;
        r.Panel(x, y, width, 354);
        r.Text(x + 24, y + 43, "능력치 배분  ·  남은 포인트 " + std::to_string(stats.points), gold,
               23);
        r.Text(x + 24, y + 87, "레벨업마다 2포인트 · 레벨당 체력 +5 / 공격 +2", muted, 18);
        r.Text(x + 24, y + 140, "[1] 힘 " + std::to_string(stats.strength) + "   → 공격력 +4", ink,
               22);
        r.Text(x + 24, y + 195, "[2] 생명 " + std::to_string(stats.vitality) + "   → 최대 체력 +15",
               ink, 22);
        r.Text(x + 24, y + 250, "[3] 저항 " + std::to_string(stats.resilience) + "   → 방어력 +2",
               ink, 22);
        r.Text(x + 24, y + 316, "[Tab / ESC] 돌아가기 · 능력치 창에서는 전투가 멈춥니다.", muted,
               17);
    }
    if (paused || stats.health <= 0)
    {
        r.Rect(0, 0, static_cast<float>(r.Width()), static_cast<float>(r.Height()),
               Color(0, 0, 0, .78f));
        r.Panel(50, 160, std::min(740.f, r.Width() - 100.f), 252);
        r.Text(76, 210, stats.health <= 0 ? "육체가 무너졌습니다" : "잠시 멈춘 사냥", gold, 25);
        r.WrappedText(76, 260, r.Width() - 152.f,
                      stats.health <= 0
                          ? "[R] 재도전 · 획득한 경험치와 능력치는 유지되고 맵은 새로 생성됩니다."
                          : "[ESC] 계속 · 시작 제단에서 E 회복 / R 새 사냥터",
                      ink, 19);
        r.Text(76, 355, modelCache.Status(), muted, 16);
    }
}

void LevelOne::UpdatePlayer(float dt)
{
    hurtTime = std::max(0.f, hurtTime - dt);
    attackCooldown = std::max(0.f, attackCooldown - dt);
    if (keys[' '] && attackCooldown <= 0)
    {
        attackTime = AttackDuration;
        attackCooldown = .55f;
        attackResolved = false;
        attackAimX = aimX;
        attackAimY = aimY;
    }
    float sx = (keys['d'] ? 1.f : 0) - (keys['a'] ? 1.f : 0);
    float sy = (keys['s'] ? 1.f : 0) - (keys['w'] ? 1.f : 0);
    float dx = sx + 2 * sy, dy = -sx + 2 * sy, length = std::hypot(dx, dy);
    float oldX = player.x, oldY = player.y;
    if (length > 0)
    {
        aimX = dx / length;
        aimY = dy / length;
        if (sx != 0)
        {
            facing = sx > 0 ? 1.f : -1.f;
        }
        float speed = attackTime > 0 ? 1.5f : 3.7f;
        Move(player.x, player.y, aimX * speed * dt, aimY * speed * dt);
    }
    float moved = std::hypot(player.x - oldX, player.y - oldY);
    movement += ((moved > .001f ? 1.f : 0.f) - movement) * (1 - std::exp(-dt * 12));
    phase += moved * 4;
    if (attackTime > 0)
    {
        attackTime = std::max(0.f, attackTime - dt);
        if (!attackResolved && attackTime <= AttackDuration - .10f)
        {
            attackResolved = true;
            Attack();
        }
    }
}
