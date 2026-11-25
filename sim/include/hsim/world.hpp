#pragma once
// The world: time, the systems that simulate it, and its recorded history.
//
// Systems run in a fixed order. Each declares a cadence; on every simulated
// day the world runs the systems that are due. "Generate" and "watch live"
// are the same loop: the CLI calls run_until() flat out, the viewer calls
// step() as fast as its speed setting allows.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "hsim/binary_io.hpp"
#include "hsim/calendar.hpp"
#include "hsim/catalog.hpp"
#include "hsim/event_log.hpp"
#include "hsim/jobs.hpp"
#include "hsim/rng.hpp"
#include "hsim/snapshots.hpp"

namespace hsim {

class World;

enum class Cadence : std::uint8_t {
    Daily,
    Monthly,   // 1st of each month
    Seasonal,  // 1 Mar, 1 Jun, 1 Sep, 1 Dec
    Yearly,    // 1 Jan
};

bool cadence_due(Cadence c, Day day);

// A requested intervention ("god power"). Phase 0 only provides the hook:
// interventions are queued, applied at the start of the next day, and any
// events they cause carry Cause::intervention().
struct Intervention {
    std::string kind;
    std::vector<Field> params;
    EntityRef target;
};

struct InspectRow {
    std::string label;
    std::string value;
    EntityRef link;  // set when the value is another entity
};

class System {
public:
    virtual ~System() = default;

    virtual const char* name() const = 0;
    // 4-character tag identifying this system's section in save files.
    virtual const char* save_tag() const = 0;
    virtual Cadence cadence() const = 0;

    virtual void register_catalog(EventCatalog& catalog) = 0;
    // Called once for a brand-new world (not after loading).
    virtual void init(World& world) = 0;
    virtual void update(World& world) = 0;

    virtual void save(BinaryWriter& w) const = 0;
    virtual void load(BinaryReader& r) = 0;

    // Entities this system owns (for the inspector and for naming).
    virtual bool owns(EntityKind) const { return false; }
    virtual std::string name_of(EntityRef) const { return {}; }
    virtual void list(EntityKind, bool /*alive_only*/, std::vector<EntityRef>&) const {}
    virtual void inspect(EntityRef, const World&, std::vector<InspectRow>&) const {}

    // Layers for map snapshots.
    virtual void snapshot_layers(std::vector<SnapshotLayer>&) const {}

    // Returns true if this system handled the intervention.
    virtual bool apply_intervention(World&, const Intervention&) { return false; }
};

struct WorldSettings {
    std::uint64_t seed = 1;
    std::int64_t start_year = -9999;  // astronomical; -9999 = 10000 BCE
    // Where this run's history (event chunks, snapshots) is written.
    // Empty = keep it in memory.
    std::filesystem::path history_dir;
    Importance min_importance = Importance::Trivial;
    int snapshot_interval_years = 10;
    unsigned threads = 0;  // 0 = all hardware threads
    bool god_powers = false;
};

class World {
public:
    // Builds a world with the standard set of systems.
    explicit World(WorldSettings settings);
    // Builds a world with a custom set of systems (tests).
    World(WorldSettings settings, std::vector<std::unique_ptr<System>> systems);
    ~World();

    // Creates initial state for a new world. Call once, or use load().
    void init_new();

    // Advances one day.
    void step();
    // Advances until `today() >= day`, or until `should_stop()` returns true
    // (checked once per simulated month).
    template <class Stop>
    void run_until(Day day, Stop&& should_stop) {
        while (today_ < day) {
            step();
            if (to_date(today_).day == 1 && should_stop()) break;
        }
    }
    void run_until(Day day) {
        run_until(day, [] { return false; });
    }

    Day today() const { return today_; }
    std::uint64_t seed() const { return settings_.seed; }
    const WorldSettings& settings() const { return settings_; }

    // Deterministic randomness, keyed by world seed + system + more context.
    Rng rng(const char* system) const { return Rng(settings_.seed).with(system); }

    EventLog& events() { return events_; }
    const EventLog& events() const { return events_; }
    SnapshotStore& snapshots() { return snapshots_; }
    const SnapshotStore& snapshots() const { return snapshots_; }
    const EventCatalog& catalog() const { return catalog_; }
    JobSystem& jobs() { return *jobs_; }

    // Records an event happening today. Returns its id (kNoEvent if dropped).
    EventId record(Event e);

    // Entity helpers (dispatch to the owning system).
    std::string name_of(EntityRef ref) const;
    std::vector<EntityRef> list(EntityKind kind, bool alive_only) const;
    std::vector<InspectRow> inspect(EntityRef ref) const;
    std::string summarize(const Event& e) const;
    std::string summarize(EventId id) const;
    std::string describe_cause(const Cause& c) const;

    // God powers. Returns false if disabled.
    bool intervene(Intervention i);
    bool intervening() const { return applying_intervention_; }

    // Hash of the full simulation state (not counting history files'
    // location). Equal hashes = identical worlds.
    std::uint64_t state_hash() const;

    // Save/load. A save is `path` plus a folder `path` + ".history" holding
    // the closed history chunks and snapshots.
    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);

    static std::filesystem::path history_folder_for(const std::filesystem::path& save_path);

    const std::vector<std::unique_ptr<System>>& systems() const { return systems_; }

private:
    void take_snapshot();
    void write_state(BinaryWriter& w) const;  // everything but history

    WorldSettings settings_;
    Day today_ = 0;
    std::vector<std::unique_ptr<System>> systems_;
    EventCatalog catalog_;
    EventLog events_;
    SnapshotStore snapshots_;
    std::unique_ptr<JobSystem> jobs_;
    std::vector<Intervention> pending_interventions_;
    bool applying_intervention_ = false;
    std::uint64_t events_hash_ = 0;
};

// A private working folder for one session's history files, removed when
// this object is destroyed. Saved worlds keep their own hard-linked copies.
class SessionDir {
public:
    SessionDir();
    ~SessionDir();
    SessionDir(const SessionDir&) = delete;
    SessionDir& operator=(const SessionDir&) = delete;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

// The standard systems, in update order.
std::vector<std::unique_ptr<System>> make_standard_systems();

// Save file format version. Bump when the layout changes.
constexpr std::uint32_t kSaveFormatVersion = 1;

}  // namespace hsim
