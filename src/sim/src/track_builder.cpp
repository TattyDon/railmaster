#include "railmaster/sim/track_builder.hpp"

#include "railmaster/sim/terrain.hpp"

#include <algorithm>
#include <stdexcept>

namespace railmaster::sim {

namespace {

std::int64_t pieces_for(std::int64_t length_mm, std::int64_t max_piece_mm) {
    if (max_piece_mm <= 0) throw std::invalid_argument("piece length must be positive");
    return std::max<std::int64_t>(1, (length_mm + max_piece_mm - 1) / max_piece_mm);
}

MapPoint midpoint(MapPoint a, MapPoint b) { return {(a.x_mm + b.x_mm) / 2, (a.y_mm + b.y_mm) / 2}; }

} // namespace

std::int64_t per_km_rate(TrackKind kind, BridgeType bridge, const Balance::Track& t) {
    switch (kind) {
    case TrackKind::Ground: return t.ground_per_km;
    case TrackKind::Tunnel: return t.ground_per_km * t.tunnel_multiple;
    case TrackKind::Bridge:
        switch (bridge) {
        case BridgeType::Wood: return t.ground_per_km * t.wood_bridge_multiple;
        case BridgeType::Steel: return t.ground_per_km * t.steel_bridge_multiple;
        case BridgeType::Stone: return t.ground_per_km * t.stone_bridge_multiple;
        case BridgeType::Suspension: return t.ground_per_km * t.suspension_bridge_multiple;
        case BridgeType::None: break;
        }
        break;
    }
    throw std::logic_error("bridge piece without a bridge type");
}

namespace {

// Pick the bridge type for this run, or explain why none fits.
std::optional<BridgeType> choose_bridge(const TrackBuildOptions& o, std::string& error) {
    if (o.bridge_type) {
        const BridgeType b = *o.bridge_type;
        if (b == BridgeType::None) {
            error = "bridge type None is not a bridge";
        } else if (!bridge_available(b, o.year)) {
            error = "that bridge type is not available yet";
        } else if (o.double_track && !bridge_carries_double_track(b)) {
            error = "wooden bridges carry single track only";
        } else {
            return b;
        }
        return std::nullopt;
    }
    // The cheapest that fits: wood, then steel, then stone.
    if (!o.double_track && bridge_available(BridgeType::Wood, o.year)) return BridgeType::Wood;
    return bridge_available(BridgeType::Steel, o.year) ? BridgeType::Steel : BridgeType::Stone;
}

} // namespace

bool bridge_available(BridgeType type, std::int32_t year) {
    switch (type) {
    case BridgeType::None: return false;
    case BridgeType::Wood:
    case BridgeType::Stone: return year <= kWoodStoneBridgeLastYear;
    case BridgeType::Steel: return year >= kSteelBridgeFirstYear;
    case BridgeType::Suspension: return year >= kSuspensionBridgeFirstYear;
    }
    return false;
}

bool bridge_carries_double_track(BridgeType type) {
    return type == BridgeType::Stone || type == BridgeType::Steel || type == BridgeType::Suspension;
}

std::vector<MapPoint> straight_points(MapPoint a, MapPoint b, std::int64_t max_piece_mm) {
    const std::int64_t n = pieces_for(distance_mm(a, b), max_piece_mm);
    std::vector<MapPoint> out;
    out.reserve(static_cast<std::size_t>(n));
    for (std::int64_t i = 1; i <= n; ++i) {
        out.push_back({a.x_mm + (b.x_mm - a.x_mm) * i / n, a.y_mm + (b.y_mm - a.y_mm) * i / n});
    }
    return out;
}

std::vector<MapPoint> curve_points(MapPoint a, MapPoint control, MapPoint b, std::int64_t max_piece_mm) {
    // The control polygon is never shorter than the curve, so this bounds piece length.
    const std::int64_t n = pieces_for(distance_mm(a, control) + distance_mm(control, b), max_piece_mm);
    // B(t) = (1-t)^2 a + 2(1-t)t c + t^2 b with t = i/n, scaled by n^2 to stay in integers.
    // Coordinates are at most ~1e9 mm and n at most a few thousand, so this fits in 64 bits.
    const std::int64_t nn = n * n;
    std::vector<MapPoint> out;
    out.reserve(static_cast<std::size_t>(n));
    for (std::int64_t i = 1; i <= n; ++i) {
        const std::int64_t u = n - i;
        const std::int64_t wa = u * u, wc = 2 * u * i, wb = i * i;
        out.push_back({(wa * a.x_mm + wc * control.x_mm + wb * b.x_mm) / nn,
                       (wa * a.y_mm + wc * control.y_mm + wb * b.y_mm) / nn});
    }
    return out;
}

std::optional<MapPoint> continuing_control_point(const TrackNetwork& net, NodeId from, MapPoint to) {
    const auto& edges = net.edges_at(from);
    if (edges.size() != 1) return std::nullopt;
    const MapPoint here = net.node(from).pos;
    const MapPoint behind = net.node(net.edge(edges.front()).other(from)).pos;
    const std::int64_t hx = here.x_mm - behind.x_mm, hy = here.y_mm - behind.y_mm;
    const std::int64_t hlen = isqrt(hx * hx + hy * hy);
    const std::int64_t reach = distance_mm(here, to) / 2;
    if (hlen == 0 || reach == 0) return std::nullopt;
    return MapPoint{here.x_mm + hx * reach / hlen, here.y_mm + hy * reach / hlen};
}

PlanResult plan_track(const TrackNetwork& net, const Terrain& terrain, NodeId from, std::vector<MapPoint> points,
                      std::optional<NodeId> end_node, const TrackBuildOptions& options, const Balance& b) {
    if (end_node && !points.empty() && net.node(*end_node).pos != points.back()) {
        return {std::nullopt, "run does not finish at the end node"};
    }
    const std::optional<std::int64_t> end_z =
        end_node ? std::optional<std::int64_t>{net.node(*end_node).z_mm} : std::nullopt;
    PlanResult r = plan_track_between(terrain, net.node(from).pos, net.node(from).z_mm, std::move(points), end_z,
                                      options, b);
    if (r.plan) {
        r.plan->from = from;
        r.plan->end_node = end_node;
    }
    return r;
}

PlanResult plan_track_between(const Terrain& terrain, MapPoint start_pos, std::int64_t start_z,
                              std::vector<MapPoint> points, std::optional<std::int64_t> end_z,
                              const TrackBuildOptions& options, const Balance& b) {
    PlanResult result;
    if (points.empty()) {
        result.error = "no track to lay";
        return result;
    }

    // Sample i = 0 is the start; samples 1..n are the run's points.
    const std::size_t n = points.size();
    std::vector<MapPoint> pos(n + 1);
    pos[0] = start_pos;
    std::copy(points.begin(), points.end(), pos.begin() + 1);

    std::vector<std::int64_t> run(n + 1, 0), ground(n + 1), z(n + 1);
    std::vector<bool> water(n + 1);
    for (std::size_t i = 0; i <= n; ++i) {
        if (i > 0) {
            run[i] = distance_mm(pos[i - 1], pos[i]);
            if (run[i] == 0) {
                result.error = "run has a zero-length piece";
                return result;
            }
        }
        ground[i] = terrain.height_at_mm(pos[i]);
        water[i] = terrain.ground_at_mm(pos[i]) == GroundType::Water;
    }
    if (water[0] && start_z <= ground[0]) {
        result.error = "track cannot start in water";
        return result;
    }
    if (!end_z && water[n]) {
        result.error = "track cannot end in water";
        return result;
    }

    // Rail profile. Two envelopes of the ground, both no steeper than the
    // grade limit, computed exactly by a forward and a backward pass:
    //   cut  - the highest line never above the ground (cuts and tunnels through hills)
    //   fill - the lowest line never below the ground (climbs over hills, bridges valleys)
    // The rail is a blend set by the tunnel preference. Blending two lines that
    // respect the grade limit gives a line that respects it too. Water places
    // no limit on either envelope, so lakes are spanned between the banks.
    const std::int64_t m = options.max_grade_bp.value_or(b.track.default_max_grade_bp);
    const auto step = [&](std::size_t i) { return m * run[i] / 10000; };
    constexpr std::int64_t kFree = std::int64_t{1} << 50;
    std::vector<std::int64_t> cut(n + 1), fill(n + 1);
    for (std::size_t i = 0; i <= n; ++i) {
        cut[i] = water[i] ? kFree : ground[i];
        fill[i] = water[i] ? -kFree : ground[i];
    }
    for (std::size_t i = 1; i <= n; ++i) {
        cut[i] = std::min(cut[i], cut[i - 1] + step(i));
        fill[i] = std::max(fill[i], fill[i - 1] - step(i));
    }
    for (std::size_t i = n; i-- > 0;) {
        cut[i] = std::min(cut[i], cut[i + 1] + step(i + 1));
        fill[i] = std::max(fill[i], fill[i + 1] - step(i + 1));
    }
    // Under the grade limit, fewer tunnels means climbing over more (on
    // embankments and bridges) and fewer bridges means cutting through more,
    // so the two settings meet in one blend: the tunnel preference pulls
    // towards the cut, the bridge preference towards the fill. "Never" is
    // absolute: no tunnels keeps to the fill, no bridges to the cut (water
    // is still bridged).
    const std::int64_t tunnels = std::clamp(options.tunnel_preference, 0, 100);
    const std::int64_t bridges = std::clamp(options.bridge_preference, 0, 100);
    std::int64_t t = (tunnels + 100 - bridges) / 2;
    if (tunnels == 0) t = 0;
    else if (bridges == 0) t = 100;
    for (std::size_t i = 0; i <= n; ++i) z[i] = (cut[i] * t + fill[i] * (100 - t)) / 100;

    // The ends are fixed to the existing node heights; re-clamp inwards from
    // each end so the pieces next to them stay within the limit where possible.
    z[0] = start_z;
    z[n] = end_z ? *end_z : ground[n];
    for (std::size_t i = 1; i < n; ++i) z[i] = std::clamp(z[i], z[i - 1] - step(i), z[i - 1] + step(i));
    for (std::size_t i = n - 1; i >= 1; --i) {
        z[i] = std::clamp(z[i], z[i + 1] - step(i + 1), z[i + 1] + step(i + 1));
        if (water[i]) z[i] = std::max(z[i], ground[i]); // never below the water
    }

    TrackPlan plan;
    plan.double_track = options.double_track;
    plan.points = std::move(points);
    plan.rail_z_mm.assign(z.begin() + 1, z.end());

    std::optional<BridgeType> bridge;
    std::vector<bool> piece_over_water(n + 1, false);
    for (std::size_t i = 1; i <= n; ++i) {
        const MapPoint mid = midpoint(pos[i - 1], pos[i]);
        const std::int64_t mid_ground = terrain.height_at_mm(mid);
        const std::int64_t mid_rail = (z[i - 1] + z[i]) / 2;
        const bool over_water = water[i - 1] || water[i] || terrain.ground_at_mm(mid) == GroundType::Water;
        piece_over_water[i] = over_water;

        PlannedPiece piece;
        piece.length_mm = run[i];
        if (over_water || mid_rail - mid_ground > b.track.viaduct_clearance_mm) {
            if (!bridge) {
                bridge = choose_bridge(options, result.error);
                if (!bridge) return result;
            }
            piece.kind = TrackKind::Bridge;
            piece.bridge = *bridge;
        } else if (mid_ground - mid_rail > b.track.tunnel_cover_mm) {
            piece.kind = TrackKind::Tunnel;
        }
        plan.pieces.push_back(piece);
    }

    if (tunnels == 0 && bridges == 0) {
        const bool structures = std::any_of(plan.pieces.begin(), plan.pieces.end(), [&](const PlannedPiece& p) {
            return p.kind == TrackKind::Tunnel;
        });
        bool viaduct = false;
        for (std::size_t i = 1; i <= n; ++i) {
            viaduct = viaduct || (plan.pieces[i - 1].kind == TrackKind::Bridge && !piece_over_water[i]);
        }
        if (structures || viaduct) {
            result.error = "the ground needs a tunnel or a bridge, and both are set to never";
            return result;
        }
    }

    // Long water crossings get a suspension bridge once one exists [D],
    // unless the player chose a bridge type.
    if (!options.bridge_type && bridge_available(BridgeType::Suspension, options.year)) {
        for (std::size_t i = 1; i <= n;) {
            if (!piece_over_water[i]) {
                ++i;
                continue;
            }
            std::size_t j = i;
            std::int64_t span = 0;
            while (j <= n && piece_over_water[j]) span += run[j++];
            if (span >= b.track.suspension_min_span_mm) {
                for (std::size_t k = i; k < j; ++k) plan.pieces[k - 1].bridge = BridgeType::Suspension;
            }
            i = j;
        }
    }

    const std::int64_t percent = options.double_track ? b.track.double_track_percent : 100;
    for (PlannedPiece& piece : plan.pieces) {
        piece.cost = Money::dollars(per_km_rate(piece.kind, piece.bridge, b.track))
                         .scaled(piece.length_mm * percent, 100LL * 1'000'000);
        plan.total_cost += piece.cost;
    }

    result.plan = std::move(plan);
    return result;
}

const char* frequency_name(Frequency f) {
    switch (f) {
    case Frequency::Never: return "never";
    case Frequency::Rarely: return "rarely";
    case Frequency::Average: return "average";
    case Frequency::Common: return "common";
    }
    return "average";
}

std::int32_t structure_preference(Frequency f, const Balance& b) {
    return b.track.structure_frequency_percent[static_cast<std::size_t>(f)];
}

bool overpass_wanted(Frequency f, bool new_line_double, bool crossed_double) {
    switch (f) {
    case Frequency::Never: return false;
    case Frequency::Rarely: return new_line_double && crossed_double;
    case Frequency::Average: return crossed_double;
    case Frequency::Common: return true;
    }
    return false;
}

NodeId build_track(TrackNetwork& net, const TrackPlan& plan, CompanyId owner, std::vector<EdgeId>* built) {
    NodeId prev = plan.from;
    for (std::size_t i = 0; i < plan.points.size(); ++i) {
        const bool last = i + 1 == plan.points.size();
        const NodeId next = (last && plan.end_node) ? *plan.end_node : net.add_node(plan.points[i], plan.rail_z_mm[i]);
        const PlannedPiece& p = plan.pieces[i];
        const EdgeId e = net.add_edge(prev, next, plan.double_track, p.kind, p.bridge, owner);
        net.add_cost(e, p.cost);
        if (built) built->push_back(e);
        prev = next;
    }
    return prev;
}

} // namespace railmaster::sim
