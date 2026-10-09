#pragma once

#include "railmaster/sim/fixed_math.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace railmaster::sim {

using NodeId = std::uint32_t;
using EdgeId = std::uint32_t;

// RT3 has no tile grid for track: lines run at any angle and curve freely.
// We model the network as a graph of points joined by straight pieces.
// A curve is a chain of short pieces, so the simulation only ever deals
// with straight segments while the renderer can smooth them.
struct TrackNode {
    NodeId id = 0;
    MapPoint pos;
    std::int64_t z_mm = 0; // rail height; differs from the ground on bridges and in tunnels
};

enum class TrackKind : std::uint8_t { Ground, Bridge, Tunnel };

// RT3's bridges [D, rt3-clone-spec §11.2]: wood (until 1865, cheapest,
// single track only, a big slowdown); stone (until 1865, double OK); steel
// (1865 on, double OK, a small slowdown); suspension (1895 on, very
// expensive, used automatically for long water spans).
enum class BridgeType : std::uint8_t { None, Wood, Stone, Steel, Suspension };

struct TrackEdge {
    EdgeId id = 0;
    NodeId a = 0;
    NodeId b = 0;
    std::int64_t length_mm = 0; // horizontal length
    bool double_track = false;
    TrackKind kind = TrackKind::Ground;
    BridgeType bridge = BridgeType::None; // set exactly when kind == Bridge

    NodeId other(NodeId n) const { return n == a ? b : a; }
};

// One step of a route: an edge plus the direction it is travelled in.
struct PathStep {
    EdgeId edge = 0;
    bool forward = true; // true = a -> b

    bool operator==(const PathStep&) const = default;
};

// A point on a piece of track, e.g. where the player clicked.
struct EdgePoint {
    EdgeId edge = 0;
    MapPoint pos;
    std::int64_t along_mm = 0; // distance from edge.a
};

class TrackNetwork {
public:
    NodeId add_node(MapPoint pos, std::int64_t z_mm);
    // Throws if a bridge has no bridge type (or a non-bridge has one), or
    // if a wooden bridge is asked to carry double track.
    EdgeId add_edge(NodeId a, NodeId b, bool double_track = false, TrackKind kind = TrackKind::Ground,
                    BridgeType bridge = BridgeType::None);

    const TrackNode& node(NodeId id) const { return nodes_.at(id); }
    const TrackEdge& edge(EdgeId id) const { return edges_.at(id); }
    const std::vector<TrackNode>& nodes() const { return nodes_; }
    const std::vector<TrackEdge>& edges() const { return edges_; }
    const std::vector<EdgeId>& edges_at(NodeId id) const { return adjacency_.at(id); }

    // Rail height at a point along an edge, interpolated between its ends.
    std::int64_t rail_z_at(EdgeId e, MapPoint at) const;

    // Nearest node within `max_mm` of `p`, ties to the lowest id.
    std::optional<NodeId> nearest_node(MapPoint p, std::int64_t max_mm) const;
    // Nearest point on any piece of track within `max_mm` of `p`.
    std::optional<EdgePoint> nearest_edge_point(MapPoint p, std::int64_t max_mm) const;

    // Insert a node part-way along an edge, so new track can branch there.
    // Edge `e` keeps its id and becomes a -> new node; a new edge (returned
    // through `second`) runs new node -> b with the same properties. Throws if
    // `at` is not strictly between the ends. Callers that hold paths over `e`
    // must patch them (Railway::split_edge does this for trains).
    NodeId split_edge(EdgeId e, MapPoint at, EdgeId* second = nullptr);

    // Upgrade or downgrade a piece. Throws for a wooden bridge.
    void set_double_track(EdgeId id, bool value);

    NodeId step_start(PathStep s) const;
    NodeId step_end(PathStep s) const;

    // Grade of a step in basis points (hundredths of a percent); positive is uphill.
    std::int32_t grade_bp(PathStep s) const;

    // Shortest path by track length. Deterministic: ties are broken by
    // node id. Returns an empty vector if from == to, nullopt if unreachable.
    std::optional<std::vector<PathStep>> shortest_path(NodeId from, NodeId to) const;

private:
    std::vector<TrackNode> nodes_;
    std::vector<TrackEdge> edges_;
    std::vector<std::vector<EdgeId>> adjacency_;
};

} // namespace railmaster::sim
