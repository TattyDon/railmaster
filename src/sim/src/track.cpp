#include "railmaster/sim/track.hpp"

#include "railmaster/sim/terrain.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace railmaster::sim {

NodeId TrackNetwork::add_node(MapPoint pos, std::int64_t z_mm) {
    const auto id = static_cast<NodeId>(nodes_.size());
    nodes_.push_back({id, pos, z_mm});
    adjacency_.emplace_back();
    return id;
}

EdgeId TrackNetwork::add_edge(NodeId a, NodeId b, bool double_track, TrackKind kind) {
    if (a == b) throw std::invalid_argument("track edge must join two different nodes");
    const std::int64_t len = distance_mm(node(a).pos, node(b).pos);
    if (len <= 0) throw std::invalid_argument("track edge must have positive length");
    const auto id = static_cast<EdgeId>(edges_.size());
    edges_.push_back({id, a, b, len, double_track, kind});
    adjacency_[a].push_back(id);
    adjacency_[b].push_back(id);
    return id;
}

NodeId TrackNetwork::step_start(PathStep s) const {
    const TrackEdge& e = edge(s.edge);
    return s.forward ? e.a : e.b;
}

NodeId TrackNetwork::step_end(PathStep s) const {
    const TrackEdge& e = edge(s.edge);
    return s.forward ? e.b : e.a;
}

std::int32_t TrackNetwork::grade_bp(PathStep s) const {
    const TrackEdge& e = edge(s.edge);
    const std::int64_t rise = node(step_end(s)).z_mm - node(step_start(s)).z_mm;
    return static_cast<std::int32_t>(rise * 10000 / e.length_mm);
}

std::optional<std::vector<PathStep>> TrackNetwork::shortest_path(NodeId from, NodeId to) const {
    if (from >= nodes_.size() || to >= nodes_.size()) throw std::out_of_range("path endpoint out of range");
    if (from == to) return std::vector<PathStep>{};

    constexpr std::int64_t kInf = std::numeric_limits<std::int64_t>::max();
    std::vector<std::int64_t> dist(nodes_.size(), kInf);
    std::vector<std::optional<PathStep>> via(nodes_.size());

    // (distance, node): std::greater pops the smallest distance, then smallest id.
    using Entry = std::pair<std::int64_t, NodeId>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    dist[from] = 0;
    open.push({0, from});

    while (!open.empty()) {
        const auto [d, n] = open.top();
        open.pop();
        if (d != dist[n]) continue;
        if (n == to) break;
        for (EdgeId eid : adjacency_[n]) {
            const TrackEdge& e = edges_[eid];
            const NodeId m = e.other(n);
            const std::int64_t nd = d + e.length_mm;
            if (nd < dist[m]) {
                dist[m] = nd;
                via[m] = PathStep{eid, e.a == n};
                open.push({nd, m});
            }
        }
    }

    if (dist[to] == kInf) return std::nullopt;
    std::vector<PathStep> path;
    for (NodeId n = to; n != from;) {
        const PathStep s = *via[n];
        path.push_back(s);
        n = step_start(s);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

namespace {

// Lay pieces from `from` towards `to`, creating intermediate nodes at ground
// height. The last piece ends at `end_node` if given, else at a new node.
NodeId lay_pieces(TrackNetwork& net, const Terrain& terrain, NodeId from, MapPoint to,
                  std::optional<NodeId> end_node, std::int64_t max_piece_mm, bool double_track) {
    if (max_piece_mm <= 0) throw std::invalid_argument("piece length must be positive");
    const MapPoint start = net.node(from).pos;
    const std::int64_t total = distance_mm(start, to);
    const std::int64_t pieces = std::max<std::int64_t>(1, (total + max_piece_mm - 1) / max_piece_mm);

    NodeId prev = from;
    for (std::int64_t i = 1; i <= pieces; ++i) {
        NodeId n;
        if (i == pieces && end_node) {
            n = *end_node;
        } else {
            const MapPoint p{start.x_mm + (to.x_mm - start.x_mm) * i / pieces,
                             start.y_mm + (to.y_mm - start.y_mm) * i / pieces};
            n = net.add_node(p, terrain.height_at_mm(p));
        }
        net.add_edge(prev, n, double_track);
        prev = n;
    }
    return prev;
}

} // namespace

NodeId lay_track_following_ground(TrackNetwork& net, const Terrain& terrain, NodeId from, MapPoint to,
                                  std::int64_t max_piece_mm, bool double_track) {
    return lay_pieces(net, terrain, from, to, std::nullopt, max_piece_mm, double_track);
}

void connect_following_ground(TrackNetwork& net, const Terrain& terrain, NodeId from, NodeId to,
                              std::int64_t max_piece_mm, bool double_track) {
    lay_pieces(net, terrain, from, net.node(to).pos, to, max_piece_mm, double_track);
}

} // namespace railmaster::sim
