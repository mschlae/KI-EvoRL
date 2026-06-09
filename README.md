# EvoRL — Neuroevolution mit Live-Dashboard

Ein evolutionärer Reinforcement-Learning-Agent: Eine Population rekurrenter
**GRU-Netze** lernt per genetischem Algorithmus, sich in einer Gitterwelt zu
bewegen und Nahrung zu sammeln. Inklusive Speziation, Novelty Search, Hall of
Fame und einem eingebauten **Web-Dashboard** zur Live-Beobachtung des Trainings.

---

## Schnellstart (Visual Studio)

1. `EvoRL.sln` öffnen.
2. Konfiguration auf **Release | x64** stellen.
3. Mit **F5** (oder Strg+F5) starten.
4. Browser öffnen: **http://localhost:8080**

OpenMP, der C++17-Standard und `ws2_32.lib` sind im Projekt bereits konfiguriert
— es ist keine weitere Einrichtung nötig.

## Alternativer Build (CMake / Linux / macOS)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/EvoRL
```

---

## Projektstruktur

```
EvoRL/
├── EvoRL.sln                 Visual-Studio-Solution
├── EvoRL.vcxproj(.filters)   Projektdatei + logische Gruppierung
├── CMakeLists.txt            plattformübergreifender Build
├── src/
│   ├── Config.h/.cpp         alle Tuning-Parameter + Curriculum-Global
│   ├── Rng.h/.cpp            thread-lokaler Zufallsgenerator
│   ├── MathUtils.h           Aktivierungen / Softmax
│   ├── World.h/.cpp          Umgebung: Sensoren, Aktionen, Reward
│   ├── Brain.h/.cpp          GRU-Netz mit selbstadaptiver Mutation
│   ├── Evolution.h/.cpp      genetischer Algorithmus + Speziation
│   ├── TrainingMonitor.h/.cpp thread-sicherer Status für das Dashboard
│   ├── HttpServer.h/.cpp     minimaler HTTP-Server (Winsock/POSIX)
│   ├── DashboardHtml.h/.cpp  eingebettete Dashboard-Seite
│   └── main.cpp              Einstiegspunkt
└── web/
    └── index.html            Dashboard-Quelle (für Anpassungen)
```

### Verantwortlichkeiten der Module

| Modul | Aufgabe |
|-------|---------|
| **Config** | Zentrale Konstanten. Hier wird getunt. |
| **World** | Die simulierte Welt: 77 Sensorwerte, Reward-Shaping, Anti-Loop, Early-Stopping. |
| **Brain** | Rekurrentes GRU-Netz (77→96→4), Mutation, Crossover, Ähnlichkeitsmaß. |
| **Evolution** | Selektion, Speziation, Novelty Search, Hall of Fame, Stagnations-Boosts. |
| **TrainingMonitor** | Mutex-geschützter Snapshot + Verlaufshistorie, JSON-Serialisierung. |
| **HttpServer** | Bedient `/` (Dashboard) und `/api/status` (JSON) in eigenem Thread. |

---

## Das Dashboard

Der Server läuft in einem eigenen Thread und stört das Training nicht. Er ist nur
**lokal** erreichbar (`127.0.0.1`) und bietet zwei Endpunkte:

- `GET /` — das HTML-Dashboard
- `GET /api/status` — der aktuelle Status als JSON (Kennzahlen + Verlauf)

Das Dashboard pollt jede Sekunde und zeigt Generation, Fortschritt, beste/­durch­schnittliche
Fitness, Nahrung pro Episode, Mutation σ, Diversität, Anzahl Spezies sowie zwei
Verlaufs-Charts.

Den Port änderst du in `Config.h` (`WEB_SERVER_PORT`).

### Dashboard anpassen

Das Dashboard ist als String in `src/DashboardHtml.cpp` einkompiliert, damit die
EXE ohne externe Dateien läuft. Zum Bearbeiten:

1. `web/index.html` ändern (bequem im Browser testbar).
2. Neu einbetten — z. B. unter Linux/macOS/Git-Bash:

   ```bash
   {
     printf '#include "DashboardHtml.h"\n\nconst char* DASHBOARD_HTML = R"EVORLHTML(';
     cat web/index.html;
     printf ')EVORLHTML";\n';
   } > src/DashboardHtml.cpp
   ```

   Alternativ `DashboardHtml.cpp` direkt editieren.

---

## Hinweise

- **OpenMP** parallelisiert die Evaluation der Population — auf Mehrkern-CPUs
  deutlich schneller. Standard: alle Kerne (`EVAL_THREADS = 0`).
- Die finale **Konsolen-Demo** des besten Agenten nutzt ANSI-Escapes zum Löschen
  des Bildschirms. In modernen Terminals (Windows Terminal, VS-Konsole) wird das
  korrekt dargestellt.
- Standardmäßig läuft das Training über sehr viele Generationen
  (`GENERATIONS` in `Config.h`). Für einen schnellen Funktionstest diesen Wert
  herabsetzen.

## Lizenz


