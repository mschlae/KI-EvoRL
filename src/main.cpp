// ============================================================
//  main.cpp  -  Einstiegspunkt
//  Startet den Web-Monitor und anschliessend das Training.
//  Nach Abschluss bleibt das Dashboard offen, bis der Nutzer
//  ENTER drueckt (so lassen sich die Endergebnisse ansehen).
//  Ctrl+C beendet das Training sauber (wie der Stop-Button).
// ============================================================
#include "Config.h"
#include "Evolution.h"
#include "HttpServer.h"
#include "TrainingMonitor.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {
    TrainingMonitor* g_monitor = nullptr;

    void onSignal(int) {
        if (g_monitor) g_monitor->requestStop();   // nur ein lock-freies atomares Store
    }

    struct Options {
        int  port = WEB_SERVER_PORT;
        int  generations = GENERATIONS;
        unsigned long long seed = 0;
        std::string modelsDir = "models";
        bool server = true;
        bool wait = true;
        bool demo = true;
        bool mask = true;
        bool quiet = false;
        bool help = false;
        bool bad = false;
    };

    void printUsage() {
        std::cout <<
            "EvoRL - Neuroevolution mit Live-Dashboard\n\n"
            "Aufruf: EvoRL [Optionen]\n"
            "  --generations N   Anzahl Generationen (Standard " << GENERATIONS << ")\n"
            "  --port N          Port des Dashboards (Standard " << WEB_SERVER_PORT << ")\n"
            "  --seed N          reproduzierbarer Lauf (Standard: zufaellig)\n"
            "  --models DIR      Ordner fuer gespeicherte Modelle (Standard: models)\n"
            "  --no-server       ohne Web-Dashboard (headless)\n"
            "  --no-wait         am Ende nicht auf ENTER warten\n"
            "  --no-demo         keine Konsolen-Demo des besten Agenten\n"
            "  --no-mask         Aktionsmaske aus (Zuege in Waende sind dann erlaubt und werden bestraft)\n"
            "  --quiet           keine Zeile pro Generation ausgeben\n"
            "  --help            diese Hilfe\n";
    }

    // Liest eine ganze Zahl in [lo, hi]; false bei Fehler.
    bool parseNumber(const char* s, long long lo, long long hi, long long& out) {
        char* end = nullptr;
        long long v = std::strtoll(s, &end, 10);
        if (end == s || *end != '\0' || v < lo || v > hi) return false;
        out = v;
        return true;
    }

    Options parseArgs(int argc, char** argv) {
        Options o;
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            auto needNum = [&](long long lo, long long hi, long long& v) {
                if (i + 1 >= argc || !parseNumber(argv[i + 1], lo, hi, v)) {
                    std::cerr << "Ungueltiger oder fehlender Wert fuer " << a << "\n";
                    o.bad = true;
                    return false;
                }
                i++;
                return true;
            };
            long long v = 0;
            if (a == "--help" || a == "-h") o.help = true;
            else if (a == "--generations") { if (needNum(1, 100000000, v)) o.generations = (int)v; }
            else if (a == "--port") { if (needNum(1, 65535, v)) o.port = (int)v; }
            else if (a == "--seed") { if (needNum(1, 9000000000000000000LL, v)) o.seed = (unsigned long long)v; }
            else if (a == "--models") {
                if (i + 1 < argc) o.modelsDir = argv[++i];
                else { std::cerr << "Fehlender Wert fuer --models\n"; o.bad = true; }
            }
            else if (a == "--no-server") o.server = false;
            else if (a == "--no-wait") o.wait = false;
            else if (a == "--no-demo") o.demo = false;
            else if (a == "--no-mask") o.mask = false;
            else if (a == "--quiet") o.quiet = true;
            else { std::cerr << "Unbekannte Option: " << a << "\n"; o.bad = true; }
        }
        return o;
    }
}

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    if (opt.help || opt.bad) {
        printUsage();
        return opt.bad ? 2 : 0;
    }

    std::cout << "EvoRL - evolutionaere RL-KI (GRU-Neuroevolution)\n";
    if (opt.server) std::cout << "Web-Dashboard: http://localhost:" << opt.port << "\n\n";

    {
        RuntimeConfig cfg = getRuntimeConfig();
        cfg.maskBlocked = opt.mask;
        setRuntimeConfig(cfg);
    }
    TrainingMonitor monitor(opt.modelsDir);
    g_monitor = &monitor;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    HttpServer server(monitor, opt.port);
    if (opt.server) server.start();   // Training laeuft auch weiter, falls der Port belegt ist

    EvolutionOptions eo;
    eo.generations = opt.generations;
    eo.seed = opt.seed;
    eo.demo = opt.demo;
    eo.verbose = !opt.quiet;

    {
        Evolution evo(eo);
        evo.setMonitor(&monitor);
        evo.train();
    }

    std::cout << "\nSimulation abgeschlossen.\n";
    if (opt.wait && opt.server && !monitor.stopRequested()) {
        std::cout << "Dashboard bleibt aktiv. ENTER druecken zum Beenden...\n";
        std::cin.get();
    }

    server.stop();
    g_monitor = nullptr;
    return 0;
}
