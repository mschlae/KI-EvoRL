#pragma once
// ============================================================
//  TrainingMonitor.h
//  Thread-sichere Bruecke zwischen Trainingsschleife (Producer)
//  und Webserver (Consumer). Die Evolution schreibt pro Generation
//  einen Snapshot, der Server serialisiert ihn als JSON.
//
//  Zusaetzlich haelt der Monitor
//   - eine Kopie des aktuell besten Brains (live),
//   - die persistierte Modell-Bibliothek (siehe ModelStore),
//   - ein vom Dashboard vorgemerktes Modell, das das Training
//     in die Population uebernimmt (requestModelLoad / takePendingLoad).
// ============================================================
#include "Brain.h"
#include "Metrics.h"
#include "ModelStore.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Ein Datenpunkt pro Generation (fuer die Verlaufs-Charts).
struct GenRecord {
    int   gen = 0;
    float best = 0;
    float avg = 0;
    float avgFood = 0;
    int   bestFood = 0;
    float bestSpf = 0;   // Schritte pro Nahrung des besten Agenten (niedriger = besser)
    float avgSpf = 0;    // durchschnittliche Schritte pro Nahrung
    float mutation = 0;
    float diversity = 0;
    int   species = 0;
};

class TrainingMonitor {
public:
    explicit TrainingMonitor(std::string modelsDir = "models");

    // Vom Trainingsthread aufgerufen (eine Momentaufnahme pro Generation).
    void update(const GenRecord& rec,
        float globalBestFitness,
        int   globalBestFood,
        float curriculum,
        bool  injecting,
        int   totalGenerations);

    // Beim Start eines Laufs aufrufen. Setzt das Stop-Flag zurueck, damit ein
    // erneuter Lauf nicht sofort wieder abbricht.
    void setRunning(bool r) {
        running_.store(r);
        if (r) stopRequested_.store(false);
    }
    void setFinished();

    // ---- Aktive Steuerung vom Dashboard ----
    void requestStop() { stopRequested_.store(true); }   // lock-frei -> auch aus Signal-Handlern nutzbar
    bool stopRequested() const { return stopRequested_.load(); }

    // Ergebnis eines Benchmark-Laufs (feste Karten, greedy) -> Dashboard.
    void setBenchmark(const BenchResult& r);

    // Aufgezeichnete Episode des aktuell besten Agenten (JSON, vom Training gesetzt).
    void setReplay(std::string json);

    // Vom Webserver-Thread aufgerufen -> liefert kompletten Status als JSON.
    std::string toJson() const;
    std::string replayJson() const;

    // ---- Modell-/Playground-API ----
    // Pro Generation vom Training aufgerufen: kopiert das aktuell beste,
    // "saubere" Brain inkl. Metadaten in den Monitor.
    void setBestBrain(const Brain& b, int generation);
    bool hasBestBrain() const;

    // Bibliothek (JSON-Array): zuerst das Live-Modell, danach die gespeicherten (neueste zuerst).
    std::string modelsJson() const;

    // Speichert das aktuell beste Brain dauerhaft. Body darf {"name":"..."} enthalten.
    std::string saveCurrentModelJson(const std::string& body);

    // Simuliert eine Episode des Modells <id> (oder "live") auf einer Standardkarte.
    std::string modelReplayJson(const std::string& id) const;

    // Laesst ein Modell auf einer im Body uebergebenen Karte laufen.
    std::string runPlaygroundJson(const std::string& body) const;

    // Merkt ein Modell zur Uebernahme in die laufende Population vor.
    bool requestModelLoad(const std::string& id);
    // Vom Training gepollt: liefert (und konsumiert) ein vorgemerktes Modell.
    bool takePendingLoad(Brain& out);

    // Runtime-Config (fuer GET/POST /api/config).
    std::string configJson() const;
    bool applyConfigJson(const std::string& jsonStr);

private:
    // Brain zu "live" oder gespeicherter ID; false, wenn unbekannt.
    bool findBrain(const std::string& id, std::unique_ptr<Brain>& out, int& gen) const;
    void rebuildHistoryJson();   // nur unter m_ aufrufen
    void rebuildBenchJson();     // nur unter m_ aufrufen

    std::string modelsDir_;
    mutable std::mutex m_;

    GenRecord latest_{};
    float globalBestFitness_ = 0.0f;
    int   globalBestFood_ = 0;
    float curriculum_ = 0.0f;
    bool  injecting_ = false;
    int   totalGenerations_ = 0;

    std::atomic<bool> running_{ false };
    std::atomic<bool> finished_{ false };
    std::atomic<bool> stopRequested_{ false };

    std::chrono::steady_clock::time_point start_;
    std::vector<GenRecord> history_;
    std::vector<BenchResult> benchHistory_;
    std::string benchJson_ = "null";       // letztes Ergebnis
    std::string benchHistoryJson_ = "{}";  // Verlauf als Arrays
    std::string historyJson_ = "{}";   // vorserialisierte Verlaufs-Arrays (einmal je Generation)
    std::string replay_;

    // Live-bestes Brain (Kopie aus der Evolution).
    std::shared_ptr<const Brain> bestBrain_;
    int   bestGen_ = 0;
    float bestFitness_ = 0;
    int   bestFood_ = 0;
    int   bestSteps_ = 0;

    // Persistierte Modell-Bibliothek.
    std::vector<SavedModel> models_;

    // Vorgemerktes Modell zur Uebernahme ins Training.
    std::shared_ptr<const Brain> pendingBrain_;
};
