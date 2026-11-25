#include <doctest/doctest.h>

#include <set>

#include "hsim/rng.hpp"

using namespace hsim;

TEST_CASE("rng: same key, same value; different key, different value") {
    Rng a = Rng(42).with("system").with(7).with(1000);
    Rng b = Rng(42).with("system").with(7).with(1000);
    Rng c = Rng(42).with("system").with(8).with(1000);
    CHECK(a.bits() == b.bits());
    CHECK(a.bits() != c.bits());
    CHECK(a.bits(0) != a.bits(1));
}

TEST_CASE("rng: order of asking does not matter") {
    Rng base(99);
    double forward[100], backward[100];
    for (int i = 0; i < 100; ++i) forward[i] = base.with(i).uniform();
    for (int i = 99; i >= 0; --i) backward[i] = base.with(i).uniform();
    for (int i = 0; i < 100; ++i) CHECK(forward[i] == backward[i]);
}

TEST_CASE("rng: uniform and range stay in bounds and look uniform") {
    Rng r(7);
    double sum = 0;
    int buckets[10] = {};
    const int n = 100000;
    for (int i = 0; i < n; ++i) {
        double u = r.uniform(static_cast<std::uint64_t>(i));
        REQUIRE(u >= 0.0);
        REQUIRE(u < 1.0);
        sum += u;
        auto k = r.range(0, 9, static_cast<std::uint64_t>(i) + n);
        REQUIRE(k >= 0);
        REQUIRE(k <= 9);
        ++buckets[k];
    }
    CHECK(sum / n == doctest::Approx(0.5).epsilon(0.01));
    for (int b : buckets) CHECK(b == doctest::Approx(n / 10).epsilon(0.05));
}

TEST_CASE("rng: normal has roughly the right mean and spread") {
    Rng r(3);
    double sum = 0, sq = 0;
    const int n = 50000;
    for (int i = 0; i < n; ++i) {
        double x = r.normal(10.0, 2.0, static_cast<std::uint64_t>(i));
        sum += x;
        sq += x * x;
    }
    double mean = sum / n;
    double var = sq / n - mean * mean;
    CHECK(mean == doctest::Approx(10.0).epsilon(0.01));
    CHECK(var == doctest::Approx(4.0).epsilon(0.05));
}

TEST_CASE("rng: tags differ") {
    std::set<std::uint64_t> seen{tag("climate"), tag("population"), tag("figures"), tag("wanderers")};
    CHECK(seen.size() == 4);
}
