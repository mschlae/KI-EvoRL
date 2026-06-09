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
#include <vector>

class TrainingMonitor;

class Evolution {
public:
    std::vector<Brain>  population;
    std::vector<Brain>  speciesRepresentatives;
    std::vector<float>  speciesFitness;
    Brain               globalBest, globalBestBase;
    std::vector<Brain>  hallOfFame;
    std::vector<std::array<float, 36>> noveltyArchive;
    float  bestEverBase = -1e30f;
    float  mutationStrength;
    int    generation;
    int    stagnationGenCount, stagnationGenCount2;
    bool   boostActive, boost2Active;
    int    earlyStopLimit = 600;

    Evolution();

    // Optionaler Live-Monitor fuer das Web-Dashboard.
    void setMonitor(TrainingMonitor* m) { monitor_ = m; }

    float computeBaseFitness(const World& w);
    float evaluate(Brain& brain, bool visualize = false, bool recordNovelty = false,
        const uint64_t* seeds = nullptr);
    void  speciate();
    void  computeNoveltyBonus();
    float measureDiversity();
    void  updateHallOfFame(const Brain& c);
    int   tournamentSelect(const std::vector<int>& idxs);
    void  train();

    // Zeichnet eine Episode eines Agenten auf einer festen Karte als JSON-Replay auf.
    std::string recordReplay(Brain brain, uint64_t seed, int genLabel);

private:
    TrainingMonitor* monitor_ = nullptr;
};
