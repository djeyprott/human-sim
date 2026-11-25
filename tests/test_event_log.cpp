#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>

#include "hsim/event_log.hpp"
#include "hsim/world.hpp"

using namespace hsim;

namespace {
Event make(Day day, std::uint32_t who, Importance imp = Importance::Notable, EventId cause = kNoEvent) {
    Event e;
    e.day = day;
    e.type = 7;
    e.importance = imp;
    e.participants = {{EntityRef{EntityKind::Person, who, 0}, 1}};
    if (cause) e.causes = {Cause::from_event(cause)};
    e.fields = {{0, std::int64_t(who)}, {1, 2.5}, {2, std::string("text")}, {3, EntityRef{EntityKind::Org, 3, 1}}};
    return e;
}

// Fills a log with events spanning several centuries.
void fill(EventLog& log) {
    Day start = first_day_of_year(-500);
    for (int i = 0; i < 1000; ++i) log.append(make(start + i * 100, static_cast<std::uint32_t>(i % 10)));
}
}  // namespace

TEST_CASE("event log: ids, lookup and per-entity index (memory mode)") {
    EventLog log;
    fill(log);
    CHECK(log.total_events() == 1000);
    CHECK(log.closed_chunks().size() >= 2);  // ~274 years span 3+ centuries

    auto e = log.get(500);
    REQUIRE(e);
    CHECK(e->id == 500);
    CHECK(e->fields.size() == 4);
    CHECK_FALSE(log.get(0));
    CHECK_FALSE(log.get(1001));

    auto mine = log.events_for(EntityRef{EntityKind::Person, 3, 0});
    CHECK(mine.size() == 100);
    for (std::size_t i = 1; i < mine.size(); ++i) CHECK(mine[i] > mine[i - 1]);
}

TEST_CASE("event log: disk mode matches memory mode") {
    SessionDir dir;
    EventLog disk(dir.path());
    EventLog mem;
    fill(disk);
    fill(mem);
    CHECK(!disk.closed_chunks().empty());
    for (EventId id = 1; id <= 1000; id += 37) CHECK(*disk.get(id) == *mem.get(id));
    CHECK(disk.events_for(EntityRef{EntityKind::Person, 4, 0}) == mem.events_for(EntityRef{EntityKind::Person, 4, 0}));
    CHECK(std::filesystem::exists(dir.path() / EventLog::chunk_file_name(disk.closed_chunks()[0].century)));
}

TEST_CASE("event log: scanning a day range, both directions") {
    EventLog log;
    fill(log);
    Day from = first_day_of_year(-450), to = first_day_of_year(-400);
    std::vector<EventId> fwd, back;
    log.scan(from, to, [&](const Event& e) {
        CHECK(e.day >= from);
        CHECK(e.day <= to);
        fwd.push_back(e.id);
        return true;
    });
    log.scan_reverse(from, to, [&](const Event& e) {
        back.push_back(e.id);
        return true;
    });
    REQUIRE(!fwd.empty());
    std::reverse(back.begin(), back.end());
    CHECK(fwd == back);
}

TEST_CASE("event log: events below the threshold are not recorded") {
    EventLog log({}, Importance::Notable);
    CHECK(log.append(make(0, 1, Importance::Trivial)) == kNoEvent);
    CHECK(log.append(make(1, 1, Importance::Major)) == 1);
    CHECK(log.total_events() == 1);
}

TEST_CASE("event log: events must be in day order") {
    EventLog log;
    log.append(make(10, 1));
    CHECK_THROWS(log.append(make(9, 1)));
}

TEST_CASE("event log: save and load through a history folder") {
    SessionDir work, save, work2;
    EventLog log(work.path());
    fill(log);
    BinaryWriter w;
    log.save(w, save.path());

    EventLog loaded(work2.path());
    BinaryReader r(w.bytes());
    loaded.load(r, save.path());
    CHECK(loaded.total_events() == log.total_events());
    for (EventId id = 1; id <= 1000; id += 13) CHECK(*loaded.get(id) == *log.get(id));
    // New events continue the sequence.
    CHECK(loaded.append(make(first_day_of_year(2000), 1)) == 1001);
}

TEST_CASE("event log: a corrupt chunk is refused") {
    std::vector<std::uint8_t> junk = {1, 2, 3};
    BinaryReader r(junk);
    CHECK_THROWS_AS(read_chunk(r), FormatError);
}
