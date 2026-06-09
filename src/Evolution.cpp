// ============================================================
//  Evolution.cpp
// ============================================================
#include "Evolution.h"
#include "MathUtils.h"
#include "Rng.h"
#include "TrainingMonitor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <sstream>
#include <thread>

#include <omp.h>

using namespace std;

Evolution::Evolution()
    : mutationStrength(MUTATION_STRENGTH_INIT), generation(0),
    stagnationGenCount(0), stagnationGenCount2(0),
    boostActive(false), boost2Active(false) {
    population.resize(POPULATION_SIZE);
    globalBest.baseFitness = globalBestBase.baseFitness = -1e30f;
    cout << "Optimierte Evolution v4. Population: " << POPULATION_SIZE << "\n";
    cout << "INPUTS=" << INPUTS << " HIDDEN=" << HIDDEN << "\n";
}

// --------------------------------------------------------
//  FITNESS: monotone Food-Belohnung, manipulationssicher
// --------------------------------------------------------
float Evolution::computeBaseFitness(const World& w) {
    float visitedFrac = w.visited.count() / float((GRID_W - 2) * (GRID_H - 2));

    // Food-Hauptsignal: streng monoton steigend, leicht abnehmend
    float foodBonus;
    if (w.foodEaten <= 20) foodBonus = w.foodEaten * 60.0f;
    else                   foodBonus = 20 * 60.0f + (w.foodEaten - 20) * 30.0f;

    // Effizienz NUR als Tiebreaker, hart an tatsaechliche Leistung gekoppelt.
    // Gating (>=6 Food UND >=80 Schritte) verhindert den "1-Food-dann-sterben"-Exploit.
    float efficiencyBonus = 0.0f;
    if (w.foodEaten >= 6 && w.steps >= 80) {
        float foodRate = float(w.foodEaten) / w.steps;
        efficiencyBonus = min(foodRate, 0.2f) * w.foodEaten * 40.0f;
    }

    // Erkundung: nachrangig
    float exploreBonus = visitedFrac * 150.0f;

    // Kleiner Start-Lerngradient (gedeckelt -> kein Dauerwandern)
    float survivalSeed = min(w.steps, 300) * 0.02f;

    // totalReward (Shaping) nur gedaempft einfliessen lassen.
    return 0.3f * w.totalReward + foodBonus + efficiencyBonus + exploreBonus + survivalSeed;
}

float Evolution::evaluate(Brain& brain, bool visualize, bool recordNovelty,
    const uint64_t* seeds) {
    int reps = visualize ? 1 : EVAL_REPEATS;
    float totBase = 0; int totSteps = 0, totFood = 0;
    float zoneAcc[36] = {};
    for (int r = 0; r < reps; r++) {
        // Common Random Numbers: identische Karte fuer alle Individuen dieser Generation
        if (seeds) set_local_seed(seeds[r]);
        World world;
        world.earlyStopLimit = earlyStopLimit;
        brain.resetHidden();
        while (world.alive && world.steps < MAX_STEPS) {
            auto sens = world.getSensors();
            float logits[OUTPUTS];
            brain.forward(sens, logits);
            softmax_inplace(logits, OUTPUTS);
            float rv = randFloat(0, 1); int act = OUTPUTS - 1; float cum = 0;
            for (int i = 0; i < OUTPUTS; i++) { cum += logits[i]; if (rv < cum) { act = i; break; } }
            world.executeAction(act);
            if (visualize && r == 0) {
                cout << "\033[2J\033[1;1H";
                cout << "=== BESTER AGENT ===\n";
                cout << "Step:" << world.steps << "  Energy:" << world.energy
                    << "  Food:" << world.foodEaten << "  Reward:" << world.totalReward << "\n";
                const char* as[] = { "<", "->", "^", "v" };
                cout << "Action:" << as[act] << "\n";
                world.display();
                this_thread::sleep_for(chrono::milliseconds(25));
            }
        }
        totBase += computeBaseFitness(world);
        totSteps += world.steps; totFood += world.foodEaten;
        for (int zy = 0; zy < 6; zy++)
            for (int zx = 0; zx < 6; zx++)
                if (world.visitedZones[zy][zx]) zoneAcc[zy * 6 + zx] += 1.0f;
    }
    brain.baseFitness = totBase / reps;
    brain.fitness = brain.baseFitness;
    brain.steps = totSteps / reps;
    brain.food = totFood / reps;
    for (int z = 0; z < 36; z++) brain.behavior[z] = zoneAcc[z] / reps;
    return brain.fitness;
}

void Evolution::speciate() {
    speciesRepresentatives.clear();
    for (auto& ind : population) ind.speciesId = -1;
    for (auto& ind : population) {
        if (speciesRepresentatives.empty()) { ind.speciesId = 0; speciesRepresentatives.push_back(ind); continue; }
        int bestMatch = -1; float bestSim = SPECIES_SIMILARITY_THRESHOLD;
        for (int s = 0; s < (int)speciesRepresentatives.size(); s++) {
            float sim = ind.similarity(speciesRepresentatives[s]);
            if (sim > bestSim) { bestSim = sim; bestMatch = s; }
        }
        if (bestMatch != -1) ind.speciesId = bestMatch;
        else { ind.speciesId = (int)speciesRepresentatives.size(); speciesRepresentatives.push_back(ind); }
    }
    speciesFitness.assign(speciesRepresentatives.size(), 0);
    vector<int> cnt(speciesRepresentatives.size(), 0);
    for (auto& ind : population) { speciesFitness[ind.speciesId] += ind.fitness; cnt[ind.speciesId]++; }
    for (int i = 0; i < (int)speciesFitness.size(); i++)
        if (cnt[i] > 0) speciesFitness[i] /= cnt[i];
}

void Evolution::computeNoveltyBonus() {
    if (generation < NOVELTY_START_GEN) return;
    float prog = min(1.0f, float(generation) / 400.0f);
    float w = NOVELTY_WEIGHT_START - prog * (NOVELTY_WEIGHT_START - NOVELTY_WEIGHT_END);
    const int k = 15;
    int P = (int)population.size();

    float meanBase = 0;
    for (auto& ind : population) meanBase += fabsf(ind.baseFitness);
    meanBase /= max(1, P);
    float scale = (meanBase > 1.0f ? meanBase : 1.0f);

    auto bdist = [](const float* a, const float* b) {
        float s = 0; for (int z = 0; z < 36; z++) { float d = a[z] - b[z]; s += d * d; } return sqrtf(s);
        };

    vector<float> nov(P, 0.0f);
    for (int i = 0; i < P; i++) {
        vector<float> dists; dists.reserve(P - 1 + noveltyArchive.size());
        for (int j = 0; j < P; j++) {
            if (i == j) continue;
            dists.push_back(bdist(population[i].behavior, population[j].behavior));
        }
        for (auto& a : noveltyArchive)
            dists.push_back(bdist(population[i].behavior, a.data()));
        sort(dists.begin(), dists.end());
        float avg = 0; int lim = min(k, (int)dists.size());
        for (int d = 0; d < lim; d++) avg += dists[d];
        avg /= (lim > 0 ? lim : 1.0f);
        nov[i] = avg;
        population[i].novelty = avg;
    }
    for (int i = 0; i < P; i++)
        population[i].fitness += w * nov[i] * scale * 0.25f;

    int mostNovel = (int)(max_element(nov.begin(), nov.end()) - nov.begin());
    array<float, 36> bc; for (int z = 0; z < 36; z++) bc[z] = population[mostNovel].behavior[z];
    noveltyArchive.push_back(bc);
    if ((int)noveltyArchive.size() > 300) noveltyArchive.erase(noveltyArchive.begin());
}

float Evolution::measureDiversity() {
    int ss = min(30, POPULATION_SIZE);
    float tot = 0; int cnt = 0;
    for (int i = 0; i < ss; i++)
        for (int j = i + 1; j < ss; j++) { tot += (1.0f - population[i].similarity(population[j])); cnt++; }
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
    if (idxs.empty()) return randInt(0, POPULATION_SIZE - 1);
    int best = idxs[randInt(0, (int)idxs.size() - 1)];
    for (int i = 1; i < TOURNAMENT_SIZE; i++) {
        int r = idxs[randInt(0, (int)idxs.size() - 1)];
        if (population[r].fitness > population[best].fitness) best = r;
    }
    return best;
}

void Evolution::train() {
    if (monitor_) monitor_->setRunning(true);

    for (generation = 0; generation < GENERATIONS; generation++) {
        float progress = float(generation) / GENERATIONS;

        // Curriculum
        g_curriculum_difficulty = generation < 150
            ? min(1.0f, progress * (200.0f / 150.0f))
            : 1.0f;

        // Dynamisches Early-Stopping
        earlyStopLimit = 500 + int(progress * 900);

        // Mutationsstaerke
        mutationStrength = max(MUTATION_STRENGTH_MIN,
            MUTATION_STRENGTH_INIT * (1 - progress) + MUTATION_STRENGTH_FINAL * progress);
        if (boostActive) { mutationStrength = min(1.0f, mutationStrength * STAGNATION_BOOST_FACTOR); boostActive = false; }
        if (boost2Active) { mutationStrength = min(1.0f, mutationStrength * STAGNATION_BOOST2); boost2Active = false; }

        // Evaluation (parallel) - Common Random Numbers pro Generation
        uint64_t genSeeds[EVAL_REPEATS];
        for (int r = 0; r < EVAL_REPEATS; r++)
            genSeeds[r] = (uint64_t)generation * 2654435761ull + (uint64_t)r * 40503ull + 1234567ull;
        int nthreads = EVAL_THREADS > 0 ? EVAL_THREADS : omp_get_max_threads();
#pragma omp parallel for num_threads(nthreads) schedule(dynamic)
        for (int i = 0; i < POPULATION_SIZE; i++)
            evaluate(population[i], false, true, genSeeds);

        // Besten merken
        for (auto& ind : population) {
            if (ind.baseFitness > globalBestBase.baseFitness) globalBestBase = ind;
            if (ind.baseFitness > globalBest.baseFitness)     globalBest = ind;
            updateHallOfFame(ind);
        }

        speciate();
        computeNoveltyBonus();

        sort(population.begin(), population.end(),
            [](const Brain& a, const Brain& b) { return a.fitness > b.fitness; });

        // Stagnationspruefung gegen beste je erreichte SAUBERE baseFitness
        float genBestBase = -1e30f;
        for (auto& ind : population) if (ind.baseFitness > genBestBase) genBestBase = ind.baseFitness;
        if (genBestBase > bestEverBase + 1e-3f) {
            bestEverBase = genBestBase;
            stagnationGenCount = 0; stagnationGenCount2 = 0;
        }
        else {
            stagnationGenCount++; stagnationGenCount2++;
            if (stagnationGenCount >= STAGNATION_LIMIT) { boostActive = true;  stagnationGenCount = 0; }
            if (stagnationGenCount2 >= STAGNATION_LIMIT2) { boost2Active = true; stagnationGenCount2 = 0; }
        }

        float diversity = measureDiversity();
        bool inject = boost2Active || (diversity < DIVERSITY_INJECT_THRESHOLD);

        // Statistiken
        float avgFit = 0; int avgStep = 0, avgFood = 0;
        for (auto& ind : population) { avgFit += ind.fitness; avgStep += ind.steps; avgFood += ind.food; }
        avgFit /= POPULATION_SIZE; avgStep /= POPULATION_SIZE; avgFood /= POPULATION_SIZE;
        if (avgFood < 1) avgFood = 1;

       
        cout << "Gen " << generation + 1 << "/" << GENERATIONS
            << " | Score=" << (avgFit + avgFood * 5 + avgStep) / 100
            << " | Best=" << population[0].fitness
            << " | AvgFit=" << avgFit
            << " | AvgFood=" << avgFood
            << " | AvgSteps=" << avgStep
            << " | BestFood=" << population[0].food
            << " | BestStep=" << population[0].steps
            << " | SPF=" << (float)avgStep / (float)avgFood
            << " | Mut=" << mutationStrength
            << " | Div=" << diversity;
        if (inject) cout << " [INJECT]";
        cout << "\n";

        // ---- Live-Status an das Dashboard melden ----
        if (monitor_) {
            // SPF = Schritte pro Nahrung (niedriger = besser), zur Anzeige gedeckelt.
            auto cap = [](float v) { return v > 400.0f ? 400.0f : v; };
            float avgSpf = (float)avgStep / (float)avgFood;
            float bestSpf = cap(population[0].food > 0
                ? (float)population[0].steps / (float)population[0].food
                : (float)population[0].steps);

            GenRecord rec;
            rec.gen = generation + 1;
            rec.best = population[0].fitness;
            rec.avg = avgFit;
            rec.avgFood = (float)avgFood;
            rec.bestFood = population[0].food;
            rec.bestSpf = bestSpf;
            rec.avgSpf = avgSpf;
            rec.mutation = mutationStrength;
            rec.diversity = diversity;
            rec.species = (int)speciesRepresentatives.size();

            // ---------- GEÄNDERT: Replay nur alle 20 Generationen ----------
            if (generation % 10 == 0) {
                monitor_->setReplay(recordReplay(population[0], genSeeds[0], generation + 1));
            }

            monitor_->update(rec, globalBest.baseFitness, globalBest.food,
                g_curriculum_difficulty, inject, GENERATIONS);
        }
        if (avgFit > 5000) {
            std::cout << "AvgFit" << avgFit << std::endl;
            generation = GENERATIONS;
        }
        // ---- Neue Generation erzeugen ----
        vector<Brain> next;
        next.reserve(POPULATION_SIZE);

        // Eliten (sanft mutiert)
        // Hinweis: Brain ist ~200 KB gross. Wir legen die Nachkommen daher NICHT
        // als benannte lokale Variablen auf dem Stack an (das fuehrte zum Stack
        // Overflow), sondern konstruieren direkt im Heap-Vektor 'next' und
        // mutieren das letzte Element in-place.
        for (int i = 0; i < ELITE_COUNT; i++) {
            next.push_back(population[i]);                 // Kopie direkt im Heap
            next.back().mutate(mutationStrength, ELITE_MUTATION_SIGMA);
        }
        // Hall of Fame
        for (auto& h : hallOfFame) {
            if ((int)next.size() >= POPULATION_SIZE) break;
            next.push_back(h);
            next.back().mutate(mutationStrength, ELITE_MUTATION_SIGMA);
        }

        int numSp = (int)speciesRepresentatives.size();
        vector<vector<int>> spMem(numSp);
        for (int i = 0; i < POPULATION_SIZE; i++) spMem[population[i].speciesId].push_back(i);
        vector<float> spTot(numSp, 0);
        for (int i = 0; i < POPULATION_SIZE; i++) spTot[population[i].speciesId] += population[i].fitness;
        float totAll = accumulate(spTot.begin(), spTot.end(), 0.0f);
        if (totAll == 0) totAll = 1;

        int rem = POPULATION_SIZE - (int)next.size();
        int injCnt = inject ? max(6, rem / 10) : 0;
        rem -= injCnt;

        vector<int> offs(numSp, 0);
        for (int s = 0; s < numSp && rem > 0; s++) { offs[s] = 1; rem--; }
        for (int s = 0; s < numSp && rem > 0; s++) {
            int add = (int)(spTot[s] / totAll * rem);
            offs[s] += add; rem -= add;
        }
        if (rem > 0 && numSp > 0) {
            int bs = (int)(max_element(spTot.begin(), spTot.end()) - spTot.begin());
            offs[bs] += rem;
        }

        for (int s = 0; s < numSp; s++) {
            auto& mem = spMem[s];
            if (mem.empty()) continue;
            for (int n = 0; n < offs[s]; n++) {
                int p1, p2;
                if (randFloat(0, 1) < CROSS_SPECIES_MATING_CHANCE && numSp > 1) {
                    int os = s; while (os == s) os = randInt(0, numSp - 1);
                    p1 = spMem[os].empty() ? tournamentSelect(mem) : tournamentSelect(spMem[os]);
                }
                else p1 = tournamentSelect(mem);
                p2 = tournamentSelect(mem);
                next.push_back(Brain::crossover(population[p1], population[p2]));
                next.back().mutate(mutationStrength);
                if ((int)next.size() >= POPULATION_SIZE) break;
            }
            if ((int)next.size() >= POPULATION_SIZE) break;
        }

        // Zufalls-Injektionen
        for (int i = 0; i < injCnt && (int)next.size() < POPULATION_SIZE; i++) {
            next.emplace_back();          // Brain() initialisiert bereits zufaellig (im Heap)
            next.back().randomize();
        }
        // Auffuellen
        while ((int)next.size() < POPULATION_SIZE) {
            next.push_back(population[randInt(0, POPULATION_SIZE - 1)]);
        }
        population = move(next);
    }

    if (monitor_) monitor_->setFinished();

    cout << "\n=== DEMO des besten Basis-Agenten ===\n";
    set_local_seed(42766);
    evaluate(globalBestBase, true, false);
}

// --------------------------------------------------------
//  REPLAY-AUFZEICHNUNG
//  Spielt eine Episode auf einer festen Karte durch und
//  protokolliert sie kompakt als JSON:
//    - statische Waende/Hindernisse (einmalig)
//    - Start-Nahrung (einmalig)
//    - Agentenposition pro Frame
//    - Nahrungs-Aenderungen als Events [frame, typ, index]
//      (typ 0 = entfernt/gefressen, typ 1 = neu erschienen)
// --------------------------------------------------------
std::string Evolution::recordReplay(Brain brain, uint64_t seed, int genLabel) {
    set_local_seed(seed);
    World world;
    world.earlyStopLimit = earlyStopLimit;
    brain.resetHidden();

    std::ostringstream walls, food0, frames, events;
    bool firstWall = true, firstFood = true, firstFrame = true, firstEvt = true;

    auto pushIdx = [](std::ostringstream& os, bool& first, int idx) {
        if (!first) os << ','; first = false; os << idx;
        };

    // Statische Karte + Start-Nahrung
    for (int i = 0; i < World::CELLS; i++) {
        if (world.grid[i] == WALL || world.grid[i] == OBSTACLE) pushIdx(walls, firstWall, i);
        if (world.grid[i] == FOOD)                              pushIdx(food0, firstFood, i);
    }

    // Frame 0 = Startposition
    auto pushFrame = [&](int x, int y) {
        if (!firstFrame) frames << ','; firstFrame = false;
        frames << '[' << x << ',' << y << ']';
        };
    pushFrame(world.agentX, world.agentY);

    std::array<bool, World::CELLS> prevFood = world.foodHere;

    int frame = 0;
    while (world.alive && world.steps < MAX_EPISODE_STEPS) {
        auto sens = world.getSensors();
        float logits[OUTPUTS];
        brain.forward(sens, logits);
        softmax_inplace(logits, OUTPUTS);
        float rv = randFloat(0, 1); int act = OUTPUTS - 1; float cum = 0;
        for (int i = 0; i < OUTPUTS; i++) { cum += logits[i]; if (rv < cum) { act = i; break; } }
        world.executeAction(act);
        frame++;
        pushFrame(world.agentX, world.agentY);

        // Nahrungs-Aenderungen seit letztem Frame protokollieren
        for (int i = 0; i < World::CELLS; i++) {
            if (world.foodHere[i] != prevFood[i]) {
                if (!firstEvt) events << ','; firstEvt = false;
                events << '[' << frame << ',' << (world.foodHere[i] ? 1 : 0) << ',' << i << ']';
                prevFood[i] = world.foodHere[i];
            }
        }
    }

    std::ostringstream os;
    os << '{'
        << "\"generation\":" << genLabel << ','
        << "\"w\":" << GRID_W << ',' << "\"h\":" << GRID_H << ','
        << "\"food\":" << world.foodEaten << ',' << "\"steps\":" << world.steps << ','
        << "\"walls\":[" << walls.str() << "],"
        << "\"food0\":[" << food0.str() << "],"
        << "\"frames\":[" << frames.str() << "],"
        << "\"events\":[" << events.str() << "]"
        << '}';

    return os.str();
}
