#pragma once

#include "render.hpp"

#include "railmaster/sim/world.hpp"

#include <SDL.h>

#include <optional>
#include <string>
#include <vector>

namespace railmaster::client {

enum class Tool { Inspect, Track, Station, ServiceTower, Maintenance, Train, Finance, Market };

// The player's build tools. Every change to the game is sent to the world
// as a command; the tools only hold in-progress choices (where a run of
// track starts, the stops picked so far for a new train).
class Tools {
public:
    Tools(sim::World& world, const Camera& camera) : world_(world), cam_(camera) {}

    Tool tool() const { return tool_; }
    // The cargo whose price map is shown, if any (O / P cycle through them).
    std::optional<sim::CargoId> overlay() const { return overlay_; }
    void select(Tool t);

    void on_mouse_move(float sx, float sy);
    void on_click(float sx, float sy, bool right_button);
    // True if the key was used. `mod` is the event's modifier state.
    bool on_key(SDL_Keycode key, Uint16 mod = KMOD_NONE);

    void draw_world_overlay() const;
    // `status` is shown at the left of the top bar (date, speed and so on).
    void draw_ui(const std::string& status) const;
    // Flash a message in the top bar for a few seconds.
    void show(std::string message, bool good);

private:
    struct Button {
        Tool tool;
        const char* label;
        float x0, y0, x1, y1;
    };

    void cancel();
    std::int64_t snap_mm() const { return static_cast<std::int64_t>(cam_.pixels_to_mm(10.0f)); }
    std::optional<sim::BuildTrack> pending_track() const;
    std::optional<sim::StationId> station_near(sim::MapPoint p) const;
    std::vector<sim::LocoTypeId> available_locos() const;
    std::string hint() const;
    std::string inspect_text() const;
    std::vector<Button> layout_buttons() const;
    void cycle_overlay(int direction);
    std::string cell_text() const;
    void draw_finance_panel() const;
    void draw_market_panel() const;

    sim::World& world_;
    const Camera& cam_;
    Tool tool_ = Tool::Inspect;

    sim::MapPoint hover_{};
    sim::TrackEnd hover_pick_{};
    float hover_sx_ = 0, hover_sy_ = 0;

    std::optional<sim::TrackEnd> track_start_;
    bool double_track_ = false;
    bool curves_ = true;
    std::int32_t tunnel_preference_ = 50;

    sim::StationSize station_size_ = sim::StationSize::Medium;

    std::vector<sim::StationId> route_;
    std::uint8_t cars_ = 4;
    std::size_t loco_choice_ = 0;

    std::optional<sim::CargoId> overlay_;

    sim::CompanyId market_choice_ = 0; // the company selected on the market screen
    std::string message_;
    bool message_good_ = true;
    Uint32 message_until_ = 0;
};

std::string format_money(sim::Money m);
// Dollars and cents, for share prices and dividends.
std::string format_cents(sim::Money m);
std::string format_count(std::int64_t n); // 600,000

} // namespace railmaster::client
