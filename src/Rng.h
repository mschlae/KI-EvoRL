#pragma once
// ============================================================
//  Rng.h  -  Thread-lokaler Zufallszahlengenerator
//  Jeder Thread besitzt einen eigenen Generator, damit die
//  parallele Evaluation deterministisch reproduzierbar bleibt
//  (Common Random Numbers ueber set_local_seed()).
// ============================================================
#include <cstdint>
#include <random>

std::mt19937_64& local_rng();
void  set_local_seed(uint64_t seed);
float randFloat(float lo, float hi);
int   randInt(int lo, int hi);
