// Railmaster client: opens a window, runs the simulation at a fixed rate,
// draws the map, and offers the build tools. The map is drawn in 3D with
// fixed-function OpenGL: a lit terrain mesh, track, trains and buildings as
// simple shapes (V switches to the old top-down view).
//
// Usage: railmaster [data-dir] [--empty] [--quick] [--rivals=N] [--map=small|medium|large] [--territories=N]
//                   [--scenario=FILE] [--load=FILE] [--landscape=IMAGE] [--no-shaders] [--look=X,Y] [--version]
// With none of --quick, --empty, --scenario, --load or --look it opens on
// the title screen (title.cpp), over the demo network running by itself.
// --look starts the view over map tile (X, Y), close in.
// The commit it was built from shows in the bottom-right corner, the window
// title and --version.
// Ctrl+S saves the game to railmaster.sav, Ctrl+L loads it back.
// A scenario sets the map, rivals and territories itself, and starts with
// no demo network.

#include "audio.hpp"
#include "build_info.hpp"
#include "editor.hpp"
#include "png.hpp"
#include "title.hpp"
#include "font.hpp"
#include "model.hpp"
#include "render.hpp"
#include "shading.hpp"
#include "ui.hpp"
#include "tools.hpp"
#include "gestures.hpp"
#include "goals.hpp"
#include "tutorial.hpp"

#include "railmaster/net/session.hpp"
#include "railmaster/sim/demo.hpp"
#include "railmaster/sim/game_speed.hpp"
#include "railmaster/sim/world.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <new>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

namespace sim = railmaster::sim;
namespace client = railmaster::client;
namespace net = railmaster::net;


std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Where the game's data is: installed beside the program (cmake --install
// puts it at RAILMASTER_INSTALLED_DATA, relative to the program's folder),
// or in a Mac app's Resources (SDL's base path there) as data/; else the
// source tree the build came from.
std::string default_data_dir() {
    if (char* base = SDL_GetBasePath()) {
        const std::filesystem::path at(base);
        SDL_free(base);
        for (const std::filesystem::path& dir : {at / RAILMASTER_INSTALLED_DATA, at / "data"}) {
            std::error_code ec;
            if (std::filesystem::exists(dir / "balance.json", ec)) return dir.lexically_normal().string();
        }
    }
    return RAILMASTER_DEFAULT_DATA_DIR;
}

sim::GameData load_game_data(const std::string& dir) {
    sim::GameData d;
    d.balance = sim::Balance::from_json(read_file(dir + "/balance.json"));
    d.cargo = sim::CargoRegistry::from_json(read_file(dir + "/cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = sim::LocomotiveRegistry::from_json(read_file(dir + "/locomotives.json"));
    d.industries = sim::IndustryRegistry::from_json(read_file(dir + "/industries.json"), d.cargo);
    d.tycoons = sim::TycoonRegistry::from_json(read_file(dir + "/tycoons.json"));
    return d;
}

} // namespace

// A hand-made landscape from an image (data/maps; the first step of the map
// editor, docs/ROADMAP.md M5): the image stretched over the config's map size.
// Problems are reported and leave the map generated.
std::optional<sim::Landscape> read_landscape(const std::string& path, const sim::WorldConfig& cfg, std::int32_t max_height_m) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* px = stbi_load(path.c_str(), &w, &h, &channels, 3);
    if (!px) {
        std::fprintf(stderr, "Cannot read %s: %s\n", path.c_str(), stbi_failure_reason());
        return std::nullopt;
    }
    std::optional<sim::Landscape> l;
    try {
        l = sim::landscape_from_image(px, w, h, cfg.width_tiles, cfg.height_tiles, max_height_m, 24);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
    }
    stbi_image_free(px);
    return l;
}

// A map as a heightmap image, the format read_landscape takes back: a pixel
// a cell, its brightness the cell's height and water blue.
bool save_map_image(const sim::Terrain& t, std::int32_t max_height_m, const std::string& path) {
    std::vector<std::uint8_t> rgb;
    rgb.reserve(static_cast<std::size_t>(t.width()) * static_cast<std::size_t>(t.height()) * 3);
    for (std::int32_t y = 0; y < t.height(); ++y) {
        for (std::int32_t x = 0; x < t.width(); ++x) {
            if (t.ground(x, y) == sim::GroundType::Water) {
                rgb.insert(rgb.end(), {40, 80, 190});
                continue;
            }
            const std::int64_t h = (std::int64_t{t.corner_height(x, y)} + t.corner_height(x + 1, y) + t.corner_height(x, y + 1) +
                                    t.corner_height(x + 1, y + 1)) / 4;
            // Dry land stays above the sea line (24 of 255) however low it lies.
            const auto grey = static_cast<std::uint8_t>(std::clamp<std::int64_t>(h * 255 / std::max(1, max_height_m), 26, 255));
            rgb.insert(rgb.end(), {grey, grey, grey});
        }
    }
    return client::write_png(path, t.width(), t.height(), rgb);
}

// A map's towns and industries, kept beside its image as NAME.json: the
// map's size; for each town its name, place (map cells) and houses; for
// each industry its type and place. Read for a map of another size, the
// places are stretched to fit.
struct MapExtras {
    std::vector<sim::ScenarioTown> towns;
    std::vector<sim::MapIndustry> industries;
    std::optional<sim::TerritoryMap> territories;
};

MapExtras read_map_extras(const std::string& image, const sim::WorldConfig& cfg) {
    MapExtras m;
    std::ifstream in(std::filesystem::path(image).replace_extension(".json"));
    if (!in) return m;
    try {
        const nlohmann::json doc = nlohmann::json::parse(in);
        const std::int32_t w = std::max(1, doc.value("width", cfg.width_tiles)), h = std::max(1, doc.value("height", cfg.height_tiles));
        const auto fit_x = [&](const nlohmann::json& j) {
            return std::clamp(static_cast<std::int32_t>(std::int64_t{j.value("x", 0)} * cfg.width_tiles / w), 0, cfg.width_tiles - 1);
        };
        const auto fit_y = [&](const nlohmann::json& j) {
            return std::clamp(static_cast<std::int32_t>(std::int64_t{j.value("y", 0)} * cfg.height_tiles / h), 0, cfg.height_tiles - 1);
        };
        for (const auto& t : doc.value("towns", nlohmann::json::array())) {
            m.towns.push_back({t.value("name", std::string("Unnamed")), fit_x(t), fit_y(t), std::clamp(t.value("houses", 25), 1, 200)});
        }
        for (const auto& i : doc.value("industries", nlohmann::json::array())) {
            m.industries.push_back({i.value("type", std::string()), fit_x(i), fit_y(i)});
        }
        if (doc.contains("territories")) {
            // Each territory's terms, and the cells as runs of [territory, count]
            // (-1 for none), row by row at the saved size.
            const nlohmann::json& t = doc["territories"];
            sim::TerritoryMap tm;
            for (const auto& j : t.value("list", nlohmann::json::array())) {
                sim::Territory terr;
                terr.name = j.value("name", std::string("Unnamed"));
                terr.access_cost = sim::Money::dollars(j.value("access_cost", std::int64_t{0}));
                terr.credit_grades = j.value("credit_grades", 0);
                terr.station_cost_percent = j.value("station_cost_percent", 100);
                terr.overhead_percent = j.value("overhead_percent", 100);
                terr.closed_border = j.value("closed_border", false);
                tm.territories.push_back(terr);
            }
            std::vector<sim::TerritoryId> saved;
            saved.reserve(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
            for (const auto& run : t.value("cells", nlohmann::json::array())) {
                const int id = run.at(0).get<int>();
                const auto count = run.at(1).get<std::size_t>();
                const sim::TerritoryId v = id < 0 || static_cast<std::size_t>(id) >= tm.territories.size() ? sim::kNoTerritory
                                                                                                          : static_cast<sim::TerritoryId>(id);
                saved.insert(saved.end(), std::min(count, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) - saved.size()), v);
            }
            saved.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), sim::kNoTerritory);
            tm.width = cfg.width_tiles;
            tm.height = cfg.height_tiles;
            tm.cells.resize(static_cast<std::size_t>(tm.width) * static_cast<std::size_t>(tm.height));
            for (std::int32_t y = 0; y < tm.height; ++y) {
                for (std::int32_t x = 0; x < tm.width; ++x) {
                    const std::int64_t sx = std::int64_t{x} * w / tm.width, sy = std::int64_t{y} * h / tm.height;
                    tm.cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(tm.width) + static_cast<std::size_t>(x)] =
                        saved[static_cast<std::size_t>(sy * w + sx)];
                }
            }
            if (!tm.territories.empty()) m.territories = std::move(tm);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: %s\n", image.c_str(), e.what());
    }
    return m;
}

bool write_map_extras(const std::string& image, std::int32_t width, std::int32_t height, const MapExtras& m) {
    const auto path = std::filesystem::path(image).replace_extension(".json");
    if (m.towns.empty() && m.industries.empty() && !m.territories) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return true;
    }
    nlohmann::json doc = {{"width", width}, {"height", height}, {"towns", nlohmann::json::array()}, {"industries", nlohmann::json::array()}};
    for (const sim::ScenarioTown& t : m.towns) doc["towns"].push_back({{"name", t.name}, {"x", t.x}, {"y", t.y}, {"houses", t.houses}});
    for (const sim::MapIndustry& i : m.industries) doc["industries"].push_back({{"type", i.type}, {"x", i.x}, {"y", i.y}});
    if (m.territories) {
        nlohmann::json list = nlohmann::json::array(), cells = nlohmann::json::array();
        for (const sim::Territory& t : m.territories->territories) {
            list.push_back({{"name", t.name},
                            {"access_cost", t.access_cost.whole_dollars()},
                            {"credit_grades", t.credit_grades},
                            {"station_cost_percent", t.station_cost_percent},
                            {"overhead_percent", t.overhead_percent},
                            {"closed_border", t.closed_border}});
        }
        const auto& c = m.territories->cells;
        for (std::size_t i = 0; i < c.size();) {
            std::size_t j = i;
            while (j < c.size() && c[j] == c[i]) ++j;
            cells.push_back({c[i] == sim::kNoTerritory ? -1 : static_cast<int>(c[i]), j - i});
            i = j;
        }
        doc["territories"] = {{"list", list}, {"cells", cells}};
    }
    std::ofstream out(path);
    out << doc.dump(1) << "\n";
    return static_cast<bool>(out);
}

// The config for a scenario file: the scenario's own, and, when it plays on
// a hand-made map, that map (its image named relative to the file) with
// its industries and territories. Throws on a bad file.
sim::WorldConfig scenario_world_config(const std::string& path, std::int32_t max_height_m) {
    const sim::Scenario s = sim::Scenario::from_json(read_file(path));
    sim::WorldConfig cfg = sim::scenario_config(s);
    if (!s.landscape.empty()) {
        const std::string image = (std::filesystem::path(path).parent_path() / s.landscape).lexically_normal().string();
        cfg.landscape = read_landscape(image, cfg, max_height_m);
        if (!cfg.landscape) throw std::runtime_error("cannot read its map, " + image);
        MapExtras extras = read_map_extras(image, cfg);
        cfg.industries = std::move(extras.industries);
        cfg.territory_map = std::move(extras.territories);
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    // An app opened from the Finder or Launchpad starts in the root folder,
    // where nothing can be saved: work from the game's own folder instead,
    // so quick saves (railmaster.sav) have somewhere to go.
    {
        std::error_code ec;
        if (std::filesystem::current_path(ec) == std::filesystem::path("/")) {
            if (char* pref = SDL_GetPrefPath("Railmaster", "Railmaster")) {
                std::filesystem::current_path(pref, ec);
                SDL_free(pref);
            }
        }
    }
    std::string data_dir = default_data_dir();
    bool empty = false;
    bool quick = false; // skip the founding dialog: found on the usual terms
    bool use_shaders = true; // --no-shaders: the fixed-function renderer
    std::optional<std::pair<float, float>> look; // --look=X,Y: where the view starts
    int rivals = 3;
    sim::MapSize map_size = sim::MapSize::Small;
    int territories = 0;
    std::string scenario_path;
    std::string load_path;
    std::string landscape_path; // --landscape=FILE: a heightmap image for the map
    // Multiplayer: --host[=PORT] opens a new game to others on the network,
    // --join=ADDRESS[:PORT] joins one; --name=NAME is how others see you.
    std::optional<std::uint16_t> host_port;
    std::string join_address;
    std::string player_name;
    if (const char* user = std::getenv("USER")) player_name = user;
    if (player_name.empty()) player_name = "Player";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version") {
            std::printf("railmaster %s (%s)\n", client::kBuildCommit, client::kBuildDate);
            return 0;
        }
        if (arg == "--no-shaders") use_shaders = false;
        else if (arg.rfind("--look=", 0) == 0) {
            const auto comma = arg.find(',');
            if (comma != std::string::npos) look = {std::stof(arg.substr(7, comma - 7)), std::stof(arg.substr(comma + 1))};
        }
        else if (arg == "--empty") empty = true;
        else if (arg == "--quick") quick = true;
        else if (arg.rfind("--rivals=", 0) == 0) rivals = std::stoi(arg.substr(9));
        else if (arg.rfind("--territories=", 0) == 0) territories = std::stoi(arg.substr(14));
        else if (arg.rfind("--scenario=", 0) == 0) scenario_path = arg.substr(11);
        else if (arg.rfind("--load=", 0) == 0) load_path = arg.substr(7);
        else if (arg.rfind("--landscape=", 0) == 0) landscape_path = arg.substr(12);
        else if (arg == "--host") host_port = net::kDefaultPort;
        else if (arg.rfind("--host=", 0) == 0) host_port = static_cast<std::uint16_t>(std::stoi(arg.substr(7)));
        else if (arg.rfind("--join=", 0) == 0) join_address = arg.substr(7);
        else if (arg.rfind("--name=", 0) == 0) player_name = arg.substr(7);
        else if (arg == "--map=small") map_size = sim::MapSize::Small;
        else if (arg == "--map=medium") map_size = sim::MapSize::Medium;
        else if (arg == "--map=large") map_size = sim::MapSize::Large;
        else data_dir = arg;
    }

    sim::GameData data;
    try {
        data = load_game_data(data_dir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Failed to load game data from %s: %s\n", data_dir.c_str(), e.what());
        return 1;
    }

    sim::WorldConfig config;
    config.rivals = rivals;
    config.set_map_size(map_size);
    config.territories = territories;
    if (!landscape_path.empty()) {
        config.landscape = read_landscape(landscape_path, config, data.balance.map.max_height_m);
        MapExtras extras = read_map_extras(landscape_path, config);
        config.towns = std::move(extras.towns);
        config.industries = std::move(extras.industries);
        config.territory_map = std::move(extras.territories);
    }
    if (!scenario_path.empty()) {
        try {
            config = scenario_world_config(scenario_path, data.balance.map.max_height_m);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Failed to load scenario %s: %s\n", scenario_path.c_str(), e.what());
            return 1;
        }
        empty = true;
    }
    // A hosted game has seats for four players (the host's and three more);
    // with no other game chosen, a new map, each player founding a company.
    if (host_port) {
        config.humans = 4;
        if (scenario_path.empty() && load_path.empty() && !quick) empty = true;
    }
    // Joining: the host's game replaces this one, so make it small and quick.
    if (!join_address.empty()) {
        config = sim::WorldConfig{};
        config.width_tiles = config.height_tiles = 64;
        config.rivals = 0;
        config.populate = false;
        empty = true;
        quick = false;
        load_path.clear();
        host_port.reset();
    }
    // Started with no game chosen on the command line: the title screen,
    // over the demo railway running on its own.
    const bool title_at_start = !quick && !empty && scenario_path.empty() && load_path.empty() && !look;
    config.found_player_company = quick || !load_path.empty() || title_at_start;
    sim::World world(config, std::move(data));
    if (!load_path.empty()) {
        try {
            world = sim::World::load(read_file(load_path), load_game_data(data_dir));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Failed to load %s: %s\n", load_path.c_str(), e.what());
            return 1;
        }
        empty = true;
    }
    // The demo network is built once the player has a company to pay for it.
    bool demo_pending = !empty;
    // A game over the network (src/net): hosting it or joining one. Either
    // goes when the game is left or another one started.
    std::unique_ptr<net::Host> host;
    std::unique_ptr<net::Guest> guest;
    if (!join_address.empty()) {
        std::printf("Joining %s...\n", join_address.c_str());
        try {
            guest = std::make_unique<net::Guest>(world, join_address, player_name, load_game_data(data_dir));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Cannot join %s: %s\n", join_address.c_str(), e.what());
            return 1;
        }
        std::printf("Joined as player %d\n", guest->seat() + 1);
        if (!world.execute(sim::RenamePlayer{player_name}).ok) {
            (void)world.execute(sim::RenamePlayer{player_name + " " + std::to_string(guest->seat() + 1)});
        }
    }
    std::string host_address; // where others join the hosted game: "192.168.1.20" (":port" if not the usual)
    // Open the game being played to others; false (with the reason shown) if it cannot be.
    const auto open_to_others = [&](std::uint16_t port, const std::string& name, std::string& error) {
        try {
            host = std::make_unique<net::Host>(world, port);
            host->set_name(name);
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
        // Others see the host by name (in the news, the market).
        if (!name.empty()) (void)world.execute(sim::RenamePlayer{name});
        const auto addresses = net::local_addresses();
        host_address = addresses.empty() ? std::string("this computer's address") : addresses.front();
        if (host->port() != net::kDefaultPort) host_address += ":" + std::to_string(host->port());
        return true;
    };
    if (host_port) {
        std::string error;
        if (!open_to_others(*host_port, player_name, error)) {
            std::fprintf(stderr, "Cannot host: %s\n", error.c_str());
            return 1;
        }
    }
    const auto leave_network = [&] {
        guest.reset();
        host.reset();
    };
    // A game others are playing too: the host's newspaper, menu and ledger
    // do not stop everyone's clock (the host can still pause it).
    const auto shared_game = [&] { return guest || (host && host->players().size() > 1); };

#ifdef SDL_HINT_MAC_SCROLL_MOMENTUM
    // A flick on a Mac trackpad carries on a little, as in a map app.
    SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "Sdl_init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    // Smooth edges: 4x multisampling, if the system offers it (else plain).
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    client::Camera cam;
    cam.mm_per_tile = static_cast<float>(world.terrain().tile_size_m()) * 1000.0f;
    cam.terrain = &world.terrain();
    cam.target_x = static_cast<float>(world.terrain().width()) / 2;
    cam.target_y = static_cast<float>(world.terrain().height()) / 2;
    // Open on the biggest town, close enough to see its streets.
    const auto look_at_biggest_town = [&] {
        cam.mm_per_tile = static_cast<float>(world.terrain().tile_size_m()) * 1000.0f;
        cam.terrain = &world.terrain();
        cam.target_x = static_cast<float>(world.terrain().width()) / 2;
        cam.target_y = static_cast<float>(world.terrain().height()) / 2;
        const sim::Economy& eco = world.economy();
        std::int64_t best = -1;
        for (std::size_t t = 0; t < eco.towns().size(); ++t) {
            const std::int64_t houses = eco.town_houses(t);
            if (houses <= best) continue;
            best = houses;
            const auto k = static_cast<float>(eco.cells_per_node());
            cam.target_x = (static_cast<float>(eco.towns()[t].cx) + 0.5f) * k;
            cam.target_y = (static_cast<float>(eco.towns()[t].cy) + 0.5f) * k;
        }
        if (best >= 0) cam.distance = 14.0f;
    };
    look_at_biggest_town();
    if (look) {
        cam.target_x = look->first;
        cam.target_y = look->second;
        cam.distance = 4.5f;
    }
    const std::string build_label = std::string("Build ") + client::kBuildCommit + "  " + client::kBuildDate;
    const std::string window_title = std::string("Railmaster (") + client::kBuildCommit + ")";
    // High-DPI: draw at the screen's full resolution on Retina displays.
    const Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window* window = SDL_CreateWindow(window_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          cam.width_px, cam.height_px, window_flags);
    if (!window) {
        // No multisampling to be had: try again without it.
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow(window_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cam.width_px,
                                  cam.height_px, window_flags);
    }
    if (!window) {
        std::fprintf(stderr, "Sdl_createwindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        // Some drivers refuse a multisampled context: fall back to a plain one.
        SDL_DestroyWindow(window);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow(window_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cam.width_px,
                                  cam.height_px, window_flags);
        gl = window ? SDL_GL_CreateContext(window) : nullptr;
    }
    if (!gl) {
        std::fprintf(stderr, "Sdl_gl_createcontext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    client::watch_pinch(); // a Mac trackpad's pinch zooms
    // The window's icon (the desktop entry has the same picture).
    {
        int iw = 0, ih = 0, channels = 0;
        if (stbi_uc* px = stbi_load((data_dir + "/icon.png").c_str(), &iw, &ih, &channels, 4)) {
            if (SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormatFrom(px, iw, ih, 32, iw * 4, SDL_PIXELFORMAT_RGBA32)) {
                SDL_SetWindowIcon(window, icon);
                SDL_FreeSurface(icon);
            }
            stbi_image_free(px);
        }
    }
    std::printf("data: %s\n", data_dir.c_str());
    std::printf("renderer: %s\n", client::shading::init(use_shaders) ? "shaders and shadows" : "fixed-function");
    client::models::init(data_dir);
    client::init_font(data_dir + "/fonts/SourceSans3-Semibold.ttf");
    std::printf("sound: %s\n", client::audio::init() ? "on" : "no audio device");
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    client::Tools tools(world, cam);
    if (world.scenario()) {
        // Open on the briefing, looking at the scenario's first town.
        tools.select(client::Tool::Status);
        if (!world.economy().towns().empty()) {
            const sim::Town& t = world.economy().towns().front();
            cam.center_on(world.economy().node_centre(t.cx, t.cy));
        }
    }
    if (!world.player_company() && !title_at_start) tools.open_founding(/*cancellable=*/false);
    sim::SpeedControl speed; // every game starts paused [D]
    // Where the others join, on screen (and in the message history).
    const auto announce_host = [&] {
        const std::string text = "Others join this game at " + host_address + " (Play together, Join at)";
        std::printf("%s\n", text.c_str());
        tools.show(text, true);
        tools.log(text, true);
    };
    if (host) announce_host();
    if (guest) speed.toggle_pause(); // the host's game sets the pace
    std::size_t choices_seen = 0; // scenario questions waiting, as of the last frame
    std::int32_t ledger_year = world.date().year(); // the ledger opens when a new year starts
    bool modal_paused = false; // the newspaper or menu paused the game, and closing it resumes
    // A saved game watched from its start (the title screen's load page, W):
    // its logged commands are given as their ticks come round, while the
    // player only watches, until Ctrl+R takes the game over from there.
    struct Watching {
        std::vector<sim::World::LoggedCommand> log;
        std::size_t next = 0;
        std::uint64_t end = 0; // the tick the save was made at
        std::string name, end_label;
        bool done = false;
    };
    std::optional<Watching> watching;
    // Saving and loading, from Ctrl+S and Ctrl+L (the quick save, in the
    // working directory) and the game menu's slots (in the player's own
    // folder). Beside each save, a line saying what it holds, for the menu.
    static constexpr const char* kQuickSave = "railmaster.sav";
    if (char* pref = SDL_GetPrefPath("Railmaster", "Railmaster")) {
        tools.set_save_paths(pref, kQuickSave);
        SDL_free(pref);
    }
    // The settings, kept in the player's folder as lines of "name value".
    std::string settings_path;
    if (char* pref = SDL_GetPrefPath("Railmaster", "Railmaster")) {
        settings_path = std::string(pref) + "settings.txt";
        SDL_free(pref);
    }
    {
        std::ifstream in(settings_path);
        std::string name;
        int value = 0;
        client::Tools::Settings& st = tools.settings();
        while (in >> name >> value) {
            if (name == "volume") st.volume = std::clamp(value, 0, 10);
            else if (name == "edge_scroll") st.edge_scroll = value != 0;
            else if (name == "time_of_day") st.time_mode = std::clamp(value, 0, 4);
            else if (name == "show_fps") st.show_fps = value != 0;
            else if (name == "cheats") st.cheats = value != 0;
            else if (name == "seasons") st.seasons = value != 0;
            else if (name == "clouds") st.clouds = value != 0;
            else if (name == "reverse_zoom") st.reverse_zoom = value != 0;
            else if (name == "scroll_mode") st.scroll_mode = std::clamp(value, 0, 2);
            else if (name == "music") st.music = std::clamp(value, 0, 10);
        }
    }
    const auto save_settings = [&] {
        if (settings_path.empty()) return;
        const client::Tools::Settings& st = tools.settings();
        std::ofstream out(settings_path);
        out << "volume " << st.volume << "\nedge_scroll " << st.edge_scroll << "\ntime_of_day " << st.time_mode
            << "\nshow_fps " << st.show_fps << "\ncheats " << st.cheats << "\nseasons " << st.seasons << "\nclouds " << st.clouds << "\nmusic " << st.music
            << "\nreverse_zoom " << st.reverse_zoom << "\nscroll_mode " << st.scroll_mode << "\n";
    };
    const auto save_game = [&](const std::string& file, bool quiet = false) {
        try {
            std::ofstream out(file, std::ios::binary);
            const std::string bytes = world.save();
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!out) throw std::runtime_error("cannot write " + file);
            std::ofstream about(file + ".txt");
            about << world.date().month_year_label() << ", "
                  << (world.player_company() ? world.company().name() : std::string("no company")) << ", net worth "
                  << client::format_money(sim::net_worth(world.investor(), world.market()))
                  << (world.assisted() ? ", assisted" : "") << "\n";
            if (!quiet) tools.show("Game saved to " + file, true);
        } catch (const std::exception& e) {
            tools.show(e.what(), false);
        }
    };
    const auto load_game = [&](const std::string& file) {
        try {
            leave_network();
            world = sim::World::load(read_file(file), load_game_data(data_dir));
            watching.reset();
            demo_pending = false;
            tools.select(tools.tool()); // drop half-made lines and routes
            // What the loaded game has already been told.
            ledger_year = world.date().year();
            choices_seen = world.pending_choices().size();
            (void)world.take_news();
            (void)world.take_headlines();
            tools.show("Game loaded from " + file + ": " + world.date().month_year_label(), true);
            return true;
        } catch (const std::exception& e) {
            tools.show(e.what(), false);
            return false;
        }
    };
    const auto save_slots = [&] {
        std::vector<std::pair<std::string, std::string>> slots;
        for (int i = 0; i <= client::Tools::kAutosaveSlot; ++i) {
            const std::string path = tools.slot_path(i);
            slots.emplace_back(path, client::save_summary(path));
        }
        return slots;
    };
    // The player's own maps, which the map editor saves.
    std::string maps_dir;
    if (char* pref = SDL_GetPrefPath("Railmaster", "Railmaster")) {
        maps_dir = std::string(pref) + "maps";
        SDL_free(pref);
    }
    // And their scenarios.
    std::string scenarios_dir;
    if (char* pref = SDL_GetPrefPath("Railmaster", "Railmaster")) {
        scenarios_dir = std::string(pref) + "scenarios";
        SDL_free(pref);
    }
    client::Title title(data_dir, maps_dir, scenarios_dir, save_slots());
    // The name others see and the last host joined, kept in the player's folder.
    const std::string player_path = settings_path.empty() ? std::string() : settings_path.substr(0, settings_path.size() - 12) + "player.txt";
    {
        std::ifstream in(player_path);
        std::string name, address;
        std::getline(in, name);
        std::getline(in, address);
        title.set_player(name.empty() ? player_name : name, address);
    }
    const auto save_player = [&] {
        if (player_path.empty()) return;
        std::ofstream out(player_path);
        out << title.player_name() << "\n" << title.join_address() << "\n";
    };
    client::Editor editor(world, cam);
    client::Tutorial tutorial;
    client::Goals goals; // a free game's goals, once the first steps are done
    // A free game shows its first steps once the company is founded.
    bool first_steps_pending = false;
    std::string editor_path; // where the map being edited was last saved
    if (title_at_start) {
        title.show();
        speed.toggle_pause(); // the railway behind the title runs
    }
    // A new game from the title screen: a fresh world and fresh tools.
    // `game_data`: the game data to play with (a replay's comes from its save);
    // `watch`: a replay to watch rather than a game to play.
    const auto start_world = [&](const sim::WorldConfig& cfg, std::optional<sim::GameData> game_data = std::nullopt, bool watch = false) {
        tutorial.end();
        leave_network();
        try {
            world = sim::World(cfg, game_data ? std::move(*game_data) : load_game_data(data_dir));
        } catch (const std::exception& e) {
            tools.show(e.what(), false);
            return false;
        }
        if (!watch) watching.reset();
        demo_pending = false;
        // The tools keep only choices in progress; start them afresh.
        // (C++20 lets an object with reference members be replaced in place.)
        const client::Tools::Settings kept = tools.settings();
        tools.~Tools();
        new (&tools) client::Tools(world, cam);
        tools.settings() = kept;
        if (char* pref = SDL_GetPrefPath("Railmaster", "Railmaster")) {
            tools.set_save_paths(pref, kQuickSave);
            SDL_free(pref);
        }
        look_at_biggest_town();
        ledger_year = world.date().year();
        choices_seen = 0;
        for (const std::string& n : world.take_news()) tools.log(n, true);
        (void)world.take_headlines();
        if (world.scenario()) tools.select(client::Tool::Status);
        if (!world.player_company() && !watch) tools.open_founding(/*cancellable=*/false);
        if (speed.speed() != sim::GameSpeed::Paused) speed.toggle_pause(); // every game starts paused [D]
        modal_paused = false;
        title.hide();
        return true;
    };
    // Watch a saved game from its start: a new world from the save's config
    // and balance, given the save's commands as it runs (World::play_logged).
    const auto start_replay = [&](const std::string& file) {
        try {
            const sim::World saved = sim::World::load(read_file(file), load_game_data(data_dir));
            sim::GameData d = load_game_data(data_dir);
            d.balance = saved.data().balance; // played with the save's balance, as load does
            Watching w{saved.command_log(), 0, saved.total_ticks(), std::filesystem::path(file).stem().string(),
                       saved.date().month_year_label(), false};
            watching = std::move(w);
            if (!start_world(saved.config(), std::move(d), true)) {
                watching.reset();
                return;
            }
            world.set_watch_only(true);
            speed.toggle_pause(); // it plays at once
            tools.show("Watching " + watching->name + " from its start, to " + watching->end_label +
                           ". +/- change speed, Space pauses, Ctrl+R takes the game over",
                       true);
        } catch (const std::exception& e) {
            watching.reset();
            tools.show(e.what(), false);
        }
    };
    // The map editor: a map with nothing on it yet, to shape and save.
    const auto start_editor = [&](const client::Title::Request& req) {
        sim::WorldConfig cfg = req.config;
        cfg.rivals = 0;
        cfg.populate = false;
        cfg.found_player_company = false;
        cfg.industries_appear = false;
        cfg.town_growth = false;
        cfg.business_cycle = false;
        sim::GameData d = load_game_data(data_dir);
        const std::int32_t max_h = d.balance.map.max_height_m;
        std::string name = "New map";
        if (req.flat) cfg.landscape = sim::flat_landscape(cfg.width_tiles, cfg.height_tiles, max_h / 6);
        else if (!req.path.empty()) {
            cfg.landscape = read_landscape(req.path, cfg, max_h);
            name = std::filesystem::path(req.path).stem().string();
        }
        const MapExtras extras = req.path.empty() ? MapExtras{} : read_map_extras(req.path, cfg);
        leave_network();
        try {
            world = sim::World(cfg, std::move(d));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Map editor: %s\n", e.what());
            return;
        }
        watching.reset();
        const client::Tools::Settings kept = tools.settings();
        tools.~Tools();
        new (&tools) client::Tools(world, cam);
        tools.settings() = kept;
        look_at_biggest_town();
        cam.distance = static_cast<float>(std::max(world.terrain().width(), world.terrain().height())) * 0.45f;
        if (speed.speed() != sim::GameSpeed::Paused) speed.toggle_pause();
        modal_paused = false;
        (void)world.take_news();
        (void)world.take_headlines();
        // A map of the player's own is saved back where it came from; the
        // shipped maps and new ones are saved as new maps.
        editor_path.clear();
        if (!req.path.empty() && !maps_dir.empty() &&
            std::filesystem::path(req.path).parent_path() == std::filesystem::path(maps_dir)) {
            editor_path = req.path;
        }
        editor.begin(name, max_h);
        editor.set_towns(extras.towns);
        editor.set_industries(extras.industries);
        if (extras.territories) editor.set_territories(*extras.territories);
        title.hide();
    };
    // What happened while the game was set up (rivals founding) goes in the
    // message history without a newspaper.
    for (const std::string& n : world.take_news()) tools.log(n, true);
    (void)world.take_headlines();
    double tick_accumulator = 0.0;
    float smoke_clock = 0.0f; // seconds of unpaused play, for the smoke
    // The time of day: N cycles day, dusk, night, dawn and a slow round of
    // all four while the game runs (one every six minutes).
    float time_phase = tools.settings().time_mode < 4 ? 0.25f * static_cast<float>(tools.settings().time_mode) : 0.0f;
    static constexpr const char* kTimeNames[] = {"Day", "Dusk", "Night", "Dawn", "Day and night"};
    double frame_ms = 16.7;   // smoothed time a frame takes
    int right_drag = 0; // pixels the mouse moved with the right button down
    // Camera bookmarks: Shift+0-9 stores the view, 0-9 returns to it (rt3-clone-spec §15.6).
    struct Bookmark {
        bool set = false;
        float x = 0, y = 0, distance = 0, yaw = 0, pitch = 0;
    };
    Bookmark bookmarks[10];
    bool hide_ui = false; // Tab: only the world, for a clean look
    float rain = 0;       // how hard it rains or snows now, 0 to 1
    bool snowing = false;
    // The radar, bottom left above the tool bar.
    constexpr float kRadar = 170.0f;
    // The big map (Shift+M): centred, as large as the window allows.
    bool big_map = false;
    bool big_map_links = false;            // Tab on the big map: the Guide's best lines
    std::vector<client::MapLink> map_links; // worked out when shown
    const auto big_size = [&] { return std::min(static_cast<float>(cam.width_px) - 80.0f, static_cast<float>(cam.height_px) - 160.0f); };
    const auto big_x = [&] { return std::max(20.0f, (static_cast<float>(cam.width_px) - big_size()) / 2 - 220.0f); };
    const auto big_y = [&] { return 90.0f; };
    // Which of the shown lines is under a click on the big map: by its
    // number's badge, or within a few pixels of the line.
    const auto link_at = [&](float px, float py) -> std::optional<std::size_t> {
        const float k = big_size() / static_cast<float>(std::max(world.terrain().width(), world.terrain().height()));
        const auto sx = [&](sim::MapPoint p) { return big_x() + static_cast<float>(p.x_mm) / cam.mm_per_tile * k; };
        const auto sy = [&](sim::MapPoint p) { return big_y() + static_cast<float>(p.y_mm) / cam.mm_per_tile * k; };
        std::optional<std::size_t> best;
        float best_d = 8.0f;
        for (std::size_t i = 0; i < map_links.size(); ++i) {
            const float ax = sx(map_links[i].a), ay = sy(map_links[i].a), bx = sx(map_links[i].b), by = sy(map_links[i].b);
            const float mx = (ax + bx) / 2, my = (ay + by) / 2;
            if (std::hypot(px - mx, py - my) <= 11.0f) return i; // the badge
            const float len2 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
            const float t = len2 > 0 ? std::clamp(((px - ax) * (bx - ax) + (py - ay) * (by - ay)) / len2, 0.0f, 1.0f) : 0.0f;
            const float d = std::hypot(px - (ax + (bx - ax) * t), py - (ay + (by - ay) * t));
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        return best;
    };
    const auto radar_x = [] { return 8.0f; };
    const auto radar_y = [&] { return static_cast<float>(cam.height_px) - 44.0f - kRadar - 10.0f; };
    // Zoom buttons beside the radar, for a trackpad that will not pinch:
    // +1 the upper (in), -1 the lower (out), 0 elsewhere. Held, they keep zooming.
    constexpr float kZoomButton = 30.0f;
    const auto zoom_button_at = [&](float x, float y) {
        const float x0 = radar_x() + kRadar + 6, y0 = radar_y();
        if (x < x0 || x >= x0 + kZoomButton) return 0;
        if (y >= y0 && y < y0 + kZoomButton) return 1;
        if (y >= y0 + kZoomButton + 6 && y < y0 + 2 * kZoomButton + 6) return -1;
        return 0;
    };
    int zoom_held = 0;
    // Under them a compass: its needle points north as the view turns, and
    // a click on it (or Home) puts the view back to north-up, as it opens.
    const auto compass_centre = [&] {
        return std::pair{radar_x() + kRadar + 6 + kZoomButton / 2, radar_y() + 2 * kZoomButton + 12 + kZoomButton / 2};
    };
    const auto on_compass = [&](float x, float y) {
        const auto [cx, cy] = compass_centre();
        return std::fabs(x - cx) <= kZoomButton / 2 && std::fabs(y - cy) <= kZoomButton / 2;
    };
    const auto north_up = [&] {
        const client::Camera fresh;
        cam.yaw = fresh.yaw;
        cam.pitch = fresh.pitch;
    };
    // In the Look tool a press on the map waits to see what it is: let go
    // where it was and it is a click; move and it drags the view.
    std::optional<std::pair<float, float>> map_press, map_last;
    bool map_dragging = false;
    // A double click goes to the place: to a town, closer in, if it is on
    // or near one.
    const auto go_to_place = [&](float sx, float sy) {
        const sim::MapPoint at = cam.to_map(sx, sy);
        const sim::Economy& eco = world.economy();
        std::optional<sim::MapPoint> town;
        double best = 0;
        for (const sim::Town& t : eco.towns()) {
            const sim::MapPoint c = eco.node_centre(t.cx, t.cy);
            const double d = std::hypot(static_cast<double>(c.x_mm - at.x_mm), static_cast<double>(c.y_mm - at.y_mm));
            if (!town || d < best) {
                town = c;
                best = d;
            }
        }
        if (town && best <= 10.0 * static_cast<double>(cam.mm_per_tile)) cam.glide_to(*town, std::min(cam.distance, 14.0f));
        else cam.glide_to(at);
    };
    Uint64 last = SDL_GetPerformanceCounter();
    // What each train was doing last frame, to hear it start, stop and be paid.
    struct Heard {
        sim::TrainState state = sim::TrainState::Dwelling;
        std::uint64_t paid_tick = 0;
    };
    std::vector<Heard> heard;
    double last_whistle = 0, clock_s = 0; // seconds; whistles are spaced out
    std::uint64_t heard_ticks = 0;
    static const Uint8 kNoKeys[SDL_NUM_SCANCODES] = {}; // no key held, while the title screen has them

    for (bool running = true; running;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            // The title screen takes the keys and clicks while it is up.
            if (title.open()) {
                if (ev.type == SDL_KEYDOWN) {
                    title.key(ev.key.keysym.sym);
                    continue;
                }
                if (ev.type == SDL_TEXTINPUT) {
                    title.text(ev.text.text);
                    continue;
                }
                if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    client::audio::play(client::audio::Sound::Click, 0.5f);
                    title.click(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y));
                    continue;
                }
                if (ev.type == SDL_MOUSEWHEEL || ev.type == SDL_MULTIGESTURE || ev.type == SDL_MOUSEBUTTONDOWN ||
                    ev.type == SDL_MOUSEBUTTONUP || ev.type == SDL_MOUSEMOTION) {
                    continue;
                }
            }
            // The map editor takes the keys (the arrows and the mouse still move the view).
            if (editor.open() && !title.open()) {
                if (ev.type == SDL_TEXTINPUT) {
                    editor.text(ev.text.text);
                    continue;
                }
                if (ev.type == SDL_KEYDOWN && editor.key(ev.key.keysym.sym)) continue;
                if (ev.type == SDL_KEYDOWN && (ev.key.keysym.mod & KMOD_CTRL) != 0) continue; // no saving or loading games
                if (ev.type == SDL_MOUSEBUTTONDOWN && editor.mouse_down(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y),
                                                                        ev.button.button)) {
                    continue;
                }
                if (ev.type == SDL_MOUSEBUTTONUP) editor.mouse_up(ev.button.button);
                if (ev.type == SDL_MOUSEMOTION) editor.mouse_move(static_cast<float>(ev.motion.x), static_cast<float>(ev.motion.y));
                if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_RIGHT) continue;
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym != SDLK_v && ev.key.keysym.sym != SDLK_n &&
                    ev.key.keysym.sym != SDLK_F12) {
                    continue;
                }
            }
            switch (ev.type) {
            case SDL_QUIT: running = false; break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    cam.width_px = ev.window.data1;
                    cam.height_px = ev.window.data2;
                }
                break;
            case SDL_MOUSEWHEEL: {
                int mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                // Smooth on a trackpad (many small scrolls), a step a notch
                // on a mouse wheel.
#if SDL_VERSION_ATLEAST(2, 0, 18)
                const float amount = std::clamp(ev.wheel.preciseY, -3.0f, 3.0f);
                const float across = ev.wheel.preciseX;
#else
                const float amount = static_cast<float>(std::clamp(ev.wheel.y, -3, 3));
                const float across = static_cast<float>(ev.wheel.x);
#endif
                // Over a list, the wheel scrolls it instead.
                if (!editor.open() && ev.wheel.y != 0 &&
                    tools.on_wheel(static_cast<float>(mx), static_cast<float>(my), ev.wheel.y)) {
                    break;
                }
                // A trackpad (fine steps, or sideways) moves the map, as in a
                // map app, and a pinch zooms; a mouse wheel (whole notches)
                // zooms. Cmd (or Ctrl) with either zooms.
                const bool trackpad = across != 0.0f || std::fabs(amount - std::round(amount)) > 0.001f;
                const int mode = tools.settings().scroll_mode;
                const bool zoom_key = (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI)) != 0;
                const bool move = !zoom_key && (mode == 2 || (mode == 0 && trackpad));
                if (move) {
                    // The map follows the scroll as a page would: the system's own
                    // scrolling direction (natural or not) carries over.
                    constexpr float kPixels = 12.0f; // a scroll step on screen
                    const auto fx = static_cast<float>(mx), fy = static_cast<float>(my);
                    cam.drag(fx, fy, fx - across * kPixels, fy + amount * kPixels);
                    break;
                }
                const float signed_amount = tools.settings().reverse_zoom ? -amount : amount;
                if (amount != 0.0f) cam.zoom_at(static_cast<float>(mx), static_cast<float>(my), std::pow(1.15f, signed_amount));
                tools.on_mouse_move(static_cast<float>(mx), static_cast<float>(my));
                break;
            }
            case SDL_MULTIGESTURE:
                // Pinch on a trackpad or touch screen: spread to zoom in.
                if (std::fabs(ev.mgesture.dDist) > 0.0005f) {
                    cam.zoom_at(static_cast<float>(cam.width_px) / 2, static_cast<float>(cam.height_px) / 2,
                                1.0f + ev.mgesture.dDist * 6.0f);
                }
                break;
            case SDL_MOUSEMOTION:
                // Left-drag on the map (Look tool) moves the view.
                if (map_press && (ev.motion.state & SDL_BUTTON_LMASK) != 0) {
                    const auto x = static_cast<float>(ev.motion.x), y = static_cast<float>(ev.motion.y);
                    if (!map_dragging && std::hypot(x - map_press->first, y - map_press->second) > 5.0f) map_dragging = true;
                    if (map_dragging) cam.drag(map_last->first, map_last->second, x, y);
                    map_last = std::pair{x, y};
                }
                // Right-drag turns and tilts the view.
                if ((ev.motion.state & SDL_BUTTON_RMASK) != 0 && cam.three_d) {
                    right_drag += std::abs(ev.motion.xrel) + std::abs(ev.motion.yrel);
                    if (right_drag > 4) cam.orbit(static_cast<float>(ev.motion.xrel) * 0.008f, static_cast<float>(ev.motion.yrel) * 0.005f);
                }
                tools.on_mouse_move(static_cast<float>(ev.motion.x), static_cast<float>(ev.motion.y));
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_RIGHT) {
                    right_drag = 0;
                    break;
                }
                // A click on the big map goes there and closes it: on one of
                // the Guide's numbered lines, to it with its plan ready.
                if (big_map) {
                    if (big_map_links) {
                        const auto i = link_at(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y));
                        if (i && tools.follow_guide_link(*i)) {
                            big_map = false;
                            break;
                        }
                    }
                    if (const auto p = client::radar_point(world, cam, big_x(), big_y(), big_size(), static_cast<float>(ev.button.x),
                                                           static_cast<float>(ev.button.y))) {
                        cam.glide_to(*p);
                    }
                    big_map = false;
                    break;
                }
                // The zoom buttons: a step now, and on while held.
                if (!tools.founding_open() && ev.button.button == SDL_BUTTON_LEFT) {
                    if (on_compass(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y))) {
                        north_up();
                        break;
                    }
                    if (const int z = zoom_button_at(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y)); z != 0) {
                        zoom_held = z;
                        cam.zoom_at(static_cast<float>(cam.width_px) / 2, static_cast<float>(cam.height_px) / 2, z > 0 ? 1.25f : 0.8f);
                        break;
                    }
                }
                // A click on the radar moves the view there.
                if (const auto p = client::radar_point(world, cam, radar_x(), radar_y(), kRadar, static_cast<float>(ev.button.x),
                                                       static_cast<float>(ev.button.y))) {
                    cam.glide_to(*p);
                    break;
                }
                client::audio::play(client::audio::Sound::Click, 0.35f);
                if (tutorial.click(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y))) break;
                if (!tutorial.open() && !tools.screen_open() && goals.click(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y))) break;
                if (ev.button.button == SDL_BUTTON_LEFT && tools.tool() == client::Tool::Inspect &&
                    !tools.over_ui(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y))) {
                    if (ev.button.clicks >= 2) {
                        go_to_place(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y));
                        map_press.reset();
                        break;
                    }
                    map_press = map_last = std::pair{static_cast<float>(ev.button.x), static_cast<float>(ev.button.y)};
                    map_dragging = false;
                    break;
                }
                tools.on_click(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y), false);
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    zoom_held = 0;
                    // A press on the map that did not drag: the click it was. On
                    // a station's name it opens the station; on a town's, it goes there.
                    if (map_press && !map_dragging) {
                        if (const auto hit = client::label_at(map_press->first, map_press->second)) {
                            if (hit->station) {
                                tools.show_station(hit->id);
                            } else if (hit->id < world.economy().towns().size()) {
                                const sim::Town& t = world.economy().towns()[hit->id];
                                cam.glide_to(world.economy().node_centre(t.cx, t.cy), std::min(cam.distance, 14.0f));
                            }
                        } else {
                            tools.on_click(map_press->first, map_press->second, false);
                        }
                    }
                    map_press.reset();
                    map_dragging = false;
                }
                // A right click that did not turn the view: stop the line or route.
                if (ev.button.button == SDL_BUTTON_RIGHT && right_drag <= 4) {
                    tools.on_click(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y), true);
                }
                break;
            case SDL_TEXTINPUT: tools.on_text(ev.text.text); break;
            case SDL_KEYDOWN:
                // A box being typed in (Find) takes every key.
                if (tools.typing()) {
                    tools.on_key(ev.key.keysym.sym, ev.key.keysym.mod);
                    break;
                }
                // Shift+M opens and closes the big map; Esc closes it.
                if (ev.key.keysym.sym == SDLK_m && (ev.key.keysym.mod & KMOD_SHIFT) != 0) {
                    big_map = !big_map;
                    if (big_map && big_map_links) map_links = tools.guide_links(10);
                    break;
                }
                if (big_map && ev.key.keysym.sym == SDLK_ESCAPE) {
                    big_map = false;
                    break;
                }
                // With the lines shown, 1 to 9 and 0 (the tenth) go to one.
                if (big_map && big_map_links && ev.key.keysym.sym >= SDLK_0 && ev.key.keysym.sym <= SDLK_9) {
                    const std::size_t i = ev.key.keysym.sym == SDLK_0 ? 9 : static_cast<std::size_t>(ev.key.keysym.sym - SDLK_1);
                    if (i < map_links.size() && tools.follow_guide_link(i)) big_map = false;
                    break;
                }
                if (big_map && ev.key.keysym.sym == SDLK_TAB) {
                    big_map_links = !big_map_links;
                    if (big_map_links) map_links = tools.guide_links(10);
                    break;
                }
                // Ctrl+S saves the game, Ctrl+L loads the last save.
                if ((ev.key.keysym.mod & KMOD_CTRL) != 0 &&
                    (ev.key.keysym.sym == SDLK_s || ev.key.keysym.sym == SDLK_l)) {
                    if (ev.key.keysym.sym == SDLK_s) save_game(kQuickSave);
                    else load_game(kQuickSave);
                    break;
                }
                // Home: north up again, at the usual tilt.
                if (ev.key.keysym.sym == SDLK_HOME) {
                    north_up();
                    break;
                }
                // Cmd (or Ctrl) and = or - zoom in and out, as in other Mac apps.
                if ((ev.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0 &&
                    (ev.key.keysym.sym == SDLK_EQUALS || ev.key.keysym.sym == SDLK_PLUS || ev.key.keysym.sym == SDLK_KP_PLUS ||
                     ev.key.keysym.sym == SDLK_MINUS || ev.key.keysym.sym == SDLK_KP_MINUS)) {
                    const bool in = ev.key.keysym.sym != SDLK_MINUS && ev.key.keysym.sym != SDLK_KP_MINUS;
                    cam.zoom_at(static_cast<float>(cam.width_px) / 2, static_cast<float>(cam.height_px) / 2, in ? 1.25f : 0.8f);
                    break;
                }
                // Ctrl+R takes a replay being watched over: the game is the player's from here.
                if ((ev.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0 && ev.key.keysym.sym == SDLK_r && watching) {
                    watching.reset();
                    world.set_watch_only(false);
                    tools.show("The game is yours from here", true);
                    if (!world.player_company()) tools.open_founding(/*cancellable=*/false);
                    break;
                }
                // Ctrl+H hides the trees, to see the track and towns (rt3-clone-spec §15.7).
                if ((ev.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0 && ev.key.keysym.sym == SDLK_h) {
                    cam.show_trees = !cam.show_trees;
                    tools.show(cam.show_trees ? "Trees shown" : "Trees hidden (Ctrl+H shows them)", true);
                    break;
                }
                // (Not in the Company and Ledger screens, where Shift+Tab turns back a page.)
                if (tools.tool() != client::Tool::Finance && tools.tool() != client::Tool::Ledger &&
                    tutorial.key(ev.key.keysym.sym, ev.key.keysym.mod)) {
                    break;
                }
                if (tools.on_key(ev.key.keysym.sym, ev.key.keysym.mod)) break;
                // Game speed: + and - step through the six speeds, Pause
                // stops and resumes [D]. Space pauses too.
                switch (ev.key.keysym.sym) {
                case SDLK_EQUALS:
                case SDLK_PLUS:
                case SDLK_KP_PLUS: speed.faster(); break;
                case SDLK_MINUS:
                case SDLK_KP_MINUS: speed.slower(); break;
                case SDLK_PAUSE:
                case SDLK_SPACE: speed.toggle_pause(); break;
                case SDLK_0:
                case SDLK_1:
                case SDLK_2:
                case SDLK_3:
                case SDLK_4:
                case SDLK_5:
                case SDLK_6:
                case SDLK_7:
                case SDLK_8:
                case SDLK_9: {
                    Bookmark& b = bookmarks[ev.key.keysym.sym - SDLK_0];
                    if ((ev.key.keysym.mod & KMOD_SHIFT) != 0) {
                        b = {true, cam.target_x, cam.target_y, cam.distance, cam.yaw, cam.pitch};
                        tools.show("View " + std::to_string(ev.key.keysym.sym - SDLK_0) + " stored", true);
                    } else if (b.set) {
                        cam.target_x = b.x;
                        cam.target_y = b.y;
                        cam.distance = b.distance;
                        cam.yaw = b.yaw;
                        cam.pitch = b.pitch;
                    }
                    break;
                }
                case SDLK_TAB:
                    // Hide the interface, to look at the railway (rt3-clone-spec §15.7).
                    hide_ui = !hide_ui;
                    if (!hide_ui) tools.show("Interface shown (Tab hides it)", true);
                    break;
                case SDLK_F12:
                    tools.settings().show_fps = !tools.settings().show_fps;
                    save_settings();
                    break;
                case SDLK_n:
                {
                    int& time_mode = tools.settings().time_mode;
                    time_mode = (time_mode + 1) % 5;
                    if (time_mode < 4) time_phase = 0.25f * static_cast<float>(time_mode);
                    tools.show(std::string("Time of day: ") + kTimeNames[time_mode] + "  (N to change)", true);
                    save_settings();
                }
                    break;
                case SDLK_v: {
                    // 3D or the top-down map, looking at the same place.
                    const sim::MapPoint at{static_cast<std::int64_t>(cam.target_x * cam.mm_per_tile),
                                           static_cast<std::int64_t>(cam.target_y * cam.mm_per_tile)};
                    const sim::MapPoint centre = cam.to_map(static_cast<float>(cam.width_px) / 2, static_cast<float>(cam.height_px) / 2);
                    cam.three_d = !cam.three_d;
                    if (!cam.three_d) cam.zoom = 6.0f;
                    cam.center_on(cam.three_d ? centre : at);
                    break;
                }
                default: break;
                }
                break;
            default: break;
            }
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        const double dt = static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency());
        last = now;

        // Arrows pan; with Shift, left and right turn the view and up and
        // down tilt it (rt3-clone-spec §15.6).
        // Behind the title screen the view circles slowly, and the keys are the title's.
        if (title.open() && cam.three_d) cam.orbit(static_cast<float>(dt) * 0.03f, 0);
        const Uint8* keys = title.open() || tools.typing() ? kNoKeys : SDL_GetKeyboardState(nullptr);
        const float pan_speed = static_cast<float>(dt) * 400.0f / cam.zoom;
        const bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        const bool alt = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
        const float turn = static_cast<float>(dt) * 1.5f;
        // A pinch on the trackpad: in where the fingers spread, at the pointer.
        if (const double pinch = client::take_pinch(); pinch != 0.0 && !title.open()) {
            int px = 0, py = 0;
            SDL_GetMouseState(&px, &py);
            cam.zoom_at(static_cast<float>(px), static_cast<float>(py), static_cast<float>(std::clamp(1.0 + pinch, 0.5, 2.0)));
        }
        if (zoom_held != 0) {
            const float step = std::pow(2.0f, static_cast<float>(dt));
            cam.zoom_at(static_cast<float>(cam.width_px) / 2, static_cast<float>(cam.height_px) / 2,
                        zoom_held > 0 ? step : 1.0f / step);
        }
        if (alt) {
            // Option (Alt) + Up/Down zooms, for laptops without a wheel.
            const float step = std::pow(3.0f, static_cast<float>(dt));
            const auto cx = static_cast<float>(cam.width_px) / 2, cy = static_cast<float>(cam.height_px) / 2;
            if (keys[SDL_SCANCODE_UP]) cam.zoom_at(cx, cy, step);
            if (keys[SDL_SCANCODE_DOWN]) cam.zoom_at(cx, cy, 1.0f / step);
        } else if (shift && cam.three_d) {
            if (keys[SDL_SCANCODE_LEFT]) cam.orbit(-turn, 0);
            if (keys[SDL_SCANCODE_RIGHT]) cam.orbit(turn, 0);
            if (keys[SDL_SCANCODE_UP]) cam.orbit(0, turn * 0.6f);
            if (keys[SDL_SCANCODE_DOWN]) cam.orbit(0, -turn * 0.6f);
        } else {
            if (keys[SDL_SCANCODE_LEFT]) cam.pan(-pan_speed, 0);
            if (keys[SDL_SCANCODE_RIGHT]) cam.pan(pan_speed, 0);
            if (keys[SDL_SCANCODE_UP]) cam.pan(0, -pan_speed);
            if (keys[SDL_SCANCODE_DOWN]) cam.pan(0, pan_speed);
        }
        // The mouse at the window's edge scrolls the view, as in RT3 (not
        // while a button is held, so drags and orbits stay put).
        if (SDL_GetMouseFocus() == window && !title.open() && tools.settings().edge_scroll) {
            int mx = 0, my = 0;
            if (SDL_GetMouseState(&mx, &my) == 0) {
                constexpr int kEdge = 4;
                if (mx < kEdge) cam.pan(-pan_speed, 0);
                if (mx >= cam.width_px - kEdge) cam.pan(pan_speed, 0);
                if (my < kEdge) cam.pan(0, -pan_speed);
                if (my >= cam.height_px - kEdge) cam.pan(0, pan_speed);
            }
        }

        if (editor.open() && !title.open()) editor.update(dt);
        frame_ms += (dt * 1000.0 - frame_ms) * 0.05;
        if (tools.take_settings_changed()) {
            // From the menu's Settings page: a fixed time of day takes effect at once.
            if (tools.settings().time_mode < 4) time_phase = 0.25f * static_cast<float>(tools.settings().time_mode);
            save_settings();
        }
        client::audio::set_master(static_cast<float>(tools.settings().volume) / 10.0f);
        client::audio::set_music_volume(static_cast<float>(tools.settings().music) / 10.0f);
        if (tools.settings().time_mode == 4 && speed.speed() != sim::GameSpeed::Paused) time_phase += static_cast<float>(dt) / 360.0f;
        client::shading::set_time_of_day(time_phase);
        {
            // The season from the day of the year: leaves turn from mid
            // September and fall by December; snow lies from late
            // November into March, deepest in midwinter.
            const sim::Date d = world.date();
            const auto doy = static_cast<float>(d.days_since_epoch() - sim::Date::from_ymd(d.year(), 1, 1).days_since_epoch());
            const auto ramp = [](float x, float a, float b) { return std::clamp((x - a) / (b - a), 0.0f, 1.0f); };
            float snow = std::max(1.0f - ramp(doy, 45, 80), ramp(doy, 328, 354));
            float autumn = ramp(doy, 253, 288) * (1.0f - ramp(doy, 314, 344));
            if (!tools.settings().seasons || editor.open()) snow = autumn = 0;
            client::shading::set_season(snow, autumn);
            const bool weather = tools.settings().clouds && !editor.open();
            client::shading::set_clouds(weather);
            // Rain (or snow in winter) when the clouds are thickest.
            const float cover = client::shading::cloud_cover(static_cast<float>(std::fmod(clock_s, 3600.0)));
            rain = weather ? std::clamp((cover - 0.43f) / 0.06f, 0.0f, 1.0f) : 0.0f;
            snowing = snow > 0.5f;
            client::shading::set_rain(rain);
            client::audio::set_rain(rain * (snowing ? 0.0f : 1.0f));
        }
        // Smoke drifts only while the game runs.
        if (speed.speed() != sim::GameSpeed::Paused) smoke_clock += static_cast<float>(dt);

        // Fixed-step simulation, decoupled from frame rate.
        tick_accumulator += dt * static_cast<double>(sim::days_per_second_milli(speed.speed(), world.data().balance.time)) /
                            1000.0 * sim::World::kTicksPerDay;
        // A guest's game runs as far as the host's has, at the host's pace.
        if (guest) {
            guest->poll();
            guest->advance(4096);
            tick_accumulator = 0;
        }
        for (int guard = 0; tick_accumulator >= 1.0 && guard < 4096; ++guard) {
            if (watching && !watching->done) {
                // The save's commands, at the ticks they were given.
                Watching& w = *watching;
                while (w.next < w.log.size() && w.log[w.next].tick <= world.total_ticks()) {
                    const sim::CommandResult r = world.play_logged(w.log[w.next++]);
                    if (!r.ok) {
                        w.done = true;
                        tools.show("The replay went astray (" + r.error + "): Ctrl+R plays on from here", false);
                        break;
                    }
                }
                if (!w.done && world.total_ticks() >= w.end && w.next >= w.log.size()) {
                    w.done = true;
                    if (speed.speed() != sim::GameSpeed::Paused) speed.toggle_pause();
                    tools.show("The end of the replay: " + w.name + " as it was saved. Ctrl+R plays on from here", true);
                    tick_accumulator = 0;
                    break;
                }
            }
            world.tick();
            if (host) host->after_tick();
            tick_accumulator -= 1.0;
        }
        if (host) {
            host->set_pace(static_cast<std::uint8_t>(speed.speed()));
            host->poll();
        }
        // Players joining and leaving, the host gone, the game out of step.
        {
            std::vector<std::string> net_news = host ? host->take_news() : guest ? guest->take_news() : std::vector<std::string>{};
            for (const std::string& n : net_news) {
                tools.show(n, true);
                tools.log(n, true);
            }
            // Chat, and who is in the game now (for the Status screen).
            const bool together = guest ? guest->connected() : host != nullptr;
            std::vector<bool> online;
            const auto here = host ? host->players() : guest ? guest->players() : std::vector<std::pair<sim::PlayerId, std::string>>{};
            for (const auto& [seat, who] : here) {
                if (online.size() <= seat) online.resize(seat + std::size_t{1});
                online[seat] = true;
            }
            tools.set_multiplayer(together, std::move(online));
            if (const auto said = tools.take_chat()) {
                if (host) host->chat(*said);
                else if (guest) guest->chat(*said);
            }
            for (std::string& line : host ? host->take_chat() : guest ? guest->take_chat() : std::vector<std::string>{}) {
                tools.add_chat(std::move(line));
            }
            static bool drift_told = false;
            if (guest && guest->drift() && !drift_told) {
                drift_told = true;
                const std::string text = "Out of step with the host's game (" + *guest->drift() + "): please report this";
                tools.show(text, false);
                tools.log(text, false);
            }
        }
        if (demo_pending && world.player_company()) {
            demo_pending = false;
            try {
                std::printf("%s\n", sim::build_demo_network(world).c_str());
                // Look at it: centre the view on its first station.
                const auto& rw = world.railway();
                if (!rw.stations().empty() && !look) {
                    cam.center_on(rw.track().node(rw.stations().front().node).pos);
                }
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s (carrying on without it)\n", e.what());
            }
        }
        // Behind the title screen the news goes unread.
        if (title.open()) {
            (void)world.take_economy_news();
            (void)world.take_news();
            (void)world.take_headlines();
            ledger_year = world.date().year();
        }
        // The economy turning makes the front page.
        if (const auto turned = world.take_economy_news()) {
            std::string name = sim::economic_state_name(*turned);
            for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            const bool better = static_cast<int>(*turned) >= static_cast<int>(sim::EconomicState::Normal);
            const std::string text = "The economy turns: the country is in " + name;
            tools.show(text, better);
            tools.log(text, better);
            if (!tools.newspaper_open()) tools.open_newspaper(text, {});
        }
        // The lists and train paging move the view.
        if (const auto p = tools.take_jump()) cam.glide_to(*p);
        cam.update(static_cast<float>(dt));
        if (!title.open() && !editor.open()) tools.check_trains();
        // A scenario event's question stops the clock until it is answered,
        // as RT3's event dialogs do.
        // (Not in a replay: its answer comes from the save.)
        if (world.pending_choices().size() > choices_seen && speed.speed() != sim::GameSpeed::Paused && !watching && !shared_game())
            speed.toggle_pause();
        choices_seen = world.pending_choices().size();
        // The year's books are closed: show the ledger, paused, as RT3 does.
        if (world.date().year() != ledger_year) {
            ledger_year = world.date().year();
            // Keep the game safe: save it at every new year, quietly.
            if (world.player_company() && !title.open() && !editor.open() && !watching) {
                save_game(tools.slot_path(client::Tools::kAutosaveSlot), true);
            }
            if (world.player_company() && !tools.founding_open() && !watching) {
                tools.open_ledger();
                if (speed.speed() != sim::GameSpeed::Paused && !shared_game()) speed.toggle_pause();
            }
        }
        // News: splits, grumbling investors, chairmen fired or appointed,
        // industries opening and closing, scenario news and disasters. All
        // of it goes in the message history (M); headlines make the paper.
        if (const auto news = world.take_news(); !news.empty()) {
            const auto bad_news = [](const std::string& n) {
                for (const char* word : {"voted", "grumbling", "bankruptcy", "closed", "Earthquake", "Sabotage", "no medal"}) {
                    if (n.find(word) != std::string::npos) return true;
                }
                return false;
            };
            std::string text;
            bool bad = false;
            for (const std::string& n : news) {
                text += (text.empty() ? "" : "   ") + n;
                bad |= bad_news(n);
                tools.log(n, !bad_news(n));
            }
            tools.show(text, !bad);
        }
        if (auto heads = world.take_headlines(); !heads.empty() && watching) {
            // A replay runs on: its headlines go in the message history.
            for (const std::string& h : heads) tools.log(h, true);
        } else if (!heads.empty()) {
            const std::string first = heads.front();
            heads.erase(heads.begin());
            tools.open_newspaper(first, std::move(heads));
        }
        // The game menu's requests.
        if (const auto req = tools.take_menu_request()) {
            switch (req->kind) {
            case client::Tools::MenuRequest::Kind::Save: save_game(req->path); break;
            case client::Tools::MenuRequest::Kind::Load: load_game(req->path); break;
            case client::Tools::MenuRequest::Kind::MainMenu:
                leave_network();
                tutorial.end();
                first_steps_pending = false;
                title.set_slots(save_slots());
                title.show();
                break;
            case client::Tools::MenuRequest::Kind::Quit: running = false; break;
            }
        }
        // The title screen's requests.
        if (const auto req = title.take_request()) {
            switch (req->kind) {
            case client::Title::Request::Kind::NewGame: {
                sim::WorldConfig cfg = req->config;
                if (!req->path.empty()) {
                    cfg.landscape = read_landscape(req->path, cfg, load_game_data(data_dir).balance.map.max_height_m);
                    MapExtras extras = read_map_extras(req->path, cfg);
                    cfg.towns = std::move(extras.towns);
                    cfg.industries = std::move(extras.industries);
                    cfg.territory_map = std::move(extras.territories);
                }
                if (!start_world(cfg)) break;
                tutorial.end();
                first_steps_pending = true;
                if (req->host) {
                    // Open to others: Play together's Host a new game.
                    save_player();
                    std::string error;
                    if (open_to_others(net::kDefaultPort, title.player_name(), error)) {
                        first_steps_pending = false; // the first steps' panel would hide the address
                        announce_host();
                    } else {
                        tools.show("Cannot open the game to others: " + error, false);
                    }
                }
                break;
            }
            case client::Title::Request::Kind::Join: {
                save_player();
                std::unique_ptr<net::Guest> joined;
                leave_network();
                try {
                    joined = std::make_unique<net::Guest>(world, req->path, title.player_name(), load_game_data(data_dir), 5000);
                } catch (const std::exception& e) {
                    title.say("Cannot join " + req->path + ": " + e.what());
                    break;
                }
                // As start_world, for the host's game now in `world`.
                tutorial.end();
                watching.reset();
                demo_pending = false;
                first_steps_pending = false;
                {
                    const client::Tools::Settings kept = tools.settings();
                    tools.~Tools();
                    new (&tools) client::Tools(world, cam);
                    tools.settings() = kept;
                    if (char* pref = SDL_GetPrefPath("Railmaster", "Railmaster")) {
                        tools.set_save_paths(pref, kQuickSave);
                        SDL_free(pref);
                    }
                }
                guest = std::move(joined);
                // Others see this player by name (a number added if it is taken).
                if (!world.execute(sim::RenamePlayer{title.player_name()}).ok) {
                    (void)world.execute(sim::RenamePlayer{title.player_name() + " " + std::to_string(guest->seat() + 1)});
                }
                look_at_biggest_town();
                ledger_year = world.date().year();
                choices_seen = world.pending_choices().size();
                (void)world.take_news();
                (void)world.take_headlines();
                if (!world.player_company()) tools.open_founding(/*cancellable=*/false);
                if (speed.speed() == sim::GameSpeed::Paused) speed.toggle_pause(); // the host's game sets the pace
                modal_paused = false;
                tools.show("Joined " + req->path + " as player " + std::to_string(guest->seat() + 1), true);
                title.hide();
                break;
            }
            case client::Title::Request::Kind::Scenario:
                first_steps_pending = false;
                try {
                    sim::WorldConfig cfg = scenario_world_config(req->path, load_game_data(data_dir).balance.map.max_height_m);
                    cfg.found_player_company = false;
                    start_world(cfg);
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "Failed to load scenario %s: %s\n", req->path.c_str(), e.what());
                }
                break;
            case client::Title::Request::Kind::Watch:
                first_steps_pending = false;
                start_replay(req->path);
                break;
            case client::Title::Request::Kind::Load:
                first_steps_pending = false;
                if (req->host) {
                    // Play together's Host a saved game: only a game hosted before has seats for others.
                    if (!load_game(req->path)) {
                        title.say("Cannot load that game");
                        break;
                    }
                    save_player();
                    std::string error;
                    if (world.config().humans <= 1) {
                        tools.show("That game was played alone: it has no seats for others. Host a new game to play together", false);
                    } else if (open_to_others(net::kDefaultPort, title.player_name(), error)) {
                        announce_host();
                    } else {
                        tools.show("Cannot open the game to others: " + error, false);
                    }
                    if (speed.speed() != sim::GameSpeed::Paused) speed.toggle_pause();
                    modal_paused = false;
                    title.hide();
                    break;
                }
                load_game(req->path);
                if (speed.speed() != sim::GameSpeed::Paused) speed.toggle_pause();
                modal_paused = false;
                title.hide();
                break;
            case client::Title::Request::Kind::Editor: start_editor(*req); break;
            case client::Title::Request::Kind::Tutorial: {
                // A small, quiet map, your railroad founded, and the steps on screen.
                sim::WorldConfig cfg;
                cfg.seed = 7;
                cfg.rivals = 0;
                cfg.found_player_company = true;
                first_steps_pending = false;
                start_world(cfg);
                tutorial.begin(world, cam);
                break;
            }
            case client::Title::Request::Kind::Quit: running = false; break;
            }
        }
        // The map editor's requests.
        if (const auto req = editor.take_request()) {
            const std::int32_t max_h = world.data().balance.map.max_height_m;
            // Save the map (image and extras); false if it cannot be written.
            const auto save_map = [&] {
                if (editor_path.empty()) {
                    // A new name in the player's maps folder: my_map_1.png, my_map_2.png, ...
                    std::error_code ec;
                    std::filesystem::create_directories(maps_dir, ec);
                    for (int n = 1; editor_path.empty() || std::filesystem::exists(editor_path); ++n) {
                        editor_path = maps_dir + "/my_map_" + std::to_string(n) + ".png";
                    }
                }
                if (save_map_image(world.terrain(), max_h, editor_path) &&
                    write_map_extras(editor_path, world.terrain().width(), world.terrain().height(),
                                     MapExtras{editor.towns(), editor.industries(),
                                               editor.territories().empty() ? std::nullopt
                                                                            : std::optional<sim::TerritoryMap>(editor.territories())})) {
                    editor.saved(editor_path);
                    return true;
                }
                std::fprintf(stderr, "Cannot write %s\n", editor_path.c_str());
                return false;
            };
            // Save the map and a scenario on it, named after the map, in the
            // player's scenarios folder; the scenario file, or empty.
            const auto save_scenario = [&]() -> std::string {
                if (!save_map()) return {};
                std::error_code ec;
                std::filesystem::create_directories(scenarios_dir, ec);
                const std::string stem = std::filesystem::path(editor_path).stem().string();
                const std::string file = scenarios_dir + "/" + stem + ".json";
                const std::string landscape = std::filesystem::relative(editor_path, scenarios_dir, ec).generic_string();
                std::ofstream out(file);
                out << editor.scenario_json(stem, landscape, "").dump(2) << "\n";
                if (!out) return {};
                editor.note("Scenario \"" + editor.scenario_name() + "\" saved: it is on the title screen's list of scenarios");
                return file;
            };
            switch (req->kind) {
            case client::Editor::Request::Kind::Save: save_map(); break;
            case client::Editor::Request::Kind::SaveScenario: save_scenario(); break;
            case client::Editor::Request::Kind::PlayScenario: {
                const std::string file = save_scenario();
                if (file.empty()) break;
                try {
                    sim::WorldConfig cfg = scenario_world_config(file, max_h);
                    cfg.found_player_company = false;
                    editor.end();
                    start_world(cfg);
                } catch (const std::exception& e) {
                    editor.note(std::string("Cannot play it: ") + e.what());
                }
                break;
            }
            case client::Editor::Request::Kind::Play: {
                // A new railroad on the map as it stands.
                sim::WorldConfig cfg;
                cfg.width_tiles = world.terrain().width();
                cfg.height_tiles = world.terrain().height();
                cfg.seed = world.config().seed;
                cfg.rivals = 3;
                cfg.found_player_company = false;
                cfg.landscape = sim::landscape_of(world.terrain());
                cfg.towns = editor.towns();
                cfg.industries = editor.industries();
                if (const sim::TerritoryMap tm = editor.territories(); !tm.empty()) cfg.territory_map = tm;
                editor.end();
                start_world(cfg);
                break;
            }
            case client::Editor::Request::Kind::Leave:
                editor.end();
                title.set_slots(save_slots());
                title.show();
                break;
            }
        }
        // The newspaper and the menu stop the clock while open, as RT3's do.
        const bool modal = !title.open() && !shared_game() && (tools.newspaper_open() || tools.menu_open());
        if (modal && !modal_paused) {
            modal_paused = speed.speed() != sim::GameSpeed::Paused;
            if (modal_paused) speed.toggle_pause();
        } else if (!modal && modal_paused) {
            modal_paused = false;
            if (speed.speed() == sim::GameSpeed::Paused) speed.toggle_pause();
        }

        // Sound: whistles as trains leave, a bell as they arrive, the cash
        // register when your trains are paid, and the engine nearest the
        // middle of the view working. Louder the closer the view.
        clock_s += dt;
        {
            const sim::Railway& rw = world.railway();
            const bool running_now = speed.speed() != sim::GameSpeed::Paused;
            const float near = std::clamp(10.0f / std::max(1.0f, cam.distance), 0.08f, 1.0f);
            const auto w = static_cast<float>(cam.width_px), h = static_cast<float>(cam.height_px);
            // Another game started or loaded: what was heard was of the old one.
            if (world.total_ticks() < heard_ticks) heard.clear();
            heard_ticks = world.total_ticks();
            if (heard.size() != rw.trains().size()) heard.resize(rw.trains().size());
            float best = 2.0f, engine_rate = 0, engine_volume = 0, engine_pan = 0;
            float motor_best = 2.0f, motor_speed = 0, motor_volume = 0, motor_pan = 0;
            int motor_kind = 0;
            for (const sim::Train& t : rw.trains()) {
                Heard& was = heard[t.id];
                const bool mine = world.player_company() && t.owner == *world.player_company();
                float sx = 0, sy = 0;
                cam.to_screen(rw.train_position(t.id), sx, sy);
                const bool seen = sx >= 0 && sx < w && sy >= 0 && sy < h;
                const float pan = std::clamp(sx / std::max(1.0f, w) * 2 - 1, -1.0f, 1.0f);
                const bool steam = world.data().locomotives.get(t.loco).fuel == sim::Fuel::Steam;
                if (t.in_service() && seen && running_now) {
                    if (was.state == sim::TrainState::Dwelling && t.state == sim::TrainState::Moving && clock_s - last_whistle > 1.5) {
                        // Steam whistles; diesels and electrics a deeper horn.
                        client::audio::play(client::audio::Sound::Whistle, 0.45f * near, pan, steam ? 1.0f : 0.72f);
                        last_whistle = clock_s;
                    }
                    if (was.state == sim::TrainState::Moving && t.state == sim::TrainState::Dwelling) {
                        client::audio::play(client::audio::Sound::Bell, 0.3f * near, pan);
                    }
                    // The diesel or electric nearest the middle of the view hums.
                    if (t.state == sim::TrainState::Moving && !steam) {
                        const float off = std::hypot(sx / w - 0.5f, sy / h - 0.5f) * 2;
                        if (off < motor_best) {
                            motor_best = off;
                            const std::int64_t unit = sim::mph_to_mm_per_tick(100, world.data().balance);
                            const float mph = unit > 0 ? static_cast<float>(t.speed_mm_per_tick * 100 / unit) : 0.0f;
                            motor_kind = world.data().locomotives.get(t.loco).fuel == sim::Fuel::Diesel ? 1 : 2;
                            motor_speed = std::clamp(mph / 100.0f, 0.0f, 1.0f);
                            motor_volume = (motor_kind == 1 ? 0.5f : 0.35f) * near * (1.0f - 0.5f * std::min(1.0f, off));
                            motor_pan = pan;
                        }
                    }
                    // The engine nearest the middle of the view.
                    if (t.state == sim::TrainState::Moving && steam) {
                        const float off = std::hypot(sx / w - 0.5f, sy / h - 0.5f) * 2;
                        if (off < best) {
                            best = off;
                            const std::int64_t unit = sim::mph_to_mm_per_tick(100, world.data().balance);
                            const float mph = unit > 0 ? static_cast<float>(t.speed_mm_per_tick * 100 / unit) : 0.0f;
                            engine_rate = std::clamp(mph * 0.15f, 1.2f, 9.0f);
                            engine_volume = 0.75f * near * (1.0f - 0.5f * std::min(1.0f, off));
                            engine_pan = pan;
                        }
                    }
                }
                if (mine && t.last_income_tick != was.paid_tick && was.paid_tick != 0 && t.last_income > sim::Money{}) {
                    client::audio::play(client::audio::Sound::Coin, seen ? 0.55f : 0.25f, seen ? pan : 0.0f);
                }
                was.state = t.state;
                was.paid_tick = t.last_income_tick == 0 ? 1 : t.last_income_tick;
            }
            client::audio::set_engine(engine_rate, engine_volume, engine_pan);
            client::audio::set_motor(motor_kind, motor_speed, motor_volume, motor_pan);
        }

        cam.terrain = &world.terrain();
        // The window's real size: the system may have made it smaller than
        // asked (to fit a laptop screen) without a resize event.
        SDL_GetWindowSize(window, &cam.width_px, &cam.height_px);
        SDL_GL_GetDrawableSize(window, &cam.drawable_w, &cam.drawable_h);
        client::set_pixel_ratio(static_cast<float>(cam.drawable_w) / static_cast<float>(std::max(1, cam.width_px)));
        cam.apply();
        client::set_animation_clock(smoke_clock);
        client::set_game_day(world.date().days_since_epoch());
        client::shading::set_clock(static_cast<float>(std::fmod(clock_s, 3600.0)));
        const bool shaded = cam.three_d && client::shading::active();
        if (shaded) {
            // Shadows first: what casts them, seen from the sun.
            client::shading::begin_shadow_pass(cam);
            client::draw_terrain(world.terrain(), cam);
            client::draw_sites(world.economy(), world.data().industries, world.railway(), world.date().year(), cam, std::nullopt);
            client::draw_railway(world, cam);
            client::shading::end_shadow_pass(cam);
            client::clear_smoke();
        }
        if (cam.three_d) {
            float hr = 0, hg = 0, hb = 0;
            client::shading::horizon_rgb(hr, hg, hb);
            glClearColor(hr, hg, hb, 1.0f); // sky
        }
        else glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (shaded) client::shading::draw_sky(cam);
        if (cam.three_d) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            // Haze: the far land fades into the sky, as in RT3; zoomed out
            // over the whole map it is too far to show.
            GLfloat sky[] = {0.55f, 0.70f, 0.85f, 1.0f};
            client::shading::horizon_rgb(sky[0], sky[1], sky[2]);
            glEnable(GL_FOG);
            glFogi(GL_FOG_MODE, GL_LINEAR);
            glFogfv(GL_FOG_COLOR, sky);
            glFogf(GL_FOG_START, cam.distance * 2.5f);
            glFogf(GL_FOG_END, cam.distance * 7.0f);
        }
        if (shaded) client::shading::begin_world(cam);
        client::draw_terrain(world.terrain(), cam);
        if (tools.overlay()) client::draw_price_overlay(world.economy(), world.data().cargo.get(*tools.overlay()), cam);
        client::draw_sites(world.economy(), world.data().industries, world.railway(), world.date().year(), cam, tools.overlay());
        client::draw_territory_borders(world.territories(), cam);
        client::draw_railway(world, cam);
        client::draw_analysis_overlay(world, tools.map_overlay(), cam);
        client::draw_smoke(cam, smoke_clock);
        client::draw_lights(cam);
        if (editor.open()) editor.draw_world_overlay();
        else tools.draw_world_overlay();
        if (shaded) client::shading::end_world();
        glDisable(GL_FOG);
        glDisable(GL_DEPTH_TEST);

        cam.apply_screen();
        if (cam.three_d) client::draw_weather(cam, rain, snowing, static_cast<float>(std::fmod(clock_s, 3600.0)));
        if (!hide_ui) client::draw_station_names(world, cam); // information, so Tab hides it
        client::draw_town_names(world.economy(), cam, world.data().balance.towns);
        client::draw_territory_names(world.territories(), cam,
                                     world.player_company() ? &world.company(*world.player_company()) : nullptr);
        std::string economy = sim::economic_state_name(world.economic_state());
        std::string pace = sim::game_speed_name(speed.speed());
        std::string status = world.date().month_year_label() + "  " + economy + " economy  " +
                             (speed.speed() == sim::GameSpeed::Paused ? pace : "Speed " + pace);
        if (guest && guest->connected()) {
            const auto host_speed = static_cast<sim::GameSpeed>(std::min<int>(guest->host_pace(), 5));
            status = world.date().month_year_label() + "  " + economy + " economy  Player " +
                     std::to_string(guest->seat() + 1) + "  " +
                     (host_speed == sim::GameSpeed::Paused ? std::string("Paused by the host")
                                                           : std::string("Speed ") + sim::game_speed_name(host_speed) + " (the host's)") +
                     (guest->behind() > 4 * sim::World::kTicksPerDay ? ", catching up" : "");
        } else if (host) {
            const auto n = host->players().size();
            status += "  Hosting at " + host_address + ", " + std::to_string(n) + (n == 1 ? " player" : " players");
        }
        // A scenario's track budget, in place of the speed keys' reminder.
        const auto tiles = world.player_company() ? world.track_tiles_left(*world.player_company()) : std::nullopt;
        status += tiles ? "  track budget " + std::to_string(*tiles) : guest ? std::string() : std::string("  +/- speed, pause");
        if (title.open()) {
            title.draw(static_cast<float>(cam.width_px), static_cast<float>(cam.height_px), build_label);
            SDL_GL_SwapWindow(window);
            continue;
        }
        // With the interface hidden, only what asks for an answer still shows.
        if (hide_ui && !editor.open() && !tools.founding_open() && !tools.menu_open() && !tools.newspaper_open() &&
            world.pending_choices().empty()) {
            SDL_GL_SwapWindow(window);
            continue;
        }
        if (!tools.founding_open()) {
            client::draw_radar(world, cam, radar_x(), radar_y(), kRadar);
            const auto tw = static_cast<float>(client::text_width("Shift+M: big map", 2));
            client::ui::chip(radar_x() - 1, radar_y() - 32, radar_x() + tw + 10, radar_y() - 12, 0.75f);
            glColor3f(1.0f, 0.9f, 0.68f);
            client::draw_text(radar_x() + 4, radar_y() - 29, "Shift+M: big map", 2);
            // The zoom buttons.
            for (const int z : {1, -1}) {
                const float x0 = radar_x() + kRadar + 6, y0 = radar_y() + (z > 0 ? 0.0f : kZoomButton + 6);
                client::ui::chip(x0, y0, x0 + kZoomButton, y0 + kZoomButton, zoom_held == z ? 0.95f : 0.75f);
                glColor3f(1.0f, 0.9f, 0.68f);
                glLineWidth(2.5f);
                glBegin(GL_LINES);
                const float cx = x0 + kZoomButton / 2, cy = y0 + kZoomButton / 2, a = kZoomButton * 0.28f;
                glVertex2f(cx - a, cy);
                glVertex2f(cx + a, cy);
                if (z > 0) {
                    glVertex2f(cx, cy - a);
                    glVertex2f(cx, cy + a);
                }
                glEnd();
                glLineWidth(1.0f);
            }
            {
                // The compass: north red, south pale; turned as the view is (north is up in the flat map).
                const auto [cx, cy] = compass_centre();
                client::ui::chip(cx - kZoomButton / 2, cy - kZoomButton / 2, cx + kZoomButton / 2, cy + kZoomButton / 2, 0.75f);
                const float yaw = cam.three_d ? cam.yaw : 0.0f;
                const float nx = -std::sin(yaw), ny = -std::cos(yaw); // north on screen (y down)
                const float r = kZoomButton * 0.36f, wd = kZoomButton * 0.12f;
                const float px = -ny * wd, py = nx * wd;
                glBegin(GL_TRIANGLES);
                glColor3f(0.9f, 0.25f, 0.2f);
                glVertex2f(cx + nx * r, cy + ny * r);
                glVertex2f(cx + px, cy + py);
                glVertex2f(cx - px, cy - py);
                glColor3f(0.92f, 0.9f, 0.84f);
                glVertex2f(cx - nx * r, cy - ny * r);
                glVertex2f(cx - px, cy - py);
                glVertex2f(cx + px, cy + py);
                glEnd();
            }
        }
        if (editor.open()) editor.draw_ui(static_cast<float>(cam.width_px), static_cast<float>(cam.height_px));
        else tools.draw_ui(status);
        if (big_map && !tools.founding_open()) {
            // Dim the world behind it.
            glColor4f(0.04f, 0.03f, 0.02f, 0.7f);
            glBegin(GL_QUADS);
            glVertex2f(0, 0);
            glVertex2f(static_cast<float>(cam.width_px), 0);
            glVertex2f(static_cast<float>(cam.width_px), static_cast<float>(cam.height_px));
            glVertex2f(0, static_cast<float>(cam.height_px));
            glEnd();
            client::draw_big_map(world, cam, big_x(), big_y(), big_size(), big_map_links ? &map_links : nullptr);
        }
        if (!editor.open() && !tools.founding_open() && !tools.menu_open() && !tools.newspaper_open()) {
            // A free game's first steps, once the company is founded (and
            // only if it has built nothing yet).
            if (first_steps_pending && world.player_company()) {
                first_steps_pending = false;
                bool built = false;
                for (const sim::TrackEdge& e : world.railway().track().edges()) built |= !e.removed && e.owner == *world.player_company();
                if (!built) tutorial.begin(world, cam, client::Tutorial::Mode::FirstGame);
            }
            tutorial.update(world, cam, tools);
            if (const std::string met = goals.update(world); !met.empty()) {
                tools.show(met, true);
                client::audio::play(client::audio::Sound::Bell, 0.6f);
            }
            if (!tutorial.open() && !title.open() && !tools.screen_open() && !big_map) goals.draw(world, static_cast<float>(cam.width_px));
            tutorial.draw_rings(cam);
            tutorial.draw(static_cast<float>(cam.width_px), static_cast<float>(cam.height_px));
        }
        // Which build this is, bottom right above the hint bar, so players
        // can tell whether they are on the latest commit.
        {
            const auto w = static_cast<float>(client::text_width(build_label, 2));
            const float x = static_cast<float>(cam.width_px) - w - 10, y = static_cast<float>(cam.height_px) - 44.0f - 22.0f;
            // Not over a detail panel reaching down this far (its buttons are there).
            if (title.open() || !tools.over_detail_panel(x, y)) {
                client::ui::chip(x - 7, y - 5, x + w + 7, y + 19);
                glColor3f(0.85f, 0.85f, 0.8f);
                client::draw_text(x, y, build_label, 2);
            }
            if (watching && !title.open()) {
                // What is being watched, and how far it has got.
                const std::string label = "Replay of " + watching->name + ": " + world.date().month_year_label() + " of " +
                                          watching->end_label + (watching->done ? " (the end)" : "") + "   Ctrl+R takes over";
                const auto lw = static_cast<float>(client::text_width(label, 2));
                const float lx = (static_cast<float>(cam.width_px) - lw) * 0.5f, ly = static_cast<float>(cam.height_px) - 44.0f - 34.0f;
                client::ui::chip(lx - 9, ly - 6, lx + lw + 9, ly + 21);
                glColor3f(1.0f, 0.85f, 0.5f);
                client::draw_text(lx, ly, label, 2);
            }
            if (tools.settings().show_fps) {
                char fps[48];
                std::snprintf(fps, sizeof fps, "%.0f fps  %.1f ms", 1000.0 / std::max(0.1, frame_ms), frame_ms);
                const auto fw = static_cast<float>(client::text_width(fps, 2));
                const float fx = static_cast<float>(cam.width_px) - fw - 10, fy = y - 26;
                client::ui::chip(fx - 7, fy - 5, fx + fw + 7, fy + 19);
                glColor3f(0.6f, 1.0f, 0.6f);
                client::draw_text(fx, fy, fps, 2);
            }
        }
        SDL_GL_SwapWindow(window);
    }

    if (guest) {
        std::printf("Matched the host's game %d times%s\n", guest->verified(),
                    guest->drift() ? (", then: " + *guest->drift()).c_str() : "");
    }
    client::audio::shutdown();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
