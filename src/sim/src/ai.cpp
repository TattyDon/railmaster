#include "railmaster/sim/ai.hpp"

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
            for (const std::int32_t v : {t.expansion, t.leverage, t.dividend, t.speculation, t.takeovers, t.industry}) {
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

MapPoint centre(const World& w, std::int32_t cx, std::int32_t cy) {
    const std::int64_t cell = std::int64_t{w.terrain().tile_size_m()} * 1000;
    return {cx * cell + cell / 2, cy * cell + cell / 2};
}

std::int64_t distance_km(const World& w, const Place& a, const Place& b) {
    const MapPoint pa = centre(w, a.cx, a.cy), pb = centre(w, b.cx, b.cy);
    return distance_mm(pa, pb) / 1'000'000;
}

std::int32_t chebyshev(std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by) {
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

bool input_wanted(const IndustryInput& in, std::int32_t year) { return !in.until_year || year <= *in.until_year; }

// Houses within a town's reach, counting each house cell's level.
std::int64_t houses(const World& w, const Town& town) {
    const Economy& eco = w.economy();
    const std::int32_t reach = w.data().balance.stations.town_reach_cells;
    std::int64_t n = 0;
    for (const Site& s : eco.sites()) {
        if (w.data().industries.get(s.type).kind == IndustryKind::House &&
            chebyshev(s.cx, s.cy, town.cx, town.cy) <= reach)
            n += s.level;
    }
    return n;
}

// Stations of `owner` (or of anyone, if unset) near a cell.
std::optional<StationId> station_near(const World& w, std::int32_t cx, std::int32_t cy, std::int32_t cells,
                                      std::optional<CompanyId> owner) {
    const Railway& rw = w.railway();
    for (const Station& st : rw.stations()) {
        if (owner && st.owner != *owner) continue;
        const MapPoint p = rw.track().node(st.node).pos;
        if (chebyshev(w.economy().cell_x(p), w.economy().cell_y(p), cx, cy) <= cells) return st.id;
    }
    return std::nullopt;
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
    std::vector<std::int64_t> town_houses;
    for (const Town& t : eco.towns()) {
        towns.push_back({t.cx, t.cy, t.name});
        town_houses.push_back(houses(w, t));
    }

    std::vector<Candidate> out;
    // Passengers and mail between towns: a fare for each load, and loads in
    // proportion to the smaller town [I].
    std::int64_t express_per_km = 0;
    for (const CargoType& c : cargo.all()) {
        if (c.cargo_class == CargoClass::Express && year >= c.available_year) express_per_km += std::int64_t{c.fare_per_km} * c.generation;
    }
    for (std::size_t i = 0; i < towns.size(); ++i) {
        for (std::size_t j = i + 1; j < towns.size(); ++j) {
            if (!in_range(towns[i], towns[j]) || (covered(towns[i]) && covered(towns[j]))) continue;
            const std::int64_t d = distance_km(w, towns[i], towns[j]);
            out.push_back({towns[i], towns[j], express_per_km * d * std::min(town_houses[i], town_houses[j]), 0});
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
            const std::int64_t here = eco.price(c, p.cx, p.cy);
            const std::int64_t loads = std::int64_t{pt.rate_per_year} * p.level;
            const auto consider = [&](const Place& to) {
                if (!in_range(from, to) || (covered(from) && covered(to))) return;
                const std::int64_t gain = eco.price(c, to.cx, to.cy) - here;
                if (gain > 0) out.push_back({from, to, gain * loads, 0});
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

// The fastest locomotive the company can afford now, if any.
std::optional<LocoTypeId> pick_loco(const World& w, Money budget) {
    std::optional<LocoTypeId> best;
    for (const LocomotiveType& l : w.data().locomotives.all()) {
        if (!l.available_in(w.date().year()) || l.cost > budget) continue;
        if (!best) {
            best = l.id;
            continue;
        }
        const LocomotiveType& cur = w.data().locomotives.get(*best);
        if (l.top_speed_mph > cur.top_speed_mph || (l.top_speed_mph == cur.top_speed_mph && l.cost < cur.cost)) best = l.id;
    }
    return best;
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
                               ? profit->scaled(ty.dividend, 100 * co.shares_outstanding())
                               : Money{};
    if (dividend != co.dividend_per_share()) w.execute(SetDividend{.per_share = dividend}, r.player);
    // Repay debt when flush, the cautious more readily.
    const Money face = Money::dollars(b.finance.bond_face_value);
    if (!co.bonds().empty() && co.cash() > face * (2 + ty.leverage / 20) + Money::dollars(b.ai.cash_reserve)) {
        w.execute(RepayBond{}, r.player);
    }
}

void add_trains(World& w, Rival& r, Company& co) {
    const Balance& b = w.data().balance;
    const Railway& rw = w.railway();
    // The company's lines, as its two-stop trains show them, inherited ones included.
    std::vector<RivalRoute> lines;
    for (const Train& t : rw.trains()) {
        if (t.owner != co.id() || t.state == TrainState::Crashed || t.route.size() != 2) continue;
        const RivalRoute line{std::min(t.route[0], t.route[1]), std::max(t.route[0], t.route[1])};
        if (std::none_of(lines.begin(), lines.end(),
                         [&](const RivalRoute& l) { return l.a == line.a && l.b == line.b; }))
            lines.push_back(line);
    }
    for (const RivalRoute& route : lines) {
        std::int32_t trains = 0;
        for (const Train& t : rw.trains()) {
            if (t.owner == co.id() && t.state != TrainState::Crashed && t.route.size() == 2 &&
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
        const Money total = plan.plan->total_cost + stations + loco_cost;
        if (!best || c.value_per_year * best->total.whole_dollars() > best->c.value_per_year * total.whole_dollars()) {
            best = Costed{c, cmd, total};
        }
    }
    if (!best) return;

    // Borrow for it if the tycoon is willing.
    const Money reserve = Money::dollars(b.ai.cash_reserve);
    const auto max_bonds = static_cast<std::size_t>(ty.leverage / 10);
    while (co.cash() < best->total + reserve && co.can_issue_bond() && co.bonds().size() < max_bonds) {
        if (!w.execute(IssueBond{}, r.player).ok) break;
    }
    if (co.cash() < best->total + reserve) return;

    r.last_build_month = now;
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

void run_rival(World& w, Rival& r, const Tycoon& ty) {
    // A tycoon whose company was merged away still trades.
    if (const auto chairs = w.investors().at(r.player).chairs) {
        Company& co = w.company(*chairs);
        manage_finance(w, r, ty, co);
        expand(w, r, ty, co);
        add_trains(w, r, co);
        invest_in_industry(w, r, ty, co);
    }
    speculate(w, r, ty);
    pursue_control(w, r, ty);
}

} // namespace railmaster::sim
