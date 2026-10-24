#include "railmaster/sim/demo.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <vector>

namespace railmaster::sim {

namespace {

CommandResult must(World& world, const Command& cmd) {
    CommandResult r = world.execute(cmd);
    if (!r.ok) throw std::runtime_error("demo network: " + r.error);
    return r;
}

TrackEnd at_node(const World& world, NodeId n) {
    return {TrackEnd::Kind::Node, n, 0, world.railway().track().node(n).pos};
}

TrackEnd on_ground(MapPoint p) { return {TrackEnd::Kind::Free, 0, 0, p}; }

// Plan a line the way a thrifty player would: of the routes with tunnels
// and bridges never, rarely or at the default frequency, the cheapest that
// can be built.
std::optional<TrackPlan> cheapest_route(const World& world, BuildTrack& cmd) {
    constexpr Frequency kTried[] = {Frequency::Never, Frequency::Rarely, Frequency::Average};
    std::optional<TrackPlan> cheapest;
    Frequency best_tunnels = cmd.tunnels, best_bridges = cmd.bridges;
    for (const Frequency tunnels : kTried) {
        for (const Frequency bridges : kTried) {
            cmd.tunnels = tunnels;
            cmd.bridges = bridges;
            if (PlanResult plan = world.preview(cmd); plan.plan && (!cheapest || plan.plan->total_cost < cheapest->total_cost)) {
                cheapest = std::move(plan.plan);
                best_tunnels = tunnels;
                best_bridges = bridges;
            }
        }
    }
    cmd.tunnels = best_tunnels;
    cmd.bridges = best_bridges;
    return cheapest;
}

// Build a line by its cheapest route, first buying access to any territory
// it crosses.
CommandResult build(World& world, BuildTrack cmd) {
    (void)cheapest_route(world, cmd);
    if (const PlanResult plan = world.preview(cmd); plan.plan) {
        std::vector<MapPoint> points = plan.plan->points;
        points.push_back(cmd.start.pos);
        for (TerritoryId t : world.access_needed(world.company().id(), points)) {
            must(world, BuyTerritoryAccess{.territory = t});
        }
    }
    return must(world, cmd);
}

} // namespace

std::string build_demo_network(World& world) {
    const auto& towns = world.economy().towns();
    if (towns.size() < 3) throw std::runtime_error("demo network: fewer than three towns on the map");
    const auto centre = [&](const Town& t) { return world.economy().node_centre(t.cx, t.cy); };
    // Each town with its two nearest neighbours is a candidate triangle. Take
    // the one with the most houses whose track and territory access cost at
    // most the share of the company's cash the balance allows, or failing
    // that the cheapest.
    const auto nearest_two = [&](std::size_t hub) {
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < towns.size(); ++i) {
            if (i != hub) order.push_back(i);
        }
        std::partial_sort(order.begin(), order.begin() + 2, order.end(), [&](std::size_t a, std::size_t b) {
            return distance_mm(centre(towns[a]), centre(towns[hub])) < distance_mm(centre(towns[b]), centre(towns[hub]));
        });
        return std::array<std::size_t, 3>{hub, order[0], order[1]};
    };
    const Money budget = world.company().cash().scaled(world.data().balance.demo.track_budget_percent, 100);
    std::optional<std::array<std::size_t, 3>> chosen, cheapest;
    std::size_t best_houses = 0;
    Money cheapest_cost;
    for (std::size_t hub = 0; hub < towns.size(); ++hub) {
        const auto tri = nearest_two(hub);
        Money cost;
        std::vector<MapPoint> points;
        bool buildable = true;
        for (std::size_t leg = 0; leg < 3; ++leg) {
            BuildTrack cmd{.start = on_ground(centre(towns[tri[leg]])), .end = on_ground(centre(towns[tri[(leg + 1) % 3]]))};
            const std::optional<TrackPlan> plan = cheapest_route(world, cmd);
            buildable = plan.has_value();
            if (!plan) break;
            cost += plan->total_cost;
            points.push_back(cmd.start.pos);
            points.insert(points.end(), plan->points.begin(), plan->points.end());
        }
        if (!buildable) continue;
        for (TerritoryId t : world.access_needed(world.company().id(), points)) {
            cost += world.territories().territories[t].access_cost;
        }
        if (!cheapest || cost < cheapest_cost) {
            cheapest = tri;
            cheapest_cost = cost;
        }
        const std::size_t houses = towns[tri[0]].houses.size() + towns[tri[1]].houses.size() + towns[tri[2]].houses.size();
        if (cost <= budget && (!chosen || houses > best_houses)) {
            chosen = tri;
            best_houses = houses;
        }
    }
    if (!chosen) chosen = cheapest;
    if (!chosen) throw std::runtime_error("demo network: no three towns can be joined");
    const Town* picked[3] = {&towns[(*chosen)[0]], &towns[(*chosen)[1]], &towns[(*chosen)[2]]};
    const MapPoint at[3] = {centre(*picked[0]), centre(*picked[1]), centre(*picked[2])};
    const TrackNetwork& net = world.railway().track();

    NodeId nodes[3];
    nodes[1] = build(world, BuildTrack{.start = on_ground(at[0]), .end = on_ground(at[1])}).created_id;
    nodes[0] = *net.nearest_node(at[0], 1);
    nodes[2] = build(world, BuildTrack{.start = at_node(world, nodes[1]), .end = on_ground(at[2])}).created_id;
    build(world, BuildTrack{.start = at_node(world, nodes[2]), .end = at_node(world, nodes[0])});

    StationId st[3];
    for (int i = 0; i < 3; ++i) {
        st[i] = must(world, BuildStation{.at = at_node(world, nodes[i]), .name = picked[i]->name}).created_id;
        must(world, BuildServiceBuilding{.at = at_node(world, nodes[i])});
    }
    must(world, BuildServiceBuilding{.at = at_node(world, nodes[0]), .type = ServiceType::MaintenanceFacility});

    // The fastest steam or diesel engine of the day hauling four cars up 1%,
    // the cheaper on a tie; the demo lines are not electrified.
    const std::int32_t year = world.date().year();
    std::optional<LocoTypeId> best;
    std::int64_t best_speed = 0;
    for (const auto& l : world.data().locomotives.all()) {
        if (!l.available_in(year) || l.fuel == Fuel::Electric) continue;
        const std::int64_t speed = chart_speed_mm_per_tick(l, 4, false, year, 100, world.data().balance);
        if (best && (speed < best_speed || (speed == best_speed && l.cost >= world.data().locomotives.get(*best).cost))) continue;
        best = l.id;
        best_speed = speed;
    }
    if (!best) throw std::runtime_error("demo network: no engine available");
    const LocoTypeId loco = *best;
    must(world, BuyTrain{.loco = loco, .cars = 4, .route = {st[0], st[1], st[2]}, .priority = 1});
    must(world, BuyTrain{.loco = loco, .cars = 4, .route = {st[0], st[2], st[1]}});
    must(world, EndTrackUndo{}); // the starting network is not the player's to undo
    return "Demo network joining " + picked[0]->name + ", " + picked[1]->name + " and " + picked[2]->name +
           " built for $" + std::to_string(world.total_spent().whole_dollars());
}


} // namespace railmaster::sim
