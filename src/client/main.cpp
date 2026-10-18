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
#include "tutorial.hpp"

#include "railmaster/sim/demo.hpp"
#include "railmaster/sim/game_speed.hpp"
#include "railmaster/sim/world.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <filesystem>
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


std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
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
    std::string data_dir = RAILMASTER_DEFAULT_DATA_DIR;
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
    std::size_t choices_seen = 0; // scenario questions waiting, as of the last frame
    std::int32_t ledger_year = world.date().year(); // the ledger opens when a new year starts
    bool modal_paused = false; // the newspaper or menu paused the game, and closing it resumes
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
        }
    }
    const auto save_settings = [&] {
        if (settings_path.empty()) return;
        const client::Tools::Settings& st = tools.settings();
        std::ofstream out(settings_path);
        out << "volume " << st.volume << "\nedge_scroll " << st.edge_scroll << "\ntime_of_day " << st.time_mode
            << "\nshow_fps " << st.show_fps << "\ncheats " << st.cheats << "\nseasons " << st.seasons << "\n";
    };
    const auto save_game = [&](const std::string& file) {
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
            tools.show("Game saved to " + file, true);
        } catch (const std::exception& e) {
            tools.show(e.what(), false);
        }
    };
    const auto load_game = [&](const std::string& file) {
        try {
            world = sim::World::load(read_file(file), load_game_data(data_dir));
            demo_pending = false;
            tools.select(tools.tool()); // drop half-made lines and routes
            // What the loaded game has already been told.
            ledger_year = world.date().year();
            choices_seen = world.pending_choices().size();
            (void)world.take_news();
            (void)world.take_headlines();
            tools.show("Game loaded from " + file + ": " + world.date().month_year_label(), true);
        } catch (const std::exception& e) {
            tools.show(e.what(), false);
        }
    };
    const auto save_slots = [&] {
        std::vector<std::pair<std::string, std::string>> slots;
        for (int i = 0; i <= client::Tools::kSaveSlots; ++i) {
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
    client::Editor editor(world, cam);
    client::Tutorial tutorial;
    std::string editor_path; // where the map being edited was last saved
    if (title_at_start) {
        title.show();
        speed.toggle_pause(); // the railway behind the title runs
    }
    // A new game from the title screen: a fresh world and fresh tools.
    const auto start_world = [&](const sim::WorldConfig& cfg) {
        tutorial.end();
        try {
            world = sim::World(cfg, load_game_data(data_dir));
        } catch (const std::exception& e) {
            tools.show(e.what(), false);
            return;
        }
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
        if (!world.player_company()) tools.open_founding(/*cancellable=*/false);
        if (speed.speed() != sim::GameSpeed::Paused) speed.toggle_pause(); // every game starts paused [D]
        modal_paused = false;
        title.hide();
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
        try {
            world = sim::World(cfg, std::move(d));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Map editor: %s\n", e.what());
            return;
        }
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
    // The radar, bottom left above the tool bar.
    constexpr float kRadar = 170.0f;
    const auto radar_x = [] { return 8.0f; };
    const auto radar_y = [&] { return static_cast<float>(cam.height_px) - 44.0f - kRadar - 10.0f; };
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
#else
                const float amount = static_cast<float>(std::clamp(ev.wheel.y, -3, 3));
#endif
                if (amount != 0.0f) cam.zoom_at(static_cast<float>(mx), static_cast<float>(my), std::pow(1.15f, amount));
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
                // A click on the radar moves the view there.
                if (const auto p = client::radar_point(world, cam, radar_x(), radar_y(), kRadar, static_cast<float>(ev.button.x),
                                                       static_cast<float>(ev.button.y))) {
                    cam.center_on(*p);
                    break;
                }
                client::audio::play(client::audio::Sound::Click, 0.35f);
                tools.on_click(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y), false);
                break;
            case SDL_MOUSEBUTTONUP:
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
                // Ctrl+S saves the game, Ctrl+L loads the last save.
                if ((ev.key.keysym.mod & KMOD_CTRL) != 0 &&
                    (ev.key.keysym.sym == SDLK_s || ev.key.keysym.sym == SDLK_l)) {
                    if (ev.key.keysym.sym == SDLK_s) save_game(kQuickSave);
                    else load_game(kQuickSave);
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
        }
        // Smoke drifts only while the game runs.
        if (speed.speed() != sim::GameSpeed::Paused) smoke_clock += static_cast<float>(dt);

        // Fixed-step simulation, decoupled from frame rate.
        tick_accumulator += dt * static_cast<double>(sim::days_per_second_milli(speed.speed(), world.data().balance.time)) /
                            1000.0 * sim::World::kTicksPerDay;
        for (int guard = 0; tick_accumulator >= 1.0 && guard < 4096; ++guard) {
            world.tick();
            tick_accumulator -= 1.0;
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
        if (const auto p = tools.take_jump()) cam.center_on(*p);
        // A scenario event's question stops the clock until it is answered,
        // as RT3's event dialogs do.
        if (world.pending_choices().size() > choices_seen && speed.speed() != sim::GameSpeed::Paused) speed.toggle_pause();
        choices_seen = world.pending_choices().size();
        // The year's books are closed: show the ledger, paused, as RT3 does.
        if (world.date().year() != ledger_year) {
            ledger_year = world.date().year();
            if (world.player_company() && !tools.founding_open()) {
                tools.open_ledger();
                if (speed.speed() != sim::GameSpeed::Paused) speed.toggle_pause();
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
        if (auto heads = world.take_headlines(); !heads.empty()) {
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
                tutorial.end();
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
                start_world(cfg);
                break;
            }
            case client::Title::Request::Kind::Scenario:
                try {
                    sim::WorldConfig cfg = scenario_world_config(req->path, load_game_data(data_dir).balance.map.max_height_m);
                    cfg.found_player_company = false;
                    start_world(cfg);
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "Failed to load scenario %s: %s\n", req->path.c_str(), e.what());
                }
                break;
            case client::Title::Request::Kind::Load:
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
        const bool modal = !title.open() && (tools.newspaper_open() || tools.menu_open());
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
        }

        cam.terrain = &world.terrain();
        // The window's real size: the system may have made it smaller than
        // asked (to fit a laptop screen) without a resize event.
        SDL_GetWindowSize(window, &cam.width_px, &cam.height_px);
        SDL_GL_GetDrawableSize(window, &cam.drawable_w, &cam.drawable_h);
        client::set_pixel_ratio(static_cast<float>(cam.drawable_w) / static_cast<float>(std::max(1, cam.width_px)));
        cam.apply();
        client::set_animation_clock(smoke_clock);
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
        tutorial.draw_world_overlay(cam);
        if (shaded) client::shading::end_world();
        glDisable(GL_FOG);
        glDisable(GL_DEPTH_TEST);

        cam.apply_screen();
        client::draw_town_names(world.economy(), cam, world.data().balance.towns);
        client::draw_territory_names(world.territories(), cam,
                                     world.player_company() ? &world.company(*world.player_company()) : nullptr);
        std::string economy = sim::economic_state_name(world.economic_state());
        std::string pace = sim::game_speed_name(speed.speed());
        std::string status = world.date().month_year_label() + "  " + economy + " economy  " +
                             (speed.speed() == sim::GameSpeed::Paused ? pace : "Speed " + pace);
        // A scenario's track budget, in place of the speed keys' reminder.
        const auto tiles = world.player_company() ? world.track_tiles_left(*world.player_company()) : std::nullopt;
        status += tiles ? "  track budget " + std::to_string(*tiles) : std::string("  +/- speed, pause");
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
        if (!tools.founding_open()) client::draw_radar(world, cam, radar_x(), radar_y(), kRadar);
        if (editor.open()) editor.draw_ui(static_cast<float>(cam.width_px), static_cast<float>(cam.height_px));
        else tools.draw_ui(status);
        if (!editor.open() && !tools.founding_open() && !tools.menu_open() && !tools.newspaper_open()) {
            tutorial.update(world, cam, tools);
            tutorial.draw(static_cast<float>(cam.width_px), static_cast<float>(cam.height_px));
        }
        // Which build this is, bottom right above the hint bar, so players
        // can tell whether they are on the latest commit.
        {
            const auto w = static_cast<float>(client::text_width(build_label, 2));
            const float x = static_cast<float>(cam.width_px) - w - 10, y = static_cast<float>(cam.height_px) - 44.0f - 22.0f;
            client::ui::chip(x - 7, y - 5, x + w + 7, y + 19);
            glColor3f(0.85f, 0.85f, 0.8f);
            client::draw_text(x, y, build_label, 2);
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

    client::audio::shutdown();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
