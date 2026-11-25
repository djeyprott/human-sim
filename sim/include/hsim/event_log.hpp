#pragma once
// The event log.
//
// Events are grouped into chunks, one per century of simulated time. The
// chunk being written stays in memory; when the simulation moves into the
// next century it is closed and written to the history directory, and only
// read back (into a small cache) when someone asks for it. Memory use thus
// depends on the living world, not on how long the run has lasted.
//
// Closed chunk files never change. A save hard-links them into the save's
// own history folder (copying where hard links are not possible), so saving
// a 10,000-year-old world is still fast.
//
// With an empty history directory everything stays in memory (tests, tools).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "hsim/binary_io.hpp"
#include "hsim/event.hpp"

namespace hsim {

struct EventChunk {
    std::int64_t century = 0;  // floor(astronomical year / 100)
    EventId first_id = kNoEvent;
    std::vector<Event> events;  // ids first_id, first_id+1, ... in order
    // Per-entity index: entity key -> event ids, ascending.
    std::map<std::uint64_t, std::vector<EventId>> by_entity;

    EventId last_id() const { return events.empty() ? first_id - 1 : events.back().id; }
    void index(const Event& e);
};

struct ChunkInfo {
    std::int64_t century = 0;
    EventId first_id = kNoEvent;
    std::uint64_t count = 0;
    Day first_day = 0, last_day = 0;
};

class EventLog {
public:
    // `history_dir` empty = keep everything in memory.
    explicit EventLog(std::filesystem::path history_dir = {}, Importance min_importance = Importance::Trivial);

    // Records an event. Assigns and returns its id, or kNoEvent if the event
    // is below the recording threshold. Events must arrive in day order.
    EventId append(Event e);

    EventId last_id() const { return next_id_ - 1; }
    std::uint64_t total_events() const { return next_id_ - 1; }

    // Lookup. Returns nullopt for ids that do not exist.
    std::optional<Event> get(EventId id) const;

    // All event ids an entity took part in, oldest first.
    std::vector<EventId> events_for(EntityRef entity) const;

    // Visits events with first_day <= day <= last_day, oldest first, until fn returns false.
    void scan(Day first_day, Day last_day, const std::function<bool(const Event&)>& fn) const;
    // Same, newest first.
    void scan_reverse(Day first_day, Day last_day, const std::function<bool(const Event&)>& fn) const;

    Importance min_importance() const { return min_importance_; }
    void set_min_importance(Importance i) { min_importance_ = i; }

    const std::vector<ChunkInfo>& closed_chunks() const { return closed_; }
    const std::filesystem::path& history_dir() const { return dir_; }

    // Save/load. The open chunk is written inline; closed chunks are linked
    // into `save_history_dir`.
    void save(BinaryWriter& w, const std::filesystem::path& save_history_dir) const;
    void load(BinaryReader& r, const std::filesystem::path& save_history_dir);

    // Chunk file name for a century (stable; used in saves).
    static std::string chunk_file_name(std::int64_t century);

private:
    void close_open_chunk();
    std::shared_ptr<const EventChunk> load_chunk(std::size_t closed_index) const;
    // Index into closed_ of the chunk holding `id`, or npos.
    std::size_t find_closed(EventId id) const;
    template <class F>
    void for_each_chunk(bool reverse, F&& f) const;

    std::filesystem::path dir_;
    Importance min_importance_;
    EventId next_id_ = 1;
    Day last_day_ = INT64_MIN;

    EventChunk open_;
    bool has_open_ = false;
    std::vector<ChunkInfo> closed_;
    // In-memory mode keeps closed chunks here; disk mode uses it as an LRU cache.
    mutable std::map<std::int64_t, std::shared_ptr<const EventChunk>> resident_;
    mutable std::list<std::int64_t> lru_;
    static constexpr std::size_t kCacheChunks = 8;
};

// Serialization of single events and chunks.
void write_event(BinaryWriter& w, const Event& e);
Event read_event(BinaryReader& r);
void write_chunk(BinaryWriter& w, const EventChunk& c);
EventChunk read_chunk(BinaryReader& r);

// Makes `to` the same file as `from`: a hard link if possible, else a copy.
void link_or_copy(const std::filesystem::path& from, const std::filesystem::path& to);

}  // namespace hsim
