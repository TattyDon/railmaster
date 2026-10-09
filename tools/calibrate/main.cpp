// Calibration: plays standard games headless and scores the economy against
// the targets the spec and research give (docs/spec/calibration.md). Exits
// 0 when every metric is in range, 1 otherwise, so it can gate changes to
// data/balance.json and the rates in data/*.json.
//
//   railmaster_calibrate [data-dir]

#include "railmaster/sim/demo.hpp"
#include "railmaster/sim/terrain.hpp"
#include "railmaster/sim/world.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace railmaster::sim;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

GameData load(const std::string& dir) {
    GameData d;
    d.balance = Balance::from_json(read_file(dir + "/balance.json"));
    d.cargo = CargoRegistry::from_json(read_file(dir + "/cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = LocomotiveRegistry::from_json(read_file(dir + "/locomotives.json"));
    d.industries = IndustryRegistry::from_json(read_file(dir + "/industries.json"), d.cargo);
    d.tycoons = TycoonRegistry::from_json(read_file(dir + "/tycoons.json"));
    return d;
}

void run_years(World& w, int years) {
    for (int d = 0; d < years * 365; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

struct Metric {
    std::string name;
    double value;
    double low, high;
    std::string unit;
    std::string source;
};

std::vector<Metric> metrics;

void report(std::string name, double value, double low, double high, std::string unit, std::string source) {
    metrics.push_back({std::move(name), value, low, high, std::move(unit), std::move(source)});
}

double dollars(Money m) { return static_cast<double>(m.whole_dollars()); }

const YearAccounts& year_of(const Company& c, std::int32_t year) {
    for (const YearAccounts& y : c.history())
        if (y.year == year) return y;
    return c.history().back();
}

// A: the economy alone, two years, no railway.
void economy_alone(const GameData& data) {
    WorldConfig cfg;
    cfg.seed = 1;
    World w(cfg, data);
    std::vector<std::int64_t> start;
    for (const Site& s : w.economy().sites()) start.push_back(s.produced_milli);
    run_years(w, 2);
    double raw_total = 0;
    int raw_n = 0;
    std::vector<double> farm_prices;
    for (const Site& s : w.economy().sites()) {
        if (s.kind != IndustryKind::Raw || s.id >= start.size()) continue;
        raw_total += static_cast<double>(s.produced_milli - start[s.id]) / kMilli / 2 / s.level;
        ++raw_n;
        farm_prices.push_back(dollars(industry_price(s, data.balance.industries)));
    }
    double proc_rate = 0;
    int proc_n = 0;
    for (const IndustryType& t : data.industries.all()) {
        if (t.kind != IndustryKind::Processor) continue;
        proc_rate += static_cast<double>(t.rate_milli) / kMilli;
        ++proc_n;
    }
    std::sort(farm_prices.begin(), farm_prices.end());
    report("raw producer output", raw_n ? raw_total / raw_n : 0, 1.5, 3.5, "loads/yr", "spec §6.1 [C]: ~2.2");
    report("processor capacity (data)", proc_n ? proc_rate / proc_n : 0, 2.0, 5.0, "loads/yr", "spec §6.1 [C]: ~3");
    report("producer price, median", farm_prices.empty() ? 0 : farm_prices[farm_prices.size() / 2], 240'000, 1'000'000,
           "$", "spec §6.2 [C]: farms $240K-$350K");
}

// D: the price field and middlemen on a strip of map, no railway
// (rt3-clone-spec §5.3 [C] behaviour). Nodes of 2 x 2 half-mile cells, as
// on the Small map.
constexpr std::int32_t kStripNodes = 48, kStripWidth = 9, kNodeCells = 2;

Economy strip(const GameData& data, bool mountains) {
    Economy eco(kStripNodes, kStripWidth, 2 * 805'000, data.cargo, data.balance, kNodeCells);
    Terrain t(kStripNodes * kNodeCells, kStripWidth * kNodeCells, 805);
    if (mountains) {
        // 150 m of relief across every node: mountains, about 9% grades.
        for (std::int32_t y = 0; y <= t.height(); ++y)
            for (std::int32_t x = 0; x <= t.width(); ++x) t.set_corner_height(x, y, (x + y) % 2 ? 150 : 0);
    }
    eco.set_terrain(t);
    return eco;
}

IndustryTypeId industry(const GameData& data, const char* key) { return *data.industries.find(key); }

void day(Economy& eco, const GameData& data) { eco.step_day(data.cargo, data.industries, 1850); }

// Years for middlemen to bring the first tenth of a carload of coal 30
// cells from a mine to a power plant: the leading edge of the spread, as
// middlemen fan cargo out rather than move it in a block. Nothing if not
// within `max_years`.
std::optional<double> middleman_years(const GameData& data, bool mountains, int max_years) {
    Economy eco = strip(data, mountains);
    const std::int32_t y = kStripWidth / 2, gap = 30 / kNodeCells;
    eco.add_site(data.industries, industry(data, "coal_mine"), 4, y);
    const SiteId plant = eco.add_site(data.industries, industry(data, "electric_plant"), 4 + gap, y);
    for (int d = 1; d <= max_years * 365; ++d) {
        day(eco, data);
        if (eco.sites()[plant].received_year_milli >= kMilli / 10) return d / 365.0;
    }
    return std::nullopt;
}

void price_field(const GameData& data) {
    const auto flat = middleman_years(data, false, 12), steep = middleman_years(data, true, 12);
    report("middlemen, flat land", flat ? 30 / *flat : 0, 4, 16, "cells/yr", "spec §5.3 [C]: ~8");
    report("middlemen, mountains", steep ? 30 / *steep : 0, 2, 8, "cells/yr", "spec §5.3 [C]: ~4");

    // With no demand, a mine's coal spreads only a few cells.
    {
        Economy eco = strip(data, false);
        const std::int32_t mx = kStripNodes / 2, my = kStripWidth / 2;
        const CargoId coal = *data.cargo.find("coal");
        eco.add_site(data.industries, industry(data, "coal_mine"), mx, my);
        for (int d = 0; d < 3 * 365; ++d) day(eco, data);
        // The radius, in cells, holding 90% of the stock.
        std::vector<std::int64_t> by_ring(kStripNodes, 0);
        std::int64_t total = 0;
        for (std::int32_t y = 0; y < kStripWidth; ++y)
            for (std::int32_t x = 0; x < kStripNodes; ++x) {
                const std::int64_t s = eco.stock_milli(coal, x, y);
                by_ring[static_cast<std::size_t>(std::max(std::abs(x - mx), std::abs(y - my)))] += s;
                total += s;
            }
        std::int64_t held = 0;
        std::size_t ring = 0;
        for (; ring < by_ring.size(); ++ring) {
            held += by_ring[ring];
            if (held * 10 >= total * 9) break;
        }
        report("spread with no demand, 3 years (90% within)", static_cast<double>(ring) * kNodeCells, 0, 8, "cells",
               "spec §5.3 [C]: a few cells");
    }

    // A new power plant reshapes coal prices 10 cells away over a year or two.
    {
        Economy eco = strip(data, false);
        const std::int32_t px = kStripNodes / 2, py = kStripWidth / 2;
        const CargoId coal = *data.cargo.find("coal");
        eco.add_site(data.industries, industry(data, "electric_plant"), px, py);
        const std::int32_t before = eco.price(coal, px - 10 / kNodeCells, py);
        std::vector<std::int32_t> seen;
        for (int d = 0; d < 4 * 365; ++d) {
            day(eco, data);
            seen.push_back(eco.price(coal, px - 10 / kNodeCells, py));
        }
        const std::int64_t change = seen.back() - before;
        std::size_t when = 0;
        while (when < seen.size() && (seen[when] - before) * 100 < change * 63) ++when;
        report("price map reshapes (63% at 10 cells)", static_cast<double>(when) / 365, 0.5, 2.5, "years",
               "spec §5.3 [C]: 1-2 years");
    }
}

// B: the demo network, ten years, six maps (fewer are too noisy to judge by).
void demo_network(const GameData& data) {
    double roi = 0, revenue = 0, growth = 0, price = 0, to_book = 0, grade = 0;
    int n = 0;
    for (const std::uint64_t seed : {1u, 2u, 3u, 4u, 5u, 6u}) {
        WorldConfig cfg;
        cfg.seed = seed;
        World w(cfg, data);
        try {
            build_demo_network(w);
        } catch (const std::exception&) {
            continue;
        }
        const Company& c = w.company();
        const Money invested = c.track_value() + c.building_value() + c.rolling_stock_value();
        std::vector<std::pair<std::size_t, std::int64_t>> served;
        for (const Station& s : w.railway().stations())
            if (s.town) served.emplace_back(*s.town, w.economy().town_houses(*s.town));
        const std::int32_t first = w.date().year();
        run_years(w, 10);
        // Years 2 to 4, once trains and prices have settled.
        Money profit, rev;
        for (std::int32_t y = first + 1; y <= first + 3; ++y) {
            profit += year_of(c, y).profit();
            rev += year_of(c, y).revenue();
        }
        roi += dollars(profit) / 3 / std::max(1.0, dollars(invested));
        revenue += dollars(rev) / 3;
        double g = 0;
        for (const auto& [t, before] : served)
            g += static_cast<double>(w.economy().town_houses(t) - before) / static_cast<double>(std::max<std::int64_t>(1, before));
        growth += served.empty() ? 0 : g / static_cast<double>(served.size());
        price += dollars(c.share_price());
        to_book += dollars(c.share_price()) / std::max(0.01, dollars(c.book_value_per_share()));
        grade = std::max(grade, static_cast<double>(c.credit_rating()));
        ++n;
    }
    if (n == 0) return;
    // Scenario goals of $10-40M of net worth over 25-30 years from $1-3M
    // are compound growth of about 4% ($3M to $10M in 30 years) to 16% ($1M
    // to $40M in 25) a year; the ceiling leaves some headroom.
    report("demo network: return on capital", roi / n * 100, 4, 20, "%/yr",
           "[I] scenario goals: 4-16% a year compound");
    report("demo network: revenue, years 2-4", revenue / n, 300'000, 2'000'000, "$/yr", "[I] as above");
    report("demo network: served towns grow, 10 years", growth / n * 100, 25, 200, "%",
           "spec §6.4 [C]: visible growth over 10-15 years");
    report("demo network: share price after 10 years", price / n, 15, 120, "$", "spec §12.3 [C]: typically $50-100");
    report("demo network: price / book value per share", to_book / n, 0.8, 2.5, "x",
           "spec §12.3 [C]: book value per share is the main anchor");
    report("demo network: credit grade (0 = A+, 4 = B)", grade, 0, 4, "worst",
           "spec §12.4 [C]: a profitable railroad can borrow");
}

// C: three AI rivals, five years.
void rivals(const GameData& data) {
    WorldConfig cfg;
    cfg.seed = 3;
    cfg.rivals = 3;
    World w(cfg, data);
    run_years(w, 5);
    int profitable = 0, n = 0;
    double revenue = 0, buildings = 0, freight = 0;
    for (const Rival& r : w.rivals()) {
        const auto co = w.investors()[r.player].chairs;
        if (!co) continue;
        const YearAccounts& y = w.company(*co).history()[w.company(*co).history().size() - 2];
        profitable += y.profit() > Money{};
        revenue += dollars(y.revenue());
        freight += dollars(y.lines[static_cast<std::size_t>(Ledger::FreightRevenue)]);
        buildings += dollars(y.lines[static_cast<std::size_t>(Ledger::StationBuildingIncome)]);
        ++n;
    }
    const auto count = static_cast<double>(std::max<std::size_t>(1, w.railway().station_buildings().size()));
    report("rivals profitable in year 5", n ? 100.0 * profitable / n : 0, 60, 100, "%", "spec §13 [I]: competent AI");
    report("rival revenue, year 5", n ? revenue / n : 0, 200'000, 5'000'000, "$/yr", "[I] as the demo network");
    report("rival revenue from freight, year 5", revenue > 0 ? 100 * freight / revenue : 0, 10, 80, "%",
           "spec §13 [I]: plans city pairs and industry-consumer pairs alike");
    report("station building income each", buildings / count, 0, 20'000, "$/yr", "spec §7.2 [C]: ~$1K");
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : RAILMASTER_DEFAULT_DATA_DIR;
    GameData data;
    try {
        data = load(dir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
    economy_alone(data);
    price_field(data);
    demo_network(data);
    rivals(data);
    int bad = 0;
    std::printf("%-44s %14s %22s  %s\n", "metric", "value", "target", "source");
    for (const Metric& m : metrics) {
        const bool ok = m.value >= m.low && m.value <= m.high;
        bad += !ok;
        char range[64];
        std::snprintf(range, sizeof range, "%.10g-%.10g %s", m.low, m.high, m.unit.c_str());
        std::printf("%-44s %14.1f %22s  %s %s\n", m.name.c_str(), m.value, range, ok ? "ok  " : (m.value < m.low ? "LOW " : "HIGH"),
                    m.source.c_str());
    }
    std::printf("%d of %zu metrics out of range\n", bad, metrics.size());
    return bad == 0 ? 0 : 1;
}
