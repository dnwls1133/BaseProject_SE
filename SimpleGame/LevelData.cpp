#include "stdafx.h"
#include "Profiler.h"
#include "LevelData.h"
#include "ContentData.h"
#include <algorithm>
#include <set>
#include <utility>

using ContentData::Require;
using ContentData::Row;

namespace
{
Color ReadColor(Row& row, bool alpha = false)
{
    float r = row.Number(0, 2), g = row.Number(0, 2), b = row.Number(0, 2);
    return Color(r, g, b, alpha ? row.Number(0, 1) : 1);
}
} // namespace

const EnemyDefinition& LevelData::Enemy(const std::string& id) const
{
    auto found = std::find_if(enemies.begin(), enemies.end(),
                              [&](const EnemyDefinition& item)
                              {
                                  return item.id == id;
                              });
    Require(found != enemies.end(), sourceFile + ": unknown enemy: " + id);
    return *found;
}

const PropDefinition& LevelData::Prop(const std::string& id) const
{
    auto found = std::find_if(props.begin(), props.end(),
                              [&](const PropDefinition& item)
                              {
                                  return item.id == id;
                              });
    Require(found != props.end(), sourceFile + ": unknown prop: " + id);
    return *found;
}

void LevelData::Load(const std::string& root, const std::string& relative, const ModelCache& models)
{
    PROFILE_SCOPE(LevelLoad);
    LevelData loaded;
    std::string enemyFile, propFile, ruleFile;
    std::set<std::string> singleton;
    std::string path = ContentData::Path(root, relative);
    loaded.sourceFile = path;
    auto once = [&](const std::string& command)
    {
        Require(singleton.insert(command).second, "duplicate record: " + command);
    };
    ContentData::Load(path,
                      [&](const std::string& command, Row& row)
                      {
                          if (command == "size")
                          {
                              once(command);
                              loaded.width = row.Integer(24, 160);
                              loaded.height = row.Integer(24, 160);
                          }
                          else if (command == "start")
                          {
                              once(command);
                              loaded.startX = row.Integer(2, 157);
                              loaded.startY = row.Integer(2, 157);
                          }
                          else if (command == "title" || command == "introduction" ||
                                   command == "enemies" || command == "props" || command == "rules" ||
                                   command == "simulation")
                          {
                              once(command);
                              std::string value = row.Word();
                              if (command == "title")
                              {
                                  loaded.title = value;
                              }
                              else if (command == "introduction")
                              {
                                  loaded.introduction = value;
                              }
                              else if (command == "simulation")
                              {
                                  loaded.simulationFile = value;
                              }
                              else if (command == "enemies")
                              {
                                  enemyFile = value;
                              }
                              else if (command == "props")
                              {
                                  propFile = value;
                              }
                              else
                              {
                                  ruleFile = value;
                              }
                          }
                          else if (command == "goal")
                          {
                              once(command);
                              loaded.goalLevel = row.Integer(1, 100);
                              loaded.goalPickups = row.Integer(0, 10000);
                              loaded.goalAllocated = row.Integer(0, 100);
                          }
                          else if (command == "region")
                          {
                              RegionDefinition region;
                              region.id = row.Word();
                              Require(singleton.insert("region:" + region.id).second,
                                      "duplicate region");
                              region.style = row.Integer(0, 5);
                              region.name = row.Word();
                              region.x = row.Integer(2, 157);
                              region.y = row.Integer(2, 157);
                              region.radiusX = row.Integer(2, 40);
                              region.radiusY = row.Integer(2, 40);
                              region.floor = ReadColor(row);
                              region.water = ReadColor(row);
                              region.fog = ReadColor(row, true);
                              int count = row.Integer(0, 16);
                              for (int i = 0; i < count; ++i)
                              {
                                  region.population.push_back(row.Word());
                              }
                              loaded.regions.push_back(region);
                              Require(loaded.regions.size() <= 64, "too many regions");
                          }
                          else if (command == "patch")
                          {
                              TerrainPatch patch;
                              patch.tile = row.Integer(0, 4);
                              patch.x = row.Integer(1, 158);
                              patch.y = row.Integer(1, 158);
                              patch.rx = row.Integer(1, 40);
                              patch.ry = row.Integer(1, 40);
                              loaded.patches.push_back(patch);
                          }
                          else if (command == "road")
                          {
                              WorldRoad road;
                              road.ax = row.Integer(2, 157);
                              road.ay = row.Integer(2, 157);
                              road.bx = row.Integer(2, 157);
                              road.by = row.Integer(2, 157);
                              road.radius = row.Integer(1, 4);
                              loaded.roads.push_back(road);
                          }
                          else if (command == "place")
                          {
                              PropPlacement prop;
                              prop.id = row.Word();
                              prop.x = row.Number(1, 158);
                              prop.y = row.Number(1, 158);
                              prop.scale = row.Number(.5f, 1.3f);
                              prop.facing = static_cast<float>(row.Integer(-1, 1));
                              Require(prop.facing != 0, "facing must be -1 or 1");
                              loaded.placements.push_back(prop);
                          }
                          else if (command == "scatter")
                          {
                              PropScatter scatter;
                              scatter.id = row.Word();
                              scatter.x = row.Integer(2, 157);
                              scatter.y = row.Integer(2, 157);
                              scatter.rx = row.Integer(1, 40);
                              scatter.ry = row.Integer(1, 40);
                              scatter.count = row.Integer(1, 100);
                              scatter.spacing = row.Number(1.5f, 8);
                              loaded.scatter.push_back(scatter);
                          }
                          else
                          {
                              Require(false, "unknown world record: " + command);
                          }
                      });
    for (const char* required :
         {"size", "start", "title", "introduction", "enemies", "props", "rules", "goal"})
    {
        Require(singleton.count(required) != 0, path + ": missing " + required);
    }
    auto inside = [&](float x, float y)
    {
        return x >= 2 && y >= 2 && x < loaded.width - 2 && y < loaded.height - 2;
    };
    Require(inside(loaded.startX, loaded.startY) && !loaded.regions.empty(),
            path + ": invalid start/regions");

    std::set<std::string> ids;
    ContentData::Load(ContentData::Path(root, enemyFile),
                      [&](const std::string& command, Row& row)
                      {
                          Require(command == "enemy", "enemy record required");
                          EnemyDefinition enemy;
                          enemy.id = row.Word();
                          enemy.name = row.Word();
                          enemy.modelName = row.Word();
                          Require(ids.insert(enemy.id).second, "duplicate enemy ID");
                          enemy.model = models.Find(enemy.modelName);
                          enemy.health = row.Integer(1, 100000);
                          enemy.attack = row.Integer(1, 10000);
                          enemy.defense = row.Integer(0, 10000);
                          enemy.experience = row.Integer(1, 10000);
                          enemy.speed = row.Number(.1f, 8);
                          enemy.windup = row.Number(.2f, 5);
                          enemy.cooldown = row.Number(.2f, 10);
                          enemy.range = row.Number(.5f, 2);
                          enemy.gait = row.Number(.1f, 12);
                          enemy.shadow = row.Number(20, 160);
                          enemy.barHeight = row.Number(30, 150);
                          enemy.aggro = row.Integer(2, 24);
                          loaded.enemies.push_back(enemy);
                      });
    ids.clear();
    ContentData::Load(ContentData::Path(root, propFile),
                      [&](const std::string& command, Row& row)
                      {
                          Require(command == "prop", "prop record required");
                          PropDefinition prop;
                          prop.id = row.Word();
                          prop.modelName = row.Word();
                          prop.role = row.Word();
                          Require(ids.insert(prop.id).second, "duplicate prop ID");
                          prop.model = models.Find(prop.modelName);
                          Require(prop.role == "static" || prop.role == "shrine" ||
                                      prop.role == "brazier" || prop.role == "fungal",
                                  "unknown prop role");
                          prop.ground = row.Integer(0, 1) != 0;
                          prop.blocking = row.Integer(0, 1) != 0;
                          prop.tall = row.Integer(0, 1) != 0;
                          prop.shadow = row.Number(20, 180);
                          Require(!prop.ground || !prop.blocking,
                                  "ground decoration cannot block movement");
                          loaded.props.push_back(prop);
                      });
    ids.clear();
    ContentData::Load(ContentData::Path(root, ruleFile),
                      [&](const std::string& command, Row& row)
                      {
                          Require(ids.insert(command).second, "duplicate rule record");
                          auto& r = loaded.rules;
                          if (command == "growth")
                          {
                              r.maximumLevel = row.Integer(2, 100);
                              r.experienceBase = row.Integer(1, 10000);
                              r.experienceStep = row.Integer(0, 10000);
                              r.pointsPerLevel = row.Integer(1, 10);
                              r.healthBase = row.Integer(1, 10000);
                              r.healthPerLevel = row.Integer(0, 1000);
                              r.healthPerVitality = row.Integer(1, 1000);
                              r.attackBase = row.Integer(1, 10000);
                              r.attackPerLevel = row.Integer(0, 1000);
                              r.attackPerStrength = row.Integer(1, 1000);
                              r.defenseBase = row.Integer(0, 1000);
                              r.defensePerResilience = row.Integer(1, 1000);
                              r.healing = row.Integer(1, 10000);
                          }
                          else if (command == "combat")
                          {
                              r.moveSpeed = row.Number(.5f, 8);
                              r.attackMoveSpeed = row.Number(.1f, 8);
                              r.attackRange = row.Number(.5f, 2.5f);
                              r.attackDuration = row.Number(.1f, 3);
                              r.attackCooldown = row.Number(r.attackDuration, 5);
                              r.attackHitTime = row.Number(.01f, r.attackDuration);
                              r.invulnerability = row.Number(.1f, 3);
                              r.minimumPotions = row.Integer(0, 20);
                              r.exchangeCost = row.Integer(1, 100);
                              r.dropPeriod = row.Integer(1, 100);
                          }
                          else
                          {
                              Require(false, "unknown rules record");
                          }
                      });
    Require(ids.count("growth") && ids.count("combat"), ruleFile + ": missing rules");
    Require(loaded.goalLevel <= loaded.rules.maximumLevel, path + ": unreachable level goal");
    for (const auto& region : loaded.regions)
    {
        Require(inside(region.x, region.y), path + ": region outside world");
        for (const auto& id : region.population)
        {
            loaded.Enemy(id);
        }
    }
    bool shrine = false;
    for (const auto& prop : loaded.placements)
    {
        const auto& type = loaded.Prop(prop.id);
        Require(inside(prop.x, prop.y), path + ": prop outside world");
        Require(!type.blocking ||
                    std::hypot(prop.x - loaded.startX - .5f, prop.y - loaded.startY - .5f) > 3,
                path + ": blocking prop overlaps spawn");
        shrine = shrine || type.role == "shrine";
    }
    Require(shrine, path + ": rest shrine required");
    for (const auto& patch : loaded.patches)
    {
        Require(inside(patch.x, patch.y), path + ": patch outside world");
    }
    for (const auto& road : loaded.roads)
    {
        Require(inside(road.ax, road.ay) && inside(road.bx, road.by),
                path + ": road outside world");
    }
    for (const auto& scatter : loaded.scatter)
    {
        loaded.Prop(scatter.id);
        Require(inside(scatter.x, scatter.y), path + ": scatter outside world");
    }
    Require(loaded.placements.size() <= 1000 && loaded.scatter.size() <= 100 &&
                loaded.patches.size() <= 256 && loaded.roads.size() <= 256,
            path + ": excessive placement count");
    *this = std::move(loaded);
}
