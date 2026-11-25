#pragma once
// Periodic snapshots of map-level state, for drawing historical maps.
//
// Every `interval_years` (on 1 January) each system contributes named layers
// of per-cell values (population, culture, controlling organization, ...).
// Snapshots are written to the history directory like event chunks and read
// back on demand. Phase 0 has no map, so layers are whatever systems provide.

#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "hsim/binary_io.hpp"
#include "hsim/calendar.hpp"

namespace hsim {

struct SnapshotLayer {
    std::string name;
    std::vector<float> values;
    friend bool operator==(const SnapshotLayer&, const SnapshotLayer&) = default;
};

struct Snapshot {
    Day day = 0;
    std::vector<SnapshotLayer> layers;
    friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

class SnapshotStore {
public:
    explicit SnapshotStore(std::filesystem::path history_dir = {}, int interval_years = 10);

    int interval_years() const { return interval_years_; }
    void set_interval_years(int years) { interval_years_ = years < 1 ? 1 : years; }

    // True if a snapshot is due on this day.
    bool due(Day day) const;

    void add(Snapshot s);

    const std::vector<Day>& days() const { return days_; }
    // The latest snapshot taken on or before `day`, if any.
    std::shared_ptr<const Snapshot> at_or_before(Day day) const;

    void save(BinaryWriter& w, const std::filesystem::path& save_dir) const;
    void load(BinaryReader& r, const std::filesystem::path& save_dir);

    static std::string file_name(Day day);

private:
    std::shared_ptr<const Snapshot> load_one(Day day) const;

    std::filesystem::path dir_;
    int interval_years_;
    std::vector<Day> days_;
    mutable std::map<Day, std::shared_ptr<const Snapshot>> resident_;
    mutable std::list<Day> lru_;
    static constexpr std::size_t kCache = 16;
};

}  // namespace hsim
