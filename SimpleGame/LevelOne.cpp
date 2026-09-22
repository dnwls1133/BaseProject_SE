#include "stdafx.h"
#include "LevelOne.h"
#include "ContentData.h"
#include <iostream>
#include "Scene.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

LevelOne::LevelOne()
{
    player.update = [this](float dt)
    {
        UpdatePlayer(dt);
    };
    player.draw = [this](Renderer& r, ActorPass pass)
    {
        DrawPlayer(r, pass);
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
    navigation.update = [this](float dt)
    {
        UpdateNavigation(dt);
    };
    minimap.draw = [this](Renderer& r, ActorPass pass)
    {
        if (pass == ActorPass::UI)
        {
            DrawMinimap(r);
        }
    };
    menus.draw = [this](Renderer& r, ActorPass pass)
    {
        if (pass == ActorPass::UI)
        {
            DrawMenus(r);
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

bool LevelOne::Start(const std::string& dataDirectory, const std::string& levelFile)
{
    try
    {
        std::string entry = levelFile;
        if (entry.empty())
        {
            ContentData::Load(ContentData::Path(dataDirectory, "world.catalog"),
                              [&](const std::string& command, ContentData::Row& row)
                              {
                                  ContentData::Require(command == "entry" && entry.empty(),
                                                       "one world entry required");
                                  entry = row.Word();
                              });
            ContentData::Require(!entry.empty(), "world.catalog: missing entry");
        }
        if (started && contentRoot == dataDirectory && worldFile == entry)
        {
            return true;
        }
        modelCache.Initialize(dataDirectory);
        contentRoot = dataDirectory;
        worldFile = entry;
        if (!Generate())
        {
            return false;
        }
        started = true;
        Notify(content.introduction);
        return true;
    }
    catch (const std::exception& error)
    {
        contentError = error.what();
        std::cerr << "Content load error: " << contentError << std::endl;
        return false;
    }
}

bool LevelOne::Generate()
{
    // Read and resolve a complete candidate before invalidating any scene references.
    LevelData nextContent;
    RandomLevelMap nextMap;
    auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto seed =
        static_cast<std::uint32_t>(ticks) ^ static_cast<std::uint32_t>((expedition + 1) * 7919);
    try
    {
        nextContent.Load(contentRoot, worldFile, modelCache);
        ContentData::Require(stats.level <= nextContent.rules.maximumLevel,
                             "growth limit below current level");
        nextMap.Generate(seed, nextContent);
    }
    catch (const std::exception& error)
    {
        contentError = error.what();
        std::cerr << "Content load error: " << contentError << std::endl;
        Notify("외부 데이터 로딩 실패 · 기존 월드를 유지합니다. 콘솔의 파일명과 행을 확인하세요.");
        return false;
    }
    contentError.clear();
    scene.Clear();
    lootGroup = numberGroup = minimapGroup = nullptr;
    content = std::move(nextContent);
    map = std::move(nextMap);
    stats.rules = content.rules;
    completed = stats.level >= content.goalLevel && stats.allocated >= content.goalAllocated &&
                pickups >= content.goalPickups;
    ++expedition;
    std::mt19937 random(seed ^ 0xBAADF00Du);
    player.x = cx = content.startX + .5f;
    player.y = cy = content.startY + .5f;
    enemies.clear();
    drops.clear();
    props.clear();
    numbers.clear();
    attackTime = attackCooldown = hurtTime = navigationTime = 0;
    phase = movement = 0;
    paused = statPanel = false;
    stats.health = stats.MaximumHealth();
    stats.potions = std::max(stats.potions, stats.rules.minimumPotions);
    ClearInput();

    std::vector<RandomLevelMap::Cell> candidates = map.reachable;
    std::shuffle(candidates.begin(), candidates.end(), random);
    for (const auto& district : map.districts)
    {
        std::size_t count = 0;
        for (const auto& cell : candidates)
        {
            if (count >= district.population.size())
            {
                break;
            }
            float x = cell.x + .5f, y = cell.y + .5f;
            if (std::abs(cell.x - district.center.x) > district.radiusX - 1 ||
                std::abs(cell.y - district.center.y) > district.radiusY - 1 ||
                std::hypot(x - player.x, y - player.y) < 10 || !map.CanStand(x, y, .35f))
            {
                continue;
            }
            bool free = std::all_of(enemies.begin(), enemies.end(),
                                    [&](const Enemy& other)
                                    {
                                        return std::hypot(x - other.x, y - other.y) >= 3;
                                    });
            if (!free)
            {
                continue;
            }
            Enemy enemy(this);
            enemy.type = content.Enemy(district.population[count]);
            enemy.SetPosition(x, y);
            enemy.maximumHealth = enemy.health = enemy.type.health;
            enemy.phase = static_cast<float>(random() % 100);
            enemies.push_back(enemy);
            ++count;
        }
    }
    // Keep the teaching item on valid ground even when the authored start changes.
    for (const auto& cell : map.reachable)
    {
        if (std::hypot(cell.x + .5f - player.x, cell.y + .5f - player.y) <= 1.5f &&
            map.CanStand(cell.x + .5f, cell.y + .5f))
        {
            SpawnDrop(cell.x + .5f, cell.y + .5f, true);
            break;
        }
    }
    for (const auto& placement : map.placements)
    {
        const auto& type = content.Prop(placement.id);
        props.emplace_back(this, placement.x, placement.y, type);
        auto& prop = props.back();
        prop.scale = placement.scale;
        prop.facing = placement.facing;
        if (type.ground)
        {
            prop.SetRenderPasses(ActorPassBit(ActorPass::Ground));
        }
    }
    distances = map.Distances(static_cast<int>(player.x), static_cast<int>(player.y));
    BuildScene();
    return true;
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
            if (!Generate())
            {
                return;
            }
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
            Notify("성혈 회복약 사용 · 체력 " + std::to_string(stats.rules.healing) + " 회복");
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
            if (!Generate())
            {
                return;
            }
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
    int reward = enemy.type.experience;
    int levels = stats.AddExperience(reward);
    SpawnDrop(enemy.x, enemy.y, (kills - 1) % stats.rules.dropPeriod == 0);
    SpawnNumber(enemy.x, enemy.y, 1.6f, "+" + std::to_string(reward) + " 경험치", true);
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
        if (distance <= stats.rules.attackRange && alignment >= .15f &&
            map.Visible(player.x, player.y, enemy.x, enemy.y))
        {
            int damage = std::max(1, stats.Attack() - enemy.type.defense);
            enemy.health = std::max(0, enemy.health - damage);
            enemy.flash = .18f;
            enemy.cooldown = std::max(enemy.cooldown, .4f);
            SpawnNumber(enemy.x, enemy.y, .8f, std::to_string(damage), false);
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
            Notify("성혈 파편 획득 · 제단에서 " + std::to_string(stats.rules.exchangeCost) +
                   "개로 회복약을 교환합니다.");
        }
        return;
    }
    const Prop* shrine = FindShrine();
    if (shrine && std::hypot(player.x - shrine->x, player.y - shrine->y) <= 3)
    {
        stats.health = stats.MaximumHealth();
        if (stats.fragments >= stats.rules.exchangeCost)
        {
            stats.fragments -= stats.rules.exchangeCost;
            ++stats.potions;
            Notify("제단에서 휴식 · 파편 " + std::to_string(stats.rules.exchangeCost) +
                   "개를 회복약으로 교환했습니다.");
        }
        else
        {
            Notify("제단에서 체력을 회복했습니다. [R] 새 사냥터 생성");
        }
    }
}

const LevelOne::Prop* LevelOne::FindShrine() const
{
    const Prop* nearest = nullptr;
    float best = 1000000.f;
    for (const auto& prop : props)
    {
        if (prop.IsActive() && prop.type.role == "shrine")
        {
            float distance = std::hypot(prop.x - player.x, prop.y - player.y);
            if (distance < best)
            {
                nearest = &prop;
                best = distance;
            }
        }
    }
    return nearest;
}

void LevelOne::UpdateNavigation(float dt)
{
    navigationTime -= dt;
    if (navigationTime <= 0)
    {
        distances = map.Distances(static_cast<int>(player.x), static_cast<int>(player.y));
        navigationTime = .2f;
    }
}

void LevelOne::UpdateEnemy(Enemy& enemy, float dt)
{
    constexpr int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    if (enemy.dead)
    {
        enemy.collapse = std::min(1.f, enemy.collapse + dt * 3.5f);
        if (enemy.collapse >= 1)
        {
            enemy.SetRenderPasses(ActorPassBit(ActorPass::Ground));
            enemy.SetTickEnabled(false);
        }
        return;
    }
    if (!enemy.IsActive() || !player.IsActive())
    {
        return;
    }
    enemy.movement *= std::exp(-dt * 10);
    enemy.phase += dt * .9f;
    enemy.strikeTime = std::max(0.f, enemy.strikeTime - dt);
    enemy.facing = player.x - player.y > enemy.x - enemy.y ? 1.f : -1.f;
    enemy.flash = std::max(0.f, enemy.flash - dt);
    enemy.cooldown = std::max(0.f, enemy.cooldown - dt);
    float distance = std::hypot(player.x - enemy.x, player.y - enemy.y);
    if (enemy.windup > 0)
    {
        enemy.windup = std::max(0.f, enemy.windup - dt);
        if (enemy.windup == 0)
        {
            bool hit =
                std::hypot(player.x - enemy.attackX, player.y - enemy.attackY) < enemy.type.range &&
                map.Visible(enemy.x, enemy.y, player.x, player.y);
            if (hit && hurtTime <= 0)
            {
                int damage = std::max(1, enemy.type.attack - stats.Defense());
                stats.health = std::max(0, stats.health - damage);
                hurtTime = stats.rules.invulnerability;
                SpawnNumber(player.x, player.y, .8f, "-" + std::to_string(damage), false);
            }
            enemy.cooldown = enemy.type.cooldown;
            enemy.strikeTime = .24f;
        }
        return;
    }
    if (distance < enemy.type.range - .1f && enemy.cooldown <= 0 &&
        map.Visible(player.x, player.y, enemy.x, enemy.y))
    {
        enemy.attackX = player.x;
        enemy.attackY = player.y;
        enemy.windup = enemy.WindupDuration();
        return;
    }
    int ex = static_cast<int>(enemy.x), ey = static_cast<int>(enemy.y);
    int cost = distances[ey * map.Width + ex];
    if (cost < 0 || cost > enemy.type.aggro || distance < 1 || enemy.flash > 0)
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
            if (map.Walkable(nx, ny) && distances[ny * map.Width + nx] < best &&
                distances[ny * map.Width + nx] >= 0 &&
                map.CanTravel(enemy.x, enemy.y, nx + .5f, ny + .5f))
            {
                best = distances[ny * map.Width + nx];
                tx = nx + .5f;
                ty = ny + .5f;
            }
        }
    }
    float dx = tx - enemy.x, dy = ty - enemy.y, length = std::hypot(dx, dy);
    if (length > .05f)
    {
        float speed = enemy.type.speed;
        float step = std::min(length, speed * dt);
        float oldX = enemy.x, oldY = enemy.y;
        Move(enemy.x, enemy.y, dx / length * step, dy / length * step);
        float moved = std::hypot(enemy.x - oldX, enemy.y - oldY);
        if (moved > .001f)
        {
            enemy.phase += moved * enemy.type.gait;
            enemy.movement = std::min(1.f, enemy.movement + dt * 14);
        }
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
    scene.Update(dt);
    numbers.remove_if(
        [](const FloatingText& number)
        {
            return number.IsDestroyed();
        });
    drops.remove_if(
        [](const Drop& drop)
        {
            return drop.IsDestroyed();
        });

    cx += (player.x - cx) * (1 - std::exp(-dt * 6));
    cy += (player.y - cy) * (1 - std::exp(-dt * 6));

    if (!completed && stats.level >= content.goalLevel &&
        stats.allocated >= content.goalAllocated && pickups >= content.goalPickups)
    {
        completed = true;
        Notify("현재 지역 목표 달성! 전투와 성장을 계속하거나 제단에서 새 사냥터를 만드세요.");
    }
}

Point LevelOne::Project(float x, float y, float z, Renderer& r) const
{
    return {r.Width() * .5f + (x - y - cx + cy) * 32,
            r.Height() * .5f + (x + y - cx - cy) * 16 - z};
}

void LevelOne::Render(Renderer& r)
{
    minimap.SetVisible(r.Width() >= 900);
    minimap.SetPosition(r.Width() - 184.f, 16);
    minimapGroup->SetClipRect({minimap.x, minimap.y, minimap.x + 158, minimap.y + 176});
    scene.Render(r, time, {(cx - cy) * 32 - r.Width() * .5f, (cx + cy) * 16 - r.Height() * .5f});
}

void LevelOne::DrawGround(Renderer& r)
{
    using Region = RandomLevelMap::Region;
    for (int y = 0; y < map.Height; ++y)
    {
        for (int x = 0; x < map.Width; ++x)
        {
            Point p = Project(x + .5f, y + .5f, 0, r);
            if (p.x < -70 || p.x > r.Width() + 70 || p.y < -60 || p.y > r.Height() + 60)
            {
                continue;
            }
            int tile = map.Tile(x, y);
            const auto& regionData = map.DistrictAt(x, y);
            auto region = regionData.region;
            std::uint32_t hash = (static_cast<std::uint32_t>(x) * 73856093u) ^
                                 (static_cast<std::uint32_t>(y) * 19349663u) ^ map.seed;
            hash = (hash ^ (hash >> 13)) * 1274126177u;
            float variation = .87f + (hash % 23) * .01f;
            Color color = regionData.floor;
            if (tile == 2)
            {
                color = regionData.water;
            }
            else if (tile == 0)
            {
                color = Color(.09f, .12f, .105f);
            }
            else if (tile == 4)
            {
                color = Color(color.r * .92f + .025f, color.g * .84f, color.b * .75f);
            }
            else if (tile == 3)
            {
                color = Color(color.r * .76f, color.g * .85f, color.b * .72f);
            }
            color.r *= variation;
            color.g *= variation;
            color.b *= variation;
            Point a = Project(static_cast<float>(x), static_cast<float>(y), 0, r);
            Point b = Project(x + 1.f, static_cast<float>(y), 0, r);
            Point c = Project(x + 1.f, y + 1.f, 0, r);
            Point d = Project(static_cast<float>(x), y + 1.f, 0, r);
            r.SetMaterial(tile == 2   ? Material::Water
                          : tile == 1 ? Material::Paving
                                      : Material::Earth);
            r.Quad(a, b, c, d, color);
            r.SetMaterial(Material::Plain);
            if (tile == 0)
            {
                continue;
            }

            // Curbs belong to surviving paving, not the open meadow boundary.
            if (tile == 1)
            {
                auto curb = [&](Point from, Point to)
                {
                    r.Line({from.x, from.y + 2}, {to.x, to.y + 2}, 3, Color(.09f, .12f, .11f));
                    r.Line(from, to, 1.5f, Color(.40f, .41f, .33f, .5f));
                };
                if (map.Tile(x - 1, y) != 1)
                {
                    curb(a, d);
                }
                if (map.Tile(x + 1, y) != 1)
                {
                    curb(b, c);
                }
                if (map.Tile(x, y - 1) != 1)
                {
                    curb(a, b);
                }
                if (map.Tile(x, y + 1) != 1)
                {
                    curb(d, c);
                }
            }
            if (tile == 3 && hash % 3 == 0)
            {
                float offset = static_cast<float>((hash >> 8) % 25) - 12;
                Color grass(color.r + .07f, color.g + .08f, color.b + .045f, .45f);
                r.Line({p.x + offset, p.y + 4}, {p.x + offset - 3, p.y - 2}, 1, grass);
                r.Line({p.x + offset, p.y + 4}, {p.x + offset + 2, p.y - 4}, 1, grass);
            }
            if (tile == 2)
            {
                if (map.Tile(x, y - 1) != 2 && map.Walkable(x, y - 1))
                {
                    r.Line(a, b, 1.3f, Color(.47f, .55f, .47f, .4f));
                }
                continue;
            }

            // Processional inlays tie each district to the religious empire.
            for (const auto& district : map.districts)
            {
                if (district.region != Region::Refuge && district.region != Region::Cathedral &&
                    district.region != Region::Processional)
                {
                    continue;
                }
                if (tile == 1 && std::abs(x - district.center.x) <= district.radiusX &&
                    std::abs(y - district.center.y) <= district.radiusY &&
                    (x == district.center.x || y == district.center.y))
                {
                    r.Quad({p.x, p.y - 6}, {p.x + 12, p.y}, {p.x, p.y + 6}, {p.x - 12, p.y},
                           Color(.48f, .40f, .26f, .30f));
                    r.Line({p.x - 12, p.y}, {p.x, p.y + 6}, 1, Color(.63f, .53f, .33f, .45f));
                    break;
                }
            }
            if (tile == 1 && hash % 5 == 0)
            {
                float offset = static_cast<float>((hash >> 8) % 17) - 8;
                r.Line({p.x - 18, p.y - 5}, {p.x + offset, p.y + 1}, 1.2f,
                       Color(.08f, .11f, .10f, .7f));
                r.Line({p.x + offset, p.y + 1}, {p.x + 13, p.y + 10}, 1,
                       Color(.08f, .11f, .10f, .6f));
            }
            if (hash % 11 == 0)
            {
                r.Effect(p.x - 29, p.y - 13, 58, 26, Material::SoftLight,
                         region == Region::Garden ? Color(.42f, .045f, .09f, .45f)
                                                  : Color(.055f, .12f, .09f, .6f));
            }
        }
    }
    r.SetMaterial(Material::Plain);
}

void LevelOne::DrawAtmosphere(Renderer& r)
{
    for (std::size_t index = 0; index < map.districts.size(); ++index)
    {
        const auto& district = map.districts[index];
        Point p = Project(district.center.x + .5f, district.center.y + .5f, 0, r);
        if (p.x < -550 || p.x > r.Width() + 550 || p.y < -350 || p.y > r.Height() + 350)
        {
            continue;
        }
        bool fungal = district.region == RandomLevelMap::Region::Garden;
        r.Effect(p.x - 440, p.y - 145, 880, 310, Material::Mist, district.fog);
        // Sparse drifting ash/spores. The aggregate atmosphere actor owns this visual layer.
        for (int i = 0; i < 9; ++i)
        {
            float seed = static_cast<float>(i * 71 + index * 37);
            float travel = std::fmod(time * (4 + i % 3) + seed * 3, 180.f);
            float x = p.x + std::sin(seed) * 280 + std::sin(time * .3f + seed) * 12;
            float y = p.y + std::cos(seed * 1.7f) * 105 - travel;
            float alpha = std::sin(travel / 180.f * 3.14159265f) * .45f;
            r.Effect(x - 4, y - 4, 8, 8, Material::SoftLight,
                     fungal ? Color(1.2f, .32f, .37f, alpha) : Color(.7f, .79f, .70f, alpha));
        }
    }
}

void LevelOne::DrawPlayer(Renderer& r, ActorPass pass)
{
    Point p = Project(player.x, player.y, 0, r);
    if (pass == ActorPass::Shadow)
    {
        r.Effect(p.x - 30, p.y - 8, 70, 27, Material::SoftLight, Color(0, 0, 0, .85f));
        return;
    }
    float attack = attackTime > 0 ? 1 - attackTime / stats.rules.attackDuration : 0;
    modelCache.Draw(r, ModelId::Pilgrim, p, phase, movement, facing, hurtTime * .4f, attack, 1, 1,
                    0, time);
    if (attackTime > 0)
    {
        float angle = std::atan2(attackAimY, attackAimX);
        float end = angle - 1.35f + attack * 2.7f;
        for (int i = 0; i < 12; ++i)
        {
            float a = end - i * .08f, b = a - .08f;
            r.Line(Project(player.x + std::cos(a) * stats.rules.attackRange,
                           player.y + std::sin(a) * stats.rules.attackRange, 22, r),
                   Project(player.x + std::cos(b) * stats.rules.attackRange,
                           player.y + std::sin(b) * stats.rules.attackRange, 22, r),
                   3, Color(.95f, .80f, .55f, (1 - i / 12.f) * .7f));
        }
    }
}

void LevelOne::DrawEnemy(Renderer& r, const Enemy& enemy, ActorPass pass)
{
    Point p = Project(enemy.x, enemy.y, 0, r);
    if (pass == ActorPass::Shadow)
    {
        float width = enemy.type.shadow;
        r.Effect(p.x - width * .5f + 6, p.y - 10, width, 33, Material::SoftLight,
                 Color(0, 0, 0, .85f));
        if (enemy.windup > 0 && !enemy.dead)
        {
            Point warning = Project(enemy.attackX, enemy.attackY, 0, r);
            float progress = 1 - enemy.windup / enemy.WindupDuration();
            r.Effect(warning.x - enemy.type.range * 45, warning.y - enemy.type.range * 23,
                     enemy.type.range * 90, enemy.type.range * 46, Material::SoftLight,
                     Color(.95f, .035f, .07f, .25f + progress * .55f));
            for (int i = 0; i < 32; ++i)
            {
                float a = i * 6.2831853f / 32, b = (i + 1) * 6.2831853f / 32;
                r.Line({warning.x + std::cos(a) * enemy.type.range * 45,
                        warning.y + std::sin(a) * enemy.type.range * 23},
                       {warning.x + std::cos(b) * enemy.type.range * 45,
                        warning.y + std::sin(b) * enemy.type.range * 23},
                       1.5f, Color(.85f, .17f, .15f, .25f + progress * .55f));
            }
        }
    }
    else if (pass == ActorPass::World || pass == ActorPass::Ground)
    {
        ModelId model = enemy.type.model;
        float attack = enemy.windup > 0 ? .15f + .35f * (1 - enemy.windup / enemy.WindupDuration())
                       : enemy.strikeTime > 0 ? 1 - .5f * enemy.strikeTime / .24f
                                              : 0;
        if (enemy.dead)
        {
            r.Effect(p.x - 40, p.y - 14, 80, 28, Material::SoftLight,
                     Color(.32f, .025f, .055f, .7f * enemy.collapse));
        }
        modelCache.Draw(r, model, p, enemy.phase, enemy.dead ? 0 : enemy.movement, enemy.facing,
                        enemy.dead ? 0 : enemy.flash * 4, enemy.dead ? 0 : attack, 1, 1,
                        enemy.collapse);
    }
    else if (pass == ActorPass::Overlay)
    {
        if (enemy.dead || std::hypot(enemy.x - player.x, enemy.y - player.y) > 8 ||
            (enemy.health == enemy.maximumHealth && enemy.windup <= 0))
        {
            return;
        }
        p = Project(enemy.x, enemy.y, enemy.type.barHeight, r);
        r.Rect(p.x - 25, p.y, 50, 5, Color(.06f, .07f, .07f, .9f));
        r.Rect(p.x - 24, p.y + 1, 48.f * enemy.health / enemy.maximumHealth, 3,
               Color(.72f, .16f, .17f));
        const std::string& name = enemy.type.name;
        r.Text(p.x - r.TextWidth(name, 13) * .5f, p.y - 5, name, Color(.78f, .73f, .62f), 13);
    }
}

void LevelOne::DrawProp(Renderer& r, const Prop& prop, ActorPass pass)
{
    Point p = Project(prop.x, prop.y, 0, r);
    if (pass == ActorPass::Ground)
    {
        modelCache.Draw(r, prop.model, p, 0, 0, prop.facing, 0, 0, prop.scale);
        return;
    }
    if (pass == ActorPass::Shadow)
    {
        float width = prop.type.shadow * prop.scale;
        r.Effect(p.x - width * .5f + 18, p.y - 13, width, 48, Material::SoftLight,
                 Color(0, 0, 0, .75f));
        if (prop.type.role == "brazier" || prop.type.role == "shrine")
        {
            float pulse = .36f + std::sin(time * 3.7f + prop.x) * .035f;
            r.Effect(p.x - 145, p.y - 65, 290, 130, Material::SoftLight,
                     Color(.95f, .40f, .12f, pulse));
        }
        else if (prop.type.role == "fungal")
        {
            r.Effect(p.x - 70, p.y - 25, 140, 50, Material::SoftLight,
                     Color(.65f, .075f, .15f, .2f));
        }
        return;
    }
    if (pass == ActorPass::World)
    {
        Point hero = Project(player.x, player.y, 30, r);
        float opacity = 1;
        bool tall = prop.type.tall;
        if (tall && player.Depth() < prop.Depth() && std::abs(hero.x - p.x) < 72 * prop.scale &&
            hero.y < p.y && hero.y > p.y - 145 * prop.scale)
        {
            opacity = .35f;
        }
        modelCache.Draw(r, prop.model, p, 0, 0, prop.facing, 0, 0, prop.scale, opacity);
        if (prop.type.role == "brazier")
        {
            r.Effect(p.x - 23 * prop.scale, p.y - 87 * prop.scale, 46 * prop.scale, 60 * prop.scale,
                     Material::Flame, Color(1, .5f, .18f));
            r.Effect(p.x - 47, p.y - 96, 94, 94, Material::SoftLight,
                     Color(1.4f, .47f, .13f, .12f));
        }
    }
}

void LevelOne::DrawDrop(Renderer& r, const Drop& drop)
{
    Point p = Project(drop.x, drop.y, 2 + std::sin(time * 2 + drop.x) * 2, r);
    r.Effect(p.x - 35, p.y - 15, 70, 30, Material::SoftLight, Color(.85f, .53f, .2f, .35f));
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
    r.Text(34, 44, map.RegionName(static_cast<int>(player.x), static_cast<int>(player.y)), gold,
           22);
    r.Text(34, 72,
           "레벨 " + std::to_string(stats.level) + "   체력 " + std::to_string(stats.health) +
               " / " + std::to_string(stats.MaximumHealth()),
           ink, 18);
    r.Rect(34, 82, 280, 9, Color(.13f, .08f, .08f));
    r.Rect(34, 82, 280.f * stats.health / stats.MaximumHealth(), 9, Color(.65f, .13f, .16f));
    r.Text(34, 118,
           stats.level >= stats.rules.maximumLevel
               ? "최대 레벨"
               : "경험치 " + std::to_string(stats.experience) + " / " +
                     std::to_string(stats.RequiredExperience()),
           muted, 17);
    r.Rect(34, 130, 280, 5, Color(.12f, .14f, .12f));
    r.Rect(34, 130,
           stats.level >= stats.rules.maximumLevel
               ? 280.f
               : 280.f * stats.experience / stats.RequiredExperience(),
           5, gold);
    r.Text(332, 89, "공격 " + std::to_string(stats.Attack()), ink, 17);
    r.Text(332, 116, "방어 " + std::to_string(stats.Defense()), ink, 17);
    r.Text(24, 183,
           completed ? "성장 목표 달성 · 자유롭게 경험치를 모으세요"
                     : "목표: 레벨 " + std::to_string(content.goalLevel) + " · 아이템 " +
                           std::to_string(content.goalPickups) + "개 · 능력치 " +
                           std::to_string(content.goalAllocated) + "포인트 배분",
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
}

void LevelOne::DrawMinimap(Renderer& r)
{
    const Color gold(.78f, .64f, .39f), muted(.60f, .65f, .61f);
    if (r.Width() >= 900)
    {
        float cellSize = 122.f / std::max(map.Width, map.Height);
        float mx = r.Width() - 172.f, my = 24;
        r.Panel(mx - 12, my - 8, 158, 176);
        for (int y = 0; y < map.Height; ++y)
        {
            for (int x = 0; x < map.Width; ++x)
            {
                if (map.Walkable(x, y))
                {
                    r.Rect(mx + x * cellSize, my + y * cellSize, cellSize, cellSize,
                           map.Tile(x, y) == 2   ? Color(.19f, .35f, .36f)
                           : map.Tile(x, y) == 4 ? Color(.49f, .40f, .25f)
                           : map.Tile(x, y) == 1 ? Color(.40f, .41f, .34f)
                                                 : Color(.23f, .29f, .22f));
                }
            }
        }
        for (const auto& enemy : enemies)
        {
            if (enemy.IsVisible() && !enemy.dead)
            {
                r.Rect(mx + enemy.x * cellSize - 1, my + enemy.y * cellSize - 1, 3, 3,
                       Color(.8f, .16f, .16f));
            }
        }
        if (player.IsVisible())
        {
            r.Rect(mx + player.x * cellSize - 2, my + player.y * cellSize - 2, 4, 4, gold);
        }
        const Prop* shrine = FindShrine();
        if (shrine)
        {
            r.Rect(mx + shrine->x * cellSize - 2, my + shrine->y * cellSize - 2, 4, 4,
                   Color(.3f, .85f, .8f));
        }
        r.Text(mx, my + 145, "노랑: 나  빨강: 적", muted, 14);
        r.Text(mx, my + 161, "청록: 휴식 제단", muted, 13);
    }
}

void LevelOne::DrawMenus(Renderer& r)
{
    const Color ink(.88f, .87f, .79f), muted(.60f, .65f, .61f), gold(.78f, .64f, .39f);
    if (statPanel)
    {
        r.Rect(0, 0, static_cast<float>(r.Width()), static_cast<float>(r.Height()),
               Color(0, 0, 0, .75f));
        float width = std::min(700.f, r.Width() - 40.f), x = (r.Width() - width) / 2, y = 160;
        r.Panel(x, y, width, 354);
        r.Text(x + 24, y + 43, "능력치 배분  ·  남은 포인트 " + std::to_string(stats.points), gold,
               23);
        r.Text(x + 24, y + 87,
               "레벨업 " + std::to_string(stats.rules.pointsPerLevel) + "포인트 · 체력 +" +
                   std::to_string(stats.rules.healthPerLevel) + " / 공격 +" +
                   std::to_string(stats.rules.attackPerLevel),
               muted, 18);
        r.Text(x + 24, y + 140,
               "[1] 힘 " + std::to_string(stats.strength) + "   → 공격력 +" +
                   std::to_string(stats.rules.attackPerStrength),
               ink, 22);
        r.Text(x + 24, y + 195,
               "[2] 생명 " + std::to_string(stats.vitality) + "   → 최대 체력 +" +
                   std::to_string(stats.rules.healthPerVitality),
               ink, 22);
        r.Text(x + 24, y + 250,
               "[3] 저항 " + std::to_string(stats.resilience) + "   → 방어력 +" +
                   std::to_string(stats.rules.defensePerResilience),
               ink, 22);
        r.Text(x + 24, y + 316, "[Tab / ESC] 돌아가기 · 능력치 창에서는 전투가 멈춥니다.", muted,
               17);
    }
    if (paused || stats.health <= 0)
    {
        r.Rect(0, 0, static_cast<float>(r.Width()), static_cast<float>(r.Height()),
               Color(0, 0, 0, .78f));
        r.Panel(50, 160, std::min(740.f, r.Width() - 100.f), 252);
        r.Text(76, 210, stats.health <= 0 ? "육체가 무너졌습니다" : content.title, gold, 25);
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
        attackTime = stats.rules.attackDuration;
        attackCooldown = stats.rules.attackCooldown;
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
        float speed = attackTime > 0 ? stats.rules.attackMoveSpeed : stats.rules.moveSpeed;
        Move(player.x, player.y, aimX * speed * dt, aimY * speed * dt);
    }
    float moved = std::hypot(player.x - oldX, player.y - oldY);
    movement += ((moved > .001f ? 1.f : 0.f) - movement) * (1 - std::exp(-dt * 12));
    phase += moved * 4;
    if (attackTime > 0)
    {
        attackTime = std::max(0.f, attackTime - dt);
        if (!attackResolved && attackTime <= stats.rules.attackDuration - stats.rules.attackHitTime)
        {
            attackResolved = true;
            Attack();
        }
    }
}

void LevelOne::BuildScene()
{
    auto& world = scene.CreateGroup(scene.Root(), "World");
    auto& ground = scene.CreateGroup(world, "Terrain");
    auto& statics = scene.CreateGroup(world, "Static sectors");
    auto& characters = scene.CreateGroup(world, "Characters");
    lootGroup = &scene.CreateGroup(world, "Loot");
    numberGroup = &scene.CreateGroup(world, "Floating text");
    auto& effects = scene.CreateGroup(world, "Atmosphere");
    auto& ui = scene.CreateGroup(scene.Root(), "UI", true);
    auto& hudGroup = scene.CreateGroup(ui, "HUD");
    minimapGroup = &scene.CreateGroup(ui, "Minimap clip");
    auto& menuGroup = scene.CreateGroup(ui, "Menus");

    atmosphere.SetTickEnabled(false);
    atmosphere.SetRenderPasses(ActorPassBit(ActorPass::Effects));
    scene.Attach(atmosphere, effects);
    terrain.SetTickEnabled(false);
    hud.SetTickEnabled(false);
    minimap.SetTickEnabled(false);
    menus.SetTickEnabled(false);
    terrain.SetRenderPasses(ActorPassBit(ActorPass::Ground));
    player.SetRenderPasses(ActorPassBit(ActorPass::Shadow) | ActorPassBit(ActorPass::World));
    player.SetRenderBounds({-160, -180, 160, 100});
    navigation.SetRenderPasses(0);
    hud.SetRenderPasses(ActorPassBit(ActorPass::UI));
    minimap.SetRenderPasses(ActorPassBit(ActorPass::UI));
    minimap.SetRenderBounds({0, 0, 158, 176});
    menus.SetRenderPasses(ActorPassBit(ActorPass::UI));
    scene.Attach(terrain, ground);
    // Preserve combat ordering: player movement/attack, navigation, then enemy AI.
    scene.Attach(player, characters);
    scene.Attach(navigation, characters);
    for (auto& enemy : enemies)
    {
        scene.Attach(enemy, characters);
    }
    for (auto& drop : drops)
    {
        scene.Attach(drop, *lootGroup);
    }
    scene.Attach(hud, hudGroup);
    scene.Attach(minimap, *minimapGroup);
    scene.Attach(menus, menuGroup);

    std::unordered_map<int, Scene::Node*> sectors;
    for (auto& prop : props)
    {
        int key = static_cast<int>(prop.x / 8) + static_cast<int>(prop.y / 8) * 128;
        auto& sector = sectors[key];
        if (!sector)
        {
            sector = &scene.CreateGroup(statics, "Sector " + std::to_string(key));
        }
        prop.SetTickEnabled(false);
        scene.Attach(prop, *sector);
    }
}

void LevelOne::SpawnDrop(float x, float y, bool potion)
{
    drops.emplace_back(this, x, y, potion, false);
    drops.back().SetTickEnabled(false);
    if (lootGroup)
    {
        scene.Attach(drops.back(), *lootGroup);
    }
}

void LevelOne::SpawnNumber(float x, float y, float remaining, const std::string& text, bool reward)
{
    numbers.emplace_back(this, x, y, remaining, text, reward);
    scene.Attach(numbers.back(), *numberGroup);
}
