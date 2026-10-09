#pragma once

#include "railmaster/sim/money.hpp"
#include "railmaster/sim/railway.hpp"
#include "railmaster/sim/track.hpp"
#include "railmaster/sim/track_builder.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace railmaster::sim {

// Every change a player makes to the game goes through a command, the AI's
// included. A command is made by a player (World::execute's `who`); building,
// buying trains and company finance act for the company that player chairs.
// The world checks a command against the current state and either applies
// all of it or none of it. Commands are plain data, so they can later be recorded for
// replays or sent to other players for lockstep multiplayer.

// Where one end of a new piece of track, or a building, goes.
struct TrackEnd {
    enum class Kind : std::uint8_t {
        Node,   // an existing node
        OnEdge, // part-way along an existing piece of track (it will be split)
        Free,   // open ground (track only)
    };
    Kind kind = Kind::Free;
    NodeId node = 0; // Kind::Node
    EdgeId edge = 0; // Kind::OnEdge
    MapPoint pos{};  // always set

    bool operator==(const TrackEnd&) const = default;
};

// What lies under a point: the nearest node within `snap_mm`, else the
// nearest point on track within `snap_mm`, else open ground.
TrackEnd pick_track_end(const TrackNetwork& net, MapPoint p, std::int64_t snap_mm);

struct BuildTrack {
    TrackEnd start{};
    TrackEnd end{};
    std::optional<MapPoint> curve_control{}; // nullopt: straight
    bool double_track = false;
    std::int32_t tunnel_preference = 50;
    std::optional<BridgeType> bridge_type{};
};

struct BuildStation {
    TrackEnd at{}; // must be on track
    StationSize size = StationSize::Medium;
    std::string name{}; // empty: a default name is chosen
};

struct BuildServiceBuilding {
    TrackEnd at{}; // must be on track
    ServiceType type = ServiceType::ServiceTower;
};

struct BuyTrain {
    LocoTypeId loco = 0;
    std::uint8_t cars = 0;
    std::vector<StationId> route{};
    std::int32_t priority = 0;
};

// Borrow $500,000 (needs a credit rating of B or better).
struct IssueBond {};
// Repay the most expensive bond outstanding at face value.
struct RepayBond {};

// A player trades shares in any company, in blocks (1,000 by default).
struct BuyShares {
    std::int64_t blocks = 1;
    CompanyId company = 0;
};
struct SellShares {
    std::int64_t blocks = 1;
    CompanyId company = 0;
};
// The company sells new shares (at most twice a year) or buys some back.
struct IssueStock {};
struct BuyBackStock {};
// Set the annual dividend per share, paid quarterly.
struct SetDividend {
    Money per_share{};
};

using Command = std::variant<BuildTrack, BuildStation, BuildServiceBuilding, BuyTrain, IssueBond, RepayBond,
                             BuyShares, SellShares, IssueStock, BuyBackStock, SetDividend>;

struct CommandResult {
    bool ok = false;
    std::string error{};
    Money cost{};
    std::uint32_t created_id = 0; // end node, station, building or train
};

} // namespace railmaster::sim
