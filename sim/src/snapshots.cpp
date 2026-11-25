#include "hsim/snapshots.hpp"

#include <algorithm>

#include "hsim/event_log.hpp"  // link_or_copy

namespace hsim {

namespace {
constexpr char kMagic[5] = "HSNP";
constexpr std::uint32_t kVersion = 1;

std::vector<std::uint8_t> encode(const Snapshot& s) {
    BinaryWriter w;
    w.bytes().insert(w.bytes().end(), kMagic, kMagic + 4);
    w.put(kVersion);
    w.put(s.day);
    w.put<std::uint32_t>(static_cast<std::uint32_t>(s.layers.size()));
    for (const auto& l : s.layers) {
        w.put_string(l.name);
        w.put_vector(l.values);
    }
    return std::move(w.bytes());
}

Snapshot decode(std::vector<std::uint8_t> bytes) {
    BinaryReader r(std::move(bytes));
    char magic[4];
    for (char& c : magic) c = static_cast<char>(r.get<std::uint8_t>());
    if (std::string(magic, 4) != std::string(kMagic, 4)) throw FormatError("not a snapshot file");
    if (r.get<std::uint32_t>() != kVersion) throw FormatError("unsupported snapshot version");
    Snapshot s;
    s.day = r.get<Day>();
    auto n = r.get<std::uint32_t>();
    for (std::uint32_t i = 0; i < n; ++i) {
        SnapshotLayer l;
        l.name = r.get_string();
        l.values = r.get_vector<float>();
        s.layers.push_back(std::move(l));
    }
    return s;
}
}  // namespace

std::string SnapshotStore::file_name(Day day) { return "snapshot_" + std::to_string(day) + ".hsn"; }

SnapshotStore::SnapshotStore(std::filesystem::path history_dir, int interval_years)
    : dir_(std::move(history_dir)), interval_years_(interval_years < 1 ? 1 : interval_years) {
    if (!dir_.empty()) std::filesystem::create_directories(dir_);
}

bool SnapshotStore::due(Day day) const {
    Date d = to_date(day);
    if (d.month != 1 || d.day != 1) return false;
    std::int64_t m = d.year % interval_years_;
    return m == 0;
}

void SnapshotStore::add(Snapshot s) {
    if (!days_.empty() && s.day <= days_.back()) return;  // already have it
    days_.push_back(s.day);
    if (dir_.empty()) {
        resident_[s.day] = std::make_shared<const Snapshot>(std::move(s));
    } else {
        write_file(dir_ / file_name(s.day), encode(s));
    }
}

std::shared_ptr<const Snapshot> SnapshotStore::load_one(Day day) const {
    auto it = resident_.find(day);
    if (it != resident_.end()) {
        if (!dir_.empty()) {
            lru_.remove(day);
            lru_.push_front(day);
        }
        return it->second;
    }
    auto snap = std::make_shared<const Snapshot>(decode(read_file(dir_ / file_name(day))));
    resident_[day] = snap;
    lru_.push_front(day);
    while (lru_.size() > kCache) {
        resident_.erase(lru_.back());
        lru_.pop_back();
    }
    return snap;
}

std::shared_ptr<const Snapshot> SnapshotStore::at_or_before(Day day) const {
    auto it = std::upper_bound(days_.begin(), days_.end(), day);
    if (it == days_.begin()) return nullptr;
    return load_one(*(it - 1));
}

void SnapshotStore::save(BinaryWriter& w, const std::filesystem::path& save_dir) const {
    w.put<std::int32_t>(interval_years_);
    w.put_vector(days_);
    std::filesystem::create_directories(save_dir);
    std::vector<std::string> wanted;
    for (Day d : days_) {
        auto name = file_name(d);
        wanted.push_back(name);
        if (!dir_.empty())
            link_or_copy(dir_ / name, save_dir / name);
        else
            write_file(save_dir / name, encode(*load_one(d)));
    }
    for (const auto& entry : std::filesystem::directory_iterator(save_dir)) {
        auto name = entry.path().filename().string();
        if (name.starts_with("snapshot_") && name.ends_with(".hsn") &&
            std::find(wanted.begin(), wanted.end(), name) == wanted.end())
            std::filesystem::remove(entry.path());
    }
}

void SnapshotStore::load(BinaryReader& r, const std::filesystem::path& save_dir) {
    interval_years_ = r.get<std::int32_t>();
    days_ = r.get_vector<Day>();
    resident_.clear();
    lru_.clear();
    for (Day d : days_) {
        auto name = file_name(d);
        if (!std::filesystem::exists(save_dir / name)) throw FormatError("missing history file " + name);
        if (!dir_.empty())
            link_or_copy(save_dir / name, dir_ / name);
        else
            resident_[d] = std::make_shared<const Snapshot>(decode(read_file(save_dir / name)));
    }
}

}  // namespace hsim
