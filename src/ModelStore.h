#pragma once
// ============================================================
//  ModelStore.h  -  Persistenz gespeicherter Modelle (Brains)
//
//  Pro Modell zwei Dateien in <dir>/:
//    <id>.meta   JSON mit Name, Generation, Fitness, ...
//    <id>.brain  Gewichte
//  Formate der .brain-Datei:
//    "BRN2" (aktuell): Magic, INPUTS, HIDDEN, OUTPUTS, danach nur die
//                      Gewichte/Biases + sigma als float32 (little endian).
//                      Layout-unabhaengig und gegen falsche Dimensionen geschuetzt.
//    "BRN1" (Altformat): Magic, sizeof(Brain), rohes Brain-Struct. Wird
//                      weiterhin gelesen, aber nicht mehr geschrieben.
//  Schreiben ist atomar (temp-Datei + rename).
// ============================================================
#include "Brain.h"

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

struct SavedModel {
    std::string id;
    std::string name;
    int   generation = 0;
    float fitness = 0;
    int   food = 0;
    int   steps = 0;
    std::string savedAt;   // menschlich lesbarer Zeitstempel
    std::shared_ptr<const Brain> brain;
};

namespace modelstore {

bool writeBrain(std::ostream& os, const Brain& b);
bool readBrain(std::istream& is, Brain& out);

// Alle gueltigen Modelle aus dem Ordner laden (aelteste zuerst).
std::vector<SavedModel> loadAll(const std::string& dir);
// Modell dauerhaft ablegen. false bei IO-Fehler (Modell bleibt dann nur im Speicher).
bool save(const std::string& dir, const SavedModel& m);

std::string timestampNow();
// Eindeutige ID aus Zeit + Zaehler.
std::string newId();

} // namespace modelstore
