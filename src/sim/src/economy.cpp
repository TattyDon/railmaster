#include "railmaster/sim/economy.hpp"

#include "railmaster/sim/random.hpp"
#include "railmaster/sim/terrain.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
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

// Price at a consumer: high when it is starved, falling as unconsumed stock
// piles up around it.
std::int32_t demand_price(const CargoType& c, std::int64_t daily_milli, std::int32_t days, std::int64_t leftover,
                          std::int32_t demand_percent) {
    const std::int64_t high = base_dollars(c) * demand_percent / 100;
    const std::int64_t s = std::max<std::int64_t>(1, daily_milli * days);
    return static_cast<std::int32_t>(high * s / (s + leftover));
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
        t.rate_per_year = entry.at("rate").get<std::int32_t>();
        if (t.rate_per_year <= 0) throw std::runtime_error("industry data: bad rate for '" + t.key + "'");
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
                 const CargoRegistry& cargo, const Balance& balance)
    : width_(width_cells), height_(height_cells), cell_size_mm_(cell_size_mm), balance_(balance.economy) {
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
    east_w_.assign(cells, 1000);
    south_w_.assign(cells, 1000);
    for (std::int32_t y = 0; y < height_; ++y) east_w_[cell(width_ - 1, y)] = 0;
    for (std::int32_t x = 0; x < width_; ++x) south_w_[cell(x, height_ - 1)] = 0;
}

void Economy::set_terrain(const Terrain& terrain) {
    if (terrain.width() != width_ || terrain.height() != height_) {
        throw std::invalid_argument("terrain and economy grids differ in size");
    }
    const auto water = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && x < width_ && y < height_ && terrain.ground(x, y) == GroundType::Water;
    };
    for (std::int32_t y = 0; y < height_; ++y) {
        for (std::int32_t x = 0; x < width_; ++x) {
            std::int32_t c = 1000;
            if (water(x, y)) {
                c = balance_.water_conductance_permille;
            } else {
                const std::int32_t hs[] = {terrain.corner_height(x, y), terrain.corner_height(x + 1, y),
                                           terrain.corner_height(x, y + 1), terrain.corner_height(x + 1, y + 1)};
                const std::int64_t relief = *std::max_element(std::begin(hs), std::end(hs)) -
                                            *std::min_element(std::begin(hs), std::end(hs));
                const std::int64_t grade_bp = relief * 10000 / terrain.tile_size_m();
                if (grade_bp >= balance_.mountain_grade_bp) c = balance_.mountain_conductance_permille;
                else if (grade_bp >= balance_.hill_grade_bp) c = balance_.hill_conductance_permille;
                else if (water(x - 1, y) || water(x + 1, y) || water(x, y - 1) || water(x, y + 1))
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
    s = std::min(balance_.max_stock_milli, s + milli);
}

std::int32_t Economy::take_stock(CargoId c, std::int32_t cx, std::int32_t cy, std::int32_t milli) {
    std::int32_t& s = stock_[c][cell(cx, cy)];
    const std::int32_t taken = std::clamp(milli, 0, s);
    s -= taken;
    return taken;
}

void Economy::run_sites(const CargoRegistry& cargo, const IndustryRegistry& industries, std::int32_t year) {
    for (auto& a : anchors_) a.clear();
    auto supply = [&](CargoId c, std::size_t at) {
        anchors_[c].push_back({at, percent_of(base_dollars(cargo.get(c)), balance_.supply_price_percent)});
    };
    auto demand = [&](CargoId c, std::size_t at, std::int64_t daily, std::int32_t days) {
        anchors_[c].push_back({at, demand_price(cargo.get(c), daily, days, stock_[c][at], balance_.demand_price_percent)});
    };

    for (Site& s : sites_) {
        if (s.closed) continue;
        const IndustryType& t = industries.get(s.type);
        const std::size_t at = cell(s.cx, s.cy);
        const auto daily = static_cast<std::int32_t>(std::int64_t{t.rate_per_year} * s.level * kMilli *
                                                     activity_percent_ / (365 * 100));

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
                demand(in.cargo, at, daily, balance_.industry_saturation_days);
            }
            const std::int32_t out = daily * (100 + (boosted ? balance_.boost_percent : 0)) / 100;
            for (std::size_t o = 0; o < t.outputs.size(); ++o) {
                const CargoId c = t.outputs[o];
                if (!cargo_available(c, cargo, year)) continue;
                add_stock(c, s.cx, s.cy, out);
                s.produced_milli += out;
                s.made_milli[o] += out;
                supply(c, at);
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
                demand(in.cargo, at, daily, balance_.industry_saturation_days);
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
                supply(c, at);
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
                    demand(in.cargo, at, daily, balance_.industry_saturation_days);
                }
            }
            if (s.port_mode != PortMode::Receive) {
                for (std::size_t o = 0; o < t.outputs.size(); ++o) {
                    const CargoId c = t.outputs[o];
                    if (!cargo_available(c, cargo, year)) continue;
                    add_stock(c, s.cx, s.cy, daily);
                    s.produced_milli += daily;
                    s.made_milli[o] += daily;
                    supply(c, at);
                }
            }
            break;
        }
        case IndustryKind::Sink:
        case IndustryKind::House: {
            const std::int32_t days = t.kind == IndustryKind::House ? balance_.saturation_days
                                                                    : balance_.industry_saturation_days;
            for (const IndustryInput& in : t.inputs) {
                if (!input_active(in, cargo, year)) continue;
                const std::int32_t took = take_stock(in.cargo, s.cx, s.cy, daily);
                if (t.kind == IndustryKind::Sink) s.received_year_milli += took;
                demand(in.cargo, at, daily, days);
            }
            for (CargoId c : t.outputs) {
                if (!cargo_available(c, cargo, year)) continue;
                add_stock(c, s.cx, s.cy, daily);
                s.produced_milli += daily;
                supply(c, at);
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
        const std::int32_t r = warehouse_radius_;
        for (std::int32_t y = std::max(0, w.cy - r); y <= std::min(height_ - 1, w.cy + r); ++y) {
            for (std::int32_t x = std::max(0, w.cx - r); x <= std::min(width_ - 1, w.cx + r); ++x) {
                std::int64_t& c = cover[cell(x, y)];
                if (c < 0) c = w.id;
            }
        }
    }
    for (const CargoType& c : cargo.all()) {
        const std::int64_t per_mille = std::int64_t{c.decay_sensitivity} * balance_.spoilage_per_mille_per_sensitivity;
        std::vector<std::int32_t>& stock = stock_[c.id];
        for (std::size_t i = 0; i < stock.size(); ++i) {
            std::int32_t& s = stock[i];
            if (s <= 0) continue;
            const auto loss = static_cast<std::int32_t>(std::max<std::int64_t>(1, s * per_mille / 1000));
            if (cover.empty() || cover[i] < 0) {
                s -= loss;
                continue;
            }
            const std::int32_t kept = loss - loss * warehouse_spoilage_percent_ / 100;
            s -= loss - kept;
            sites_[static_cast<std::size_t>(cover[i])].spoilage_saved += c.base_price.scaled(kept, kMilli);
        }
    }
}

void Economy::relax(const CargoRegistry& cargo) {
    const auto w = static_cast<std::size_t>(width_);
    const auto h = static_cast<std::size_t>(height_);
    for (const CargoType& c : cargo.all()) {
        if (!active_[c.id]) continue;
        std::vector<std::int32_t>& p = price_[c.id];
        const std::int64_t neutral = percent_of(base_dollars(c), balance_.neutral_price_percent);
        for (std::size_t y = 0; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                const std::size_t i = y * w + x;
                // Neighbours weighted by how well the edge to them conducts.
                // Edges reflect: a missing neighbour counts as this cell.
                std::int64_t weight = 0, sum = 0;
                const auto add = [&](std::int64_t wt, std::int32_t price) {
                    weight += wt;
                    sum += wt * price;
                };
                const std::int64_t self = conductance_[i];
                if (x > 0) add(east_w_[i - 1], p[i - 1]); else add(self, p[i]);
                if (x + 1 < w) add(east_w_[i], p[i + 1]); else add(self, p[i]);
                if (y > 0) add(south_w_[i - w], p[i - w]); else add(self, p[i]);
                if (y + 1 < h) add(south_w_[i], p[i + w]); else add(self, p[i]);
                // (weighted mean of neighbours + lambda * neutral) / (1 + lambda),
                // with lambda scaled to four flat edges; on flat land this is
                // the plain screened average.
                const std::int64_t screen = std::int64_t{4000} * balance_.screening_per_10000;
                scratch_[i] = static_cast<std::int32_t>((sum * 10000 + screen * neutral) / (weight * 10000 + screen));
            }
        }
        for (const Anchor& a : anchors_[c.id]) scratch_[a.cell] = a.price;
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
        const std::int32_t threshold = percent_of(base_dollars(c), balance_.transport_cost_percent);
        std::fill(scratch_.begin(), scratch_.end(), 0);
        for (std::size_t y = 0; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                const std::size_t i = y * w + x;
                if (s[i] <= 0) continue;
                // The neighbour that pays best after the middleman's cost,
                // which is cheaper where the edge conducts better. Checked in
                // a fixed order so ties are deterministic.
                std::size_t best = i;
                std::int64_t best_net = p[i];
                std::int64_t best_w = 0;
                const auto consider = [&](std::size_t j, std::int64_t wt) {
                    const std::int64_t net = p[j] - std::int64_t{threshold} * 1000 / std::max<std::int64_t>(1, wt);
                    if (net > best_net) {
                        best = j;
                        best_net = net;
                        best_w = wt;
                    }
                };
                if (y > 0) consider(i - w, south_w_[i - w]);
                if (y + 1 < h) consider(i + w, south_w_[i]);
                if (x > 0) consider(i - 1, east_w_[i - 1]);
                if (x + 1 < w) consider(i + 1, east_w_[i]);
                if (best == i) continue;
                // And faster: the share moved scales with conductance.
                const auto move = static_cast<std::int32_t>(std::clamp<std::int64_t>(
                    std::int64_t{s[i]} * balance_.drift_percent_per_day * best_w / (100 * 1000), 1, s[i]));
                scratch_[i] -= move;
                scratch_[best] += move;
            }
        }
        for (std::size_t i = 0; i < s.size(); ++i) {
            s[i] = std::min(balance_.max_stock_milli, s[i] + scratch_[i]);
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
        const std::int64_t capacity = std::int64_t{t.rate_per_year} * s.level * kMilli * months / 12;
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
            const std::int64_t capacity = std::int64_t{t.rate_per_year} * s.level * kMilli;
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
std::optional<SiteId> place_site(Economy& economy, const Terrain& terrain, const IndustryRegistry& industries,
                                 IndustryTypeId type, Random& rng, std::vector<bool>& taken) {
    const std::int32_t w = economy.width(), h = economy.height();
    const bool near_town = industries.get(type).kind != IndustryKind::Raw;
    for (int attempt = 0; attempt < 300; ++attempt) {
        std::int32_t x, y;
        if (near_town && !economy.towns().empty()) {
            const Town& t = economy.towns()[rng.below(static_cast<std::uint32_t>(economy.towns().size()))];
            x = t.cx + rng.between(-6, 6);
            y = t.cy + rng.between(-6, 6);
        } else {
            x = rng.between(0, w - 1);
            y = rng.between(0, h - 1);
        }
        if (x < 0 || y < 0 || x >= w || y >= h || terrain.ground(x, y) == GroundType::Water) continue;
        const auto i = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
        if (taken[i]) continue;
        taken[i] = true;
        return economy.add_site(industries, type, x, y);
    }
    return std::nullopt;
}

} // namespace

std::vector<SiteId> spawn_industries(Economy& economy, const Terrain& terrain, const CargoRegistry& cargo,
                                     const IndustryRegistry& industries, Random& rng, std::int32_t year,
                                     const Balance& b) {
    const std::int32_t w = economy.width(), h = economy.height();
    std::vector<bool> taken(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), false);
    for (const Site& s : economy.sites()) {
        if (!s.closed) taken[static_cast<std::size_t>(s.cy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(s.cx)] = true;
    }
    const std::int32_t area_scale = std::max(1, w * h / 16384);
    std::vector<SiteId> opened;
    for (const IndustryType& t : industries.all()) {
        if (!industry_eligible(t, cargo, year)) continue;
        std::int32_t open = 0;
        for (const Site& s : economy.sites()) open += s.type == t.id && !s.closed;
        const std::int32_t usual = usual_count(t, b.map) * area_scale;
        // Below the usual number: one more (new types, and replacing closures).
        // Otherwise a chance of one more, better in good times, up to a cap [I].
        const bool short_of = open < usual;
        const auto chance = static_cast<std::uint32_t>(
            std::max<std::int64_t>(0, std::int64_t{b.map.appear_chance_percent} * economy.activity_percent() / 100));
        const bool extra = open < usual * b.map.max_count_multiple && rng.chance(chance, 100);
        if (!short_of && !extra) continue;
        if (const auto id = place_site(economy, terrain, industries, t.id, rng, taken)) opened.push_back(*id);
    }
    return opened;
}



void populate_economy(Economy& economy, const Terrain& terrain, const CargoRegistry& cargo,
                      const IndustryRegistry& industries, Random& rng, std::int32_t year, const Balance& b) {
    const Balance::MapGeneration& mg = b.map;
    const std::int32_t w = economy.width(), h = economy.height();
    const auto land = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && x < w && y < h && terrain.ground(x, y) != GroundType::Water;
    };
    std::vector<bool> taken(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), false);
    const auto idx = [&](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    const std::int32_t area_scale = std::max(1, w * h / 16384); // 1 for a 128 x 128 map

    // Towns.
    const std::optional<IndustryTypeId> house = industries.find("house");
    const std::int32_t town_count = std::max(1, w * h / mg.cells_per_town);
    std::vector<std::string> used_names;
    for (std::int32_t t = 0; t < town_count; ++t) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            const std::int32_t cx = rng.between(3, std::max(3, w - 4)), cy = rng.between(3, std::max(3, h - 4));
            if (!land(cx, cy) || taken[idx(cx, cy)]) continue;
            const bool crowded = std::any_of(economy.towns().begin(), economy.towns().end(), [&](const Town& o) {
                return std::abs(o.cx - cx) + std::abs(o.cy - cy) < mg.town_spacing_cells;
            });
            if (crowded && attempt < 150) continue;
            std::string name = town_name(rng);
            while (std::find(used_names.begin(), used_names.end(), name) != used_names.end()) name = town_name(rng);
            used_names.push_back(name);
            economy.add_town({name, cx, cy});

            if (house) {
                std::vector<std::int32_t> houses(25, 0); // 5 x 5 around the centre
                const std::int32_t count = rng.between(mg.town_min_houses, mg.town_max_houses);
                for (std::int32_t i = 0; i < count; ++i) {
                    const std::int32_t dx = rng.between(-2, 2), dy = rng.between(-2, 2);
                    if (land(cx + dx, cy + dy)) ++houses[static_cast<std::size_t>((dy + 2) * 5 + dx + 2)];
                }
                for (std::int32_t dy = -2; dy <= 2; ++dy) {
                    for (std::int32_t dx = -2; dx <= 2; ++dx) {
                        const std::int32_t n = houses[static_cast<std::size_t>((dy + 2) * 5 + dx + 2)];
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
        for (std::int32_t i = 0; i < usual_count(t, mg) * area_scale; ++i) {
            place_site(economy, terrain, industries, t.id, rng, taken);
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
    for (std::int32_t i = 0; i < mg.ports * area_scale; ++i) {
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
