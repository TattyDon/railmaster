#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <set>
#include <sstream>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

MapPoint cell_centre(std::int32_t cx, std::int32_t cy) { return {cx * kKm + kKm / 2, cy * kKm + kKm / 2}; }
TrackEnd free_at(int cx, int cy) { return {TrackEnd::Kind::Free, 0, 0, cell_centre(cx, cy)}; }
TrackEnd node_at(const World& w, int cx, int cy) {
    const TrackNetwork& net = w.railway().track();
    const NodeId n = *net.nearest_node(cell_centre(cx, cy), 1);
    return {TrackEnd::Kind::Node, n, 0, net.node(n).pos};
}

void run_days(World& w, int days) {
    for (int d = 0; d < days; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

// No price impact and deep pockets, so stakes can be built at a known price.
GameData corporate_data() {
    GameData d;
    d.balance.stock.impact_per_share_of_company = 0;
    d.balance.stock.founder_fortune = 13'000'000; // $10M left after founding
    d.cargo = CargoRegistry::from_json(R"({"cargo": [{"key": "coal", "name": "Coal", "base_price": 30}]})");
    d.locomotives = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "l", "name": "L", "fuel": "diesel", "available_from": 1800,
         "top_speed_mph": 60, "cost": 50000, "maintenance_per_year": 1}]})");
    return d;
}

struct Fixture {
    World w;
    PlayerId rival = 0;
    CompanyId rival_co = 0;

    Fixture() : w(make()) {
        rival = w.add_player_company("Rival Lines", "A. Rival");
        rival_co = *w.investors()[rival].chairs;
    }
    static World make() {
        WorldConfig cfg;
        cfg.width_tiles = 40;
        cfg.height_tiles = 20;
        cfg.populate = false;
        cfg.business_cycle = false;
        World w(cfg, corporate_data());
        for (int y = 0; y <= 20; ++y)
            for (int x = 0; x <= 40; ++x) w.terrain().set_corner_height(x, y, 10);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 40; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
        return w;
    }
    // The rival builds a line with two stations and a train, and borrows.
    void rival_builds() {
        REQUIRE(w.execute(BuildTrack{.start = free_at(5, 5), .end = free_at(20, 5)}, rival).ok);
        const CommandResult a = w.execute(BuildStation{.at = node_at(w, 5, 5)}, rival);
        const CommandResult b = w.execute(BuildStation{.at = node_at(w, 20, 5)}, rival);
        REQUIRE(a.ok);
        REQUIRE(b.ok);
        REQUIRE(w.execute(BuyTrain{.loco = 0, .cars = 2, .route = {a.created_id, b.created_id}}, rival).ok);
        REQUIRE(w.execute(IssueBond{}, rival).ok);
    }
};

} // namespace

TEST_CASE("a takeover needs the shareholders: a small stake fails, and you must wait a year to retry [C]") {
    Fixture f;
    const CommandResult first = f.w.execute(AttemptTakeover{.target = f.rival_co});
    REQUIRE_FALSE(first.ok);
    CHECK(first.error.find("voted no") != std::string::npos);
    const CommandResult again = f.w.execute(AttemptTakeover{.target = f.rival_co});
    REQUIRE_FALSE(again.ok);
    CHECK(again.error.find("wait a year") != std::string::npos);
    CHECK_FALSE(f.w.execute(AttemptTakeover{.target = 0}).ok); // already yours
    CHECK_FALSE(f.w.execute(AttemptTakeover{.target = 9}).ok);
    run_days(f.w, 366);
    const CommandResult later = f.w.execute(AttemptTakeover{.target = f.rival_co});
    CHECK(later.error.find("voted no") != std::string::npos); // allowed to try, still too small
}

TEST_CASE("over half the shares wins a takeover: the chairmen swap companies [D/I]") {
    Fixture f;
    REQUIRE(f.w.execute(SellShares{.blocks = 10, .company = f.rival_co}, f.rival).ok); // the founder sells some
    REQUIRE(f.w.execute(BuyShares{.blocks = 301, .company = f.rival_co}).ok);
    CHECK(f.w.investor().shares_in(f.rival_co) == 301'000);
    const CommandResult r = f.w.execute(AttemptTakeover{.target = f.rival_co});
    REQUIRE(r.ok);
    CHECK(f.w.investor().chairs == f.rival_co);
    CHECK(f.w.company().id() == f.rival_co);
    CHECK(f.w.investors()[f.rival].chairs == CompanyId{0}); // the ousted chairman gets the old one
    // The player now builds for the company they took over.
    REQUIRE(f.w.execute(BuildTrack{.start = free_at(5, 12), .end = free_at(15, 12)}).ok);
    CHECK(f.w.railway().track().edges().back().owner == f.rival_co);
}

TEST_CASE("the public backs a takeover of a company trading below book or losing money [I]") {
    const Balance::Corporate& b = default_balance().corporate;
    Market m;
    m.companies.push_back(Company("Target", Money::dollars(6'000'000), 1850, default_balance(), 0));
    m.investors.push_back(Investor::founder("Chair", 0));
    Investor bidder;
    bidder.add_shares(0, 200'000); // a third
    m.investors.push_back(bidder);
    // At book value and no losses: a quarter of the remaining 100,000 public shares.
    CHECK(takeover_vote(m, 1, 0, b).yes == 200'000 + 25'000);
    CHECK_FALSE(takeover_vote(m, 1, 0, b).passed());
    m.companies[0].set_share_price(Money::dollars(5)); // below book: half the public
    CHECK(takeover_vote(m, 1, 0, b).yes == 200'000 + 50'000);
    m.companies[0].post(Ledger::Fuel, Money::dollars(1'000'000));
    m.companies[0].start_year(1851); // a loss year behind it: three quarters
    CHECK(takeover_vote(m, 1, 0, b).yes == 200'000 + 75'000);
    CHECK_FALSE(takeover_vote(m, 1, 0, b).passed()); // the founder's half still blocks it
}

TEST_CASE("a merger needs a premium the shareholders accept [C/I]") {
    Fixture f;
    const Money price = f.w.company(f.rival_co).share_price();
    // At the market price the public mostly says no, and the chairman does.
    const CommandResult cheap = f.w.execute(AttemptMerger{.target = f.rival_co, .offer_per_share = price});
    REQUIRE_FALSE(cheap.ok);
    CHECK(cheap.error.find("voted no") != std::string::npos);

    Fixture g;
    // 30% over market: everyone accepts. 600,000 shares at $13 costs $7.8M,
    // more than the player's company has.
    const Money offer = price.scaled(130, 100);
    const CommandResult broke = g.w.execute(AttemptMerger{.target = g.rival_co, .offer_per_share = offer});
    REQUIRE_FALSE(broke.ok);
    CHECK(broke.error.find("costs") != std::string::npos);
    CHECK(merger_vote(g.w.market(), kHumanPlayer, g.rival_co, offer, default_balance().corporate).passed());
    CHECK_FALSE(merger_vote(g.w.market(), kHumanPlayer, g.rival_co, price, default_balance().corporate).passed());
}

TEST_CASE("a merger moves everything to the buyer and pays out the other shareholders [D/C]") {
    Fixture f;
    f.rival_builds();
    const Company& target = f.w.company(f.rival_co);
    const Money target_cash = target.cash();
    const Money target_track = target.track_value();
    REQUIRE(f.w.execute(SellShares{.blocks = 10, .company = f.rival_co}, f.rival).ok); // the founder sells some
    const Money rival_cash = f.w.investors()[f.rival].cash;
    REQUIRE(f.w.execute(BuyShares{.blocks = 301, .company = f.rival_co}).ok); // a majority
    const Money offer = target.share_price().scaled(110, 100);
    const Money buyer_cash = f.w.company().cash();
    const std::int64_t buyer_shares = f.w.company().shares_outstanding();

    const CommandResult r = f.w.execute(AttemptMerger{.target = f.rival_co, .offer_per_share = offer});
    REQUIRE(r.ok);
    // The others' 299,000 shares are bought for cash; the target's cash comes across.
    CHECK(r.cost == offer * 299'000);
    CHECK(f.w.company().cash() == buyer_cash - offer * 299'000 + target_cash);
    CHECK(f.w.company().track_value() == target_track);
    CHECK(f.w.company().bonds().size() == 1);          // debts too
    CHECK(f.w.company().this_year().acquisitions == offer * 299'000);
    CHECK(f.w.investors()[f.rival].cash == rival_cash + offer * 290'000);
    CHECK_FALSE(f.w.investors()[f.rival].chairs.has_value());
    // The player's own stake becomes new shares in their company, at market value.
    CHECK(f.w.investor().shares_in(f.rival_co) == 0);
    CHECK(f.w.company().shares_outstanding() > buyer_shares);
    CHECK(f.w.investor().shares_in(0) - 300'000 == f.w.company().shares_outstanding() - buyer_shares);

    const Company& gone = f.w.company(f.rival_co);
    CHECK(gone.defunct());
    CHECK(gone.merged_into() == CompanyId{0});
    CHECK(gone.shares_outstanding() == 0);
    for (const TrackEdge& e : f.w.railway().track().edges()) CHECK(e.owner == 0);
    for (const Station& s : f.w.railway().stations()) CHECK(s.owner == 0);
    for (const Train& t : f.w.railway().trains()) CHECK(t.owner == 0);

    // A merged company can no longer be traded or bid for.
    CHECK_FALSE(f.w.execute(BuyShares{.company = f.rival_co}).ok);
    CHECK_FALSE(f.w.execute(AttemptTakeover{.target = f.rival_co}).ok);
    CHECK_FALSE(f.w.execute(AttemptMerger{.target = f.rival_co, .offer_per_share = offer}).ok);
    // Its old chairman has no company to run, but can still trade.
    CHECK_FALSE(f.w.execute(BuildTrack{.start = free_at(5, 15), .end = free_at(9, 15)}, f.rival).ok);
    CHECK(f.w.execute(BuyShares{.blocks = 1, .company = 0}, f.rival).ok);
    run_days(f.w, 100); // and the world carries on
}

TEST_CASE("short sellers are closed out at the merger price; rivals cannot merge the player away") {
    Fixture f;
    const PlayerId third = f.w.add_player_company("Third", "C. Third");
    REQUIRE(f.w.execute(SellShares{.blocks = 10, .company = f.rival_co}, third).ok); // short 10,000
    const Money third_cash = f.w.investors()[third].cash;
    REQUIRE(f.w.execute(SellShares{.blocks = 10, .company = f.rival_co}, f.rival).ok); // the founder sells some
    REQUIRE(f.w.execute(BuyShares{.blocks = 301, .company = f.rival_co}).ok);
    const Money offer = f.w.company(f.rival_co).share_price().scaled(110, 100);
    REQUIRE(f.w.execute(AttemptMerger{.target = f.rival_co, .offer_per_share = offer}).ok);
    CHECK(f.w.investors()[third].shares_in(f.rival_co) == 0);
    CHECK(f.w.investors()[third].cash == third_cash - offer * 10'000);

    const CommandResult away =
        f.w.execute(AttemptMerger{.target = 0, .offer_per_share = Money::dollars(100)}, third);
    REQUIRE_FALSE(away.ok);
    CHECK(away.error.find("player's own company") != std::string::npos);
}

namespace {

std::string read_data(const char* name) {
    std::ifstream in(std::string(RAILMASTER_DATA_DIR) + "/" + name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("with every tycoon playing for years, ownership stays consistent") {
    GameData d;
    d.balance = Balance::from_json(read_data("balance.json"));
    d.cargo = CargoRegistry::from_json(read_data("cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = LocomotiveRegistry::from_json(read_data("locomotives.json"));
    d.industries = IndustryRegistry::from_json(read_data("industries.json"), d.cargo);
    d.tycoons = TycoonRegistry::from_json(read_data("tycoons.json"));
    WorldConfig cfg;
    cfg.seed = 4;
    cfg.width_tiles = cfg.height_tiles = 80;
    cfg.rivals = 7;
    World w(cfg, d);
    run_days(w, 365 * 2);

    std::set<CompanyId> chaired;
    for (const Investor& inv : w.investors()) {
        if (!inv.chairs) continue;
        CHECK(chaired.insert(*inv.chairs).second); // one chairman per company
        CHECK_FALSE(w.company(*inv.chairs).defunct());
    }
    CHECK(w.investor().chairs.has_value()); // the player always runs a company
    for (const Company& c : w.companies()) {
        std::int64_t longs = 0;
        for (const Investor& inv : w.investors()) longs += std::max<std::int64_t>(0, inv.shares_in(c.id()));
        CHECK(longs <= c.shares_outstanding());
        if (c.defunct()) CHECK(c.shares_outstanding() == 0);
    }
    for (const Train& t : w.railway().trains()) CHECK_FALSE(w.company(t.owner).defunct());
    for (const Investor& inv : w.investors()) {
        // Shorts stay within the cap they were opened under, give or take price moves.
        Money shorts;
        for (const Company& c : w.companies()) {
            if (inv.shares_in(c.id()) < 0) shorts += c.share_price() * -inv.shares_in(c.id());
        }
        CHECK(purchasing_power(inv, w.market()) > Money{} - shorts);
    }
}
