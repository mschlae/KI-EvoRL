#pragma once
// ============================================================
//  Config.h  -  Globale Konfiguration der Simulation
//  Alle Tuning-Parameter zentral an einem Ort.
// ============================================================

// ----- Welt -----
constexpr int   GRID_W = 45;
constexpr int   GRID_H = 45;
constexpr int   NUM_BIOMES = 6;
constexpr int   OBSTACLE_COUNT = 5;

// ----- Reward Shaping -----
constexpr float START_ENERGY = 80.0f;   // mehr Startenergie -> längere Episoden früh
constexpr float STEP_COST = 4.5f;    // geringer -> Agent wird nicht für Bewegung bestraft
constexpr float WALL_PENALTY = -20.0f;  // harte Strafe für Wandkollisionen
constexpr float FOOD_REWARD = 150.0f;  // höher -> klares Lernsignal
constexpr float EXPLORATION_BONUS = 0.2f;    // kleiner Anreiz für neue Zellen
constexpr float NEW_ZONE_BONUS = 1.5f;    // Zonenbonus erhöht
constexpr float REVISIT_PENALTY = -1.0f;   // Strafe für häufiges Wiederbesuchen
constexpr float APPROACH_FOOD_BONUS = 2.0f;    // NEU: Bonus wenn Distanz zu nächstem Food sinkt
constexpr float IDLE_PENALTY = -0.5f;   // NEU: Strafe wenn Agent keine Fortschritte macht

constexpr int   BASE_FOOD_COUNT = 15;      // mehr Nahrung -> leichteres Lernsignal
constexpr float FOOD_RESPAWN_RATE = 0.3f;


// ----- Netz -----
constexpr int   VISION_RANGE = 3;            // 7x7-Fenster
constexpr int   VISION_CELLS = (2 * VISION_RANGE + 1) * (2 * VISION_RANGE + 1); // 49
constexpr int   INPUTS = 28 + VISION_CELLS;  // 28 Basis + 49 Vision = 77
constexpr int   HIDDEN = 84;
constexpr int   OUTPUTS = 4;

// ----- Evolution -----
constexpr int   EVAL_REPEATS = 4;
constexpr int   POPULATION_SIZE = 100;
constexpr int   GENERATIONS = 10000;
constexpr int   MAX_STEPS = 800000;
constexpr int   ELITE_COUNT = 10;
constexpr int   TOURNAMENT_SIZE = 6;
constexpr int   HALL_OF_FAME_SIZE = 5;

constexpr int   SPECIES_TARGET = 5;
constexpr float SPECIES_SIMILARITY_THRESHOLD = 0.83f;

constexpr float BASE_MUTATION_RATE = 0.50f;
constexpr float MUTATION_STRENGTH_INIT = 0.20f;
constexpr float MUTATION_STRENGTH_FINAL = 0.06f;
constexpr float MUTATION_STRENGTH_MIN = 0.030f;
constexpr int   STAGNATION_LIMIT = 5;
constexpr float STAGNATION_BOOST_FACTOR = 2.0f;
constexpr int   STAGNATION_LIMIT2 = 12;
constexpr float STAGNATION_BOOST2 = 3.0f;
constexpr float DIVERSITY_INJECT_THRESHOLD = 0.02f;

constexpr float NOVELTY_WEIGHT_START = 0.55f;
constexpr float NOVELTY_WEIGHT_END = 0.10f;
constexpr int   NOVELTY_START_GEN = 2;
constexpr float NOVELTY_FOOD_SCALE = 0.12f;
constexpr float NOVELTY_STEPS_SCALE = 0.0008f;
constexpr float NOVELTY_VISITED_SCALE = 0.0015f;

constexpr int   ANTI_LOOP_MEMORY_SIZE = 24;
constexpr int   EVAL_THREADS = 0;   // 0 = alle verfuegbaren Kerne nutzen

constexpr float CROSSOVER_PROB = 0.78f;
constexpr float CROSS_SPECIES_MATING_CHANCE = 0.04f;
constexpr float ELITE_MUTATION_SIGMA = 0.018f;

// Harte Grenzen gegen Reward-Hacking / Runaway-Episoden
constexpr float ENERGY_MAX = 250.0f;     // Energie deckeln -> Hunger bleibt relevant
constexpr int   MAX_EPISODE_STEPS = 1500; // feste Episodenlaenge -> Food vergleichbar

constexpr float MUTATION_FLOOR_FROM_GLOBAL = 0.6f; // Anteil der globalen Staerke als Untergrenze

// ----- Webserver -----
constexpr int   WEB_SERVER_PORT = 8080;

// ----- Globaler Curriculum-Schwierigkeitsgrad -----
// Wird von Evolution::train() gesetzt und von World::reset() gelesen.
extern float g_curriculum_difficulty;
