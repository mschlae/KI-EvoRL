// ============================================================
//  Rng.cpp
// ============================================================
#include "Rng.h"
#include <functional>
#include <thread>

std::mt19937_64& local_rng() {
    static std::random_device rd;
    static thread_local std::mt19937_64 gen([&]() {
        auto seed = rd();
        seed ^= std::hash<std::thread::id>()(std::this_thread::get_id());
        return seed;
        }());
    return gen;
}

void set_local_seed(uint64_t seed) { local_rng().seed(seed); }

float randFloat(float lo, float hi) {
    std::uniform_real_distribution<float> d(lo, hi);
    return d(local_rng());
}

int randInt(int lo, int hi) {
    std::uniform_int_distribution<int> d(lo, hi);
    return d(local_rng());
}
