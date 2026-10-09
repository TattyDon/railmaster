#include "railmaster/sim/ai.hpp"

#include "railmaster/sim/freight.hpp"
#include "railmaster/sim/world.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace railmaster::sim {

TycoonRegistry TycoonRegistry::from_json(std::string_view json_text) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("tycoon data: ") + e.what());
    }
    TycoonRegistry reg;
    try {
        for (const auto& entry : doc.at("tycoons")) {
            Tycoon t;
            t.key = entry.at("key").get<std::string>();
            t.name = entry.at("name").get<std::string>();
            t.company = entry.at("company").get<std::string>();
            t.bio = entry.value("bio", std::string());
            t.expansion = entry.value("expansion", 50);
            t.leverage = entry.value("leverage", 50);
            t.dividend = entry.value("dividend", 30);
            t.speculation = entry.value("speculation", 30);
            t.takeovers = entry.value("takeovers", 30);
            t.industry = entry.value("industry", 30);
            t.recession_caution = entry.value("recession_caution", 50);
            for (const std::int32_t v :
                 {t.expansion, t.leverage, t.dividend, t.speculation, t.takeovers, t.industry, t.recession_caution}) {
                if (v < 0 || v > 100) throw std::runtime_error("tycoon data: personality out of 0..100 for '" + t.key + "'");
            }
            for (const Tycoon& other : reg.tycoons_) {
                if (other.key == t.key) throw std::runtime_error("tycoon data: duplicate key '" + t.key + "'");
            }
            reg.tycoons_.push_back(std::move(t));
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("tycoon data: ") + e.what());
    }
    return reg;
}

namespace {

struct Place {
    std::int32_t cx = 0, cy = 0;
    std::string name;
};

struct Candidate {
    Place a, b;
    std::int64_t value_per_year = 0; // dollars, a rough guess
    std::int64_t rough_cost = 0;
};

// How close track must be to a planned end for the AI to join it.
constexpr std::int64_t kJoinMm = 300'000;

std::int32_t month_index(Date d) { return d.year() * 12 + d.month() - 1; }

MapPoint centre(const World& w, std::int32_t cx, std::int32_t cy) { return w.economy().node_centre(cx, cy); }

std::int64_t distance_km(const World& w, const Place& a, const Place& b) {
    const MapPoint pa = centre(w, a.cx, a.cy), pb = centre(w, b.cx, b.cy);
    return distance_mm(pa, pb) / 1'000'000;
}


bool input_wanted(const IndustryInput& in, std::int32_t year) { return !in.until_year || year <= *in.until_year; }

// Stations of `owner` (or of anyone, if unset) near a cell.
std::optional<StationId> station_near(const World& w, std::int32_t cx, std::int32_t cy, std::int32_t cells,
                                      std::optional<CompanyId> owner) {
    const Railway& rw = w.railway();
    for (const Station& st : rw.stations()) {
        if (owner && st.owner != *owner) continue;
        const MapPoint p = rw.track().node(st.node).pos;
        if (w.economy().cells_between(w.economy().cell_x(p), w.economy().cell_y(p), cx, cy) <= cells) return st.id;
    }
    return std::nullopt;
}

// The yearly rate, in carload thousandths, of the sites within a town's
// reach that make (or, with outputs false, take) cargo `c`.
std::int64_t town_rate(const World& w, const Town& town, CargoId c, bool outputs) {
    const Economy& eco = w.economy();
    const std::int32_t reach = w.data().balance.stations.town_reach_cells;
    std::int64_t total = 0;
    for (const Site& s : eco.sites()) {
        if (s.closed || eco.cells_between(s.cx, s.cy, town.cx, town.cy) > reach) continue;
        const IndustryType& t = w.data().industries.get(s.type);
        const bool has = outputs ? std::find(t.outputs.begin(), t.outputs.end(), c) != t.outputs.end()
                                 : std::any_of(t.inputs.begin(), t.inputs.end(),
                                               [&](const IndustryInput& in) { return in.cargo == c; });
        if (has) total += t.rate_milli * s.level;
    }
    return total;
}

// What a medium station at `p` would buy or sell cargo `c` for: the best
// price in its catchment, as the freight code uses.
std::int64_t station_price(const World& w, const Place& p, CargoId c) {
    const Economy& eco = w.economy();
    const std::int64_t r = eco.cells_mm(catchment_radius(StationSize::Medium, w.data().balance));
    std::int64_t best = 0;
    eco.for_nodes_within(eco.node_centre(p.cx, p.cy), r,
                         [&](std::int32_t x, std::int32_t y) { best = std::max<std::int64_t>(best, eco.price(c, x, y)); });
    return best;
}

std::vector<Candidate> candidates(const World& w, const Tycoon& ty) {
    const Balance& b = w.data().balance;
    const Economy& eco = w.economy();
    const IndustryRegistry& ind = w.data().industries;
    const CargoRegistry& cargo = w.data().cargo;
    const std::int32_t year = w.date().year();
    const std::int64_t min_km = b.ai.min_route_km;
    const std::int64_t max_km = b.ai.max_route_km + b.ai.max_route_km * ty.expansion / 200;
    const auto in_range = [&](const Place& p, const Place& q) {
        const std::int64_t d = distance_km(w, p, q);
        return d >= min_km && d <= max_km;
    };
    const auto covered = [&](const Place& p) { return station_near(w, p.cx, p.cy, b.ai.cover_cells, std::nullopt).has_value(); };

    std::vector<Place> towns;
    for (const Town& t : eco.towns()) {
        towns.push_back({t.cx, t.cy, t.name});
    }

    std::vector<Candidate> out;
    // Passengers and mail between towns, estimated as the express code
    // generates them: each town's rate x the cargo's generation, sent to the
    // other in proportion to its draw A / (A + half), at the fare per km.
    struct ExpressRates {
        const CargoType* cargo;
        std::vector<std::int64_t> made, drawn; // per town, carload thousandths a year
    };
    std::vector<ExpressRates> express;
    for (const CargoType& c : cargo.all()) {
        if (c.cargo_class != CargoClass::Express || c.generation <= 0 || year < c.available_year) continue;
        ExpressRates e{&c, {}, {}};
        for (const Town& t : eco.towns()) {
            e.made.push_back(town_rate(w, t, c.id, true) * c.generation);
            e.drawn.push_back(town_rate(w, t, c.id, false));
        }
        express.push_back(std::move(e));
    }
    const std::int64_t half = std::int64_t{b.express.attraction_half} * kMilli;
    for (std::size_t i = 0; i < towns.size(); ++i) {
        for (std::size_t j = i + 1; j < towns.size(); ++j) {
            if (!in_range(towns[i], towns[j]) || (covered(towns[i]) && covered(towns[j]))) continue;
            const std::int64_t d = distance_km(w, towns[i], towns[j]);
            std::int64_t value = 0;
            for (const ExpressRates& e : express) {
                const std::int64_t ij = e.made[i] * e.drawn[j] / std::max<std::int64_t>(1, e.drawn[j] + half);
                const std::int64_t ji = e.made[j] * e.drawn[i] / std::max<std::int64_t>(1, e.drawn[i] + half);
                value += std::int64_t{e.cargo->fare_per_km} * d * (ij + ji) / kMilli;
            }
            if (value > 0) out.push_back({towns[i], towns[j], value, 0});
        }
    }

    // Freight from a producer to somewhere that pays more for it.
    for (const Site& p : eco.sites()) {
        const IndustryType& pt = ind.get(p.type);
        const bool supplies = pt.kind == IndustryKind::Raw || pt.kind == IndustryKind::Processor ||
                              (trades(pt.kind) && p.port_mode != PortMode::Receive);
        if (!supplies || p.closed) continue;
        // A processor makes nothing without inputs; only count working ones.
        if (pt.kind == IndustryKind::Processor && p.produced_milli == 0) continue;
        const Place from{p.cx, p.cy, pt.name};
        for (CargoId c : pt.outputs) {
            const CargoType& ct = cargo.get(c);
            if (ct.cargo_class != CargoClass::Freight || year < ct.available_year || !eco.active(c)) continue;
            // Bought and sold at the stations' prices: the best in each catchment.
            const std::int64_t here = station_price(w, from, c);
            const std::int64_t loads_milli = pt.rate_milli * p.level;
            const auto consider = [&](const Place& to) {
                if (!in_range(from, to) || (covered(from) && covered(to))) return;
                const std::int64_t gain = station_price(w, to, c) - here;
                if (gain > 0) out.push_back({from, to, gain * loads_milli / kMilli, 0});
            };
            for (std::size_t t = 0; t < towns.size(); ++t) {
                bool wanted = false;
                for (const IndustryType& it : ind.all()) {
                    if (it.kind != IndustryKind::House) continue;
                    for (const IndustryInput& in : it.inputs) wanted |= in.cargo == c && input_wanted(in, year);
                }
                if (wanted) consider(towns[t]);
            }
            for (const Site& d : eco.sites()) {
                const IndustryType& dt = ind.get(d.type);
                const bool receives = dt.kind == IndustryKind::Sink || dt.kind == IndustryKind::Processor ||
                                      (trades(dt.kind) && d.port_mode != PortMode::Supply);
                if (!receives || d.closed) continue;
                bool wanted = false;
                for (const IndustryInput& in : dt.inputs) wanted |= in.cargo == c && input_wanted(in, year);
                if (wanted) consider({d.cx, d.cy, dt.name});
            }
        }
    }
    return out;
}

// The fastest steam or diesel locomotive the company can afford now, if any.
std::optional<LocoTypeId> pick_loco(const World& w, Money budget) {
    std::optional<LocoTypeId> best;
    for (const LocomotiveType& l : w.data().locomotives.all()) {
        if (!l.available_in(w.date().year()) || l.cost > budget) continue;
        if (l.fuel == Fuel::Electric) continue; // rivals do not electrify their lines (yet)
        if (!best) {
            best = l.id;
            continue;
        }
        const LocomotiveType& cur = w.data().locomotives.get(*best);
        if (l.top_speed_mph > cur.top_speed_mph || (l.top_speed_mph == cur.top_speed_mph && l.cost < cur.cost)) best = l.id;
    }
    return best;
}

// How much a tycoon holds back now: its caution x 1 in a recession, x 2 in
// a depression, 0 otherwise [I].
std::int32_t caution(const World& w, const Tycoon& ty) {
    switch (w.economic_state()) {
    case EconomicState::Recession: return ty.recession_caution;
    case EconomicState::Depression: return 2 * ty.recession_caution;
    default: return 0;
    }
}

// The cash a tycoon keeps in hand, more in bad times by its caution.
Money reserve(const World& w, const Tycoon& ty) {
    const Balance::Ai& b = w.data().balance.ai;
    return Money::dollars(b.cash_reserve).scaled(100 + std::int64_t{b.recession_reserve_percent} * caution(w, ty) / 100, 100);
}

// What is left of dividends and borrowing in bad times, in percent.
std::int64_t kept_percent(const World& w, const Tycoon& ty) {
    return std::max<std::int64_t>(0, 100 - std::int64_t{w.data().balance.ai.recession_cut_percent} * caution(w, ty) / 100);
}

void manage_finance(World& w, const Rival& r, const Tycoon& ty, Company& co) {
    const Balance& b = w.data().balance;
    // Borrow to stay solvent; failing that, go bankrupt as a last resort [C].
    if (co.cash() < Money{} && co.can_issue_bond()) w.execute(IssueBond{}, r.player);
    if (co.cash() < Money{} && !co.can_issue_bond() && !co.bankruptcy_problem()) {
        w.execute(DeclareBankruptcy{}, r.player);
    }
    // Pay out a share of profit, by temperament.
    const auto profit = co.trailing_profit();
    const Money dividend = profit && *profit > Money{} && co.shares_outstanding() > 0
                               ? profit->scaled(ty.dividend * kept_percent(w, ty), 100 * 100 * co.shares_outstanding())
                               : Money{};
    if (dividend != co.dividend_per_share()) w.execute(SetDividend{.per_share = dividend}, r.player);
    // Repay debt when flush, the cautious more readily.
    const Money face = Money::dollars(b.finance.bond_face_value);
    if (!co.bonds().empty() && co.cash() > face * (2 + ty.leverage / 20) + reserve(w, ty)) {
        w.execute(RepayBond{}, r.player);
    }
}

void add_trains(World& w, Rival& r, Company& co) {
    const Balance& b = w.data().balance;
    const Railway& rw = w.railway();
    // The company's lines, as its two-stop trains show them, inherited ones included.
    std::vector<RivalRoute> lines;
    for (const Train& t : rw.trains()) {
        if (t.owner != co.id() || !t.in_service() || t.route.size() != 2) continue;
        const RivalRoute line{std::min(t.route[0], t.route[1]), std::max(t.route[0], t.route[1])};
        if (std::none_of(lines.begin(), lines.end(),
                         [&](const RivalRoute& l) { return l.a == line.a && l.b == line.b; }))
            lines.push_back(line);
    }
    for (const RivalRoute& route : lines) {
        std::int32_t trains = 0;
        for (const Train& t : rw.trains()) {
            if (t.owner == co.id() && t.in_service() && t.route.size() == 2 &&
                ((t.route[0] == route.a && t.route[1] == route.b) || (t.route[0] == route.b && t.route[1] == route.a)))
                ++trains;
        }
        if (trains >= b.ai.max_trains_per_route) continue;
        std::int64_t waiting = 0;
        for (StationId s : {route.a, route.b}) {
            for (const WaitingCargo& c : rw.station(s).waiting) waiting += c.milli;
            for (const ExpressWaiting& e : rw.station(s).express) waiting += e.milli;
        }
        if (waiting < std::int64_t{b.ai.waiting_carloads_for_train} * kMilli * (trains + 1)) continue;
        const auto loco = pick_loco(w, co.cash() - Money::dollars(b.ai.cash_reserve));
        if (!loco) continue;
        w.execute(BuyTrain{.loco = *loco, .cars = static_cast<std::uint8_t>(b.ai.cars_per_train), .route = {route.a, route.b}},
                  r.player);
    }
}

// Re-engine the oldest train whose engine has reached the replacement age,
// with the fastest engine the company can spare the money for; one a month
// (rt3-clone-spec §13.2 [I]).
void replace_old_engines(World& w, const Rival& r, const Tycoon& ty, const Company& co) {
    const std::int32_t today = w.date().days_since_epoch();
    const std::int32_t age_days = w.data().balance.ai.replace_engine_age_years * 365;
    std::optional<TrainId> oldest;
    for (const Train& t : w.railway().trains()) {
        if (t.owner != co.id() || !t.in_service() || today - t.built_day < age_days) continue;
        if (!oldest || t.built_day < w.railway().train(*oldest).built_day) oldest = t.id;
    }
    if (!oldest) return;
    const auto loco = pick_loco(w, co.cash() - reserve(w, ty));
    if (!loco) return;
    w.execute(ReplaceLocomotive{.train = *oldest, .loco = *loco}, r.player);
}

// A station of ours near `p`, or a new one at `node` named for the place.
std::optional<StationId> station_at(World& w, const Rival& r, NodeId node, const Place& p) {
    for (const Station& st : w.railway().stations())
        if (st.node == node) return st.id;
    const CommandResult res =
        w.execute(BuildStation{.at = {TrackEnd::Kind::Node, node, 0, w.railway().track().node(node).pos},
                               .size = StationSize::Medium,
                               .name = p.name},
                  r.player);
    if (!res.ok) return std::nullopt;
    return res.created_id;
}

void ensure_service(World& w, const Rival& r, const Company& co, NodeId node) {
    bool tower = false, facility = false;
    for (const ServiceBuilding& sb : w.railway().service_buildings()) {
        if (sb.owner != co.id()) continue;
        facility |= sb.type == ServiceType::MaintenanceFacility;
        tower |= sb.type == ServiceType::ServiceTower && sb.node == node;
    }
    const TrackEnd at{TrackEnd::Kind::Node, node, 0, w.railway().track().node(node).pos};
    if (!facility) w.execute(BuildServiceBuilding{.at = at, .type = ServiceType::MaintenanceFacility}, r.player);
    if (!tower) w.execute(BuildServiceBuilding{.at = at, .type = ServiceType::ServiceTower}, r.player);
}

void expand(World& w, Rival& r, const Tycoon& ty, Company& co) {
    const Balance& b = w.data().balance;
    const std::int32_t now = month_index(w.date());
    const std::int32_t interval =
        b.ai.build_interval_months_max -
        (b.ai.build_interval_months_max - b.ai.build_interval_months_min) * ty.expansion / 100;
    if (now - r.last_build_month < interval) return;
    // The cautious build nothing new in bad times.
    if (caution(w, ty) >= b.ai.recession_stop_building) return;

    std::vector<Candidate> cands = candidates(w, ty);
    if (cands.empty()) return;
    const Money loco_cost = [&] {
        const auto l = pick_loco(w, Money::dollars(1'000'000'000));
        return l ? w.data().locomotives.get(*l).cost : Money{};
    }();
    const Money stations = w.construction_cost(station_cost(StationSize::Medium, b)) * 2;
    for (Candidate& c : cands) {
        c.rough_cost = std::max<std::int64_t>(1, distance_km(w, c.a, c.b) * b.track.ground_per_km * w.cost_percent() / 100 +
                                                    (stations + loco_cost).whole_dollars());
    }
    // Best value for money first; ties by position for determinism.
    std::stable_sort(cands.begin(), cands.end(), [](const Candidate& x, const Candidate& y) {
        return x.value_per_year * y.rough_cost > y.value_per_year * x.rough_cost;
    });

    struct Costed {
        Candidate c;
        BuildTrack cmd;
        Money total;
        std::vector<TerritoryId> access; // territories to buy rights to first
    };
    std::optional<Costed> best;
    const auto n = std::min<std::size_t>(cands.size(), static_cast<std::size_t>(std::max(1, b.ai.candidates_previewed)));
    for (std::size_t i = 0; i < n; ++i) {
        const Candidate& c = cands[i];
        BuildTrack cmd;
        const auto end_for = [&](const Place& p) -> TrackEnd {
            if (const auto s = station_near(w, p.cx, p.cy, b.ai.cover_cells, co.id())) {
                const NodeId node = w.railway().station(*s).node;
                return {TrackEnd::Kind::Node, node, 0, w.railway().track().node(node).pos};
            }
            // Join any track already there (a rival's included: trackage rights).
            return pick_track_end(w.railway().track(), centre(w, p.cx, p.cy), kJoinMm);
        };
        cmd.start = end_for(c.a);
        cmd.end = end_for(c.b);
        const PlanResult plan = w.preview(cmd);
        if (!plan.plan) continue;
        // Access rights to any territory the line crosses count as cost.
        std::vector<MapPoint> where = plan.plan->points;
        where.push_back(cmd.start.pos);
        where.push_back(cmd.end.pos);
        const std::vector<TerritoryId> access = w.access_needed(co.id(), where);
        Money total = plan.plan->total_cost + stations + loco_cost;
        for (TerritoryId t : access) total += w.territories().territories[t].access_cost;
        if (!best || c.value_per_year * best->total.whole_dollars() > best->c.value_per_year * total.whole_dollars()) {
            best = Costed{c, cmd, total, access};
        }
    }
    if (!best) return;

    // Borrow for it if the tycoon is willing.
    const Money keep = reserve(w, ty);
    const auto max_bonds = static_cast<std::size_t>(ty.leverage * kept_percent(w, ty) / 1000);
    while (co.cash() < best->total + keep && co.can_issue_bond() && co.bonds().size() < max_bonds) {
        if (!w.execute(IssueBond{}, r.player).ok) break;
    }
    if (co.cash() < best->total + keep) return;

    r.last_build_month = now;
    for (TerritoryId t : best->access)
        if (!w.execute(BuyTerritoryAccess{.territory = t}, r.player).ok) return;
    const CommandResult track = w.execute(best->cmd, r.player);
    if (!track.ok) return;
    const NodeId end_node = track.created_id;
    // Where the run began: the node it started from, or the one made there.
    const NodeId start_node = best->cmd.start.kind == TrackEnd::Kind::Node
                                  ? best->cmd.start.node
                                  : *w.railway().track().nearest_node(best->cmd.start.pos, 1);
    const auto sa = station_at(w, r, start_node, best->c.a);
    const auto sb = station_at(w, r, end_node, best->c.b);
    if (!sa || !sb) return;
    ensure_service(w, r, co, start_node);
    const auto loco = pick_loco(w, co.cash());
    if (!loco) return;
    if (w.execute(BuyTrain{.loco = *loco, .cars = static_cast<std::uint8_t>(b.ai.cars_per_train), .route = {*sa, *sb}},
                  r.player)
            .ok) {
        r.routes.push_back({*sa, *sb});
    }
}

std::optional<CompanyId> own_company(const World& w, const Rival& r) { return w.investors().at(r.player).chairs; }

// Trade rivals' shares against where their prices are heading (the target
// price the market moves towards each month): buy below it, sell above it,
// and for the boldest, sell short well above it and buy back once the price
// has come down to it [I].
void speculate(World& w, const Rival& r, const Tycoon& ty) {
    const Balance& b = w.data().balance;
    if (ty.speculation < 40) return;
    const auto mine = own_company(w, r);
    for (const Company& other : w.companies()) {
        if (other.defunct() || other.id() == mine) continue;
        const Investor& me = w.investors().at(r.player);
        const Money value = target_share_price(other);
        const std::int64_t block = std::max<std::int64_t>(1, other.stock_balance().share_block);
        const std::int64_t held = me.shares_in(other.id());
        const Money price = other.share_price();
        const bool dear = price > value.scaled(b.ai.sell_above_value_percent, 100);
        const bool very_dear = price > value.scaled(b.ai.short_above_value_percent, 100);
        const bool cheap = price < value.scaled(b.ai.buy_below_value_percent, 100);
        if (held < 0 && price <= value) {
            w.execute(BuyShares{.blocks = (-held + block - 1) / block, .company = other.id()}, r.player);
        } else if (held >= block && dear && ty.takeovers < 50) {
            w.execute(SellShares{.blocks = held / block, .company = other.id()}, r.player);
        } else if (held <= 0 && very_dear && ty.speculation >= 70) {
            const std::int64_t blocks = 1 + ty.speculation / 40;
            if (purchasing_power(me, w.market()) > other.share_price() * (blocks * block) * 2) {
                w.execute(SellShares{.blocks = blocks, .company = other.id()}, r.player);
            }
        } else if (cheap && held >= 0) {
            const std::int64_t blocks = 1 + ty.speculation / 25;
            if (purchasing_power(me, w.market()) > other.share_price() * (blocks * block) * 3) {
                w.execute(BuyShares{.blocks = blocks, .company = other.id()}, r.player);
            }
        }
    }
}

// Build a stake in the weakest rival, take it over once it is big enough,
// and merge companies already controlled (rt3-clone-spec §13.2 step 4).
void pursue_control(World& w, const Rival& r, const Tycoon& ty) {
    const Balance& b = w.data().balance;
    if (ty.takeovers < 50) return;
    const auto mine = own_company(w, r);

    // The rival trading furthest below book value.
    std::optional<CompanyId> target;
    std::int64_t best_ratio = 0;
    for (const Company& c : w.companies()) {
        if (c.defunct() || c.id() == mine || c.book_value_per_share() <= Money{}) continue;
        const std::int64_t ratio = c.share_price().in_cents() * 1000 / c.book_value_per_share().in_cents();
        if (ratio < 1000 && (!target || ratio < best_ratio)) {
            target = c.id();
            best_ratio = ratio;
        }
    }

    for (const Company& c : w.companies()) {
        if (c.defunct() || c.id() == mine || c.shares_outstanding() <= 0) continue;
        const Investor& me = w.investors().at(r.player);
        const std::int64_t stake_permille = std::max<std::int64_t>(0, me.shares_in(c.id())) * 1000 / c.shares_outstanding();
        // Merge what it controls, if its company can pay the others out.
        if (mine && stake_permille > 500 && ty.takeovers >= 70 && c.id() != w.company().id()) {
            const Money offer = c.share_price().scaled(b.corporate.merger_neutral_premium_percent + 15, 100);
            const Money cost = offer * (c.shares_outstanding() - me.shares_in(c.id()));
            if (w.company(*mine).cash() > cost + Money::dollars(b.ai.cash_reserve)) {
                w.execute(AttemptMerger{.target = c.id(), .offer_per_share = offer}, r.player);
                continue;
            }
        }
        // Take over a cheap company once it holds over 35% [C/I]: always when
        // it has no company of its own, and for the boldest raiders even
        // though its own company then passes to the ousted chairman.
        if (stake_permille > 350 && c.share_price() < c.book_value_per_share() && (!mine || ty.takeovers >= 80)) {
            w.execute(AttemptTakeover{.target = c.id()}, r.player);
        }
    }

    // Keep buying into the target up to a majority.
    if (!target) return;
    const Company& c = w.company(*target);
    const Investor& me = w.investors().at(r.player);
    const std::int64_t block = std::max<std::int64_t>(1, c.stock_balance().share_block);
    if (me.shares_in(c.id()) * 2 > c.shares_outstanding()) return;
    const std::int64_t blocks = std::max(1, ty.takeovers / 20);
    if (purchasing_power(me, w.market()) > c.share_price() * (blocks * block) * 2) {
        w.execute(BuyShares{.blocks = blocks, .company = c.id()}, r.player);
    }
}

} // namespace

// Buy the best-paying industry its stations serve, and double the capacity
// of a plant running near full that would pay for it within a year
// (rt3-clone-spec §13.2 industryInvestment [I]).
void invest_in_industry(World& w, const Rival& r, const Tycoon& ty, Company& co) {
    if (ty.industry < 40) return;
    const Balance& b = w.data().balance;
    const Money reserve = Money::dollars(b.ai.cash_reserve) * 2;
    std::optional<SiteId> best;
    Money best_profit;
    for (const Site& s : w.economy().sites()) {
        const IndustryType& t = w.data().industries.get(s.type);
        if (s.closed || !ownable(t.kind)) continue;
        const Money profit = annual_profit(s);
        if (s.owner == co.id() && t.kind == IndustryKind::Processor && profit > Money{} &&
            s.utilisation_permille >= 900) {
            const Money cost = w.construction_cost(industry_upgrade_cost(s, b.industries));
            if (profit > cost && co.cash() > cost + reserve) {
                w.execute(UpgradeIndustry{.site = s.id}, r.player);
                return;
            }
        }
        if (s.owner || profit <= Money{}) continue;
        if (!station_near(w, s.cx, s.cy, b.stations.catchment_medium, co.id())) continue;
        const Money price = industry_price(s, b.industries);
        // A return of at least one part in (multiple + 2) a year, by temperament.
        if (profit * (b.industries.profit_multiple + 2) < price || co.cash() < price + reserve) continue;
        if (!best || profit > best_profit) {
            best = s.id;
            best_profit = profit;
        }
    }
    if (best) w.execute(BuyIndustry{.site = *best}, r.player);
}

// A restaurant by each of its stations that has none nearby, one a month
// (rt3-clone-spec §7.2: rivals build by stations [C]).
void add_station_buildings(World& w, const Rival& r, const Company& co) {
    const Balance& b = w.data().balance;
    const Money cost = w.construction_cost(station_building_cost(StationBuildingType::Restaurant, b));
    if (co.cash() < cost + Money::dollars(b.ai.cash_reserve) * 4) return;
    for (const Station& st : w.railway().stations()) {
        if (st.owner != co.id() || w.railway().near_building(st.id, StationBuildingType::Restaurant)) continue;
        const MapPoint at = w.railway().track().node(st.node).pos;
        // Just off the station, on whichever side is dry.
        for (const MapPoint p : {MapPoint{at.x_mm + 300'000, at.y_mm}, MapPoint{at.x_mm - 300'000, at.y_mm},
                                 MapPoint{at.x_mm, at.y_mm + 300'000}, MapPoint{at.x_mm, at.y_mm - 300'000}}) {
            if (w.execute(BuildStationBuilding{.type = StationBuildingType::Restaurant, .pos = p}, r.player).ok) return;
        }
    }
}

void run_rival(World& w, Rival& r, const Tycoon& ty) {
    // A tycoon whose company was merged away still trades.
    if (const auto chairs = w.investors().at(r.player).chairs) {
        Company& co = w.company(*chairs);
        manage_finance(w, r, ty, co);
        expand(w, r, ty, co);
        add_trains(w, r, co);
        replace_old_engines(w, r, ty, co);
        invest_in_industry(w, r, ty, co);
        add_station_buildings(w, r, co);
    }
    speculate(w, r, ty);
    pursue_control(w, r, ty);
}

} // namespace railmaster::sim
