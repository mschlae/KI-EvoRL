#pragma once
// ============================================================
//  TrainingMonitor.h
//  Thread-sichere Bruecke zwischen Trainingsschleife (Producer)
//  und Webserver (Consumer). Die Evolution schreibt pro Generation
//  einen Snapshot, der Server serialisiert ihn als JSON.
// ============================================================
#include <atomic>
#include <chrono>
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
    TrainingMonitor();

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
    // Vom GUI-/Webserver-Thread aufgerufen (Stop-Button).
    void requestStop() { stopRequested_.store(true); }
    // Vom Trainingsthread gepollt -> Schleife bricht sauber ab.
    bool stopRequested() const { return stopRequested_.load(); }

    // Aufgezeichnete Episode des aktuell besten Agenten (JSON, vom Training gesetzt).
    void setReplay(std::string json);

    // Vom Webserver-Thread aufgerufen -> liefert kompletten Status als JSON.
    std::string toJson() const;
    // Liefert die zuletzt aufgezeichnete Best-Agent-Episode als JSON.
    std::string replayJson() const;

private:
    mutable std::mutex m_;

    GenRecord latest_{};
    float globalBestFitness_ = 0.0f;
    int   globalBestFood_ = 0;
    float curriculum_ = 0.0f;
    bool  injecting_ = false;
    int   totalGenerations_ = 0;

    std::atomic<bool> running_{ false };
    std::atomic<bool> finished_{ false };
    std::atomic<bool> stopRequested_{ false }; // vom Stop-Button gesetzt

    std::chrono::steady_clock::time_point start_;
    std::vector<GenRecord> history_; // gedeckelt, fuer Charts
    std::string replay_;             // letzte Best-Agent-Episode (JSON)

public:
    // Liefert die aktuelle Runtime-Config als JSON (für GET /api/config)
    std::string configJson() const;
    // Wendet JSON-Config (vom Webserver empfangen) an und gibt Erfolg zurück
    bool applyConfigJson(const std::string& jsonStr);
};