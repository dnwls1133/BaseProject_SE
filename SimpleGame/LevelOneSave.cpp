#include "stdafx.h"
#include "LevelOne.h"
#include "ContentData.h"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <iostream>

namespace
{
std::uint32_t SaveHash(const std::string& text)
{
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : text)
    {
        hash = (hash ^ c) * 16777619u;
    }
    return hash;
}
} // namespace

std::string LevelOne::SavePath() const
{
    return (std::filesystem::path(contentRoot).parent_path() / "Saves" / "world.save").string();
}

bool LevelOne::SaveWorld(bool manual)
{
    PROFILE_SCOPE(WorldSave);
    if (!started || content.societyFile.empty())
    {
        return false;
    }
    try
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<double>::max_digits10);
        out << "version 1\nsave_schema 9\nsave_header " << std::quoted(worldFile) << ' '
            << society.Signature() << ' ' << map.seed << ' ' << enemies.size() << '\n';
        out << "player_stats " << stats.level << ' ' << stats.experience << ' '
            << stats.totalExperience << ' ' << stats.points << ' ' << stats.strength << ' '
            << stats.vitality << ' ' << stats.resilience << ' ' << stats.allocated << ' '
            << stats.health << ' ' << stats.potions << ' ' << stats.fragments << ' ' << kills << ' '
            << pickups << ' ' << expedition << '\n';
        out << "player_motion " << player.x << ' ' << player.y << ' ' << cx << ' ' << cy << ' '
            << time << ' ' << phase << ' ' << movement << ' ' << facing << ' ' << aimX << ' '
            << aimY << ' ' << attackTime << ' ' << attackCooldown << ' ' << hurtTime << ' '
            << attackAimX << ' ' << attackAimY << ' ' << attackResolved << '\n';
        out << "population_random " << populationRandomState << '\n';
        simulation.Write(out);
        society.Write(out);
        for (const auto& e : enemies)
        {
            out << "enemy_state " << std::quoted(e.type.id) << ' ' << e.x << ' ' << e.y << ' '
                << e.health << ' ' << e.dead << ' ' << e.cooldown << ' ' << e.windup << ' '
                << e.flash << ' ' << e.phase << ' ' << e.attackX << ' ' << e.attackY << ' '
                << e.movement << ' ' << e.facing << ' ' << e.strikeTime << ' ' << e.collapse << ' '
                << e.targetKind << ' ' << e.targetIndex << ' ' << e.decisionTime << ' ' << e.homeX
                << ' ' << e.homeY << ' ' << e.route.x << ' ' << e.route.y << ' ' << e.yaw << ' '
                << e.wilderness << ' ' << e.respawnRemaining << '\n';
        }
        for (const auto& d : drops)
        {
            if (!d.collected && !d.IsDestroyed())
            {
                out << "drop_state " << d.x << ' ' << d.y << ' ' << d.potion << '\n';
            }
        }
        std::string bytes = out.str();
        bytes += "save_checksum " + std::to_string(SaveHash(bytes)) + "\n";
        ContentData::Require(bytes.size() <= 1024 * 1024, "save exceeds 1 MiB budget");
        const std::filesystem::path path(SavePath()), temporary(path.string() + ".tmp");
        std::filesystem::create_directories(path.parent_path());
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            ContentData::Require(static_cast<bool>(file), "cannot open temporary save");
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            file.flush();
            ContentData::Require(static_cast<bool>(file), "cannot flush temporary save");
            file.close();
            ContentData::Require(!file.fail(), "cannot close temporary save");
        }
        if (autosaveAllowed && std::filesystem::exists(path))
        {
            const std::filesystem::path backup(path.string() + ".bak");
            ContentData::Require(CopyFileW(path.c_str(), backup.c_str(), FALSE) != 0,
                                 "cannot back up previous save");
        }
        ContentData::Require(MoveFileExW(temporary.c_str(), path.c_str(),
                                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                             "cannot commit save");
        autosaveAllowed = true;
        Profile::Event("world_saved",
                       "path=" + path.string() + " bytes=" + std::to_string(bytes.size()));
        if (manual)
        {
            Notify("세계를 저장했습니다. NPC·관계·사건·요청과 플레이어 진행을 함께 보존합니다.");
        }
        return true;
    }
    catch (const std::exception& e)
    {
        autosaveAllowed = false;
        Profile::Event("save_error", e.what());
        std::cerr << "World save error: " << e.what() << std::endl;
        Notify("저장 실패 · 기존 저장을 유지합니다. 콘솔을 확인하세요.");
        return false;
    }
}

bool LevelOne::RestoreWorld(bool automatic)
{
    PROFILE_SCOPE(WorldRestore);
    if (content.societyFile.empty())
    {
        return false;
    }
    const std::string path = SavePath();
    try
    {
        if (!std::filesystem::exists(path))
        {
            if (!automatic)
            {
                Notify("저장 파일이 없습니다. K로 현재 세계를 저장하세요.");
            }
            return false;
        }
        const std::string bytes = ContentData::Read(path);
        const auto footer = bytes.rfind("save_checksum ");
        ContentData::Require(footer != std::string::npos && footer > 0 && bytes[footer - 1] == '\n',
                             "missing save checksum");
        const auto expected = SaveHash(bytes.substr(0, footer));
        WorldSimulation restoredSimulation = simulation;
        restoredSimulation.BeginRestore();
        WorldSociety restoredSociety;
        restoredSociety.Load(contentRoot, content.societyFile, content, modelCache, 1,
                             simulation.Minute());
        restoredSociety.BeginRestore();
        RpgStats restoredStats;
        restoredStats.rules = content.rules;
        std::vector<Enemy> restoredEnemies;
        std::list<Drop> restoredDrops;
        std::array<float, 15> motion{};
        bool resolved = false;
        int restoredKills = 0, restoredPickups = 0, restoredExpedition = 0, expectedEnemies = 0;
        std::uint32_t seed = 0, restoredPopulationRandom = 1;
        std::set<std::string> records;
        bool ended = false;
        ContentData::Rows(
            path, bytes,
            [&](const std::string& command, ContentData::Row& row)
            {
                using ContentData::Require;
                Require(!ended, "data after save checksum");
                if (command == "save_schema")
                {
                    Require(records.insert(command).second, "duplicate save schema");
                    row.Integer(9, 9);
                }
                else if (command == "save_header")
                {
                    Require(records.insert(command).second, "duplicate save header");
                    Require(row.Word() == worldFile, "saved world does not match current world");
                    Require(row.Unsigned(4294967295ULL) == restoredSociety.Signature(),
                            "saved content signature mismatch");
                    seed = static_cast<std::uint32_t>(row.Unsigned(4294967295ULL));
                    Require(seed != 0, "invalid map seed");
                    expectedEnemies = row.Integer(0, 1024);
                }
                else if (command == "population_random")
                {
                    Require(records.insert(command).second, "duplicate population random state");
                    restoredPopulationRandom = std::uint32_t(row.Unsigned(4294967295ULL));
                }
                else if (command == "player_stats")
                {
                    Require(records.insert(command).second, "duplicate player stats");
                    auto& s = restoredStats;
                    s.level = row.Integer(1, s.rules.maximumLevel);
                    s.experience = row.Integer(0, 10000000);
                    s.totalExperience = row.Integer(0, 100000000);
                    s.points = row.Integer(0, 10000);
                    s.strength = row.Integer(0, 10000);
                    s.vitality = row.Integer(0, 10000);
                    s.resilience = row.Integer(0, 10000);
                    s.allocated = row.Integer(0, 30000);
                    s.health = row.Integer(0, s.MaximumHealth());
                    s.potions = row.Integer(0, 1000000);
                    s.fragments = row.Integer(0, 1000000);
                    Require(s.allocated == s.strength + s.vitality + s.resilience,
                            "invalid allocated stats");
                    restoredKills = row.Integer(0, 1000000);
                    restoredPickups = row.Integer(0, 1000000);
                    restoredExpedition = row.Integer(0, 1000000);
                }
                else if (command == "player_motion")
                {
                    Require(records.insert(command).second, "duplicate player motion");
                    for (auto& value : motion)
                    {
                        value = row.Number(-100000000, 100000000);
                    }
                    resolved = row.Integer(0, 1) != 0;
                    Require(motion[4] >= 0 && motion[6] >= 0 && motion[6] <= 1 && motion[10] >= 0 &&
                                motion[10] <= content.rules.attackDuration && motion[11] >= 0 &&
                                motion[12] >= 0,
                            "invalid player animation timers");
                }
                else if (command == "enemy_state")
                {
                    Require(restoredEnemies.size() < 1024, "too many saved enemies");
                    Enemy enemy(this);
                    enemy.type = content.Enemy(row.Word());
                    enemy.maximumHealth = enemy.type.health;
                    enemy.x = row.Number(0, static_cast<float>(map.Width));
                    enemy.y = row.Number(0, static_cast<float>(map.Height));
                    enemy.health = row.Integer(0, enemy.maximumHealth);
                    enemy.dead = row.Integer(0, 1) != 0;
                    Require(enemy.dead == (enemy.health == 0),
                            "inconsistent saved enemy life state");
                    enemy.cooldown = row.Number(0, 20);
                    enemy.windup = row.Number(0, 5);
                    enemy.flash = row.Number(0, 1);
                    enemy.phase = row.Number(0, 1e9f);
                    enemy.attackX = row.Number(0, 256);
                    enemy.attackY = row.Number(0, 256);
                    enemy.movement = row.Number(0, 1);
                    enemy.facing = row.Number(-1, 1);
                    enemy.strikeTime = row.Number(0, 1);
                    enemy.collapse = row.Number(0, 1);
                    enemy.targetKind = row.Integer(0, 3);
                    enemy.targetIndex = row.Integer(-1, 1023);
                    enemy.decisionTime = row.Number(-1, 1);
                    enemy.homeX = row.Number(0, static_cast<float>(map.Width));
                    enemy.homeY = row.Number(0, static_cast<float>(map.Height));
                    enemy.route.x = row.Number(0, static_cast<float>(map.Width));
                    enemy.route.y = row.Number(0, static_cast<float>(map.Height));
                    enemy.yaw = row.Number(-3.142f, 3.142f);
                    enemy.wilderness = row.Integer(0, 1) != 0;
                    enemy.respawnRemaining = row.Number(0, content.respawnSeconds);
                    Require(!enemy.wilderness || content.wildernessCount > 0,
                            "unexpected saved wilderness unit");
                    Require(enemy.dead || enemy.respawnRemaining == 0,
                            "living enemy has a respawn timer");
                    if (enemy.dead && enemy.collapse >= 1)
                    {
                        enemy.SetRenderPasses(ActorPassBit(ActorPass::Ground));
                        enemy.SetTickEnabled(false);
                    }
                    restoredEnemies.push_back(enemy);
                }
                else if (command == "drop_state")
                {
                    Require(restoredDrops.size() < 10000, "too many saved drops");
                    const float x = row.Number(0, 256), y = row.Number(0, 256);
                    const bool potion = row.Integer(0, 1) != 0;
                    restoredDrops.emplace_back(this, x, y, potion, false);
                    restoredDrops.back().SetTickEnabled(false);
                }
                else if (command == "save_checksum")
                {
                    Require(row.Unsigned(4294967295ULL) == expected, "save checksum mismatch");
                    ended = true;
                }
                else if (!restoredSimulation.ReadRow(command, row) &&
                         !restoredSociety.ReadRow(command, row))
                {
                    Require(false, "unknown save record: " + command);
                }
            });
        ContentData::Require(ended && records.size() == 4 &&
                                 restoredEnemies.size() ==
                                     static_cast<std::size_t>(expectedEnemies),
                             "incomplete world save");
        for (const auto& e : restoredEnemies)
        {
            ContentData::Require(
                e.targetKind < 2 ||
                    (e.targetIndex >= 0 &&
                     (e.targetKind == 2
                          ? e.targetIndex < static_cast<int>(restoredSociety.Npcs().size())
                          : e.targetIndex < static_cast<int>(restoredEnemies.size()))),
                "invalid creature target");
        }
        for (const auto& n : restoredSociety.Npcs())
        {
            ContentData::Require(n.wildTarget < static_cast<int>(restoredEnemies.size()),
                                 "invalid wildlife target");
        }
        ContentData::Require(records.count("population_random") != 0,
                             "missing population random state");
        ContentData::Require(std::count_if(restoredEnemies.begin(), restoredEnemies.end(),
                                           [](const Enemy& enemy)
                                           {
                                               return enemy.wilderness;
                                           }) == content.wildernessCount,
                             "saved wilderness population does not match content");
        restoredSimulation.FinishRestore();
        ContentData::Require(restoredSimulation.Minute() == restoredSociety.Minute(),
                             "world clock mismatch");
        RandomLevelMap restoredMap;
        restoredMap.Generate(seed, content);
        ContentData::Require(restoredMap.CanStand(motion[0], motion[1]),
                             "invalid saved player position");
        ContentData::Require(motion[2] >= 0 && motion[2] < map.Width && motion[3] >= 0 &&
                                 motion[3] < map.Height,
                             "invalid saved camera position");
        for (const auto& e : restoredEnemies)
        {
            ContentData::Require(restoredMap.CanStand(e.x, e.y), "invalid saved enemy position");
        }
        for (const auto& d : restoredDrops)
        {
            ContentData::Require(restoredMap.CanStand(d.x, d.y), "invalid saved drop position");
        }
        restoredSociety.FinishRestore(restoredMap);
        restoredSociety.ReconcilePositions(restoredMap);
        // All saved records are validated before replacing the live scene.
        autosaveAllowed = false;
        if (!Generate(seed))
        {
            return false;
        }
        scene.Clear();
        lootGroup = numberGroup = minimapGroup = nullptr;
        simulation = std::move(restoredSimulation);
        society = std::move(restoredSociety);
        stats = restoredStats;
        enemies = std::move(restoredEnemies);
        populationRandomState = restoredPopulationRandom;
        drops = std::move(restoredDrops);
        numbers.clear();
        kills = restoredKills;
        pickups = restoredPickups;
        expedition = restoredExpedition;
        player.SetPosition(motion[0], motion[1]);
        cx = motion[2];
        cy = motion[3];
        time = motion[4];
        phase = motion[5];
        movement = motion[6];
        facing = motion[7];
        aimX = motion[8];
        aimY = motion[9];
        attackTime = motion[10];
        attackCooldown = motion[11];
        hurtTime = motion[12];
        attackAimX = motion[13];
        attackAimY = motion[14];
        attackResolved = resolved;
        completed = stats.level >= content.goalLevel && stats.allocated >= content.goalAllocated &&
                    pickups >= content.goalPickups;
        CreateCitizens();
        BuildScene();
        ClearInput();
        distances = map.Distances(static_cast<int>(player.x), static_cast<int>(player.y));
        navigationTime = 0;
        autosaveTime = 0;
        autosaveAllowed = true;
        Profile::Event("world_restored", "path=" + path + " map_seed=" + std::to_string(seed));
        Notify("저장된 세계를 이어갑니다. 인물·세력·관계·요청·플레이어 상태를 복원했습니다.");
        return true;
    }
    catch (const std::exception& e)
    {
        autosaveAllowed = false;
        Profile::Event("restore_error", e.what());
        std::cerr << "World restore error: " << e.what() << std::endl;
        Notify("불러오기 실패 · 현재 세계를 유지하고 자동 저장을 중단합니다. 콘솔 확인 후 K로 새 "
               "저장할 수 있습니다.");
        return false;
    }
}
