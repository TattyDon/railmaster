// Railmaster client: opens a window, runs the simulation at a fixed rate,
// draws the map, and offers the build tools. Rendering is deliberately
// throwaway (fixed-function OpenGL, top-down view); the real 3D renderer
// replaces it later.
//
// Usage: railmaster [data-dir] [--empty] [--quick] [--rivals=N] [--map=small|medium|large] [--territories=N]

#include "render.hpp"
#include "tools.hpp"

#include "railmaster/sim/demo.hpp"
#include "railmaster/sim/game_speed.hpp"
#include "railmaster/sim/world.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
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

int main(int argc, char* argv[]) {
    std::string data_dir = RAILMASTER_DEFAULT_DATA_DIR;
    bool empty = false;
    bool quick = false; // skip the founding dialog: found on the usual terms
    int rivals = 3;
    sim::MapSize map_size = sim::MapSize::Small;
    int territories = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--empty") empty = true;
        else if (arg == "--quick") quick = true;
        else if (arg.rfind("--rivals=", 0) == 0) rivals = std::stoi(arg.substr(9));
        else if (arg.rfind("--territories=", 0) == 0) territories = std::stoi(arg.substr(14));
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
    config.found_player_company = quick;
    sim::World world(config, std::move(data));
    // The demo network is built once the player has a company to pay for it.
    bool demo_pending = !empty;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    client::Camera cam;
    cam.mm_per_tile = static_cast<float>(world.terrain().tile_size_m()) * 1000.0f;
    SDL_Window* window = SDL_CreateWindow("Railmaster", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          cam.width_px, cam.height_px, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    client::Tools tools(world, cam);
    if (!world.player_company()) tools.open_founding(/*cancellable=*/false);
    sim::SpeedControl speed; // every game starts paused [D]
    double tick_accumulator = 0.0;
    Uint64 last = SDL_GetPerformanceCounter();

    for (bool running = true; running;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
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
                cam.zoom_at(static_cast<float>(mx), static_cast<float>(my), ev.wheel.y > 0 ? 1.15f : 0.87f);
                tools.on_mouse_move(static_cast<float>(mx), static_cast<float>(my));
                break;
            }
            case SDL_MOUSEMOTION:
                tools.on_mouse_move(static_cast<float>(ev.motion.x), static_cast<float>(ev.motion.y));
                break;
            case SDL_MOUSEBUTTONDOWN:
                tools.on_click(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y),
                               ev.button.button == SDL_BUTTON_RIGHT);
                break;
            case SDL_KEYDOWN:
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
                default: break;
                }
                break;
            default: break;
            }
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        const double dt = static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency());
        last = now;

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const float pan_speed = static_cast<float>(dt) * 400.0f / cam.zoom;
        if (keys[SDL_SCANCODE_LEFT]) cam.pan_x -= pan_speed;
        if (keys[SDL_SCANCODE_RIGHT]) cam.pan_x += pan_speed;
        if (keys[SDL_SCANCODE_UP]) cam.pan_y -= pan_speed;
        if (keys[SDL_SCANCODE_DOWN]) cam.pan_y += pan_speed;

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
                if (!rw.stations().empty()) {
                    const sim::MapPoint p = rw.track().node(rw.stations().front().node).pos;
                    cam.pan_x = static_cast<float>(p.x_mm) / cam.mm_per_tile - static_cast<float>(cam.width_px) / (2 * cam.zoom);
                    cam.pan_y = static_cast<float>(p.y_mm) / cam.mm_per_tile - static_cast<float>(cam.height_px) / (2 * cam.zoom);
                }
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s (carrying on without it)\n", e.what());
            }
        }
        // News ticker stand-in: tell the player when the economy turns.
        if (const auto turned = world.take_economy_news()) {
            std::string name = sim::economic_state_name(*turned);
            for (char& ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            const bool better = static_cast<int>(*turned) >= static_cast<int>(sim::EconomicState::Normal);
            tools.show("THE ECONOMY IS NOW IN " + name, better);
        }
        // Corporate news: splits, grumbling investors, chairmen fired or appointed.
        if (const auto news = world.take_news(); !news.empty()) {
            std::string text;
            for (const std::string& n : news) text += (text.empty() ? "" : "   ") + n;
            const bool bad = text.find("voted") != std::string::npos || text.find("grumbling") != std::string::npos;
            tools.show(text, !bad);
        }

        cam.apply();
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        client::draw_terrain(world.terrain());
        if (tools.overlay()) client::draw_price_overlay(world.economy(), world.data().cargo.get(*tools.overlay()));
        client::draw_sites(world.economy(), world.data().industries, cam, tools.overlay());
        client::draw_territory_borders(world.territories());
        client::draw_railway(world.railway(), cam, world.player_company());
        tools.draw_world_overlay();

        cam.apply_screen();
        client::draw_town_names(world.economy(), cam, world.data().balance.towns);
        client::draw_territory_names(world.territories(), cam,
                                     world.player_company() ? &world.company(*world.player_company()) : nullptr);
        std::string economy = sim::economic_state_name(world.economic_state());
        for (char& ch : economy) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        std::string pace = sim::game_speed_name(speed.speed());
        for (char& ch : pace) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const std::string status =
            world.date().month_year_label() + "  " + economy + " ECONOMY  " +
            (speed.speed() == sim::GameSpeed::Paused ? pace : "SPEED " + pace) + "  +/- SPEED, PAUSE";
        tools.draw_ui(status);
        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
