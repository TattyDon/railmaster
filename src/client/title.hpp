#pragma once

#include "railmaster/sim/world.hpp"

#include <SDL.h>

#include <optional>
#include <string>
#include <vector>

namespace railmaster::client {

// The title screen (rt3-clone-spec §15.4's main menu): a new railroad on a
// map of the player's choosing, a scenario, a saved game, or quit. It is
// drawn over a game running behind it, and takes all input while open;
// what it asks for comes out of take_request() for main to carry out.
class Title {
public:
    struct Request {
        enum class Kind { NewGame, Scenario, Load, Watch, Editor, Tutorial, Join, Quit } kind;
        sim::WorldConfig config{}; // NewGame
        std::string path;          // Scenario, Load and Watch (a saved game to replay from its start); for NewGame and Editor, a landscape image (none: generated); Join: the host's address
        bool flat = false;         // Editor: start from flat land
        bool host = false;         // NewGame and Load: open it to other players (multiplayer)
    };

    // `data_dir` holds scenarios/; `slots` are the save files to offer, each
    // with a line saying what it holds ("Empty" when there is none).
    // `maps_dir`: the player's own maps (the editor saves there), listed with data/maps'.
    // `scenarios_dir`: the player's own scenarios (the editor saves there), listed with data/scenarios'.
    Title(std::string data_dir, std::string maps_dir, std::string scenarios_dir, std::vector<std::pair<std::string, std::string>> slots);

    bool open() const { return open_; }
    void show();
    void hide() { open_ = false; }
    void set_slots(std::vector<std::pair<std::string, std::string>> slots) { slots_ = std::move(slots); }
    // Nothing saved yet: the player is new.
    bool first_time() const;
    std::optional<Request> take_request() { return std::exchange(request_, std::nullopt); }

    void key(SDL_Keycode key);
    void text(const char* utf8); // typed text, for the Play together page's name and address
    // The player's name and the last host joined, for the Play together page.
    void set_player(std::string name, std::string address) {
        name_ = std::move(name);
        address_ = std::move(address);
    }
    const std::string& player_name() const { return name_; }
    const std::string& join_address() const { return address_; }
    // A line on the page, for what went wrong ("Cannot join ...").
    void say(std::string message) { message_ = std::move(message); }
    void click(float sx, float sy);
    void draw(float width, float height, const std::string& build) const;

private:
    struct ScenarioInfo {
        std::string path, name, briefing;
        std::int32_t year = 0;
    };
    struct Box {
        float x0, y0, x1, y1;
    };
    int items() const;
    void choose();
    void change(int step); // Left/Right on the new-railroad settings

    bool open_ = false;
    int page_ = 0; // 0 the title, 1 new railroad, 2 scenarios, 3 load, 4 map editor, 5 play together
    bool hosting_ = false; // the new railroad is to be hosted (from Play together)
    std::string name_, address_, message_;
    void scan_maps();
    void scan_scenarios();
    std::string scenarios_dir_;
    std::string maps_dir_;
    int editor_from_ = 0; // the map editor starts from: 0 flat land, 1 generated, else maps_[n - 2]
    int sel_ = 0;
    std::string data_dir_;
    std::vector<ScenarioInfo> scenarios_;
    std::vector<std::pair<std::string, std::string>> slots_; // path, what it holds
    mutable std::vector<Box> boxes_;
    std::optional<Request> request_;
    // The new railroad's settings.
    int map_size_ = 0;     // small, medium, large
    int rivals_ = 3;
    int territories_ = 0;  // none, 4, 8
    int difficulty_ = 1;   // easy, medium, hard, expert
    int landscape_ = 0;    // 0 generated, else maps_[landscape_ - 1]
    std::vector<std::pair<std::string, std::string>> maps_; // the images in data/maps: path, name
    std::uint64_t seed_ = 1;
};

} // namespace railmaster::client
