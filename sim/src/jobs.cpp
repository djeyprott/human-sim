#include "hsim/jobs.hpp"

#include <algorithm>

namespace hsim {

JobSystem::JobSystem(unsigned threads) {
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned i = 1; i < threads; ++i) workers_.emplace_back([this] { worker_loop(); });
}

JobSystem::~JobSystem() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    for (auto& t : workers_) t.join();
}

void JobSystem::parallel_for(std::size_t n, const std::function<void(std::size_t, std::size_t)>& fn,
                             std::size_t min_chunk) {
    if (n == 0) return;
    min_chunk = std::max<std::size_t>(1, min_chunk);
    if (workers_.empty() || n <= min_chunk) {
        fn(0, n);
        return;
    }
    std::size_t chunk = std::max(min_chunk, n / (thread_count() * 4) + 1);

    std::unique_lock lock(mutex_);
    fn_ = &fn;
    n_ = n;
    chunk_ = chunk;
    next_ = 0;
    pending_ = (n + chunk - 1) / chunk;
    ++batch_;
    wake_.notify_all();

    // The calling thread works too.
    while (next_ < n_) {
        std::size_t b = next_;
        next_ = std::min(n_, b + chunk_);
        std::size_t e = next_;
        lock.unlock();
        fn(b, e);
        lock.lock();
        --pending_;
    }
    done_.wait(lock, [this] { return pending_ == 0; });
    fn_ = nullptr;
}

void JobSystem::worker_loop() {
    std::unique_lock lock(mutex_);
    std::uint64_t seen = 0;
    for (;;) {
        wake_.wait(lock, [&] { return stop_ || (fn_ && batch_ != seen && next_ < n_); });
        if (stop_) return;
        while (fn_ && next_ < n_) {
            const auto* fn = fn_;
            std::size_t b = next_;
            next_ = std::min(n_, b + chunk_);
            std::size_t e = next_;
            lock.unlock();
            (*fn)(b, e);
            lock.lock();
            if (--pending_ == 0) done_.notify_all();
        }
        seen = batch_;
    }
}

}  // namespace hsim
