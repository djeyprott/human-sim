#include "hsim/event_log.hpp"

#include <algorithm>
#include <stdexcept>
#include <system_error>

namespace hsim {

namespace {
constexpr char kChunkMagic[5] = "HEVC";
constexpr std::uint32_t kChunkVersion = 1;

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

std::int64_t century_of(Day day) { return floor_div(to_date(day).year, 100); }

void put_ref(BinaryWriter& w, EntityRef r) {
    w.put(r.kind);
    w.put(r.index);
    w.put(r.generation);
}

EntityRef get_ref(BinaryReader& r) {
    EntityRef e;
    e.kind = r.get<EntityKind>();
    e.index = r.get<std::uint32_t>();
    e.generation = r.get<std::uint32_t>();
    return e;
}

}  // namespace

void write_event(BinaryWriter& w, const Event& e) {
    w.put(e.id);
    w.put(e.day);
    w.put(e.place);
    w.put(e.type);
    w.put(e.importance);
    w.put<std::uint16_t>(static_cast<std::uint16_t>(e.participants.size()));
    for (const auto& p : e.participants) {
        put_ref(w, p.entity);
        w.put(p.role);
    }
    w.put<std::uint16_t>(static_cast<std::uint16_t>(e.causes.size()));
    for (const auto& c : e.causes) {
        w.put(c.kind);
        w.put(c.event);
        w.put(c.condition);
        w.put(c.value);
        put_ref(w, c.subject);
    }
    w.put<std::uint16_t>(static_cast<std::uint16_t>(e.fields.size()));
    for (const auto& f : e.fields) {
        w.put(f.key);
        w.put<std::uint8_t>(static_cast<std::uint8_t>(f.value.index()));
        switch (f.value.index()) {
            case 0: w.put(std::get<std::int64_t>(f.value)); break;
            case 1: w.put(std::get<double>(f.value)); break;
            case 2: w.put_string(std::get<std::string>(f.value)); break;
            case 3: put_ref(w, std::get<EntityRef>(f.value)); break;
        }
    }
}

Event read_event(BinaryReader& r) {
    Event e;
    e.id = r.get<EventId>();
    e.day = r.get<Day>();
    e.place = r.get<std::uint32_t>();
    e.type = r.get<EventType>();
    e.importance = r.get<Importance>();
    auto np = r.get<std::uint16_t>();
    e.participants.resize(np);
    for (auto& p : e.participants) {
        p.entity = get_ref(r);
        p.role = r.get<Role>();
    }
    auto nc = r.get<std::uint16_t>();
    e.causes.resize(nc);
    for (auto& c : e.causes) {
        c.kind = r.get<Cause::Kind>();
        c.event = r.get<EventId>();
        c.condition = r.get<std::uint16_t>();
        c.value = r.get<double>();
        c.subject = get_ref(r);
    }
    auto nf = r.get<std::uint16_t>();
    e.fields.resize(nf);
    for (auto& f : e.fields) {
        f.key = r.get<std::uint16_t>();
        switch (r.get<std::uint8_t>()) {
            case 0: f.value = r.get<std::int64_t>(); break;
            case 1: f.value = r.get<double>(); break;
            case 2: f.value = r.get_string(); break;
            case 3: f.value = get_ref(r); break;
            default: throw FormatError("bad field value type");
        }
    }
    return e;
}

void EventChunk::index(const Event& e) {
    for (const auto& p : e.participants) {
        auto& ids = by_entity[p.entity.key()];
        if (ids.empty() || ids.back() != e.id) ids.push_back(e.id);  // once per event
    }
}

void write_chunk(BinaryWriter& w, const EventChunk& c) {
    w.put(c.century);
    w.put(c.first_id);
    w.put<std::uint64_t>(c.events.size());
    for (const auto& e : c.events) write_event(w, e);
}

EventChunk read_chunk(BinaryReader& r) {
    EventChunk c;
    c.century = r.get<std::int64_t>();
    c.first_id = r.get<EventId>();
    auto n = r.get<std::uint64_t>();
    if (n > r.remaining()) throw FormatError("event count out of range");
    c.events.reserve(static_cast<std::size_t>(n));
    for (std::uint64_t i = 0; i < n; ++i) {
        c.events.push_back(read_event(r));
        if (c.events.back().id != c.first_id + i) throw FormatError("event ids out of sequence");
        c.index(c.events.back());
    }
    return c;
}

void link_or_copy(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    if (std::filesystem::equivalent(from, to, ec)) return;
    std::filesystem::remove(to, ec);
    std::filesystem::create_hard_link(from, to, ec);
    if (ec) std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
}

std::string EventLog::chunk_file_name(std::int64_t century) {
    return "events_" + std::to_string(century) + ".hev";
}

EventLog::EventLog(std::filesystem::path history_dir, Importance min_importance)
    : dir_(std::move(history_dir)), min_importance_(min_importance) {
    if (!dir_.empty()) std::filesystem::create_directories(dir_);
}

EventId EventLog::append(Event e) {
    if (e.importance < min_importance_) return kNoEvent;
    if (e.day < last_day_) throw std::logic_error("events must be appended in day order");
    last_day_ = e.day;

    std::int64_t century = century_of(e.day);
    if (has_open_ && century != open_.century) close_open_chunk();
    if (!has_open_) {
        open_ = EventChunk{};
        open_.century = century;
        open_.first_id = next_id_;
        has_open_ = true;
    }
    e.id = next_id_++;
    open_.index(e);
    open_.events.push_back(std::move(e));
    return open_.events.back().id;
}

void EventLog::close_open_chunk() {
    if (!has_open_) return;
    ChunkInfo info;
    info.century = open_.century;
    info.first_id = open_.first_id;
    info.count = open_.events.size();
    info.first_day = open_.events.empty() ? 0 : open_.events.front().day;
    info.last_day = open_.events.empty() ? 0 : open_.events.back().day;

    if (dir_.empty()) {
        resident_[open_.century] = std::make_shared<const EventChunk>(std::move(open_));
    } else {
        BinaryWriter w;
        w.bytes().insert(w.bytes().end(), kChunkMagic, kChunkMagic + 4);
        w.put(kChunkVersion);
        write_chunk(w, open_);
        write_file(dir_ / chunk_file_name(open_.century), w.bytes());
    }
    closed_.push_back(info);
    open_ = EventChunk{};
    has_open_ = false;
}

std::shared_ptr<const EventChunk> EventLog::load_chunk(std::size_t i) const {
    std::int64_t century = closed_[i].century;
    auto it = resident_.find(century);
    if (it != resident_.end()) {
        if (!dir_.empty()) {
            lru_.remove(century);
            lru_.push_front(century);
        }
        return it->second;
    }
    if (dir_.empty()) throw std::logic_error("missing in-memory chunk");

    BinaryReader r(read_file(dir_ / chunk_file_name(century)));
    char magic[4];
    for (char& c : magic) c = static_cast<char>(r.get<std::uint8_t>());
    if (std::string(magic, 4) != std::string(kChunkMagic, 4)) throw FormatError("not an event chunk file");
    if (r.get<std::uint32_t>() != kChunkVersion) throw FormatError("unsupported event chunk version");
    auto chunk = std::make_shared<const EventChunk>(read_chunk(r));

    resident_[century] = chunk;
    lru_.push_front(century);
    while (lru_.size() > kCacheChunks) {
        resident_.erase(lru_.back());
        lru_.pop_back();
    }
    return chunk;
}

std::size_t EventLog::find_closed(EventId id) const {
    auto it = std::upper_bound(closed_.begin(), closed_.end(), id,
                               [](EventId v, const ChunkInfo& c) { return v < c.first_id; });
    if (it == closed_.begin()) return std::size_t(-1);
    --it;
    if (id >= it->first_id + it->count) return std::size_t(-1);
    return static_cast<std::size_t>(it - closed_.begin());
}

std::optional<Event> EventLog::get(EventId id) const {
    if (id == kNoEvent || id >= next_id_) return std::nullopt;
    if (has_open_ && id >= open_.first_id) {
        std::size_t k = static_cast<std::size_t>(id - open_.first_id);
        if (k < open_.events.size()) return open_.events[k];
        return std::nullopt;
    }
    std::size_t i = find_closed(id);
    if (i == std::size_t(-1)) return std::nullopt;
    auto chunk = load_chunk(i);
    return chunk->events[static_cast<std::size_t>(id - chunk->first_id)];
}

std::vector<EventId> EventLog::events_for(EntityRef entity) const {
    std::vector<EventId> out;
    auto key = entity.key();
    for (std::size_t i = 0; i < closed_.size(); ++i) {
        auto chunk = load_chunk(i);
        auto it = chunk->by_entity.find(key);
        if (it != chunk->by_entity.end()) out.insert(out.end(), it->second.begin(), it->second.end());
    }
    if (has_open_) {
        auto it = open_.by_entity.find(key);
        if (it != open_.by_entity.end()) out.insert(out.end(), it->second.begin(), it->second.end());
    }
    return out;
}

void EventLog::scan(Day first_day, Day last_day, const std::function<bool(const Event&)>& fn) const {
    auto visit = [&](const std::vector<Event>& events) {
        auto it = std::lower_bound(events.begin(), events.end(), first_day,
                                   [](const Event& e, Day d) { return e.day < d; });
        for (; it != events.end() && it->day <= last_day; ++it)
            if (!fn(*it)) return false;
        return true;
    };
    for (std::size_t i = 0; i < closed_.size(); ++i) {
        const auto& info = closed_[i];
        if (info.last_day < first_day || info.first_day > last_day) continue;
        if (!visit(load_chunk(i)->events)) return;
    }
    if (has_open_) visit(open_.events);
}

void EventLog::scan_reverse(Day first_day, Day last_day, const std::function<bool(const Event&)>& fn) const {
    auto visit = [&](const std::vector<Event>& events) {
        auto it = std::upper_bound(events.begin(), events.end(), last_day,
                                   [](Day d, const Event& e) { return d < e.day; });
        while (it != events.begin()) {
            --it;
            if (it->day < first_day) return true;
            if (!fn(*it)) return false;
        }
        return true;
    };
    if (has_open_ && !visit(open_.events)) return;
    for (std::size_t i = closed_.size(); i-- > 0;) {
        const auto& info = closed_[i];
        if (info.last_day < first_day || info.first_day > last_day) continue;
        if (!visit(load_chunk(i)->events)) return;
    }
}

void EventLog::save(BinaryWriter& w, const std::filesystem::path& save_dir) const {
    w.put(next_id_);
    w.put(last_day_);
    w.put(min_importance_);
    w.put<std::uint64_t>(closed_.size());
    for (const auto& c : closed_) {
        w.put(c.century);
        w.put(c.first_id);
        w.put(c.count);
        w.put(c.first_day);
        w.put(c.last_day);
    }
    w.put<std::uint8_t>(has_open_ ? 1 : 0);
    if (has_open_) write_chunk(w, open_);

    // Closed chunks go into the save's history folder.
    std::filesystem::create_directories(save_dir);
    std::vector<std::string> wanted;
    for (std::size_t i = 0; i < closed_.size(); ++i) {
        auto name = chunk_file_name(closed_[i].century);
        wanted.push_back(name);
        if (!dir_.empty()) {
            link_or_copy(dir_ / name, save_dir / name);
        } else {
            BinaryWriter cw;
            cw.bytes().insert(cw.bytes().end(), kChunkMagic, kChunkMagic + 4);
            cw.put(kChunkVersion);
            write_chunk(cw, *load_chunk(i));
            write_file(save_dir / name, cw.bytes());
        }
    }
    // Remove chunk files left over from an older save at the same path.
    for (const auto& entry : std::filesystem::directory_iterator(save_dir)) {
        auto name = entry.path().filename().string();
        if (name.starts_with("events_") && name.ends_with(".hev") &&
            std::find(wanted.begin(), wanted.end(), name) == wanted.end())
            std::filesystem::remove(entry.path());
    }
}

void EventLog::load(BinaryReader& r, const std::filesystem::path& save_dir) {
    next_id_ = r.get<EventId>();
    last_day_ = r.get<Day>();
    min_importance_ = r.get<Importance>();
    auto n = r.get<std::uint64_t>();
    if (n > r.remaining()) throw FormatError("chunk count out of range");
    closed_.clear();
    resident_.clear();
    lru_.clear();
    for (std::uint64_t i = 0; i < n; ++i) {
        ChunkInfo c;
        c.century = r.get<std::int64_t>();
        c.first_id = r.get<EventId>();
        c.count = r.get<std::uint64_t>();
        c.first_day = r.get<Day>();
        c.last_day = r.get<Day>();
        closed_.push_back(c);
    }
    has_open_ = r.get<std::uint8_t>() != 0;
    open_ = has_open_ ? read_chunk(r) : EventChunk{};

    for (const auto& c : closed_) {
        auto name = chunk_file_name(c.century);
        if (!std::filesystem::exists(save_dir / name)) throw FormatError("missing history file " + name);
        if (!dir_.empty()) {
            link_or_copy(save_dir / name, dir_ / name);
        } else {
            BinaryReader cr(read_file(save_dir / name));
            for (int k = 0; k < 4; ++k) cr.get<std::uint8_t>();
            if (cr.get<std::uint32_t>() != kChunkVersion) throw FormatError("unsupported event chunk version");
            resident_[c.century] = std::make_shared<const EventChunk>(read_chunk(cr));
        }
    }
}

}  // namespace hsim
