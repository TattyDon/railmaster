#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

using namespace railmaster::sim;

namespace {

std::string read_data(const char* name) {
    std::ifstream in(std::string(RAILMASTER_DATA_DIR) + "/" + name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

GameData shipped() {
    GameData d;
    d.balance = Balance::from_json(read_data("balance.json"));
    d.cargo = CargoRegistry::from_json(read_data("cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = LocomotiveRegistry::from_json(read_data("locomotives.json"));
    d.industries = IndustryRegistry::from_json(read_data("industries.json"), d.cargo);
    d.tycoons = TycoonRegistry::from_json(read_data("tycoons.json"));
    return d;
}

// An image that rises from black on the left to white on the right, with
// a blue lake in its top-right corner.
std::vector<std::uint8_t> ramp(int w, int h) {
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w * h * 3));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &rgb[static_cast<std::size_t>((y * w + x) * 3)];
            const auto grey = static_cast<std::uint8_t>(x * 255 / (w - 1));
            p[0] = p[1] = p[2] = grey;
            if (x >= w * 3 / 4 && y < h / 4) {
                p[0] = 30;
                p[1] = 60;
                p[2] = 200;
            }
        }
    }
    return rgb;
}

} // namespace

TEST_CASE("a heightmap image becomes a landscape: brightness is height, blue and dark are water") {
    const auto rgb = ramp(16, 16);
    const Landscape l = landscape_from_image(rgb.data(), 16, 16, 32, 32, 400, 24);
    REQUIRE(l.corners.size() == 33u * 33u);
    REQUIRE(l.water.size() == 32u * 32u);
    const auto corner = [&](int x, int y) { return l.corners[static_cast<std::size_t>(y * 33 + x)]; };
    const auto water = [&](int x, int y) { return l.water[static_cast<std::size_t>(y * 32 + x)] != 0; };
    // Rising to the east, all the way to (nearly) the maximum.
    CHECK(corner(0, 20) < corner(16, 20));
    CHECK(corner(16, 20) < corner(32, 20));
    CHECK(corner(32, 20) > 360);
    CHECK(corner(0, 20) < 30);
    // The dark western edge is sea; the blue patch is a lake, lying low.
    CHECK(water(0, 20));
    CHECK_FALSE(water(16, 20));
    CHECK(water(30, 2));
    CHECK(corner(31, 2) < corner(31, 30));
}

TEST_CASE("a world on a hand-made landscape keeps its heights and water, places towns on land, and saves it") {
    WorldConfig cfg;
    cfg.seed = 3;
    cfg.rivals = 0;
    const auto rgb = ramp(64, 64);
    cfg.landscape = landscape_from_image(rgb.data(), 64, 64, cfg.width_tiles, cfg.height_tiles, 400, 24);
    World w(cfg, shipped());
    const Terrain& t = w.terrain();
    CHECK(t.corner_height(0, 100) < t.corner_height(cfg.width_tiles, 100));
    CHECK(t.ground(0, 100) == GroundType::Water);
    CHECK(t.ground(cfg.width_tiles - 2, 2) == GroundType::Water);
    // No generated rivers: the middle of the map is dry.
    int wet = 0;
    for (int y = 0; y < cfg.height_tiles; ++y) wet += t.ground(cfg.width_tiles / 2, y) == GroundType::Water;
    CHECK(wet == 0);
    REQUIRE_FALSE(w.economy().towns().empty());

    const World back = World::load(w.save(), shipped());
    CHECK(back.terrain().corner_height(cfg.width_tiles, 100) == t.corner_height(cfg.width_tiles, 100));
    CHECK(back.terrain().ground(0, 100) == GroundType::Water);
}

TEST_CASE("a terrain gives back its landscape, which makes the same terrain again") {
    Terrain t(8, 6, 805);
    t.apply(flat_landscape(8, 6, 50));
    t.set_corner_height(3, 2, 120);
    t.set_ground(5, 4, GroundType::Water);
    const Landscape l = landscape_of(t);
    CHECK(l.corners[static_cast<std::size_t>(2 * 9 + 3)] == 120);
    CHECK(l.corners[0] == 50);
    CHECK(l.water[static_cast<std::size_t>(4 * 8 + 5)] == 1);
    Terrain again(8, 6, 805);
    again.apply(l);
    CHECK(again.corner_height(3, 2) == 120);
    CHECK(again.ground(5, 4) == GroundType::Water);
    CHECK(again.ground(4, 4) == GroundType::Grass);
}

TEST_CASE("a hand-made map's towns are its only towns, placed where they were put, and saved") {
    WorldConfig cfg;
    cfg.seed = 5;
    cfg.rivals = 0;
    cfg.landscape = flat_landscape(cfg.width_tiles, cfg.height_tiles, 60);
    cfg.towns = {{"Ashcombe", 40, 50, 30}, {"Bexley Wharf", 200, 180, 12}};
    World w(cfg, shipped());
    REQUIRE(w.economy().towns().size() == 2);
    const std::int32_t k = w.economy().cells_per_node();
    CHECK(w.economy().towns()[0].name == "Ashcombe");
    CHECK(w.economy().towns()[0].cx == 40 / k);
    CHECK(w.economy().towns()[1].name == "Bexley Wharf");
    CHECK(w.economy().towns()[1].cy == 180 / k);
    CHECK(w.economy().town_houses(0) > w.economy().town_houses(1));
    const World back = World::load(w.save(), shipped());
    CHECK(back.config().towns.size() == 2);
    CHECK(back.economy().towns()[1].name == "Bexley Wharf");
}
