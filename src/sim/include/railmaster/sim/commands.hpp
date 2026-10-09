#pragma once

#include "railmaster/sim/economy.hpp"
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

// Put a new engine on one of your trains, keeping its route, cars and
// priority (rt3-clone-spec §9.3 [D]). The new engine starts at age 0 with
// full water, sand and oil; the old one is scrapped with nothing back [I].
struct ReplaceLocomotive {
    TrainId train = 0;
    LocoTypeId loco = 0;
};

// Set what one of your trains takes on at route stop `stop`, or at every
// stop if unset ("apply this consist to all stations") (rt3-clone-spec
// §9.3 [D]). Cars, caboose and dining car share 8 slots.
struct SetConsist {
    TrainId train = 0;
    std::optional<std::size_t> stop{};
    ConsistRule rule{};
};
// Add or remove a train's caboose (halves breakdowns) and dining car
// (passengers pay 20% more); each takes a car slot [D].
struct SetSpecialCars {
    TrainId train = 0;
    bool caboose = false;
    bool diner = false;
};
// Buy a copy of one of your trains: the same engine type, route, consist
// rules and special cars [D], at the engine's price.
struct CopyTrain {
    TrainId train = 0;
};
// Take one of your trains out of service for good [D]. Its engine is
// written off and whatever it carries is lost.
struct RetireTrain {
    TrainId train = 0;
};

// Electrify your own track so electric engines can use it (rt3-clone-spec
// §11.2 [D]): the listed pieces, or with none listed all of your track
// ("Electrify all track"). Pieces already electrified cost nothing.
struct ElectrifyTrack {
    std::vector<EdgeId> edges{};
};

// Buy your company access rights to a territory, so it may build there
// (rt3-clone-spec §3.3 [D]). Paid once, as a territory fee.
struct BuyTerritoryAccess {
    std::uint16_t territory = 0;
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

// Ask the shareholders of `target` to make you its chairman [D]. If you
// chair another company, its chairman and yours swap places [I].
struct AttemptTakeover {
    CompanyId target = 0;
};
// Your company offers to buy out every other shareholder of `target` at
// `offer_per_share` [D]; if they vote yes, it absorbs everything the target
// owns, debts included [C].
struct AttemptMerger {
    CompanyId target = 0;
    Money offer_per_share{};
};

// Step down as chairman, keeping your shares [D]. The board appoints a
// successor.
struct Resign {};

// Found a company (rt3-clone-spec §12.2, the founding dialog [I, RT2]): put
// some of your own money in, and take up to the outside investors' offer.
// The capital is issued as shares at the founding price; you get the shares
// your money bought. Only a player who runs no company may.
struct FoundCompany {
    std::string name{};
    Money personal_investment{};
    Money outside_investment{};
};

// Industries (rt3-clone-spec §6.2): buy one nobody owns, at a multiple of
// its yearly profit or a floor price [C]; build a processing plant on open
// dry land [C/D]; or double a plant's capacity [D].
struct BuyIndustry {
    SiteId site = 0;
};
struct BuildIndustry {
    IndustryTypeId type = 0;
    std::int32_t cx = 0, cy = 0; // economy cell
};
struct UpgradeIndustry {
    SiteId site = 0;
};
// Put up a post office, hotel, restaurant or tavern near a station, anyone's
// [D/C]: on dry land within the buildings' range of at least one station.
struct BuildStationBuilding {
    StationBuildingType type = StationBuildingType::Restaurant;
    MapPoint pos{};
};

// Set what one of your warehouses does: receive, supply or exchange [C].
struct SetPortMode {
    SiteId site = 0;
    PortMode mode = PortMode::Exchange;
};

// Declare the company bankrupt [D]: a last resort that halves its bond debt,
// gives the bondholders new shares for the rest, and ruins its credit.
struct DeclareBankruptcy {};

using Command = std::variant<BuildTrack, BuildStation, BuildServiceBuilding, BuyTrain, ReplaceLocomotive, SetConsist, SetSpecialCars,
                             CopyTrain, RetireTrain, ElectrifyTrack, BuyTerritoryAccess, IssueBond, RepayBond,
                             BuyShares, SellShares, IssueStock, BuyBackStock, SetDividend, AttemptTakeover,
                             AttemptMerger, Resign, FoundCompany, DeclareBankruptcy, BuyIndustry, BuildIndustry,
                             UpgradeIndustry, SetPortMode, BuildStationBuilding>;

struct CommandResult {
    bool ok = false;
    std::string error{};
    Money cost{};
    std::uint32_t created_id = 0; // end node, station, building or train
};

} // namespace railmaster::sim
