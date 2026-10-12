#pragma once
// ============================================================
//  Rng.h  -  Schneller, thread-lokaler Zufallszahlengenerator
//  Jeder Thread besitzt einen eigenen xoshiro256++-Generator.
//  Mit set_local_seed() laesst sich ein Thread deterministisch
//  seeden (Common Random Numbers bei der Evaluation).
// ============================================================
#include <cstdint>

class Rng {
public:
    Rng();                               // zufaellig (je Thread unterschiedlich) geseedet
    void     seed(uint64_t s);
    uint64_t next();
    float    uniform01() { return float(next() >> 40) * (1.0f / 16777216.0f); } // [0, 1)
    float    uniform(float lo, float hi) { return lo + (hi - lo) * uniform01(); }   // [lo, hi)
    int      integer(int lo, int hi);                                               // [lo, hi] inklusive

private:
    uint64_t s_[4];
};

Rng&  local_rng();                       // Generator des aufrufenden Threads
void  set_local_seed(uint64_t seed);
float randFloat(float lo, float hi);     // [lo, hi)
int   randInt(int lo, int hi);           // [lo, hi] inklusive
inline float rand01() { return local_rng().uniform01(); }
