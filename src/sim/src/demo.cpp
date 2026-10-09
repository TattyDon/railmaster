#include "railmaster/sim/demo.hpp"

#include <algorithm>
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

} // namespace

std::string build_demo_network(World& world) {
    const auto& towns = world.economy().towns();
    if (towns.size() < 3) throw std::runtime_error("demo network: fewer than three towns on the map");
    const auto centre = [&](const Town& t) { return world.economy().node_centre(t.cx, t.cy); };
    std::vector<std::size_t> order(towns.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin() + 1, order.end(), [&](std::size_t a, std::size_t b) {
        return distance_mm(centre(towns[a]), centre(towns[0])) < distance_mm(centre(towns[b]), centre(towns[0]));
    });
    const Town* picked[3] = {&towns[order[0]], &towns[order[1]], &towns[order[2]]};
    const MapPoint at[3] = {centre(*picked[0]), centre(*picked[1]), centre(*picked[2])};
    const TrackNetwork& net = world.railway().track();

    NodeId nodes[3];
    nodes[1] = must(world, BuildTrack{.start = on_ground(at[0]), .end = on_ground(at[1])}).created_id;
    nodes[0] = *net.nearest_node(at[0], 1);
    nodes[2] = must(world, BuildTrack{.start = at_node(world, nodes[1]), .end = on_ground(at[2])}).created_id;
    must(world, BuildTrack{.start = at_node(world, nodes[2]), .end = at_node(world, nodes[0])});

    StationId st[3];
    for (int i = 0; i < 3; ++i) {
        st[i] = must(world, BuildStation{.at = at_node(world, nodes[i]), .name = picked[i]->name}).created_id;
        must(world, BuildServiceBuilding{.at = at_node(world, nodes[i])});
    }
    must(world, BuildServiceBuilding{.at = at_node(world, nodes[0]), .type = ServiceType::MaintenanceFacility});

    LocoTypeId loco = 0;
    for (const auto& l : world.data().locomotives.all()) {
        if (l.available_in(world.date().year())) {
            loco = l.id;
            break;
        }
    }
    must(world, BuyTrain{.loco = loco, .cars = 4, .route = {st[0], st[1], st[2]}, .priority = 1});
    must(world, BuyTrain{.loco = loco, .cars = 4, .route = {st[0], st[2], st[1]}});
    return "Demo network joining " + picked[0]->name + ", " + picked[1]->name + " and " + picked[2]->name +
           " built for $" + std::to_string(world.total_spent().whole_dollars());
}


} // namespace railmaster::sim
