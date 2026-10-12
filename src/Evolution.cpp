// ============================================================
//  Evolution.cpp
//
//  Ablauf einer Generation:
//    1. Evaluation (parallel, Common Random Numbers)
//    2. Bestes merken / Hall of Fame
//    3. Speziation (adaptive Schwelle) + Novelty-Bonus
//    4. Sortieren, Stagnationspruefung, Diversitaet
//    5. Dashboard-Update
//    6. Reproduktion (Eliten, Hall of Fame, Arten, Injektionen)
// ============================================================
#include "Evolution.h"
#include "MathUtils.h"
#include "Replay.h"
#include "Rng.h"
#include "TrainingMonitor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <thread>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#else
static int omp_get_max_threads() { return 1; }
#endif

using namespace std;

namespace {
    constexpr float kNegInf = -1e30f;
}

Evolution::Evolution(const EvolutionOptions& opts)
    : mutationStrength(MUTATION_STRENGTH_INIT), opts_(opts) {
    if (opts_.seed) set_local_seed(opts_.seed);
    population.resize(POPULATION_SIZE);
    globalBest.baseFitness = globalBestBase.baseFitness = kNegInf;
    if (opts_.verbose) {
        cout << "Evolution: Population " << POPULATION_SIZE << ", Generationen " << opts_.generations
            << ", INPUTS=" << INPUTS << ", HIDDEN=" << HIDDEN
            << ", Threads=" << omp_get_max_threads() << "\n";
    }
}

// --------------------------------------------------------
//  FITNESS: monotone Food-Belohnung, manipulationssicher.
//  Nutzt die in der World eingefrorene Konfiguration.
// --------------------------------------------------------
float Evolution::computeBaseFitness(const World& w) const {
    const RuntimeConfig& c = w.cfg;
    float visitedFrac = w.visitedCount / float((GRID_W - 2) * (GRID_H - 2));

    // Food-Hauptsignal: streng monoton steigend, leicht abnehmend
    float baseUnit = c.foodReward > 0 ? c.foodReward : 60.0f;
    float foodBonus;
    if (w.foodEaten <= 20) foodBonus = w.foodEaten * baseUnit;
    else                   foodBonus = 20 * baseUnit + (w.foodEaten - 20) * (baseUnit * 0.5f);

    // Effizienz NUR als Tiebreaker, hart an tatsaechliche Leistung gekoppelt.
    // Gating (>=6 Food UND >=80 Schritte) verhindert den "1-Food-dann-sterben"-Exploit.
    float efficiencyBonus = 0.0f;
    if (w.foodEaten >= 6 && w.steps >= 80) {
        float foodRate = float(w.foodEaten) / w.steps;
        efficiencyBonus = min(foodRate, 0.2f) * w.foodEaten * 40.0f * c.efficiencyBonus;
    }

    // Erkundung: nachrangig
    float exploreBonus = visitedFrac * (c.exploreBonus > 0 ? c.exploreBonus * 750.0f : 150.0f);

    // Kleiner Start-Lerngradient (gedeckelt -> kein Dauerwandern)
    float survivalSeed = min(w.steps, 300) * 0.02f;

    // totalReward (Shaping) nur gedaempft einfliessen lassen.
    return c.rewardWeight * w.totalReward + foodBonus + efficiencyBonus + exploreBonus + survivalSeed;
}

float Evolution::evaluate(Brain& brain, bool visualize, const uint64_t* seeds) {
    const int reps = visualize ? 1 : EVAL_REPEATS;
    float totBase = 0; int totSteps = 0, totFood = 0;
    float zoneAcc[BEHAVIOR_DIM] = {};

    for (int r = 0; r < reps; r++) {
        // Common Random Numbers: identische Karte fuer alle Individuen dieser Generation
        if (seeds) set_local_seed(seeds[r]);
        World world(difficulty_, rcfg_);
        world.earlyStopLimit = earlyStopLimit;
        brain.resetHidden();
        while (world.alive && world.steps < MAX_EPISODE_STEPS) {
            auto sens = world.getSensors();
            int act = brain.selectAction(sens, world.rng);
            world.executeAction(act);
            if (visualize) {
                cout << "\033[2J\033[1;1H";
                cout << "=== BESTER AGENT ===\n";
                cout << "Step:" << world.steps << "  Energy:" << world.energy
                    << "  Food:" << world.foodEaten << "  Reward:" << world.totalReward << "\n";
                static const char* as[] = { "<", "->", "^", "v" };
                cout << "Action:" << as[act] << "\n";
                world.display();
                this_thread::sleep_for(chrono::milliseconds(25));
            }
        }
        totBase += computeBaseFitness(world);
        totSteps += world.steps; totFood += world.foodEaten;
        for (int zy = 0; zy < ZONE_GRID; zy++)
            for (int zx = 0; zx < ZONE_GRID; zx++)
                if (world.visitedZones[zy][zx]) zoneAcc[zy * ZONE_GRID + zx] += 1.0f;
    }
    brain.baseFitness = totBase / reps;
    brain.fitness = brain.baseFitness;
    brain.steps = totSteps / reps;
    brain.food = totFood / reps;
    for (int z = 0; z < BEHAVIOR_DIM; z++) brain.behavior[z] = zoneAcc[z] / reps;
    return brain.fitness;
}

// --------------------------------------------------------
//  Speziation. Die Distanzschwelle wird je Generation so
//  nachgefuehrt, dass etwa SPECIES_TARGET Arten entstehen
//  (zu wenige Arten -> strenger, zu viele -> lockerer).
// --------------------------------------------------------
void Evolution::speciate() {
    const int P = (int)population.size();
    vector<int> reps;                         // Index des Repraesentanten je Art
    reps.reserve(16);
    for (auto& ind : population) ind.speciesId = -1;

    for (int i = 0; i < P; i++) {
        int bestMatch = -1; float bestSim = speciesThreshold;
        for (int s = 0; s < (int)reps.size(); s++) {
            float sim = population[i].similarity(population[reps[s]]);
            if (sim > bestSim) { bestSim = sim; bestMatch = s; }
        }
        if (bestMatch == -1) { bestMatch = (int)reps.size(); reps.push_back(i); }
        population[i].speciesId = bestMatch;
    }
    numSpecies = (int)reps.size();

    // Die Artenzahl reagiert sehr empfindlich auf die Schwelle (die Distanzen liegen eng
    // beieinander): sanft nachfuehren, bei deutlichem Ueberschuss aber schneller lockern.
    float dist = 1.0f - speciesThreshold;
    if (numSpecies < SPECIES_TARGET)          dist *= (1.0f - SPECIES_THRESHOLD_ADAPT);
    else if (numSpecies > 2 * SPECIES_TARGET) dist *= (1.0f + 3 * SPECIES_THRESHOLD_ADAPT);
    else if (numSpecies > SPECIES_TARGET)     dist *= (1.0f + SPECIES_THRESHOLD_ADAPT);
    speciesThreshold = max(SPECIES_THRESHOLD_MIN, min(SPECIES_THRESHOLD_MAX, 1.0f - dist));
}

// --------------------------------------------------------
//  Novelty: parallel + nth_element statt Vollsortierung
// --------------------------------------------------------
void Evolution::computeNoveltyBonus() {
    if (generation < NOVELTY_START_GEN) return;
    float prog = min(1.0f, float(generation) / NOVELTY_RAMP_GENS);
    float w = NOVELTY_WEIGHT_START - prog * (NOVELTY_WEIGHT_START - NOVELTY_WEIGHT_END);
    const int k = NOVELTY_K;
    const int P = (int)population.size();
    const int A = (int)noveltyArchive.size();
    constexpr int D = BEHAVIOR_DIM;

    float meanBase = 0;
    for (auto& ind : population) meanBase += fabsf(ind.baseFitness);
    meanBase /= max(1, P);
    float scale = (meanBase > 1.0f ? meanBase : 1.0f);

    // Verhaltensvektoren kompakt zusammenkopieren (Cache-freundlich).
    vector<float> beh((size_t)(P + A) * D);
    for (int i = 0; i < P; i++)
        copy(population[i].behavior, population[i].behavior + D, beh.begin() + (size_t)i * D);
    for (int a = 0; a < A; a++)
        copy(noveltyArchive[a].begin(), noveltyArchive[a].end(), beh.begin() + (size_t)(P + a) * D);

    auto bdist2 = [&](int i, int j) {       // quadrierte Distanz reicht fuer die Auswahl
        const float* a = &beh[(size_t)i * D];
        const float* b = &beh[(size_t)j * D];
        float s = 0;
        for (int z = 0; z < D; z++) { float d = a[z] - b[z]; s += d * d; }
        return s;
    };

    vector<float> nov(P, 0.0f);
#pragma omp parallel
    {
        vector<float> dists;                 // pro Thread wiederverwendet
        dists.reserve((size_t)P + A);
#pragma omp for schedule(dynamic, 8)
        for (int i = 0; i < P; i++) {
            dists.clear();
            for (int j = 0; j < P; j++) if (i != j) dists.push_back(bdist2(i, j));
            for (int a = 0; a < A; a++) dists.push_back(bdist2(i, P + a));

            int lim = min(k, (int)dists.size());
            if (lim > 0 && lim < (int)dists.size())
                nth_element(dists.begin(), dists.begin() + lim, dists.end());

            float avg = 0;
            for (int d = 0; d < lim; d++) avg += sqrtf(dists[d]);
            avg /= (lim > 0 ? (float)lim : 1.0f);
            nov[i] = avg;
            population[i].novelty = avg;
        }
    }

    for (int i = 0; i < P; i++)
        population[i].fitness += w * nov[i] * scale * 0.25f;

    int mostNovel = (int)(max_element(nov.begin(), nov.end()) - nov.begin());
    array<float, D> bc;
    copy(population[mostNovel].behavior, population[mostNovel].behavior + D, bc.begin());
    noveltyArchive.push_back(bc);
    if ((int)noveltyArchive.size() > NOVELTY_ARCHIVE_MAX) noveltyArchive.erase(noveltyArchive.begin());
}

float Evolution::measureDiversity() {
    int ss = min(30, (int)population.size());
    float tot = 0; int cnt = 0;
#pragma omp parallel for reduction(+:tot,cnt) schedule(dynamic)
    for (int i = 0; i < ss; i++) {
        for (int j = i + 1; j < ss; j++) {
            tot += (1.0f - population[i].similarity(population[j]));
            cnt++;
        }
    }
    return cnt > 0 ? tot / cnt : 0.0f;
}

void Evolution::updateHallOfFame(const Brain& c) {
    if ((int)hallOfFame.size() < HALL_OF_FAME_SIZE) {
        hallOfFame.push_back(c);
    }
    else {
        auto worst = min_element(hallOfFame.begin(), hallOfFame.end(),
            [](const Brain& a, const Brain& b) { return a.baseFitness < b.baseFitness; });
        if (c.baseFitness > worst->baseFitness) *worst = c;
    }
}

int Evolution::tournamentSelect(const vector<int>& idxs) {
    if (idxs.empty()) return randInt(0, (int)population.size() - 1);
    int best = idxs[randInt(0, (int)idxs.size() - 1)];
    for (int i = 1; i < TOURNAMENT_SIZE; i++) {
        int r = idxs[randInt(0, (int)idxs.size() - 1)];
        if (population[r].fitness > population[best].fitness) best = r;
    }
    return best;
}

// Neue Schwierigkeitsstufe: Rekorde aus leichteren Welten waeren unerreichbar und wuerden
// Stagnations-Boosts ausloesen bzw. die Hall of Fame blockieren.
void Evolution::resetRecords() {
    bestEverBase = kNegInf;
    stagnationGenCount = stagnationGenCount2 = 0;
    boostActive = boost2Active = false;
    hallOfFame.clear();
    globalBest.baseFitness = globalBestBase.baseFitness = kNegInf;
}

// Globales Bestes + Hall of Fame. Die HOF-Schwelle wird gecacht, damit die teure
// Brain-Kopie nur fuer echte Kandidaten faellt.
void Evolution::updateBest() {
    auto hofMin = [&] {
        if ((int)hallOfFame.size() < HALL_OF_FAME_SIZE) return kNegInf;
        float m = 1e30f;
        for (auto& h : hallOfFame) m = min(m, h.baseFitness);
        return m;
    };
    float threshold = hofMin();
    for (auto& ind : population) {
        if (ind.baseFitness > globalBestBase.baseFitness) globalBestBase = ind;
        if (ind.baseFitness > globalBest.baseFitness)     globalBest = ind;
        if (ind.baseFitness > threshold) { updateHallOfFame(ind); threshold = hofMin(); }
    }
}

// Absteigend nach Fitness. Es werden Indizes sortiert und die grossen Brains
// nur einmal umkopiert (std::sort wuerde sie O(n log n)-mal bewegen).
void Evolution::sortPopulation() {
    const int P = (int)population.size();
    vector<int> order(P);
    iota(order.begin(), order.end(), 0);
    stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return population[a].fitness > population[b].fitness;
    });
    vector<Brain> sorted;
    sorted.reserve(P);
    for (int idx : order) sorted.push_back(move(population[idx]));
    population = move(sorted);
}

// Stagnation gegen die beste je erreichte SAUBERE baseFitness (ohne Novelty).
void Evolution::updateStagnation() {
    float genBestBase = kNegInf;
    for (auto& ind : population) genBestBase = max(genBestBase, ind.baseFitness);
    if (genBestBase > bestEverBase + 1e-3f) {
        bestEverBase = genBestBase;
        stagnationGenCount = 0; stagnationGenCount2 = 0;
    }
    else {
        stagnationGenCount++; stagnationGenCount2++;
        if (stagnationGenCount >= STAGNATION_LIMIT) { boostActive = true;  stagnationGenCount = 0; }
        if (stagnationGenCount2 >= STAGNATION_LIMIT2) { boost2Active = true; stagnationGenCount2 = 0; }
    }
}

void Evolution::reproduce(bool inject) {
    const int P = POPULATION_SIZE;
    vector<Brain> next;
    next.reserve(P);

    // Eliten. Der Beste bleibt unveraendert erhalten (echter Elitismus), die uebrigen
    // werden sanft mutiert. Brains sind ~165 KB gross -> direkt im Heap-Vektor arbeiten.
    next.push_back(population[0]);
    for (int i = 1; i < ELITE_COUNT && (int)next.size() < P; i++) {
        next.push_back(population[i]);
        next.back().mutate(mutationStrength, ELITE_MUTATION_SIGMA);
    }
    for (auto& h : hallOfFame) {
        if ((int)next.size() >= P) break;
        next.push_back(h);
        next.back().mutate(mutationStrength, ELITE_MUTATION_SIGMA);
    }

    const int numSp = max(1, numSpecies);
    vector<vector<int>> spMem(numSp);
    for (int i = 0; i < P; i++) spMem[population[i].speciesId].push_back(i);

    // Fitness-Verschiebung: Die Fitness kann negativ sein (Wandstrafen frueh im Training).
    // Ohne Verschiebung wuerde die Verteilung der Nachkommen umkippen und schwache Arten beguenstigen.
    float minFit = 1e30f;
    for (auto& ind : population) minFit = min(minFit, ind.fitness);
    vector<float> spScore(numSp, 0.0f);       // mittlere verschobene Fitness je Art
    for (int s = 0; s < numSp; s++) {
        for (int i : spMem[s]) spScore[s] += population[i].fitness - minFit + 1.0f;
        if (!spMem[s].empty()) spScore[s] /= (float)spMem[s].size();
    }
    float totScore = accumulate(spScore.begin(), spScore.end(), 0.0f);
    if (totScore <= 0) totScore = 1;

    int rem = P - (int)next.size();
    int injCnt = inject ? min(rem, max(6, rem / 10)) : 0;
    rem -= injCnt;

    vector<int> offs(numSp, 0);
    for (int s = 0; s < numSp && rem > 0; s++) if (!spMem[s].empty()) { offs[s] = 1; rem--; }
    const int share = rem;
    for (int s = 0; s < numSp && rem > 0; s++) {
        int add = min(rem, (int)(spScore[s] / totScore * share));
        offs[s] += add; rem -= add;
    }
    if (rem > 0) {
        int bs = (int)(max_element(spScore.begin(), spScore.end()) - spScore.begin());
        offs[bs] += rem;
    }

    for (int s = 0; s < numSp && (int)next.size() < P; s++) {
        auto& mem = spMem[s];
        if (mem.empty()) continue;
        for (int n = 0; n < offs[s] && (int)next.size() < P; n++) {
            int p1;
            if (rand01() < CROSS_SPECIES_MATING_CHANCE && numSp > 1) {
                int os = s; while (os == s) os = randInt(0, numSp - 1);
                p1 = spMem[os].empty() ? tournamentSelect(mem) : tournamentSelect(spMem[os]);
            }
            else p1 = tournamentSelect(mem);
            int p2 = tournamentSelect(mem);
            next.push_back(Brain::crossover(population[p1], population[p2]));
            next.back().mutate(mutationStrength);
        }
    }

    // Zufalls-Injektionen (frisches Blut gegen Diversitaetsverlust)
    for (int i = 0; i < injCnt && (int)next.size() < P; i++) next.emplace_back();
    // Auffuellen, falls Arten leer waren
    while ((int)next.size() < P) next.push_back(population[randInt(0, P - 1)]);
    population = move(next);
}

void Evolution::reportGeneration(float diversity, bool inject, const vector<uint64_t>& genSeeds) {
    const int P = POPULATION_SIZE;
    float avgFit = 0; int avgStep = 0, avgFood = 0;
    for (auto& ind : population) { avgFit += ind.fitness; avgStep += ind.steps; avgFood += ind.food; }
    avgFit /= P; avgStep /= P; avgFood /= P;
    if (avgFood < 1) avgFood = 1;
    const Brain& best = population[0];

    if (opts_.verbose) {
        cout << "Gen " << generation + 1 << "/" << opts_.generations
            << " | Score=" << (avgFit + avgFood * 5 + avgStep) / 100
            << " | Best=" << best.fitness
            << " | AvgFit=" << avgFit
            << " | AvgFood=" << avgFood
            << " | AvgSteps=" << avgStep
            << " | BestFood=" << best.food
            << " | BestStep=" << best.steps
            << " | SPF=" << (float)avgStep / (float)avgFood
            << " | Mut=" << mutationStrength
            << " | Div=" << diversity
            << " | Spec=" << numSpecies;
        if (inject) cout << " [INJECT]";
        cout << "\n";
    }

    if (!monitor_) return;
    // SPF = Schritte pro Nahrung (niedriger = besser), zur Anzeige gedeckelt.
    auto cap = [](float v) { return v > 400.0f ? 400.0f : v; };
    GenRecord rec;
    rec.gen = generation + 1;
    rec.best = best.fitness;
    rec.avg = avgFit;
    rec.avgFood = (float)avgFood;
    rec.bestFood = best.food;
    rec.bestSpf = cap(best.food > 0 ? (float)best.steps / (float)best.food : (float)best.steps);
    rec.avgSpf = (float)avgStep / (float)avgFood;
    rec.mutation = mutationStrength;
    rec.diversity = diversity;
    rec.species = numSpecies;

    // Replay simuliert eine komplette Episode -> nur alle REPLAY_INTERVAL Generationen.
    if (generation % REPLAY_INTERVAL == 0)
        monitor_->setReplay(recordReplay(best, genSeeds[0], generation + 1));
    monitor_->update(rec, globalBest.baseFitness, globalBest.food, difficulty_, inject, opts_.generations);
    // Aktuell bestes, sauberes Brain fuer Speichern/Vorfuehren/Playground.
    monitor_->setBestBrain(globalBestBase, generation + 1);
}

void Evolution::train() {
    if (monitor_) monitor_->setRunning(true);
    const int nthreads = EVAL_THREADS > 0 ? EVAL_THREADS : omp_get_max_threads();
    // Eigener Strom fuer Reproduktion & Co. (die Evaluation re-seedet die Threads)
    uint64_t masterSeed = opts_.seed ? opts_.seed : local_rng().next();
    bool interrupted = false;

    for (generation = 0; generation < opts_.generations; generation++) {
        if (monitor_ && monitor_->stopRequested()) {
            cout << "\n[Training unterbrochen bei Generation " << (generation + 1) << "]\n";
            interrupted = true;
            break;
        }

        // Vom Dashboard vorgemerktes Modell uebernehmen (ersetzt das schwaechste Individuum,
        // wird in dieser Generation normal mitbewertet).
        if (monitor_) {
            Brain loaded{ Brain::NoInit{} };
            if (monitor_->takePendingLoad(loaded) && !population.empty())
                population.back() = loaded;
        }

        const float progress = float(generation) / float(max(1, opts_.generations));

        // Parameter dieser Generation einfrieren (Dashboard-Aenderungen wirken ab der naechsten)
        rcfg_ = getRuntimeConfig();
        // Curriculum in Stufen von 0.1. Fitnesswerte sind nur innerhalb einer Stufe vergleichbar:
        // bei jedem Stufenwechsel werden die Rekorde (Bestwerte, Hall of Fame, Stagnation) zurueckgesetzt.
        float level = floorf(10.0f * min(1.0f, float(generation) / CURRICULUM_GENS)) / 10.0f;
        if (generation > 0 && level != difficulty_) resetRecords();
        difficulty_ = level;
        earlyStopLimit = 500 + int(progress * 900);                     // dynamisches Early-Stopping

        mutationStrength = max(MUTATION_STRENGTH_MIN,
            rcfg_.mutation * (1 - progress) + MUTATION_STRENGTH_FINAL * progress);
        if (boostActive) { mutationStrength = min(1.0f, mutationStrength * STAGNATION_BOOST_FACTOR); boostActive = false; }
        if (boost2Active) { mutationStrength = min(1.0f, mutationStrength * STAGNATION_BOOST2); boost2Active = false; }

        // Evaluation (parallel) - Common Random Numbers pro Generation
        vector<uint64_t> genSeeds(EVAL_REPEATS);
        for (int r = 0; r < EVAL_REPEATS; r++)
            genSeeds[r] = (uint64_t)generation * 2654435761ull + (uint64_t)r * 40503ull + 1234567ull + masterSeed;
#pragma omp parallel for num_threads(nthreads) schedule(dynamic)
        for (int i = 0; i < POPULATION_SIZE; i++)
            evaluate(population[i], false, genSeeds.data());

        updateBest();
        speciate();
        computeNoveltyBonus();
        sortPopulation();
        updateStagnation();

        const float diversity = measureDiversity();
        const bool inject = boost2Active || (diversity < DIVERSITY_INJECT_THRESHOLD);

        reportGeneration(diversity, inject, genSeeds);

        // Der Hauptthread war Teil der Evaluation (und Replay-Aufzeichnung) und wurde dabei
        // re-seedet -> fuer die Reproduktion wieder einen definierten Strom setzen.
        set_local_seed(masterSeed ^ (0x9E3779B97F4A7C15ull * (uint64_t)(generation + 1)));
        reproduce(inject);
    }

    if (monitor_) monitor_->setFinished();

    if (opts_.demo && !interrupted) {
        cout << "\n=== DEMO des besten Basis-Agenten ===\n";
        set_local_seed(42766);
        difficulty_ = 1.0f;
        evaluate(globalBestBase, true);
    }
}

// --------------------------------------------------------
//  Replay des besten Agenten auf einer festen Karte
// --------------------------------------------------------
string Evolution::recordReplay(const Brain& brain, uint64_t seed, int genLabel) {
    set_local_seed(seed);
    World world(difficulty_, rcfg_);
    world.earlyStopLimit = earlyStopLimit;
    auto copy = make_unique<Brain>(brain);   // forward() veraendert den Hidden-State
    return buildReplayJson(world, *copy, genLabel, MAX_EPISODE_STEPS);
}
