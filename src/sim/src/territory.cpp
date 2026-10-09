#include "railmaster/sim/territory.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace railmaster::sim {

TerritoryId TerritoryMap::at(std::int32_t cx, std::int32_t cy) const {
    if (cx < 0 || cy < 0 || cx >= width || cy >= height || cells.empty()) return kNoTerritory;
    return cells[static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cx)];
}

namespace {

std::string territory_name(Random& rng) {
    static constexpr const char* kStem[] = {"Ald", "Bran", "Cor", "Dal", "Esk", "Fen", "Gar", "Hol", "Ister", "Kel",
                                            "Lom", "Mar", "Nor", "Os", "Pell", "Rav", "Sel", "Tor", "Val", "Wend"};
    static constexpr const char* kEnd[] = {"land", "mark", "shire", "ia", "avia", "onia", "heim", "wick"};
    return std::string(kStem[rng.below(std::size(kStem))]) + kEnd[rng.below(std::size(kEnd))];
}

} // namespace

TerritoryMap generate_territories(std::int32_t width, std::int32_t height, std::int32_t count, std::int32_t home_x,
                                  std::int32_t home_y, Random& rng, const Balance::MapGeneration& b) {
    if (width <= 0 || height <= 0 || count <= 0) throw std::invalid_argument("territories need a map and a count");
    TerritoryMap map;
    map.width = width;
    map.height = height;
    std::vector<std::pair<std::int32_t, std::int32_t>> seeds;
    for (std::int32_t i = 0; i < count; ++i) seeds.emplace_back(rng.between(0, width - 1), rng.between(0, height - 1));
    map.cells.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::int32_t y = 0; y < height; ++y) {
        for (std::int32_t x = 0; x < width; ++x) {
            std::size_t best = 0;
            std::int64_t best_d = -1;
            for (std::size_t s = 0; s < seeds.size(); ++s) {
                const std::int64_t dx = x - seeds[s].first, dy = y - seeds[s].second;
                const std::int64_t d = dx * dx + dy * dy;
                if (best_d < 0 || d < best_d) {
                    best = s;
                    best_d = d;
                }
            }
            map.cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                static_cast<TerritoryId>(best);
        }
    }
    const TerritoryId home = map.at(std::clamp(home_x, 0, width - 1), std::clamp(home_y, 0, height - 1));
    std::vector<std::string> used;
    for (std::int32_t i = 0; i < count; ++i) {
        Territory t;
        do t.name = territory_name(rng);
        while (std::find(used.begin(), used.end(), t.name) != used.end() && used.size() < 160);
        used.push_back(t.name);
        if (i != home) {
            t.access_cost = rng.chance(static_cast<std::uint32_t>(b.territory_big_percent), 100)
                                ? Money::dollars(b.territory_access_big)
                                : Money::dollars(rng.between(static_cast<std::int32_t>(b.territory_access_min / 1000),
                                                             static_cast<std::int32_t>(b.territory_access_max / 1000)) *
                                                 std::int64_t{1000});
            t.station_cost_percent = 100 + rng.between(0, b.territory_station_cost_max_percent);
            t.overhead_percent = 100 + rng.between(0, b.territory_overhead_max_percent);
            if (rng.chance(static_cast<std::uint32_t>(b.territory_credit_percent), 100)) t.credit_grades = rng.chance(1, 2) ? 1 : -1;
            t.closed_border = rng.chance(static_cast<std::uint32_t>(b.territory_closed_percent), 100);
        }
        map.territories.push_back(std::move(t));
    }
    return map;
}

} // namespace railmaster::sim
