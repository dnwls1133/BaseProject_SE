#include "stdafx.h"
#include "WorldSociety.h"
#include "WorldSimulation.h"
#include "RandomLevelMap.h"
#include "LevelData.h"
#include "ContentData.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

bool WorldSociety::IsHive(int index) const
{
    return conflictEnabled && index >= 0 && index < int(npcs.size()) &&
           npcs[index].faction == hiveFaction;
}

const char* WorldSociety::AttackPatternName(int pattern) const
{
    static const char* names[] = {"대기",      "횡베기",    "관통 찌르기", "정화 화염",
                                  "도약 습격", "포자 투사", "분쇄 강타",   "감염 포낭"};
    return names[std::clamp(pattern, 0, 7)];
}

void WorldSociety::LoadConflict(const std::string& root, const std::string& file,
                                const LevelData& level)
{
    if (file.empty())
    {
        return;
    }
    using ContentData::Require;
    const auto path = ContentData::Path(root, file);
    const auto text = ContentData::Read(path);
    for (unsigned char byte : text)
    {
        signature = (signature ^ byte) * 16777619u;
    }
    std::set<std::string> seen;
    ContentData::Rows(path, text,
                      [&](const std::string& command, ContentData::Row& row)
                      {
                          if (command == "rules")
                          {
                              Require(seen.insert(command).second, "duplicate conflict rules");
                              knightFaction = FactionIndex(row.Word());
                              hiveFaction = FactionIndex(row.Word());
                              Require(factions.size() == 2 && knightFaction >= 0 &&
                                          hiveFaction >= 0 && knightFaction != hiveFaction,
                                      "conflict requires exactly two factions");
                              conflictCell = row.Number(2, 8);
                              spreadRate = row.Number(.005f, .3f);
                              scarSeconds = row.Number(5, 180);
                              contaminationWidth = int(std::ceil(level.width / conflictCell));
                              contamination.resize(contaminationWidth *
                                                   int(std::ceil(level.height / conflictCell)));
                          }
                          else if (command == "attack")
                          {
                              const int pattern = row.Integer(1, 7);
                              Require(seen.insert("attack:" + std::to_string(pattern)).second,
                                      "duplicate pattern");
                              auto& a = conflictAttacks[pattern];
                              a.range = row.Number(1, 12);
                              a.radius = row.Number(.3f, 5);
                              a.windup = row.Number(.3f, 3);
                              a.cooldown = row.Number(.5f, 10);
                              a.damage = row.Number(.1f, 3);
                          }
                          else if (command == "unit")
                          {
                              const int n = NpcIndex(row.Word());
                              Require(n >= 0 && seen.insert("unit:" + npcs[n].id).second,
                                      "invalid conflict unit");
                              npcs[n].combatClass = row.Integer(0, 7);
                              npcs[n].health = row.Number(40, 100);
                          }
                          else if (command == "seed")
                          {
                              Require(!contamination.empty(), "conflict rules must precede seed");
                              const float x = row.Number(0, float(level.width - 1));
                              const float y = row.Number(0, float(level.height - 1));
                              const float radius = row.Number(1, 30);
                              for (int i = 0; i < int(contamination.size()); ++i)
                              {
                                  const float cx = (i % contaminationWidth + .5f) * conflictCell;
                                  const float cy = (i / contaminationWidth + .5f) * conflictCell;
                                  if (std::hypot(cx - x, cy - y) <= radius)
                                  {
                                      contamination[i].density = .8f;
                                  }
                              }
                          }
                          else
                          {
                              Require(false, "unknown conflict record: " + command);
                          }
                      });
    Require(seen.count("rules") != 0, "missing conflict rules");
    for (int pattern = 1; pattern <= 7; ++pattern)
    {
        Require(seen.count("attack:" + std::to_string(pattern)) != 0, "missing attack pattern");
    }
    for (const auto& n : npcs)
    {
        Require(seen.count("unit:" + n.id) != 0, "missing unit profile: " + n.id);
        Require(n.faction != hiveFaction || n.combatClass >= 4, "hive requires biological profile");
        Require(n.faction != knightFaction || n.combatClass <= 3, "invalid knight profile");
    }
    diplomacy[knightFaction][hiveFaction] = diplomacy[hiveFaction][knightFaction] = -100;
    conflictEnabled = true;
}

void WorldSociety::BurnContamination(Point center, float radius, int actor)
{
    int changed = 0;
    for (int i = 0; i < int(contamination.size()); ++i)
    {
        const float x = (i % contaminationWidth + .5f) * conflictCell;
        const float y = (i / contaminationWidth + .5f) * conflictCell;
        if (std::hypot(x - center.x, y - center.y) > radius + conflictCell * .5f)
        {
            continue;
        }
        auto& cell = contamination[i];
        if (cell.density > .01f)
        {
            ++changed;
            cell.density = 0;
            cell.scar = scarSeconds;
            cell.fire = 3;
        }
    }
    if (changed)
    {
        Record("contamination_burn", npcs[actor].id, "terrain", std::to_string(changed), "0",
               "소각으로 오염 제거 · 재확산 지연");
    }
}

void WorldSociety::TickContamination(float dt, WorldSimulation& world, const RandomLevelMap& map)
{
    if (!conflictEnabled)
    {
        return;
    }
    PROFILE_SCOPE(ConflictField);
    conflictClock += dt;
    if (conflictClock < 1)
    {
        return;
    }
    // Fixed simulation seconds, independent of view culling and accelerated calendar time.
    conflictClock -= 1;
    auto next = contamination;
    int dirty = 0, burning = 0;
    for (int i = 0; i < int(contamination.size()); ++i)
    {
        const float x = (i % contaminationWidth + .5f) * conflictCell;
        const float y = (i / contaminationWidth + .5f) * conflictCell;
        auto& cell = next[i];
        cell.fire = std::max(0.f, cell.fire - 1);
        cell.scar = std::max(0.f, cell.scar - 1);
        if (!map.CanStand(x, y))
        {
            cell.density = 0;
            continue;
        }
        if (cell.scar <= 0)
        {
            float sourceDensity = contamination[i].density;
            for (int offset : {-1, 1, -contaminationWidth, contaminationWidth})
            {
                const int j = i + offset;
                if (j < 0 || j >= int(contamination.size()) ||
                    std::abs(j % contaminationWidth - i % contaminationWidth) > 1)
                {
                    continue;
                }
                const float nx = (j % contaminationWidth + .5f) * conflictCell;
                const float ny = (j / contaminationWidth + .5f) * conflictCell;
                if (map.CanTravel(x, y, nx, ny))
                {
                    sourceDensity = std::max(sourceDensity, contamination[j].density - .45f);
                }
            }
            cell.density = std::min(1.f, cell.density + sourceDensity * spreadRate);
        }
        dirty += cell.density > .02f;
        burning += cell.fire > 0;
    }
    contamination.swap(next);
    for (int i = 0; i < int(npcs.size()); ++i)
    {
        auto& n = npcs[i];
        if (!n.Alive())
        {
            continue;
        }
        const int cell = int(n.y / conflictCell) * contaminationWidth + int(n.x / conflictCell);
        if (cell < 0 || cell >= int(contamination.size()))
        {
            continue;
        }
        if (IsHive(i) && contamination[cell].fire > 0)
        {
            Damage(-1, i, 4, world, "정화 화염");
        }
        if (IsHive(i) && n.combatClass == 7 && contamination[cell].scar <= 0)
        {
            contamination[cell].density = std::min(1.f, contamination[cell].density + .12f);
        }
    }
    for (auto& region : regions)
    {
        float sum = 0;
        int count = 0;
        for (int i = 0; i < int(contamination.size()); ++i)
        {
            const float x = (i % contaminationWidth + .5f) * conflictCell;
            const float y = (i / contaminationWidth + .5f) * conflictCell;
            if (std::hypot(x - region.x, y - region.y) < 15)
            {
                sum += contamination[i].density;
                ++count;
            }
        }
        region.hive = count ? sum / count : 0;
        if (world.Minute() != minute)
        {
            const auto* state = world.FindRegion(region.id);
            if (state)
            {
                world.ChangeRegion(region.id, 0, region.hive - state->infection, "contamination",
                                   "오염 지대의 실제 피복률 반영");
            }
        }
    }
    Profile::Set(Profile::Metric::ContaminatedCells, dirty);
    Profile::Set(Profile::Metric::BurningCells, burning);
}
