// Saving and loading a game. One function per type lists its members, and
// the same list both writes and reads them (`Ar` is a Writer or a Reader),
// so the two cannot drift apart. The format is binary, little-endian, with
// fixed-width integers, so a save made on one machine loads on another and
// carries on exactly as the original would have (the simulation is
// deterministic). Everything is saved, caches included, so a loaded game is
// bit-for-bit the game that was saved.

#include "railmaster/sim/world.hpp"

#include <array>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace railmaster::sim {

namespace {

constexpr char kMagic[8] = {'R', 'M', 'S', 'A', 'V', 'E', '\0', '\0'};
// Bump when the members saved change; older saves are then refused.
constexpr std::uint32_t kSaveVersion = 5; // 2: events, Grants; 3: restrictions; 4: breakdowns; 5: crossings

class Writer {
public:
    static constexpr bool loading = false;
    void put(std::uint64_t v, std::size_t bytes) {
        for (std::size_t i = 0; i < bytes; ++i) out_.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
    void put_bytes(const char* p, std::size_t n) { out_.append(p, n); }
    std::string take() { return std::move(out_); }

private:
    std::string out_;
};

class Reader {
public:
    static constexpr bool loading = true;
    explicit Reader(std::string_view in) : in_(in) {}
    std::uint64_t get(std::size_t bytes) {
        need(bytes);
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < bytes; ++i) v |= std::uint64_t{static_cast<unsigned char>(in_[pos_ + i])} << (8 * i);
        pos_ += bytes;
        return v;
    }
    std::string get_bytes(std::size_t n) {
        need(n);
        std::string s(in_.substr(pos_, n));
        pos_ += n;
        return s;
    }
    // A count read from the file, checked against what is left of it so a
    // damaged file cannot ask for a huge allocation.
    std::size_t count(std::size_t min_bytes_each) {
        const auto n = static_cast<std::size_t>(get(8));
        if (min_bytes_each > 0 && n > (in_.size() - pos_) / min_bytes_each) throw std::runtime_error("save: damaged file");
        return n;
    }
    bool done() const { return pos_ == in_.size(); }
    const Balance* balance = nullptr; // set once read, for objects built while loading

private:
    void need(std::size_t n) const {
        if (n > in_.size() - pos_) throw std::runtime_error("save: the file ends early");
    }
    std::string_view in_;
    std::size_t pos_ = 0;
};

template <class Ar, class T>
void io(Ar& ar, T& v);

template <class Ar, class... T>
void fields(Ar& ar, T&... v) {
    (io(ar, v), ...);
}

template <class>
inline constexpr bool kVector = false;
template <class T, class A>
inline constexpr bool kVector<std::vector<T, A>> = true;
template <class>
inline constexpr bool kOptional = false;
template <class T>
inline constexpr bool kOptional<std::optional<T>> = true;
template <class>
inline constexpr bool kPair = false;
template <class A, class B>
inline constexpr bool kPair<std::pair<A, B>> = true;
template <class>
inline constexpr bool kArray = false;
template <class T, std::size_t N>
inline constexpr bool kArray<std::array<T, N>> = true;
template <class>
inline constexpr bool kMap = false;
template <class K, class V, class C, class A>
inline constexpr bool kMap<std::map<K, V, C, A>> = true;

// Plain values and standard containers; the game's own types have their
// own overloads below, found by argument-dependent lookup.
template <class Ar, class T>
void io(Ar& ar, T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        if constexpr (Ar::loading) v = ar.get(1) != 0;
        else ar.put(v ? 1 : 0, 1);
    } else if constexpr (std::is_integral_v<T>) {
        if constexpr (Ar::loading) v = static_cast<T>(ar.get(sizeof(T)));
        else ar.put(static_cast<std::uint64_t>(v), sizeof(T));
    } else if constexpr (std::is_enum_v<T>) {
        auto u = static_cast<std::underlying_type_t<T>>(v);
        io(ar, u);
        if constexpr (Ar::loading) v = static_cast<T>(u);
    } else if constexpr (std::is_same_v<T, std::string>) {
        if constexpr (Ar::loading) {
            v = ar.get_bytes(ar.count(1));
        } else {
            ar.put(v.size(), 8);
            ar.put_bytes(v.data(), v.size());
        }
    } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
        if constexpr (Ar::loading) {
            v.assign(ar.count(1), false);
            for (std::size_t i = 0; i < v.size(); ++i) v[i] = ar.get(1) != 0;
        } else {
            ar.put(v.size(), 8);
            for (bool b : v) ar.put(b ? 1 : 0, 1);
        }
    } else if constexpr (kVector<T>) {
        if constexpr (Ar::loading) {
            v.clear();
            v.resize(ar.count(1));
        } else {
            ar.put(v.size(), 8);
        }
        for (auto& e : v) io(ar, e);
    } else if constexpr (kArray<T>) {
        for (auto& e : v) io(ar, e);
    } else if constexpr (kOptional<T>) {
        bool has = v.has_value();
        io(ar, has);
        if constexpr (Ar::loading) {
            if (has) v.emplace();
            else v.reset();
        }
        if (has) io(ar, *v);
    } else if constexpr (kPair<T>) {
        fields(ar, v.first, v.second);
    } else if constexpr (kMap<T>) {
        if constexpr (Ar::loading) {
            v.clear();
            const std::size_t n = ar.count(1);
            for (std::size_t i = 0; i < n; ++i) {
                std::pair<typename T::key_type, typename T::mapped_type> kv;
                io(ar, kv);
                v.insert(std::move(kv));
            }
        } else {
            ar.put(v.size(), 8);
            for (auto& [k, val] : v) {
                auto key = k;
                fields(ar, key, val);
            }
        }
    } else {
        static_assert(sizeof(T) == 0, "no save rule for this type");
    }
}

// --- Values ---

template <class Ar>
void io(Ar& ar, Money& m) {
    std::int64_t c = m.in_cents();
    io(ar, c);
    if constexpr (Ar::loading) m = Money::cents(c);
}

template <class Ar>
void io(Ar& ar, Date& d) {
    std::int32_t days = d.days_since_epoch();
    io(ar, days);
    if constexpr (Ar::loading) d = Date::from_days(days);
}

template <class Ar>
void io(Ar& ar, MapPoint& p) {
    fields(ar, p.x_mm, p.y_mm);
}

// --- Track, railway ---

template <class Ar>
void io(Ar& ar, TrackNode& n) {
    fields(ar, n.id, n.pos, n.z_mm);
}
template <class Ar>
void io(Ar& ar, TrackEdge& e) {
    fields(ar, e.id, e.a, e.b, e.length_mm, e.double_track, e.kind, e.bridge, e.owner, e.electrified, e.cost, e.removed);
}
template <class Ar>
void io(Ar& ar, PathStep& s) {
    fields(ar, s.edge, s.forward);
}
template <class Ar>
void io(Ar& ar, WaitingCargo& w) {
    fields(ar, w.milli, w.value);
}
template <class Ar>
void io(Ar& ar, ExpressWaiting& e) {
    fields(ar, e.cargo, e.destination, e.milli);
}
template <class Ar>
void io(Ar& ar, Station& s) {
    fields(ar, s.id, s.name, s.node, s.size, s.owner, s.waiting, s.express, s.received_this_month, s.town, s.built_day,
           s.passengers_boarded_milli, s.passengers_arrived_milli, s.passengers_waiting_milli_days);
}
template <class Ar>
void io(Ar& ar, Crossing& c) {
    fields(ar, c.a, c.b, c.at, c.overpass);
}
template <class Ar>
void io(Ar& ar, StationBuilding& b) {
    fields(ar, b.id, b.type, b.owner, b.pos);
}
template <class Ar>
void io(Ar& ar, ServiceBuilding& b) {
    fields(ar, b.id, b.type, b.node, b.owner);
}
template <class Ar>
void io(Ar& ar, ConsistRule& r) {
    fields(ar, r.custom, r.filter, r.min, r.max, r.cars);
}
template <class Ar>
void io(Ar& ar, Car& c) {
    fields(ar, c.cargo, c.milli, c.pickup_price, c.loaded_day, c.loaded_at, c.destination, c.expected_value,
           c.loaded_tick);
}
template <class Ar>
void io(Ar& ar, Train& t) {
    fields(ar, t.id, t.owner, t.loco, t.cars, t.priority, t.route, t.rules, t.caboose, t.diner, t.holding, t.stop_index);
    fields(ar, t.state, t.at_node, t.path, t.step, t.offset_mm, t.speed_mm_per_tick, t.wait_ticks_left, t.yielding,
           t.replan);
    fields(ar, t.water, t.sand, t.oil, t.water_used_mm, t.sand_used_climb_mm, t.oil_used_mm);
    fields(ar, t.stops_made, t.service_stops, t.breakdowns, t.built_day, t.distance_mm, t.fuel_kg_km, t.leg_mm_by_owner);
    fields(ar, t.revenue, t.last_income, t.last_income_tick);
}

// --- Economy ---

template <class Ar>
void io(Ar& ar, Site& s) {
    fields(ar, s.id, s.type, s.kind, s.cx, s.cy, s.level, s.buffer, s.produced_milli, s.port_mode, s.received_year_milli,
           s.spoilage_saved);
    fields(ar, s.owner, s.closed, s.made_milli, s.used_milli, s.monthly_profit, s.loss_years, s.utilisation_permille,
           s.pace_permille, s.owner_paid);
}
template <class Ar>
void io(Ar& ar, Town& t) {
    fields(ar, t.name, t.cx, t.cy, t.first_station_day, t.houses, t.express_this_month, t.freight_this_month,
           t.express_months, t.freight_months, t.growth_milli);
}
template <class Ar>
void io(Ar& ar, Territory& t) {
    fields(ar, t.name, t.access_cost, t.credit_grades, t.station_cost_percent, t.overhead_percent, t.closed_border);
}
template <class Ar>
void io(Ar& ar, TerritoryMap& m) {
    fields(ar, m.territories, m.width, m.height, m.cells);
}

// --- Companies and the market ---

template <class Ar>
void io(Ar& ar, Bond& b) {
    fields(ar, b.principal, b.rate_bp, b.issued_year);
}
template <class Ar>
void io(Ar& ar, YearAccounts& y) {
    fields(ar, y.year, y.lines, y.track_built, y.buildings_built, y.trains_bought, y.industries_bought, y.dividends_paid,
           y.acquisitions, y.debt_forgiven, y.start_price, y.dividends_per_share, y.dividend_quarters,
           y.share_return_permille);
}
template <class Ar>
void io(Ar& ar, Investor& i) {
    fields(ar, i.name, i.cash, i.holdings, i.chairs);
}
template <class Ar>
void io(Ar& ar, Market& m) {
    // Companies have no default constructor: make them, then fill them in.
    if constexpr (Ar::loading) {
        m.companies.clear();
        const std::size_t n = ar.count(1);
        for (std::size_t i = 0; i < n; ++i) m.companies.emplace_back("", Money{}, 0, *ar.balance);
    } else {
        ar.put(m.companies.size(), 8);
    }
    for (Company& c : m.companies) io(ar, c);
    io(ar, m.investors);
}
template <class Ar>
void io(Ar& ar, RivalRoute& r) {
    fields(ar, r.a, r.b);
}
template <class Ar>
void io(Ar& ar, Rival& r) {
    fields(ar, r.player, r.tycoon, r.last_build_month, r.routes);
}

// --- Scenarios ---

template <class Ar>
void io(Ar& ar, ScenarioTown& t) {
    fields(ar, t.name, t.x, t.y, t.houses);
}
template <class Ar>
void io(Ar& ar, Goal& g) {
    fields(ar, g.kind, g.towns, g.cargo, g.count, g.amount);
}
template <class Ar>
void io(Ar& ar, MedalTier& t) {
    fields(ar, t.deadline, t.goals);
}
template <class Ar>
void io(Ar& ar, EventCondition& c) {
    fields(ar, c.after, c.before, c.goals, c.variable, c.at_least, c.chance_percent);
}
template <class Ar>
void io(Ar& ar, EventEffect& f) {
    fields(ar, f.kind, f.target, f.text, f.percent, f.months, f.amount, f.value);
}
template <class Ar>
void io(Ar& ar, ScenarioEvent& e) {
    fields(ar, e.id, e.when, e.effects, e.repeat_months);
}
template <class Ar>
void io(Ar& ar, Restrictions& r) {
    fields(ar, r.track_tiles, r.tiles_per_year, r.tile_refund, r.no_unconnected_track, r.one_company_only, r.no_track,
           r.no_stations, r.no_trains, r.no_industries);
}
template <class Ar>
void io(Ar& ar, Scenario& s) {
    fields(ar, s.key, s.name, s.briefing, s.start, s.seed, s.map_size, s.rivals, s.territories, s.founder_fortune,
           s.founder_investment, s.outside_investment);
    fields(ar, s.chairman_can_resign, s.chairman_can_be_fired, s.towns, s.medals, s.events, s.restrictions);
}
template <class Ar>
void io(Ar& ar, ScenarioResult& r) {
    fields(ar, r.medal, r.won_on, r.finished);
}

// The data the save was made with, by key: loading checks it matches.
std::string data_fingerprint(const GameData& d) {
    std::string f;
    for (const CargoType& c : d.cargo.all()) f += "c:" + c.key + ";";
    for (const LocomotiveType& l : d.locomotives.all()) f += "l:" + l.key + ";";
    for (const IndustryType& i : d.industries.all()) f += "i:" + i.key + ";";
    for (const Tycoon& t : d.tycoons.all()) f += "t:" + t.key + ";";
    return f;
}

} // namespace

// --- Classes with private state (friends) ---

template <class Ar>
void io(Ar& ar, Random& r) {
    fields(ar, r.state_, r.inc_);
}

template <class Ar>
void io(Ar& ar, Terrain& t) {
    fields(ar, t.width_, t.height_, t.tile_size_m_, t.heights_, t.ground_, t.revision_);
}

template <class Ar>
void io(Ar& ar, TrackNetwork& n) {
    fields(ar, n.nodes_, n.edges_, n.adjacency_);
}

template <class Ar>
void io(Ar& ar, Railway& r) {
    fields(ar, r.track_, r.stations_, r.service_buildings_, r.station_buildings_, r.trains_, r.arrivals_, r.crashes_,
           r.breakdowns_, r.crossings_,
           r.today_, r.year_, r.rules_.breakdowns, r.rng_);
}

template <class Ar>
void io(Ar& ar, Economy& e) {
    fields(ar, e.width_, e.height_, e.cell_size_mm_, e.cells_per_node_, e.water_, e.price_, e.stock_);
    // Anchors are rebuilt every day, but saved anyway so nothing depends on that.
    if constexpr (Ar::loading) e.anchors_.assign(ar.count(1), {});
    else ar.put(e.anchors_.size(), 8);
    for (auto& list : e.anchors_) {
        if constexpr (Ar::loading) list.assign(ar.count(1), Economy::Anchor{0});
        else ar.put(list.size(), 8);
        for (auto& a : list) fields(ar, a.cell, a.demand_milli, a.supply_milli);
    }
    fields(ar, e.active_, e.scratch_, e.conductance_, e.east_w_, e.south_w_, e.node_territory_, e.closed_territory_,
           e.activity_percent_, e.warehouse_radius_, e.warehouse_spoilage_percent_, e.sites_, e.towns_);
    if constexpr (Ar::loading) e.balance_ = ar.balance->economy;
}

template <class Ar>
void io(Ar& ar, Company& c) {
    // The balance sections it keeps a copy of come from the game's balance.
    fields(ar, c.name_, c.id_, c.merged_into_, c.months_above_split_, c.bankrupt_year_, c.state_, c.cash_, c.track_,
           c.buildings_, c.rolling_stock_, c.industries_, c.bonds_, c.access_, c.delivered_, c.history_);
    fields(ar, c.shares_, c.price_, c.dividend_, c.issues_this_year_, c.pressure_, c.lifetime_profit_,
           c.lifetime_revenue_, c.month_marks_, c.revenue_marks_);
    if constexpr (Ar::loading) {
        const Balance& b = *ar.balance;
        c.finance_ = b.finance;
        c.stock_ = b.stock;
        c.states_ = b.economic_states;
        c.corporate_ = b.corporate;
    }
}

template <class Ar>
void io(Ar& ar, World& w) {
    fields(ar, w.rng_, w.date_, w.tick_of_day_, w.total_ticks_, w.terrain_, w.economy_, w.territories_, w.railway_);
    fields(ar, w.spent_, w.earned_, w.sandbox_, w.difficulty_, w.market_, w.rivals_, w.news_, w.last_player_company_);
    fields(ar, w.chairman_can_be_fired_, w.chairman_can_resign_, w.town_growth_, w.industries_appear_,
           w.failed_attempts_, w.rival_ai_, w.actor_, w.last_forced_sale_, w.business_cycle_, w.economy_news_,
           w.economic_state_, w.economy_terrain_revision_);
    if constexpr (Ar::loading) w.track_undo_.assign(ar.count(1), {});
    else ar.put(w.track_undo_.size(), 8);
    for (auto& stack : w.track_undo_) {
        if constexpr (Ar::loading) stack.assign(ar.count(1), {});
        else ar.put(stack.size(), 8);
        for (auto& u : stack) fields(ar, u.edges, u.ends, u.first_new_node, u.tiles);
    }
    fields(ar, w.scenario_, w.scenario_result_, w.event_month_, w.variables_, w.founder_, w.track_tiles_);
    if constexpr (Ar::loading) w.modifiers_.assign(ar.count(1), {});
    else ar.put(w.modifiers_.size(), 8);
    for (auto& m : w.modifiers_) fields(ar, m.kind, m.target, m.percent, m.until_month);
}

World::World(LoadTag, GameData data)
    : rng_(1), terrain_(1, 1, 1), data_(std::move(data)), economy_(1, 1, 1, data_.cargo, data_.balance, 1), railway_(1) {}

std::string World::save() const {
    Writer ar;
    ar.put_bytes(kMagic, sizeof kMagic);
    ar.put(kSaveVersion, 4);
    std::string balance = data_.balance.to_json();
    std::string fingerprint = data_fingerprint(data_);
    fields(ar, balance, fingerprint);
    io(ar, const_cast<World&>(*this)); // the writer only reads
    return ar.take();
}

World World::load(std::string_view bytes, GameData data) {
    Reader ar(bytes);
    if (ar.get_bytes(sizeof kMagic) != std::string(kMagic, sizeof kMagic)) {
        throw std::runtime_error("save: not a Railmaster save");
    }
    const auto version = static_cast<std::uint32_t>(ar.get(4));
    if (version != kSaveVersion) {
        throw std::runtime_error("save: version " + std::to_string(version) + " is not supported (this game reads " +
                                 std::to_string(kSaveVersion) + ")");
    }
    std::string balance, fingerprint;
    fields(ar, balance, fingerprint);
    data.balance = Balance::from_json(balance);
    if (fingerprint != data_fingerprint(data)) {
        throw std::runtime_error("save: made with different game data (cargo, engines, industries or tycoons)");
    }
    World w(LoadTag{}, std::move(data));
    ar.balance = &w.data_.balance;
    io(ar, w);
    if (!ar.done()) throw std::runtime_error("save: unexpected data at the end of the file");
    w.railway_.set_balance(w.data_.balance); // its copy of the balance, as for the parts below
    // Event effects in force are rebuilt on the data as loaded.
    w.base_cargo_ = w.data_.cargo;
    w.base_industries_ = w.data_.industries;
    w.base_locos_ = w.data_.locomotives;
    w.apply_modifiers();
    return w;
}

} // namespace railmaster::sim
