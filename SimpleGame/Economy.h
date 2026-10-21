#pragma once
#include <array>
#include <cstdint>
#include <string>

// Food uses WorldSimulation's ratio (100 portions); medicine uses the existing clinic.
// Materials and currency are the only new stores. Prices are currency per unit.
struct EconomyRegion
{
    int warehouse = -1, market = -1, clinic = -1;
    float coins = 0, materials = 0;
    std::array<float, 3> target{}, price{}, produced{}, imported{}, exported{};
};

struct EconomyWorker
{
    float coins = 0;
    std::uint64_t order = 0, nextJob = 0, nextWork = 0;
    int lastWork = -1;
};

struct EconomyRecipe
{
    int place = -1, product = 0, ingredient = 0;
    float input = 0, output = 0, seconds = 0, wage = 0;
};

struct EconomyOrder
{
    std::uint64_t id = 0, available = 0, deadline = 0;
    int source = -1, destination = -1, good = 0, carrier = -1;
    // 0 posted, 1 reserved, 2 carrying, 3 returning, 4 delivered, 5 cancelled/lost.
    int supplier = -1, receiver = -1;
    int stage = 0;
    float units = 0, unitPrice = 0, freight = 0, escrow = 0;
    std::string outcome;
};

struct EconomyRules
{
    std::array<float, 3> basePrice{{1, 2, 1.5f}};
    std::uint64_t period = 12, lifetime = 900;
    float batch = 3, freight = .6f, startingCoins = 3;
};
