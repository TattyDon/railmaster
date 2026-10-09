// Railmaster client: opens a window, runs the simulation at a fixed rate
// and draws the terrain. Rendering is deliberately throwaway (fixed-function
// OpenGL, top-down view); the real 3D renderer replaces it later.

#include "railmaster/sim/world.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

using railmaster::sim::GroundType;
using railmaster::sim::Terrain;
using railmaster::sim::World;

// Game days advanced per real second at each speed setting.
constexpr int kSpeedDaysPerSecond[] = {0, 2, 8, 32};

void shade(const Terrain& t, int tx, int ty, int max_h) {
    const float h = static_cast<float>(t.corner_height(tx, ty)) / static_cast<float>(max_h);
    switch (t.ground(tx, ty)) {
    case GroundType::Water: glColor3f(0.15f, 0.30f, 0.55f); break;
    default: glColor3f(0.25f + 0.45f * h, 0.45f + 0.35f * h, 0.20f + 0.25f * h); break;
    }
}

void draw_terrain(const Terrain& t, float pan_x, float pan_y, float zoom) {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glScalef(zoom, zoom, 1.0f);
    glTranslatef(-pan_x, -pan_y, 0.0f);

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

int main(int /*argc*/, char* /*argv*/[]) {
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

    World world(railmaster::sim::WorldConfig{});

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
        draw_terrain(world.terrain(), pan_x, pan_y, zoom);
        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
