#pragma once

#include "railmaster/sim/fixed_math.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace railmaster::sim {

class Terrain;

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

struct TrackEdge {
    EdgeId id = 0;
    NodeId a = 0;
    NodeId b = 0;
    std::int64_t length_mm = 0; // horizontal length
    bool double_track = false;
    TrackKind kind = TrackKind::Ground;

    NodeId other(NodeId n) const { return n == a ? b : a; }
};

// One step of a route: an edge plus the direction it is travelled in.
struct PathStep {
    EdgeId edge = 0;
    bool forward = true; // true = a -> b

    bool operator==(const PathStep&) const = default;
};

class TrackNetwork {
public:
    NodeId add_node(MapPoint pos, std::int64_t z_mm);
    EdgeId add_edge(NodeId a, NodeId b, bool double_track = false, TrackKind kind = TrackKind::Ground);

    const TrackNode& node(NodeId id) const { return nodes_.at(id); }
    const TrackEdge& edge(EdgeId id) const { return edges_.at(id); }
    const std::vector<TrackNode>& nodes() const { return nodes_; }
    const std::vector<TrackEdge>& edges() const { return edges_; }
    const std::vector<EdgeId>& edges_at(NodeId id) const { return adjacency_.at(id); }

    void set_double_track(EdgeId id, bool value) { edges_.at(id).double_track = value; }

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

// Lay a straight run of ground-level track from `from` to a new point,
// split into pieces no longer than `max_piece_mm`, with each node at the
// terrain height so the line follows the ground. Returns the end node.
NodeId lay_track_following_ground(TrackNetwork& net, const Terrain& terrain, NodeId from, MapPoint to,
                                  std::int64_t max_piece_mm, bool double_track = false);

// As above, but ending at an existing node, e.g. to join two lines.
void connect_following_ground(TrackNetwork& net, const Terrain& terrain, NodeId from, NodeId to,
                              std::int64_t max_piece_mm, bool double_track = false);

} // namespace railmaster::sim
