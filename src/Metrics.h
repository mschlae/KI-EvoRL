#pragma once
// ============================================================
//  Metrics.h  -  Kennzahlen eines Benchmark-Laufs
//  Feste Karten + greedy Politik -> ueber das Training hinweg
//  vergleichbar (anders als die Fitness, die pro Generation
//  auf neuen Karten gemessen wird).
// ============================================================
struct BenchResult {
    int   gen = 0;
    int   episodes = 0;
    float avgFood = 0;          // Futter pro Episode
    float avgSteps = 0;         // Schritte pro Episode
    float stepsPerFood = 0;     // niedriger = besser
    float collisions = 0;       // Wand-/Hindernis-Kollisionen pro Episode
    float detour = 0;           // Umwegfaktor: gelaufene Schritte / kuerzester Weg (1.0 = perfekt)
    float survived = 0;         // Anteil der Episoden, die bis zum Schrittlimit ueberlebt wurden
};
