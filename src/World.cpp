// ============================================================
//  World.cpp
// ============================================================
#include "World.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using namespace std;

namespace {
    // Zellindex -> Koordinaten ohne Division/Modulo im Hotpath.
    struct CellTables {
        uint8_t x[World::CELLS];
        uint8_t y[World::CELLS];
        constexpr CellTables() : x{}, y{} {
            for (int i = 0; i < World::CELLS; i++) { x[i] = uint8_t(i % GRID_W); y[i] = uint8_t(i / GRID_W); }
        }
    };
    constexpr CellTables kCell{};

    constexpr int kPlaceAttempts = 256;  // Obergrenze fuer Zufallssuche nach freien Zellen
}

World::World(float difficulty_, const RuntimeConfig& cfg_, bool generate)
    : cfg(cfg_), difficulty(difficulty_) {
    rng.seed(local_rng().next());
    if (generate) reset();
    else { buildBorder(); agentX = GRID_W / 2; agentY = GRID_H / 2; startEpisode(); }
}

void World::generateBiome(int x, int y, int type) {
    biome[y * GRID_W + x] = type;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 1 && nx < GRID_W - 1 && ny >= 1 && ny < GRID_H - 1 && rng.uniform01() < 0.3f)
                biome[ny * GRID_W + nx] = type;
        }
}

void World::rebuildFoodList() {
    foodIndices.clear();
    for (int i = 0; i < CELLS; i++) {
        if (grid[i] == FOOD) { foodIndices.push_back(i); foodHere[i] = true; }
        else                  foodHere[i] = false;
    }
    invalidateNearest();
}

void World::placeFood(int idx) {
    grid[idx] = FOOD;
    foodHere[idx] = true;
    foodIndices.push_back(idx);
    invalidateNearest();
    if (foodEventSink) foodEventSink->push_back({ steps + 1, idx, true });
}

void World::removeFood(int idx) {
    grid[idx] = EMPTY;
    foodHere[idx] = false;
    auto it = find(foodIndices.begin(), foodIndices.end(), idx);
    if (it != foodIndices.end()) { *it = foodIndices.back(); foodIndices.pop_back(); }
    invalidateNearest();
    if (foodEventSink) foodEventSink->push_back({ steps + 1, idx, false });
}

// Naechstes Food (Manhattan-Distanz); bei Gleichstand gewinnt das erste in foodIndices.
const World::Nearest& World::nearest() {
    if (!nearestValid_) {
        Nearest n;
        for (int fi : foodIndices) {
            int d = abs(int(kCell.x[fi]) - agentX) + abs(int(kCell.y[fi]) - agentY);
            if (d < n.dist) { n.dist = d; n.idx = fi; }
        }
        nearestCache_ = n;
        nearestValid_ = true;
    }
    return nearestCache_;
}

float World::nearestFoodDist() { return (float)nearest().dist; }

bool World::spawnFoodRandom() {
    for (int t = 0; t < kPlaceAttempts; t++) {
        int fx = rng.integer(1, GRID_W - 2), fy = rng.integer(1, GRID_H - 2), fi = fy * GRID_W + fx;
        if (grid[fi] == EMPTY) { placeFood(fi); return true; }
    }
    return false;  // Karte (nahezu) voll
}

void World::buildBorder() {
    grid.fill(EMPTY);
    biome.fill(0);
    memset(visitedZones, 0, sizeof(visitedZones));
    for (int x = 0; x < GRID_W; x++) { grid[x] = WALL; grid[(GRID_H - 1) * GRID_W + x] = WALL; }
    for (int y = 0; y < GRID_H; y++) { grid[y * GRID_W] = WALL; grid[y * GRID_W + GRID_W - 1] = WALL; }
    foodIndices.clear();
    foodHere.fill(false);
}

void World::startEpisode() {
    energy = START_ENERGY; alive = true; steps = 0; foodEaten = 0; totalReward = 0;
    lastAction = 0;
    visited.reset();
    visited.set(agentY * GRID_W + agentX);
    visitedCount = 1;
    recentIdx = 0; recentSize = 0;
    lastPositions.fill({ 0, 0 }); lastPosCount = 0;
    stepsSinceProgress = 0; lastVisitedCount = 1; lastFoodCount = 0;
    invalidateNearest();
    prevFoodDist = nearestFoodDist();
}

void World::reset() {
    buildBorder();

    for (int i = 0; i < 20; i++)
        generateBiome(rng.integer(5, GRID_W - 6), rng.integer(5, GRID_H - 6), rng.integer(0, NUM_BIOMES - 1));

    int obsCnt = int(OBSTACLE_COUNT * difficulty);
    for (int i = 0; i < obsCnt; i++) {
        for (int t = 0; t < kPlaceAttempts; t++) {
            int ox = rng.integer(1, GRID_W - 2), oy = rng.integer(1, GRID_H - 2), idx = oy * GRID_W + ox;
            if (grid[idx] == EMPTY) { grid[idx] = OBSTACLE; break; }
        }
    }

    int targetFood = int(BASE_FOOD_COUNT + (1.0f - difficulty) * 10);
    int placed = 0, attempts = 0;
    while (placed < targetFood && attempts < 10000) {
        attempts++;
        int x = rng.integer(1, GRID_W - 2), y = rng.integer(1, GRID_H - 2);
        int idx = y * GRID_W + x;
        if (grid[idx] != EMPTY) continue;
        float prob = 0.14f;                 // biomabhaengige Dichte
        if (biome[idx] == 1) prob = 0.28f;
        if (biome[idx] == 2) prob = 0.05f;
        if (rng.uniform01() < prob) { grid[idx] = FOOD; placed++; }
    }
    // Falls zu wenig Food platziert wurde, restliche direkt setzen
    while (placed < targetFood) {
        int x = rng.integer(1, GRID_W - 2), y = rng.integer(1, GRID_H - 2), idx = y * GRID_W + x;
        if (grid[idx] == EMPTY) { grid[idx] = FOOD; placed++; }
    }
    rebuildFoodList();

    do { agentX = rng.integer(1, GRID_W - 2); agentY = rng.integer(1, GRID_H - 2); }
    while (grid[agentY * GRID_W + agentX] != EMPTY);

    startEpisode();
}

void World::loadCustomMap(const CustomMap& map) {
    buildBorder();
    for (int idx : map.walls)
        if (idx >= 0 && idx < CELLS) grid[idx] = WALL;
    for (int idx : map.food)
        if (idx >= 0 && idx < CELLS && grid[idx] == EMPTY) grid[idx] = FOOD;

    int ax = max(1, min(GRID_W - 2, map.startX));
    int ay = max(1, min(GRID_H - 2, map.startY));
    int agIdx = ay * GRID_W + ax;
    if (grid[agIdx] == WALL || grid[agIdx] == OBSTACLE) {
        // naechste freie Zelle (nach Index-Abstand) suchen
        int best = -1, bestD = 1 << 30;
        for (int i = 0; i < CELLS; i++) {
            if (grid[i] == WALL || grid[i] == OBSTACLE) continue;
            int d = abs(int(kCell.x[i]) - ax) + abs(int(kCell.y[i]) - ay);
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best >= 0) agIdx = best;
    }
    if (grid[agIdx] == FOOD) grid[agIdx] = EMPTY;
    agentX = agIdx % GRID_W;
    agentY = agIdx / GRID_W;

    rebuildFoodList();
    startEpisode();
}

// --------------------------------------------------------
//  SENSOREN (77 Eingaben)
// --------------------------------------------------------
array<float, INPUTS> World::getSensors() {
    array<float, INPUTS> s{};
    int agIdx = agentY * GRID_W + agentX;

    // [0..6] Naechstes Food: Distanz + normalisierte Richtung
    const Nearest& nf = nearest();
    if (nf.idx != -1) {
        s[0] = 1.0f / (nf.dist + 1.0f);
        float dx = (float)(int(kCell.x[nf.idx]) - agentX);
        float dy = (float)(int(kCell.y[nf.idx]) - agentY);
        float len = sqrtf(dx * dx + dy * dy) + 1e-6f;
        s[1] = dx / len; // -1..1 (Ost/West)
        s[2] = dy / len; // -1..1 (Sued/Nord)
        if (fabsf(dx) > fabsf(dy)) { s[dx > 0 ? 3 : 4] = 1.0f; }
        else                       { s[dy > 0 ? 5 : 6] = 1.0f; }
    }

    // [7..10] Food-Richtungssensoren (4 Strahlen)
    for (int i = 1; i <= 15; i++) if (agentX - i > 0 && grid[agIdx - i] == FOOD) { s[7] = 1.0f / i; break; }
    for (int i = 1; i <= 15; i++) if (agentX + i < GRID_W - 1 && grid[agIdx + i] == FOOD) { s[8] = 1.0f / i; break; }
    for (int i = 1; i <= 15; i++) if (agentY - i > 0 && grid[agIdx - i * GRID_W] == FOOD) { s[9] = 1.0f / i; break; }
    for (int i = 1; i <= 15; i++) if (agentY + i < GRID_H - 1 && grid[agIdx + i * GRID_W] == FOOD) { s[10] = 1.0f / i; break; }

    // [11..14] Wandabstand (normalisiert)
    s[11] = (agentX - 1) / float(GRID_W);
    s[12] = (GRID_W - agentX - 2) / float(GRID_W);
    s[13] = (agentY - 1) / float(GRID_H);
    s[14] = (GRID_H - agentY - 2) / float(GRID_H);

    // [15] Energie
    s[15] = energy / START_ENERGY;

    // [16..17] Position (normalisiert)
    s[16] = float(agentX) / GRID_W;
    s[17] = float(agentY) / GRID_H;

    // [18..21] Letzte Aktion (one-hot)
    s[18 + lastAction] = 1.0f;

    // [22] Biom
    s[22] = biome[agIdx] / float(NUM_BIOMES - 1);

    // [23] Erkundungsfortschritt
    s[23] = visitedCount / float((GRID_W - 2) * (GRID_H - 2));

    // [24] Wiederholungsrate (Anti-Loop)
    int rep = 0;
    for (int i = 0; i < recentSize; i++)
        if (recentBuf[i].first == agentX && recentBuf[i].second == agentY) rep++;
    s[24] = rep / float(ANTI_LOOP_MEMORY_SIZE);

    // [25] Zufallsrauschen (Exploration)
    s[25] = rng.uniform(-1.0f, 1.0f);

    // [26..27] Gegessenes Food / Fooddichte
    s[26] = float(foodEaten) / 50.0f;
    s[27] = (float)foodIndices.size() / (float)BASE_FOOD_COUNT;

    // [28..76] 7x7 Sichtfeld (Vision)
    int base = 28;
    for (int dy = -VISION_RANGE; dy <= VISION_RANGE; dy++) {
        for (int dx = -VISION_RANGE; dx <= VISION_RANGE; dx++) {
            int x = agentX + dx, y = agentY + dy;
            if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) { s[base++] = 1.0f; }
            else {
                int ci = y * GRID_W + x;
                if (grid[ci] == WALL || grid[ci] == OBSTACLE) s[base++] = 1.0f;
                else if (grid[ci] == FOOD)                    s[base++] = 0.7f;
                else if (visited.test(ci))                    s[base++] = -0.3f; // schon besucht
                else                                          s[base++] = 0.0f;
            }
        }
    }
    return s;
}

float World::executeAction(int action) {
    lastAction = action;
    int dx = 0, dy = 0;
    switch (action) {
    case 0: dx = -1; break;
    case 1: dx = 1; break;
    case 2: dy = -1; break;
    case 3: dy = 1; break;
    }
    int nx = agentX + dx, ny = agentY + dy;
    int nIdx = ny * GRID_W + nx;
    float reward = -cfg.stepCost;

    if (grid[nIdx] == WALL || grid[nIdx] == OBSTACLE) {
        reward += WALL_PENALTY;
    }
    else {
        agentX = nx; agentY = ny;
        invalidateNearest();
        if (lastPosCount < 3) lastPosCount++;
        lastPositions[2] = lastPositions[1];
        lastPositions[1] = lastPositions[0];
        lastPositions[0] = { agentX, agentY };
        int curIdx = agentY * GRID_W + agentX;

        // Erkundungsbonus
        if (!visited.test(curIdx)) {
            reward += cfg.exploreBonus;
            visited.set(curIdx);
            visitedCount++;
            int zx = agentX / ZONE_SIZE, zy = agentY / ZONE_SIZE;
            if (zx < ZONE_GRID && zy < ZONE_GRID && !visitedZones[zy][zx]) {
                visitedZones[zy][zx] = true;
                reward += NEW_ZONE_BONUS;
            }
        }

        // Annaeherungsbonus: belohne Bewegung Richtung Food
        float curDist = nearestFoodDist();
        if (curDist < prevFoodDist) reward += APPROACH_FOOD_BONUS;
        else if (curDist > prevFoodDist) reward -= 0.3f; // leichte Strafe fuers Entfernen
        prevFoodDist = curDist;

        // Anti-Loop-Strafe
        int posCount = 0;
        for (int i = 0; i < recentSize; i++)
            if (recentBuf[i].first == agentX && recentBuf[i].second == agentY) posCount++;
        if (posCount > 2) reward += REVISIT_PENALTY * (posCount - 2);

        // Food fressen
        if (grid[curIdx] == FOOD) {
            reward += cfg.foodReward;
            removeFood(curIdx);
            foodEaten++;
            energy += cfg.foodReward * 0.4f; // Energie regenerieren beim Essen
            if (rng.uniform01() < FOOD_RESPAWN_RATE) spawnFoodRandom();
            prevFoodDist = nearestFoodDist();
        }
        else if (rng.uniform01() < FOOD_RESPAWN_RATE * 0.4f) {
            spawnFoodRandom();
        }
    }

    energy += reward;
    if (energy > ENERGY_MAX) energy = ENERGY_MAX; // Deckel
    if (energy <= 0.0f) alive = false;
    steps++;
    totalReward += reward;

    recentBuf[recentIdx] = { agentX, agentY };
    recentIdx = (recentIdx + 1) % ANTI_LOOP_MEMORY_SIZE;
    if (recentSize < ANTI_LOOP_MEMORY_SIZE) recentSize++;

    // Early-Stopping
    if (visitedCount != lastVisitedCount || foodEaten != lastFoodCount) {
        stepsSinceProgress = 0; lastVisitedCount = visitedCount; lastFoodCount = foodEaten;
    }
    else if (++stepsSinceProgress > earlyStopLimit) {
        alive = false;
    }
    return reward;
}

void World::display() const {
    string out;
    out.reserve((GRID_W + 1) * GRID_H);
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (x == agentX && y == agentY) { out += 'A'; continue; }
            switch (grid[y * GRID_W + x]) {
            case WALL:     out += '#'; break;
            case OBSTACLE: out += 'X'; break;
            case FOOD:     out += 'o'; break;
            default:       out += '.';
            }
        }
        out += '\n';
    }
    cout << out;
}
