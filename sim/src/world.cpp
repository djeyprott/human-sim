#include "hsim/world.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace hsim {

namespace {
constexpr char kSaveMagic[5] = "HSIM";
}

bool cadence_due(Cadence c, Day day) {
    switch (c) {
        case Cadence::Daily: return true;
        case Cadence::Monthly: return to_date(day).day == 1;
        case Cadence::Seasonal: {
            Date d = to_date(day);
            return d.day == 1 && (d.month == 3 || d.month == 6 || d.month == 9 || d.month == 12);
        }
        case Cadence::Yearly: {
            Date d = to_date(day);
            return d.day == 1 && d.month == 1;
        }
    }
    return false;
}

World::World(WorldSettings settings) : World(settings, make_standard_systems()) {}

World::World(WorldSettings settings, std::vector<std::unique_ptr<System>> systems)
    : settings_(std::move(settings)),
      systems_(std::move(systems)),
      events_(settings_.history_dir, settings_.min_importance),
      snapshots_(settings_.history_dir, settings_.snapshot_interval_years),
      jobs_(std::make_unique<JobSystem>(settings_.threads)) {
    for (auto& s : systems_) s->register_catalog(catalog_);
    today_ = first_day_of_year(settings_.start_year);
}

World::~World() = default;

void World::init_new() {
    today_ = first_day_of_year(settings_.start_year);
    for (auto& s : systems_) s->init(*this);
}

void World::step() {
    if (!pending_interventions_.empty()) {
        applying_intervention_ = true;
        auto pending = std::move(pending_interventions_);
        pending_interventions_.clear();
        for (const auto& i : pending)
            for (auto& s : systems_)
                if (s->apply_intervention(*this, i)) break;
        applying_intervention_ = false;
    }

    for (auto& s : systems_)
        if (cadence_due(s->cadence(), today_)) s->update(*this);

    if (snapshots_.due(today_)) take_snapshot();
    ++today_;
}

void World::take_snapshot() {
    Snapshot snap;
    snap.day = today_;
    for (auto& s : systems_) s->snapshot_layers(snap.layers);
    snapshots_.add(std::move(snap));
}

EventId World::record(Event e) {
    e.day = today_;
    if (applying_intervention_) {
        bool has = std::any_of(e.causes.begin(), e.causes.end(),
                               [](const Cause& c) { return c.kind == Cause::Kind::Intervention; });
        if (!has) e.causes.push_back(Cause::intervention());
    }
    EventId id = events_.append(std::move(e));
    if (id != kNoEvent) {
        // Running hash of all history, so state_hash() covers every event
        // without re-reading old chunks.
        BinaryWriter w;
        write_event(w, *events_.get(id));
        events_hash_ = hash_bytes(w.bytes(), events_hash_);
    }
    return id;
}

std::string World::name_of(EntityRef ref) const {
    if (!ref.valid()) return "nobody";
    for (const auto& s : systems_)
        if (s->owns(ref.kind)) {
            auto n = s->name_of(ref);
            if (!n.empty()) return n;
        }
    return std::string(entity_kind_name(ref.kind)) + " #" + std::to_string(ref.index);
}

std::vector<EntityRef> World::list(EntityKind kind, bool alive_only) const {
    std::vector<EntityRef> out;
    for (const auto& s : systems_)
        if (s->owns(kind)) s->list(kind, alive_only, out);
    return out;
}

std::vector<InspectRow> World::inspect(EntityRef ref) const {
    std::vector<InspectRow> rows;
    for (const auto& s : systems_)
        if (s->owns(ref.kind)) s->inspect(ref, *this, rows);
    return rows;
}

std::string World::summarize(const Event& e) const {
    return catalog_.summarize(e, [this](EntityRef r) { return name_of(r); });
}

std::string World::summarize(EventId id) const {
    auto e = events_.get(id);
    if (!e) return "(event #" + std::to_string(id) + " not recorded)";
    return summarize(*e);
}

std::string World::describe_cause(const Cause& c) const {
    return catalog_.describe_cause(
        c, [this](EntityRef r) { return name_of(r); }, [this](EventId id) { return summarize(id); });
}

bool World::intervene(Intervention i) {
    if (!settings_.god_powers) return false;
    pending_interventions_.push_back(std::move(i));
    return true;
}

void World::write_state(BinaryWriter& w) const {
    BinaryWriter meta;
    meta.put(settings_.seed);
    meta.put(settings_.start_year);
    meta.put(today_);
    meta.put(settings_.min_importance);
    meta.put<std::int32_t>(settings_.snapshot_interval_years);
    meta.put<std::uint8_t>(settings_.god_powers ? 1 : 0);
    meta.put(events_hash_);
    meta.put<std::uint32_t>(static_cast<std::uint32_t>(systems_.size()));
    for (const auto& s : systems_) meta.put_string(s->name());
    w.put_section("META", meta);

    for (const auto& s : systems_) {
        BinaryWriter body;
        s->save(body);
        const char* tag = s->save_tag();
        if (std::strlen(tag) != 4) throw std::logic_error("save tag must be 4 characters");
        w.bytes().insert(w.bytes().end(), tag, tag + 4);
        w.put_bytes(body.bytes());
    }
}

std::uint64_t World::state_hash() const {
    BinaryWriter w;
    write_state(w);
    w.put(events_.total_events());
    w.put_vector(snapshots_.days());
    return hash_bytes(w.bytes());
}

std::filesystem::path World::history_folder_for(const std::filesystem::path& save_path) {
    auto p = save_path;
    p += ".history";
    return p;
}

void World::save(const std::filesystem::path& path) const {
    BinaryWriter w;
    w.bytes().insert(w.bytes().end(), kSaveMagic, kSaveMagic + 4);
    w.put(kSaveFormatVersion);
    write_state(w);

    auto hist = history_folder_for(path);
    BinaryWriter ev;
    events_.save(ev, hist);
    w.put_section("EVNT", ev);
    BinaryWriter sn;
    snapshots_.save(sn, hist);
    w.put_section("SNAP", sn);

    write_file(path, w.bytes());
}

void World::load(const std::filesystem::path& path) {
    BinaryReader r(read_file(path));
    char magic[4];
    for (char& c : magic) c = static_cast<char>(r.get<std::uint8_t>());
    if (std::memcmp(magic, kSaveMagic, 4) != 0) throw FormatError("not a human-sim save file");
    auto version = r.get<std::uint32_t>();
    if (version != kSaveFormatVersion)
        throw FormatError("save format version " + std::to_string(version) + " is not supported (expected " +
                          std::to_string(kSaveFormatVersion) + ")");

    auto meta = r.get_section("META");
    settings_.seed = meta.get<std::uint64_t>();
    settings_.start_year = meta.get<std::int64_t>();
    today_ = meta.get<Day>();
    settings_.min_importance = meta.get<Importance>();
    settings_.snapshot_interval_years = meta.get<std::int32_t>();
    settings_.god_powers = meta.get<std::uint8_t>() != 0;
    events_hash_ = meta.get<std::uint64_t>();
    auto n = meta.get<std::uint32_t>();
    if (n != systems_.size()) throw FormatError("save was made with a different set of systems");
    for (const auto& s : systems_)
        if (meta.get_string() != s->name()) throw FormatError("save was made with a different set of systems");

    for (auto& s : systems_) {
        char tag[5] = {};
        std::memcpy(tag, s->save_tag(), 4);
        BinaryReader body = r.get_section(reinterpret_cast<const char(&)[5]>(tag));
        s->load(body);
    }

    auto hist = history_folder_for(path);
    auto ev = r.get_section("EVNT");
    events_.load(ev, hist);
    auto sn = r.get_section("SNAP");
    snapshots_.load(sn, hist);
    events_.set_min_importance(settings_.min_importance);
}

SessionDir::SessionDir() {
    auto base = std::filesystem::temp_directory_path() / "humansim";
    std::filesystem::create_directories(base);
    // Unique name from the clock and this object's address; retry on clash.
    auto stamp = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    for (std::uint64_t n = 0;; ++n) {
        auto candidate = base / ("session-" + std::to_string(mix64(stamp ^ reinterpret_cast<std::uintptr_t>(this) ^ n)));
        if (std::filesystem::create_directory(candidate)) {
            path_ = candidate;
            return;
        }
    }
}

SessionDir::~SessionDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

const char* entity_kind_name(EntityKind kind) {
    switch (kind) {
        case EntityKind::None: return "None";
        case EntityKind::Wanderer: return "Wanderer";
        case EntityKind::Cell: return "Cell";
        case EntityKind::Cohort: return "Cohort";
        case EntityKind::Person: return "Person";
        case EntityKind::Org: return "Organization";
    }
    return "?";
}

const char* importance_name(Importance i) {
    switch (i) {
        case Importance::Trivial: return "trivial";
        case Importance::Notable: return "notable";
        case Importance::Major: return "major";
        case Importance::Epochal: return "epochal";
    }
    return "?";
}

}  // namespace hsim
