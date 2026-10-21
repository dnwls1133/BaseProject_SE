#include "stdafx.h"
#include "Profiler.h"
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
        meshCache3D.Load(dataDirectory);
        contentRoot = dataDirectory;
        worldFile = entry;
        if (!Generate())
        {
            return false;
        }
        started = true;
        Notify(content.introduction);
        RestoreWorld(true);
        return true;
    }
    catch (const std::exception& error)
    {
        contentError = error.what();
        Profile::Event("content_error", contentError);
        std::cerr << "Content load error: " << contentError << std::endl;
        return false;
    }
}

bool LevelOne::Generate(std::uint32_t fixedSeed)
{
    PROFILE_SCOPE(WorldGenerate);
    // Read and resolve a complete candidate before invalidating any scene references.
    LevelData nextContent;
    RandomLevelMap nextMap;
    WorldSimulation nextSimulation = simulation;
    WorldSociety nextSociety = society;
    auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto seed =
        static_cast<std::uint32_t>(ticks) ^ static_cast<std::uint32_t>((expedition + 1) * 7919);
    if (fixedSeed != 0)
    {
        seed = fixedSeed;
    }
    if (seed == 0)
    {
        seed = 1;
    }
    Profile::Event("world_generate", worldFile + " seed=" + std::to_string(seed));
    try
    {
        nextContent.Load(contentRoot, worldFile, modelCache);
        ContentData::Require(stats.level <= nextContent.rules.maximumLevel,
                             "growth limit below current level");
        nextSimulation.Load(contentRoot, nextContent.simulationFile, nextContent.regions,
                            nextContent.sourceFile);
        nextMap.Generate(seed, nextContent);
        ContentData::Require(nextContent.societyFile.empty() || !nextContent.simulationFile.empty(),
                             "society requires a world simulation file");
        nextSociety.Load(contentRoot, nextContent.societyFile, nextContent, modelCache,
                         seed ^ 0x8734u, nextSimulation.Minute());
        nextSociety.ReconcilePositions(nextMap);
        for (const auto& place : nextSociety.LifePlaces())
        {
            ContentData::Require(meshCache3D.Find(place.model) >= 0,
                                 "unknown activity model: " + place.model);
        }
    }
    catch (const std::exception& error)
    {
        contentError = error.what();
        std::cerr << "Content load error: " << contentError << std::endl;
        Notify("외부 데이터 로딩 실패 · 기존 월드를 유지합니다. 콘솔의 파일명과 행을 확인하세요.");
        Profile::Event("content_error", contentError);
        return false;
    }
    contentError.clear();
    scene.Clear();
    lootGroup = numberGroup = minimapGroup = nullptr;
    content = std::move(nextContent);
    map = std::move(nextMap);
    terrain3D.Build(map);
    simulation = std::move(nextSimulation);
    society = std::move(nextSociety);
    society.ResetWildlifeTargets();
    BuildMinimapBackground();
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
            for (const auto& place : society.LifePlaces())
            {
                if (std::hypot(x - place.position.x, y - place.position.y) < 9)
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
            enemy.type = content.Enemy(district.population[count]);
            enemy.SetPosition(x, y);
            enemy.homeX = x;
            enemy.homeY = y;
            enemy.route = {x, y};
            enemy.decisionTime = static_cast<float>(enemies.size() % 10) * .05f;
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
        prop.placedParts =
            meshCache3D.Place(int(prop.model), {prop.x, Terrain3D::Height(prop.x, prop.y), prop.y},
                              prop.facing < 0 ? 3.14159f : 0, prop.scale);
        Vec3 minimum, maximum;
        meshCache3D.Bounds(static_cast<int>(prop.model), minimum, maximum);
        const float extentX = std::max(std::abs(minimum.x), std::abs(maximum.x)) * prop.scale;
        const float extentZ = std::max(std::abs(minimum.z), std::abs(maximum.z)) * prop.scale;
        prop.bounds3DMin = {-extentX, minimum.y * prop.scale - 1, -extentZ};
        prop.bounds3DMax = {extentX, maximum.y * prop.scale + 1, extentZ};
        if (type.ground)
        {
            prop.SetRenderPasses(ActorPassBit(ActorPass::Ground));
        }
    }
    distances = map.Distances(static_cast<int>(player.x), static_cast<int>(player.y));
    CreateCitizens();
    CloseConversation();
    focusedNpc = -1;
    journalOpen = false;
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

    if (SocietyKey(key))
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

void LevelOne::Kill(Enemy& enemy, bool playerCredit)
{
    if (!enemy.IsActive() || enemy.dead)
    {
        return;
    }
    enemy.dead = true;
    enemy.health = 0;
    enemy.windup = 0;
    society.MonsterKilled(map.DistrictAt(static_cast<int>(enemy.x), static_cast<int>(enemy.y)).id,
                          playerCredit);
    if (!playerCredit)
    {
        return;
    }
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
    // Hive units are ordinary hostiles; X is still required to attack inhabitants.
    if (keys['x'] || society.TwoFactionWar())
    {
        for (std::size_t i = 0; i < society.Npcs().size(); ++i)
        {
            const auto& npc = society.Npcs()[i];
            if (!keys['x'] && !society.IsHive(int(i)))
            {
                continue;
            }
            const float dx = npc.x - player.x, dy = npc.y - player.y;
            const float distance = std::hypot(dx, dy);
            const float alignment =
                distance > .01f ? (dx * attackAimX + dy * attackAimY) / distance : 1;
            if (npc.Alive() && distance <= stats.rules.attackRange && alignment >= .15f &&
                map.Visible(player.x, player.y, npc.x, npc.y))
            {
                society.PlayerAttack(static_cast<int>(i), stats.Attack(), simulation);
            }
        }
    }
    PROFILE_SCOPE(Combat);
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

void LevelOne::Update(float dt)
{
    if (paused || statPanel || stats.health <= 0)
    {
        PublishMonitoring(dt);
        return;
    }
    time += dt;
    noticeTime -= dt;
    simulation.Update(dt);
    society.Update(dt, simulation, map, {player.x, player.y});
    BuildEcologyIndex();
    scene.Update(dt);
    focusedNpc = -1;
    float nearestNpcDistance = 3.5f;
    for (std::size_t i = 0; i < society.Npcs().size(); ++i)
    {
        const auto& npc = society.Npcs()[i];
        const float distance = std::hypot(npc.x - player.x, npc.y - player.y);
        if (npc.Alive() && distance < nearestNpcDistance &&
            map.Visible(player.x, player.y, npc.x, npc.y))
        {
            focusedNpc = int(i);
            nearestNpcDistance = distance;
        }
    }
    if (talkingNpc >= 0 && (!society.IsConversationActive(talkingNpc) || stats.health <= 0))
    {
        CloseConversation();
        Notify("대화가 끝났습니다. 상대는 자신의 행동을 이어갑니다.");
    }
    PublishMonitoring(dt);
    const int socialDamage = society.TakePlayerDamage();
    if (socialDamage > 0 && hurtTime <= 0)
    {
        const int damage = std::max(1, socialDamage - stats.Defense());
        stats.health = std::max(0, stats.health - damage);
        hurtTime = stats.rules.invulnerability;
        SpawnNumber(player.x, player.y, .8f, "-" + std::to_string(damage), false);
    }
    autosaveTime += dt;
    if (autosaveAllowed && autosaveTime >= 60)
    {
        SaveWorld(false);
        autosaveTime = 0;
    }
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
    const Vec3 p = r.Project3D({x, Terrain3D::Height(x, y) + z / 42.f, y});
    return {p.x, p.y};
}

void LevelOne::Render(Renderer& r)
{
    Profile::Set(Profile::Metric::MapWidth, map.Width);
    Profile::Set(Profile::Metric::MapHeight, map.Height);
    Profile::Set(Profile::Metric::EnemyCount, double(enemies.size()));
    Profile::Set(Profile::Metric::PropCount, double(props.size()));
    Profile::Set(Profile::Metric::DropCount, double(drops.size()));
    Profile::Set(Profile::Metric::NumberCount, double(numbers.size()));
    Profile::Set(Profile::Metric::Paused, paused || statPanel || stats.health <= 0 ? 1 : 0);
    simulation.RecordMetrics(
        map.DistrictAt(static_cast<int>(player.x), static_cast<int>(player.y)).id);
    minimap.SetVisible(r.Width() >= 900);
    minimap.SetPosition(r.Width() - 184.f, 16);
    minimapGroup->SetClipRect({minimap.x, minimap.y, minimap.x + 158, minimap.y + 176});
    camera3D.Update({cx, Terrain3D::Height(cx, cy) + .6f, cy}, r.Width(), r.Height());
    r.UseWorld3D(camera3D);
    scene.Render(r, time, {(cx - cy) * 32 - r.Width() * .5f, (cx + cy) * 16 - r.Height() * .5f});
}

void LevelOne::DrawGround(Renderer& r)
{
    terrain3D.Draw(r);
    const auto& cells = society.ContaminationCells();
    const int width = society.ContaminationWidth();
    const float size = society.ContaminationCellSize();
    if (!width)
    {
        return;
    }
    // The terrain CallbackActor owns this pass. Only visible vicinity is submitted;
    // simulation continues over the whole map, independent of this rendering bound.
    const int height = int(cells.size()) / width;
    const int x0 = std::max(0, int((cx - 40) / size));
    const int x1 = std::min(width - 1, int((cx + 40) / size));
    const int y0 = std::max(0, int((cy - 40) / size));
    const int y1 = std::min(height - 1, int((cy + 40) / size));
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const auto& cell = cells[y * width + x];
            if (cell.density < .015f && cell.scar <= 0)
            {
                continue;
            }
            for (int part = 0; part < 4; ++part)
            {
                const float px = (x + .25f + (part % 2) * .5f) * size;
                const float py = (y + .25f + (part / 2) * .5f) * size;
                if (!map.CanStand(px, py))
                {
                    continue;
                }
                const float coverage =
                    cell.scar > 0 ? .95f : std::min(1.f, .15f + cell.density * 1.5f);
                const float radius = size * .34f * coverage;
                r.Submit3D(1,
                           Mat4::Translation({px, Terrain3D::Height(px, py) + .025f, py}) *
                               Mat4::Scale({radius, .055f, radius}),
                           cell.scar > 0 ? Color(.07f, .065f, .06f) : Color(.29f, .055f, .13f),
                           cell.scar > 0 ? Material::Earth : Material::Organic);
                if (cell.fire > 0)
                {
                    r.Submit3D(1,
                               Mat4::Translation({px, Terrain3D::Height(px, py) + .4f, py}) *
                                   Mat4::Scale({.22f, .65f, .22f}),
                               Color(.9f, .23f, .025f), Material::Flame);
                }
            }
        }
    }
}

void LevelOne::DrawAtmosphere(Renderer& r)
{
    for (int i = 0; i < 56; ++i)
    {
        float x = cx + std::sin(i * 2.399f) * float(i % 17 + 2),
              z = cy + std::cos(i * 2.399f) * float(i % 17 + 2);
        float height = 1.3f + std::sin(time * .35f + i) * .9f;
        r.Submit3D(1,
                   Mat4::Translation({x, Terrain3D::Height(x, z) + height, z}) *
                       Mat4::Scale({.035f, .035f, .035f}),
                   Color(.45f, .28f, .22f), Material::SoftLight);
    }
}

void LevelOne::DrawPlayer(Renderer& r, ActorPass pass)
{
    if (pass != ActorPass::World)
    {
        return;
    }
    Pose3D pose;
    pose.phase = phase;
    pose.movement = movement;
    pose.time = time;
    pose.flash = hurtTime * .4f;
    if (attackTime > 0)
    {
        const float elapsed = stats.rules.attackDuration - attackTime;
        pose.attack = elapsed < stats.rules.attackHitTime
                          ? .01f + .44f * elapsed / std::max(.001f, stats.rules.attackHitTime)
                          : .45f + .55f * (elapsed - stats.rules.attackHitTime) /
                                       std::max(.001f, stats.rules.attackDuration -
                                                           stats.rules.attackHitTime);
    }
    meshCache3D.Draw(
        r, 0, {player.x, Terrain3D::Height(player.x, player.y), player.y},
        attackTime > 0 ? std::atan2(-attackAimX, -attackAimY) : std::atan2(-aimX, -aimY), 1, pose);
    if (attackTime > 0)
    {
        DrawAttackEffect(r, {player.x, Terrain3D::Height(player.x, player.y), player.y},
                         std::atan2(-attackAimX, -attackAimY), pose.attack, stats.rules.attackRange,
                         Color(.9f, .64f, .35f));
    }
}

void LevelOne::DrawAttackEffect(Renderer& r, Vec3 position, float yaw, float progress, float reach,
                                Color color)
{
    // A short swept ribbon shares the instance batch; it is not a separate draw call.
    if (progress < .38f || progress > .83f)
    {
        return;
    }
    const float sweep = std::min(1.f, (progress - .38f) / .32f);
    for (int i = 0; i < 9; ++i)
    {
        const float t = sweep - i * .045f;
        if (t < 0)
        {
            continue;
        }
        const float angle = yaw + 3.14159f - 1.05f + t * 2.1f;
        const Vec3 point{position.x + std::sin(angle) * reach * .72f,
                         position.y + 1.05f + std::sin(t * 3.14159f) * .23f,
                         position.z + std::cos(angle) * reach * .72f};
        const float fade = (1 - i / 10.f) * std::min(1.f, (.83f - progress) * 8);
        r.Submit3D(0,
                   Mat4::Translation(point) * Mat4::Rotation({0, 1, 0}, angle) *
                       Mat4::Scale({.15f * fade, .045f, .38f}),
                   Color(color.r * fade, color.g * fade, color.b * fade), Material::SoftLight);
    }
}

void LevelOne::DrawEnemy(Renderer& r, const Enemy& enemy, ActorPass pass)
{
    if (pass == ActorPass::Shadow)
    {
        return;
    }
    if (pass == ActorPass::World || pass == ActorPass::Ground)
    {
        Pose3D pose;
        pose.phase = enemy.phase;
        pose.movement = enemy.dead ? 0 : enemy.movement;
        pose.time = time;
        pose.flash = enemy.flash * 4;
        pose.collapse = enemy.collapse;
        pose.attack = enemy.windup > 0 ? .15f + .35f * (1 - enemy.windup / enemy.WindupDuration())
                      : enemy.strikeTime > 0 ? 1 - .5f * enemy.strikeTime / .24f
                                             : 0;
        const float yaw = enemy.windup > 0
                              ? std::atan2(enemy.x - enemy.attackX, enemy.y - enemy.attackY)
                              : enemy.yaw;
        meshCache3D.Draw(
            r, static_cast<int>(enemy.type.model),
            {enemy.x, Terrain3D::Height(enemy.x, enemy.y) + enemy.collapse * .25f, enemy.y}, yaw,
            enemy.type.model == ModelId::Brute ? 1.25f : 1, pose);
        if (enemy.flash > 0)
        {
            const float elapsed = .18f - std::min(.18f, enemy.flash);
            for (int i = 0; i < 6; ++i)
            {
                const float angle = i * 1.0472f;
                r.Submit3D(4,
                           Mat4::Translation(
                               {enemy.x + std::cos(angle) * elapsed * 3,
                                Terrain3D::Height(enemy.x, enemy.y) + .8f + elapsed * (1 + i % 3),
                                enemy.y + std::sin(angle) * elapsed * 3}) *
                               Mat4::Scale({.045f, .07f, .045f}),
                           Color(.75f, .16f, .12f), Material::SoftLight);
            }
        }
        if (enemy.windup > 0 && !enemy.dead)
        {
            for (int i = 0; i < 40; ++i)
            {
                float a = i * 6.2831853f / 40;
                float x = enemy.attackX + std::cos(a) * enemy.type.range,
                      z = enemy.attackY + std::sin(a) * enemy.type.range;
                r.Submit3D(1,
                           Mat4::Translation({x, Terrain3D::Height(x, z) + .07f, z}) *
                               Mat4::Scale({.10f, .04f, .10f}),
                           Color(.9f, .08f, .035f), Material::SoftLight);
            }
        }
    }
    else if (pass == ActorPass::Overlay && !enemy.dead &&
             std::hypot(enemy.x - player.x, enemy.y - player.y) < 8 &&
             (enemy.health < enemy.maximumHealth || enemy.windup > 0))
    {
        Point p = Project(enemy.x, enemy.y, enemy.type.barHeight, r);
        r.Rect(p.x - 25, p.y, 50, 5, Color(.06f, .07f, .07f));
        r.Rect(p.x - 24, p.y + 1, 48.f * enemy.health / enemy.maximumHealth, 3,
               Color(.72f, .16f, .17f));
        r.Text(p.x - r.TextWidth(enemy.type.name, 13) * .5f, p.y - 5, enemy.type.name,
               Color(.8f, .73f, .62f), 13);
    }
}

void LevelOne::DrawProp(Renderer& r, const Prop& prop, ActorPass pass)
{
    if (pass != ActorPass::World && pass != ActorPass::Ground)
    {
        return;
    }
    Color tint(1, 1, 1);
    Point p = Project(prop.x, prop.y, 0, r), hero = Project(player.x, player.y, 45, r);
    if (prop.type.tall && prop.Depth() > player.Depth() &&
        std::abs(hero.x - p.x) < 60 * prop.scale && hero.y < p.y && hero.y > p.y - 180 * prop.scale)
    {
        tint.a = .35f;
    }
    Pose3D pose;
    pose.time = time;
    if (!prop.placedParts.empty())
    {
        meshCache3D.DrawPlaced(r, prop.placedParts, tint);
    }
    else
    {
        meshCache3D.Draw(r, static_cast<int>(prop.model),
                         {prop.x, Terrain3D::Height(prop.x, prop.y), prop.y},
                         prop.facing < 0 ? 3.14159f : 0, prop.scale, pose, tint);
    }
    if (prop.type.role == "brazier" || prop.type.role == "shrine")
    {
        r.Light3D({prop.x, Terrain3D::Height(prop.x, prop.y) + 1.8f, prop.y},
                  Color(1.5f, .45f, .12f));
    }
    if (prop.type.role == "fungal")
    {
        r.Light3D({prop.x, Terrain3D::Height(prop.x, prop.y) + 1, prop.y}, Color(.7f, .05f, .16f));
    }
}

void LevelOne::DrawDrop(Renderer& r, const Drop& drop)
{
    Pose3D pose;
    pose.time = time;
    meshCache3D.Draw(
        r, static_cast<int>(drop.potion ? ModelId::Potion : ModelId::Fragment),
        {drop.x, Terrain3D::Height(drop.x, drop.y) + .15f + std::sin(time * 2) * .08f, drop.y},
        time * .7f, 1, pose);
}

void LevelOne::DrawNumber(Renderer& r, const FloatingText& number)
{
    Point p = Project(number.x, number.y, 65 + (1.6f - number.remaining) * 15, r);
    r.Text(p.x - 20, p.y, number.text,
           number.reward ? Color(.83f, .72f, .39f) : Color(.95f, .72f, .62f), 17);
}

void LevelOne::DrawUI(Renderer& r)
{
    const Color ink(.93f, .92f, .85f), muted(.75f, .78f, .73f), gold(.89f, .74f, .48f);
    r.Panel(18, 16, 330, 107);
    r.Text(32, 45, map.RegionName(int(player.x), int(player.y)), gold, 21);
    r.Text(32, 73,
           "Lv." + std::to_string(stats.level) + "  체력 " + std::to_string(stats.health) + " / " +
               std::to_string(stats.MaximumHealth()),
           ink, 18);
    r.Rect(32, 84, 298, 10, Color(.13f, .07f, .07f));
    r.Rect(32, 84, 298.f * stats.health / stats.MaximumHealth(), 10, Color(.68f, .16f, .19f));
    const float experience = stats.level >= stats.rules.maximumLevel
                                 ? 1.f
                                 : float(stats.experience) / stats.RequiredExperience();
    r.Rect(32, 104, 298, 4, Color(.12f, .13f, .12f));
    r.Rect(32, 104, 298 * experience, 4, gold);
    if (talkingNpc >= 0 || journalOpen || paused || statPanel || stats.health <= 0)
    {
        return;
    }
    if (noticeTime > 0)
    {
        r.Panel(18, 136, std::min(620.f, r.Width() - 36.f), 92);
        r.WrappedText(32, 164, std::min(590.f, r.Width() - 64.f), notice, ink, 18);
    }
    const auto minute = simulation.Minute();
    r.Text(26, r.Height() - 112.f,
           "세계 " + std::to_string(minute / 1440 + 1) + "일 " + std::to_string(minute / 60 % 24) +
               "시 · J 주민과 세력 기록",
           muted, 17);
    r.Panel(18, r.Height() - 96.f, r.Width() - 36.f, 78);
    r.Text(32, r.Height() - 67.f, "WASD 이동 · Space 공격 · E 획득 · F 대화 · Q 회복 · Tab 능력치",
           ink, 18);
    r.Text(32, r.Height() - 37.f,
           "회복약 " + std::to_string(stats.potions) + " · 파편 " +
               std::to_string(stats.fragments) + " · 능력치 포인트 " +
               std::to_string(stats.points) + " · K 저장 / L 불러오기",
           gold, 17);
}

void LevelOne::BuildMinimapBackground()
{
    Profile::Add(Profile::Metric::MinimapCacheBuild);
    Profile::Add(Profile::Metric::MinimapTiles, double(map.Width) * map.Height);
    // Terrain changes only when the map is replaced. Merge equal-color horizontal runs.
    minimapBackground.clear();
    const float cellSize = 122.f / std::max(map.Width, map.Height);
    for (int y = 0; y < map.Height; ++y)
    {
        int x = 0;
        while (x < map.Width)
        {
            const int tile = map.Tile(x, y);
            if (tile == 0)
            {
                ++x;
                continue;
            }
            const int begin = x++;
            while (x < map.Width && map.Tile(x, y) == tile)
            {
                ++x;
            }
            const Color color = tile == 2   ? Color(.19f, .35f, .36f)
                                : tile == 4 ? Color(.49f, .40f, .25f)
                                : tile == 1 ? Color(.40f, .41f, .34f)
                                            : Color(.23f, .29f, .22f);
            const float left = begin * cellSize, right = x * cellSize;
            const float top = y * cellSize, bottom = (y + 1) * cellSize;
            for (Point point : {Point{left, top}, Point{right, top}, Point{right, bottom},
                                Point{left, top}, Point{right, bottom}, Point{left, bottom}})
            {
                minimapBackground.push_back(
                    {point.x, point.y, color.r, color.g, color.b, color.a, 0, 0, 0});
            }
        }
    }
}

void LevelOne::DrawMinimap(Renderer& r)
{
    PROFILE_SCOPE(Minimap);
    Profile::Set(Profile::Metric::MinimapCacheVertices, double(minimapBackground.size()));
    const Color gold(.78f, .64f, .39f), muted(.60f, .65f, .61f);
    if (r.Width() >= 900)
    {
        float cellSize = 122.f / std::max(map.Width, map.Height);
        float mx = r.Width() - 172.f, my = 24;
        r.Panel(mx - 12, my - 8, 158, 176);
        r.AppendMesh(minimapBackground, {mx, my});
        for (const auto& enemy : enemies)
        {
            if (enemy.IsVisible() && !enemy.dead)
            {
                r.Rect(mx + enemy.x * cellSize - 1, my + enemy.y * cellSize - 1, 3, 3,
                       Color(.8f, .16f, .16f));
            }
        }
        for (const auto& npc : society.Npcs())
        {
            if (npc.Alive())
            {
                const Color color = society.Factions()[npc.faction].color;
                r.Rect(mx + npc.x * cellSize - 1.5f, my + npc.y * cellSize - 1.5f, 3, 3, color);
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
    DrawSocietyUI(r);
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
    PROFILE_SCOPE(Player);
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
    navigation.SetTickEnabled(false);
    hud.SetRenderPasses(ActorPassBit(ActorPass::UI));
    minimap.SetRenderPasses(ActorPassBit(ActorPass::UI));
    minimap.SetRenderBounds({0, 0, 158, 176});
    menus.SetRenderPasses(ActorPassBit(ActorPass::UI));
    scene.Attach(terrain, ground);
    // Player inputs precede one Actor-owned movement/combat update per inhabitant.
    scene.Attach(player, characters);
    scene.Attach(navigation, characters);
    for (auto& place : activityPlaces)
    {
        scene.Attach(place, statics);
    }
    for (auto& citizen : citizens)
    {
        scene.Attach(citizen, characters);
    }
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
