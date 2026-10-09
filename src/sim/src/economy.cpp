#include "railmaster/sim/economy.hpp"

#include "railmaster/sim/fixed_math.hpp"
#include "railmaster/sim/random.hpp"
#include "railmaster/sim/terrain.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace railmaster::sim {

namespace {

IndustryKind parse_kind(const std::string& s, const std::string& key) {
    if (s == "raw") return IndustryKind::Raw;
    if (s == "processor") return IndustryKind::Processor;
    if (s == "sink") return IndustryKind::Sink;
    if (s == "house") return IndustryKind::House;
    if (s == "port") return IndustryKind::Port;
    if (s == "warehouse") return IndustryKind::Warehouse;
    throw std::runtime_error("industry data: unknown kind '" + s + "' for '" + key + "'");
}

CargoId resolve(const CargoRegistry& cargo, const std::string& key, const std::string& industry) {
    const auto id = cargo.find(key);
    if (!id) throw std::runtime_error("industry data: unknown cargo '" + key + "' in '" + industry + "'");
    return *id;
}

std::int64_t base_dollars(const CargoType& c) { return c.base_price.whole_dollars(); }

std::int32_t percent_of(std::int64_t base, std::int32_t pct) { return static_cast<std::int32_t>(base * pct / 100); }

// Express cargo (passengers, mail, troops) is handled by the freight code,
// which sends each load to a destination; the price field only trades freight.
bool input_active(const IndustryInput& in, const CargoRegistry& cargo, std::int32_t year) {
    const CargoType& c = cargo.get(in.cargo);
    return c.cargo_class == CargoClass::Freight && year >= c.available_year &&
           (!in.until_year || year <= *in.until_year);
}

bool cargo_available(CargoId c, const CargoRegistry& cargo, std::int32_t year) {
    return cargo.get(c).cargo_class == CargoClass::Freight && year >= cargo.get(c).available_year;
}

std::string town_name(Random& rng) {
    static constexpr const char* kFirst[] = {"Ash",  "Brook", "Car",   "Dun",   "Elm",  "Fair", "Glen",
                                             "Hart", "Iron",  "Kings", "Lake",  "Mill", "North", "Oak",
                                             "Port", "Red",   "Stone", "West",  "Wood", "Bright"};
    static constexpr const char* kSecond[] = {"ford", "vale", "ton",  "bury",  "field", "wick",  "port", "ham",
                                              "burg", "ville", "dale", "mouth", "ridge", "crest", "haven"};
    return std::string(kFirst[rng.below(std::size(kFirst))]) + kSecond[rng.below(std::size(kSecond))];
}

} // namespace

// --- Industry data -----------------------------------------------------------

IndustryRegistry IndustryRegistry::from_json(std::string_view json_text, const CargoRegistry& cargo) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("industry data: ") + e.what());
    }
    IndustryRegistry reg;
    for (const auto& entry : doc.at("industries")) {
        IndustryType t;
        t.key = entry.at("key").get<std::string>();
        if (reg.find(t.key)) throw std::runtime_error("industry data: duplicate key '" + t.key + "'");
        t.id = static_cast<IndustryTypeId>(reg.types_.size());
        t.name = entry.at("name").get<std::string>();
        t.kind = parse_kind(entry.at("kind").get<std::string>(), t.key);
        const std::string rule = entry.value("rule", std::string("any"));
        if (rule == "all") t.rule = InputRule::All;
        else if (rule != "any") throw std::runtime_error("industry data: unknown rule '" + rule + "' for '" + t.key + "'");
        if (t.kind == IndustryKind::Raw) {
            for (const auto& b : entry.value("boosters", nlohmann::json::array())) {
                t.inputs.push_back({resolve(cargo, b.get<std::string>(), t.key), std::nullopt});
            }
        } else {
            for (const auto& in : entry.value("inputs", nlohmann::json::array())) {
                IndustryInput i{resolve(cargo, in.at("cargo").get<std::string>(), t.key), std::nullopt};
                if (in.contains("until")) i.until_year = in["until"].get<std::int32_t>();
                t.inputs.push_back(i);
            }
        }
        for (const auto& out : entry.value("outputs", nlohmann::json::array())) {
            t.outputs.push_back(resolve(cargo, out.get<std::string>(), t.key));
        }
        // Read as a decimal and fixed to thousandths at once, so nothing
        // downstream sees floating point.
        t.rate_milli = std::llround(entry.at("rate").get<double>() * kMilli);
        if (t.rate_milli <= 0) throw std::runtime_error("industry data: bad rate for '" + t.key + "'");
        const bool needs_inputs = t.kind == IndustryKind::Processor || t.kind == IndustryKind::Sink;
        if (needs_inputs && t.inputs.empty()) throw std::runtime_error("industry data: '" + t.key + "' has no inputs");
        if (trades(t.kind) && t.inputs.empty() && t.outputs.empty()) {
            throw std::runtime_error("industry data: port '" + t.key + "' trades nothing");
        }
        if (t.kind == IndustryKind::Raw && t.outputs.empty()) {
            throw std::runtime_error("industry data: '" + t.key + "' produces nothing");
        }
        reg.types_.push_back(std::move(t));
    }
    return reg;
}

std::optional<IndustryTypeId> IndustryRegistry::find(std::string_view key) const {
    for (const auto& t : types_) {
        if (t.key == key) return t.id;
    }
    return std::nullopt;
}

// --- The economy grid --------------------------------------------------------

Economy::Economy(std::int32_t width_cells, std::int32_t height_cells, std::int64_t cell_size_mm,
                 const CargoRegistry& cargo, const Balance& balance, std::int32_t cells_per_node)
    : width_(width_cells), height_(height_cells), cell_size_mm_(cell_size_mm),
      cells_per_node_(std::max(1, cells_per_node)), balance_(balance.economy) {
    warehouse_radius_ = balance.industries.warehouse_radius_cells;
    warehouse_spoilage_percent_ = balance.industries.warehouse_spoilage_percent;
    if (width_ <= 0 || height_ <= 0 || cell_size_mm_ <= 0) throw std::invalid_argument("bad economy grid size");
    const auto cells = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    const std::size_t n = cargo.all().size();
    price_.resize(n);
    stock_.assign(n, std::vector<std::int32_t>(cells, 0));
    anchors_.resize(n);
    active_.assign(n, false);
    for (const CargoType& c : cargo.all()) {
        price_[c.id].assign(cells, percent_of(base_dollars(c), balance_.neutral_price_percent));
    }
    scratch_.resize(cells);
    conductance_.assign(cells, 1000);
    water_.assign(cells, false);
    east_w_.assign(cells, 1000);
    south_w_.assign(cells, 1000);
    for (std::int32_t y = 0; y < height_; ++y) east_w_[cell(width_ - 1, y)] = 0;
    for (std::int32_t x = 0; x < width_; ++x) south_w_[cell(x, height_ - 1)] = 0;
}

void Economy::set_terrain(const Terrain& terrain) {
    const std::int32_t k = cells_per_node_;
    if (terrain.width() < (width_ - 1) * k + 1 || terrain.width() > width_ * k ||
        terrain.height() < (height_ - 1) * k + 1 || terrain.height() > height_ * k) {
        throw std::invalid_argument("terrain and economy grids differ in size");
    }
    // The map cells a node covers (the last row and column may be partial).
    const auto tiles = [&](std::int32_t x, std::int32_t y, auto&& f) {
        for (std::int32_t ty = y * k; ty < std::min(terrain.height(), (y + 1) * k); ++ty)
            for (std::int32_t tx = x * k; tx < std::min(terrain.width(), (x + 1) * k); ++tx) f(tx, ty);
    };
    // A node is water when most of it is; it is coast when any of its cells,
    // or a neighbouring node, is water.
    std::vector<bool> wet(water_.size(), false);
    for (std::int32_t y = 0; y < height_; ++y) {
        for (std::int32_t x = 0; x < width_; ++x) {
            std::int32_t n = 0, w = 0;
            tiles(x, y, [&](std::int32_t tx, std::int32_t ty) {
                ++n;
                w += terrain.ground(tx, ty) == GroundType::Water;
            });
            water_[cell(x, y)] = n > 0 && 2 * w > n;
            wet[cell(x, y)] = w > 0;
        }
    }
    const auto water = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && x < width_ && y < height_ && water_[cell(x, y)];
    };
    for (std::int32_t y = 0; y < height_; ++y) {
        for (std::int32_t x = 0; x < width_; ++x) {
            std::int32_t c = 1000;
            if (water(x, y)) {
                c = balance_.water_conductance_permille;
            } else {
                // Relief across the node: the highest corner less the lowest.
                std::int32_t lo = terrain.corner_height(x * k, y * k), hi = lo;
                const std::int32_t x1 = std::min(terrain.width(), (x + 1) * k), y1 = std::min(terrain.height(), (y + 1) * k);
                for (std::int32_t ty = y * k; ty <= y1; ++ty) {
                    for (std::int32_t tx = x * k; tx <= x1; ++tx) {
                        lo = std::min(lo, terrain.corner_height(tx, ty));
                        hi = std::max(hi, terrain.corner_height(tx, ty));
                    }
                }
                const std::int64_t span_m = std::int64_t{terrain.tile_size_m()} * std::max(1, std::min(x1 - x * k, y1 - y * k));
                const std::int64_t grade_bp = std::int64_t{hi - lo} * 10000 / span_m;
                if (grade_bp >= balance_.mountain_grade_bp) c = balance_.mountain_conductance_permille;
                else if (grade_bp >= balance_.hill_grade_bp) c = balance_.hill_conductance_permille;
                else if (wet[cell(x, y)] || water(x - 1, y) || water(x + 1, y) || water(x, y - 1) || water(x, y + 1))
                    c = balance_.coast_conductance_permille;
            }
            conductance_[cell(x, y)] = std::max(1, c);
        }
    }
    // An edge conducts as the mean of the cells it joins.
    for (std::int32_t y = 0; y < height_; ++y) {
        for (std::int32_t x = 0; x < width_; ++x) {
            const std::size_t i = cell(x, y);
            east_w_[i] = x + 1 < width_ ? (conductance_[i] + conductance_[i + 1]) / 2 : 0;
            south_w_[i] = y + 1 < height_ ? (conductance_[i] + conductance_[cell(x, y + 1)]) / 2 : 0;
        }
    }
}

bool Economy::within(std::int32_t cx, std::int32_t cy, MapPoint p, std::int64_t r_mm) const {
    const MapPoint c = node_centre(cx, cy);
    return std::abs(c.x_mm - p.x_mm) <= r_mm && std::abs(c.y_mm - p.y_mm) <= r_mm;
}

std::int64_t Economy::area_permille() const {
    constexpr std::int64_t kReferenceMm = 128'000'000; // 128 km
    const std::int64_t wmm = width_ * cell_size_mm_, hmm = height_ * cell_size_mm_;
    // Smaller maps still get the usual numbers.
    return std::max<std::int64_t>(1000, (wmm / 1000) * (hmm / 1000) / (kReferenceMm / 1000 * (kReferenceMm / 1000) / 1000));
}

std::size_t Economy::cell(std::int32_t cx, std::int32_t cy) const {
    if (cx < 0 || cy < 0 || cx >= width_ || cy >= height_) throw std::out_of_range("economy cell out of range");
    return static_cast<std::size_t>(cy) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(cx);
}

std::int32_t Economy::cell_x(MapPoint p) const {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(p.x_mm / cell_size_mm_, 0, width_ - 1));
}

std::int32_t Economy::cell_y(MapPoint p) const {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(p.y_mm / cell_size_mm_, 0, height_ - 1));
}

SiteId Economy::add_site(const IndustryRegistry& industries, IndustryTypeId type, std::int32_t cx, std::int32_t cy,
                         std::int32_t level) {
    cell(cx, cy); // range check
    if (level <= 0) throw std::invalid_argument("site level must be positive");
    Site s;
    s.id = static_cast<SiteId>(sites_.size());
    s.type = type;
    s.kind = industries.get(type).kind;
    s.cx = cx;
    s.cy = cy;
    s.level = level;
    s.buffer.assign(industries.get(type).inputs.size(), 0);
    s.made_milli.assign(industries.get(type).outputs.size(), 0);
    s.used_milli.assign(industries.get(type).inputs.size(), 0);
    sites_.push_back(std::move(s));
    return sites_.back().id;
}

void Economy::add_stock(CargoId c, std::int32_t cx, std::int32_t cy, std::int32_t milli) {
    std::int32_t& s = stock_[c][cell(cx, cy)];
    s = std::min(balance_.max_stock_milli * kMicroPerMilli, s + milli * kMicroPerMilli);
}

std::int32_t Economy::take_stock(CargoId c, std::int32_t cx, std::int32_t cy, std::int32_t milli) {
    std::int32_t& s = stock_[c][cell(cx, cy)];
    const std::int32_t taken = std::clamp(milli, 0, s / kMicroPerMilli);
    s -= taken * kMicroPerMilli;
    return taken;
}

std::int32_t Economy::equilibrium_price(const CargoType& c, std::int64_t demand_milli,
                                       std::int64_t supply_milli) const {
    const std::int64_t eps = std::max(1, balance_.price_epsilon_milli);
    const std::int64_t ratio = pow_ratio_permille(std::max<std::int64_t>(0, demand_milli) + eps,
                                                  std::max<std::int64_t>(0, supply_milli) + eps,
                                                  balance_.price_alpha_permille);
    const std::int64_t base = base_dollars(c);
    const std::int64_t pct10 = std::clamp<std::int64_t>(ratio * balance_.neutral_price_percent / 100,
                                                        std::int64_t{balance_.price_floor_percent} * 10,
                                                        std::int64_t{balance_.price_ceiling_percent} * 10);
    return static_cast<std::int32_t>(base * pct10 / 1000);
}

std::int32_t Economy::output_pace(const CargoRegistry& cargo, const IndustryType& t, std::int64_t rate, std::size_t at,
                                  std::int32_t year) const {
    const std::int32_t stop = balance_.output_stop_percent, full = balance_.output_full_percent;
    if (full <= stop) return 1000; // switched off
    std::int32_t best = 0;
    bool any = false;
    for (CargoId c : t.outputs) {
        if (!cargo_available(c, cargo, year)) continue;
        any = true;
        // This producer's own equilibrium price with its unsold stock,
        // against what it would be with none (before the floor and ceiling).
        const std::int64_t clear = rate * balance_.supply_days + std::max(1, balance_.price_epsilon_milli);
        const std::int64_t pct =
            pow_ratio_permille(clear, clear + stock_[c][at] / kMicroPerMilli, balance_.price_alpha_permille) / 10;
        best = std::max(best, static_cast<std::int32_t>(std::clamp<std::int64_t>((pct - stop) * 1000 / (full - stop), 0, 1000)));
    }
    return any ? best : 1000;
}

void Economy::run_sites(const CargoRegistry& cargo, const IndustryRegistry& industries, std::int32_t year) {
    for (auto& a : anchors_) a.clear();
    // Each site pulls on its node's price: consumers by what they want over
    // their horizon, producers by what they make over theirs (§5.3).
    auto supply = [&](CargoId c, std::size_t at, std::int64_t daily) {
        anchors_[c].push_back({at, 0, daily * balance_.supply_days});
    };
    auto demand = [&](CargoId c, std::size_t at, std::int64_t daily, std::int32_t days) {
        anchors_[c].push_back({at, daily * days, 0});
    };

    for (Site& s : sites_) {
        if (s.closed) continue;
        const IndustryType& t = industries.get(s.type);
        const std::size_t at = cell(s.cx, s.cy);
        // `rate` is what the site makes or wants at full pace; `daily` what it
        // actually makes, slowed when its products sell cheaply here [C].
        const auto rate = static_cast<std::int32_t>(t.rate_milli * s.level * activity_percent_ / (365 * 100));
        std::int32_t daily = rate;
        if (t.kind == IndustryKind::Raw || t.kind == IndustryKind::Processor) {
            s.pace_permille = output_pace(cargo, t, rate, at, year);
            daily = static_cast<std::int32_t>(std::int64_t{rate} * s.pace_permille / 1000);
        }

        switch (t.kind) {
        case IndustryKind::Raw: {
            bool boosted = false;
            for (std::size_t i = 0; i < t.inputs.size(); ++i) {
                const IndustryInput& in = t.inputs[i];
                if (!input_active(in, cargo, year)) continue;
                const std::int32_t want = daily / 2;
                const std::int32_t took = take_stock(in.cargo, s.cx, s.cy, want);
                s.used_milli[i] += took;
                if (took >= want && want > 0) boosted = true;
                demand(in.cargo, at, rate, balance_.industry_demand_days);
            }
            const std::int32_t out = daily * (100 + (boosted ? balance_.boost_percent : 0)) / 100;
            for (std::size_t o = 0; o < t.outputs.size(); ++o) {
                const CargoId c = t.outputs[o];
                if (!cargo_available(c, cargo, year)) continue;
                add_stock(c, s.cx, s.cy, out);
                s.produced_milli += out;
                s.made_milli[o] += out;
                supply(c, at, rate);
            }
            break;
        }
        case IndustryKind::Processor: {
            const std::int32_t cap = daily * balance_.input_buffer_days;
            std::int32_t can_make = t.rule == InputRule::All ? daily : 0;
            bool any_input = false;
            for (std::size_t i = 0; i < t.inputs.size(); ++i) {
                const IndustryInput& in = t.inputs[i];
                if (!input_active(in, cargo, year)) continue;
                any_input = true;
                s.buffer[i] += take_stock(in.cargo, s.cx, s.cy, cap - s.buffer[i]);
                demand(in.cargo, at, rate, balance_.industry_demand_days);
                if (t.rule == InputRule::All) can_make = std::min(can_make, s.buffer[i]);
                else can_make += s.buffer[i];
            }
            const std::int32_t made = any_input ? std::min(daily, can_make) : 0;
            // Use up the inputs: all of them equally, or in listed order.
            std::int32_t owed = made;
            for (std::size_t i = 0; i < t.inputs.size(); ++i) {
                if (!input_active(t.inputs[i], cargo, year)) continue;
                if (t.rule == InputRule::All) {
                    s.buffer[i] -= made;
                    s.used_milli[i] += made;
                } else {
                    const std::int32_t use = std::min(owed, s.buffer[i]);
                    s.buffer[i] -= use;
                    s.used_milli[i] += use;
                    owed -= use;
                }
            }
            for (std::size_t o = 0; o < t.outputs.size(); ++o) {
                const CargoId c = t.outputs[o];
                if (!cargo_available(c, cargo, year)) continue;
                add_stock(c, s.cx, s.cy, made);
                s.made_milli[o] += made;
                supply(c, at, rate);
            }
            s.produced_milli += made;
            break;
        }
        case IndustryKind::Port:
        case IndustryKind::Warehouse: {
            // Exports leave; imports arrive, both up to capacity.
            if (s.port_mode != PortMode::Supply) {
                for (std::size_t i = 0; i < t.inputs.size(); ++i) {
                    const IndustryInput& in = t.inputs[i];
                    if (!input_active(in, cargo, year)) continue;
                    const std::int32_t took = take_stock(in.cargo, s.cx, s.cy, daily);
                    s.received_year_milli += took;
                    s.used_milli[i] += took;
                    demand(in.cargo, at, rate, balance_.industry_demand_days);
                }
            }
            if (s.port_mode != PortMode::Receive) {
                for (std::size_t o = 0; o < t.outputs.size(); ++o) {
                    const CargoId c = t.outputs[o];
                    if (!cargo_available(c, cargo, year)) continue;
                    add_stock(c, s.cx, s.cy, daily);
                    s.produced_milli += daily;
                    s.made_milli[o] += daily;
                    supply(c, at, rate);
                }
            }
            break;
        }
        case IndustryKind::Sink:
        case IndustryKind::House: {
            const std::int32_t days = t.kind == IndustryKind::House ? balance_.town_demand_days
                                                                    : balance_.industry_demand_days;
            for (const IndustryInput& in : t.inputs) {
                if (!input_active(in, cargo, year)) continue;
                const std::int32_t took = take_stock(in.cargo, s.cx, s.cy, daily);
                if (t.kind == IndustryKind::Sink) s.received_year_milli += took;
                demand(in.cargo, at, rate, days);
            }
            for (CargoId c : t.outputs) {
                if (!cargo_available(c, cargo, year)) continue;
                add_stock(c, s.cx, s.cy, daily);
                s.produced_milli += daily;
                supply(c, at, rate);
            }
            break;
        }
        }
    }
    for (const CargoType& c : cargo.all()) {
        active_[c.id] = c.cargo_class == CargoClass::Freight && !anchors_[c.id].empty();
    }
}

void Economy::spoil(const CargoRegistry& cargo) {
    // Which warehouse, if any, protects each cell (the first built wins).
    std::vector<std::int64_t> cover;
    for (const Site& w : sites_) {
        if (w.closed || w.kind != IndustryKind::Warehouse) continue;
        if (cover.empty()) cover.assign(scratch_.size(), -1);
        for_nodes_within(node_centre(w.cx, w.cy), cells_mm(warehouse_radius_), [&](std::int32_t x, std::int32_t y) {
            std::int64_t& c = cover[cell(x, y)];
            if (c < 0) c = w.id;
        });
    }
    for (const CargoType& c : cargo.all()) {
        const std::int64_t per_mille = std::int64_t{c.decay_sensitivity} * balance_.spoilage_per_mille_per_sensitivity;
        std::vector<std::int32_t>& stock = stock_[c.id];
        for (std::size_t i = 0; i < stock.size(); ++i) {
            std::int32_t& s = stock[i];
            if (s <= 0) continue;
            const auto loss = static_cast<std::int32_t>(std::max<std::int64_t>(1, s * per_mille / 1000)); // micro
            if (cover.empty() || cover[i] < 0) {
                s -= loss;
                continue;
            }
            const std::int32_t kept = loss - loss * warehouse_spoilage_percent_ / 100;
            s -= loss - kept;
            sites_[static_cast<std::size_t>(cover[i])].spoilage_saved +=
                c.base_price.scaled(kept, std::int64_t{kMilli} * kMicroPerMilli);
        }
    }
}

namespace {

// Move `from` toward `to` by 1/days of the gap, rounded to the nearest dollar.
std::int32_t toward(std::int32_t from, std::int64_t to, std::int32_t days) {
    const std::int64_t gap = to - from, d = std::max(1, days);
    return static_cast<std::int32_t>(from + (gap >= 0 ? (gap + d / 2) / d : -((-gap + d / 2) / d)));
}

} // namespace

void Economy::relax(const CargoRegistry& cargo) {
    const auto w = static_cast<std::size_t>(width_);
    const auto h = static_cast<std::size_t>(height_);
    std::vector<std::size_t> order;
    for (const CargoType& c : cargo.all()) {
        if (!active_[c.id]) continue;
        std::vector<std::int32_t>& p = price_[c.id];
        const std::vector<std::int32_t>& stock = stock_[c.id];
        const std::int32_t neutral = equilibrium_price(c, 0, 0);

        // Nodes with no sites: smoothed toward their neighbours, weighted
        // by how well each edge conducts, and relaxing toward their own
        // equilibrium (neutral, less any stock lying there).
        for (std::size_t y = 0; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                const std::size_t i = y * w + x;
                std::int64_t pull = 0; // sum of conductance x (neighbour - here), permille
                if (x > 0) pull += std::int64_t{east_w_[i - 1]} * (p[i - 1] - p[i]);
                if (x + 1 < w) pull += std::int64_t{east_w_[i]} * (p[i + 1] - p[i]);
                if (y > 0) pull += std::int64_t{south_w_[i - w]} * (p[i - w] - p[i]);
                if (y + 1 < h) pull += std::int64_t{south_w_[i]} * (p[i + w] - p[i]);
                const std::int32_t target = stock[i] > 0 ? equilibrium_price(c, 0, stock[i] / kMicroPerMilli) : neutral;
                const std::int32_t relaxed = toward(p[i], target, balance_.field_relax_days);
                scratch_[i] = static_cast<std::int32_t>(relaxed + pull * balance_.coupling_permille / 1'000'000);
            }
        }

        // Nodes with sites: their own demand and supply (stock included) set
        // the equilibrium, which their price approaches over months.
        std::vector<Anchor>& anchors = anchors_[c.id];
        std::sort(anchors.begin(), anchors.end(), [](const Anchor& a, const Anchor& b) { return a.cell < b.cell; });
        for (std::size_t k = 0; k < anchors.size();) {
            const std::size_t at = anchors[k].cell;
            std::int64_t d = 0, s = stock[at] / kMicroPerMilli;
            for (; k < anchors.size() && anchors[k].cell == at; ++k) {
                d += anchors[k].demand_milli;
                s += anchors[k].supply_milli;
            }
            scratch_[at] = toward(p[at], equilibrium_price(c, d, s), balance_.site_relax_days);
        }
        p.swap(scratch_);
    }
}

void Economy::drift(const CargoRegistry& cargo) {
    const auto w = static_cast<std::size_t>(width_);
    const auto h = static_cast<std::size_t>(height_);
    for (const CargoType& c : cargo.all()) {
        if (!active_[c.id]) continue;
        const std::vector<std::int32_t>& p = price_[c.id];
        std::vector<std::int32_t>& s = stock_[c.id];
        const std::int64_t base = std::max<std::int64_t>(1, base_dollars(c));
        const std::int64_t cost = percent_of(base, balance_.transport_cost_percent);
        std::fill(scratch_.begin(), scratch_.end(), 0);
        for (std::size_t y = 0; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                const std::size_t i = y * w + x;
                if (s[i] <= 0) continue;
                // Middlemen carry stock to every dearer neighbour, more the
                // bigger the gap after their cost and the better the edge
                // conducts (cheaper and faster on water, slower in mountains).
                std::size_t to[4];
                std::int64_t share[4]; // millionths of this node's stock
                int n = 0;
                std::int64_t total = 0;
                const auto consider = [&](std::size_t j, std::int64_t wt) {
                    if (wt <= 0) return;
                    const std::int64_t gap = p[j] - p[i] - cost;
                    if (gap <= 0) return;
                    to[n] = j;
                    share[n] = balance_.middleman_permille * gap * wt / base;
                    total += share[n++];
                };
                if (y > 0) consider(i - w, south_w_[i - w]);
                if (y + 1 < h) consider(i + w, south_w_[i]);
                if (x > 0) consider(i - 1, east_w_[i - 1]);
                if (x + 1 < w) consider(i + 1, east_w_[i]);
                if (n == 0) continue;
                const std::int64_t scale = std::max<std::int64_t>(total, 1'000'000); // never more than all of it
                for (int k = 0; k < n; ++k) {
                    const auto move = static_cast<std::int32_t>(std::int64_t{s[i]} * share[k] / scale);
                    scratch_[i] -= move;
                    scratch_[to[k]] += move;
                }
            }
        }
        for (std::size_t i = 0; i < s.size(); ++i) {
            s[i] = std::min(balance_.max_stock_milli * kMicroPerMilli, s[i] + scratch_[i]);
        }
    }
}

void Economy::step_day(const CargoRegistry& cargo, const IndustryRegistry& industries, std::int32_t year) {
    run_sites(cargo, industries, year);
    spoil(cargo);
    relax(cargo);
    drift(cargo);
}

void Economy::settle(const CargoRegistry& cargo, const IndustryRegistry& industries, std::int32_t year, int days) {
    for (int i = 0; i < days; ++i) step_day(cargo, industries, year);
}

// --- Industry accounts ---------------------------------------------------------

bool ownable(IndustryKind k) {
    return k == IndustryKind::Raw || k == IndustryKind::Processor || k == IndustryKind::Warehouse;
}
bool buildable(IndustryKind k) { return k == IndustryKind::Processor || k == IndustryKind::Warehouse; }

std::vector<IndustryAccounts> Economy::close_accounts(const CargoRegistry& cargo, const IndustryRegistry& industries,
                                                      const Balance::Industries& b, std::int32_t months) {
    std::vector<IndustryAccounts> out(sites_.size());
    for (Site& s : sites_) {
        const IndustryType& t = industries.get(s.type);
        if (!ownable(t.kind)) continue;
        IndustryAccounts& a = out[s.id];
        if (t.kind == IndustryKind::Warehouse) {
            // Its income is the spoilage it saves; its only cost, overhead.
            if (!s.closed) {
                a.revenue = s.spoilage_saved;
                a.costs = Money::dollars(b.overhead_per_level * s.level).scaled(months, 12);
            }
            s.spoilage_saved = Money{};
        } else if (!s.closed) {
            for (std::size_t o = 0; o < t.outputs.size(); ++o) {
                a.revenue += cargo.get(t.outputs[o]).base_price.scaled(s.made_milli[o], kMilli);
            }
            for (std::size_t i = 0; i < t.inputs.size(); ++i) {
                a.costs += cargo.get(t.inputs[i].cargo).base_price.scaled(s.used_milli[i], kMilli);
            }
            a.costs += a.revenue.scaled(b.labour_percent, 100);
            a.costs += Money::dollars(b.overhead_per_level * s.level).scaled(months, 12);
        }
        const std::int64_t capacity = t.rate_milli * s.level * months / 12;
        s.utilisation_permille =
            !s.made_milli.empty() && capacity > 0
                ? static_cast<std::int32_t>(std::min<std::int64_t>(2000, s.made_milli.front() * 1000 / capacity))
                : 0;
        std::fill(s.made_milli.begin(), s.made_milli.end(), 0);
        std::fill(s.used_milli.begin(), s.used_milli.end(), 0);
        // Spread the period over its months, so the history is always monthly.
        for (std::int32_t m = 0; m < months; ++m) s.monthly_profit.push_back(a.profit().scaled(1, months));
        if (s.monthly_profit.size() > 12) {
            s.monthly_profit.erase(s.monthly_profit.begin(),
                                   s.monthly_profit.end() - 12);
        }
    }
    return out;
}

const char* port_mode_name(PortMode m) {
    switch (m) {
    case PortMode::Exchange: return "imports and exports";
    case PortMode::Receive: return "exports";
    case PortMode::Supply: return "imports";
    }
    return "?";
}

Economy::YearEnd Economy::close_year(const IndustryRegistry& industries, const Balance::Industries& b, Random& rng) {
    YearEnd out;
    for (Site& s : sites_) {
        const IndustryType& t = industries.get(s.type);
        if (s.closed) continue;
        if ((t.kind == IndustryKind::Sink || t.kind == IndustryKind::Port) && !s.owner) {
            const std::int64_t capacity = t.rate_milli * s.level;
            if (s.level < b.receiver_max_level && s.received_year_milli * 1000 >= capacity * b.receiver_upgrade_permille) {
                s.level *= 2;
                out.upgraded.push_back(s.id);
            }
        }
        s.received_year_milli = 0;
        if (!ownable(t.kind)) continue;
        s.loss_years = annual_profit(s) < Money{} ? s.loss_years + 1 : 0;
        if (!s.owner && s.loss_years >= b.close_after_loss_years &&
            rng.chance(static_cast<std::uint32_t>(b.close_chance_percent), 100)) {
            s.closed = true;
            out.closed.push_back(s.id);
        }
    }
    return out;
}

std::int32_t town_stars(std::int64_t houses, const Balance::Towns& b) {
    std::int32_t stars = 1;
    for (const std::int32_t threshold : b.star_houses) stars += houses >= threshold;
    return stars;
}

std::int64_t Economy::town_houses(std::size_t t) const {
    std::int64_t n = 0;
    for (const SiteId s : towns_.at(t).houses) n += sites_[s].level;
    return n;
}

Money annual_profit(const Site& s) {
    if (s.monthly_profit.empty()) return Money{};
    Money sum;
    for (const Money& m : s.monthly_profit) sum += m;
    return sum.scaled(12, static_cast<std::int64_t>(s.monthly_profit.size()));
}

Money industry_price(const Site& s, const Balance::Industries& b) {
    const Money floor = Money::dollars(b.floor_price * s.level);
    return std::max(floor, annual_profit(s) * b.profit_multiple);
}

Money industry_build_cost(const Balance::Industries& b) {
    return Money::dollars(b.floor_price).scaled(b.build_cost_percent, 100);
}

Money industry_upgrade_cost(const Site& s, const Balance::Industries& b) {
    return (industry_build_cost(b) * s.level).scaled(b.upgrade_cost_percent, 100);
}

// --- Map population ----------------------------------------------------------

namespace {

// Can this industry run in `year`: are its products (or, for consumers, its
// inputs) in use yet?
bool industry_eligible(const IndustryType& t, const CargoRegistry& cargo, std::int32_t year) {
    const auto avail = [&](CargoId c) { return cargo_available(c, cargo, year); };
    const auto in_avail = [&](const IndustryInput& in) { return input_active(in, cargo, year); };
    switch (t.kind) {
    case IndustryKind::Raw: return std::any_of(t.outputs.begin(), t.outputs.end(), avail);
    case IndustryKind::Processor:
        return std::any_of(t.outputs.begin(), t.outputs.end(), avail) &&
               std::any_of(t.inputs.begin(), t.inputs.end(), in_avail);
    case IndustryKind::Sink: return std::any_of(t.inputs.begin(), t.inputs.end(), in_avail);
    case IndustryKind::House:
    case IndustryKind::Port:      // placed on the coast
    case IndustryKind::Warehouse: // built by companies
        return false;
    }
    return false;
}

// How many of each a 128 x 128 map usually has.
std::int32_t usual_count(const IndustryType& t, const Balance::MapGeneration& mg) {
    switch (t.kind) {
    case IndustryKind::Raw: return mg.raw_per_type;
    case IndustryKind::Processor: return mg.processors_per_type;
    case IndustryKind::Sink: return mg.sinks_per_type;
    default: return 0;
    }
}

// Raw producers go anywhere on open land; processors and consumers go near
// towns, where their customers are. `taken` marks occupied cells.
std::optional<SiteId> place_site(Economy& economy, const IndustryRegistry& industries, IndustryTypeId type,
                                 Random& rng, std::vector<bool>& taken, const Balance::MapGeneration& mg) {
    const std::int32_t w = economy.width(), h = economy.height();
    const bool near_town = industries.get(type).kind != IndustryKind::Raw;
    const std::int32_t near = std::max(1, mg.industry_near_town_cells / economy.cells_per_node());
    for (int attempt = 0; attempt < 300; ++attempt) {
        std::int32_t x, y;
        if (near_town && !economy.towns().empty()) {
            const Town& t = economy.towns()[rng.below(static_cast<std::uint32_t>(economy.towns().size()))];
            x = t.cx + rng.between(-near, near);
            y = t.cy + rng.between(-near, near);
        } else {
            x = rng.between(0, w - 1);
            y = rng.between(0, h - 1);
        }
        if (x < 0 || y < 0 || x >= w || y >= h || economy.water(x, y)) continue;
        const auto i = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
        if (taken[i]) continue;
        taken[i] = true;
        return economy.add_site(industries, type, x, y);
    }
    return std::nullopt;
}

} // namespace

std::vector<SiteId> spawn_industries(Economy& economy, const CargoRegistry& cargo,
                                     const IndustryRegistry& industries, Random& rng, std::int32_t year,
                                     const Balance& b) {
    const std::int32_t w = economy.width(), h = economy.height();
    std::vector<bool> taken(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), false);
    for (const Site& s : economy.sites()) {
        if (!s.closed) taken[static_cast<std::size_t>(s.cy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(s.cx)] = true;
    }
    const std::int64_t area = economy.area_permille();
    std::vector<SiteId> opened;
    for (const IndustryType& t : industries.all()) {
        if (!industry_eligible(t, cargo, year)) continue;
        std::int32_t open = 0;
        for (const Site& s : economy.sites()) open += s.type == t.id && !s.closed;
        const auto usual = static_cast<std::int32_t>(std::max<std::int64_t>(1, usual_count(t, b.map) * area / 1000));
        // Below the usual number: one more (new types, and replacing closures).
        // Otherwise a chance of one more, better in good times, up to a cap [I].
        const bool short_of = open < usual;
        const auto chance = static_cast<std::uint32_t>(
            std::max<std::int64_t>(0, std::int64_t{b.map.appear_chance_percent} * economy.activity_percent() / 100));
        const bool extra = open < usual * b.map.max_count_multiple && rng.chance(chance, 100);
        if (!short_of && !extra) continue;
        if (const auto id = place_site(economy, industries, t.id, rng, taken, b.map)) opened.push_back(*id);
    }
    return opened;
}



void populate_economy(Economy& economy, const CargoRegistry& cargo,
                      const IndustryRegistry& industries, Random& rng, std::int32_t year, const Balance& b) {
    const Balance::MapGeneration& mg = b.map;
    const std::int32_t w = economy.width(), h = economy.height();
    const auto land = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && x < w && y < h && !economy.water(x, y);
    };
    std::vector<bool> taken(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), false);
    const auto idx = [&](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    // Counts are per 128 km x 128 km of map, distances in map cells.
    const std::int64_t area = economy.area_permille();
    const auto scaled = [&](std::int32_t per_map) {
        return static_cast<std::int32_t>(std::max<std::int64_t>(1, per_map * area / 1000));
    };
    const std::int32_t k = economy.cells_per_node();
    const std::int32_t spread = mg.town_spread_cells / k; // nodes either side of the centre
    const std::int32_t side = 2 * spread + 1;

    // Towns.
    const std::optional<IndustryTypeId> house = industries.find("house");
    const std::int32_t town_count = scaled(mg.towns_per_map);
    std::vector<std::string> used_names;
    for (std::int32_t t = 0; t < town_count; ++t) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            const std::int32_t cx = rng.between(3, std::max(3, w - 4)), cy = rng.between(3, std::max(3, h - 4));
            if (!land(cx, cy) || taken[idx(cx, cy)]) continue;
            const bool crowded = std::any_of(economy.towns().begin(), economy.towns().end(), [&](const Town& o) {
                return (std::abs(o.cx - cx) + std::abs(o.cy - cy)) * k < mg.town_spacing_cells;
            });
            if (crowded && attempt < 150) continue;
            std::string name = town_name(rng);
            while (std::find(used_names.begin(), used_names.end(), name) != used_names.end()) name = town_name(rng);
            used_names.push_back(name);
            economy.add_town({name, cx, cy});

            if (house) {
                // Houses scattered over the nodes around the centre.
                std::vector<std::int32_t> houses(static_cast<std::size_t>(side * side), 0);
                const auto slot = [&](std::int32_t dx, std::int32_t dy) {
                    return static_cast<std::size_t>((dy + spread) * side + dx + spread);
                };
                const std::int32_t count = rng.between(mg.town_min_houses, mg.town_max_houses);
                for (std::int32_t i = 0; i < count; ++i) {
                    const std::int32_t dx = rng.between(-spread, spread), dy = rng.between(-spread, spread);
                    if (land(cx + dx, cy + dy)) ++houses[slot(dx, dy)];
                }
                for (std::int32_t dy = -spread; dy <= spread; ++dy) {
                    for (std::int32_t dx = -spread; dx <= spread; ++dx) {
                        const std::int32_t n = houses[slot(dx, dy)];
                        if (n == 0) continue;
                        const SiteId site = economy.add_site(industries, *house, cx + dx, cy + dy, n);
                        economy.town_mut(economy.towns().size() - 1).houses.push_back(site);
                        taken[idx(cx + dx, cy + dy)] = true;
                    }
                }
            }
            break;
        }
    }

    // Industries, in the map's usual numbers.
    for (const IndustryType& t : industries.all()) {
        if (!industry_eligible(t, cargo, year)) continue;
        for (std::int32_t i = 0; i < scaled(usual_count(t, mg)); ++i) {
            place_site(economy, industries, t.id, rng, taken, mg);
        }
    }

    // Ports, on the coast; failing that, at the map edge [C].
    const std::optional<IndustryTypeId> port = industries.find("port");
    if (!port) return;
    std::vector<std::pair<std::int32_t, std::int32_t>> coast, edge;
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            if (!land(x, y) || taken[idx(x, y)]) continue;
            const bool wet = (x > 0 && !land(x - 1, y)) || (x + 1 < w && !land(x + 1, y)) || (y > 0 && !land(x, y - 1)) ||
                             (y + 1 < h && !land(x, y + 1));
            if (wet) coast.emplace_back(x, y);
            else if (x == 0 || y == 0 || x == w - 1 || y == h - 1) edge.emplace_back(x, y);
        }
    }
    for (std::int32_t i = 0; i < scaled(mg.ports); ++i) {
        auto& pool = !coast.empty() ? coast : edge;
        if (pool.empty()) return;
        const std::size_t pick = rng.below(static_cast<std::uint32_t>(pool.size()));
        const auto [x, y] = pool[pick];
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(pick));
        if (taken[idx(x, y)]) continue;
        const SiteId id = economy.add_site(industries, *port, x, y);
        economy.site_mut(id).port_mode = static_cast<PortMode>(rng.below(3));
        taken[idx(x, y)] = true;
    }
}

} // namespace railmaster::sim
