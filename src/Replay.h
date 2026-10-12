#pragma once
// ============================================================
//  Replay.h  -  Episoden als kompaktes JSON-Replay aufzeichnen
//  Format:
//    { generation, w, h, food, steps,
//      walls:[idx..], food0:[idx..],           statische Karte / Start-Nahrung
//      frames:[[x,y],..],                      Agentenposition je Frame
//      events:[[frame, typ, idx],..] }         typ 0 = Nahrung weg, 1 = neu
// ============================================================
#include "Brain.h"
#include "World.h"
#include <string>

// Spielt eine Episode auf der bereits initialisierten World durch (bis Tod
// oder `maxSteps`) und liefert das Replay-JSON. Setzt das Gedaechtnis des
// Brains zurueck. `temperature` 0 = greedy (so verhaelt sich ein fertiges
// Modell); die Aktionsmaske folgt world.cfg.maskBlocked.
std::string buildReplayJson(World& world, Brain& brain, int genLabel, int maxSteps,
    float temperature = 0.0f);
