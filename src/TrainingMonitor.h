#pragma once
// ============================================================
//  TrainingMonitor.h
//  Thread-sichere Bruecke zwischen Trainingsschleife (Producer)
//  und Webserver (Consumer). Die Evolution schreibt pro Generation
//  einen Snapshot, der Server serialisiert ihn als JSON.
//
//  Erweiterung (Modell-/Playground-API):
//   - Der Monitor haelt zusaetzlich eine Kopie des aktuell besten
//     Brains (live) und einen kleinen, auf Platte persistierten
//     Modell-Speicher.
//   - Damit kann der Webserver ohne Zugriff auf die Evolution:
//       * das aktuell beste Modell ausliefern  (modelsJson / live)
//       * das beste Modell speichern           (saveCurrentModelJson)
//       * ein gespeichertes/Live-Modell als Replay simulieren
//         (modelReplayJson)
//       * ein Modell auf einer selbst gezeichneten Karte laufen
//         lassen                               (runPlaygroundJson)
//       * ein Modell zur Uebernahme ins Training vormerken
//         (requestModelLoad / takePendingLoad)
//   - Das Training ruft setBestBrain() pro Generation auf und
//     pollt takePendingLoad(). Beides ist optional; fehlt der
//     Monitor, laeuft alles unveraendert weiter.
// ============================================================
#include "Brain.h"

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

// Ein gespeichertes Modell (Brain + Metadaten) in der Bibliothek.
struct SavedModel {
    std::string id;
    std::string name;
    int   generation = 0;
    float fitness = 0;
    int   food = 0;
    int   steps = 0;
    std::string savedAt;   // menschlich lesbarer Zeitstempel
    Brain brain;
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
    void requestStop() { stopRequested_.store(true); }
    bool stopRequested() const { return stopRequested_.load(); }

    // Aufgezeichnete Episode des aktuell besten Agenten (JSON, vom Training gesetzt).
    void setReplay(std::string json);

    // Vom Webserver-Thread aufgerufen -> liefert kompletten Status als JSON.
    std::string toJson() const;
    std::string replayJson() const;

    // ---- Modell-/Playground-API ----
    // Pro Generation vom Training aufgerufen: kopiert das aktuell beste,
    // "saubere" Brain (globalBestBase) inkl. Metadaten in den Monitor.
    void setBestBrain(const Brain& b, int generation);
    bool hasBestBrain() const;

    // Bibliothek (JSON-Array). Enthaelt zuerst das Live-Modell (sofern
    // vorhanden), danach die gespeicherten Modelle.
    std::string modelsJson() const;

    // Speichert das aktuell beste Brain dauerhaft. Body darf {"name":"..."}
    // enthalten. Gibt den neuen Bibliothekseintrag als JSON zurueck.
    std::string saveCurrentModelJson(const std::string& body);

    // Simuliert eine Episode des Modells <id> (oder "live") auf einer
    // Standardkarte und liefert das Replay-JSON (Format wie recordReplay).
    std::string modelReplayJson(const std::string& id) const;

    // Laesst ein Modell auf einer im Body uebergebenen Karte laufen.
    std::string runPlaygroundJson(const std::string& body);

    // Merkt ein Modell zur Uebernahme in die laufende Population vor.
    bool requestModelLoad(const std::string& id);
    // Vom Training gepollt: liefert (und konsumiert) ein vorgemerktes Modell.
    bool takePendingLoad(Brain& out);

    // Runtime-Config (fuer GET/POST /api/config).
    std::string configJson() const;
    bool applyConfigJson(const std::string& jsonStr);

private:
    void loadModelsFromDisk();   // beim Start: vorhandene Modelle einlesen

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
    std::string replay_;

    // Live-bestes Brain (Kopie aus der Evolution).
    Brain bestBrain_;
    bool  haveBest_ = false;
    int   bestGen_ = 0;

    // Persistierte Modell-Bibliothek.
    std::vector<SavedModel> models_;

    // Vorgemerktes Modell zur Uebernahme ins Training.
    Brain pendingBrain_;
    bool  havePending_ = false;
};