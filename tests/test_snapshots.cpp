#include <doctest/doctest.h>

#include "hsim/snapshots.hpp"
#include "hsim/world.hpp"

using namespace hsim;

TEST_CASE("snapshots: due every N years on 1 January") {
    SnapshotStore s({}, 10);
    CHECK(s.due(first_day_of_year(-9990)));
    CHECK(s.due(first_day_of_year(0)));
    CHECK_FALSE(s.due(first_day_of_year(-9995)));
    CHECK_FALSE(s.due(first_day_of_year(-9990) + 1));
}

TEST_CASE("snapshots: lookup at or before a day, disk and memory") {
    SessionDir dir;
    for (auto path : {std::filesystem::path{}, dir.path()}) {
        SnapshotStore s(path, 10);
        for (int y = 0; y < 100; y += 10)
            s.add(Snapshot{first_day_of_year(y), {{"pop", {float(y), 1.0f}}}});
        CHECK(s.at_or_before(first_day_of_year(0) - 1) == nullptr);
        auto snap = s.at_or_before(first_day_of_year(35));
        REQUIRE(snap);
        CHECK(snap->day == first_day_of_year(30));
        CHECK(snap->layers[0].values[0] == 30.0f);
    }
}
