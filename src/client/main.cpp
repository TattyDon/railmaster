// Railmaster client: opens a window, runs the simulation at a fixed rate
// and draws the terrain, track and trains. Rendering is deliberately
// throwaway (fixed-function OpenGL, top-down view); the real 3D renderer
// replaces it later.

#include "railmaster/sim/track_builder.hpp"
#include "railmaster/sim/world.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

namespace sim = railmaster::sim;
using sim::GroundType;
using sim::Terrain;
using sim::World;

constexpr std::int64_t kKm = 1'000'000;

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

// Plan and build one run of track, or report why it could not be built.
sim::NodeId build_run(World& world, sim::NodeId from, std::vector<sim::MapPoint> points,
                      std::optional<sim::NodeId> end = std::nullopt) {
    sim::TrackBuildOptions opts;
    opts.year = world.date().year();
    auto r = sim::plan_track(world.railway().track(), world.terrain(), from, std::move(points), end, opts);
    if (!r.plan) throw std::runtime_error("demo track: " + r.error);
    int bridges = 0, tunnels = 0;
    for (const auto& p : r.plan->pieces) {
        bridges += p.kind == sim::TrackKind::Bridge;
        tunnels += p.kind == sim::TrackKind::Tunnel;
    }
    std::printf("Built %zu pieces of track (%d bridge, %d tunnel) for $%lld\n", r.plan->pieces.size(), bridges,
                tunnels, static_cast<long long>(r.plan->total_cost.whole_dollars()));
    return sim::build_track(world.railway().track(), *r.plan);
}

// A demonstration network until the track-building UI exists: three towns
// joined in a loop of straight and curved track, with two trains running
// round it in opposite directions so single-track meets can be seen.
void build_demo_network(World& world) {
    sim::Railway& rw = world.railway();
    sim::TrackNetwork& net = rw.track();
    const Terrain& terrain = world.terrain();
    const sim::MapPoint towns[] = {{20 * kKm, 20 * kKm}, {105 * kKm, 35 * kKm}, {60 * kKm, 105 * kKm}};
    const char* names[] = {"Ashford", "Brookvale", "Carrow"};
    constexpr std::int64_t piece = sim::provisional::kDefaultPieceMm;

    sim::NodeId nodes[3];
    nodes[0] = net.add_node(towns[0], terrain.height_at_mm(towns[0]));
    nodes[1] = build_run(world, nodes[0], sim::straight_points(towns[0], towns[1], piece));
    // Curves that carry on in the direction the track was already heading.
    const sim::MapPoint c1 = *sim::continuing_control_point(net, nodes[1], towns[2]);
    nodes[2] = build_run(world, nodes[1], sim::curve_points(towns[1], c1, towns[2], piece));
    const sim::MapPoint c2 = *sim::continuing_control_point(net, nodes[2], towns[0]);
    build_run(world, nodes[2], sim::curve_points(towns[2], c2, towns[0], piece), nodes[0]);

    sim::StationId st[3];
    for (int i = 0; i < 3; ++i) {
        st[i] = rw.add_station(names[i], nodes[i], sim::StationSize::Medium);
        rw.add_service_building(sim::ServiceType::ServiceTower, nodes[i]);
    }
    rw.add_service_building(sim::ServiceType::MaintenanceFacility, nodes[0]);
    // Steam tenders run dry in about 150 km, so add towers along the line too.
    // Nodes are numbered in building order, about half a kilometre apart.
    for (sim::NodeId n = 120; n < net.nodes().size(); n += 120) {
        rw.add_service_building(sim::ServiceType::ServiceTower, n);
    }

    const auto& locos = world.data().locomotives.all();
    sim::LocoTypeId loco = 0;
    for (const auto& l : locos) {
        if (l.available_in(world.date().year())) {
            loco = l.id;
            break;
        }
    }
    rw.add_train(loco, std::vector<sim::CargoId>(4, 0), {st[0], st[1], st[2]}, /*priority=*/1);
    rw.add_train(loco, std::vector<sim::CargoId>(4, 0), {st[0], st[2], st[1]}, /*priority=*/0);
}

void draw_railway(const sim::Railway& rw, float mm_per_tile) {
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (const sim::TrackEdge& e : rw.track().edges()) {
        switch (e.kind) {
        case sim::TrackKind::Ground: glColor3f(0.35f, 0.25f, 0.15f); break;
        case sim::TrackKind::Bridge: glColor3f(0.85f, 0.85f, 0.85f); break;
        case sim::TrackKind::Tunnel: glColor3f(0.10f, 0.08f, 0.05f); break;
        }
        const sim::MapPoint a = rw.track().node(e.a).pos, b = rw.track().node(e.b).pos;
        glVertex2f(static_cast<float>(a.x_mm) / mm_per_tile, static_cast<float>(a.y_mm) / mm_per_tile);
        glVertex2f(static_cast<float>(b.x_mm) / mm_per_tile, static_cast<float>(b.y_mm) / mm_per_tile);
    }
    glEnd();

    auto square = [&](sim::MapPoint p, float half) {
        const float x = static_cast<float>(p.x_mm) / mm_per_tile, y = static_cast<float>(p.y_mm) / mm_per_tile;
        glVertex2f(x - half, y - half);
        glVertex2f(x + half, y - half);
        glVertex2f(x + half, y + half);
        glVertex2f(x - half, y + half);
    };

    glBegin(GL_QUADS);
    glColor3f(0.9f, 0.9f, 0.85f);
    for (const sim::Station& s : rw.stations()) square(rw.track().node(s.node).pos, 1.2f);
    for (const sim::ServiceBuilding& b : rw.service_buildings()) {
        if (b.type == sim::ServiceType::ServiceTower) glColor3f(0.3f, 0.6f, 0.95f);
        else glColor3f(0.95f, 0.75f, 0.2f);
        square(rw.track().node(b.node).pos, 0.5f);
    }
    for (const sim::Train& t : rw.trains()) {
        if (t.state == sim::TrainState::BrokenDown) glColor3f(1.0f, 0.55f, 0.0f);
        else if (t.state == sim::TrainState::Servicing) glColor3f(0.2f, 0.4f, 1.0f);
        else if (t.yielding) glColor3f(0.9f, 0.15f, 0.1f);
        else glColor3f(0.1f, 0.1f, 0.1f);
        square(rw.train_position(t.id), 0.8f);
    }
    glEnd();
}

// Game days advanced per real second at each speed setting.
constexpr int kSpeedDaysPerSecond[] = {0, 2, 8, 32};

void shade(const Terrain& t, int tx, int ty, int max_h) {
    const float h = static_cast<float>(t.corner_height(tx, ty)) / static_cast<float>(max_h);
    switch (t.ground(tx, ty)) {
    case GroundType::Water: glColor3f(0.15f, 0.30f, 0.55f); break;
    default: glColor3f(0.25f + 0.45f * h, 0.45f + 0.35f * h, 0.20f + 0.25f * h); break;
    }
}

void draw_terrain(const Terrain& t) {
    glBegin(GL_QUADS);
    for (int ty = 0; ty < t.height(); ++ty) {
        for (int tx = 0; tx < t.width(); ++tx) {
            shade(t, tx, ty, 400);
            const auto x = static_cast<float>(tx), y = static_cast<float>(ty);
            glVertex2f(x, y);
            glVertex2f(x + 1, y);
            glVertex2f(x + 1, y + 1);
            glVertex2f(x, y + 1);
        }
    }
    glEnd();
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string data_dir = argc > 1 ? argv[1] : RAILMASTER_DEFAULT_DATA_DIR;
    sim::GameData data;
    try {
        data = load_game_data(data_dir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Failed to load game data from %s: %s\n", data_dir.c_str(), e.what());
        return 1;
    }

    World world(sim::WorldConfig{}, std::move(data));
    try {
        build_demo_network(world);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    const auto mm_per_tile = static_cast<float>(world.terrain().tile_size_m()) * 1000.0f;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int win_w = 1280, win_h = 800;
    SDL_Window* window = SDL_CreateWindow("Railmaster", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w,
                                          win_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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


    float pan_x = 0.0f, pan_y = 0.0f, zoom = 6.0f;
    int speed = 1;
    double tick_accumulator = 0.0;
    Uint64 last = SDL_GetPerformanceCounter();
    std::string shown_label;

    for (bool running = true; running;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                win_w = ev.window.data1;
                win_h = ev.window.data2;
            }
            if (ev.type == SDL_MOUSEWHEEL) zoom = std::clamp(zoom * (ev.wheel.y > 0 ? 1.15f : 0.87f), 1.0f, 40.0f);
            if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_SPACE: speed = speed == 0 ? 1 : 0; break;
                case SDLK_1: speed = 1; break;
                case SDLK_2: speed = 2; break;
                case SDLK_3: speed = 3; break;
                default: break;
                }
            }
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        const double dt = static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency());
        last = now;

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const float pan_speed = static_cast<float>(dt) * 400.0f / zoom;
        if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) pan_x -= pan_speed;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) pan_x += pan_speed;
        if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) pan_y -= pan_speed;
        if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) pan_y += pan_speed;

        // Fixed-step simulation, decoupled from frame rate.
        tick_accumulator += dt * kSpeedDaysPerSecond[speed] * World::kTicksPerDay;
        for (int guard = 0; tick_accumulator >= 1.0 && guard < 4096; ++guard) {
            world.tick();
            tick_accumulator -= 1.0;
        }

        std::string label = "Railmaster - " + world.date().month_year_label() + (speed == 0 ? " (paused)" : "");
        if (label != shown_label) {
            SDL_SetWindowTitle(window, label.c_str());
            shown_label = std::move(label);
        }

        glViewport(0, 0, win_w, win_h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, win_w, win_h, 0, -1, 1);
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glScalef(zoom, zoom, 1.0f);
        glTranslatef(-pan_x, -pan_y, 0.0f);
        draw_terrain(world.terrain());
        draw_railway(world.railway(), mm_per_tile);
        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
