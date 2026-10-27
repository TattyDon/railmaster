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

TrackBuildOptions options_for(const BuildTrack& cmd, std::int32_t year, const Balance& b) {
    TrackBuildOptions o;
    o.year = year;
    o.double_track = cmd.double_track;
    o.tunnel_preference = structure_preference(cmd.tunnels, b);
    o.bridge_preference = structure_preference(cmd.bridges, b);
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
    PlanResult r = priced(plan_track_between(terrain_, cmd.start.pos, anchor_z(net, terrain_, cmd.start),
                                             run_points(cmd, data_.balance), end_z, options_for(cmd, date_.year(), data_.balance),
                                             data_.balance));
    if (r.plan) mark_crossings(*r.plan, cmd.start.pos, cmd.overpasses);
    return r;
}

void World::mark_crossings(TrackPlan& plan, MapPoint start, Frequency overpasses) const {
    const TrackNetwork& net = railway_.track();
    plan.crossings.clear();
    if (plan.points.empty()) return;
    const MapPoint finish = plan.points.back();
    MapPoint prev = start;
    for (std::size_t i = 0; i < plan.pieces.size(); ++i) {
        const MapPoint next = plan.points[i];
        // Bridges and tunnels already pass at another level.
        if (plan.pieces[i].kind == TrackKind::Ground) {
            for (const TrackEdge& e : net.edges()) {
                if (e.removed || e.kind != TrackKind::Ground) continue;
                const auto at = segment_meeting(prev, next, net.node(e.a).pos, net.node(e.b).pos);
                // Where the run starts or ends on track it joins rather than crosses it.
                if (!at || *at == start || *at == finish) continue;
                // One crossing per place, though pieces of either line may meet there twice.
                const bool known = std::any_of(plan.crossings.begin(), plan.crossings.end(), [&](const TrackPlan::Crossing& c) {
                    return std::abs(c.at.x_mm - at->x_mm) <= 2 && std::abs(c.at.y_mm - at->y_mm) <= 2;
                });
                if (!known) plan.crossings.push_back({i, e.id, *at, overpass_wanted(overpasses, plan.double_track, e.double_track)});
            }
        }
        prev = next;
    }
    std::vector<bool> lifted(plan.pieces.size(), false);
    for (const TrackPlan::Crossing& c : plan.crossings) {
        if (!c.overpass || lifted[c.piece]) continue;
        lifted[c.piece] = true;
        PlannedPiece& p = plan.pieces[c.piece];
        const Money extra = construction_cost(
            Money::dollars(data_.balance.track.ground_per_km * (data_.balance.track.overpass_multiple - 1))
                .scaled(p.length_mm, 1'000'000));
        p.cost += extra;
        plan.total_cost += extra;
    }
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
    if (who == kLocalPlayer) who = local_;
    if (who >= market_.investors.size()) return fail("unknown player");
    if (watch_only_ && !ticking_) return fail("this game is a replay being watched");
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
    // Commands from outside the simulation, for replays; failed ones change nothing.
    if (r.ok && !ticking_) command_log_.push_back({total_ticks_, who, cmd});
    return r;
}

CommandResult World::play_logged(const LoggedCommand& c) {
    const bool watching = watch_only_;
    watch_only_ = false;
    CommandResult r = execute(c.cmd, c.who);
    watch_only_ = watching;
    return r;
}

Company& World::acting() { return market_.companies.at(*market_.investors.at(actor_).chairs); }
const Company& World::acting() const { return market_.companies.at(*market_.investors.at(actor_).chairs); }

bool World::money_no_object() const { return sandbox_ && human(actor_); }

bool World::owns_track_at(const TrackEnd& at) const {
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
    // Scenario restrictions (rt3-clone-spec §14.3).
    std::int64_t tiles = 0;
    if (scenario_) {
        const Restrictions& r = scenario_->restrictions;
        if (r.no_track) return fail("this scenario does not allow building track");
        if (r.no_unconnected_track && !owns_track_at(cmd.start) && !owns_track_at(cmd.end)) {
            const bool has_track = std::any_of(railway_.track().edges().begin(), railway_.track().edges().end(),
                                               [&](const TrackEdge& e) { return !e.removed && e.owner == acting().id(); });
            if (has_track) return fail("new track must join your own track in this scenario");
        }
        if (r.track_tiles) {
            for (const PlannedPiece& p : check.plan->pieces) tiles += p.length_mm;
            const std::int64_t cell = std::int64_t{terrain_.tile_size_m()} * 1000;
            tiles = (tiles + cell - 1) / cell * (cmd.double_track ? 2 : 1);
            const std::int64_t left = track_tiles(acting().id());
            if (tiles > left) {
                return fail("needs " + std::to_string(tiles) + " track tiles; the budget has " + std::to_string(left) +
                            " left");
            }
        }
    }

    const auto first_new_node = static_cast<NodeId>(railway_.track().nodes().size());
    const NodeId from = resolve_on_track(cmd.start);
    const NodeId to = resolve_on_track(cmd.end);
    // The end node already exists now (even for open ground), so plan into it.
    std::vector<MapPoint> points = run_points(cmd, data_.balance);
    PlanResult plan = priced(plan_track(railway_.track(), terrain_, from, std::move(points), to,
                                        options_for(cmd, date_.year(), data_.balance), data_.balance));
    if (!plan.plan) throw std::logic_error("track plan changed between check and build: " + plan.error);
    mark_crossings(*plan.plan, railway_.track().node(from).pos, cmd.overpasses);
    TrackUndo undo;
    undo.first_new_node = first_new_node;
    undo.tiles = tiles;
    if (tiles > 0) track_tiles(acting().id()) -= tiles;
    build_track(railway_.track(), *plan.plan, acting().id(), &undo.edges);
    for (EdgeId e : undo.edges) undo.ends.emplace_back(railway_.track().edge(e).a, railway_.track().edge(e).b);
    for (const TrackPlan::Crossing& c : plan.plan->crossings) {
        railway_.add_crossing({c.edge, undo.edges[c.piece], c.at, c.overpass});
    }
    acting().invest_track(plan.plan->total_cost);
    if (track_undo_.size() <= actor_) track_undo_.resize(std::size_t{actor_} + 1);
    auto& stack = track_undo_[actor_];
    stack.push_back(std::move(undo));
    const auto depth = static_cast<std::size_t>(std::max(0, data_.balance.track.undo_depth));
    if (stack.size() > depth) stack.erase(stack.begin(), stack.end() - static_cast<std::ptrdiff_t>(depth));
    return success(plan.plan->total_cost, to);
}

CommandResult World::run(const RemoveTrack& cmd) {
    std::vector<EdgeId> edges = cmd.edges;
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    if (edges.empty()) return fail("no track chosen");
    const TrackNetwork& net = railway_.track();
    Money book;
    for (EdgeId id : edges) {
        if (id >= net.edges().size() || net.edge(id).removed) return fail("no such track");
        if (net.edge(id).owner != acting().id()) return fail("you can only remove your own track");
        if (railway_.train_on(id)) return fail("a train is on that track");
        book += net.edge(id).cost;
    }
    const Money refund = book.scaled(data_.balance.track.remove_refund_percent, 100);
    if (scenario_ && scenario_->restrictions.track_tiles && scenario_->restrictions.tile_refund) {
        std::int64_t length = 0;
        for (EdgeId id : edges) length += net.edge(id).length_mm * (net.edge(id).double_track ? 2 : 1);
        track_tiles(acting().id()) += length / (std::int64_t{terrain_.tile_size_m()} * 1000);
    }
    railway_.remove_edges(edges);
    acting().remove_track(book, refund);
    return success(Money{} - refund, 0);
}

CommandResult World::run(const UndoTrack&) {
    if (actor_ >= track_undo_.size() || track_undo_[actor_].empty()) return fail("nothing to undo");
    const TrackUndo& last = track_undo_[actor_].back();
    const TrackNetwork& net = railway_.track();
    Money cost;
    for (std::size_t i = 0; i < last.edges.size(); ++i) {
        const TrackEdge& e = net.edge(last.edges[i]);
        if (e.removed || std::pair{e.a, e.b} != last.ends[i]) return fail("that track has been changed since");
        if (railway_.train_on(e.id)) return fail("a train is on that track");
        cost += e.cost;
    }
    const auto built_here = [&](NodeId n) { return n >= last.first_new_node; };
    for (const Station& st : railway_.stations())
        if (built_here(st.node)) return fail("a station stands on that track");
    for (const ServiceBuilding& b : railway_.service_buildings())
        if (built_here(b.node)) return fail("a support building stands on that track");
    railway_.remove_edges(last.edges);
    acting().remove_track(cost, cost);
    if (last.tiles > 0) track_tiles(acting().id()) += last.tiles; // undo is free [D]
    track_undo_[actor_].pop_back();
    return success(Money{} - cost, 0);
}

Money World::double_upgrade_cost(EdgeId id) const {
    const TrackEdge& e = railway_.track().edge(id);
    const Balance::Track& b = data_.balance.track;
    const Money laid = Money::dollars(per_km_rate(e.kind, e.bridge, b)).scaled(e.length_mm, 1'000'000);
    return construction_cost(laid.scaled(b.double_upgrade_percent, 100));
}

CommandResult World::run(const UpgradeTrack& cmd) {
    if (scenario_ && scenario_->restrictions.no_track) return fail("this scenario does not allow building track");
    std::vector<EdgeId> edges = cmd.edges;
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    if (edges.empty()) return fail("no track chosen");
    const TrackNetwork& net = railway_.track();
    Money cost;
    std::int64_t length = 0;
    for (EdgeId id : edges) {
        if (id >= net.edges().size() || net.edge(id).removed) return fail("no such track");
        const TrackEdge& e = net.edge(id);
        if (e.owner != acting().id()) return fail("you can only double your own track");
        if (e.double_track) return fail("that track is already double");
        if (e.bridge == BridgeType::Wood) return fail("wooden bridges are single track");
        cost += double_upgrade_cost(id);
        length += e.length_mm;
    }
    std::int64_t tiles = 0;
    if (scenario_ && scenario_->restrictions.track_tiles) {
        const std::int64_t cell = std::int64_t{terrain_.tile_size_m()} * 1000;
        tiles = (length + cell - 1) / cell;
        if (tiles > track_tiles(acting().id())) {
            return fail("needs " + std::to_string(tiles) + " track tiles; the budget has " +
                        std::to_string(track_tiles(acting().id())) + " left");
        }
    }
    if (auto why = cannot_afford(cost)) return fail(*why);
    acting().invest_track(cost);
    track_tiles(acting().id()) -= tiles;
    for (EdgeId id : edges) {
        railway_.track().add_cost(id, double_upgrade_cost(id));
        railway_.track().set_double_track(id, true);
    }
    return success(cost, 0);
}

void World::remember_build(std::uint8_t kind, std::uint32_t id, MapPoint at, Money cost) {
    if (build_undo_.size() <= actor_) build_undo_.resize(std::size_t{actor_} + 1);
    build_undo_[actor_].push_back({kind, id, at, cost});
}

void World::forget_builds() {
    for (auto& stack : build_undo_) stack.clear();
}

CommandResult World::run(const UndoBuild&) {
    if (actor_ >= build_undo_.size() || build_undo_[actor_].empty()) return fail("nothing to undo");
    const BuildUndo last = build_undo_[actor_].back();
    const CompanyId me = acting().id();
    switch (last.kind) {
    case 0: {
        if (last.id >= railway_.stations().size()) return fail("that station has gone");
        const Station& st = railway_.station(last.id);
        if (st.owner != me || railway_.track().node(st.node).pos != last.at) return fail("that station has been changed since");
        for (const Train& t : railway_.trains()) {
            if (t.in_service() && std::find(t.route.begin(), t.route.end(), last.id) != t.route.end()) {
                return fail("Train " + std::to_string(t.id + 1) + " already stops there");
            }
        }
        railway_.remove_station(last.id);
        for (Rival& r : rivals_) {
            for (RivalRoute& route : r.routes) {
                if (route.a > last.id) --route.a;
                if (route.b > last.id) --route.b;
            }
        }
        break;
    }
    case 1: {
        if (last.id >= railway_.service_buildings().size()) return fail("that building has gone");
        const ServiceBuilding& b = railway_.service_buildings()[last.id];
        if (b.owner != me || railway_.track().node(b.node).pos != last.at) return fail("that building has been changed since");
        railway_.remove_service_building(last.id);
        break;
    }
    default: {
        if (last.id >= railway_.station_buildings().size()) return fail("that building has gone");
        const StationBuilding& b = railway_.station_buildings()[last.id];
        if (b.owner != me || b.pos != last.at) return fail("that building has been changed since");
        railway_.remove_station_building(last.id);
        break;
    }
    }
    acting().remove_buildings(last.cost, last.cost); // undo is free, as for track [D]
    build_undo_[actor_].pop_back();
    return success(Money{} - last.cost, 0);
}

CommandResult World::run(const EndBuildUndo&) {
    if (actor_ < build_undo_.size()) build_undo_[actor_].clear();
    return success(Money{}, 0);
}

CommandResult World::run(const EndTrackUndo&) {
    if (actor_ < track_undo_.size()) track_undo_[actor_].clear();
    return success(Money{}, 0);
}

std::string World::station_name_for(MapPoint p) const {
    const auto taken = [&](const std::string& n) {
        for (const Station& s : railway_.stations())
            if (s.name == n) return true;
        return false;
    };
    // The nearest town centre (by the larger of the two offsets, as a town's reach is measured).
    std::optional<std::size_t> nearest;
    std::int64_t best = 0;
    for (std::size_t t = 0; t < economy_.towns().size(); ++t) {
        const MapPoint c = economy_.node_centre(economy_.towns()[t].cx, economy_.towns()[t].cy);
        const std::int64_t d = std::max(std::abs(c.x_mm - p.x_mm), std::abs(c.y_mm - p.y_mm));
        if (!nearest || d < best) {
            nearest = t;
            best = d;
        }
    }
    if (!nearest) {
        std::string n = "Station " + std::to_string(railway_.stations().size() + 1);
        for (std::size_t i = railway_.stations().size() + 2; taken(n); ++i) n = "Station " + std::to_string(i);
        return n;
    }
    const Town& town = economy_.towns()[*nearest];
    std::vector<std::string> names;
    if (best <= economy_.cells_mm(data_.balance.stations.town_reach_cells)) {
        const MapPoint c = economy_.node_centre(town.cx, town.cy);
        const std::int64_t dx = p.x_mm - c.x_mm, dy = p.y_mm - c.y_mm;
        const char* side = std::abs(dx) >= std::abs(dy) ? (dx >= 0 ? " East" : " West") : (dy >= 0 ? " South" : " North");
        names = {town.name, town.name + side, town.name + " Junction"};
    } else {
        names = {town.name + " Junction", town.name + " Crossing", town.name + " Siding"};
    }
    for (const std::string& n : names)
        if (!taken(n)) return n;
    for (int i = 2;; ++i) {
        const std::string n = names.back() + " " + std::to_string(i);
        if (!taken(n)) return n;
    }
}

std::optional<std::string> World::station_problem(const BuildStation& cmd) const {
    if (scenario_ && scenario_->restrictions.no_stations) return "this scenario does not allow building stations";
    if (cmd.at.kind == TrackEnd::Kind::Free) return "stations must be built on track";
    if (!owns_track_at(cmd.at)) return "stations must be built on your own track";
    if (cmd.at.kind == TrackEnd::Kind::Node) {
        for (const Station& s : railway_.stations()) {
            if (s.node == cmd.at.node) return "there is already a station here";
        }
    }
    if (auto why = access_problem(acting().id(), cmd.at.pos)) return why;
    return cannot_afford(station_build_cost(cmd));
}

Money World::station_build_cost(const BuildStation& cmd) const {
    // A territory may make stations dearer [C].
    const TerritoryId terr = territory_at(cmd.at.pos);
    const std::int64_t terr_pct =
        terr < territories_.territories.size() ? territories_.territories[terr].station_cost_percent : 100;
    return construction_cost(station_cost(cmd.size, data_.balance)).scaled(terr_pct, 100);
}

CommandResult World::run(const BuildStation& cmd) {
    if (auto why = station_problem(cmd)) return fail(*why);
    const Money cost = station_build_cost(cmd);
    const NodeId node = resolve_on_track(cmd.at);
    acting().invest_buildings(cost);
    std::string name = cmd.name.empty() ? station_name_for(railway_.track().node(node).pos) : cmd.name;
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
    remember_build(0, id, railway_.track().node(node).pos, cost);
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
    const Money cost = construction_cost(service_building_cost(cmd.type, data_.balance));
    remember_build(1, id, railway_.track().node(node).pos, cost);
    return success(cost, id);
}

CommandResult World::run(const BuyTrain& cmd) {
    if (scenario_ && scenario_->restrictions.no_trains) return fail("this scenario does not allow buying trains");
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
    if (scenario_ && scenario_->restrictions.no_trains) return fail("this scenario does not allow buying engines");
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

CommandResult World::run(const SetRoute& cmd) {
    if (auto why = own_train_problem(cmd.train)) return fail(*why);
    if (cmd.route.size() < 2) return fail("a route needs at least two stops");
    if (cmd.service_stops.size() > cmd.route.size()) return fail("more service stops than stops");
    for (std::size_t i = 0; i < cmd.route.size(); ++i) {
        if (cmd.route[i] >= railway_.stations().size()) return fail("unknown station in route");
        if (cmd.route[i] == cmd.route[(i + 1) % cmd.route.size()]) return fail("a route cannot call at the same station twice running");
    }
    const Train& t = railway_.train(cmd.train);
    if (data_.locomotives.get(t.loco).fuel == Fuel::Electric) {
        if (auto why = railway_.electric_route_problem(cmd.route)) return fail(*why);
    }
    std::vector<std::uint8_t> service(cmd.route.size(), 0);
    for (std::size_t i = 0; i < cmd.service_stops.size(); ++i) service[i] = cmd.service_stops[i] ? 1 : 0;
    railway_.set_route(cmd.train, cmd.route, std::move(service));
    return success(Money{}, cmd.train);
}

CommandResult World::run(const SetPriority& cmd) {
    if (auto why = own_train_problem(cmd.train)) return fail(*why);
    railway_.train_mut(cmd.train).priority = cmd.priority;
    return success(Money{}, cmd.train);
}

CommandResult World::run(const CopyTrain& cmd) {
    if (scenario_ && scenario_->restrictions.no_trains) return fail("this scenario does not allow buying trains");
    if (auto why = own_train_problem(cmd.train)) return fail(*why);
    const Train original = railway_.train(cmd.train);
    const CommandResult bought = run(BuyTrain{.loco = original.loco,
                                              .cars = static_cast<std::uint8_t>(original.slots_at(0)),
                                              .route = original.route,
                                              .priority = original.priority});
    if (!bought.ok) return bought;
    Train& copy = railway_.train_mut(bought.created_id);
    copy.rules = original.rules;
    copy.service_stop = original.service_stop;
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
            if (e.owner == acting().id() && !e.electrified && !e.removed) edges.push_back(e.id);
        if (edges.empty()) return fail("all your track is already electrified");
    }
    Money cost;
    for (EdgeId id : edges) {
        if (id >= net.edges().size() || net.edge(id).removed) return fail("no such track");
        if (net.edge(id).owner != acting().id()) return fail("you can only electrify your own track");
        cost += electrify_cost(id);
    }
    if (auto why = cannot_afford(cost)) return fail(*why);
    acting().invest_track(cost);
    for (EdgeId id : edges) {
        railway_.track().add_cost(id, electrify_cost(id)); // before wiring: then it is free
        railway_.track().set_electrified(id, true);
    }
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
    // A human always runs a company: one with nothing to swap cannot take theirs [I].
    if (incumbent && human(*incumbent) && !bidder.chairs) return fail("you have no company to offer in exchange");
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
    if (!human(actor_) && human_company(target.id())) {
        return fail("a player's own company cannot be merged away"); // [I] for now
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
    const std::string& who = market_.investors[actor_].name;
    if (who == "You") news_.push_back(who + " have founded " + company(id).name());
    else headline(who + " has founded " + company(id).name());
    return success(Money{}, id);
}

CommandResult World::run(const DeclareBankruptcy&) {
    Company& c = acting();
    if (auto why = c.bankruptcy_problem()) return fail(*why);
    c.declare_bankruptcy();
    headline(c.name() + " has declared bankruptcy");
    return success(Money{}, c.id());
}

CommandResult World::run(const BuyIndustry& cmd) {
    if (scenario_ && scenario_->restrictions.no_industries) return fail("this scenario does not allow buying industries");
    if (cmd.site >= economy_.sites().size()) return fail("no such industry");
    Site& s = economy_.site_mut(cmd.site);
    const IndustryType& t = data_.industries.get(s.type);
    if (!ownable(t.kind)) return fail("only producers, processing plants and ports can be bought");
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

std::optional<std::string> World::industry_problem(const BuildIndustry& cmd) const {
    if (scenario_ && scenario_->restrictions.no_industries) return "this scenario does not allow building industries";
    if (cmd.type >= data_.industries.all().size()) return "unknown industry";
    const IndustryType& t = data_.industries.get(cmd.type);
    if (!buildable(t.kind)) return "only processing plants and warehouses can be built; producers can only be bought";
    // A warehouse trades whatever of its cargo exists; a plant needs its product to.
    for (CargoId c : t.kind == IndustryKind::Warehouse ? std::vector<CargoId>{} : t.outputs) {
        const CargoType& ct = data_.cargo.get(c);
        if (date_.year() < ct.available_year) return ct.name + " is not made until " + std::to_string(ct.available_year);
    }
    if (cmd.cx < 0 || cmd.cy < 0 || cmd.cx >= economy_.width() || cmd.cy >= economy_.height()) return "that is off the map";
    if (economy_.water(cmd.cx, cmd.cy)) return "industries need dry land";
    if (auto why = access_problem(acting().id(), economy_.node_centre(cmd.cx, cmd.cy))) return why;
    for (const Site& s : economy_.sites()) {
        if (!s.closed && s.cx == cmd.cx && s.cy == cmd.cy) {
            return s.kind == IndustryKind::House ? "a town's houses stand there" : "something is already built there";
        }
    }
    // Footprints never overlap (rt3-clone-spec §3.1).
    const std::int32_t apart = 2 * economy_.footprint_ring();
    for (const Site& s : economy_.sites()) {
        if (s.closed || s.kind == IndustryKind::House) continue;
        if (std::max(std::abs(s.cx - cmd.cx), std::abs(s.cy - cmd.cy)) <= apart) return "too close to another industry";
    }
    return cannot_afford(construction_cost(industry_build_cost(data_.balance.industries)));
}

CommandResult World::run(const BuildIndustry& cmd) {
    if (terrain_.revision() != economy_terrain_revision_) refresh_economy_terrain();
    if (auto why = industry_problem(cmd)) return fail(*why);
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
    const IndustryKind kind = data_.industries.get(s.type).kind;
    // Owned ports no longer expand by themselves, so their owners may.
    if (!buildable(kind) && kind != IndustryKind::Port) {
        return fail("only processing plants, warehouses and ports can be upgraded");
    }
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
    const std::uint32_t id = railway_.add_station_building(cmd.type, cmd.pos, acting().id());
    remember_build(2, id, cmd.pos, cost);
    return success(cost, id);
}

CommandResult World::run(const SetPortMode& cmd) {
    if (cmd.site >= economy_.sites().size()) return fail("no such industry");
    Site& s = economy_.site_mut(cmd.site);
    if (s.kind != IndustryKind::Warehouse && s.kind != IndustryKind::Port) return fail("only warehouses and ports can be set");
    if (s.owner != acting().id()) return fail("you can only set your own warehouses and ports");
    if (cmd.mode == PortMode::Convert) {
        if (s.kind != IndustryKind::Warehouse) return fail("only warehouses convert cargo");
        const IndustryType& t = data_.industries.get(s.type);
        const bool takes = std::any_of(t.inputs.begin(), t.inputs.end(),
                                       [&](const IndustryInput& i) { return i.cargo == cmd.convert_from; });
        const bool makes = std::find(t.outputs.begin(), t.outputs.end(), cmd.convert_to) != t.outputs.end();
        if (!takes || !makes) return fail("a warehouse converts one of the cargos it takes into one it supplies");
        if (cmd.convert_from == cmd.convert_to) return fail("that would convert a cargo into itself");
        s.convert_from = cmd.convert_from;
        s.convert_to = cmd.convert_to;
    }
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

// The price of making a station `to` from `from`: the difference in their
// prices, at today's costs and its territory's.
Money World::station_upgrade_cost(StationId id) const {
    const Station& s = railway_.station(id);
    if (s.size == StationSize::Large) return Money{};
    const auto to = static_cast<StationSize>(static_cast<int>(s.size) + 1);
    const MapPoint at = railway_.track().node(s.node).pos;
    const TerritoryId terr = territory_at(at);
    const std::int64_t terr_pct =
        terr < territories_.territories.size() ? territories_.territories[terr].station_cost_percent : 100;
    return construction_cost(station_cost(to, data_.balance) - station_cost(s.size, data_.balance)).scaled(terr_pct, 100);
}

CommandResult World::run(const UpgradeStation& cmd) {
    if (cmd.station >= railway_.stations().size()) return fail("no such station");
    const Station& s = railway_.station(cmd.station);
    if (s.owner != acting().id()) return fail("you can only enlarge your own stations");
    if (s.size == StationSize::Large) return fail("it is already a large station");
    const Money cost = station_upgrade_cost(cmd.station);
    if (auto why = cannot_afford(cost)) return fail(*why);
    acting().invest_buildings(cost);
    Station& st = railway_.station_mut(cmd.station);
    st.size = static_cast<StationSize>(static_cast<int>(st.size) + 1);
    return success(cost, cmd.station);
}

CommandResult World::run(const Cheat& cmd) {
    const auto co = player_company(kHumanPlayer);
    switch (cmd.kind) {
    case CheatKind::PlayerMoney: investor(kHumanPlayer).cash += cmd.amount; break;
    case CheatKind::CompanyMoney:
        if (!co) return fail("you run no company");
        company_of(kHumanPlayer).post(Ledger::Grants, cmd.amount);
        break;
    case CheatKind::AllTerritories:
        if (!co) return fail("you run no company");
        for (std::size_t t = 0; t < territories_.territories.size(); ++t) {
            const Territory& terr = territories_.territories[t];
            if (!terr.open() && !company_of(kHumanPlayer).has_access(static_cast<TerritoryId>(t))) {
                company_of(kHumanPlayer).grant_access(static_cast<TerritoryId>(t), terr.credit_grades);
            }
        }
        break;
    case CheatKind::AllEngines:
        for (const LocomotiveType& l : base_locos_.all()) modifiers_.push_back({EffectKind::UnlockEngine, l.key, 100, 0});
        apply_modifiers();
        break;
    case CheatKind::DoubleProduction:
        for (const IndustryType& t : base_industries_.all()) modifiers_.push_back({EffectKind::Production, t.key, 200, 0});
        apply_modifiers();
        break;
    case CheatKind::DoubleSpeed: {
        OperatingRules rules = railway_.rules();
        rules.speed_percent = rules.speed_percent == 100 ? 200 : 100;
        railway_.set_rules(rules);
        break;
    }
    case CheatKind::NoCrashes: {
        OperatingRules rules = railway_.rules();
        rules.crashes = !rules.crashes;
        railway_.set_rules(rules);
        break;
    }
    case CheatKind::Crash:
        if (cmd.train >= railway_.trains().size()) return fail("no such train");
        if (!railway_.crash(cmd.train)) return fail("the train is not out on the line");
        break;
    case CheatKind::ForceWin:
    case CheatKind::ForceLoss:
        if (!scenario_) return fail("this game has no scenario");
        if (scenario_result_.finished) return fail("the scenario is already over");
        scenario_result_.finished = true;
        if (cmd.kind == CheatKind::ForceWin) {
            const auto medal = static_cast<Medal>(std::clamp(cmd.tier, 0, 2));
            scenario_result_.medal = medal;
            scenario_result_.won_on = date_;
            headline(std::string(medal_name(medal)) + " medal won: " + scenario_->name);
        } else {
            scenario_result_.medal.reset();
            scenario_result_.won_on.reset();
            headline(scenario_->name + ": the scenario is lost");
        }
        break;
    case CheatKind::BreakDown: {
        if (cmd.train >= railway_.trains().size()) return fail("no such train");
        const Balance::Breakdowns& b = data_.balance.breakdowns;
        if (!railway_.break_down(cmd.train, rng_.between(b.breakdown_min_days, b.breakdown_max_days))) {
            return fail("the train is not out on the line");
        }
        break;
    }
    }
    assisted_ = true;
    return success(Money{}, 0);
}

CommandResult World::run(const RemoveStation& cmd) {
    if (cmd.station >= railway_.stations().size()) return fail("no such station");
    const Station& st = railway_.station(cmd.station);
    if (st.owner != acting().id()) return fail("you can only take down your own stations");
    for (const Train& t : railway_.trains()) {
        if (!t.in_service() || std::find(t.route.begin(), t.route.end(), cmd.station) == t.route.end()) continue;
        return fail("Train " + std::to_string(t.id + 1) + " still stops there: change its route first (E)");
    }
    const Money book = station_cost(st.size, data_.balance);
    const Money refund = book.scaled(data_.balance.track.remove_refund_percent, 100);
    railway_.remove_station(cmd.station);
    forget_builds(); // ids above it have moved
    for (Rival& r : rivals_) {
        for (RivalRoute& route : r.routes) {
            if (route.a > cmd.station) --route.a;
            if (route.b > cmd.station) --route.b;
        }
    }
    acting().remove_buildings(book, refund);
    return success(Money{} - refund, 0);
}

CommandResult World::run(const RemoveServiceBuilding& cmd) {
    if (cmd.building >= railway_.service_buildings().size()) return fail("no such building");
    const ServiceBuilding& b = railway_.service_buildings()[cmd.building];
    if (b.owner != acting().id()) return fail("you can only take down your own buildings");
    const Money book = service_building_cost(b.type, data_.balance);
    const Money refund = book.scaled(data_.balance.track.remove_refund_percent, 100);
    railway_.remove_service_building(cmd.building);
    forget_builds();
    acting().remove_buildings(book, refund);
    return success(Money{} - refund, 0);
}

CommandResult World::run(const RenameStation& cmd) {
    if (cmd.station >= railway_.stations().size()) return fail("no such station");
    if (railway_.station(cmd.station).owner != acting().id()) return fail("you can only rename your own stations");
    // Trimmed of spaces at either end.
    const auto first = cmd.name.find_first_not_of(' ');
    const std::string name = first == std::string::npos ? "" : cmd.name.substr(first, cmd.name.find_last_not_of(' ') - first + 1);
    if (name.empty()) return fail("a station needs a name");
    if (name.size() > 30) return fail("names are at most 30 characters");
    for (const Station& s : railway_.stations()) {
        if (s.id != cmd.station && s.name == name) return fail("another station is called that");
    }
    railway_.station_mut(cmd.station).name = name;
    return success(Money{}, cmd.station);
}

CommandResult World::run(const AnswerChoice& cmd) {
    if (actor_ != kHumanPlayer) return fail("only the player answers scenario questions");
    if (cmd.choice >= pending_choices_.size()) return fail("no such question");
    const PendingChoice c = pending_choices_[cmd.choice];
    const EventEffect& f = choice_effect(c);
    if (cmd.option >= f.options.size()) return fail("no such answer");
    pending_choices_.erase(pending_choices_.begin() + static_cast<std::ptrdiff_t>(cmd.choice));
    apply_effects(f.options[cmd.option].effects, c.event, false);
    return success(Money{}, 0);
}

} // namespace railmaster::sim
