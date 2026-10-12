// Railmaster client: opens a window, runs the simulation at a fixed rate,
// draws the map, and offers the build tools. The map is drawn in 3D with
// fixed-function OpenGL: a lit terrain mesh, track, trains and buildings as
// simple shapes (V switches to the old top-down view).
//
// Usage: railmaster [data-dir] [--empty] [--quick] [--rivals=N] [--map=small|medium|large] [--territories=N]
//                   [--scenario=FILE] [--load=FILE] [--no-shaders] [--look=X,Y] [--version]
// --look starts the view over map tile (X, Y), close in.
// The commit it was built from shows in the bottom-right corner, the window
// title and --version.
// Ctrl+S saves the game to railmaster.sav, Ctrl+L loads it back.
// A scenario sets the map, rivals and territories itself, and starts with
// no demo network.

#include "build_info.hpp"
#include "font.hpp"
#include "model.hpp"
#include "render.hpp"
#include "shading.hpp"
#include "tools.hpp"

#include "railmaster/sim/demo.hpp"
#include "railmaster/sim/game_speed.hpp"
#include "railmaster/sim/world.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cctype>
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
    if (!scenario_path.empty()) {
        try {
            config = sim::scenario_config(sim::Scenario::from_json(read_file(scenario_path)));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Failed to load scenario %s: %s\n", scenario_path.c_str(), e.what());
            return 1;
        }
        empty = true;
    }
    config.found_player_company = quick || !load_path.empty();
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
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
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
    {
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
    }
    if (look) {
        cam.target_x = look->first;
        cam.target_y = look->second;
        cam.distance = 4.5f;
    }
    const std::string build_label = std::string("BUILD ") + client::kBuildCommit + "  " + client::kBuildDate;
    const std::string title = std::string("Railmaster (") + client::kBuildCommit + ")";
    // High-DPI: draw at the screen's full resolution on Retina displays.
    const Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window* window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          cam.width_px, cam.height_px, window_flags);
    if (!window) {
        // No multisampling to be had: try again without it.
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cam.width_px,
                                  cam.height_px, window_flags);
    }
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        // Some drivers refuse a multisampled context: fall back to a plain one.
        SDL_DestroyWindow(window);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cam.width_px,
                                  cam.height_px, window_flags);
        gl = window ? SDL_GL_CreateContext(window) : nullptr;
    }
    if (!gl) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    std::printf("renderer: %s\n", client::shading::init(use_shaders) ? "shaders and shadows" : "fixed-function");
    client::models::init(data_dir);
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
    if (!world.player_company()) tools.open_founding(/*cancellable=*/false);
    sim::SpeedControl speed; // every game starts paused [D]
    std::size_t choices_seen = 0; // scenario questions waiting, as of the last frame
    double tick_accumulator = 0.0;
    float smoke_clock = 0.0f; // seconds of unpaused play, for the smoke
    bool show_fps = false;    // F12: frames a second, bottom right
    double frame_ms = 16.7;   // smoothed time a frame takes
    int right_drag = 0; // pixels the mouse moved with the right button down
    // Camera bookmarks: Shift+0-9 stores the view, 0-9 returns to it (rt3-clone-spec §15.6).
    struct Bookmark {
        bool set = false;
        float x = 0, y = 0, distance = 0, yaw = 0, pitch = 0;
    };
    Bookmark bookmarks[10];
    // The radar, bottom left above the tool bar.
    constexpr float kRadar = 170.0f;
    const auto radar_x = [] { return 8.0f; };
    const auto radar_y = [&] { return static_cast<float>(cam.height_px) - 44.0f - kRadar - 10.0f; };
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
                tools.on_click(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y), false);
                break;
            case SDL_MOUSEBUTTONUP:
                // A right click that did not turn the view: stop the line or route.
                if (ev.button.button == SDL_BUTTON_RIGHT && right_drag <= 4) {
                    tools.on_click(static_cast<float>(ev.button.x), static_cast<float>(ev.button.y), true);
                }
                break;
            case SDL_KEYDOWN:
                // Ctrl+S saves the game, Ctrl+L loads the last save.
                if ((ev.key.keysym.mod & KMOD_CTRL) != 0 &&
                    (ev.key.keysym.sym == SDLK_s || ev.key.keysym.sym == SDLK_l)) {
                    const std::string file = "railmaster.sav";
                    try {
                        if (ev.key.keysym.sym == SDLK_s) {
                            std::ofstream out(file, std::ios::binary);
                            const std::string bytes = world.save();
                            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                            if (!out) throw std::runtime_error("cannot write " + file);
                            tools.show("GAME SAVED TO " + file, true);
                        } else {
                            world = sim::World::load(read_file(file), load_game_data(data_dir));
                            demo_pending = false;
                            tools.select(tools.tool()); // drop half-made lines and routes
                            tools.show("GAME LOADED FROM " + file + ": " + world.date().month_year_label(), true);
                        }
                    } catch (const std::exception& e) {
                        std::string why = e.what();
                        for (char& ch : why) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                        tools.show(why, false);
                    }
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
                        tools.show("VIEW " + std::to_string(ev.key.keysym.sym - SDLK_0) + " STORED", true);
                    } else if (b.set) {
                        cam.target_x = b.x;
                        cam.target_y = b.y;
                        cam.distance = b.distance;
                        cam.yaw = b.yaw;
                        cam.pitch = b.pitch;
                    }
                    break;
                }
                case SDLK_F12: show_fps = !show_fps; break;
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
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const float pan_speed = static_cast<float>(dt) * 400.0f / cam.zoom;
        const bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        const float turn = static_cast<float>(dt) * 1.5f;
        if (shift && cam.three_d) {
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
        if (SDL_GetMouseFocus() == window) {
            int mx = 0, my = 0;
            if (SDL_GetMouseState(&mx, &my) == 0) {
                constexpr int kEdge = 4;
                if (mx < kEdge) cam.pan(-pan_speed, 0);
                if (mx >= cam.width_px - kEdge) cam.pan(pan_speed, 0);
                if (my < kEdge) cam.pan(0, -pan_speed);
                if (my >= cam.height_px - kEdge) cam.pan(0, pan_speed);
            }
        }

        frame_ms += (dt * 1000.0 - frame_ms) * 0.05;
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
        // News ticker stand-in: tell the player when the economy turns.
        if (const auto turned = world.take_economy_news()) {
            std::string name = sim::economic_state_name(*turned);
            for (char& ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            const bool better = static_cast<int>(*turned) >= static_cast<int>(sim::EconomicState::Normal);
            tools.show("THE ECONOMY IS NOW IN " + name, better);
        }
        // The lists and train paging move the view.
        if (const auto p = tools.take_jump()) cam.center_on(*p);
        // A scenario event's question stops the clock until it is answered,
        // as RT3's event dialogs do.
        if (world.pending_choices().size() > choices_seen && speed.speed() != sim::GameSpeed::Paused) speed.toggle_pause();
        choices_seen = world.pending_choices().size();
        // Corporate news: splits, grumbling investors, chairmen fired or appointed.
        if (const auto news = world.take_news(); !news.empty()) {
            std::string text;
            for (const std::string& n : news) text += (text.empty() ? "" : "   ") + n;
            const bool bad = text.find("voted") != std::string::npos || text.find("grumbling") != std::string::npos;
            tools.show(text, !bad);
        }

        cam.terrain = &world.terrain();
        // The window's real size: the system may have made it smaller than
        // asked (to fit a laptop screen) without a resize event.
        SDL_GetWindowSize(window, &cam.width_px, &cam.height_px);
        SDL_GL_GetDrawableSize(window, &cam.drawable_w, &cam.drawable_h);
        cam.apply();
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
        if (cam.three_d) glClearColor(0.55f, 0.70f, 0.85f, 1.0f); // sky
        else glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (shaded) client::shading::draw_sky(cam);
        if (cam.three_d) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            // Haze: the far land fades into the sky, as in RT3; zoomed out
            // over the whole map it is too far to show.
            GLfloat sky[] = {0.55f, 0.70f, 0.85f, 1.0f};
            if (shaded) client::shading::horizon_rgb(sky[0], sky[1], sky[2]);
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
        tools.draw_world_overlay();
        if (shaded) client::shading::end_world();
        glDisable(GL_FOG);
        glDisable(GL_DEPTH_TEST);

        cam.apply_screen();
        client::draw_town_names(world.economy(), cam, world.data().balance.towns);
        client::draw_territory_names(world.territories(), cam,
                                     world.player_company() ? &world.company(*world.player_company()) : nullptr);
        std::string economy = sim::economic_state_name(world.economic_state());
        for (char& ch : economy) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        std::string pace = sim::game_speed_name(speed.speed());
        for (char& ch : pace) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        std::string status = world.date().month_year_label() + "  " + economy + " ECONOMY  " +
                             (speed.speed() == sim::GameSpeed::Paused ? pace : "SPEED " + pace);
        // A scenario's track budget, in place of the speed keys' reminder.
        const auto tiles = world.player_company() ? world.track_tiles_left(*world.player_company()) : std::nullopt;
        status += tiles ? "  TRACK BUDGET " + std::to_string(*tiles) : std::string("  +/- SPEED, PAUSE");
        if (!tools.founding_open()) client::draw_radar(world, cam, radar_x(), radar_y(), kRadar);
        tools.draw_ui(status);
        // Which build this is, bottom right above the hint bar, so players
        // can tell whether they are on the latest commit.
        {
            const auto w = static_cast<float>(client::text_width(build_label, 2));
            const float x = static_cast<float>(cam.width_px) - w - 10, y = static_cast<float>(cam.height_px) - 44.0f - 22.0f;
            glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
            glBegin(GL_QUADS);
            glVertex2f(x - 5, y - 4);
            glVertex2f(x + w + 5, y - 4);
            glVertex2f(x + w + 5, y + 18);
            glVertex2f(x - 5, y + 18);
            glEnd();
            glColor3f(0.85f, 0.85f, 0.8f);
            client::draw_text(x, y, build_label, 2);
            if (show_fps) {
                char fps[48];
                std::snprintf(fps, sizeof fps, "%.0f FPS  %.1f MS", 1000.0 / std::max(0.1, frame_ms), frame_ms);
                const auto fw = static_cast<float>(client::text_width(fps, 2));
                const float fx = static_cast<float>(cam.width_px) - fw - 10, fy = y - 26;
                glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
                glBegin(GL_QUADS);
                glVertex2f(fx - 5, fy - 4);
                glVertex2f(fx + fw + 5, fy - 4);
                glVertex2f(fx + fw + 5, fy + 18);
                glVertex2f(fx - 5, fy + 18);
                glEnd();
                glColor3f(0.6f, 1.0f, 0.6f);
                client::draw_text(fx, fy, fps, 2);
            }
        }
        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
