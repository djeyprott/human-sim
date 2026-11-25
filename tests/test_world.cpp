#include <doctest/doctest.h>

#include "hsim/systems/wanderers.hpp"
#include "hsim/world.hpp"

using namespace hsim;

namespace {
WorldSettings settings(std::uint64_t seed, const SessionDir& dir, unsigned threads = 2) {
    WorldSettings s;
    s.seed = seed;
    s.start_year = -9999;
    s.history_dir = dir.path();
    s.threads = threads;
    return s;
}

const Day kStart = first_day_of_year(-9999);
const Day k250Years = first_day_of_year(-9749);
}  // namespace

TEST_CASE("world: cadences") {
    CHECK(cadence_due(Cadence::Daily, kStart + 17));
    CHECK(cadence_due(Cadence::Monthly, to_day(Date{-500, 7, 1})));
    CHECK_FALSE(cadence_due(Cadence::Monthly, to_day(Date{-500, 7, 2})));
    CHECK(cadence_due(Cadence::Seasonal, to_day(Date{-500, 9, 1})));
    CHECK_FALSE(cadence_due(Cadence::Seasonal, to_day(Date{-500, 8, 1})));
    CHECK(cadence_due(Cadence::Yearly, to_day(Date{-500, 1, 1})));
}

TEST_CASE("world: the placeholder system produces history with causes") {
    SessionDir dir;
    World world(settings(1, dir));
    world.init_new();
    world.run_until(k250Years);

    CHECK(world.today() == k250Years);
    CHECK(world.events().total_events() > 1000);
    CHECK(world.events().closed_chunks().size() >= 2);
    CHECK(world.snapshots().days().size() == 25);

    // Some deaths, some births, and causes that resolve to text.
    bool saw_birth = false, saw_death = false;
    world.events().scan(INT64_MIN, INT64_MAX, [&](const Event& e) {
        if (e.type == WanderersSystem::kBirth) {
            saw_birth = true;
            CHECK(!e.causes.empty());
        }
        if (e.type == WanderersSystem::kDeath) {
            saw_death = true;
            REQUIRE(!e.causes.empty());
            CHECK(!world.describe_cause(e.causes[0]).empty());
        }
        CHECK(world.summarize(e).find('{') == std::string::npos);  // template fully filled
        return true;
    });
    CHECK(saw_birth);
    CHECK(saw_death);

    // Every wanderer, living or dead, can be named and has a history.
    auto everyone = world.list(EntityKind::Wanderer, false);
    CHECK(everyone.size() > 60);
    for (auto r : everyone) {
        CHECK(!world.name_of(r).empty());
        CHECK(!world.events().events_for(r).empty());
        CHECK(!world.inspect(r).empty());
    }
}

TEST_CASE("world: determinism - same seed, same history, any thread count") {
    SessionDir d1, d2, d3;
    World a(settings(7, d1, 1));
    World b(settings(7, d2, 8));
    World c(settings(8, d3, 1));
    for (World* w : {&a, &b, &c}) {
        w->init_new();
        w->run_until(k250Years);
    }
    CHECK(a.state_hash() == b.state_hash());
    CHECK(a.events().total_events() == b.events().total_events());
    CHECK(a.state_hash() != c.state_hash());
}

TEST_CASE("world: save, load and continue gives the same result as never stopping") {
    SessionDir d1, d2, d3, saves;
    const Day mid = first_day_of_year(-9850);

    World straight(settings(3, d1));
    straight.init_new();
    straight.run_until(k250Years);

    auto path = saves.path() / "world.hsim";
    {
        World first(settings(3, d2));
        first.init_new();
        first.run_until(mid);
        first.save(path);

        World check(settings(0, d3));  // seed comes from the save
        check.load(path);
        CHECK(check.state_hash() == first.state_hash());
    }

    SessionDir d4;
    World resumed(settings(0, d4));
    resumed.load(path);
    CHECK(resumed.today() == mid);
    CHECK(resumed.seed() == 3);
    resumed.run_until(k250Years);
    CHECK(resumed.state_hash() == straight.state_hash());

    // History from before the save is still readable after continuing.
    auto e = resumed.events().get(1);
    REQUIRE(e);
    CHECK(*e == *straight.events().get(1));
}

TEST_CASE("world: saving over an older save replaces its history") {
    SessionDir d1, d2, saves;
    auto path = saves.path() / "world.hsim";
    World a(settings(1, d1));
    a.init_new();
    a.run_until(k250Years);
    a.save(path);

    World b(settings(2, d2));
    b.init_new();
    b.run_until(first_day_of_year(-9900));
    b.save(path);

    SessionDir d3;
    World loaded(settings(0, d3));
    loaded.load(path);
    CHECK(loaded.state_hash() == b.state_hash());
    std::size_t chunk_files = 0;
    for (auto& f : std::filesystem::directory_iterator(World::history_folder_for(path)))
        chunk_files += f.path().extension() == ".hev";
    CHECK(chunk_files == b.events().closed_chunks().size());
}

TEST_CASE("world: corrupt or foreign saves are refused") {
    SessionDir dir, d1;
    auto path = dir.path() / "bad.hsim";
    write_file(path, {'N', 'O', 'P', 'E', 1, 0, 0, 0});
    World w(settings(1, d1));
    CHECK_THROWS_AS(w.load(path), FormatError);

    write_file(path, {'H', 'S', 'I', 'M', 99, 0, 0, 0});
    CHECK_THROWS_AS(w.load(path), FormatError);
}

TEST_CASE("world: god powers are off by default") {
    SessionDir dir;
    World w(settings(1, dir));
    w.init_new();
    CHECK_FALSE(w.intervene(Intervention{"anything", {}, {}}));
}
