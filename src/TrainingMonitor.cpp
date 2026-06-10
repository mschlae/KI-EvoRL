// ============================================================
//  TrainingMonitor.cpp
// ============================================================
#include "TrainingMonitor.h"
#include "Config.h"
#include "World.h"
#include "Rng.h"
#include "MathUtils.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
    constexpr size_t MAX_HISTORY = 2000; // gedeckelt, damit das JSON nicht explodiert
    const char* MODELS_DIR = "models";   // Ablageort der gespeicherten Brains
    const uint32_t MODEL_MAGIC = 0x42524E31u; // "BRN1"

    // ---- kleine JSON-Helfer (genuegen fuer unsere flachen Bodies) ----
    bool extractFloat(const std::string& s, const std::string& key, float& out) {
        size_t p = s.find(std::string("\"") + key + "\"");
        if (p == std::string::npos) return false;
        size_t colon = s.find(':', p);
        if (colon == std::string::npos) return false;
        size_t start = s.find_first_not_of(" \t\r\n", colon + 1);
        if (start == std::string::npos) return false;
        size_t end = start;
        while (end < s.size()) {
            char c = s[end];
            if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == 'e' || c == 'E' || c == '+') ++end;
            else break;
        }
        if (end == start) return false;
        try { out = std::stof(s.substr(start, end - start)); return true; }
        catch (...) { return false; }
    }
    bool extractInt(const std::string& s, const std::string& key, long& out) {
        float f; if (!extractFloat(s, key, f)) return false; out = (long)f; return true;
    }
    bool extractString(const std::string& s, const std::string& key, std::string& out) {
        size_t p = s.find(std::string("\"") + key + "\"");
        if (p == std::string::npos) return false;
        size_t c = s.find(':', p);
        if (c == std::string::npos) return false;
        size_t q1 = s.find('"', c + 1);
        if (q1 == std::string::npos) return false;
        size_t q2 = s.find('"', q1 + 1);
        while (q2 != std::string::npos && q2 > 0 && s[q2 - 1] == '\\') q2 = s.find('"', q2 + 1);
        if (q2 == std::string::npos) return false;
        out = s.substr(q1 + 1, q2 - q1 - 1);
        return true;
    }
    std::vector<int> parseIntArray(const std::string& s, const std::string& key) {
        std::vector<int> v;
        size_t p = s.find(std::string("\"") + key + "\"");
        if (p == std::string::npos) return v;
        size_t lb = s.find('[', p);
        if (lb == std::string::npos) return v;
        size_t rb = s.find(']', lb);
        if (rb == std::string::npos) return v;
        size_t i = lb + 1;
        while (i < rb) {
            while (i < rb && !(std::isdigit((unsigned char)s[i]) || s[i] == '-')) i++;
            if (i >= rb) break;
            size_t j = i;
            while (j < rb && (std::isdigit((unsigned char)s[j]) || s[j] == '-')) j++;
            try { v.push_back(std::stoi(s.substr(i, j - i))); }
            catch (...) {}
            i = j;
        }
        return v;
    }
    std::string jsonEscape(const std::string& s) {
        std::string o; o.reserve(s.size() + 4);
        for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n' || c == '\r' || c == '\t') o += ' ';
            else o += c;
        }
        return o;
    }
    std::string nowString() {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
        return std::string(buf);
    }

    // ---- Episode -> Replay-JSON (Format identisch zu Evolution::recordReplay) ----
    // Laeuft auf der bereits vollstaendig initialisierten World.
    std::string buildReplayJson(World& world, Brain& brain, int label, int cap) {
        brain.resetHidden();

        std::ostringstream walls, food0, frames, events;
        bool fw = true, ff = true, ffr = true, fe = true;
        auto pushIdx = [](std::ostringstream& os, bool& first, int idx) {
            if (!first) os << ','; first = false; os << idx;
            };

        for (int i = 0; i < World::CELLS; i++) {
            if (world.grid[i] == WALL || world.grid[i] == OBSTACLE) pushIdx(walls, fw, i);
            if (world.grid[i] == FOOD)                              pushIdx(food0, ff, i);
        }
        auto pushFrame = [&](int x, int y) {
            if (!ffr) frames << ','; ffr = false; frames << '[' << x << ',' << y << ']';
            };
        pushFrame(world.agentX, world.agentY);

        std::array<bool, World::CELLS> prevFood = world.foodHere;

        int frame = 0;
        while (world.alive && world.steps < cap) {
            auto sens = world.getSensors();
            float logits[OUTPUTS];
            brain.forward(sens, logits);
            softmax_inplace(logits, OUTPUTS);
            float rv = randFloat(0, 1); int act = OUTPUTS - 1; float cum = 0;
            for (int i = 0; i < OUTPUTS; i++) { cum += logits[i]; if (rv < cum) { act = i; break; } }
            world.executeAction(act);
            frame++;
            pushFrame(world.agentX, world.agentY);
            for (int i = 0; i < World::CELLS; i++) {
                if (world.foodHere[i] != prevFood[i]) {
                    if (!fe) events << ','; fe = false;
                    events << '[' << frame << ',' << (world.foodHere[i] ? 1 : 0) << ',' << i << ']';
                    prevFood[i] = world.foodHere[i];
                }
            }
        }

        std::ostringstream os;
        os << '{'
            << "\"generation\":" << label << ','
            << "\"w\":" << GRID_W << ',' << "\"h\":" << GRID_H << ','
            << "\"food\":" << world.foodEaten << ',' << "\"steps\":" << world.steps << ','
            << "\"walls\":[" << walls.str() << "],"
            << "\"food0\":[" << food0.str() << "],"
            << "\"frames\":[" << frames.str() << "],"
            << "\"events\":[" << events.str() << "]"
            << '}';
        return os.str();
    }
} // namespace

TrainingMonitor::TrainingMonitor()
    : start_(std::chrono::steady_clock::now()) {
    loadModelsFromDisk();
}

void TrainingMonitor::update(const GenRecord& rec,
    float globalBestFitness,
    int   globalBestFood,
    float curriculum,
    bool  injecting,
    int   totalGenerations) {
    std::lock_guard<std::mutex> lk(m_);
    latest_ = rec;
    globalBestFitness_ = globalBestFitness;
    globalBestFood_ = globalBestFood;
    curriculum_ = curriculum;
    injecting_ = injecting;
    totalGenerations_ = totalGenerations;

    history_.push_back(rec);
    if (history_.size() > MAX_HISTORY) {
        history_.erase(history_.begin(), history_.begin() + (history_.size() - MAX_HISTORY));
    }
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
    std::lock_guard<std::mutex> lk(m_);

    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start_).count();

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
    os << "\"bestFitness\":" << latest_.best << ',';
    os << "\"avgFitness\":" << latest_.avg << ',';
    os << "\"avgFood\":" << latest_.avgFood << ',';
    os << "\"bestFood\":" << latest_.bestFood << ',';
    os << "\"bestSpf\":" << latest_.bestSpf << ',';
    os << "\"avgSpf\":" << latest_.avgSpf << ',';
    os << "\"mutation\":" << latest_.mutation << ',';
    os << "\"diversity\":" << latest_.diversity << ',';
    os << "\"species\":" << latest_.species << ',';
    os << "\"globalBestFitness\":" << globalBestFitness_ << ',';
    os << "\"globalBestFood\":" << globalBestFood_ << ',';
    os << "\"curriculum\":" << curriculum_ << ',';
    os << "\"injecting\":" << (injecting_ ? "true" : "false") << ',';
    // Feste Weltgroesse der Engine -> der Playground richtet sich danach.
    os << "\"gridW\":" << GRID_W << ',';
    os << "\"gridH\":" << GRID_H << ',';
    os << "\"hasBest\":" << (haveBest_ ? "true" : "false") << ',';

    auto writeArr = [&](const char* name, auto getter, bool last = false) {
        os << '"' << name << "\":[";
        for (size_t i = 0; i < history_.size(); ++i) {
            if (i) os << ',';
            os << getter(history_[i]);
        }
        os << ']';
        if (!last) os << ',';
        };

    os << "\"history\":{";
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

    os << ",\"config\":" << configJson();
    os << '}';
    return os.str();
}

std::string TrainingMonitor::configJson() const {
    auto cfg = getRuntimeConfig();
    std::ostringstream os;
    os.setf(std::ios::fixed); os.precision(6);
    os << '{';
    os << "\"mutation\":" << cfg.mutation << ',';
    os << "\"foodReward\":" << cfg.foodReward << ',';
    os << "\"stepCost\":" << cfg.stepCost << ',';
    os << "\"rewardWeight\":" << cfg.rewardWeight << ',';
    os << "\"exploreBonus\":" << cfg.exploreBonus << ',';
    os << "\"efficiencyBonus\":" << cfg.efficiencyBonus;
    os << '}';
    return os.str();
}

bool TrainingMonitor::applyConfigJson(const std::string& jsonStr) {
    RuntimeConfig cfg = getRuntimeConfig();
    float v;
    bool any = false;
    if (extractFloat(jsonStr, "mutation", v)) { cfg.mutation = v; any = true; }
    if (extractFloat(jsonStr, "foodReward", v)) { cfg.foodReward = v; any = true; }
    if (extractFloat(jsonStr, "stepCost", v)) { cfg.stepCost = v; any = true; }
    if (extractFloat(jsonStr, "rewardWeight", v)) { cfg.rewardWeight = v; any = true; }
    if (extractFloat(jsonStr, "exploreBonus", v)) { cfg.exploreBonus = v; any = true; }
    if (extractFloat(jsonStr, "efficiencyBonus", v)) { cfg.efficiencyBonus = v; any = true; }
    if (!any) return false;
    setRuntimeConfig(cfg);
    return true;
}

// ============================================================
//  Modell-/Playground-API
// ============================================================

void TrainingMonitor::setBestBrain(const Brain& b, int generation) {
    std::lock_guard<std::mutex> lk(m_);
    bestBrain_ = b;
    bestGen_ = generation;
    haveBest_ = true;
}

bool TrainingMonitor::hasBestBrain() const {
    std::lock_guard<std::mutex> lk(m_);
    return haveBest_;
}

bool TrainingMonitor::takePendingLoad(Brain& out) {
    std::lock_guard<std::mutex> lk(m_);
    if (!havePending_) return false;
    out = pendingBrain_;
    havePending_ = false;
    return true;
}

bool TrainingMonitor::requestModelLoad(const std::string& id) {
    std::lock_guard<std::mutex> lk(m_);
    if (id == "live") {
        if (!haveBest_) return false;
        pendingBrain_ = bestBrain_;
        havePending_ = true;
        return true;
    }
    for (const auto& m : models_) {
        if (m.id == id) {
            pendingBrain_ = m.brain;
            havePending_ = true;
            return true;
        }
    }
    return false;
}

static void appendModelObject(std::ostringstream& os,
    const std::string& id, const std::string& name,
    int gen, float fit, int food, int steps,
    const std::string* savedAt) {
    os.setf(std::ios::fixed); os.precision(2);
    os << '{'
        << "\"id\":\"" << jsonEscape(id) << "\","
        << "\"name\":\"" << jsonEscape(name) << "\","
        << "\"generation\":" << gen << ','
        << "\"fitness\":" << fit << ','
        << "\"food\":" << food << ','
        << "\"steps\":" << steps << ',';
    if (savedAt) os << "\"savedAt\":\"" << jsonEscape(*savedAt) << "\"";
    else         os << "\"savedAt\":null";
    os << '}';
}

std::string TrainingMonitor::modelsJson() const {
    std::lock_guard<std::mutex> lk(m_);
    std::ostringstream os;
    os << '[';
    bool first = true;
    if (haveBest_) {
        appendModelObject(os, "live", "Aktuell bestes Modell (live)",
            bestGen_, bestBrain_.baseFitness, bestBrain_.food, bestBrain_.steps, nullptr);
        first = false;
    }
    // neueste zuerst
    for (auto it = models_.rbegin(); it != models_.rend(); ++it) {
        if (!first) os << ',';
        first = false;
        appendModelObject(os, it->id, it->name, it->generation,
            it->fitness, it->food, it->steps, &it->savedAt);
    }
    os << ']';
    return os.str();
}

std::string TrainingMonitor::saveCurrentModelJson(const std::string& body) {
    SavedModel m;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!haveBest_) return "{\"ok\":false,\"error\":\"no_model\"}";
        m.brain = bestBrain_;
        m.generation = bestGen_;
        m.fitness = bestBrain_.baseFitness;
        m.food = bestBrain_.food;
        m.steps = bestBrain_.steps;
    }
    std::string name;
    if (!extractString(body, "name", name) || name.empty())
        name = "Gen " + std::to_string(m.generation);
    m.name = name;
    m.savedAt = nowString();

    // ID aus Zeit + Zaehler
    static std::atomic<uint32_t> counter{ 0 };
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m.id = "m" + std::to_string(ms) + "_" + std::to_string(counter.fetch_add(1));

    // Persistieren (Metadaten .meta + Rohgewichte .brain). Fehlschlaege sind
    // nicht fatal: das Modell bleibt zumindest fuer diese Sitzung verfuegbar.
    try {
        std::filesystem::create_directories(MODELS_DIR);
        std::filesystem::path base = std::filesystem::path(MODELS_DIR) / m.id;

        std::ofstream meta(base.string() + ".meta", std::ios::binary | std::ios::trunc);
        if (meta) {
            meta.setf(std::ios::fixed); meta.precision(4);
            meta << '{'
                << "\"id\":\"" << jsonEscape(m.id) << "\","
                << "\"name\":\"" << jsonEscape(m.name) << "\","
                << "\"generation\":" << m.generation << ','
                << "\"fitness\":" << m.fitness << ','
                << "\"food\":" << m.food << ','
                << "\"steps\":" << m.steps << ','
                << "\"savedAt\":\"" << jsonEscape(m.savedAt) << "\"}";
        }
        std::ofstream br(base.string() + ".brain", std::ios::binary | std::ios::trunc);
        if (br) {
            uint32_t magic = MODEL_MAGIC;
            uint32_t sz = (uint32_t)sizeof(Brain);
            br.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            br.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
            br.write(reinterpret_cast<const char*>(&m.brain), sizeof(Brain));
        }
    }
    catch (...) { /* IO-Fehler ignorieren (Modell bleibt in-memory) */ }

    {
        std::lock_guard<std::mutex> lk(m_);
        models_.push_back(m);
    }

    std::ostringstream os;
    os << "{\"ok\":true,";
    os << "\"id\":\"" << jsonEscape(m.id) << "\",";
    os << "\"name\":\"" << jsonEscape(m.name) << "\",";
    os.setf(std::ios::fixed); os.precision(2);
    os << "\"generation\":" << m.generation << ',';
    os << "\"fitness\":" << m.fitness << ',';
    os << "\"food\":" << m.food << ',';
    os << "\"steps\":" << m.steps << ',';
    os << "\"savedAt\":\"" << jsonEscape(m.savedAt) << "\"}";
    return os.str();
}

std::string TrainingMonitor::modelReplayJson(const std::string& id) const {
    Brain brain;
    int gen = 0;
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (id == "live") {
            if (haveBest_) { brain = bestBrain_; gen = bestGen_; found = true; }
        }
        else {
            for (const auto& m : models_) {
                if (m.id == id) { brain = m.brain; gen = m.generation; found = true; break; }
            }
        }
    }
    if (!found) return "{\"frames\":[]}";

    // Deterministische Standardkarte; danach die Episode aufzeichnen.
    set_local_seed(20240517ull);
    World world;
    world.earlyStopLimit = MAX_STEPS;
    return buildReplayJson(world, brain, gen, MAX_STEPS);
}

std::string TrainingMonitor::runPlaygroundJson(const std::string& body) {
    // ---- Modell beschaffen ----
    std::string id;
    extractString(body, "model", id);
    Brain brain;
    int gen = 0;
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (id == "live") {
            if (haveBest_) { brain = bestBrain_; gen = bestGen_; found = true; }
        }
        else {
            for (const auto& m : models_) {
                if (m.id == id) { brain = m.brain; gen = m.generation; found = true; break; }
            }
        }
    }
    if (!found) return "{\"ok\":false,\"error\":\"model_not_found\"}";

    // ---- Karte aus dem Body lesen ----
    long w = GRID_W, h = GRID_H, maxSteps = 1000;
    extractInt(body, "w", w);
    extractInt(body, "h", h);
    extractInt(body, "maxSteps", maxSteps);

    // Die Engine arbeitet mit fester Weltgroesse -> Maße muessen passen.
    if (w != GRID_W || h != GRID_H) {
        std::ostringstream e;
        e << "{\"ok\":false,\"error\":\"grid_size\",\"gridW\":" << GRID_W
            << ",\"gridH\":" << GRID_H << "}";
        return e.str();
    }
    if (maxSteps < 1) maxSteps = 1;
    if (maxSteps > MAX_STEPS) maxSteps = MAX_STEPS;

    std::vector<int> walls = parseIntArray(body, "walls");
    std::vector<int> food = parseIntArray(body, "food");
    std::vector<int> start = parseIntArray(body, "start");
    int ax = (start.size() >= 1) ? start[0] : GRID_W / 2;
    int ay = (start.size() >= 2) ? start[1] : GRID_H / 2;
    if (ax < 1) ax = 1; if (ax > GRID_W - 2) ax = GRID_W - 2;
    if (ay < 1) ay = 1; if (ay > GRID_H - 2) ay = GRID_H - 2;

    float frVal, scVal; bool haveFr = extractFloat(body, "foodReward", frVal);
    bool haveSc = extractFloat(body, "stepCost", scVal);

    // ---- Runtime-Config temporaer ueberschreiben (foodReward/stepCost) ----
    // Hinweis: getRuntimeConfig ist global; laeuft parallel ein Training, wirkt
    // die Aenderung kurz auch dort. Da der Playground typischerweise zwischen
    // Laeufen genutzt wird, ist das akzeptabel; die Werte werden danach
    // zuverlaessig zurueckgesetzt. Ein dedizierter Mutex serialisiert Laeufe.
    static std::mutex pgMutex;
    std::lock_guard<std::mutex> pglk(pgMutex);

    RuntimeConfig saved = getRuntimeConfig();
    RuntimeConfig tmp = saved;
    if (haveFr) tmp.foodReward = frVal;
    if (haveSc) tmp.stepCost = scVal;
    setRuntimeConfig(tmp);

    std::string result;
    try {
        set_local_seed(777ull);   // deterministische Respawns

        World world;
        world.earlyStopLimit = (int)maxSteps;

        // Karte setzen (Rand immer Wand)
        world.grid.fill(EMPTY);
        world.biome.fill(0);
        std::memset(world.visitedZones, 0, sizeof(world.visitedZones));
        for (int x = 0; x < GRID_W; x++) { world.grid[x] = WALL; world.grid[(GRID_H - 1) * GRID_W + x] = WALL; }
        for (int y = 0; y < GRID_H; y++) { world.grid[y * GRID_W] = WALL; world.grid[y * GRID_W + GRID_W - 1] = WALL; }
        for (int idx : walls)
            if (idx >= 0 && idx < World::CELLS) world.grid[idx] = WALL;
        for (int idx : food)
            if (idx >= 0 && idx < World::CELLS && world.grid[idx] != WALL) world.grid[idx] = FOOD;

        // Startzelle frei machen
        int agIdx = ay * GRID_W + ax;
        if (world.grid[agIdx] == WALL || world.grid[agIdx] == OBSTACLE) {
            // naechste freie Zelle suchen
            for (int i = 0; i < World::CELLS && (world.grid[agIdx] == WALL || world.grid[agIdx] == OBSTACLE); i++) {
                if (world.grid[i] != WALL && world.grid[i] != OBSTACLE) { agIdx = i; }
            }
        }
        if (world.grid[agIdx] == FOOD) world.grid[agIdx] = EMPTY;
        world.agentX = agIdx % GRID_W;
        world.agentY = agIdx / GRID_W;

        world.rebuildFoodList();

        // Buchhaltung wie in World::reset() initialisieren
        world.energy = START_ENERGY;
        world.alive = true;
        world.steps = 0;
        world.foodEaten = 0;
        world.totalReward = 0;
        world.lastAction = 0;
        world.visited.reset();
        world.visited.set(world.agentY * GRID_W + world.agentX);
        world.recentIdx = 0; world.recentSize = 0;
        world.lastPositions.fill({ 0, 0 }); world.lastPosCount = 0;
        world.stepsSinceProgress = 0; world.lastVisitedCount = 1; world.lastFoodCount = 0;
        world.prevFoodDist = world.nearestFoodDist();

        result = buildReplayJson(world, brain, gen, (int)maxSteps);
    }
    catch (...) {
        setRuntimeConfig(saved);
        return "{\"ok\":false,\"error\":\"simulation_failed\"}";
    }
    setRuntimeConfig(saved);
    return result;
}

void TrainingMonitor::loadModelsFromDisk() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(MODELS_DIR, ec)) return;

    for (const auto& entry : fs::directory_iterator(MODELS_DIR, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const auto& p = entry.path();
        if (p.extension() != ".meta") continue;

        std::ifstream meta(p, std::ios::binary);
        if (!meta) continue;
        std::stringstream ss; ss << meta.rdbuf();
        std::string js = ss.str();

        SavedModel m;
        extractString(js, "id", m.id);
        extractString(js, "name", m.name);
        extractString(js, "savedAt", m.savedAt);
        long lv;
        if (extractInt(js, "generation", lv)) m.generation = (int)lv;
        if (extractInt(js, "food", lv)) m.food = (int)lv;
        if (extractInt(js, "steps", lv)) m.steps = (int)lv;
        float fv; if (extractFloat(js, "fitness", fv)) m.fitness = fv;
        if (m.id.empty()) m.id = p.stem().string();

        // zugehoerige Gewichte laden
        fs::path brainPath = p; brainPath.replace_extension(".brain");
        std::ifstream br(brainPath, std::ios::binary);
        if (!br) continue;
        uint32_t magic = 0, sz = 0;
        br.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        br.read(reinterpret_cast<char*>(&sz), sizeof(sz));
        if (magic != MODEL_MAGIC || sz != (uint32_t)sizeof(Brain)) continue; // Format passt nicht
        br.read(reinterpret_cast<char*>(&m.brain), sizeof(Brain));
        if (!br) continue;

        models_.push_back(std::move(m));
    }
}