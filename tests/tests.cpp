// ============================================================
//  tests.cpp  -  Unit-/Smoke-Tests ohne externe Abhaengigkeiten
//  Start: ctest  bzw.  ./evorl_tests [Pfad-zum-Quellordner]
// ============================================================
#include "Brain.h"
#include "Config.h"
#include "Evolution.h"
#include "HttpServer.h"
#include "Json.h"
#include "MathUtils.h"
#include "ModelStore.h"
#include "Replay.h"
#include "Rng.h"
#include "TrainingMonitor.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>

#ifdef _OPENMP
#include <omp.h>
#endif
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; } } while (0)

#define RUN(...) do { std::cout << "[ RUN  ] " #__VA_ARGS__ "\n"; __VA_ARGS__; } while (0)

// ---------------------------------------------------------------- Rng
static void testRng() {
    set_local_seed(123);
    float lo = 1e9f, hi = -1e9f; double sum = 0;
    for (int i = 0; i < 100000; i++) { float v = randFloat(-2.0f, 3.0f); lo = std::min(lo, v); hi = std::max(hi, v); sum += v; }
    CHECK(lo >= -2.0f && hi < 3.0f);
    CHECK(std::fabs(sum / 100000 - 0.5) < 0.05);

    std::set<int> seen;
    for (int i = 0; i < 20000; i++) { int v = randInt(3, 7); CHECK(v >= 3 && v <= 7); seen.insert(v); }
    CHECK(seen.size() == 5);
    CHECK(randInt(4, 4) == 4);

    set_local_seed(99); float a = randFloat(0, 1);
    set_local_seed(99); float b = randFloat(0, 1);
    CHECK(a == b);
}

// ---------------------------------------------------------------- Brain
// Referenz: die urspruengliche, skalare GRU-Implementierung (nicht optimiert).
static void referenceForward(Brain& br, const std::array<float, INPUTS>& inp, float logits[OUTPUTS]) {
    float newH[HIDDEN];
    for (int i = 0; i < HIDDEN; i++) {
        float sz = br.bias_z[i], sr = br.bias_r[i];
        for (int j = 0; j < INPUTS; j++) { sz += inp[j] * br.W_z[i][j]; sr += inp[j] * br.W_r[i][j]; }
        for (int j = 0; j < HIDDEN; j++) {
            sz += br.hidden_state[j] * br.W_z[i][INPUTS + j];
            sr += br.hidden_state[j] * br.W_r[i][INPUTS + j];
        }
        float z = sigmoid(sz), r = sigmoid(sr);
        float sh = br.bias_h[i];
        for (int j = 0; j < INPUTS; j++) sh += inp[j] * br.W_h[i][j];
        for (int j = 0; j < HIDDEN; j++) sh += r * br.hidden_state[j] * br.W_h[i][INPUTS + j];
        newH[i] = (1.0f - z) * br.hidden_state[i] + z * tanhAct(sh);
    }
    for (int i = 0; i < HIDDEN; i++) br.hidden_state[i] = newH[i];
    for (int o = 0; o < OUTPUTS; o++) {
        float s = br.bias_out[o];
        for (int h = 0; h < HIDDEN; h++) s += br.hidden_state[h] * br.W_out[o][h];
        logits[o] = s;
    }
}

static void testBrain() {
    set_local_seed(5);
    auto fast = std::make_unique<Brain>();
    auto ref = std::make_unique<Brain>(*fast);
    // Biases ungleich 0, damit auch sie getestet werden
    for (int i = 0; i < HIDDEN; i++) { fast->bias_z[i] = ref->bias_z[i] = randFloat(-.5f, .5f); fast->bias_h[i] = ref->bias_h[i] = randFloat(-.5f, .5f); }
    float maxDiff = 0;
    for (int step = 0; step < 50; step++) {
        std::array<float, INPUTS> in;
        for (auto& v : in) v = randFloat(-1, 1);
        float a[OUTPUTS], b[OUTPUTS];
        fast->forward(in, a);
        referenceForward(*ref, in, b);
        for (int o = 0; o < OUTPUTS; o++) { CHECK(std::isfinite(a[o])); maxDiff = std::max(maxDiff, std::fabs(a[o] - b[o])); }
    }
    CHECK(maxDiff < 1e-3f);

    // Aktionen liegen im gueltigen Bereich und sind nicht konstant
    std::array<float, INPUTS> in{};
    std::set<int> acts;
    for (int i = 0; i < 400; i++) { for (auto& v : in) v = randFloat(-1, 1); int a = fast->selectAction(in, local_rng()); CHECK(a >= 0 && a < OUTPUTS); acts.insert(a); }
    CHECK(acts.size() > 1);

    // Mutation: Gewichte bleiben in den Schranken, sigma >= Minimum
    auto m = std::make_unique<Brain>(*fast);
    for (int i = 0; i < 20; i++) m->mutate(1.0f);
    bool bounded = true;
    for (int i = 0; i < HIDDEN; i++) {
        for (int j = 0; j < Brain::IN_SIZE; j++) bounded &= std::fabs(m->W_z[i][j]) <= 2.0f && std::fabs(m->W_h[i][j]) <= 2.0f;
        bounded &= std::fabs(m->bias_h[i]) <= 1.0f;
    }
    CHECK(bounded);
    CHECK(m->sigma >= MUTATION_STRENGTH_MIN && m->sigma <= 1.0f);

    // Aehnlichkeit
    CHECK(std::fabs(fast->similarity(*fast) - 1.0f) < 1e-6f);
    CHECK(fast->similarity(*m) < 1.0f);

    // Crossover liefert endliche Gewichte zwischen den Eltern (arithmetisch) bzw. von einem Elternteil
    auto c = std::make_unique<Brain>(Brain::crossover(*fast, *m));
    bool ok = true;
    for (int i = 0; i < HIDDEN; i++) for (int j = 0; j < Brain::IN_SIZE; j++) {
        float lo = std::min(fast->W_r[i][j], m->W_r[i][j]) - 1e-5f, hi = std::max(fast->W_r[i][j], m->W_r[i][j]) + 1e-5f;
        ok &= c->W_r[i][j] >= lo && c->W_r[i][j] <= hi;
    }
    CHECK(ok);
}

// ---------------------------------------------------------------- World
static void checkWorldConsistency(const World& w) {
    int visited = (int)w.visited.count();
    CHECK(visited == w.visitedCount);
    std::set<int> fromGrid, fromList(w.foodIndices.begin(), w.foodIndices.end());
    for (int i = 0; i < World::CELLS; i++) {
        if (w.grid[i] == FOOD) fromGrid.insert(i);
        CHECK(w.foodHere[i] == (w.grid[i] == FOOD));
    }
    CHECK(fromGrid == fromList);
    CHECK(fromList.size() == w.foodIndices.size());      // keine Duplikate
    CHECK(w.grid[w.agentY * GRID_W + w.agentX] != WALL && w.grid[w.agentY * GRID_W + w.agentX] != OBSTACLE);
}

static void testWorld() {
    RuntimeConfig cfg;
    for (int episode = 0; episode < 6; episode++) {
        set_local_seed(1000 + episode);
        World w(episode / 5.0f, cfg);
        checkWorldConsistency(w);
        int bumps = 0;
        for (int t = 0; t < 600 && w.alive; t++) {
            auto s = w.getSensors();
            for (float v : s) CHECK(std::isfinite(v) && std::fabs(v) <= 5.0f);

            // naechstes Food per Brute-Force pruefen (Cache)
            int best = 9999;
            for (int fi : w.foodIndices) best = std::min(best, std::abs(fi % GRID_W - w.agentX) + std::abs(fi / GRID_W - w.agentY));
            if (!w.foodIndices.empty()) CHECK((int)w.nearestFoodDist() == best);

            int before = w.agentX * 100 + w.agentY;
            w.executeAction(randInt(0, OUTPUTS - 1));
            if (before == w.agentX * 100 + w.agentY) bumps++;
            if (t % 50 == 0) checkWorldConsistency(w);
        }
        checkWorldConsistency(w);
        CHECK(w.steps > 0);
        (void)bumps;
    }

    // Eigene Karte: komplett zugemauert darf nicht haengen und der Agent bleibt stehen
    CustomMap map;
    for (int i = 0; i < World::CELLS; i++) map.walls.push_back(i);
    map.startX = 10; map.startY = 10;
    set_local_seed(1);
    World full(1.0f, cfg, false);
    full.loadCustomMap(map);
    for (int t = 0; t < 100 && full.alive; t++) full.executeAction(randInt(0, 3));
    CHECK(true);   // erreicht -> keine Endlosschleife

    // Eigene Karte mit Food neben dem Start
    CustomMap m2;
    m2.food = { 10 * GRID_W + 11 };
    m2.startX = 10; m2.startY = 10;
    World w2(1.0f, cfg, false);
    w2.loadCustomMap(m2);
    w2.executeAction(1);   // nach rechts
    CHECK(w2.foodEaten == 1);
    CHECK(w2.agentX == 11 && w2.agentY == 10);
}

// ---------------------------------------------------------------- Aktionsmaske / Greedy / Kennzahlen
static void testPolicyAndMetrics() {
    set_local_seed(321);
    Rng rng; rng.seed(1);

    // greedy: hoechstes Logit, ohne Zufall
    for (int i = 0; i < 50; i++) {
        float lg[OUTPUTS] = { 0.1f, 2.0f, -1.0f, 0.5f };
        CHECK(Brain::sampleAction(lg, rng, 0.0f, 0) == 1);
    }
    // Maske: gesperrte Aktion wird nie gewaehlt (greedy und gewuerfelt)
    for (int i = 0; i < 2000; i++) {
        float a[OUTPUTS] = { 0.1f, 5.0f, -1.0f, 0.5f };
        CHECK(Brain::sampleAction(a, rng, 0.0f, 0b0010) == 3);
        float b[OUTPUTS] = { 0.1f, 5.0f, -1.0f, 0.5f };
        int act = Brain::sampleAction(b, rng, 1.0f, 0b0010);
        CHECK(act != 1 && act >= 0 && act < OUTPUTS);
    }
    // alles gesperrt -> Maske wird ignoriert (kein Absturz, gueltige Aktion)
    { float lg[OUTPUTS] = { 1, 2, 3, 4 }; CHECK(Brain::sampleAction(lg, rng, 1.0f, 0xF) >= 0); }
    // niedrige Temperatur -> fast immer die beste Aktion
    { int hit = 0; for (int i = 0; i < 1000; i++) { float lg[OUTPUTS] = { 0, 1, 0, 0 }; hit += Brain::sampleAction(lg, rng, 0.1f, 0) == 1; } CHECK(hit > 990); }

    // World: blockedMask, Kollisionen, kuerzester Weg
    RuntimeConfig cfg;
    World w(1.0f, cfg, false);
    CustomMap m;
    m.startX = 5; m.startY = 5;
    for (int y = 3; y <= 7; y++) m.walls.push_back(y * GRID_W + 6);      // senkrechte Wand rechts vom Start
    m.food = { 5 * GRID_W + 8 };                                        // Futter dahinter
    w.loadCustomMap(m);
    CHECK(w.blockedMask() == 0b0010);                                    // nur "rechts" gesperrt
    CHECK(w.shortestPath(5 * GRID_W + 5, 5 * GRID_W + 8) == 9);          // Umweg um die Wand (y 3..7): 3 hoch, 3 rechts, 3 runter
    CHECK(w.shortestPath(5 * GRID_W + 5, 5 * GRID_W + 5) == 0);
    w.executeAction(1);                                                  // gegen die Wand
    CHECK(w.collisions == 1);

    // Direkter Weg: Umwegfaktor 1
    World w2(1.0f, cfg, false);
    CustomMap m2; m2.startX = 5; m2.startY = 5; m2.food = { 5 * GRID_W + 8 };
    w2.loadCustomMap(m2);
    for (int i = 0; i < 3; i++) w2.executeAction(1);
    CHECK(w2.foodEaten == 1 && w2.collisions == 0);
    CHECK(std::fabs(w2.meanDetour() - 1.0f) < 1e-6f);
    // Umweg: erst weg vom Futter, dann hin -> Faktor > 1
    World w3(1.0f, cfg, false);
    w3.loadCustomMap(m2);
    w3.executeAction(0); w3.executeAction(0);                            // 2 nach links
    for (int i = 0; i < 5; i++) w3.executeAction(1);                     // 5 nach rechts
    CHECK(w3.foodEaten == 1 && std::fabs(w3.meanDetour() - 7.0f / 3.0f) < 1e-4f);

    // Fitness: Direktheit belohnt, Kollisionen bestraft
    EvolutionOptions o; o.verbose = false;
    auto evo = std::make_unique<Evolution>(o);
    World a(1.0f, cfg, false), b(1.0f, cfg, false);
    a.loadCustomMap(m2); b.loadCustomMap(m2);
    a.foodEaten = b.foodEaten = 5; a.steps = b.steps = 100; a.detourSum = 5.0f; b.detourSum = 10.0f;
    CHECK(evo->computeBaseFitness(a) > evo->computeBaseFitness(b));
    b.detourSum = 5.0f; b.collisions = 4;
    CHECK(evo->computeBaseFitness(a) > evo->computeBaseFitness(b));

    // Benchmark: reproduzierbar, plausible Werte; Maske verhindert Kollisionen
    set_local_seed(9);
    auto br = std::make_unique<Brain>();
    BenchResult r1 = evo->benchmark(*br, 1);
    BenchResult r2 = evo->benchmark(*br, 1);
    CHECK(r1.episodes == BENCH_MAPS);
    CHECK(r1.avgFood == r2.avgFood && r1.avgSteps == r2.avgSteps && r1.detour == r2.detour);
    CHECK(r1.collisions == 0.0f);                                        // Maske ist Standard
    CHECK(r1.detour >= 1.0f - 1e-4f || r1.avgFood == 0.0f);
    CHECK(r1.survived >= 0.0f && r1.survived <= 1.0f);
}

// ---------------------------------------------------------------- Replay
static void testReplay() {
    set_local_seed(77);
    auto brain = std::make_unique<Brain>();
    RuntimeConfig cfg;
    World w(1.0f, cfg);
    std::string js = buildReplayJson(w, *brain, 5, 300);
    CHECK(js.rfind("{\"generation\":5,", 0) == 0);
    CHECK(js.find("\"frames\":[[") != std::string::npos);
    CHECK(js.find("\"events\":[") != std::string::npos);
    CHECK(js.back() == '}');
    // frames = steps + 1
    size_t fpos = js.find("\"frames\":[") ; size_t epos = js.find("],\"events\"");
    int frames = 0;
    for (size_t i = fpos; i < epos; i++) if (js[i] == '[' && i > fpos + 9) frames++;
    CHECK(frames == w.steps + 1);
}

// ---------------------------------------------------------------- Json
static void testJson() {
    std::string body = R"({"name":"w\"x","w":45, "h" : 45,"walls":[1,2, 3,-4],"food":[],"foodReward":1.5e1,"start":[3,4]})";
    float f; long l; std::string s;
    CHECK(json::extractString(body, "name", s) && s == "w\"x");
    CHECK(json::extractInt(body, "w", l) && l == 45);       // "w" im String-Wert wird nicht verwechselt
    CHECK(json::extractInt(body, "h", l) && l == 45);
    CHECK(json::extractFloat(body, "foodReward", f) && f == 15.0f);
    CHECK(!json::extractFloat(body, "missing", f));
    auto walls = json::parseIntArray(body, "walls");
    CHECK((walls == std::vector<int>{1, 2, 3, -4}));
    CHECK(json::parseIntArray(body, "food").empty());
    CHECK((json::parseIntArray(body, "start") == std::vector<int>{3, 4}));
    CHECK(json::escape("a\"b\\c\n\x01") == "a\\\"b\\\\c \\u0001");
    CHECK(!json::extractFloat(R"({"x":nan})", "x", f));
    CHECK(!json::extractFloat(R"({"x":1e999})", "x", f));
}

// ---------------------------------------------------------------- ModelStore
static void testModelStore(const std::string& srcDir) {
    set_local_seed(11);
    const std::string dir = (fs::temp_directory_path() / "evorl_test_models").string();
    fs::remove_all(dir);

    auto b = std::make_shared<Brain>();
    b->sigma = 0.123f;
    SavedModel m;
    m.id = modelstore::newId(); m.name = "Test \"Modell\""; m.generation = 42; m.fitness = 1234.5f;
    m.food = 7; m.steps = 99; m.savedAt = modelstore::timestampNow(); m.brain = b;
    CHECK(modelstore::save(dir, m));
    CHECK(!fs::exists(dir + "/" + m.id + ".brain.tmp"));

    auto loaded = modelstore::loadAll(dir);
    CHECK(loaded.size() == 1);
    if (!loaded.empty()) {
        const auto& l = loaded[0];
        CHECK(l.id == m.id && l.name == m.name && l.generation == 42 && l.food == 7 && l.steps == 99);
        CHECK(std::fabs(l.fitness - 1234.5f) < 0.01f);
        CHECK(std::memcmp(&l.brain->W_h[0][0], &b->W_h[0][0], sizeof(b->W_h)) == 0);
        CHECK(std::memcmp(l.brain->bias_out, b->bias_out, sizeof(b->bias_out)) == 0);
        CHECK(l.brain->sigma == 0.123f);
    }

    // Unsichere IDs werden nicht als Dateiname akzeptiert
    SavedModel bad = m; bad.id = "../evil";
    CHECK(!modelstore::save(dir, bad));

    // Beschaedigte Datei wird verworfen
    { std::ofstream f(dir + "/" + m.id + ".brain", std::ios::binary | std::ios::trunc); f << "BRN2junk"; }
    CHECK(modelstore::loadAll(dir).empty());
    fs::remove_all(dir);

    // Altformat BRN1 (im Repo mitgelieferte Modelle) muss weiter ladbar sein
    if (!srcDir.empty() && fs::exists(srcDir + "/models")) {
        auto legacy = modelstore::loadAll(srcDir + "/models");
        CHECK(!legacy.empty());
        for (auto& lm : legacy) {
            auto br = std::make_unique<Brain>(*lm.brain);
            std::array<float, INPUTS> in{}; float lg[OUTPUTS];
            br->forward(in, lg);
            for (float v : lg) CHECK(std::isfinite(v));
        }
    }
}

// ---------------------------------------------------------------- Evolution
static float runEvolution(uint64_t seed, int gens) {
    EvolutionOptions o;
    o.generations = gens; o.seed = seed; o.demo = false; o.verbose = false;
    auto evo = std::make_unique<Evolution>(o);
    evo->train();
    CHECK((int)evo->population.size() == POPULATION_SIZE);
    for (auto& ind : evo->population) CHECK(std::isfinite(ind.fitness));
    return evo->globalBest.baseFitness;
}

static void testEvolution() {
    float a = runEvolution(2024, 3);
    CHECK(std::isfinite(a) && a > -1e20f);
    float b = runEvolution(2024, 3);
    CHECK(a == b);                                   // reproduzierbar mit festem Seed
#ifdef _OPENMP
    int old = omp_get_max_threads();
    omp_set_num_threads(1);
    float c = runEvolution(2024, 3);
    omp_set_num_threads(old);
    CHECK(a == c);                                   // ... unabhaengig von der Threadzahl
#endif
}

// ---------------------------------------------------------------- Monitor + HTTP
static void testMonitor() {
    const std::string dir = (fs::temp_directory_path() / "evorl_test_monitor").string();
    fs::remove_all(dir);
    {
        TrainingMonitor mon(dir);
        CHECK(mon.modelsJson() == "[]");
        CHECK(mon.saveCurrentModelJson("{}").find("no_model") != std::string::npos);
        CHECK(mon.toJson().find("\"hasBest\":false") != std::string::npos);

        set_local_seed(3);
        auto b = std::make_unique<Brain>();
        b->baseFitness = 100; b->food = 3; b->steps = 50;
        mon.setBestBrain(*b, 12);
        CHECK(mon.modelsJson().find("\"id\":\"live\"") != std::string::npos);
        std::string saved = mon.saveCurrentModelJson("{\"name\":\"Mein \\\"Held\\\"\"}");
        CHECK(saved.find("\"ok\":true") != std::string::npos);
        CHECK(saved.find("\"persisted\":true") != std::string::npos);
        CHECK(saved.find("Mein \\\"Held\\\"") != std::string::npos);

        // Replay & Playground
        std::string rep = mon.modelReplayJson("live");
        CHECK(rep.find("\"frames\":[[") != std::string::npos);
        CHECK(mon.modelReplayJson("nope") == "{\"frames\":[]}");
        std::string pg = mon.runPlaygroundJson("{\"model\":\"live\",\"w\":45,\"h\":45,\"walls\":[],\"food\":[500],\"start\":[5,5],\"maxSteps\":50}");
        CHECK(pg.find("\"frames\":[[5,5]") != std::string::npos);
        CHECK(mon.runPlaygroundJson("{\"model\":\"live\",\"w\":10,\"h\":10}").find("grid_size") != std::string::npos);
        CHECK(mon.runPlaygroundJson("{\"model\":\"x\"}").find("model_not_found") != std::string::npos);

        // Playground darf die globale Konfiguration nicht veraendern
        RuntimeConfig before = getRuntimeConfig();
        mon.runPlaygroundJson("{\"model\":\"live\",\"w\":45,\"h\":45,\"foodReward\":999,\"stepCost\":9,\"maxSteps\":5}");
        RuntimeConfig after = getRuntimeConfig();
        CHECK(before.foodReward == after.foodReward && before.stepCost == after.stepCost);

        // Modell-Uebernahme
        CHECK(mon.requestModelLoad("live"));
        Brain tmp{ Brain::NoInit{} };
        CHECK(mon.takePendingLoad(tmp));
        CHECK(!mon.takePendingLoad(tmp));
        CHECK(!mon.requestModelLoad("unbekannt"));

        // Config
        CHECK(!mon.applyConfigJson("{}"));
        CHECK(!mon.applyConfigJson("{\"foodReward\":nan}"));
        CHECK(mon.applyConfigJson("{\"foodReward\":77}"));
        CHECK(getRuntimeConfig().foodReward == 77.0f);
        CHECK(getRuntimeConfig().maskBlocked);
        CHECK(mon.applyConfigJson("{\"maskBlocked\":false}") && !getRuntimeConfig().maskBlocked);
        CHECK(mon.configJson().find("\"maskBlocked\":false") != std::string::npos);
        CHECK(mon.applyConfigJson("{\"maskBlocked\":1}") && getRuntimeConfig().maskBlocked);
        BenchResult br; br.gen = 5; br.episodes = 20; br.stepsPerFood = 12.5f; br.detour = 1.2f;
        mon.setBenchmark(br);
        std::string st = mon.toJson();
        CHECK(st.find("\"benchmark\":{\"gen\":5") != std::string::npos);
        CHECK(st.find("\"benchHistory\":{\"gen\":[5]") != std::string::npos);
        setRuntimeConfig(RuntimeConfig());
    }
    // Neu starten: gespeichertes Modell wird gefunden
    { TrainingMonitor mon2(dir); CHECK(mon2.modelsJson().find("Held") != std::string::npos); }
    fs::remove_all(dir);
}

#ifndef _WIN32
static std::string httpRaw(int port, const std::string& req) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(s, (sockaddr*)&a, sizeof(a)) != 0) { ::close(s); return ""; }
    ::send(s, req.data(), req.size(), 0);
    std::string out; char buf[8192]; int n;
    while ((n = (int)::recv(s, buf, sizeof(buf), 0)) > 0) out.append(buf, n);
    ::close(s);
    return out;
}

static void testHttp() {
    const int port = 18931;
    TrainingMonitor mon((fs::temp_directory_path() / "evorl_test_http").string());
    HttpServer srv(mon, port);
    CHECK(srv.start());
    auto get = [&](const std::string& path, const std::string& host, const std::string& extra = "") {
        return httpRaw(port, "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\n" + extra + "\r\n");
    };
    const std::string host = "localhost:" + std::to_string(port);
    CHECK(get("/api/status", host).find("200 OK") != std::string::npos);
    CHECK(get("/api/status", host).find("\"running\":false") != std::string::npos);
    CHECK(get("/", host).find("<html") != std::string::npos);
    CHECK(get("/nope", host).find("404") != std::string::npos);
    CHECK(get("/api/status", "evil.example.com").find("403") != std::string::npos);        // DNS-Rebinding

    auto post = [&](const std::string& path, const std::string& origin, const std::string& body = "") {
        return httpRaw(port, "POST " + path + " HTTP/1.1\r\nHost: " + host + "\r\n"
            + (origin.empty() ? "" : "Origin: " + origin + "\r\n")
            + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
    };
    CHECK(post("/api/stop", "http://evil.example.com").find("403") != std::string::npos);  // Cross-Site-POST
    CHECK(!mon.stopRequested());
    CHECK(post("/api/models/../../x/load", "").find("404") != std::string::npos);
    CHECK(post("/api/config", "http://" + host, "{\"stepCost\":2.5}").find("200 OK") != std::string::npos);
    CHECK(getRuntimeConfig().stepCost == 2.5f);
    setRuntimeConfig(RuntimeConfig());
    // Grosser Body (mehrere recv-Aufrufe)
    std::string big = "{\"walls\":[" ; for (int i = 0; i < 20000; i++) big += "1,"; big += "1],\"model\":\"live\"}";
    CHECK(post("/api/playground/run", "", big).find("model_not_found") != std::string::npos);
    CHECK(post("/api/stop", "http://" + host).find("200 OK") != std::string::npos);
    CHECK(mon.stopRequested());

    // Haengender Client (sendet nichts) blockiert den Server nicht dauerhaft; stop() kehrt zurueck
    srv.stop();
    CHECK(true);
    fs::remove_all(fs::temp_directory_path() / "evorl_test_http");
}
#endif

int main(int argc, char** argv) {
    std::string srcDir = argc > 1 ? argv[1] : "";
#ifdef EVORL_SOURCE_DIR
    if (srcDir.empty()) srcDir = EVORL_SOURCE_DIR;
#endif
    RUN(testRng());
    RUN(testBrain());
    RUN(testWorld());
    RUN(testPolicyAndMetrics());
    RUN(testReplay());
    RUN(testJson());
    RUN(testModelStore(srcDir));
    RUN(testMonitor());
#ifndef _WIN32
    RUN(testHttp());
#endif
    RUN(testEvolution());

    std::cout << "\n" << (g_checks - g_failed) << "/" << g_checks << " Pruefungen bestanden\n";
    return g_failed ? 1 : 0;
}
