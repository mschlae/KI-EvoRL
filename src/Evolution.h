#pragma once
// ============================================================
//  Evolution.h  -  Genetischer Algorithmus mit Speziation,
//  Novelty Search, Hall of Fame, Stagnations-Boosts und
//  hack-sicherer Fitnessfunktion.
// ============================================================
#include "Brain.h"
#include "World.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class TrainingMonitor;

struct EvolutionOptions {
    int      generations = GENERATIONS;
    uint64_t seed = 0;        // 0 = zufaellig; sonst reproduzierbarer Lauf (unabhaengig von der Threadzahl)
    bool     demo = true;     // am Ende den besten Agenten in der Konsole vorfuehren
    bool     verbose = true;  // Zeile pro Generation ausgeben
};

class Evolution {
public:
    std::vector<Brain>  population;
    Brain               globalBest, globalBestBase;
    std::vector<Brain>  hallOfFame;
    std::vector<std::array<float, BEHAVIOR_DIM>> noveltyArchive;
    float  bestEverBase = -1e30f;
    float  mutationStrength;
    int    generation = 0;
    int    stagnationGenCount = 0, stagnationGenCount2 = 0;
    bool   boostActive = false, boost2Active = false;
    int    earlyStopLimit = 600;
    int    numSpecies = 0;
    float  speciesThreshold = SPECIES_SIMILARITY_THRESHOLD;  // adaptiv, siehe speciate()

    explicit Evolution(const EvolutionOptions& opts = EvolutionOptions());

    // Optionaler Live-Monitor fuer das Web-Dashboard.
    void setMonitor(TrainingMonitor* m) { monitor_ = m; }

    float computeBaseFitness(const World& w) const;
    // Bewertet ein Brain ueber EVAL_REPEATS Episoden (bzw. 1 bei `visualize`).
    // `seeds` (EVAL_REPEATS Stueck) liefert identische Karten fuer alle Individuen (CRN).
    float evaluate(Brain& brain, bool visualize = false, const uint64_t* seeds = nullptr);
    void  speciate();
    void  computeNoveltyBonus();
    float measureDiversity();
    void  updateHallOfFame(const Brain& c);
    int   tournamentSelect(const std::vector<int>& idxs);
    void  train();

    // Zeichnet eine Episode eines Agenten auf einer festen Karte als JSON-Replay auf.
    std::string recordReplay(const Brain& brain, uint64_t seed, int genLabel);

private:
    void  resetRecords();
    void  updateBest();
    void  sortPopulation();
    void  updateStagnation();
    void  reproduce(bool inject);
    void  reportGeneration(float diversity, bool inject, const std::vector<uint64_t>& genSeeds);

    EvolutionOptions opts_;
    RuntimeConfig    rcfg_;            // pro Generation eingefroren
    float            difficulty_ = 0;  // Curriculum der aktuellen Generation
    TrainingMonitor* monitor_ = nullptr;
};
