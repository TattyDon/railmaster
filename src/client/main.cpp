// Railmaster client: opens a window, runs the simulation at a fixed rate,
// draws the map, and offers the build tools. Rendering is deliberately
// throwaway (fixed-function OpenGL, top-down view); the real 3D renderer
// replaces it later.
//
// Usage: railmaster [data-dir] [--empty]

#include "render.hpp"
#include "tools.hpp"

#include "railmaster/sim/world.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

namespace sim = railmaster::sim;
namespace client = railmaster::client;

constexpr std::int64_t kKm = 1'000'000;

// Game days advanced per real second at each speed setting.
constexpr int kSpeedDaysPerSecond[] = {0, 2, 8, 32};

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

sim::GameData load_game_data(const std::string& dir) {
    return {sim::CargoRegistry::from_json(read_file(dir + "/cargo.json")),
            sim::LocomotiveRegistry::from_json(read_file(dir + "/locomotives.json"))};
}

sim::CommandResult must(sim::World& world, const sim::Command& cmd) {
    sim::CommandResult r = world.execute(cmd);
    if (!r.ok) throw std::runtime_error("demo network: " + r.error);
    return r;
}

sim::TrackEnd at_node(const sim::World& world, sim::NodeId n) {
    return {sim::TrackEnd::Kind::Node, n, 0, world.railway().track().node(n).pos};
}

sim::TrackEnd on_ground(sim::MapPoint p) { return {sim::TrackEnd::Kind::Free, 0, 0, p}; }

// A starting network, built with the same commands the tools use: three
// towns in a loop of straight and curved track, with two trains running
// round it in opposite directions.
void build_demo_network(sim::World& world) {
    const sim::MapPoint towns[] = {{20 * kKm, 20 * kKm}, {105 * kKm, 35 * kKm}, {60 * kKm, 105 * kKm}};
    const char* names[] = {"Ashford", "Brookvale", "Carrow"};
    const sim::TrackNetwork& net = world.railway().track();

    sim::NodeId nodes[3];
    nodes[1] = must(world, sim::BuildTrack{.start = on_ground(towns[0]), .end = on_ground(towns[1])}).created_id;
    nodes[0] = *net.nearest_node(towns[0], 1);
    // Curves that carry on in the direction the track was already heading.
    nodes[2] = must(world, sim::BuildTrack{.start = at_node(world, nodes[1]),
                                           .end = on_ground(towns[2]),
                                           .curve_control = sim::continuing_control_point(net, nodes[1], towns[2])})
                   .created_id;
    must(world, sim::BuildTrack{.start = at_node(world, nodes[2]),
                                .end = at_node(world, nodes[0]),
                                .curve_control = sim::continuing_control_point(net, nodes[2], towns[0])});

    sim::StationId st[3];
    for (int i = 0; i < 3; ++i) {
        st[i] = must(world, sim::BuildStation{.at = at_node(world, nodes[i]), .name = names[i]}).created_id;
        must(world, sim::BuildServiceBuilding{.at = at_node(world, nodes[i])});
    }
    must(world, sim::BuildServiceBuilding{.at = at_node(world, nodes[0]), .type = sim::ServiceType::MaintenanceFacility});
    // Steam tenders run dry in about 150 km, so add towers along the line too.
    for (sim::NodeId n = 120; n < net.nodes().size(); n += 120) must(world, sim::BuildServiceBuilding{.at = at_node(world, n)});

    sim::LocoTypeId loco = 0;
    for (const auto& l : world.data().locomotives.all()) {
        if (l.available_in(world.date().year())) {
            loco = l.id;
            break;
        }
    }
    must(world, sim::BuyTrain{.loco = loco, .cars = 4, .route = {st[0], st[1], st[2]}, .priority = 1});
    must(world, sim::BuyTrain{.loco = loco, .cars = 4, .route = {st[0], st[2], st[1]}});
    std::printf("Demo network built for %s\n", client::format_money(world.total_spent()).c_str());
}

} // namespace

int main(int argc, char* argv[]) {
    std::string data_dir = RAILMASTER_DEFAULT_DATA_DIR;
    bool empty = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--empty") empty = true;
        else data_dir = arg;
    }

    sim::GameData data;
    try {
        data = load_game_data(data_dir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Failed to load game data from %s: %s\n", data_dir.c_str(), e.what());
        return 1;
    }

    sim::World world(sim::WorldConfig{}, std::move(data));
    if (!empty) {
        try {
            build_demo_network(world);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s\n", e.what());
            return 1;
        }
    }

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
    int speed = 1;
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
                if (tools.on_key(ev.key.keysym.sym)) break;
                switch (ev.key.keysym.sym) {
                case SDLK_SPACE: speed = speed == 0 ? 1 : 0; break;
                case SDLK_1: speed = 1; break;
                case SDLK_2: speed = 2; break;
                case SDLK_3: speed = 3; break;
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
        tick_accumulator += dt * kSpeedDaysPerSecond[speed] * sim::World::kTicksPerDay;
        for (int guard = 0; tick_accumulator >= 1.0 && guard < 4096; ++guard) {
            world.tick();
            tick_accumulator -= 1.0;
        }

        cam.apply();
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        client::draw_terrain(world.terrain());
        client::draw_railway(world.railway(), cam);
        tools.draw_world_overlay();

        cam.apply_screen();
        const std::string status = world.date().month_year_label() +
                                   (speed == 0 ? "  PAUSED" : "  SPEED " + std::to_string(speed)) +
                                   "  SPACE PAUSE, 1-3 SPEED";
        tools.draw_ui(status);
        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
