#pragma once
// ============================================================
//  World.h  -  Die Gitterwelt (Environment)
//  Enthaelt Nahrung, Hindernisse, Biome und den Agenten.
//  Liefert 77 Sensorwerte und verarbeitet Aktionen inkl.
//  Reward-Shaping, Anti-Loop-Logik und Early-Stopping.
// ============================================================
#include "Config.h"
#include <array>
#include <bitset>
#include <cstdint>
#include <utility>
#include <vector>

enum Tile : uint8_t { EMPTY = 0, FOOD = 1, WALL = 2, OBSTACLE = 3 };

struct World {
    static constexpr int CELLS = GRID_W * GRID_H;

    std::array<Tile, CELLS> grid;
    std::array<int, CELLS>  biome;
    std::bitset<CELLS>      visited;
    bool                    visitedZones[6][6] = {};

    std::vector<int>        foodIndices;
    std::array<bool, CELLS> foodHere = {};

    int   agentX, agentY;
    int   lastAction = 0;
    float energy;
    bool  alive;
    int   steps;
    int   foodEaten;
    float totalReward;

    // Anti-Loop-Ringpuffer
    std::array<std::pair<int, int>, ANTI_LOOP_MEMORY_SIZE> recentBuf;
    int recentIdx = 0, recentSize = 0;

    // Letzte Positionen fuer Sensoren
    std::array<std::pair<int, int>, 3> lastPositions;
    int lastPosCount = 0;

    // Distanz zum naechsten Food (Annaeherungsbonus)
    float prevFoodDist = 999.f;

    // Early-Stopping
    int earlyStopLimit = 600;
    int stepsSinceProgress = 0;
    int lastVisitedCount = 0;
    int lastFoodCount = 0;

    World();

    void  generateBiome(int x, int y, int type);
    void  rebuildFoodList();
    void  placeFood(int idx);
    void  removeFood(int idx);
    float nearestFoodDist();
    void  reset();

    std::array<float, INPUTS> getSensors();
    float executeAction(int action);
    void  display();
};
