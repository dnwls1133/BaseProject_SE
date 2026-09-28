#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct RegionDefinition;

// Regional indices are simulation state, separate from inventory and model caches.
class WorldSimulation
{
  public:
    struct RegionState
    {
        std::string id, name;
        double food = 0, infection = 0;
        double foodPerHour = 0, infectionPerHour = 0;
        std::uint64_t nextEventMinute = 0;
    };

    void Load(const std::string& root, const std::string& relative,
              const std::vector<RegionDefinition>& definitions, const std::string& world);
    void Update(float dt);
    void RecordMetrics(const std::string& regionId);
    const RegionState* FindRegion(const std::string& id) const;

    std::uint64_t Minute() const
    {
        return minute;
    }

    const std::string& News() const;

  private:
    std::string sourceFile, worldFile, eventId, eventMessage, lastNews, loggedRegion;
    std::vector<RegionState> regions;
    std::uint64_t minute = 0;
    double remainder = 0, minutesPerSecond = 0;
    double foodThreshold = 0, infectionThreshold = 0;
    double foodEffect = 0, infectionEffect = 0;
    int cooldownMinutes = 1;
    float newsTime = 0;
    void AdvanceMinute();
};
