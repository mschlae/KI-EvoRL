#pragma once
// ============================================================
//  Brain.h  -  Rekurrentes GRU-Netz (das "Gehirn" eines Agenten)
//  Besitzt eine selbstadaptive Mutationsstaerke (sigma) und
//  einen 36-dimensionalen Verhaltensdeskriptor fuer Novelty Search.
//
//  ACHTUNG: Die Reihenfolge/Typen der Member sind Teil des alten
//  Modellformats "BRN1" (rohes Struct-Abbild). Aenderungen am
//  Layout machen aeltere gespeicherte Modelle unlesbar.
// ============================================================
#include "Config.h"
#include "Rng.h"
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
    float behavior[BEHAVIOR_DIM] = {}; // Besuchsanteil je 8x8-Zone
    int   speciesId = -1, stagnationCounter = 0;
    float sigma;

    struct NoInit {};           // Marker: Gewichte NICHT zufaellig fuellen (sparen den RNG-Aufwand)

    Brain();                    // zufaellig initialisiert
    explicit Brain(NoInit);     // Gewichte genullt, wird vom Aufrufer befuellt

    void  randomize();
    void  resetHidden();
    // Hotspot des Programms (~90 % der Rechenzeit).
    void  forward(const std::array<float, INPUTS>& inp, float logits[OUTPUTS]);
    // Aktion aus rohen Logits waehlen. `blockedMask` (Bit i = Aktion i gesperrt) schliesst
    // Zuege aus, solange noch mindestens einer frei ist. temperature >= ~0.001: Softmax-
    // Ziehen mit logits/temperature; darunter: greedy (hoechstes Logit, kein Zufall).
    static int sampleAction(float logits[OUTPUTS], Rng& rng, float temperature = 1.0f,
        uint8_t blockedMask = 0);
    // forward + sampleAction (nutzt den Hidden-State des Brains).
    int   selectAction(const std::array<float, INPUTS>& inp, Rng& rng, float temperature = 1.0f,
        uint8_t blockedMask = 0);
    void  mutate(float globalStrength, float forceSigma = -1.0f);
    float similarity(const Brain& o) const;

    static Brain crossover(const Brain& a, const Brain& b);
};
