#pragma once
// A small fixed-size thread pool.
//
// parallel_for splits [0, n) into contiguous chunks. Each index is processed
// exactly once; the caller must make the work for one index independent of
// every other (read shared state, write only that index's own outputs, use
// counter-based Rng). Then results never depend on thread timing.
//
// For sums across indices, use parallel_reduce, which combines per-chunk
// results in chunk order, so floating-point totals are reproducible.

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace hsim {

class JobSystem {
public:
    // threads == 0 means "one per hardware thread".
    explicit JobSystem(unsigned threads = 0);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    unsigned thread_count() const { return static_cast<unsigned>(workers_.size()) + 1; }

    // Calls fn(begin, end) for disjoint chunks covering [0, n). Blocks until done.
    void parallel_for(std::size_t n, const std::function<void(std::size_t, std::size_t)>& fn,
                      std::size_t min_chunk = 256);

    // Deterministic reduction: the chunk layout depends only on n and
    // `chunk`, never on the number of threads.
    template <class T, class F, class Combine>
    T parallel_reduce(std::size_t n, std::size_t chunk, T init, F&& map_chunk, Combine&& combine) {
        if (n == 0) return init;
        std::size_t chunks = (n + chunk - 1) / chunk;
        std::vector<T> partial(chunks, init);
        parallel_for(chunks, [&](std::size_t b, std::size_t e) {
            for (std::size_t c = b; c < e; ++c) {
                std::size_t lo = c * chunk, hi = lo + chunk < n ? lo + chunk : n;
                partial[c] = map_chunk(lo, hi);
            }
        }, 1);
        T total = init;
        for (auto& p : partial) total = combine(total, p);
        return total;
    }

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;

    // Current batch.
    const std::function<void(std::size_t, std::size_t)>* fn_ = nullptr;
    std::size_t n_ = 0, chunk_ = 0, next_ = 0, pending_ = 0;
    std::uint64_t batch_ = 0;
    bool stop_ = false;
};

}  // namespace hsim
