#pragma once

#include "railmaster/sim/world.hpp"

#include <string>

namespace railmaster::client {

// A free game's goals (no scenario): a ladder of aims from a first paying
// line to a railroad empire, shown one at a time in a small banner under
// the toolbar with how far along it is and a hint how to get there. Each is
// read from the game itself, so nothing is saved and a loaded game picks
// up where it stands.
class Goals {
public:
    struct Progress {
        int index = 0;   // the goal being worked on, from 0; == count() when all are met
        int count = 0;
        std::string title, hint, status;
    };
    static Progress progress(const sim::World& world);

    // Once a frame: a message when a goal is met. Returns its text, if one was.
    std::string update(const sim::World& world);
    // The banner, centred under the toolbar; click toggles its hint.
    void draw(const sim::World& world, float width) const;
    bool click(float x, float y);

private:
    int last_index_ = -1; // the goal last seen, to tell when one is met
    bool expanded_ = true;
    mutable float box_[4] = {0, 0, 0, 0};
};

} // namespace railmaster::client
