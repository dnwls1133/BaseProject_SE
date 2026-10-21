#pragma once
#include "ModelCache.h"
#include <string>
#include <vector>

struct RpgRules
{
    int maximumLevel = 20, experienceBase = 60, experienceStep = 35, pointsPerLevel = 2;
    int healthBase = 100, healthPerLevel = 5, healthPerVitality = 15;
    int attackBase = 18, attackPerLevel = 2, attackPerStrength = 4;
    int defenseBase = 2, defensePerResilience = 2, healing = 40;
    float moveSpeed = 3.7f, attackMoveSpeed = 1.5f, attackRange = 2.15f;
    float attackDuration = .36f, attackCooldown = .55f, attackHitTime = .1f, invulnerability = .65f;
    int minimumPotions = 3, exchangeCost = 3, dropPeriod = 3;
};

struct EnemyDefinition
{
    std::string id, name, modelName;
    ModelId model = ModelId::Infected;
    int health = 48, attack = 13, defense = 1, experience = 28;
    float speed = 1.65f, windup = .7f, cooldown = 1.25f, range = 1.45f;
    float gait = 4.5f, shadow = 76, barHeight = 108;
    int aggro = 11;
    std::vector<std::string> rivals;
};

struct PropDefinition
{
    std::string id, modelName, role;
    ModelId model = ModelId::Pillar;
    bool ground = false, blocking = false, tall = false;
    float shadow = 100;
    int footprint = 0;
};

struct RegionDefinition
{
    std::string id, name;
    int style = 0, x = 0, y = 0, radiusX = 1, radiusY = 1;
    Color floor = Color(.24f, .28f, .23f);
    Color water = Color(.105f, .24f, .25f);
    Color fog = Color(.36f, .45f, .43f, .17f);
    std::vector<std::string> population;
    float villageRadius = 0;
    int residents = 0;
};

struct TerrainPatch
{
    int tile = 3, x = 0, y = 0, rx = 1, ry = 1;
};

struct WorldRoad
{
    int ax = 0, ay = 0, bx = 0, by = 0, radius = 1;
};

struct PropPlacement
{
    std::string id;
    float x = 0, y = 0, scale = 1, facing = 1;
};

struct PropScatter
{
    std::string id;
    int x = 0, y = 0, rx = 1, ry = 1, count = 1;
    float spacing = 2;
};

// Content changes replace files, not gameplay classes. IDs are resolved before scene mutation.
struct LevelData
{
    int width = 80, height = 64, startX = 12, startY = 15;
    int goalLevel = 3, goalPickups = 1, goalAllocated = 1;
    std::string title, introduction, sourceFile, simulationFile, societyFile;
    std::uint32_t contentHash = 2166136261u;
    RpgRules rules;
    int wildernessCount = 0;
    float respawnSeconds = 4, respawnRelocation = 12, spawnPlayerClearance = 18;
    std::vector<std::string> wildernessSpecies;
    std::vector<RegionDefinition> regions;
    std::vector<EnemyDefinition> enemies;
    std::vector<PropDefinition> props;
    std::vector<TerrainPatch> patches;
    std::vector<WorldRoad> roads;
    std::vector<PropPlacement> placements;
    std::vector<PropScatter> scatter;

    void Load(const std::string& root, const std::string& relative, const ModelCache& models);
    const EnemyDefinition& Enemy(const std::string& id) const;
    const PropDefinition& Prop(const std::string& id) const;
};
