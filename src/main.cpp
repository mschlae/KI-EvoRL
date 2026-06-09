// ============================================================
//  main.cpp  -  Einstiegspunkt
//  Startet den Web-Monitor und anschliessend das Training.
//  Nach Abschluss bleibt das Dashboard offen, bis der Nutzer
//  ENTER drueckt (so lassen sich die Endergebnisse ansehen).
// ============================================================
#include "Config.h"
#include "Evolution.h"
#include "HttpServer.h"
#include "TrainingMonitor.h"

#include <iostream>

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::cout << "EVOLUTIONAERE RL KI - MODULARE VERSION v4\n";
    std::cout << "Fixes: hack-sichere Fitness, Common Random Numbers, echte Novelty,\n";
    std::cout << "       wirksamer Mutations-Boost, Energie-/Episoden-Deckel\n";
    std::cout << "Web-Dashboard: http://localhost:" << WEB_SERVER_PORT << "\n\n";

    TrainingMonitor monitor;
    HttpServer server(monitor, WEB_SERVER_PORT);
    server.start(); // laeuft auch ohne Server weiter, falls Port belegt ist

    Evolution evo;
    evo.setMonitor(&monitor);
    evo.train();

    std::cout << "\nSimulation abgeschlossen.\n";
    std::cout << "Dashboard bleibt aktiv. ENTER druecken zum Beenden...\n";
    std::cin.get();

    server.stop();
    return 0;
}
