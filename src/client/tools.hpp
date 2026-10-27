#pragma once

#include "render.hpp"

#include "railmaster/sim/world.hpp"

#include <SDL.h>

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace railmaster::client {

enum class Tool { Inspect, Track, Station, ServiceTower, Maintenance, Train, Finance, Market, Industry, Status, Lists, Ledger };

// The player's build tools. Every change to the game is sent to the world
// as a command; the tools only hold in-progress choices (where a run of
// track starts, the stops picked so far for a new train).
class Tools {
public:
    Tools(sim::World& world, const Camera& camera) : world_(world), cam_(camera) {}

    Tool tool() const { return tool_; }
    // The cargo whose price map is shown, if any (O / P cycle through them).
    // The cargo whose price map is shown, if that overlay is on.
    std::optional<sim::CargoId> overlay() const {
        return map_overlay_ == MapOverlay::Cargo ? overlay_ : std::nullopt;
    }
    MapOverlay map_overlay() const { return map_overlay_; }
    // A map point the view should centre on (from the lists, or paging
    // through trains), once.
    // While following a train from its detail panel, its position every frame.
    std::optional<sim::MapPoint> take_jump();
    void select(Tool t);
    // The ledger, opened on its income statement (at the year's end).
    void open_ledger();

    void on_mouse_move(float sx, float sy);
    void on_click(float sx, float sy, bool right_button);
    // The mouse wheel over a list scrolls it; true if it did (else it zooms).
    bool on_wheel(float sx, float sy, int steps);
    // True if the key was used. `mod` is the event's modifier state.
    bool on_key(SDL_Keycode key, Uint16 mod = KMOD_NONE);
    // Typed text (SDL_TEXTINPUT); true if a box took it.
    bool on_text(const char* text);
    // A box is taking typed keys (the arrows should not pan).
    bool typing() const { return find_open_ || guide_open_ || renaming_; }
    bool guide_open() const { return guide_open_; }
    // The Guide's best `n` lines not yet served, for the big map.
    std::vector<MapLink> guide_links(std::size_t n) const;
    // Go to the `n`th of those (from 0), with its line planned as the
    // Guide's Enter does: B prices it, B again builds it.
    bool follow_guide_link(std::size_t n);
    // A screen that fills the middle of the window is open (the Guide, help,
    // Find, the Company, Market, Status, Lists or Ledger), so banners give way.
    bool screen_open() const {
        return guide_open_ || help_open_ || find_open_ || tool_ == Tool::Finance || tool_ == Tool::Market || tool_ == Tool::Status ||
               tool_ == Tool::Lists || tool_ == Tool::Ledger;
    }

    void draw_world_overlay() const;
    // `status` is shown at the left of the top bar (date, speed and so on).
    void draw_ui(const std::string& status) const;
    // Warn once when one of your steam trains runs out of water (it crawls
    // until it passes a service tower). Call each frame.
    void check_trains();
    // Money rising from your trains as they are paid (+$12,345), fading out.
    struct Floater {
        sim::MapPoint at;
        std::string text;
        Uint32 born;
    };
    std::vector<Floater> floaters_;
    std::vector<sim::Money> last_revenue_;
    // Flash a message in the top bar for a few seconds.
    void show(std::string message, bool good);
    // The founding dialog: how much to invest and how much outside money to
    // take. While open it takes all input; Enter founds the company.
    // `cancellable`: Esc closes it (when founding a further company).
    void open_founding(bool cancellable);
    bool founding_open() const { return founding_; }

    // News (rt3-clone-spec §15.4): every item goes into the message history
    // (M, newest first); a headline opens the newspaper, which takes all
    // input until Esc, Enter, Space or a click closes it (Shift+N reopens).
    void log(const std::string& text, bool good);
    void open_newspaper(const std::string& headline, std::vector<std::string> more);
    bool newspaper_open() const { return paper_open_; }

    // The game menu (rt3-clone-spec §15.4 File Options), on Esc when nothing
    // else is open: resume, save, load, the briefing, quit. It takes all
    // input while open; what it asks of the game comes out of
    // take_menu_request() for main to carry out.
    struct MenuRequest {
        enum class Kind { Save, Load, MainMenu, Quit } kind;
        std::string path;
    };
    std::optional<MenuRequest> take_menu_request() { return std::exchange(menu_request_, std::nullopt); }
    bool menu_open() const { return menu_open_; }
    // Whether (sx, sy) is on the interface rather than the map: the bars at
    // the top and bottom, an open panel or screen, a dialog.
    bool over_ui(float sx, float sy) const;
    // Open the Find box (Ctrl+F), or a station's panel (the Look tool), as a click on it does.
    void open_find();
    void show_station(sim::StationId id);
    void open_menu();
    // Where the menu's save slots live (a folder of the player's own); the
    // quick save (Ctrl+S) stays at `quick`.
    void set_save_paths(std::string dir, std::string quick) {
        save_dir_ = std::move(dir);
        quick_save_ = std::move(quick);
    }
    // The player's settings, changed on the menu's Settings page (and by
    // F12 and N); main applies them and keeps them between games.
    struct Settings {
        int volume = 7; // 0 (off) to 10
        int music = 4;  // the music's volume, 0 (off) to 10
        bool edge_scroll = true;
        int time_mode = 0; // day, dusk, night, dawn, the whole day round
        bool show_fps = false;
        bool cheats = false; // the debug console (.) is allowed
        bool seasons = true; // snow in winter, leaves turning in autumn
        bool clouds = true;  // clouds' shadows drifting over the land
        bool reverse_zoom = false; // a scroll the other way zooms in (as some trackpads feel natural)
    };
    Settings& settings() { return settings_; }
    // Whether the menu changed them since the last call (to save them).
    bool take_settings_changed() { return std::exchange(settings_changed_, false); }
    // The save slots: the quick save first, then the named games.
    static constexpr int kSaveSlots = 6;
    // After the named games, the autosave made at each new year (loading only).
    static constexpr int kAutosaveSlot = kSaveSlots + 1;
    std::string slot_path(int slot) const;

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
    // The station under the mouse: within a few pixels on screen, else by the map.
    // How many different stations the route being planned calls at.
    std::size_t distinct_stops() const;
    std::optional<sim::StationId> station_at_cursor() const;
    std::vector<sim::LocoTypeId> available_locos() const;
    std::string hint() const;
    std::string inspect_text() const;
    std::vector<Button> layout_buttons() const;
    void cycle_overlay(int direction);
    std::string cell_text() const;
    std::string speed_chart_text() const;
    // The Company Detail screen (F7; rt3-clone-spec §15.4): Overview, Bonds,
    // Stock and Territories tabs. The finance keys work on every tab.
    void draw_finance_panel() const;
    int company_tab_ = 0;
    std::size_t territory_sel_ = 0;
    // The Ledger (L; §12.7): overview, ten years' income statements, every
    // company's balance sheet, haulage by cargo, the players, and lists of
    // your trains, stations and industries and of the cargo.
    void draw_ledger_panel() const;
    int ledger_tab_ = 0;
    std::size_t ledger_sel_ = 0; // the chosen row in the trains, stations, industries and cargo lists
    // The ids those lists show, in order (trains and stations best first).
    std::vector<std::uint32_t> ledger_items() const;
    bool ledger_key(SDL_Keycode key);
    // Tabs along the top of a screen; their places are kept for clicks.
    struct TabBox {
        float x0, y0, x1, y1;
    };
    mutable std::vector<TabBox> tab_boxes_;
    float draw_tabs(float x, float y, const std::vector<std::string>& labels, int chosen) const;
    void draw_market_panel() const;
    void draw_status_panel() const;
    // Lists of trains, stations and companies (rt3-clone-spec §15.4).
    void draw_lists_panel() const;
    bool lists_key(SDL_Keycode key);
    std::size_t list_size() const;
    int list_kind_ = 0;            // 0 trains, 1 stations, 2 companies, 3 towns
    // The towns, biggest first (the Lists' Towns page).
    std::vector<std::size_t> towns_by_size() const;

    // The detail panel (rt3-clone-spec §15.4): a train, station or industry
    // opened by a click in the Look tool or Enter in the lists, shown at the
    // right of the screen until Esc. The train keys act on its train.
    enum class DetailKind { Train, Station, Industry };
    struct Detail {
        DetailKind kind;
        std::uint32_t id;
    };
    std::optional<Detail> detail_{};
    bool follow_ = false; // the view follows the detail panel's train
    mutable std::array<float, 4> detail_box_{}; // where the panel was last drawn: x0, y0, x1, y1
    // Rows scrolled past (under the title) when the panel is taller than the
    // window; the wheel over it moves this.
    mutable std::size_t detail_skip_ = 0;
    void open_detail(DetailKind kind, std::uint32_t id);
    // A train, else a station, else an industry under the cursor; false if none.
    bool open_detail_under_cursor();
    std::optional<Detail> detail_under_cursor() const;
    // A card by the cursor, once it rests over a train, station or industry
    // in the Look tool: the few facts that matter most.
    Uint32 hover_moved_at_ = 0;
    std::vector<std::string> hover_card_lines(const Detail& d) const;
    void draw_hover_card() const;
    std::optional<sim::TrainId> detail_train() const;
    void draw_detail_panel() const;

    std::size_t list_sel_ = 0;
    std::optional<sim::MapPoint> jump_{};
    std::size_t page_train_ = 0;   // PgUp/PgDn in Inspect: the train last jumped to

    sim::World& world_;
    const Camera& cam_;
    Tool tool_ = Tool::Inspect;

    sim::MapPoint hover_{};
    sim::TrackEnd hover_pick_{};
    float hover_sx_ = 0, hover_sy_ = 0;

    std::optional<sim::TrackEnd> track_start_;
    bool double_track_ = false;
    bool curves_ = true;
    // RT3's frequency settings for new track (rt3-clone-spec §11.2).
    sim::Frequency tunnels_ = sim::Frequency::Average;
    sim::Frequency bridges_ = sim::Frequency::Average;
    sim::Frequency overpasses_ = sim::Frequency::Average;

    sim::StationSize station_size_ = sim::StationSize::Medium;
    // What the station tool places: a station, or a building near one.
    std::optional<sim::StationBuildingType> station_building_{};

    // The route being put together in the Train tool, stop by stop, with
    // what the train takes on at each (rt3-clone-spec §9.3).
    struct PendingStop {
        sim::StationId station = 0;
        bool service = false;
        sim::ConsistRule rule{};
    };
    std::vector<PendingStop> stops_;
    std::size_t stop_sel_ = 0;                 // the stop the consist keys change
    std::size_t cargo_choice_ = 0;             // the cargo a custom consist's next car carries
    std::optional<sim::TrainId> editing_{};    // the train whose route the Train tool is changing
    std::vector<sim::StationId> pending_route() const;
    // Whether a service tower stands by any of these stations (any owner's:
    // trains passing it fill up).
    bool tower_near(const std::vector<sim::StationId>& route) const;
    // What a station here would reach, for the Station tool: made and wanted.
    std::string station_reach_text() const;
    // What is wrong with a train, if anything, and how bad: 2 stuck or
    // crawling (red), 1 slowed (amber), 0 fine. `advice` says what to do.
    int train_trouble(const sim::Train& t, std::string* advice = nullptr) const;
    std::vector<bool> pending_service() const;
    // Give the train each pending stop's consist; false (with a message) on refusal.
    bool apply_consists(sim::TrainId id);
    std::string stop_text() const;
    // The Train tool's panel: engines to pick, the stops so far, buttons for
    // what the keys do, and Buy (or Save route); a click on a row picks it.
    void draw_train_panel() const;
    void train_panel_click(std::size_t index);
    // The Track and Station tools' options as a panel in the same place:
    // what their keys set, to click.
    void draw_tool_options() const;
    void tool_options_click(std::size_t index);
    void draw_industry_options() const;
    // A row of buttons across a panel, from x0 to x1 at y, each pressing a
    // key (the screens' actions, for the mouse).
    struct KeyButton {
        std::string label;
        SDL_Keycode key;
    };
    void key_buttons(float x0, float x1, float y, const std::vector<KeyButton>& buttons) const;
    mutable std::array<float, 4> side_panel_box_{};
    std::uint8_t cars_ = 4;
    std::size_t loco_choice_ = 0;

    std::optional<sim::CargoId> overlay_;
    MapOverlay map_overlay_ = MapOverlay::None;

    sim::CompanyId market_choice_ = 0; // the company selected on the market screen
    std::size_t build_choice_ = 0;      // the plant to build, among those buildable now
    std::vector<sim::IndustryTypeId> buildable_types() const;
    // The industry in the cell under the cursor, if any.
    std::optional<sim::SiteId> site_under_cursor() const;
    std::string industry_text() const;
    Uint32 resign_armed_until_ = 0;     // a first Q press arms resigning for a few seconds
    Uint32 retire_armed_until_ = 0;     // and a first X press retiring this train
    sim::TrainId retire_armed_ = 0;
    Uint32 remove_armed_until_ = 0;     // and a first X in the Track tool removing this run
    std::vector<sim::EdgeId> remove_armed_;
    // A first X over a station (Station tool) or a tower or shed (F4, F5):
    // the second X within the time takes it down.
    Uint32 remove_site_until_ = 0;
    std::optional<std::uint32_t> remove_site_;
    std::optional<sim::ServiceBuildingId> building_near(sim::MapPoint p) const;
    bool remove_site_key();
    bool undo_build(); // Ctrl+Z in the Station, Tower and Shed tools
    Uint32 bankrupt_armed_until_ = 0;   // likewise K for bankruptcy
    bool founding_ = false;
    bool founding_cancellable_ = false;
    std::int64_t found_personal_ = 0;   // dollars, as chosen in the dialog
    std::int64_t found_outside_ = 0;
    bool founding_key(SDL_Keycode key, Uint16 mod);
    void draw_founding_dialog() const;
    void draw_choice_dialog() const; // a scenario event's question, answered with 1-9
    struct LogEntry {
        std::string when;
        std::string text;
        bool good;
    };
    std::vector<LogEntry> log_;
    bool log_open_ = false;
    // Find (Ctrl+F, rt3-clone-spec §15.7 Find city): type part of a name,
    // pick a town or station, and the view goes there.
    struct FindHit {
        std::string name, kind;
        sim::MapPoint at{};
        std::optional<sim::StationId> station{};
    };
    bool console_open_ = false; // the debug console (rt3-clone-spec §15.8)
    bool console_key(SDL_Keycode key);
    void draw_console() const;
    // The Guide (Ctrl+G): what each cargo is made by and wanted by, and the
    // best-paying links between industries not yet served.
    struct Opportunity {
        sim::CargoId cargo = 0;
        sim::SiteId from = 0, to = 0;
        std::int64_t per_load = 0;    // dollars: the price there less the price here
        std::int64_t loads_milli = 0; // a year, at the source's rate
        std::int64_t tenths_of_miles = 0;
        bool served = false; // both ends already within reach of your stations
        // Passengers and mail between two towns (cargo is then passengers):
        // from and to name the towns, not sites.
        bool towns = false;
        // A rival's stations already reach both ends (they may run it).
        std::optional<sim::CompanyId> rival{};
        // Freight into a works: what it would make of it, and the best place
        // to carry that on to (a chain: both lines together).
        std::optional<sim::CargoId> then_cargo{};
        sim::SiteId then_to = 0;
        std::int64_t then_per_load = 0;   // dollars
        std::int64_t then_loads_milli = 0; // a year: as much as this line feeds it, up to its capacity
        std::int64_t then_tenths_of_miles = 0;
    };
    void go_to_opportunity(const Opportunity& o);
    std::string opp_from_name(const Opportunity& o) const;
    std::string opp_to_name(const Opportunity& o) const;
    // Each end's map cell, and where its station should go.
    std::pair<std::int32_t, std::int32_t> opp_cell(const Opportunity& o, bool from) const;
    sim::MapPoint opp_station_point(const Opportunity& o, bool from) const;
    // The link chosen in the Guide: marked on the map until built or let go;
    // B builds it (track, a station at each end, a train), the second B
    // confirming the price the first one showed.
    std::optional<Opportunity> link_;
    std::vector<bool> dry_warned_; // trains already warned of running out of water
    Uint32 link_armed_until_ = 0;
    struct LinkPlan {
        sim::BuildTrack track;
        sim::Money track_cost{}, stations_cost{}, train_cost{}, tower_cost{};
        bool tower = false; // a service tower is wanted at the first station
        std::optional<sim::LocoTypeId> loco;
        std::string error;
        sim::Money total() const { return track_cost + stations_cost + train_cost + tower_cost; }
    };
    LinkPlan plan_link(const Opportunity& o) const;
    void build_link();
    void draw_link() const;
    bool guide_open_ = false;
    int guide_tab_ = 0; // 0 opportunities, 1 supply chains
    std::size_t guide_sel_ = 0;
    std::vector<Opportunity> guide_opps_;
    void open_guide();
    std::vector<Opportunity> find_opportunities() const;
    bool guide_key(SDL_Keycode key);
    void draw_guide() const;
    std::string site_name(sim::SiteId id) const;
    std::array<float, 4> guide_button() const;
    std::array<float, 4> find_button() const; // after the Guide's: Find a town or station

    bool help_open_ = false; // the help card (?)
    int help_page_ = 0;      // 0 the keys, 1 what makes a line pay (Tab turns)
    void draw_help() const;
    // The help button, after the tool buttons.
    std::array<float, 4> help_button() const;
    bool find_open_ = false;
    std::string find_text_;
    // Renaming the station whose panel is open (Enter in Look): the name as typed.
    bool renaming_ = false;
    std::string rename_text_;
    bool rename_key(SDL_Keycode key);
    std::size_t find_sel_ = 0;
    std::vector<FindHit> find_hits() const;
    bool find_key(SDL_Keycode key);
    void draw_find_panel() const;
    std::size_t log_scroll_ = 0; // entries skipped from the newest
    void draw_log_panel() const;
    struct Paper {
        std::string when;
        std::string headline;
        std::vector<std::string> more;
    };
    std::optional<Paper> paper_;
    bool paper_open_ = false;
    void draw_newspaper() const;
    bool menu_open_ = false;
    int menu_page_ = 0; // 0 the menu, 1 save, 2 load, 3 quit?, 4 main menu?
    int menu_sel_ = 0;
    mutable std::vector<TabBox> menu_boxes_; // each item's place, for clicks
    // Rows a click can choose, put down as the panels draw each frame: one
    // click selects a row, a click on the chosen row acts as Enter would.
    enum class RowPanel { Find, Console, Lists, Ledger, Territories, Market, Guide, TrainTool, ToolOptions, KeyButton };
    struct RowBox {
        float x0, y0, x1, y1;
        RowPanel panel;
        std::size_t index;
    };
    mutable std::vector<RowBox> row_boxes_;
    void add_row(float x0, float y0, float x1, float y1, RowPanel panel, std::size_t index) const {
        row_boxes_.push_back({x0, y0, x1, y1, panel, index});
    }
    bool click_row(float sx, float sy);
    std::vector<std::string> slot_info_;      // what each slot holds, read when its page opens
    std::optional<MenuRequest> menu_request_;
    std::string save_dir_, quick_save_ = "railmaster.sav";
    Settings settings_{};
    bool settings_changed_ = false;
    void change_setting(int step);
    int menu_items() const;
    void menu_choose();
    bool menu_key(SDL_Keycode key);
    void draw_menu() const;
    std::string message_;
    bool message_good_ = true;
    Uint32 message_until_ = 0;
};

std::string format_money(sim::Money m);
// What a save file holds, from the line written beside it: "Jun 1835,
// Railmaster Railroad, net worth $5,120,000"; "Empty" if there is no save.
std::string save_summary(const std::string& path);
// Dollars and cents, for share prices and dividends.
std::string format_cents(sim::Money m);
std::string format_count(std::int64_t n); // 600,000
std::string format_permille(std::int32_t p); // +12.5%

} // namespace railmaster::client
