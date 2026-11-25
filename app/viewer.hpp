#pragma once
// The viewer's windows, independent of the platform/graphics backend.
// main_win32.cpp owns the window and Direct3D; Viewer::frame() draws the UI
// and advances the simulation within a per-frame time budget.

#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "hsim/world.hpp"

namespace hsim::app {

// Platform hooks (file dialogs), supplied by the backend.
struct Platform {
    // Return an empty path if cancelled.
    std::filesystem::path (*open_file_dialog)() = nullptr;
    std::filesystem::path (*save_file_dialog)() = nullptr;
};

class Viewer {
public:
    explicit Viewer(Platform platform);
    ~Viewer();

    // Draws one frame of UI and runs the simulation for up to `budget_ms`.
    void frame(double dt_seconds, double budget_ms);

    bool wants_quit() const { return quit_; }

    // Hooks for the headless smoke test.
    void debug_run_until_year(std::int64_t year);
    bool debug_select_some_event();
    bool debug_save_and_reload();
    Day debug_today() const;

private:
    void new_world(std::uint64_t seed, std::int64_t start_year);
    void load_world(const std::filesystem::path& path);
    void save_world(const std::filesystem::path& path);
    void advance(double dt_seconds, double budget_ms);

    void draw_menu();
    void draw_time_controls();
    void draw_new_world_popup();
    void draw_event_browser();
    void draw_event_details();
    void draw_entity_inspector();
    void draw_status();

    void select_event(EventId id, bool remember = true);
    void select_entity(EntityRef ref);
    void refresh_event_list();
    bool event_link(EventId id, const std::string& label);
    bool entity_link(EntityRef ref);

    Platform platform_;
    std::unique_ptr<SessionDir> session_;
    std::unique_ptr<World> world_;
    std::filesystem::path save_path_;
    std::string message_;

    // Time controls.
    int speed_index_ = 0;  // 0 = paused
    bool running_ = false;
    Day run_until_ = INT64_MAX;
    char run_until_text_[32] = "5000BCE";
    double day_accumulator_ = 0.0;
    double sim_days_per_second_ = 0.0;  // measured
    double measure_time_ = 0.0;
    Day measure_start_day_ = 0;

    // New world dialog.
    bool open_new_world_ = false;
    int new_seed_ = 1;
    char new_start_text_[32] = "10000BCE";

    // Event browser state.
    int filter_importance_ = 1;  // notable and up
    int filter_type_ = -1;       // -1 = all
    char filter_from_[32] = "";
    char filter_to_[32] = "";
    char filter_text_[64] = "";
    bool filter_by_entity_ = false;
    bool newest_first_ = true;
    int max_rows_ = 2000;
    std::vector<Event> event_rows_;
    bool event_list_dirty_ = true;
    double event_list_age_ = 0.0;
    std::uint64_t event_list_total_ = 0;

    // Selection.
    EventId selected_event_ = kNoEvent;
    std::deque<EventId> event_back_stack_;
    EntityRef selected_entity_;
    std::vector<EventId> entity_events_;
    int inspect_kind_ = static_cast<int>(EntityKind::Wanderer);
    bool inspect_alive_only_ = false;
    char inspect_filter_[64] = "";

    bool quit_ = false;
};

}  // namespace hsim::app
