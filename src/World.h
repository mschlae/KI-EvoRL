#pragma once
// ============================================================
//  World.h  -  Die Gitterwelt (Environment)
//  Enthaelt Nahrung, Hindernisse, Biome und den Agenten.
//  Liefert 77 Sensorwerte und verarbeitet Aktionen inkl.
//  Reward-Shaping, Anti-Loop-Logik und Early-Stopping.
//
//  Eine World haelt ihre eigene Kopie der RuntimeConfig (wird bei
//  der Konstruktion eingefroren). Dadurch gibt es im Schrittbetrieb
//  keine Mutex-Zugriffe und Dashboard-Aenderungen wirken sich nicht
//  mitten in einer Episode (oder Generation) aus.
// ============================================================
#include "Config.h"
#include "Rng.h"
#include <array>
#include <bitset>
#include <cstdint>
#include <utility>
#include <vector>

enum Tile : uint8_t { EMPTY = 0, FOOD = 1, WALL = 2, OBSTACLE = 3 };

// Aenderung am Nahrungsbestand (fuer Replays): step = Frame, in dem sie passiert.
struct FoodEvent { int step; int idx; bool appeared; };

// Vom Nutzer gezeichnete Karte (Playground). Indizes sind y * GRID_W + x.
struct CustomMap {
    std::vector<int> walls;
    std::vector<int> food;
    int startX = GRID_W / 2;
    int startY = GRID_H / 2;
};

struct World {
    static constexpr int CELLS = GRID_W * GRID_H;

    std::array<Tile, CELLS> grid;
    std::array<int, CELLS>  biome;
    std::bitset<CELLS>      visited;
    int                     visitedCount = 0;
    bool                    visitedZones[ZONE_GRID][ZONE_GRID] = {};

    std::vector<int>        foodIndices;
    std::array<bool, CELLS> foodHere = {};

    int   agentX = 0, agentY = 0;
    int   lastAction = 0;
    float energy = 0;
    bool  alive = true;
    int   steps = 0;
    int   foodEaten = 0;
    float totalReward = 0;

    // Kennzahlen: Kollisionen und Umweg (gelaufene Schritte je Futter / kuerzester Weg dorthin)
    int   collisions = 0;
    float detourSum = 0;      // Summe der Umwegfaktoren aller gefressenen Futter
    int   legSteps = 0;       // Schritte seit dem letzten Futter (bzw. Start)
    int   legStartIdx = 0;

    // Eigener Zufallsstrom (Karte, Respawns, Sensorrauschen, Aktionswahl). Wird bei der
    // Konstruktion aus dem Thread-Generator gezogen -> set_local_seed() vor dem Erzeugen
    // macht die ganze Episode reproduzierbar, und mehrere Welten koennen verzahnt laufen.
    Rng rng;

    RuntimeConfig cfg;        // eingefrorene Laufzeit-Parameter
    float difficulty = 1.0f;  // Curriculum 0..1

    // Optional: wird gesetzt, um Nahrungsaenderungen mitzuschreiben (Replay).
    std::vector<FoodEvent>* foodEventSink = nullptr;

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

    // Zufaellige Karte (generate = true) oder leere Huelle fuer loadCustomMap().
    explicit World(float difficulty = 1.0f, const RuntimeConfig& cfg = getRuntimeConfig(),
        bool generate = true);

    void  generateBiome(int x, int y, int type);
    void  rebuildFoodList();
    void  placeFood(int idx);
    void  removeFood(int idx);
    float nearestFoodDist();
    void  reset();
    // Ersetzt die Karte durch eine eigene (Rand ist immer Wand) und startet die Episode neu.
    void  loadCustomMap(const CustomMap& map);

    // Bitmaske der Aktionen (Bit i = Aktion i), die gegen Wand/Hindernis laufen wuerden.
    uint8_t blockedMask() const;
    // Kuerzeste Weglaenge (Zellen) zwischen zwei Zellen ueber begehbare Felder; -1 = unerreichbar.
    int shortestPath(int fromIdx, int toIdx) const;
    float meanDetour() const { return foodEaten > 0 ? detourSum / foodEaten : 1.0f; }

    std::array<float, INPUTS> getSensors();
    float executeAction(int action);
    void  display() const;

private:
    struct Nearest { int idx = -1; int dist = 9999; };

    void  buildBorder();
    void  startEpisode();
    bool  spawnFoodRandom();                 // true, wenn eine freie Zelle gefunden wurde
    const Nearest& nearest();                // gecacht, bis Agent/Nahrung sich aendern
    void  invalidateNearest() { nearestValid_ = false; }

    Nearest nearestCache_;
    bool    nearestValid_ = false;
};
