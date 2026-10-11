#include "railmaster/sim/track.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace railmaster::sim {

namespace {

// Which side of line ab point p lies on: +1, -1, or 0 on the line.
int side(MapPoint a, MapPoint b, MapPoint p) {
    const std::int64_t c = (b.x_mm - a.x_mm) * (p.y_mm - a.y_mm) - (b.y_mm - a.y_mm) * (p.x_mm - a.x_mm);
    return (c > 0) - (c < 0);
}

} // namespace

std::optional<MapPoint> segment_crossing(MapPoint a, MapPoint b, MapPoint c, MapPoint d) {
    const int ca = side(c, d, a), cb = side(c, d, b), ac = side(a, b, c), ad = side(a, b, d);
    if (ca == 0 || cb == 0 || ac == 0 || ad == 0 || ca == cb || ac == ad) return std::nullopt;
    // Halve ab until the crossing is pinned to a millimetre: integer only,
    // so every machine finds the same point.
    MapPoint lo = a, hi = b;
    for (int i = 0; i < 64 && std::max(std::abs(hi.x_mm - lo.x_mm), std::abs(hi.y_mm - lo.y_mm)) > 1; ++i) {
        const MapPoint mid{lo.x_mm + (hi.x_mm - lo.x_mm) / 2, lo.y_mm + (hi.y_mm - lo.y_mm) / 2};
        if (side(c, d, mid) == ca) lo = mid;
        else hi = mid;
    }
    return lo;
}

std::optional<MapPoint> segment_meeting(MapPoint a, MapPoint b, MapPoint c, MapPoint d) {
    const int ca = side(c, d, a), cb = side(c, d, b), ac = side(a, b, c), ad = side(a, b, d);
    if (ca == 0 && cb == 0) return std::nullopt; // along the same line
    if (ca * cb > 0 || ac * ad > 0) return std::nullopt;
    const auto within = [](MapPoint p, MapPoint s, MapPoint e) {
        return p.x_mm >= std::min(s.x_mm, e.x_mm) && p.x_mm <= std::max(s.x_mm, e.x_mm) &&
               p.y_mm >= std::min(s.y_mm, e.y_mm) && p.y_mm <= std::max(s.y_mm, e.y_mm);
    };
    if (ca == 0) return within(a, c, d) ? std::optional<MapPoint>{a} : std::nullopt;
    if (cb == 0) return within(b, c, d) ? std::optional<MapPoint>{b} : std::nullopt;
    if (ac == 0) return within(c, a, b) ? std::optional<MapPoint>{c} : std::nullopt;
    if (ad == 0) return within(d, a, b) ? std::optional<MapPoint>{d} : std::nullopt;
    return segment_crossing(a, b, c, d);
}

NodeId TrackNetwork::add_node(MapPoint pos, std::int64_t z_mm) {
    const auto id = static_cast<NodeId>(nodes_.size());
    nodes_.push_back({id, pos, z_mm});
    adjacency_.emplace_back();
    return id;
}

EdgeId TrackNetwork::add_edge(NodeId a, NodeId b, bool double_track, TrackKind kind, BridgeType bridge,
                              CompanyId owner) {
    if (a == b) throw std::invalid_argument("track edge must join two different nodes");
    if ((kind == TrackKind::Bridge) != (bridge != BridgeType::None)) {
        throw std::invalid_argument("bridge type must be set for bridges and only for bridges");
    }
    if (double_track && bridge == BridgeType::Wood) throw std::invalid_argument("wooden bridges are single track");
    const std::int64_t len = distance_mm(node(a).pos, node(b).pos);
    if (len <= 0) throw std::invalid_argument("track edge must have positive length");
    const auto id = static_cast<EdgeId>(edges_.size());
    edges_.push_back({id, a, b, len, double_track, kind, bridge, owner});
    adjacency_[a].push_back(id);
    adjacency_[b].push_back(id);
    return id;
}

std::optional<NodeId> TrackNetwork::nearest_node(MapPoint p, std::int64_t max_mm) const {
    std::optional<NodeId> best;
    std::int64_t best_d = max_mm + 1;
    for (const TrackNode& n : nodes_) {
        const std::int64_t d = distance_mm(p, n.pos);
        if (d < best_d) { // strict, so ties keep the lowest id
            best = n.id;
            best_d = d;
        }
    }
    return best;
}

std::optional<EdgePoint> TrackNetwork::nearest_edge_point(MapPoint p, std::int64_t max_mm) const {
    std::optional<EdgePoint> best;
    std::int64_t best_d = max_mm + 1;
    for (const TrackEdge& e : edges_) {
        if (e.removed) continue;
        const MapPoint a = node(e.a).pos, b = node(e.b).pos;
        const std::int64_t dx = b.x_mm - a.x_mm, dy = b.y_mm - a.y_mm;
        const std::int64_t len = e.length_mm;
        // Project p onto the segment as a distance along it, clamped to its
        // ends. Working in distance keeps every product within 64 bits.
        const std::int64_t dot = (p.x_mm - a.x_mm) * dx + (p.y_mm - a.y_mm) * dy;
        const std::int64_t along = std::clamp<std::int64_t>(dot / len, 0, len);
        const MapPoint q{a.x_mm + dx * along / len, a.y_mm + dy * along / len};
        const std::int64_t d = distance_mm(p, q);
        if (d < best_d) {
            best_d = d;
            best = EdgePoint{e.id, q, along};
        }
    }
    return best;
}

std::int64_t TrackNetwork::rail_z_at(EdgeId id, MapPoint at) const {
    const TrackEdge& e = edge(id);
    const TrackNode& na = node(e.a);
    const TrackNode& nb = node(e.b);
    const std::int64_t da = distance_mm(na.pos, at);
    const std::int64_t db = distance_mm(at, nb.pos);
    if (da + db == 0) return na.z_mm;
    return na.z_mm + (nb.z_mm - na.z_mm) * da / (da + db);
}

NodeId TrackNetwork::split_edge(EdgeId id, MapPoint at, EdgeId* second) {
    const TrackEdge old = edge(id);
    const TrackNode& na = node(old.a);
    const TrackNode& nb = node(old.b);
    const std::int64_t da = distance_mm(na.pos, at);
    const std::int64_t db = distance_mm(at, nb.pos);
    if (da <= 0 || db <= 0) throw std::invalid_argument("split point must lie strictly inside the edge");
    const NodeId mid = add_node(at, rail_z_at(id, at));

    TrackEdge& first = edges_[id];
    first.b = mid;
    first.length_mm = da;
    first.cost = old.cost.scaled(da, da + db);
    const auto new_id = static_cast<EdgeId>(edges_.size());
    edges_.push_back(
        {new_id, mid, old.b, db, old.double_track, old.kind, old.bridge, old.owner, old.electrified, old.cost - first.cost});
    std::replace(adjacency_[old.b].begin(), adjacency_[old.b].end(), id, new_id);
    adjacency_[mid] = {id, new_id};
    if (second) *second = new_id;
    return mid;
}

void TrackNetwork::remove_edge(EdgeId id) {
    TrackEdge& e = edges_.at(id);
    if (e.removed) return;
    e.removed = true;
    for (const NodeId n : {e.a, e.b}) {
        auto& adj = adjacency_[n];
        adj.erase(std::remove(adj.begin(), adj.end(), id), adj.end());
    }
}

void TrackNetwork::transfer_owner(CompanyId from, CompanyId to) {
    for (TrackEdge& e : edges_)
        if (e.owner == from) e.owner = to;
}

void TrackNetwork::set_double_track(EdgeId id, bool value) {
    TrackEdge& e = edges_.at(id);
    if (value && e.bridge == BridgeType::Wood) throw std::invalid_argument("wooden bridges are single track");
    e.double_track = value;
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

std::optional<std::vector<PathStep>> TrackNetwork::shortest_path(NodeId from, NodeId to, bool electric_only) const {
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
            if (electric_only && !e.electrified) continue;
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

} // namespace railmaster::sim
