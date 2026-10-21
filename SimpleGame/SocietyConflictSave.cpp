#include "stdafx.h"
#include "WorldSociety.h"
#include "ContentData.h"
#include <iomanip>
#include <ostream>

void WorldSociety::WriteConflict(std::ostream& out) const
{
    if (!conflictEnabled)
    {
        return;
    }
    out << "war_clock " << conflictClock << '\n';
    for (int i = 0; i < int(contamination.size()); ++i)
    {
        const auto& cell = contamination[i];
        out << "war_cell " << i << ' ' << cell.density << ' ' << cell.scar << ' ' << cell.fire
            << '\n';
    }
    for (const auto& n : npcs)
    {
        out << "war_unit " << std::quoted(n.id) << ' ' << n.attackPattern << ' ' << n.attackSequence
            << ' ' << n.attackDuration << ' ' << n.attackOrigin.x << ' ' << n.attackOrigin.y << ' '
            << n.attackPoint.x << ' ' << n.attackPoint.y << '\n';
    }
}

bool WorldSociety::ReadConflict(const std::string& command, ContentData::Row& row)
{
    if (command.compare(0, 4, "war_") != 0)
    {
        return false;
    }
    using ContentData::Require;
    Require(conflictEnabled, "unexpected conflict save records");
    if (command == "war_clock")
    {
        Require(restoreSeen.insert(command).second, "duplicate war clock");
        conflictClock = row.Number(0, 2);
    }
    else if (command == "war_cell")
    {
        const int index = row.Integer(0, int(contamination.size()) - 1);
        Require(restoreSeen.insert(command + std::to_string(index)).second, "duplicate war cell");
        auto& cell = contamination[index];
        cell.density = row.Number(0, 1);
        cell.scar = row.Number(0, scarSeconds);
        cell.fire = row.Number(0, 3);
    }
    else if (command == "war_unit")
    {
        const int index = NpcIndex(row.Word());
        Require(index >= 0 && restoreSeen.insert(command + std::to_string(index)).second,
                "invalid/duplicate war unit");
        auto& n = npcs[index];
        n.attackPattern = row.Integer(0, 7);
        n.attackSequence = row.Integer(0, 1000000000);
        n.attackDuration = row.Number(.3f, 3);
        const float width = contaminationWidth * conflictCell;
        const float height = float(contamination.size() / contaminationWidth) * conflictCell;
        n.attackOrigin = {row.Number(0, width), row.Number(0, height)};
        n.attackPoint = {row.Number(0, width), row.Number(0, height)};
    }
    else
    {
        Require(false, "unknown conflict save record");
    }
    return true;
}

void WorldSociety::FinishConflictRestore() const
{
    if (!conflictEnabled)
    {
        return;
    }
    using ContentData::Require;
    Require(factions.size() == 2 && restoreSeen.count("war_clock"), "incomplete conflict save");
    for (int i = 0; i < int(contamination.size()); ++i)
    {
        Require(restoreSeen.count("war_cell" + std::to_string(i)) != 0, "missing war cell");
    }
    for (int i = 0; i < int(npcs.size()); ++i)
    {
        Require(restoreSeen.count("war_unit" + std::to_string(i)) != 0, "missing war unit");
        Require(npcs[i].windup <= npcs[i].attackDuration, "invalid saved attack timing");
    }
}
