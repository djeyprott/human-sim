#include <doctest/doctest.h>

#include "hsim/ids.hpp"

using namespace hsim;

namespace {
struct ThingTag {
    static constexpr EntityKind kind = EntityKind::Person;
};
}  // namespace

TEST_CASE("ids: a reused slot gets a new generation") {
    IdAllocator<ThingTag> ids;
    auto a = ids.create();
    auto b = ids.create();
    CHECK(ids.alive(a));
    CHECK(ids.alive(b));
    CHECK(ids.alive_count() == 2);

    ids.destroy(a);
    CHECK_FALSE(ids.alive(a));
    auto c = ids.create();
    CHECK(c.index == a.index);         // slot reused
    CHECK(c.generation != a.generation);
    CHECK_FALSE(ids.alive(a));         // the old id stays dead
    CHECK(ids.alive(c));
    CHECK(ids.alive_count() == 2);
}

TEST_CASE("ids: entity refs keep their kind") {
    IdAllocator<ThingTag> ids;
    auto a = ids.create();
    EntityRef r = ref(a);
    CHECK(r.kind == EntityKind::Person);
    CHECK(r.index == a.index);
    CHECK(r.valid());
    CHECK_FALSE(EntityRef{}.valid());
}
