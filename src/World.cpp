// ============================================================
//  World.cpp
// ============================================================
#include "World.h"
#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace std;

World::World() { reset(); }

void World::generateBiome(int x, int y, int type) {
    biome[y * GRID_W + x] = type;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 1 && nx < GRID_W - 1 && ny >= 1 && ny < GRID_H - 1 && randFloat(0, 1) < 0.3f)
                biome[ny * GRID_W + nx] = type;
        }
}

void World::rebuildFoodList() {
    foodIndices.clear();
    for (int i = 0; i < CELLS; i++) {
        if (grid[i] == FOOD) { foodIndices.push_back(i); foodHere[i] = true; }
        else                  foodHere[i] = false;
    }
}

void World::placeFood(int idx) {
    grid[idx] = FOOD;
    foodHere[idx] = true;
    foodIndices.push_back(idx);
}

void World::removeFood(int idx) {
    grid[idx] = EMPTY;
    foodHere[idx] = false;
    auto it = find(foodIndices.begin(), foodIndices.end(), idx);
    if (it != foodIndices.end()) { *it = foodIndices.back(); foodIndices.pop_back(); }
}

// Manhattan-Distanz zum naechsten Food
float World::nearestFoodDist() {
    int best = 9999;
    for (int fi : foodIndices) {
        int dx = fi % GRID_W - agentX, dy = fi / GRID_W - agentY;
        int d = abs(dx) + abs(dy);
        if (d < best) best = d;
    }
    return (float)best;
}

void World::reset() {
    grid.fill(EMPTY);
    biome.fill(0);
    memset(visitedZones, 0, sizeof(visitedZones));

    for (int x = 0; x < GRID_W; x++) { grid[x] = WALL; grid[(GRID_H - 1) * GRID_W + x] = WALL; }
    for (int y = 0; y < GRID_H; y++) { grid[y * GRID_W] = WALL; grid[y * GRID_W + GRID_W - 1] = WALL; }

    for (int i = 0; i < 20; i++)
        generateBiome(randInt(5, GRID_W - 6), randInt(5, GRID_H - 6), randInt(0, NUM_BIOMES - 1));

    int obsCnt = int(OBSTACLE_COUNT * g_curriculum_difficulty);
    for (int i = 0; i < obsCnt; i++) {
        int ox, oy, idx;
        do { ox = randInt(1, GRID_W - 2); oy = randInt(1, GRID_H - 2); idx = oy * GRID_W + ox; } while (grid[idx] != EMPTY);
        grid[idx] = OBSTACLE;
    }

    int targetFood = int(BASE_FOOD_COUNT + (1.0f - g_curriculum_difficulty) * 10);
    int placed = 0;
    int attempts = 0;
    while (placed < targetFood && attempts < 10000) {
        attempts++;
        int x = randInt(1, GRID_W - 2), y = randInt(1, GRID_H - 2);
        int idx = y * GRID_W + x;
        if (grid[idx] != EMPTY) continue;
        float prob = 0.14f;
        if (biome[idx] == 1) prob = 0.28f;
        if (biome[idx] == 2) prob = 0.05f;
        if (randFloat(0, 1) < prob) { grid[idx] = FOOD; placed++; }
    }
    // Falls zu wenig Food platziert wurde, restliche direkt setzen
    while (placed < targetFood) {
        int x = randInt(1, GRID_W - 2), y = randInt(1, GRID_H - 2), idx = y * GRID_W + x;
        if (grid[idx] == EMPTY) { grid[idx] = FOOD; placed++; }
    }
    rebuildFoodList();

    do { agentX = randInt(1, GRID_W - 2); agentY = randInt(1, GRID_H - 2); } while (grid[agentY * GRID_W + agentX] != EMPTY);

    // Read runtime parameters
    auto rcfg = getRuntimeConfig();
    energy = START_ENERGY; alive = true; steps = 0; foodEaten = 0; totalReward = 0;
    visited.reset();
    visited.set(agentY * GRID_W + agentX);
    recentIdx = 0; recentSize = 0;
    lastPositions.fill({ 0, 0 }); lastPosCount = 0;
    stepsSinceProgress = 0; lastVisitedCount = 1; lastFoodCount = 0;
    prevFoodDist = nearestFoodDist();
}

// --------------------------------------------------------
//  SENSOREN (77 Eingaben)
// --------------------------------------------------------
array<float, INPUTS> World::getSensors() {
    array<float, INPUTS> s{};
    int agIdx = agentY * GRID_W + agentX;

    // [0..6] Naechstes Food: Distanz + normalisierte Richtung
    int nearestIdx = -1; float minDist = 9999;
    for (int fi : foodIndices) {
        int fx = fi % GRID_W, fy = fi / GRID_W;
        float d = (float)(abs(fx - agentX) + abs(fy - agentY));
        if (d < minDist) { minDist = d; nearestIdx = fi; }
    }
    if (nearestIdx != -1) {
        float invDist = 1.0f / (minDist + 1.0f);
        s[0] = invDist;
        float dx = (float)(nearestIdx % GRID_W - agentX);
        float dy = (float)(nearestIdx / GRID_W - agentY);
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
    s[23] = visited.count() / float((GRID_W - 2) * (GRID_H - 2));

    // [24] Wiederholungsrate (Anti-Loop)
    int rep = 0;
    for (int i = 0; i < recentSize; i++)
        if (recentBuf[i].first == agentX && recentBuf[i].second == agentY) rep++;
    s[24] = rep / float(ANTI_LOOP_MEMORY_SIZE);

    // [25] Zufallsrauschen (Exploration)
    s[25] = randFloat(-1.0f, 1.0f);

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
    // base == 28 + 49 == 77 == INPUTS

    return s;
}

float World::executeAction(int action) {
    auto rcfg = getRuntimeConfig();
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
    float reward = -rcfg.stepCost;

    if (grid[nIdx] == WALL || grid[nIdx] == OBSTACLE) {
        reward += WALL_PENALTY;
    }
    else {
        agentX = nx; agentY = ny;
        if (lastPosCount < 3) lastPosCount++;
        lastPositions[2] = lastPositions[1];
        lastPositions[1] = lastPositions[0];
        lastPositions[0] = { agentX, agentY };
        int curIdx = agentY * GRID_W + agentX;

        // Erkundungsbonus
        if (!visited.test(curIdx)) {
            reward += rcfg.exploreBonus;
            visited.set(curIdx);
            int zx = agentX / 8, zy = agentY / 8;
            if (zx < 6 && zy < 6 && !visitedZones[zy][zx]) {
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
            reward += rcfg.foodReward;
            removeFood(curIdx);
            foodEaten++;
            energy += rcfg.foodReward * 0.4f; // Energie regenerieren beim Essen
            prevFoodDist = nearestFoodDist();
            if (randFloat(0, 1) < FOOD_RESPAWN_RATE) {
                int fx, fy, fi;
                do { fx = randInt(1, GRID_W - 2); fy = randInt(1, GRID_H - 2); fi = fy * GRID_W + fx; } while (grid[fi] != EMPTY);
                placeFood(fi);
            }
        }
        else if (randFloat(0, 1) < FOOD_RESPAWN_RATE * 0.4f) {
            int fx, fy, fi;
            do { fx = randInt(1, GRID_W - 2); fy = randInt(1, GRID_H - 2); fi = fy * GRID_W + fx; } while (grid[fi] != EMPTY);
            placeFood(fi);
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
    int nowVisited = (int)visited.count(), nowFood = foodEaten;
    if (nowVisited != lastVisitedCount || nowFood != lastFoodCount) {
        stepsSinceProgress = 0; lastVisitedCount = nowVisited; lastFoodCount = nowFood;
    }
    else {
        if (++stepsSinceProgress > earlyStopLimit) alive = false;
    }
    return reward;
}

void World::display() {
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (x == agentX && y == agentY) cout << 'A';
            else {
                Tile t = grid[y * GRID_W + x];
                switch (t) {
                case WALL:     cout << '#'; break;
                case OBSTACLE: cout << 'X'; break;
                case FOOD:     cout << 'o'; break;
                default:       cout << '.';
                }
            }
        }
        cout << '\n';
    }
}
