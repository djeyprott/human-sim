#include <doctest/doctest.h>

#include <atomic>
#include <vector>

#include "hsim/jobs.hpp"
#include "hsim/rng.hpp"

using namespace hsim;

TEST_CASE("jobs: every index is processed exactly once") {
    JobSystem jobs(4);
    std::vector<std::atomic<int>> hits(10007);
    for (int round = 0; round < 20; ++round) {
        jobs.parallel_for(hits.size(), [&](std::size_t b, std::size_t e) {
            for (std::size_t i = b; i < e; ++i) hits[i].fetch_add(1);
        }, 64);
    }
    for (auto& h : hits) CHECK(h.load() == 20);
}

TEST_CASE("jobs: reductions give identical results for any thread count") {
    const std::size_t n = 200000;
    auto run = [&](unsigned threads) {
        JobSystem jobs(threads);
        return jobs.parallel_reduce(
            n, 1024, 0.0,
            [](std::size_t lo, std::size_t hi) {
                double s = 0;
                for (std::size_t i = lo; i < hi; ++i) s += Rng(5).with(i).uniform() * 1e-3;
                return s;
            },
            [](double a, double b) { return a + b; });
    };
    double one = run(1);
    CHECK(run(2) == one);   // bitwise equal, not approximately
    CHECK(run(8) == one);
}
