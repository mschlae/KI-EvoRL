// ============================================================
//  Rng.cpp
// ============================================================
#include "Rng.h"

#include <atomic>
#include <chrono>
#include <random>

namespace {
    inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    uint64_t splitmix64(uint64_t& x) {
        uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // Prozessweite Basis + Zaehler: jeder Thread bekommt einen eigenen Startwert.
    uint64_t freshSeed() {
        static const uint64_t base = [] {
            std::random_device rd;
            uint64_t t = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
            return ((uint64_t)rd() << 32) ^ rd() ^ t;
        }();
        static std::atomic<uint64_t> counter{ 0 };
        uint64_t x = base + counter.fetch_add(1) * 0xD1B54A32D192ED03ull;
        return splitmix64(x);
    }
}

Rng::Rng() { seed(freshSeed()); }

void Rng::seed(uint64_t s) {
    for (auto& v : s_) v = splitmix64(s);
}

uint64_t Rng::next() {
    const uint64_t result = rotl(s_[0] + s_[3], 23) + s_[0];
    const uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
}

Rng& local_rng() {
    static thread_local Rng gen;
    return gen;
}

void set_local_seed(uint64_t seed) { local_rng().seed(seed); }

int Rng::integer(int lo, int hi) {
    if (hi <= lo) return lo;
    uint64_t range = (uint64_t)((int64_t)hi - (int64_t)lo) + 1;
    return lo + (int)(((next() >> 32) * range) >> 32);
}

float randFloat(float lo, float hi) { return local_rng().uniform(lo, hi); }
int   randInt(int lo, int hi) { return local_rng().integer(lo, hi); }
