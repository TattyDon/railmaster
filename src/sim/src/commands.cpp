#include "railmaster/sim/commands.hpp"

#include "railmaster/sim/world.hpp"

#include <stdexcept>

namespace railmaster::sim {

namespace {

CommandResult fail(std::string why) {
    CommandResult r;
    r.error = std::move(why);
    return r;
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

std::vector<MapPoint> run_points(const BuildTrack& cmd) {
    constexpr std::int64_t piece = provisional::kDefaultPieceMm;
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
    return plan_track_between(terrain_, cmd.start.pos, anchor_z(net, terrain_, cmd.start), run_points(cmd), end_z,
                              options_for(cmd, date_.year()));
}

CommandResult World::execute(const Command& cmd) {
    CommandResult r = std::visit([this](const auto& c) { return run(c); }, cmd);
    if (r.ok) spent_ += r.cost;
    return r;
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

CommandResult World::run(const BuildTrack& cmd) {
    // Validate everything before changing anything.
    const PlanResult check = preview(cmd);
    if (!check.plan) return fail(check.error);

    const NodeId from = resolve_on_track(cmd.start);
    const NodeId to = resolve_on_track(cmd.end);
    // The end node already exists now (even for open ground), so plan into it.
    std::vector<MapPoint> points = run_points(cmd);
    PlanResult plan = plan_track(railway_.track(), terrain_, from, std::move(points), to, options_for(cmd, date_.year()));
    if (!plan.plan) throw std::logic_error("track plan changed between check and build: " + plan.error);
    build_track(railway_.track(), *plan.plan);
    return success(plan.plan->total_cost, to);
}

CommandResult World::run(const BuildStation& cmd) {
    if (cmd.at.kind == TrackEnd::Kind::Free) return fail("stations must be built on track");
    if (cmd.at.kind == TrackEnd::Kind::Node) {
        for (const Station& s : railway_.stations()) {
            if (s.node == cmd.at.node) return fail("there is already a station here");
        }
    }
    const NodeId node = resolve_on_track(cmd.at);
    std::string name = cmd.name.empty() ? "Station " + std::to_string(railway_.stations().size() + 1) : cmd.name;
    const StationId id = railway_.add_station(std::move(name), node, cmd.size);
    return success(station_cost(cmd.size), id);
}

CommandResult World::run(const BuildServiceBuilding& cmd) {
    if (cmd.at.kind == TrackEnd::Kind::Free) return fail("support buildings must be built on track");
    if (cmd.at.kind == TrackEnd::Kind::Node) {
        for (const ServiceBuilding& b : railway_.service_buildings()) {
            if (b.node == cmd.at.node && b.type == cmd.type) return fail("there is already one here");
        }
    }
    const NodeId node = resolve_on_track(cmd.at);
    const ServiceBuildingId id = railway_.add_service_building(cmd.type, node);
    return success(service_building_cost(cmd.type), id);
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
    const TrainId id = railway_.add_train(cmd.loco, cmd.cars, cmd.route, cmd.priority);
    return success(loco.cost, id);
}

} // namespace railmaster::sim
