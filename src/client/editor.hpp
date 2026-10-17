#pragma once

#include "camera.hpp"

#include "railmaster/sim/world.hpp"

#include <SDL.h>

#include <optional>
#include <string>

namespace railmaster::client {

// The map editor (docs/ROADMAP.md M5): shape a map's land and water with
// brushes, then save it as a heightmap image (data/maps' format) or play a
// new railroad on it. It works on the world it is given, a map with no
// towns, industries or companies; the camera keys work as in the game.
class Editor {
public:
    enum class Brush { Raise, Lower, Smooth, Flatten, Water, Land };
    struct Request {
        enum class Kind { Save, Play, Leave } kind;
    };

    Editor(sim::World& world, const Camera& cam) : world_(world), cam_(cam) {}

    bool open() const { return open_; }
    // Start editing the world as it now is; `name` is what saving calls it.
    void begin(std::string name, std::int32_t max_height_m);
    void end() { open_ = false; }
    void saved(const std::string& path); // the map was saved there
    std::optional<Request> take_request() { return std::exchange(request_, std::nullopt); }
    const std::string& name() const { return name_; }

    // True if the event was the editor's.
    bool key(SDL_Keycode key);
    bool mouse_down(float sx, float sy, int button);
    void mouse_up(int button);
    void mouse_move(float sx, float sy);
    // Shape the land under a held brush; `dt` in seconds.
    void update(double dt);

    void draw_world_overlay() const; // the brush's outline, on the land
    void draw_ui(float width, float height) const;

private:
    void apply(float seconds);

    sim::World& world_;
    const Camera& cam_;
    bool open_ = false;
    std::string name_;
    std::int32_t max_height_m_ = 400;
    Brush brush_ = Brush::Raise;
    int radius_ = 6; // tiles
    bool held_ = false;
    float hover_sx_ = 0, hover_sy_ = 0;
    sim::MapPoint hover_{};
    std::int32_t flatten_to_ = 0; // Flatten: the height where the stroke began
    double pending_ = 0;          // seconds of brush not yet applied
    bool dirty_ = false;          // changed since saved
    bool leave_armed_ = false;    // Esc pressed once with changes unsaved
    std::optional<Request> request_;
    std::string message_;
    Uint32 message_until_ = 0;
    mutable std::vector<std::pair<float, float>> buttons_; // each brush button's x0, x1
};

} // namespace railmaster::client
