#include "viewer.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace hsim::app {

namespace {

struct Speed {
    const char* label;
    double days_per_second;  // < 0 = as fast as possible
};
constexpr Speed kSpeeds[] = {
    {"Paused", 0},          {"1 day/s", 1},        {"1 week/s", 7},         {"1 month/s", 30},
    {"1 year/s", 365.2425}, {"10 years/s", 3652.425}, {"100 years/s", 36524.25}, {"Max", -1},
};
constexpr int kSpeedCount = static_cast<int>(sizeof(kSpeeds) / sizeof(kSpeeds[0]));

constexpr const char* kImportance[] = {"Trivial", "Notable", "Major", "Epochal"};

constexpr EntityKind kKinds[] = {EntityKind::Wanderer, EntityKind::Cell, EntityKind::Cohort, EntityKind::Person,
                                 EntityKind::Org};

ImVec4 importance_color(Importance i) {
    switch (i) {
        case Importance::Trivial: return ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
        case Importance::Notable: return ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
        case Importance::Major: return ImVec4(1.00f, 0.78f, 0.35f, 1.0f);
        case Importance::Epochal: return ImVec4(1.00f, 0.45f, 0.35f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

std::string utf8(const std::filesystem::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool contains_case_insensitive(const std::string& hay, const char* needle) {
    if (!*needle) return true;
    auto it = std::search(hay.begin(), hay.end(), needle, needle + std::strlen(needle), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != hay.end();
}

}  // namespace

Viewer::Viewer(Platform platform) : platform_(platform) { new_world(1, -9999); }

Viewer::~Viewer() = default;

void Viewer::new_world(std::uint64_t seed, std::int64_t start_year) {
    world_.reset();  // release the old session's files first
    session_ = std::make_unique<SessionDir>();
    WorldSettings s;
    s.seed = seed;
    s.start_year = start_year;
    s.history_dir = session_->path();
    world_ = std::make_unique<World>(s);
    world_->init_new();
    save_path_.clear();
    selected_event_ = kNoEvent;
    event_back_stack_.clear();
    selected_entity_ = {};
    entity_events_.clear();
    event_list_dirty_ = true;
    running_ = false;
    speed_index_ = 0;
    message_ = "New world, seed " + std::to_string(seed) + ", starting " + format_year(start_year);
}

void Viewer::load_world(const std::filesystem::path& path) {
    auto session = std::make_unique<SessionDir>();
    WorldSettings s;
    s.history_dir = session->path();
    auto world = std::make_unique<World>(s);
    try {
        world->load(path);
    } catch (const std::exception& e) {
        message_ = "Could not load " + utf8(path) + ": " + e.what();
        return;
    }
    world_.reset();
    session_ = std::move(session);
    world_ = std::move(world);
    save_path_ = path;
    selected_event_ = kNoEvent;
    event_back_stack_.clear();
    selected_entity_ = {};
    entity_events_.clear();
    event_list_dirty_ = true;
    running_ = false;
    speed_index_ = 0;
    message_ = "Loaded " + utf8(path);
}

void Viewer::save_world(const std::filesystem::path& path) {
    try {
        world_->save(path);
        save_path_ = path;
        message_ = "Saved " + utf8(path);
    } catch (const std::exception& e) {
        message_ = "Could not save " + utf8(path) + ": " + e.what();
    }
}

void Viewer::advance(double dt, double budget_ms) {
    const Speed& speed = kSpeeds[speed_index_];
    if (!running_ || speed.days_per_second == 0) {
        day_accumulator_ = 0;
        return;
    }
    auto start = std::chrono::steady_clock::now();
    auto over_budget = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() > budget_ms;
    };

    std::int64_t days;
    if (speed.days_per_second < 0) {
        days = INT64_MAX;
    } else {
        day_accumulator_ += speed.days_per_second * dt;
        days = static_cast<std::int64_t>(day_accumulator_);
        day_accumulator_ -= static_cast<double>(days);
    }
    for (std::int64_t i = 0; i < days; ++i) {
        if (world_->today() >= run_until_) {
            running_ = false;
            message_ = "Reached " + format_date(world_->today());
            break;
        }
        world_->step();
        if ((i & 31) == 31 && over_budget()) {
            day_accumulator_ = 0;  // can't keep up; don't build a backlog
            break;
        }
    }
}

void Viewer::frame(double dt, double budget_ms) {
    Day before = world_->today();
    advance(dt, budget_ms);

    measure_time_ += dt;
    if (measure_time_ >= 0.5) {
        sim_days_per_second_ = static_cast<double>(world_->today() - measure_start_day_) / measure_time_;
        measure_time_ = 0;
        measure_start_day_ = world_->today();
    }
    if (world_->today() != before) {
        event_list_age_ += dt;
        if (event_list_age_ > 0.5) event_list_dirty_ = true;  // live refresh, twice a second
    }

    ImGui::DockSpaceOverViewport();
    draw_menu();
    draw_time_controls();
    draw_event_browser();
    draw_event_details();
    draw_entity_inspector();
    draw_status();
    draw_new_world_popup();
}

void Viewer::draw_menu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("World")) {
        if (ImGui::MenuItem("New world...")) open_new_world_ = true;
        if (ImGui::MenuItem("Open...") && platform_.open_file_dialog) {
            auto path = platform_.open_file_dialog();
            if (!path.empty()) load_world(path);
        }
        if (ImGui::MenuItem("Save", nullptr, false, !save_path_.empty())) save_world(save_path_);
        if (ImGui::MenuItem("Save as...") && platform_.save_file_dialog) {
            auto path = platform_.save_file_dialog();
            if (!path.empty()) save_world(path);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) quit_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void Viewer::draw_new_world_popup() {
    if (open_new_world_) {
        ImGui::OpenPopup("New world");
        open_new_world_ = false;
    }
    if (ImGui::BeginPopupModal("New world", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputInt("Seed", &new_seed_);
        ImGui::InputText("Start year", new_start_text_, sizeof new_start_text_);
        auto year = parse_year(new_start_text_);
        if (!year) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Year not understood (try 10000BCE)");
        ImGui::BeginDisabled(!year);
        if (ImGui::Button("Create")) {
            new_world(static_cast<std::uint64_t>(static_cast<std::uint32_t>(new_seed_)), *year);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Viewer::draw_time_controls() {
    ImGui::Begin("Time");
    ImGui::Text("%s", format_date(world_->today()).c_str());
    ImGui::SameLine(0, 24);
    ImGui::TextDisabled("seed %llu", static_cast<unsigned long long>(world_->seed()));

    if (ImGui::Button(running_ ? "Pause" : "Run")) {
        running_ = !running_;
        if (running_ && speed_index_ == 0) speed_index_ = 4;  // 1 year/s
        if (running_ && world_->today() >= run_until_) run_until_ = INT64_MAX;
    }
    ImGui::SameLine();
    if (ImGui::Button("Step day")) {
        world_->step();
        event_list_dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::Combo("Speed", &speed_index_, [](void*, int i) { return kSpeeds[i].label; }, nullptr, kSpeedCount);

    ImGui::SetNextItemWidth(140);
    ImGui::InputText("##until", run_until_text_, sizeof run_until_text_);
    ImGui::SameLine();
    auto year = parse_year(run_until_text_);
    ImGui::BeginDisabled(!year);
    if (ImGui::Button("Run until year")) {
        run_until_ = first_day_of_year(*year);
        if (run_until_ > world_->today()) {
            running_ = true;
            speed_index_ = kSpeedCount - 1;  // Max
        }
    }
    ImGui::EndDisabled();
    if (run_until_ != INT64_MAX) {
        ImGui::SameLine();
        ImGui::TextDisabled("stopping at %s", format_year(to_date(run_until_).year).c_str());
    }

    ImGui::Text("Simulating %.0f days/s (%.1f years/s)", sim_days_per_second_, sim_days_per_second_ / 365.2425);
    ImGui::Text("%llu events recorded", static_cast<unsigned long long>(world_->events().total_events()));
    ImGui::End();
}

void Viewer::refresh_event_list() {
    event_rows_.clear();
    event_list_dirty_ = false;
    event_list_age_ = 0;
    event_list_total_ = 0;

    Day from = INT64_MIN, to = INT64_MAX;
    if (auto y = parse_year(filter_from_)) from = first_day_of_year(*y);
    if (auto y = parse_year(filter_to_)) to = first_day_of_year(*y + 1) - 1;
    auto min = static_cast<Importance>(filter_importance_);
    std::string text = filter_text_;

    auto accept = [&](const Event& e) {
        if (e.importance < min) return true;
        if (filter_type_ >= 0 && e.type != static_cast<EventType>(filter_type_)) return true;
        if (e.day < from || e.day > to) return true;
        if (!text.empty() && !contains_case_insensitive(world_->summarize(e), text.c_str())) return true;
        ++event_list_total_;
        if (static_cast<int>(event_rows_.size()) < max_rows_) event_rows_.push_back(e);
        return static_cast<int>(event_rows_.size()) < max_rows_;
    };

    if (filter_by_entity_ && selected_entity_.valid()) {
        auto ids = world_->events().events_for(selected_entity_);
        if (newest_first_) std::reverse(ids.begin(), ids.end());
        for (EventId id : ids)
            if (auto e = world_->events().get(id); e && !accept(*e)) break;
    } else if (newest_first_) {
        world_->events().scan_reverse(from, to, accept);
    } else {
        world_->events().scan(from, to, accept);
    }
}

void Viewer::draw_event_browser() {
    ImGui::Begin("Events");
    bool changed = false;

    ImGui::SetNextItemWidth(110);
    changed |= ImGui::Combo("Min importance", &filter_importance_, kImportance, 4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    const auto* type_info = filter_type_ >= 0 ? world_->catalog().type(static_cast<EventType>(filter_type_)) : nullptr;
    if (ImGui::BeginCombo("Type", type_info ? type_info->name.c_str() : "All")) {
        if (ImGui::Selectable("All", filter_type_ < 0)) {
            filter_type_ = -1;
            changed = true;
        }
        for (const auto* t : world_->catalog().all_types())
            if (ImGui::Selectable(t->name.c_str(), filter_type_ == t->code)) {
                filter_type_ = t->code;
                changed = true;
            }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(110);
    changed |= ImGui::InputTextWithHint("##from", "from year", filter_from_, sizeof filter_from_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    changed |= ImGui::InputTextWithHint("##to", "to year", filter_to_, sizeof filter_to_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    changed |= ImGui::InputTextWithHint("##text", "search text", filter_text_, sizeof filter_text_);

    ImGui::BeginDisabled(!selected_entity_.valid());
    std::string involving = "Only " + (selected_entity_.valid() ? world_->name_of(selected_entity_) : std::string("selected entity"));
    changed |= ImGui::Checkbox(involving.c_str(), &filter_by_entity_);
    ImGui::EndDisabled();
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Newest first", &newest_first_);
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) changed = true;

    if (changed) event_list_dirty_ = true;
    if (event_list_dirty_) refresh_event_list();

    if (static_cast<int>(event_rows_.size()) >= max_rows_)
        ImGui::TextDisabled("Showing the first %d matches; narrow the filters to see others.", max_rows_);
    else
        ImGui::TextDisabled("%zu matching events", event_rows_.size());

    if (ImGui::BeginTable("events", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Importance", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(event_rows_.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const Event& e = event_rows_[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(row);
                if (ImGui::Selectable(format_date(e.day).c_str(), selected_event_ == e.id,
                                      ImGuiSelectableFlags_SpanAllColumns))
                    select_event(e.id);
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextColored(importance_color(e.importance), "%s", kImportance[static_cast<int>(e.importance)]);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(world_->summarize(e).c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

bool Viewer::event_link(EventId id, const std::string& label) {
    ImGui::PushID(static_cast<int>(id));
    bool clicked = ImGui::Selectable(label.c_str(), false);
    ImGui::PopID();
    if (clicked) select_event(id);
    return clicked;
}

bool Viewer::entity_link(EntityRef ref) {
    std::string label = world_->name_of(ref) + "##" + std::to_string(ref.key());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 1.0f, 1.0f));
    bool clicked = ImGui::Selectable(label.c_str(), selected_entity_ == ref, ImGuiSelectableFlags_None,
                                     ImVec2(ImGui::CalcTextSize(label.c_str(), nullptr, true).x, 0));
    ImGui::PopStyleColor();
    if (clicked) select_entity(ref);
    return clicked;
}

void Viewer::select_event(EventId id, bool remember) {
    if (remember && selected_event_ != kNoEvent && selected_event_ != id) {
        event_back_stack_.push_back(selected_event_);
        if (event_back_stack_.size() > 100) event_back_stack_.pop_front();
    }
    selected_event_ = id;
}

void Viewer::select_entity(EntityRef ref) {
    selected_entity_ = ref;
    entity_events_ = world_->events().events_for(ref);
    if (filter_by_entity_) event_list_dirty_ = true;
}

void Viewer::draw_event_details() {
    ImGui::Begin("Event");
    ImGui::BeginDisabled(event_back_stack_.empty());
    if (ImGui::Button("< Back")) {
        EventId prev = event_back_stack_.back();
        event_back_stack_.pop_back();
        select_event(prev, false);
    }
    ImGui::EndDisabled();

    auto e = selected_event_ != kNoEvent ? world_->events().get(selected_event_) : std::nullopt;
    if (!e) {
        ImGui::TextDisabled("Select an event to see who took part and why it happened.");
        ImGui::End();
        return;
    }
    const auto* info = world_->catalog().type(e->type);

    ImGui::TextWrapped("%s", world_->summarize(*e).c_str());
    ImGui::Separator();
    ImGui::Text("Date:        %s", format_date(e->day).c_str());
    ImGui::Text("Type:        %s", info ? info->name.c_str() : "?");
    ImGui::Text("Importance:  ");
    ImGui::SameLine(0, 0);
    ImGui::TextColored(importance_color(e->importance), "%s", kImportance[static_cast<int>(e->importance)]);
    ImGui::TextDisabled("Event #%llu", static_cast<unsigned long long>(e->id));

    if (!e->participants.empty()) {
        ImGui::SeparatorText("Who");
        for (const auto& p : e->participants) {
            ImGui::Text("%s:", world_->catalog().role_name(p.role).c_str());
            ImGui::SameLine();
            entity_link(p.entity);
        }
    }
    if (!e->fields.empty() && info) {
        ImGui::SeparatorText("Details");
        for (const auto& f : e->fields) {
            const char* name = f.key < info->fields.size() ? info->fields[f.key].c_str() : "?";
            if (const auto* r = std::get_if<EntityRef>(&f.value)) {
                ImGui::Text("%s:", name);
                ImGui::SameLine();
                entity_link(*r);
            } else {
                ImGui::Text("%s: %s", name,
                            world_->catalog().format_value(f.value, [&](EntityRef x) { return world_->name_of(x); }).c_str());
            }
        }
    }
    ImGui::SeparatorText("Because");
    if (e->causes.empty()) ImGui::TextDisabled("No recorded cause.");
    for (const auto& c : e->causes) {
        std::string text = world_->describe_cause(c);
        if (c.kind == Cause::Kind::Event) {
            auto ce = world_->events().get(c.event);
            std::string label = (ce ? format_date(ce->day) + "  " : std::string()) + text;
            ImGui::Bullet();
            ImGui::SameLine();
            event_link(c.event, label);
        } else {
            ImGui::BulletText("%s", text.c_str());
        }
    }
    ImGui::End();
}

void Viewer::draw_entity_inspector() {
    ImGui::Begin("Entities");

    std::vector<const char*> kind_names;
    std::vector<EntityKind> kinds;
    for (auto k : kKinds) {
        bool owned = false;
        for (const auto& s : world_->systems()) owned |= s->owns(k);
        if (owned) {
            kinds.push_back(k);
            kind_names.push_back(entity_kind_name(k));
        }
    }
    int kind_pos = 0;
    for (std::size_t i = 0; i < kinds.size(); ++i)
        if (static_cast<int>(kinds[i]) == inspect_kind_) kind_pos = static_cast<int>(i);
    ImGui::SetNextItemWidth(140);
    if (!kinds.empty() && ImGui::Combo("Kind", &kind_pos, kind_names.data(), static_cast<int>(kind_names.size())))
        inspect_kind_ = static_cast<int>(kinds[static_cast<std::size_t>(kind_pos)]);
    ImGui::SameLine();
    ImGui::Checkbox("Alive only", &inspect_alive_only_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##name", "name contains", inspect_filter_, sizeof inspect_filter_);

    if (ImGui::BeginTable("inspector", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("List", ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        // Left: the list.
        ImGui::TableNextColumn();
        ImGui::BeginChild("list", ImVec2(0, 0));
        std::vector<EntityRef> all = world_->list(static_cast<EntityKind>(inspect_kind_), inspect_alive_only_);
        if (inspect_filter_[0]) {
            std::vector<EntityRef> keep;
            for (auto r : all)
                if (contains_case_insensitive(world_->name_of(r), inspect_filter_)) keep.push_back(r);
            all.swap(keep);
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(all.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                EntityRef r = all[static_cast<std::size_t>(i)];
                std::string label = world_->name_of(r) + "##" + std::to_string(r.key());
                if (ImGui::Selectable(label.c_str(), selected_entity_ == r)) select_entity(r);
            }
        ImGui::EndChild();

        // Right: the selected entity.
        ImGui::TableNextColumn();
        ImGui::BeginChild("details", ImVec2(0, 0));
        if (!selected_entity_.valid()) {
            ImGui::TextDisabled("Select an entity.");
        } else {
            ImGui::Text("%s", world_->name_of(selected_entity_).c_str());
            ImGui::TextDisabled("%s #%u", entity_kind_name(selected_entity_.kind), selected_entity_.index);
            ImGui::Separator();
            for (const auto& row : world_->inspect(selected_entity_)) {
                ImGui::Text("%s:", row.label.c_str());
                ImGui::SameLine(160);
                if (row.link.valid())
                    entity_link(row.link);
                else
                    ImGui::TextUnformatted(row.value.c_str());
            }
            ImGui::SeparatorText("Life");
            ImGuiListClipper ev;
            ev.Begin(static_cast<int>(entity_events_.size()));
            while (ev.Step())
                for (int i = ev.DisplayStart; i < ev.DisplayEnd; ++i) {
                    EventId id = entity_events_[static_cast<std::size_t>(i)];
                    auto e = world_->events().get(id);
                    if (e) event_link(id, format_date(e->day) + "  " + world_->summarize(*e));
                }
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::End();
}

void Viewer::draw_status() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, ImGui::GetFrameHeight()));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
    if (ImGui::Begin("##status", nullptr, flags)) {
        ImGui::TextDisabled("%s", message_.c_str());
        if (!save_path_.empty()) {
            std::string p = utf8(save_path_);
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(p.c_str()).x - 16);
            ImGui::TextDisabled("%s", p.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Viewer::debug_run_until_year(std::int64_t year) {
    std::snprintf(run_until_text_, sizeof run_until_text_, "%s", format_year(year).c_str());
    run_until_ = first_day_of_year(year);
    running_ = true;
    speed_index_ = kSpeedCount - 1;
}

bool Viewer::debug_select_some_event() {
    refresh_event_list();
    for (const auto& e : event_rows_)
        if (!e.causes.empty() && !e.participants.empty()) {
            select_event(e.id);
            select_entity(e.participants[0].entity);
            if (e.causes[0].kind == Cause::Kind::Event) select_event(e.causes[0].event);
            filter_by_entity_ = true;
            event_list_dirty_ = true;
            return true;
        }
    return false;
}

bool Viewer::debug_save_and_reload() {
    auto path = platform_.save_file_dialog();
    save_world(path);
    auto hash = world_->state_hash();
    load_world(path);
    return save_path_ == path && world_->state_hash() == hash;
}

Day Viewer::debug_today() const { return world_->today(); }

}  // namespace hsim::app
