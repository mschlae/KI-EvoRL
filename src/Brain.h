#pragma once
// ============================================================
//  Brain.h  -  Rekurrentes GRU-Netz (das "Gehirn" eines Agenten)
//  Besitzt eine selbstadaptive Mutationsstaerke (sigma) und
//  einen 36-dimensionalen Verhaltensdeskriptor fuer Novelty Search.
// ============================================================
#include "Config.h"
#include <array>

struct Brain {
    static constexpr int IN_SIZE = INPUTS + HIDDEN;

    float W_z[HIDDEN][IN_SIZE];
    float W_r[HIDDEN][IN_SIZE];
    float W_h[HIDDEN][IN_SIZE];
    float W_out[OUTPUTS][HIDDEN];
    float bias_z[HIDDEN] = {};
    float bias_r[HIDDEN] = {};
    float bias_h[HIDDEN] = {};
    float bias_out[OUTPUTS] = {};
    float hidden_state[HIDDEN];

    float fitness = 0, baseFitness = 0;
    int   steps = 0, food = 0;
    float novelty = 0;
    float behavior[36] = {}; // Besuchsanteil je 6x6-Zone
    int   speciesId = -1, stagnationCounter = 0;
    float sigma;

    Brain();

    void  randomize();
    void  resetHidden();
    void  forward(const std::array<float, INPUTS>& inp, float logits[OUTPUTS]);
    void  mutate(float globalStrength, float forceSigma = -1.0f);
    float similarity(const Brain& o) const;

    static Brain crossover(const Brain& a, const Brain& b);
};
