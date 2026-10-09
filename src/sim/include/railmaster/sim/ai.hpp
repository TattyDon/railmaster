#pragma once

#include "railmaster/sim/railway.hpp"
#include "railmaster/sim/stock.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace railmaster::sim {

class World;

// A rival: a historical tycoon whose AI "tends to act like" the person [D].
// Personality values run 0-100 (rt3-clone-spec §13.2 [I]).
struct Tycoon {
    std::string key;
    std::string name;
    std::string company; // the name of the railroad they found
    std::string bio;
    std::int32_t expansion = 50;   // how often and how far they build
    std::int32_t leverage = 50;    // willingness to borrow
    std::int32_t dividend = 30;    // share of profit paid out
    std::int32_t speculation = 30; // trading in rivals' shares, short selling from 70
    std::int32_t takeovers = 30;   // building stakes in rivals, takeovers and mergers from 50
    std::int32_t industry = 30;    // buying and upgrading industries its lines serve, from 40
};

class TycoonRegistry {
public:
    // Parse {"tycoons": [...]}. Throws std::runtime_error on bad data.
    static TycoonRegistry from_json(std::string_view json_text);
    const std::vector<Tycoon>& all() const { return tycoons_; }

private:
    std::vector<Tycoon> tycoons_;
};

// A line a rival built: two of its stations with trains between them.
struct RivalRoute {
    StationId a = 0;
    StationId b = 0;
};

// What the AI remembers about one rival between turns.
struct Rival {
    PlayerId player = 0;
    std::size_t tycoon = 0; // index into TycoonRegistry::all()
    std::int32_t last_build_month = -1'000'000;
    std::vector<RivalRoute> routes;
};

// One month of decisions for a rival: finance, new lines, more trains, and
// share trading. Everything is done through World::execute, as a player
// would, so the rules and costs are the same.
void run_rival(World& world, Rival& rival, const Tycoon& tycoon);

} // namespace railmaster::sim
