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
    // The tutorial proper, or the first steps shown in a free game: what
    // the game is for, then the same steps from the track on, with a way to
    // hide them.
    enum class Mode : std::uint8_t { Tutorial, FirstGame };
    bool open() const { return open_; }
    // Picks the two nearest towns for the first line and frames them in `cam`.
    void begin(const sim::World& world, Camera& cam, Mode mode = Mode::Tutorial);
    // Gold rings round the two towns, labelled, while the line is being
    // built; on the screen, with the rest of the interface.
    void draw_rings(const Camera& cam) const;
    void end() { open_ = false; }
    // Check the step against the game; call once a frame. When the track
    // step begins it brings both towns back into view.
    void update(const sim::World& world, Camera& cam, const Tools& tools);
    // Enter passes the last step (and closes); Shift+Tab skips a step.
    bool key(SDL_Keycode key, Uint16 mod);
    void draw(float width, float height) const;
    // A click on "Hide these tips" (first steps only); true if it was one.
    bool click(float x, float y);

private:
    // Both towns in view, comfortably inside the screen.
    void frame(Camera& cam, bool glide) const;
    bool open_ = false;
    std::int64_t span_mm_ = 0; // between the towns
    Mode mode_ = Mode::Tutorial;
    std::string company_, cash_; // for the first steps' opening
    mutable float hide_box_[4] = {0, 0, 0, 0};
    int step_ = 0;
    float start_distance_ = 0, start_x_ = 0, start_y_ = 0, start_yaw_ = 0;
    Uint32 step_since_ = 0; // when this step began, for the "well done" flash
    std::string town_a_, town_b_;   // the first line's towns
    sim::MapPoint at_a_{}, at_b_{};
};

} // namespace railmaster::client
