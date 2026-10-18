#pragma once

#include "camera.hpp"
#include "tools.hpp"

#include "railmaster/sim/world.hpp"

#include <SDL.h>

#include <string>

namespace railmaster::client {

// The tutorial (title screen): a few steps through the first railroad,
// each shown in a panel and passed when the game sees it done: moving the
// view, track to the next town, a station at each end, a train,
// running the clock and the books.
class Tutorial {
public:
    bool open() const { return open_; }
    void begin(const Camera& cam);
    void end() { open_ = false; }
    // Check the step against the game; call once a frame.
    void update(const sim::World& world, const Camera& cam, const Tools& tools);
    // Enter passes the last step (and closes); Shift+Tab skips a step.
    bool key(SDL_Keycode key, Uint16 mod);
    void draw(float width, float height) const;

private:
    bool open_ = false;
    int step_ = 0;
    float start_distance_ = 0, start_x_ = 0, start_y_ = 0, start_yaw_ = 0;
    Uint32 step_since_ = 0; // when this step began, for the "well done" flash
};

} // namespace railmaster::client
