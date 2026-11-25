#pragma once
// Counter-based random numbers.
//
// Instead of drawing from a shared stream (whose results depend on the order
// in which code asks for numbers), every random value is a pure function of a
// key: (world seed, system, entity, day, purpose, n). Two threads can update
// different entities in any order and the results are identical.
//
// Usage:
//   Rng rng = Rng(world.seed()).with(SystemId).with(entity.key()).with(today);
//   double x = rng.uniform(Purpose::Travel);

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace hsim {

// Strong 64-bit mixer (SplitMix64 finalizer). Good avalanche, cheap.
constexpr std::uint64_t mix64(std::uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

constexpr std::uint64_t hash_combine(std::uint64_t a, std::uint64_t b) {
    return mix64(a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2)));
}

// Hash of a string at compile time, for purpose/system tags.
constexpr std::uint64_t tag(const char* s) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    while (*s) {
        h ^= static_cast<unsigned char>(*s++);
        h *= 0x100000001B3ull;
    }
    return mix64(h);
}

class Rng {
public:
    constexpr explicit Rng(std::uint64_t key) : key_(mix64(key)) {}

    // Derive a new key by folding in more context.
    template <class T>
        requires std::is_integral_v<T> || std::is_enum_v<T>
    constexpr Rng with(T part) const {
        return Rng(key_, static_cast<std::uint64_t>(part));
    }
    constexpr Rng with(const char* name) const { return Rng(key_, tag(name)); }

    // The raw 64-bit value for sub-key `n` (draw number n under this key).
    constexpr std::uint64_t bits(std::uint64_t n = 0) const { return mix64(hash_combine(key_, n)); }

    // Uniform in [0, 1).
    double uniform(std::uint64_t n = 0) const { return static_cast<double>(bits(n) >> 11) * 0x1.0p-53; }

    // Uniform integer in [lo, hi] (inclusive).
    std::int64_t range(std::int64_t lo, std::int64_t hi, std::uint64_t n = 0) const {
        auto span = static_cast<std::uint64_t>(hi - lo) + 1;
        if (span == 0) return static_cast<std::int64_t>(bits(n));  // full range
        // Multiply-shift reduction: bias is negligible at 64 bits.
        return lo + static_cast<std::int64_t>(mul_hi(bits(n), span));
    }

    bool chance(double p, std::uint64_t n = 0) const { return uniform(n) < p; }

    // Standard normal via Box-Muller using two sub-draws.
    double normal(double mean, double stddev, std::uint64_t n = 0) const {
        double u1 = uniform(2 * n);
        double u2 = uniform(2 * n + 1);
        if (u1 < 1e-300) u1 = 1e-300;
        return mean + stddev * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }

    constexpr std::uint64_t key() const { return key_; }

private:
    constexpr Rng(std::uint64_t parent, std::uint64_t part) : key_(hash_combine(parent, part)) {}

    static std::uint64_t mul_hi(std::uint64_t a, std::uint64_t b) {
        // Portable 64x64 -> high 64 bits.
        std::uint64_t a_lo = a & 0xFFFFFFFFu, a_hi = a >> 32;
        std::uint64_t b_lo = b & 0xFFFFFFFFu, b_hi = b >> 32;
        std::uint64_t p0 = a_lo * b_lo;
        std::uint64_t p1 = a_lo * b_hi;
        std::uint64_t p2 = a_hi * b_lo;
        std::uint64_t p3 = a_hi * b_hi;
        std::uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFFu) + (p2 & 0xFFFFFFFFu);
        return p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    }

    std::uint64_t key_;
};

}  // namespace hsim
