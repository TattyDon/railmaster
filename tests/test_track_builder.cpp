#include "railmaster/sim/terrain.hpp"
#include "railmaster/sim/track_builder.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000; // millimetres

Terrain flat_terrain() { return Terrain(20, 20, 1000); }

// A ridge 100 m high along x = 10 km with 5% sides, reaching 0 at x = 8 and x = 12 km.
Terrain ridge_terrain() {
    Terrain t(20, 20, 1000);
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 20; ++x) t.set_corner_height(x, y, std::max(0, 100 - 50 * std::abs(x - 10)));
    return t;
}

// Flat ground with a lake covering tiles x = 8..11.
Terrain lake_terrain() {
    Terrain t(20, 20, 1000);
    for (int y = 0; y < 20; ++y)
        for (int x = 8; x <= 11; ++x) t.set_ground(x, y, GroundType::Water);
    return t;
}

std::size_t count_kind(const TrackPlan& p, TrackKind k) {
    return static_cast<std::size_t>(
        std::count_if(p.pieces.begin(), p.pieces.end(), [k](const PlannedPiece& pc) { return pc.kind == k; }));
}

} // namespace

TEST_CASE("straight points are evenly spaced and end exactly") {
    const auto pts = straight_points({0, 0}, {10 * kKm, 0}, 3 * kKm);
    REQUIRE(pts.size() == 4);
    CHECK(pts.back() == MapPoint{10 * kKm, 0});
    CHECK(pts.front().x_mm == 2'500'000);
}

TEST_CASE("curve points bend towards the control point and stay within piece length") {
    const MapPoint a{0, 0}, c{5 * kKm, 5 * kKm}, b{10 * kKm, 0};
    const auto pts = curve_points(a, c, b, kKm / 2);
    CHECK(pts.back() == b);
    const MapPoint mid = pts[pts.size() / 2 - 1];
    CHECK(mid.y_mm > 2 * kKm); // bulges towards the control point
    MapPoint prev = a;
    for (const MapPoint& p : pts) {
        CHECK(distance_mm(prev, p) <= kKm / 2);
        prev = p;
    }
}

TEST_CASE("a continuing curve leaves in the direction the track was heading") {
    TrackNetwork net;
    const NodeId a = net.add_node({0, 0}, 0);
    const NodeId b = net.add_node({kKm, 0}, 0);
    net.add_edge(a, b);
    const auto c = continuing_control_point(net, b, {5 * kKm, 4 * kKm});
    REQUIRE(c.has_value());
    CHECK(c->y_mm == 0);         // straight on from the existing track
    CHECK(c->x_mm > kKm);
    CHECK_FALSE(continuing_control_point(net, net.add_node({9 * kKm, 9 * kKm}, 0), {0, 0}).has_value());
}

TEST_CASE("on flat land the plan is all ground track and builds as planned") {
    const Terrain terrain = flat_terrain();
    TrackNetwork net;
    const NodeId start = net.add_node({kKm, kKm}, 0);
    auto r = plan_track(net, terrain, start, straight_points({kKm, kKm}, {6 * kKm, kKm}, kKm), std::nullopt, {});
    REQUIRE(r.plan.has_value());
    CHECK(r.plan->pieces.size() == 5);
    CHECK(count_kind(*r.plan, TrackKind::Ground) == 5);
    CHECK(r.plan->total_cost == Money::dollars(5 * default_balance().track.ground_per_km));

    const NodeId end = build_track(net, *r.plan);
    CHECK(net.node(end).pos == MapPoint{6 * kKm, kKm});
    CHECK(net.shortest_path(start, end)->size() == 5);
}

TEST_CASE("double track costs more") {
    const Terrain terrain = flat_terrain();
    TrackNetwork net;
    const NodeId start = net.add_node({kKm, kKm}, 0);
    const auto pts = straight_points({kKm, kKm}, {6 * kKm, kKm}, kKm);
    TrackBuildOptions dbl;
    dbl.double_track = true;
    const auto single = plan_track(net, terrain, start, pts, std::nullopt, {});
    const auto twin = plan_track(net, terrain, start, pts, std::nullopt, dbl);
    // More than single, less than twice [D].
    CHECK(twin.plan->total_cost > single.plan->total_cost);
    CHECK(twin.plan->total_cost < single.plan->total_cost * 2);
}

TEST_CASE("a ridge is tunnelled, keeping the grade under the limit") {
    const Terrain terrain = ridge_terrain();
    TrackNetwork net;
    const NodeId start = net.add_node({2 * kKm, 5 * kKm}, 0);
    TrackBuildOptions opts;
    opts.tunnel_preference = 100;
    auto r = plan_track(net, terrain, start, straight_points({2 * kKm, 5 * kKm}, {18 * kKm, 5 * kKm}, kKm / 2),
                        std::nullopt, opts);
    REQUIRE(r.plan.has_value());
    CHECK(count_kind(*r.plan, TrackKind::Tunnel) > 0);
    CHECK(count_kind(*r.plan, TrackKind::Bridge) == 0);

    const NodeId end = build_track(net, *r.plan);
    const auto path = net.shortest_path(start, end);
    for (const PathStep& s : *path) CHECK(std::abs(net.grade_bp(s)) <= default_balance().track.default_max_grade_bp);
}

TEST_CASE("with tunnels disallowed the line climbs over the ridge") {
    const Terrain terrain = ridge_terrain();
    TrackNetwork net;
    const NodeId start = net.add_node({2 * kKm, 5 * kKm}, 0);
    TrackBuildOptions opts;
    opts.tunnel_preference = 0;
    auto r = plan_track(net, terrain, start, straight_points({2 * kKm, 5 * kKm}, {18 * kKm, 5 * kKm}, kKm / 2),
                        std::nullopt, opts);
    REQUIRE(r.plan.has_value());
    CHECK(count_kind(*r.plan, TrackKind::Tunnel) == 0);
    CHECK(*std::max_element(r.plan->rail_z_mm.begin(), r.plan->rail_z_mm.end()) > 80'000);
}

TEST_CASE("the default blend tunnels the crest and stays within the grade limit") {
    const Terrain terrain = ridge_terrain();
    TrackNetwork net;
    const NodeId start = net.add_node({2 * kKm, 5 * kKm}, 0);
    auto r = plan_track(net, terrain, start, straight_points({2 * kKm, 5 * kKm}, {18 * kKm, 5 * kKm}, kKm / 2),
                        std::nullopt, {});
    REQUIRE(r.plan.has_value());
    CHECK(count_kind(*r.plan, TrackKind::Tunnel) > 0);
    const NodeId end = build_track(net, *r.plan);
    const auto path = net.shortest_path(start, end);
    REQUIRE(path.has_value());
    for (const PathStep& s : *path) CHECK(std::abs(net.grade_bp(s)) <= default_balance().track.default_max_grade_bp);
}

TEST_CASE("water is bridged: wood for single track, steel or stone for double") {
    const Terrain terrain = lake_terrain();
    TrackNetwork net;
    const NodeId start = net.add_node({2 * kKm, 5 * kKm}, 0);
    const auto pts = straight_points({2 * kKm, 5 * kKm}, {16 * kKm, 5 * kKm}, kKm);

    auto single = plan_track(net, terrain, start, pts, std::nullopt, {});
    REQUIRE(single.plan.has_value());
    CHECK(count_kind(*single.plan, TrackKind::Bridge) >= 4);
    for (const auto& p : single.plan->pieces)
        if (p.kind == TrackKind::Bridge) CHECK(p.bridge == BridgeType::Wood);

    TrackBuildOptions dbl;
    dbl.double_track = true;
    dbl.year = 1850;
    const auto early = plan_track(net, terrain, start, pts, std::nullopt, dbl);
    REQUIRE(early.plan.has_value());
    for (const auto& p : early.plan->pieces)
        if (p.kind == TrackKind::Bridge) CHECK(p.bridge == BridgeType::Stone);
    dbl.year = 1870; // steel's era, before suspension bridges (1895)
    const auto later = plan_track(net, terrain, start, pts, std::nullopt, dbl);
    REQUIRE(later.plan.has_value());
    for (const auto& p : later.plan->pieces)
        if (p.kind == TrackKind::Bridge) CHECK(p.bridge == BridgeType::Steel);
}

TEST_CASE("impossible bridge requests are refused with a reason") {
    const Terrain terrain = lake_terrain();
    TrackNetwork net;
    const NodeId start = net.add_node({2 * kKm, 5 * kKm}, 0);
    const auto pts = straight_points({2 * kKm, 5 * kKm}, {16 * kKm, 5 * kKm}, kKm);

    TrackBuildOptions wood_double;
    wood_double.double_track = true;
    wood_double.bridge_type = BridgeType::Wood;
    auto r = plan_track(net, terrain, start, pts, std::nullopt, wood_double);
    CHECK_FALSE(r.plan.has_value());
    CHECK_FALSE(r.error.empty());

    TrackBuildOptions early_steel;
    early_steel.year = 1840;
    early_steel.bridge_type = BridgeType::Steel;
    CHECK_FALSE(plan_track(net, terrain, start, pts, std::nullopt, early_steel).plan.has_value());

    CHECK_FALSE(plan_track(net, terrain, start, straight_points({2 * kKm, 5 * kKm}, {9 * kKm, 5 * kKm}, kKm),
                           std::nullopt, {})
                    .plan.has_value()); // would end in the lake
}

TEST_CASE("bridge eras: wood and stone until 1865, steel from 1865, suspension from 1895") {
    CHECK(bridge_available(BridgeType::Wood, 1865));
    CHECK_FALSE(bridge_available(BridgeType::Wood, 1866));
    CHECK_FALSE(bridge_available(BridgeType::Stone, 1866));
    CHECK_FALSE(bridge_available(BridgeType::Steel, 1864));
    CHECK(bridge_available(BridgeType::Steel, 1865));
    CHECK_FALSE(bridge_available(BridgeType::Suspension, 1894));
    CHECK(bridge_available(BridgeType::Suspension, 1895));
}

TEST_CASE("long water crossings get suspension bridges once available") {
    // A lake 4 km wide (tiles 8..11): long enough for a suspension bridge.
    Terrain terrain = lake_terrain();
    TrackNetwork net;
    const NodeId start = net.add_node({2 * kKm, 5 * kKm}, 0);
    const auto pts = straight_points({2 * kKm, 5 * kKm}, {16 * kKm, 5 * kKm}, kKm);
    TrackBuildOptions later;
    later.year = 1900;
    const auto r = plan_track(net, terrain, start, pts, std::nullopt, later);
    REQUIRE(r.plan.has_value());
    bool any = false;
    for (const auto& p : r.plan->pieces) any |= p.bridge == BridgeType::Suspension;
    CHECK(any);
    later.bridge_type = BridgeType::Steel; // the player's choice wins
    const auto chosen = plan_track(net, terrain, start, pts, std::nullopt, later);
    REQUIRE(chosen.plan.has_value());
    for (const auto& p : chosen.plan->pieces) CHECK(p.bridge != BridgeType::Suspension);
}

TEST_CASE("bridge prices follow the researched order: wood < steel < stone") {
    CHECK(default_balance().track.wood_bridge_multiple < default_balance().track.steel_bridge_multiple);
    CHECK(default_balance().track.steel_bridge_multiple < default_balance().track.stone_bridge_multiple);
}

TEST_CASE("a run can finish on an existing node") {
    const Terrain terrain = flat_terrain();
    TrackNetwork net;
    const NodeId a = net.add_node({kKm, kKm}, 0);
    const NodeId b = net.add_node({5 * kKm, kKm}, 0);
    auto r = plan_track(net, terrain, a, curve_points({kKm, kKm}, {3 * kKm, 4 * kKm}, {5 * kKm, kKm}, kKm), b, {});
    REQUIRE(r.plan.has_value());
    CHECK(build_track(net, *r.plan) == b);
    CHECK(net.shortest_path(a, b).has_value());

    // The last point must actually be the end node.
    CHECK_FALSE(plan_track(net, terrain, a, straight_points({kKm, kKm}, {4 * kKm, kKm}, kKm), b, {}).plan);
}
