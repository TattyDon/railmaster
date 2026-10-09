#include "railmaster/sim/terrain.hpp"
#include "railmaster/sim/track.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

namespace {
constexpr std::int64_t kKm = 1'000'000; // millimetres
}

TEST_CASE("edges measure horizontal length and directional grade") {
    TrackNetwork net;
    const NodeId a = net.add_node({0, 0}, 0);
    const NodeId b = net.add_node({3 * kKm, 4 * kKm}, 50'000); // 5 km away, 50 m higher
    const EdgeId e = net.add_edge(a, b);
    CHECK(net.edge(e).length_mm == 5 * kKm);
    CHECK(net.grade_bp({e, true}) == 100);   // 1.00% uphill
    CHECK(net.grade_bp({e, false}) == -100); // downhill the other way
    CHECK(net.step_start({e, false}) == b);
    CHECK(net.step_end({e, false}) == a);
}

TEST_CASE("degenerate edges are rejected") {
    TrackNetwork net;
    const NodeId a = net.add_node({0, 0}, 0);
    const NodeId b = net.add_node({0, 0}, 0);
    CHECK_THROWS(net.add_edge(a, a));
    CHECK_THROWS(net.add_edge(a, b));
}

TEST_CASE("shortest path picks the shorter of two routes") {
    // a --(long detour via c)-- b, and a direct a--b edge.
    TrackNetwork net;
    const NodeId a = net.add_node({0, 0}, 0);
    const NodeId b = net.add_node({10 * kKm, 0}, 0);
    const NodeId c = net.add_node({5 * kKm, 8 * kKm}, 0);
    net.add_edge(a, c);
    net.add_edge(c, b);
    const EdgeId direct = net.add_edge(a, b);

    auto path = net.shortest_path(a, b);
    REQUIRE(path.has_value());
    REQUIRE(path->size() == 1);
    CHECK((*path)[0] == PathStep{direct, true});

    auto back = net.shortest_path(b, a);
    REQUIRE(back.has_value());
    CHECK((*back)[0] == PathStep{direct, false});
}

TEST_CASE("path is empty for same node and absent when disconnected") {
    TrackNetwork net;
    const NodeId a = net.add_node({0, 0}, 0);
    const NodeId b = net.add_node({kKm, 0}, 0);
    const NodeId island = net.add_node({5 * kKm, 5 * kKm}, 0);
    net.add_edge(a, b);
    CHECK(net.shortest_path(a, a)->empty());
    CHECK_FALSE(net.shortest_path(a, island).has_value());
}

TEST_CASE("laid track follows the ground in bounded pieces") {
    Terrain terrain(10, 10, 1000);
    for (int y = 0; y <= 10; ++y)
        for (int x = 0; x <= 10; ++x) terrain.set_corner_height(x, y, x * 10); // 1% slope rising east

    TrackNetwork net;
    const NodeId start = net.add_node({0, 0}, terrain.height_at_mm({0, 0}));
    const NodeId end = lay_track_following_ground(net, terrain, start, {8 * kKm, 0}, kKm);
    CHECK(net.edges().size() == 8);
    CHECK(net.node(end).pos == MapPoint{8 * kKm, 0});
    CHECK(net.node(end).z_mm == 80'000);
    for (const TrackEdge& e : net.edges()) CHECK(net.grade_bp({e.id, true}) == 100);
}

TEST_CASE("connecting two existing nodes reuses both ends") {
    Terrain terrain(10, 10, 1000);
    TrackNetwork net;
    const NodeId a = net.add_node({0, 0}, 0);
    const NodeId b = net.add_node({5 * kKm, 0}, 0);
    connect_following_ground(net, terrain, a, b, kKm);
    CHECK(net.edges().size() == 5);
    CHECK(net.nodes().size() == 6); // a, b and four in between
    const auto path = net.shortest_path(a, b);
    REQUIRE(path.has_value());
    CHECK(path->size() == 5);
}

TEST_CASE("terrain height interpolates between corners") {
    Terrain terrain(2, 2, 1000);
    terrain.set_corner_height(1, 0, 10);
    CHECK(terrain.height_at_mm({0, 0}) == 0);
    CHECK(terrain.height_at_mm({kKm, 0}) == 10'000);
    CHECK(terrain.height_at_mm({kKm / 2, 0}) == 5'000);
    CHECK(terrain.height_at_mm({-kKm, -kKm}) == 0); // clamped
}
