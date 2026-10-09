#include "railmaster/sim/commands.hpp"

#include "railmaster/sim/world.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>

namespace railmaster::sim {

namespace {

CommandResult fail(std::string why) {
    CommandResult r;
    r.error = std::move(why);
    return r;
}

std::string dollars(Money m) {
    const std::string digits = std::to_string(std::max<std::int64_t>(0, m.whole_dollars()));
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return "$" + out;
}

CommandResult success(Money cost, std::uint32_t id) {
    CommandResult r;
    r.ok = true;
    r.cost = cost;
    r.created_id = id;
    return r;
}

std::int64_t anchor_z(const TrackNetwork& net, const Terrain& terrain, const TrackEnd& at) {
    switch (at.kind) {
    case TrackEnd::Kind::Node: return net.node(at.node).z_mm;
    case TrackEnd::Kind::OnEdge: return net.rail_z_at(at.edge, at.pos);
    case TrackEnd::Kind::Free: return terrain.height_at_mm(at.pos);
    }
    return 0;
}

std::vector<MapPoint> run_points(const BuildTrack& cmd, const Balance& b) {
    const std::int64_t piece = b.track.piece_mm;
    if (cmd.curve_control) return curve_points(cmd.start.pos, *cmd.curve_control, cmd.end.pos, piece);
    return straight_points(cmd.start.pos, cmd.end.pos, piece);
}

TrackBuildOptions options_for(const BuildTrack& cmd, std::int32_t year) {
    TrackBuildOptions o;
    o.year = year;
    o.double_track = cmd.double_track;
    o.tunnel_preference = cmd.tunnel_preference;
    o.bridge_type = cmd.bridge_type;
    return o;
}

} // namespace

TrackEnd pick_track_end(const TrackNetwork& net, MapPoint p, std::int64_t snap_mm) {
    if (const auto n = net.nearest_node(p, snap_mm)) {
        return {TrackEnd::Kind::Node, *n, 0, net.node(*n).pos};
    }
    if (const auto ep = net.nearest_edge_point(p, snap_mm)) {
        // A projection that lands on an end is that end's node.
        const TrackEdge& e = net.edge(ep->edge);
        if (ep->along_mm <= 0) return {TrackEnd::Kind::Node, e.a, 0, net.node(e.a).pos};
        if (ep->along_mm >= e.length_mm) return {TrackEnd::Kind::Node, e.b, 0, net.node(e.b).pos};
        return {TrackEnd::Kind::OnEdge, 0, ep->edge, ep->pos};
    }
    return {TrackEnd::Kind::Free, 0, 0, p};
}

PlanResult World::preview(const BuildTrack& cmd) const {
    const TrackNetwork& net = railway_.track();
    if (cmd.start.pos == cmd.end.pos) return {std::nullopt, "start and end are the same place"};
    if (cmd.start.kind == TrackEnd::Kind::Node && cmd.end.kind == TrackEnd::Kind::Node &&
        cmd.start.node == cmd.end.node) {
        return {std::nullopt, "start and end are the same place"};
    }
    const std::optional<std::int64_t> end_z =
        cmd.end.kind == TrackEnd::Kind::Free ? std::nullopt
                                             : std::optional<std::int64_t>{anchor_z(net, terrain_, cmd.end)};
    return priced(plan_track_between(terrain_, cmd.start.pos, anchor_z(net, terrain_, cmd.start),
                                     run_points(cmd, data_.balance), end_z, options_for(cmd, date_.year()),
                                     data_.balance));
}

PlanResult World::priced(PlanResult r) const {
    if (!r.plan) return r;
    r.plan->total_cost = Money{};
    for (auto& piece : r.plan->pieces) {
        piece.cost = construction_cost(piece.cost);
        r.plan->total_cost += piece.cost;
    }
    return r;
}

CommandResult World::execute(const Command& cmd, PlayerId who) {
    if (who >= market_.investors.size()) return fail("unknown player");
    actor_ = who;
    CommandResult r = std::visit(
        [this](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            // Trading shares and bidding for control are personal; everything
            // else acts for a company.
            if constexpr (!std::is_same_v<T, BuyShares> && !std::is_same_v<T, SellShares> &&
                          !std::is_same_v<T, AttemptTakeover> && !std::is_same_v<T, Resign> &&
                          !std::is_same_v<T, FoundCompany>) {
                if (!market_.investors[actor_].chairs) return fail("you do not run a company");
            }
            return run(c);
        },
        cmd);
    actor_ = kHumanPlayer;
    if (r.ok && who == kHumanPlayer) spent_ += r.cost;
    return r;
}

Company& World::acting() { return market_.companies.at(*market_.investors.at(actor_).chairs); }
const Company& World::acting() const { return market_.companies.at(*market_.investors.at(actor_).chairs); }

bool World::money_no_object() const { return sandbox_ && actor_ == kHumanPlayer; }

bool World::owns_track_at(const TrackEnd& at) {
    const TrackNetwork& net = railway_.track();
    const CompanyId me = acting().id();
    if (at.kind == TrackEnd::Kind::OnEdge) return net.edge(at.edge).owner == me;
    if (at.kind == TrackEnd::Kind::Node) {
        for (EdgeId e : net.edges_at(at.node))
            if (net.edge(e).owner == me) return true;
    }
    return false;
}

NodeId World::resolve_on_track(const TrackEnd& at) {
    TrackNetwork& net = railway_.track();
    switch (at.kind) {
    case TrackEnd::Kind::Node: return at.node;
    case TrackEnd::Kind::OnEdge: {
        // Look the edge up again by position: an earlier split in the same
        // command may have moved this point onto the new half of the edge.
        const auto ep = net.nearest_edge_point(at.pos, 2);
        if (!ep) throw std::logic_error("track point vanished");
        const TrackEdge& e = net.edge(ep->edge);
        if (net.node(e.a).pos == at.pos) return e.a;
        if (net.node(e.b).pos == at.pos) return e.b;
        return railway_.split_edge(ep->edge, at.pos);
    }
    case TrackEnd::Kind::Free: return net.add_node(at.pos, terrain_.height_at_mm(at.pos));
    }
    throw std::logic_error("unknown track end");
}

std::optional<std::string> World::cannot_afford(Money cost) const {
    if (money_no_object() || cost <= acting().cash()) return std::nullopt;
    return "not enough cash: costs " + dollars(cost) + ", the company has " + dollars(acting().cash());
}

CommandResult World::run(const BuildTrack& cmd) {
    // Validate everything before changing anything.
    const PlanResult check = preview(cmd);
    if (!check.plan) return fail(check.error);
    // Every piece must lie where the company may build [D].
    std::vector<MapPoint> where = check.plan->points;
    where.push_back(cmd.start.pos);
    where.push_back(cmd.end.pos);
    for (const MapPoint& p : where)
        if (auto why = access_problem(acting().id(), p)) return fail(*why);
    if (auto why = cannot_afford(check.plan->total_cost)) return fail(*why);

    const NodeId from = resolve_on_track(cmd.start);
    const NodeId to = resolve_on_track(cmd.end);
    // The end node already exists now (even for open ground), so plan into it.
    std::vector<MapPoint> points = run_points(cmd, data_.balance);
    PlanResult plan = priced(plan_track(railway_.track(), terrain_, from, std::move(points), to,
                                        options_for(cmd, date_.year()), data_.balance));
    if (!plan.plan) throw std::logic_error("track plan changed between check and build: " + plan.error);
    build_track(railway_.track(), *plan.plan, acting().id());
    acting().invest_track(plan.plan->total_cost);
    return success(plan.plan->total_cost, to);
}

CommandResult World::run(const BuildStation& cmd) {
    if (cmd.at.kind == TrackEnd::Kind::Free) return fail("stations must be built on track");
    if (!owns_track_at(cmd.at)) return fail("stations must be built on your own track");
    if (cmd.at.kind == TrackEnd::Kind::Node) {
        for (const Station& s : railway_.stations()) {
            if (s.node == cmd.at.node) return fail("there is already a station here");
        }
    }
    if (auto why = access_problem(acting().id(), cmd.at.pos)) return fail(*why);
    // A territory may make stations dearer [C].
    const TerritoryId terr = territory_at(cmd.at.pos);
    const std::int64_t terr_pct =
        terr < territories_.territories.size() ? territories_.territories[terr].station_cost_percent : 100;
    const Money cost = construction_cost(station_cost(cmd.size, data_.balance)).scaled(terr_pct, 100);
    if (auto why = cannot_afford(cost)) return fail(*why);
    const NodeId node = resolve_on_track(cmd.at);
    acting().invest_buildings(cost);
    std::string name = cmd.name.empty() ? "Station " + std::to_string(railway_.stations().size() + 1) : cmd.name;
    const StationId id = railway_.add_station(std::move(name), node, cmd.size, acting().id());
    // Which town it serves, for the station-age modifier: the nearest town
    // centre within the reach of a town's houses.
    Station& st = railway_.station_mut(id);
    st.built_day = date_.days_since_epoch();
    const MapPoint p = railway_.track().node(node).pos;
    std::int64_t best = economy_.cells_mm(data_.balance.stations.town_reach_cells);
    for (std::size_t t = 0; t < economy_.towns().size(); ++t) {
        const Town& town = economy_.towns()[t];
        const MapPoint c = economy_.node_centre(town.cx, town.cy);
        const std::int64_t d = std::max(std::abs(c.x_mm - p.x_mm), std::abs(c.y_mm - p.y_mm));
        if (d <= best) {
            best = d;
            st.town = t;
        }
    }
    if (st.town && !economy_.towns()[*st.town].first_station_day) {
        economy_.town_mut(*st.town).first_station_day = st.built_day;
    }
    return success(cost, id);
}

CommandResult World::run(const BuildServiceBuilding& cmd) {
    if (cmd.at.kind == TrackEnd::Kind::Free) return fail("support buildings must be built on track");
    if (!owns_track_at(cmd.at)) return fail("support buildings must be built on your own track");
    if (cmd.at.kind == TrackEnd::Kind::Node) {
        for (const ServiceBuilding& b : railway_.service_buildings()) {
            if (b.node == cmd.at.node && b.type == cmd.type) return fail("there is already one here");
        }
    }
    if (auto why = access_problem(acting().id(), cmd.at.pos)) return fail(*why);
    if (auto why = cannot_afford(construction_cost(service_building_cost(cmd.type, data_.balance)))) return fail(*why);
    const NodeId node = resolve_on_track(cmd.at);
    acting().invest_buildings(construction_cost(service_building_cost(cmd.type, data_.balance)));
    const ServiceBuildingId id = railway_.add_service_building(cmd.type, node, acting().id());
    return success(construction_cost(service_building_cost(cmd.type, data_.balance)), id);
}

CommandResult World::run(const BuyTrain& cmd) {
    if (cmd.loco >= data_.locomotives.all().size()) return fail("unknown locomotive");
    const LocomotiveType& loco = data_.locomotives.get(cmd.loco);
    if (!loco.available_in(date_.year())) return fail(loco.name + " is not available in " + std::to_string(date_.year()));
    if (cmd.cars > kMaxCarsPerTrain) return fail("a train can pull at most 8 cars");
    if (cmd.route.size() < 2) return fail("a route needs at least two stops");
    for (StationId s : cmd.route) {
        if (s >= railway_.stations().size()) return fail("unknown station in route");
    }
    if (loco.fuel == Fuel::Electric) {
        if (auto why = railway_.electric_route_problem(cmd.route)) return fail(*why);
    }
    if (auto why = cannot_afford(loco.cost)) return fail(*why);
    const TrainId id = railway_.add_train(cmd.loco, cmd.cars, cmd.route, cmd.priority, acting().id());
    railway_.train_mut(id).built_day = date_.days_since_epoch();
    acting().invest_train(loco.cost);
    return success(loco.cost, id);
}

CommandResult World::run(const ReplaceLocomotive& cmd) {
    if (cmd.loco >= data_.locomotives.all().size()) return fail("unknown locomotive");
    if (auto why = own_train_problem(cmd.train)) return fail(*why);
    const Train& t = railway_.train(cmd.train);
    const LocomotiveType& loco = data_.locomotives.get(cmd.loco);
    if (!loco.available_in(date_.year())) return fail(loco.name + " is not available in " + std::to_string(date_.year()));
    if (loco.fuel == Fuel::Electric) {
        if (auto why = railway_.electric_route_problem(t.route)) return fail(*why);
    }
    if (auto why = cannot_afford(loco.cost)) return fail(*why);
    acting().write_off_train(data_.locomotives.get(t.loco).cost); // scrapped
    acting().invest_train(loco.cost);
    Train& tm = railway_.train_mut(cmd.train);
    tm.loco = cmd.loco;
    tm.built_day = date_.days_since_epoch();
    tm.water = tm.sand = tm.oil = kGaugeFull;
    tm.water_used_mm = tm.sand_used_climb_mm = tm.oil_used_mm = 0;
    return success(loco.cost, cmd.train);
}

std::optional<std::string> World::own_train_problem(TrainId id) const {
    if (id >= railway_.trains().size()) return "no such train";
    const Train& t = railway_.train(id);
    if (!t.in_service()) return "that train is no longer in service";
    if (t.owner != acting().id()) return "you can only change your own trains";
    return std::nullopt;
}

CommandResult World::run(const SetConsist& cmd) {
    if (auto why = own_train_problem(cmd.train)) return fail(*why);
    const Train& t = railway_.train(cmd.train);
    if (cmd.stop && *cmd.stop >= t.route.size()) return fail("the train has no such stop");
    const ConsistRule& r = cmd.rule;
    const std::size_t room = kMaxCarsPerTrain - std::size_t{t.caboose} - std::size_t{t.diner};
    if (r.capacity() > room) return fail("only " + std::to_string(room) + " car slots are free");
    if (r.min > r.capacity()) return fail("the minimum is more cars than the consist has");
    for (CargoId c : r.cars)
        if (c >= data_.cargo.all().size()) return fail("unknown cargo in the consist");
    Train& tm = railway_.train_mut(cmd.train);
    if (cmd.stop) tm.rules[*cmd.stop] = r;
    else std::fill(tm.rules.begin(), tm.rules.end(), r);
    return success(Money{}, cmd.train);
}

CommandResult World::run(const SetSpecialCars& cmd) {
    if (auto why = own_train_problem(cmd.train)) return fail(*why);
    const Train& t = railway_.train(cmd.train);
    const std::size_t room = kMaxCarsPerTrain - std::size_t{cmd.caboose} - std::size_t{cmd.diner};
    for (const ConsistRule& r : t.rules)
        if (r.capacity() > room) return fail("the consist needs more slots than would be left; take cars off first");
    Train& tm = railway_.train_mut(cmd.train);
    tm.caboose = cmd.caboose;
    tm.diner = cmd.diner;
    return success(Money{}, cmd.train);
}

CommandResult World::run(const CopyTrain& cmd) {
    if (auto why = own_train_problem(cmd.train)) return fail(*why);
    const Train original = railway_.train(cmd.train);
    const CommandResult bought = run(BuyTrain{.loco = original.loco,
                                              .cars = static_cast<std::uint8_t>(original.slots_at(0)),
                                              .route = original.route,
                                              .priority = original.priority});
    if (!bought.ok) return bought;
    Train& copy = railway_.train_mut(bought.created_id);
    copy.rules = original.rules;
    copy.caboose = original.caboose;
    copy.diner = original.diner;
    return bought;
}

CommandResult World::run(const RetireTrain& cmd) {
    if (auto why = own_train_problem(cmd.train)) return fail(*why);
    Train& t = railway_.train_mut(cmd.train);
    acting().write_off_train(data_.locomotives.get(t.loco).cost);
    t.state = TrainState::Retired;
    t.cars.clear();
    t.path.clear();
    t.holding = false;
    return success(Money{}, cmd.train);
}

Money World::electrify_cost(EdgeId id) const {
    const TrackEdge& e = railway_.track().edge(id);
    if (e.electrified) return Money{};
    const Balance::Track& b = data_.balance.track;
    Money cost = Money::dollars(b.ground_per_km).scaled(e.length_mm * b.electrify_percent, 1'000'000LL * 100);
    if (e.double_track) cost = cost.scaled(b.double_track_percent, 100);
    return construction_cost(cost);
}

CommandResult World::run(const ElectrifyTrack& cmd) {
    const TrackNetwork& net = railway_.track();
    std::vector<EdgeId> edges = cmd.edges;
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    if (edges.empty()) {
        for (const TrackEdge& e : net.edges())
            if (e.owner == acting().id() && !e.electrified) edges.push_back(e.id);
        if (edges.empty()) return fail("all your track is already electrified");
    }
    Money cost;
    for (EdgeId id : edges) {
        if (id >= net.edges().size()) return fail("no such track");
        if (net.edge(id).owner != acting().id()) return fail("you can only electrify your own track");
        cost += electrify_cost(id);
    }
    if (auto why = cannot_afford(cost)) return fail(*why);
    acting().invest_track(cost);
    for (EdgeId id : edges) railway_.track().set_electrified(id, true);
    return success(cost, 0);
}

CommandResult World::run(const BuyTerritoryAccess& cmd) {
    if (cmd.territory >= territories_.territories.size()) return fail("no such territory");
    const Territory& t = territories_.territories[cmd.territory];
    if (t.open()) return fail(t.name + " is open to all");
    if (acting().has_access(cmd.territory)) return fail("you already have access to " + t.name);
    if (auto why = cannot_afford(t.access_cost)) return fail(*why);
    acting().post(Ledger::TerritoryFees, t.access_cost);
    acting().grant_access(cmd.territory, t.credit_grades);
    return success(t.access_cost, cmd.territory);
}

CommandResult World::run(const IssueBond&) {
    const Balance::Finance& f = data_.balance.finance;
    if (acting().bonds().size() >= static_cast<std::size_t>(f.max_bonds)) {
        return fail("the company already has the maximum of " + std::to_string(f.max_bonds) + " bonds");
    }
    if (!acting().can_issue_bond()) {
        return fail(std::string("credit rating ") + rating_name(acting().credit_rating()) +
                    " is too low: bonds need B or better");
    }
    acting().issue_bond(date_.year());
    // Cash raised, net of the fee; not counted as spending.
    CommandResult r = success(Money{}, static_cast<std::uint32_t>(acting().bonds().size() - 1));
    return r;
}

namespace {
CommandResult from(const std::optional<std::string>& error) {
    if (error) return fail(*error);
    return success(Money{}, 0);
}
} // namespace

CommandResult World::run(const BuyShares& cmd) { return from(buy_shares(market_, actor_, cmd.company, cmd.blocks)); }
CommandResult World::run(const SellShares& cmd) { return from(sell_shares(market_, actor_, cmd.company, cmd.blocks)); }
CommandResult World::run(const IssueStock&) { return from(issue_stock(acting())); }
CommandResult World::run(const BuyBackStock&) { return from(buy_back_stock(market_, acting().id())); }

CommandResult World::run(const SetDividend& cmd) {
    if (cmd.per_share < Money{}) return fail("a dividend cannot be negative");
    acting().set_dividend_per_share(cmd.per_share);
    return success(Money{}, 0);
}

CommandResult World::run(const RepayBond&) {
    if (acting().bonds().empty()) return fail("there are no bonds to repay");
    const Money due = Money::dollars(data_.balance.finance.bond_face_value)
                          .scaled(100 + data_.balance.finance.bond_early_repayment_percent, 100);
    if (!money_no_object() && acting().cash() < due) return fail("not enough cash to repay a bond (" + dollars(due) + ")");
    acting().repay_bond();
    return success(Money{}, 0);
}

std::optional<std::string> World::too_soon(CompanyId target) const {
    const auto it = failed_attempts_.find({actor_, target});
    if (it != failed_attempts_.end() &&
        date_.days_since_epoch() - it->second < data_.balance.corporate.retry_days) {
        return "after a failed attempt you must wait a year before trying again";
    }
    return std::nullopt;
}

void World::record_failure(CompanyId target) { failed_attempts_[{actor_, target}] = date_.days_since_epoch(); }

CommandResult World::run(const AttemptTakeover& cmd) {
    if (cmd.target >= market_.companies.size()) return fail("no such company");
    if (company(cmd.target).defunct()) return fail("that company no longer exists");
    Investor& bidder = market_.investors[actor_];
    if (bidder.chairs == cmd.target) return fail("you already run that company");
    std::optional<PlayerId> incumbent;
    for (std::size_t i = 0; i < market_.investors.size(); ++i) {
        if (market_.investors[i].chairs == cmd.target) incumbent = static_cast<PlayerId>(i);
    }
    // The human always runs a company: one with nothing to swap cannot take theirs [I].
    if (incumbent == kHumanPlayer && !bidder.chairs) return fail("you have no company to offer in exchange");
    if (auto why = too_soon(cmd.target)) return fail(*why);

    const Vote v = takeover_vote(market_, actor_, cmd.target, data_.balance.corporate);
    if (!v.passed()) {
        record_failure(cmd.target);
        return fail("the shareholders voted no (" + std::to_string(v.percent_yes()) + "% in favour)");
    }
    // The winner takes the chair; the ousted chairman takes the winner's old seat, if any [I].
    const std::optional<CompanyId> old = bidder.chairs;
    bidder.chairs = cmd.target;
    if (incumbent) market_.investors[*incumbent].chairs = old;
    note_player_company();
    return success(Money{}, cmd.target);
}

CommandResult World::run(const AttemptMerger& cmd) {
    Company& buyer = acting();
    if (cmd.target >= market_.companies.size()) return fail("no such company");
    const Company& target = company(cmd.target);
    if (target.defunct()) return fail("that company no longer exists");
    if (target.id() == buyer.id()) return fail("a company cannot merge with itself");
    if (actor_ != kHumanPlayer && target.id() == company().id()) {
        return fail("the player's own company cannot be merged away"); // [I] for now
    }
    if (cmd.offer_per_share <= Money{}) return fail("the offer must be above nothing");
    if (auto why = too_soon(cmd.target)) return fail(*why);
    const std::int64_t own = std::max<std::int64_t>(0, market_.investors[actor_].shares_in(cmd.target));
    const Money cost = cmd.offer_per_share * (target.shares_outstanding() - own);
    if (!money_no_object() && cost > buyer.cash()) {
        return fail("buying out the other shareholders costs " + dollars(cost) + "; the company has " +
                    dollars(buyer.cash()));
    }

    const Vote v = merger_vote(market_, actor_, cmd.target, cmd.offer_per_share, data_.balance.corporate);
    if (!v.passed()) {
        record_failure(cmd.target);
        return fail("the shareholders voted no (" + std::to_string(v.percent_yes()) + "% in favour)");
    }
    merge(buyer, cmd.target, cmd.offer_per_share);
    return success(cost, cmd.target);
}

CommandResult World::run(const Resign&) {
    Investor& me = market_.investors[actor_];
    if (!me.chairs) return fail("you do not run a company");
    if (!chairman_can_resign_) return fail("the chairman may not resign in this game");
    const CompanyId c = *me.chairs;
    me.chairs.reset();
    news_.push_back(me.name + " has resigned as chairman of " + company(c).name());
    appoint_chairman(c, actor_);
    return success(Money{}, c);
}

CommandResult World::run(const FoundCompany& cmd) {
    if (auto why = founding_problem(actor_, cmd)) return fail(*why);
    const CompanyId id = found_for(actor_, cmd);
    news_.push_back(market_.investors[actor_].name + " has founded " + company(id).name());
    return success(Money{}, id);
}

CommandResult World::run(const DeclareBankruptcy&) {
    Company& c = acting();
    if (auto why = c.bankruptcy_problem()) return fail(*why);
    c.declare_bankruptcy();
    news_.push_back(c.name() + " has declared bankruptcy");
    return success(Money{}, c.id());
}

CommandResult World::run(const BuyIndustry& cmd) {
    if (cmd.site >= economy_.sites().size()) return fail("no such industry");
    Site& s = economy_.site_mut(cmd.site);
    const IndustryType& t = data_.industries.get(s.type);
    if (!ownable(t.kind)) return fail("only producers and processing plants can be bought");
    if (s.closed) return fail("that industry has closed");
    if (s.owner) {
        return fail(s.owner == acting().id() ? "your company already owns it"
                                             : "it belongs to " + company(*s.owner).name());
    }
    const Money price = industry_price(s, data_.balance.industries);
    if (auto why = cannot_afford(price)) return fail(*why);
    acting().invest_industry(price);
    s.owner = acting().id();
    s.owner_paid = price;
    return success(price, s.id);
}

CommandResult World::run(const BuildIndustry& cmd) {
    if (cmd.type >= data_.industries.all().size()) return fail("unknown industry");
    const IndustryType& t = data_.industries.get(cmd.type);
    if (!buildable(t.kind)) return fail("only processing plants and warehouses can be built; producers can only be bought");
    // A warehouse trades whatever of its cargo exists; a plant needs its product to.
    for (CargoId c : t.kind == IndustryKind::Warehouse ? std::vector<CargoId>{} : t.outputs) {
        const CargoType& ct = data_.cargo.get(c);
        if (date_.year() < ct.available_year) return fail(ct.name + " is not made until " + std::to_string(ct.available_year));
    }
    if (cmd.cx < 0 || cmd.cy < 0 || cmd.cx >= economy_.width() || cmd.cy >= economy_.height()) {
        return fail("that is off the map");
    }
    if (terrain_.revision() != economy_terrain_revision_) refresh_economy_terrain();
    if (economy_.water(cmd.cx, cmd.cy)) return fail("industries need dry land");
    if (auto why = access_problem(acting().id(), economy_.node_centre(cmd.cx, cmd.cy))) return fail(*why);
    for (const Site& s : economy_.sites()) {
        if (!s.closed && s.cx == cmd.cx && s.cy == cmd.cy) return fail("something is already built there");
    }
    const Money cost = construction_cost(industry_build_cost(data_.balance.industries));
    if (auto why = cannot_afford(cost)) return fail(*why);
    acting().invest_industry(cost);
    const SiteId id = economy_.add_site(data_.industries, cmd.type, cmd.cx, cmd.cy);
    Site& s = economy_.site_mut(id);
    s.owner = acting().id();
    s.owner_paid = cost;
    return success(cost, id);
}

CommandResult World::run(const UpgradeIndustry& cmd) {
    if (cmd.site >= economy_.sites().size()) return fail("no such industry");
    Site& s = economy_.site_mut(cmd.site);
    if (s.owner != acting().id()) return fail("you can only upgrade your own industries");
    if (!buildable(data_.industries.get(s.type).kind)) return fail("only processing plants and warehouses can be upgraded");
    if (s.closed) return fail("that industry has closed");
    const Money cost = construction_cost(industry_upgrade_cost(s, data_.balance.industries));
    if (auto why = cannot_afford(cost)) return fail(*why);
    acting().invest_industry(cost);
    s.owner_paid += cost;
    s.level *= 2; // doubles capacity, and with it the overhead [D]
    return success(cost, s.id);
}

CommandResult World::run(const BuildStationBuilding& cmd) {
    if (cmd.pos.x_mm < 0 || cmd.pos.y_mm < 0 || cmd.pos.x_mm >= terrain_.width_mm() || cmd.pos.y_mm >= terrain_.height_mm()) {
        return fail("that is off the map");
    }
    if (terrain_.ground_at_mm(cmd.pos) == GroundType::Water) return fail("it needs dry land");
    bool near = false;
    for (const Station& s : railway_.stations()) {
        near |= distance_mm(railway_.track().node(s.node).pos, cmd.pos) <= data_.balance.stations.building_range_mm;
    }
    if (!near) return fail("it must be near a station");
    if (auto why = access_problem(acting().id(), cmd.pos)) return fail(*why);
    const Money cost = construction_cost(station_building_cost(cmd.type, data_.balance));
    if (auto why = cannot_afford(cost)) return fail(*why);
    acting().invest_buildings(cost);
    return success(cost, railway_.add_station_building(cmd.type, cmd.pos, acting().id()));
}

CommandResult World::run(const SetPortMode& cmd) {
    if (cmd.site >= economy_.sites().size()) return fail("no such industry");
    Site& s = economy_.site_mut(cmd.site);
    if (s.kind != IndustryKind::Warehouse) return fail("only warehouses can be set");
    if (s.owner != acting().id()) return fail("you can only set your own warehouses");
    s.port_mode = cmd.mode;
    return success(Money{}, s.id);
}

void World::merge(Company& buyer, CompanyId tid, Money offer) {
    Company& target = company(tid);
    std::int64_t paid_shares = target.shares_outstanding();
    for (std::size_t i = 0; i < market_.investors.size(); ++i) {
        Investor& inv = market_.investors[i];
        const std::int64_t h = inv.shares_in(tid);
        if (h > 0 && i == actor_) {
            // The bidder's own stake is exchanged for new shares of the buyer, at market value [I].
            const std::int64_t granted = (offer * h).in_cents() / std::max<std::int64_t>(1, buyer.share_price().in_cents());
            buyer.grant_shares(granted);
            inv.add_shares(buyer.id(), granted);
            paid_shares -= h;
        } else if (h > 0) {
            inv.cash += offer * h;
        } else if (h < 0) {
            inv.cash -= offer * -h; // shorts are closed at the offer price
        }
        inv.add_shares(tid, -h);
        if (inv.chairs == tid) inv.chairs.reset();
    }
    buyer.pay_for_acquisition(offer * paid_shares);
    buyer.absorb(target);
    railway_.transfer_owner(tid, buyer.id());
    for (const Site& s : economy_.sites()) {
        if (s.owner == tid) economy_.site_mut(s.id).owner = buyer.id(); // industries go too [C]
    }
}

} // namespace railmaster::sim
