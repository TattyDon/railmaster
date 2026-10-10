#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace railmaster::sim;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string read_data(const char* name) { return read_file(std::string(RAILMASTER_DATA_DIR) + "/" + name); }

GameData shipped() {
    GameData d;
    d.balance = Balance::from_json(read_data("balance.json"));
    d.cargo = CargoRegistry::from_json(read_data("cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = LocomotiveRegistry::from_json(read_data("locomotives.json"));
    d.industries = IndustryRegistry::from_json(read_data("industries.json"), d.cargo);
    d.tycoons = TycoonRegistry::from_json(read_data("tycoons.json"));
    return d;
}

// A scenario around two towns, with the given medal goals as JSON.
std::string scenario_json(const std::string& bronze, const std::string& silver, const std::string& gold,
                          const std::string& extra = "") {
    return R"({"key": "t", "name": "Test", "start": "1850-01-01", "seed": 5, )" + extra +
           R"("towns": [{"name": "Alpha", "x": 20, "y": 30}, {"name": "Beta", "x": 44, "y": 30}],
              "medals": {"bronze": )" + bronze + R"(, "silver": )" + silver + R"(, "gold": )" + gold + "}}";
}

const std::string kNoGoals = R"({"by": "1851-12-31", "goals": []})";

// A world for a scenario on a small 64 x 64 cell map, so tests run fast.
World small_world(const Scenario& s, GameData d = {}) {
    WorldConfig cfg = scenario_config(s);
    cfg.width_tiles = 64;
    cfg.height_tiles = 64;
    cfg.business_cycle = false;
    cfg.industries_appear = false;
    return World(cfg, std::move(d));
}

void run_days(World& w, int days) {
    for (int d = 0; d < days; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

void run_to(World& w, Date when) {
    while (w.date() < when) run_days(w, 1);
}

std::size_t town_index(const World& w, const std::string& name) {
    const auto& towns = w.economy().towns();
    const auto it = std::find_if(towns.begin(), towns.end(), [&](const Town& t) { return t.name == name; });
    REQUIRE(it != towns.end());
    return static_cast<std::size_t>(it - towns.begin());
}

TrackEnd free_at(MapPoint p) { return {TrackEnd::Kind::Free, 0, 0, p}; }

TrackEnd node_near(const World& w, MapPoint p) {
    const TrackNetwork& net = w.railway().track();
    const NodeId n = *net.nearest_node(p, 1'000'000);
    return {TrackEnd::Kind::Node, n, 0, net.node(n).pos};
}

} // namespace

TEST_CASE("a scenario file is parsed: towns, founding money, and nested medal tiers [D/I]") {
    const Scenario s = Scenario::from_json(read_data("scenarios/lowland_junction.json"));
    CHECK(s.name == "Lowland Junction");
    CHECK(s.start == Date::from_ymd(1850, 1, 1));
    CHECK(s.towns.size() == 4);
    CHECK(s.towns[1].name == "Brennan Falls");
    // Silver needs Bronze's goals as well as its own; Gold needs all.
    CHECK(s.goals_for(Medal::Bronze).size() == 1);
    CHECK(s.goals_for(Medal::Silver).size() == 3);
    CHECK(s.goals_for(Medal::Gold).size() == 5);
    CHECK(s.last_deadline() == Date::from_ymd(1866, 12, 31));

    const Scenario p = Scenario::from_json(read_data("scenarios/six_provinces.json"));
    CHECK(p.territories == 6);
    CHECK(p.founder_fortune == 2'000'000);
    CHECK_FALSE(p.chairman_can_be_fired);
    CHECK_FALSE(p.chairman_can_resign);
}

TEST_CASE("bad scenario files are refused with a reason") {
    const std::string ok = R"({"by": "1851-12-31", "goals": [{"kind": "book_value", "amount": 1}]})";
    const auto error = [](const std::string& json) {
        try {
            (void)Scenario::from_json(json);
        } catch (const std::runtime_error& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    CHECK(error(scenario_json(ok, ok, ok)).empty());
    CHECK(error("{").find("scenario:") == 0);
    CHECK(error(scenario_json(R"({"by": "1851-12-31", "goals": [{"kind": "fly"}]})", ok, ok)).find("unknown goal kind") !=
          std::string::npos);
    CHECK(error(scenario_json(R"({"by": "1851-12-31", "goals": [{"kind": "connect_towns", "towns": ["Alpha", "Gamma"]}]})",
                              ok, ok))
              .find("Gamma") != std::string::npos);
    CHECK(error(scenario_json(R"({"by": "1851-12-31", "goals": [{"kind": "connect_towns", "towns": ["Alpha"]}]})", ok, ok))
              .find("two towns") != std::string::npos);
    CHECK(error(scenario_json(R"({"by": "1851-12-31", "goals": [{"kind": "deliver", "count": 5}]})", ok, ok))
              .find("cargo") != std::string::npos);
    CHECK(error(scenario_json(kNoGoals, ok, ok)).find("bronze") != std::string::npos);
    CHECK(error(scenario_json(R"({"by": "1849-12-31", "goals": [{"kind": "book_value", "amount": 1}]})", ok, ok))
              .find("after the start") != std::string::npos);
    CHECK(error(scenario_json(R"({"by": "1851-13-01", "goals": [{"kind": "book_value", "amount": 1}]})", ok, ok))
              .find("bad date") != std::string::npos);
    CHECK(error(scenario_json(ok, ok, ok, R"("founder_fortune": 100, "founder_investment": 200, )"))
              .find("founder_investment") != std::string::npos);
    CHECK(error(scenario_json(ok, ok, ok, R"("map": "huge", )")).find("map") != std::string::npos);
}

TEST_CASE("score: medal x difficulty x 5% a whole year early [I]") {
    CHECK(scenario_score_milli(Medal::Bronze, 100, 0) == 1000);
    CHECK(scenario_score_milli(Medal::Silver, 75, 0) == 1500);
    CHECK(scenario_score_milli(Medal::Gold, 150, 2) == 4950);
    CHECK(scenario_score_milli(Medal::Gold, 100, -3) == 3000); // late never costs
    CHECK(difficulty_score_percent(Difficulty::Expert) == 150);
}

TEST_CASE("every shipped scenario builds a world with its towns, territories and founding money") {
    for (const auto& entry : std::filesystem::directory_iterator(std::string(RAILMASTER_DATA_DIR) + "/scenarios")) {
        CAPTURE(entry.path().string());
        const Scenario s = Scenario::from_json(read_file(entry.path().string()));
        const World w(scenario_config(s), shipped());
        CHECK(w.date() == s.start);
        CHECK(w.rivals().size() == static_cast<std::size_t>(s.rivals));
        CHECK(w.territories().territories.size() == static_cast<std::size_t>(s.territories));
        const std::int32_t k = w.economy().cells_per_node();
        for (const ScenarioTown& t : s.towns) {
            const Town& town = w.economy().towns()[town_index(w, t.name)];
            CHECK(town.cx == t.x / k);
            CHECK(town.cy == t.y / k);
            CHECK(w.economy().town_houses(town_index(w, t.name)) > 0);
        }
        // The first scenario town's territory is the free home territory.
        if (s.territories > 0 && !s.towns.empty()) {
            const Town& home = w.economy().towns().front();
            CHECK(home.name == s.towns.front().name);
            const TerritoryId t = w.territory_at(w.economy().node_centre(home.cx, home.cy));
            CHECK(w.territories().territories[t].access_cost == Money{});
        }
        if (s.founder_fortune && s.founder_investment) {
            CHECK(w.investor().cash == Money::dollars(*s.founder_fortune - *s.founder_investment));
        }
        CHECK_FALSE(w.scenario_result().medal.has_value());
    }
}

TEST_CASE("connecting the named towns wins Bronze at the next month end, not before") {
    const Scenario s = Scenario::from_json(scenario_json(
        R"({"by": "1851-12-31", "goals": [{"kind": "connect_towns", "towns": ["Alpha", "Beta"]}]})",
        R"({"by": "1851-12-31", "goals": [{"kind": "book_value", "amount": 900000000}]})", kNoGoals));
    World w = small_world(s, shipped());
    const Town& a = w.economy().towns()[town_index(w, "Alpha")];
    const Town& b = w.economy().towns()[town_index(w, "Beta")];
    const MapPoint pa = w.economy().node_centre(a.cx, a.cy), pb = w.economy().node_centre(b.cx, b.cy);
    // Flatten the ground between them so the line is easy to build.
    for (int y = 25; y <= 36; ++y)
        for (int x = 15; x <= 50; ++x) w.terrain().set_corner_height(x, y, 20);
    for (int y = 25; y < 36; ++y)
        for (int x = 15; x < 50; ++x) w.terrain().set_ground(x, y, GroundType::Grass);

    const Goal& connect = s.medals[0].goals.front();
    CHECK_FALSE(check_goal(w, connect).met);
    CHECK(check_goal(w, connect).text == "Connect Alpha Beta: 0 of 2");

    REQUIRE(w.execute(BuildTrack{.start = free_at(pa), .end = free_at(pb)}).ok);
    REQUIRE(w.execute(BuildStation{.at = node_near(w, pa)}).ok);
    CHECK(check_goal(w, connect).text == "Connect Alpha Beta: 1 of 2");
    REQUIRE(w.execute(BuildStation{.at = node_near(w, pb)}).ok);
    CHECK(check_goal(w, connect).met);

    CHECK_FALSE(w.scenario_result().medal.has_value()); // checked at month end
    run_to(w, Date::from_ymd(1850, 2, 1));
    REQUIRE(w.scenario_result().medal == Medal::Bronze);
    CHECK(w.scenario_result().won_on == Date::from_ymd(1850, 2, 1));
    CHECK_FALSE(w.scenario_result().finished);
    const auto news = w.take_news();
    CHECK(std::find(news.begin(), news.end(), "Bronze medal won: Test") != news.end());
    // Won about 1.9 years before the deadline: one whole year early.
    CHECK(w.scenario_score_milli() == 1050);
}

TEST_CASE("the scenario ends at the last deadline with the best medal won") {
    const Scenario s = Scenario::from_json(scenario_json(
        R"({"by": "1850-02-28", "goals": [{"kind": "company_cash", "amount": 1}]})",
        R"({"by": "1850-02-28", "goals": [{"kind": "highest_value"}, {"kind": "only_railroad"}]})",
        R"({"by": "1850-03-31", "goals": [{"kind": "book_value", "amount": 900000000}]})"));
    World w = small_world(s);
    run_to(w, Date::from_ymd(1850, 2, 1));
    // No rivals: the only railroad, and the most valuable.
    CHECK(w.scenario_result().medal == Medal::Silver);
    run_to(w, Date::from_ymd(1850, 3, 1));
    CHECK_FALSE(w.scenario_result().finished);
    run_to(w, Date::from_ymd(1850, 4, 1));
    CHECK(w.scenario_result().finished);
    CHECK(w.scenario_result().medal == Medal::Silver);
    CHECK(w.scenario_result().won_on == Date::from_ymd(1850, 2, 1));
}

TEST_CASE("a missed deadline wins nothing, even if the goal is met later") {
    const std::string none = R"({"by": "1850-01-31", "goals": []})";
    const Scenario s = Scenario::from_json(scenario_json(
        R"({"by": "1850-01-31", "goals": [{"kind": "deliver", "cargo": "coal", "count": 2}]})", none, none));
    World w = small_world(s, shipped());
    const Goal& deliver = s.medals[0].goals.front();
    CHECK(check_goal(w, deliver).text == "Deliver coal: 0 of 2 loads");
    run_to(w, Date::from_ymd(1850, 2, 1));
    CHECK(w.scenario_result().finished);
    CHECK_FALSE(w.scenario_result().medal.has_value());
    const auto news = w.take_news();
    CHECK(std::find(news.begin(), news.end(), "Test: time is up, no medal") != news.end());

    w.company().record_delivery(*w.data().cargo.find("coal"), 2'500);
    CHECK(check_goal(w, deliver).met);
    run_to(w, Date::from_ymd(1850, 3, 1));
    CHECK_FALSE(w.scenario_result().medal.has_value());
    CHECK(w.scenario_score_milli() == 0);
}

TEST_CASE("money goals read the company's books and the player's net worth") {
    const Scenario s = Scenario::from_json(scenario_json(
        R"({"by": "1851-12-31", "goals": [{"kind": "book_value", "amount": 1000}]})", kNoGoals, kNoGoals));
    World w = small_world(s);
    const Goal book{.kind = GoalKind::BookValue, .amount = Money::dollars(1000)};
    CHECK(check_goal(w, book).met);
    const Goal rich{.kind = GoalKind::PersonalNetWorth, .amount = Money::dollars(900'000'000)};
    const GoalProgress p = check_goal(w, rich);
    CHECK_FALSE(p.met);
    CHECK(p.text.find("of $900,000,000") != std::string::npos);
    const Goal revenue{.kind = GoalKind::Revenue, .amount = Money::dollars(1)};
    CHECK_FALSE(check_goal(w, revenue).met); // nothing earned yet
    const Goal places{.kind = GoalKind::Territories, .count = 1};
    CHECK(check_goal(w, places).text == "Stations in 0 of 1 territories");
}
