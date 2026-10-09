#pragma once

#include "railmaster/sim/fixed_math.hpp"
#include "railmaster/sim/money.hpp"
#include "railmaster/sim/track.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace railmaster::sim {

class Terrain;

// Stand-ins for track-building rules the research has not pinned down.
// Documented in docs/spec/m1-provisional-models.md.
namespace provisional {
constexpr std::int32_t kDefaultMaxGradeBp = 300;         // 3%: steeper ground is cut, tunnelled or bridged
constexpr std::int64_t kTunnelCoverMm = 12'000;          // rail this far below ground becomes a tunnel
constexpr std::int64_t kViaductClearanceMm = 10'000;     // rail this far above ground becomes a bridge
constexpr std::int64_t kDefaultPieceMm = 500'000;        // curve smoothness, not a game rule
// Construction prices per kilometre of single track. The structure
// multiples follow rt3-clone-spec §11.2 [I]: wood 3x, steel 5x, stone 6x,
// suspension 10x, tunnel 15x plain track.
constexpr std::int64_t kGroundTrackPerKm = 25'000;
constexpr std::int64_t kWoodBridgePerKm = 3 * kGroundTrackPerKm;
constexpr std::int64_t kSteelBridgePerKm = 5 * kGroundTrackPerKm;
constexpr std::int64_t kStoneBridgePerKm = 6 * kGroundTrackPerKm;
constexpr std::int64_t kSuspensionBridgePerKm = 10 * kGroundTrackPerKm;
constexpr std::int64_t kTunnelPerKm = 15 * kGroundTrackPerKm;
// Double track costs more than single but less than twice [D]: +70% [I].
constexpr std::int64_t kDoubleTrackPercent = 170;
// Water stretches at least this long get a suspension bridge, once available [I].
constexpr std::int64_t kSuspensionMinSpanMm = 2'000'000;
} // namespace provisional

// Bridge eras [D, rt3-clone-spec §11.2].
constexpr std::int32_t kWoodStoneBridgeLastYear = 1865;
constexpr std::int32_t kSteelBridgeFirstYear = 1865;
constexpr std::int32_t kSuspensionBridgeFirstYear = 1895;

bool bridge_available(BridgeType type, std::int32_t year);
bool bridge_carries_double_track(BridgeType type);

// --- Geometry: the horizontal shape of a run ---------------------------------
// Each returns the points after `a` up to and including `b`, spaced no
// more than `max_piece_mm` apart.

std::vector<MapPoint> straight_points(MapPoint a, MapPoint b, std::int64_t max_piece_mm);

// Quadratic Bezier curve from a to b, bending towards `control`.
std::vector<MapPoint> curve_points(MapPoint a, MapPoint control, MapPoint b, std::int64_t max_piece_mm);

// A control point that makes a curve from `from` carry on in the direction
// the existing track was heading, so new track joins smoothly. Returns
// nullopt unless exactly one piece of track ends at `from`.
std::optional<MapPoint> continuing_control_point(const TrackNetwork& net, NodeId from, MapPoint to);

// --- Planning and building ---------------------------------------------------

struct TrackBuildOptions {
    std::int32_t year = 1830;
    bool double_track = false;
    std::int32_t max_grade_bp = provisional::kDefaultMaxGradeBp;
    // How strongly the route cuts through high ground rather than climbing
    // over it, 0..100. At 0 the rail never goes below the ground, so no
    // tunnels. Mirrors RT3's "Tunnels" build setting.
    std::int32_t tunnel_preference = 50;
    std::optional<BridgeType> bridge_type; // nullopt: cheapest type that fits
};

struct PlannedPiece {
    TrackKind kind = TrackKind::Ground;
    BridgeType bridge = BridgeType::None;
    std::int64_t length_mm = 0;
    Money cost;
};

struct TrackPlan {
    NodeId from = 0;
    std::optional<NodeId> end_node; // join an existing node rather than ending at a new one
    std::vector<MapPoint> points;   // after `from`, one per piece
    std::vector<std::int64_t> rail_z_mm;
    std::vector<PlannedPiece> pieces;
    bool double_track = false;
    Money total_cost;
};

struct PlanResult {
    std::optional<TrackPlan> plan;
    std::string error; // set when plan is empty
};

// Work out rail heights, structure types and cost for a run of track along
// `points` (as produced by the geometry helpers). Does not change the network.
PlanResult plan_track(const TrackNetwork& net, const Terrain& terrain, NodeId from,
                      std::vector<MapPoint> points, std::optional<NodeId> end_node,
                      const TrackBuildOptions& options);

// The same, starting from a position and rail height that need not be a
// node yet, and ending at ground level unless `end_z` is given. Used to
// preview a run before its end points exist. The plan's from/end_node are
// left unset; build such a plan only via plan_track.
PlanResult plan_track_between(const Terrain& terrain, MapPoint start_pos, std::int64_t start_z,
                              std::vector<MapPoint> points, std::optional<std::int64_t> end_z,
                              const TrackBuildOptions& options);

// Add a plan's nodes and pieces to the network. Returns the end node.
NodeId build_track(TrackNetwork& net, const TrackPlan& plan);

} // namespace railmaster::sim
