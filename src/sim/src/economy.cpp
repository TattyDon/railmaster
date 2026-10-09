#include "railmaster/sim/economy.hpp"

#include "railmaster/sim/random.hpp"
#include "railmaster/sim/terrain.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace railmaster::sim {

namespace {

constexpr std::int64_t kScreening = 25; // per ten thousand: lambda = 0.0025, so pull fades over ~10 cells

IndustryKind parse_kind(const std::string& s, const std::string& key) {
    if (s == "raw") return IndustryKind::Raw;
    if (s == "processor") return IndustryKind::Processor;
    if (s == "sink") return IndustryKind::Sink;
    if (s == "house") return IndustryKind::House;
    throw std::runtime_error("industry data: unknown kind '" + s + "' for '" + key + "'");
}

CargoId resolve(const CargoRegistry& cargo, const std::string& key, const std::string& industry) {
    const auto id = cargo.find(key);
    if (!id) throw std::runtime_error("industry data: unknown cargo '" + key + "' in '" + industry + "'");
    return *id;
}

std::int64_t base_dollars(const CargoType& c) { return c.base_price.whole_dollars(); }

std::int32_t percent_of(std::int64_t base, std::int32_t pct) { return static_cast<std::int32_t>(base * pct / 100); }

bool input_active(const IndustryInput& in, const CargoRegistry& cargo, std::int32_t year) {
    return year >= cargo.get(in.cargo).available_year && (!in.until_year || year <= *in.until_year);
}

bool cargo_available(CargoId c, const CargoRegistry& cargo, std::int32_t year) {
    return year >= cargo.get(c).available_year;
}

// Price at a consumer: high when it is starved, falling as unconsumed stock
// piles up around it.
std::int32_t demand_price(const CargoType& c, std::int64_t daily_milli, std::int32_t days, std::int64_t leftover) {
    const std::int64_t high = base_dollars(c) * provisional::kDemandPricePercent / 100;
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
                 const CargoRegistry& cargo)
    : width_(width_cells), height_(height_cells), cell_size_mm_(cell_size_mm) {
    if (width_ <= 0 || height_ <= 0 || cell_size_mm_ <= 0) throw std::invalid_argument("bad economy grid size");
    const auto cells = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    const std::size_t n = cargo.all().size();
    price_.resize(n);
    stock_.assign(n, std::vector<std::int32_t>(cells, 0));
    anchors_.resize(n);
    active_.assign(n, false);
    for (const CargoType& c : cargo.all()) {
        price_[c.id].assign(cells, percent_of(base_dollars(c), provisional::kNeutralPricePercent));
    }
    scratch_.resize(cells);
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
    s.cx = cx;
    s.cy = cy;
    s.level = level;
    s.buffer.assign(industries.get(type).inputs.size(), 0);
    sites_.push_back(std::move(s));
    return sites_.back().id;
}

void Economy::add_stock(CargoId c, std::int32_t cx, std::int32_t cy, std::int32_t milli) {
    std::int32_t& s = stock_[c][cell(cx, cy)];
    s = std::min(provisional::kMaxStockMilli, s + milli);
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
        anchors_[c].push_back({at, percent_of(base_dollars(cargo.get(c)), provisional::kSupplyPricePercent)});
    };
    auto demand = [&](CargoId c, std::size_t at, std::int64_t daily, std::int32_t days) {
        anchors_[c].push_back({at, demand_price(cargo.get(c), daily, days, stock_[c][at])});
    };

    for (Site& s : sites_) {
        const IndustryType& t = industries.get(s.type);
        const std::size_t at = cell(s.cx, s.cy);
        const auto daily = static_cast<std::int32_t>(std::int64_t{t.rate_per_year} * s.level * kMilli / 365);

        switch (t.kind) {
        case IndustryKind::Raw: {
            bool boosted = false;
            for (const IndustryInput& in : t.inputs) {
                if (!input_active(in, cargo, year)) continue;
                const std::int32_t want = daily / 2;
                if (take_stock(in.cargo, s.cx, s.cy, want) >= want && want > 0) boosted = true;
                demand(in.cargo, at, daily, provisional::kIndustrySaturationDays);
            }
            const std::int32_t out = daily * (100 + (boosted ? provisional::kBoostPercent : 0)) / 100;
            for (CargoId c : t.outputs) {
                if (!cargo_available(c, cargo, year)) continue;
                add_stock(c, s.cx, s.cy, out);
                s.produced_milli += out;
                supply(c, at);
            }
            break;
        }
        case IndustryKind::Processor: {
            const std::int32_t cap = daily * provisional::kInputBufferDays;
            std::int32_t can_make = t.rule == InputRule::All ? daily : 0;
            bool any_input = false;
            for (std::size_t i = 0; i < t.inputs.size(); ++i) {
                const IndustryInput& in = t.inputs[i];
                if (!input_active(in, cargo, year)) continue;
                any_input = true;
                s.buffer[i] += take_stock(in.cargo, s.cx, s.cy, cap - s.buffer[i]);
                demand(in.cargo, at, daily, provisional::kIndustrySaturationDays);
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
                } else {
                    const std::int32_t use = std::min(owed, s.buffer[i]);
                    s.buffer[i] -= use;
                    owed -= use;
                }
            }
            for (CargoId c : t.outputs) {
                if (!cargo_available(c, cargo, year)) continue;
                add_stock(c, s.cx, s.cy, made);
                supply(c, at);
            }
            s.produced_milli += made;
            break;
        }
        case IndustryKind::Sink:
        case IndustryKind::House: {
            const std::int32_t days = t.kind == IndustryKind::House ? provisional::kSaturationDays
                                                                    : provisional::kIndustrySaturationDays;
            for (const IndustryInput& in : t.inputs) {
                if (!input_active(in, cargo, year)) continue;
                take_stock(in.cargo, s.cx, s.cy, daily);
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
    for (const CargoType& c : cargo.all()) {
        const std::int64_t per_mille = std::int64_t{c.decay_sensitivity} * provisional::kSpoilagePerMillePerSensitivity;
        for (std::int32_t& s : stock_[c.id]) {
            if (s > 0) s -= static_cast<std::int32_t>(std::max<std::int64_t>(1, s * per_mille / 1000));
        }
    }
}

void Economy::relax(const CargoRegistry& cargo) {
    const auto w = static_cast<std::size_t>(width_);
    const auto h = static_cast<std::size_t>(height_);
    for (const CargoType& c : cargo.all()) {
        if (!active_[c.id]) continue;
        std::vector<std::int32_t>& p = price_[c.id];
        const std::int64_t neutral = percent_of(base_dollars(c), provisional::kNeutralPricePercent);
        for (std::size_t y = 0; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                const std::size_t i = y * w + x;
                // Edges reflect: a missing neighbour counts as this cell.
                const std::int64_t sum = std::int64_t{x > 0 ? p[i - 1] : p[i]} + (x + 1 < w ? p[i + 1] : p[i]) +
                                         (y > 0 ? p[i - w] : p[i]) + (y + 1 < h ? p[i + w] : p[i]);
                // (mean of neighbours + lambda * neutral) / (1 + lambda), in integers.
                scratch_[i] = static_cast<std::int32_t>((sum * 10000 + 4 * kScreening * neutral) /
                                                        (4 * (10000 + kScreening)));
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
        const std::int32_t threshold = percent_of(base_dollars(c), provisional::kTransportCostPercent);
        std::fill(scratch_.begin(), scratch_.end(), 0);
        for (std::size_t y = 0; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                const std::size_t i = y * w + x;
                if (s[i] <= 0) continue;
                // The best neighbour, checked in a fixed order so ties are deterministic.
                std::size_t best = i;
                std::int32_t best_price = p[i] + threshold;
                const auto consider = [&](std::size_t j) {
                    if (p[j] > best_price) {
                        best = j;
                        best_price = p[j];
                    }
                };
                if (y > 0) consider(i - w);
                if (y + 1 < h) consider(i + w);
                if (x > 0) consider(i - 1);
                if (x + 1 < w) consider(i + 1);
                if (best == i) continue;
                const std::int32_t move = std::max(1, s[i] * provisional::kDriftPercentPerDay / 100);
                scratch_[i] -= move;
                scratch_[best] += move;
            }
        }
        for (std::size_t i = 0; i < s.size(); ++i) {
            s[i] = std::min(provisional::kMaxStockMilli, s[i] + scratch_[i]);
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

// --- Map population ----------------------------------------------------------

void populate_economy(Economy& economy, const Terrain& terrain, const CargoRegistry& cargo,
                      const IndustryRegistry& industries, Random& rng, std::int32_t year) {
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
    const std::int32_t town_count = std::max(1, w * h / 2048);
    std::vector<std::string> used_names;
    for (std::int32_t t = 0; t < town_count; ++t) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            const std::int32_t cx = rng.between(3, std::max(3, w - 4)), cy = rng.between(3, std::max(3, h - 4));
            if (!land(cx, cy) || taken[idx(cx, cy)]) continue;
            const bool crowded = std::any_of(economy.towns().begin(), economy.towns().end(), [&](const Town& o) {
                return std::abs(o.cx - cx) + std::abs(o.cy - cy) < 12;
            });
            if (crowded && attempt < 150) continue;
            std::string name = town_name(rng);
            while (std::find(used_names.begin(), used_names.end(), name) != used_names.end()) name = town_name(rng);
            used_names.push_back(name);
            economy.add_town({name, cx, cy});

            if (house) {
                std::vector<std::int32_t> houses(25, 0); // 5 x 5 around the centre
                const std::int32_t count = rng.between(10, 40);
                for (std::int32_t i = 0; i < count; ++i) {
                    const std::int32_t dx = rng.between(-2, 2), dy = rng.between(-2, 2);
                    if (land(cx + dx, cy + dy)) ++houses[static_cast<std::size_t>((dy + 2) * 5 + dx + 2)];
                }
                for (std::int32_t dy = -2; dy <= 2; ++dy) {
                    for (std::int32_t dx = -2; dx <= 2; ++dx) {
                        const std::int32_t n = houses[static_cast<std::size_t>((dy + 2) * 5 + dx + 2)];
                        if (n == 0) continue;
                        economy.add_site(industries, *house, cx + dx, cy + dy, n);
                        taken[idx(cx + dx, cy + dy)] = true;
                    }
                }
            }
            break;
        }
    }

    // Industries. Raw producers go anywhere on open land; processors and
    // sinks go near towns, where their customers are.
    const auto place = [&](IndustryTypeId type, bool near_town) {
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
            if (!land(x, y) || taken[idx(x, y)]) continue;
            economy.add_site(industries, type, x, y);
            taken[idx(x, y)] = true;
            return;
        }
    };
    for (const IndustryType& t : industries.all()) {
        const auto avail = [&](CargoId c) { return cargo_available(c, cargo, year); };
        const auto in_avail = [&](const IndustryInput& in) { return input_active(in, cargo, year); };
        bool eligible = false;
        std::int32_t count = 0;
        switch (t.kind) {
        case IndustryKind::Raw:
            eligible = std::any_of(t.outputs.begin(), t.outputs.end(), avail);
            count = 4;
            break;
        case IndustryKind::Processor:
            eligible = std::any_of(t.outputs.begin(), t.outputs.end(), avail) &&
                       std::any_of(t.inputs.begin(), t.inputs.end(), in_avail);
            count = 2;
            break;
        case IndustryKind::Sink:
            eligible = std::any_of(t.inputs.begin(), t.inputs.end(), in_avail);
            count = 2;
            break;
        case IndustryKind::House: break;
        }
        if (!eligible) continue;
        for (std::int32_t i = 0; i < count * area_scale; ++i) place(t.id, t.kind != IndustryKind::Raw);
    }
}

} // namespace railmaster::sim
