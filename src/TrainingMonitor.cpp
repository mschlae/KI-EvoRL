// ============================================================
//  TrainingMonitor.cpp
// ============================================================
#include "TrainingMonitor.h"
#include "Config.h"
#include "Json.h"
#include "Replay.h"
#include "Rng.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <type_traits>

namespace {
    constexpr size_t MAX_HISTORY = 2000;   // gedeckelt, damit das JSON nicht explodiert
    constexpr size_t NAME_MAX_LEN = 80;
    constexpr int    PLAYGROUND_MAX_STEPS = 20000;  // Obergrenze pro Playground-/Replay-Lauf

    // NaN/Inf sind kein gueltiges JSON -> durch 0 ersetzen.
    float safe(float v) { return std::isfinite(v) ? v : 0.0f; }

    // Konfigurationswerte aus dem Netz muessen endlich und im sinnvollen Bereich liegen.
    bool validCfg(float v) { return std::isfinite(v) && v > -1e6f && v < 1e6f; }

    void appendModelObject(std::ostringstream& os, const std::string& id, const std::string& name,
        int gen, float fit, int food, int steps, const std::string* savedAt) {
        os << '{'
            << "\"id\":\"" << json::escape(id) << "\","
            << "\"name\":\"" << json::escape(name) << "\","
            << "\"generation\":" << gen << ','
            << "\"fitness\":" << safe(fit) << ','
            << "\"food\":" << food << ','
            << "\"steps\":" << steps << ',';
        if (savedAt) os << "\"savedAt\":\"" << json::escape(*savedAt) << "\"";
        else         os << "\"savedAt\":null";
        os << '}';
    }
} // namespace

TrainingMonitor::TrainingMonitor(std::string modelsDir)
    : modelsDir_(std::move(modelsDir)), start_(std::chrono::steady_clock::now()) {
    models_ = modelstore::loadAll(modelsDir_);
    rebuildHistoryJson();   // leere Arrays, damit das Dashboard vor der ersten Generation nichts Fehlendes sieht
}

void TrainingMonitor::update(const GenRecord& rec, float globalBestFitness, int globalBestFood,
    float curriculum, bool injecting, int totalGenerations) {
    std::lock_guard<std::mutex> lk(m_);
    latest_ = rec;
    globalBestFitness_ = globalBestFitness;
    globalBestFood_ = globalBestFood;
    curriculum_ = curriculum;
    injecting_ = injecting;
    totalGenerations_ = totalGenerations;

    history_.push_back(rec);
    if (history_.size() > MAX_HISTORY)
        history_.erase(history_.begin(), history_.begin() + (history_.size() - MAX_HISTORY));
    rebuildHistoryJson();
}

void TrainingMonitor::rebuildHistoryJson() {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(3);
    auto writeArr = [&](const char* name, auto getter, bool last = false) {
        os << '"' << name << "\":[";
        for (size_t i = 0; i < history_.size(); ++i) {
            if (i) os << ',';
            auto v = getter(history_[i]);
            if constexpr (std::is_floating_point_v<decltype(v)>) os << safe(v);
            else                                                 os << v;
        }
        os << ']';
        if (!last) os << ',';
    };
    os << '{';
    writeArr("gen", [](const GenRecord& r) { return r.gen; });
    writeArr("best", [](const GenRecord& r) { return r.best; });
    writeArr("avg", [](const GenRecord& r) { return r.avg; });
    writeArr("avgFood", [](const GenRecord& r) { return r.avgFood; });
    writeArr("bestFood", [](const GenRecord& r) { return r.bestFood; });
    writeArr("bestSpf", [](const GenRecord& r) { return r.bestSpf; });
    writeArr("avgSpf", [](const GenRecord& r) { return r.avgSpf; });
    writeArr("diversity", [](const GenRecord& r) { return r.diversity; });
    writeArr("mutation", [](const GenRecord& r) { return r.mutation; }, /*last=*/true);
    os << '}';
    historyJson_ = os.str();
}

void TrainingMonitor::setFinished() {
    finished_.store(true);
    running_.store(false);
}

void TrainingMonitor::setReplay(std::string json) {
    std::lock_guard<std::mutex> lk(m_);
    replay_ = std::move(json);
}

std::string TrainingMonitor::replayJson() const {
    std::lock_guard<std::mutex> lk(m_);
    if (replay_.empty()) return "{\"frames\":[]}";
    return replay_;
}

std::string TrainingMonitor::toJson() const {
    std::string cfg = configJson();   // eigener Lock (Config.cpp), nicht unter m_

    std::lock_guard<std::mutex> lk(m_);
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();

    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(3);
    os << '{';
    os << "\"running\":" << (running_.load() ? "true" : "false") << ',';
    os << "\"finished\":" << (finished_.load() ? "true" : "false") << ',';
    os << "\"stopRequested\":" << (stopRequested_.load() ? "true" : "false") << ',';
    os << "\"generation\":" << latest_.gen << ',';
    os << "\"totalGenerations\":" << totalGenerations_ << ',';
    os << "\"elapsedSeconds\":" << elapsed << ',';
    os << "\"bestFitness\":" << safe(latest_.best) << ',';
    os << "\"avgFitness\":" << safe(latest_.avg) << ',';
    os << "\"avgFood\":" << safe(latest_.avgFood) << ',';
    os << "\"bestFood\":" << latest_.bestFood << ',';
    os << "\"bestSpf\":" << safe(latest_.bestSpf) << ',';
    os << "\"avgSpf\":" << safe(latest_.avgSpf) << ',';
    os << "\"mutation\":" << safe(latest_.mutation) << ',';
    os << "\"diversity\":" << safe(latest_.diversity) << ',';
    os << "\"species\":" << latest_.species << ',';
    os << "\"globalBestFitness\":" << safe(globalBestFitness_) << ',';
    os << "\"globalBestFood\":" << globalBestFood_ << ',';
    os << "\"curriculum\":" << safe(curriculum_) << ',';
    os << "\"injecting\":" << (injecting_ ? "true" : "false") << ',';
    // Feste Weltgroesse der Engine -> der Playground richtet sich danach.
    os << "\"gridW\":" << GRID_W << ',';
    os << "\"gridH\":" << GRID_H << ',';
    os << "\"hasBest\":" << (bestBrain_ ? "true" : "false") << ',';
    os << "\"history\":" << historyJson_ << ',';
    os << "\"config\":" << cfg;
    os << '}';
    return os.str();
}

std::string TrainingMonitor::configJson() const {
    auto cfg = getRuntimeConfig();
    std::ostringstream os;
    os.setf(std::ios::fixed); os.precision(6);
    os << '{'
        << "\"mutation\":" << cfg.mutation << ','
        << "\"foodReward\":" << cfg.foodReward << ','
        << "\"stepCost\":" << cfg.stepCost << ','
        << "\"rewardWeight\":" << cfg.rewardWeight << ','
        << "\"exploreBonus\":" << cfg.exploreBonus << ','
        << "\"efficiencyBonus\":" << cfg.efficiencyBonus
        << '}';
    return os.str();
}

bool TrainingMonitor::applyConfigJson(const std::string& body) {
    RuntimeConfig cfg = getRuntimeConfig();
    bool any = false;
    auto take = [&](const char* key, float& dst, float lo, float hi) {
        float v;
        if (json::extractFloat(body, key, v) && validCfg(v)) {
            dst = std::max(lo, std::min(hi, v));
            any = true;
        }
    };
    take("mutation", cfg.mutation, MUTATION_STRENGTH_MIN, 1.0f);   // < Minimum waere wirkungslos
    take("foodReward", cfg.foodReward, 0.0f, 100000.0f);
    take("stepCost", cfg.stepCost, 0.0f, 1000.0f);
    take("rewardWeight", cfg.rewardWeight, 0.0f, 1000.0f);
    take("exploreBonus", cfg.exploreBonus, 0.0f, 1000.0f);
    take("efficiencyBonus", cfg.efficiencyBonus, 0.0f, 1000.0f);
    if (!any) return false;
    setRuntimeConfig(cfg);
    return true;
}

// ============================================================
//  Modell-/Playground-API
// ============================================================

void TrainingMonitor::setBestBrain(const Brain& b, int generation) {
    auto copy = std::make_shared<Brain>(b);   // Kopie ausserhalb des Locks
    std::lock_guard<std::mutex> lk(m_);
    bestBrain_ = std::move(copy);
    bestGen_ = generation;
    bestFitness_ = b.baseFitness;
    bestFood_ = b.food;
    bestSteps_ = b.steps;
}

bool TrainingMonitor::hasBestBrain() const {
    std::lock_guard<std::mutex> lk(m_);
    return (bool)bestBrain_;
}

bool TrainingMonitor::takePendingLoad(Brain& out) {
    std::shared_ptr<const Brain> p;
    {
        std::lock_guard<std::mutex> lk(m_);
        p = std::move(pendingBrain_);
        pendingBrain_.reset();
    }
    if (!p) return false;
    out = *p;
    return true;
}

bool TrainingMonitor::requestModelLoad(const std::string& id) {
    std::lock_guard<std::mutex> lk(m_);
    if (id == "live") {
        if (!bestBrain_) return false;
        pendingBrain_ = bestBrain_;
        return true;
    }
    for (const auto& m : models_)
        if (m.id == id) { pendingBrain_ = m.brain; return true; }
    return false;
}

bool TrainingMonitor::findBrain(const std::string& id, std::unique_ptr<Brain>& out, int& gen) const {
    std::shared_ptr<const Brain> src;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (id == "live") {
            src = bestBrain_; gen = bestGen_;
        }
        else {
            for (const auto& m : models_)
                if (m.id == id) { src = m.brain; gen = m.generation; break; }
        }
    }
    if (!src) return false;
    out = std::make_unique<Brain>(*src);   // forward() veraendert den Hidden-State -> private Kopie
    return true;
}

std::string TrainingMonitor::modelsJson() const {
    std::lock_guard<std::mutex> lk(m_);
    std::ostringstream os;
    os.setf(std::ios::fixed); os.precision(2);
    os << '[';
    bool first = true;
    if (bestBrain_) {
        appendModelObject(os, "live", "Aktuell bestes Modell (live)",
            bestGen_, bestFitness_, bestFood_, bestSteps_, nullptr);
        first = false;
    }
    for (auto it = models_.rbegin(); it != models_.rend(); ++it) {   // neueste zuerst
        if (!first) os << ',';
        first = false;
        appendModelObject(os, it->id, it->name, it->generation, it->fitness, it->food, it->steps, &it->savedAt);
    }
    os << ']';
    return os.str();
}

std::string TrainingMonitor::saveCurrentModelJson(const std::string& body) {
    SavedModel m;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!bestBrain_) return "{\"ok\":false,\"error\":\"no_model\"}";
        m.brain = bestBrain_;
        m.generation = bestGen_;
        m.fitness = bestFitness_;
        m.food = bestFood_;
        m.steps = bestSteps_;
    }
    std::string name;
    if (!json::extractString(body, "name", name)) name.clear();
    // Steuerzeichen entfernen, Laenge begrenzen
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return c < 0x20; }), name.end());
    if (name.size() > NAME_MAX_LEN) name.resize(NAME_MAX_LEN);
    if (name.empty()) name = "Gen " + std::to_string(m.generation);
    m.name = name;
    m.savedAt = modelstore::timestampNow();
    m.id = modelstore::newId();

    // IO-Fehler sind nicht fatal: das Modell bleibt fuer diese Sitzung verfuegbar.
    bool persisted = modelstore::save(modelsDir_, m);
    {
        std::lock_guard<std::mutex> lk(m_);
        models_.push_back(m);
    }

    std::ostringstream os;
    os.setf(std::ios::fixed); os.precision(2);
    os << "{\"ok\":true,\"persisted\":" << (persisted ? "true" : "false") << ',';
    os << "\"id\":\"" << json::escape(m.id) << "\","
        << "\"name\":\"" << json::escape(m.name) << "\","
        << "\"generation\":" << m.generation << ','
        << "\"fitness\":" << safe(m.fitness) << ','
        << "\"food\":" << m.food << ','
        << "\"steps\":" << m.steps << ','
        << "\"savedAt\":\"" << json::escape(m.savedAt) << "\"}";
    return os.str();
}

std::string TrainingMonitor::modelReplayJson(const std::string& id) const {
    std::unique_ptr<Brain> brain;
    int gen = 0;
    if (!findBrain(id, brain, gen)) return "{\"frames\":[]}";

    // Deterministische Standardkarte (volle Schwierigkeit, aktuelle Konfiguration).
    set_local_seed(20240517ull);
    World world(1.0f, getRuntimeConfig());
    world.earlyStopLimit = MAX_EPISODE_STEPS;
    return buildReplayJson(world, *brain, gen, MAX_EPISODE_STEPS);
}

std::string TrainingMonitor::runPlaygroundJson(const std::string& body) const {
    std::string id;
    json::extractString(body, "model", id);
    std::unique_ptr<Brain> brain;
    int gen = 0;
    if (!findBrain(id, brain, gen)) return "{\"ok\":false,\"error\":\"model_not_found\"}";

    long w = GRID_W, h = GRID_H, maxSteps = 1000;
    json::extractInt(body, "w", w);
    json::extractInt(body, "h", h);
    json::extractInt(body, "maxSteps", maxSteps);

    // Die Engine arbeitet mit fester Weltgroesse -> Masse muessen passen.
    if (w != GRID_W || h != GRID_H) {
        std::ostringstream e;
        e << "{\"ok\":false,\"error\":\"grid_size\",\"gridW\":" << GRID_W << ",\"gridH\":" << GRID_H << "}";
        return e.str();
    }
    maxSteps = std::max<long>(1, std::min<long>(maxSteps, PLAYGROUND_MAX_STEPS));

    CustomMap map;
    map.walls = json::parseIntArray(body, "walls");
    map.food = json::parseIntArray(body, "food");
    std::vector<int> start = json::parseIntArray(body, "start");
    if (start.size() >= 2) { map.startX = start[0]; map.startY = start[1]; }

    // Belohnungs-Parameter gelten nur fuer diesen Lauf (die globale Konfiguration
    // bleibt unangetastet -> keine Wechselwirkung mit dem laufenden Training).
    RuntimeConfig cfg = getRuntimeConfig();
    float v;
    if (json::extractFloat(body, "foodReward", v) && validCfg(v)) cfg.foodReward = std::max(0.0f, v);
    if (json::extractFloat(body, "stepCost", v) && validCfg(v)) cfg.stepCost = std::max(0.0f, v);

    set_local_seed(777ull);   // deterministische Respawns
    World world(1.0f, cfg, /*generate=*/false);
    world.earlyStopLimit = (int)maxSteps;
    world.loadCustomMap(map);
    return buildReplayJson(world, *brain, gen, (int)maxSteps);
}
