# EvoRL — Neuroevolution with Live Dashboard

An evolutionary Reinforcement Learning agent: A population of recurrent **GRU networks** learns through a genetic algorithm to move in a grid world and collect food. Includes speciation, novelty search, hall of fame, and a built-in **web dashboard** for live training observation.

---

## Quick Start (Visual Studio)

1. Open `EvoRL.sln`.
2. Set configuration to **Release | x64**.
3. Start with **F5** (or Ctrl+F5).
4. Open browser: **http://localhost:8080**

OpenMP, the C++17 standard, and `ws2_32.lib` are already configured in the project — no further setup required.

## Alternative Build (CMake / Linux / macOS)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/EvoRL
```

---

## Project Structure

```
EvoRL/
├── EvoRL.sln                 Visual Studio solution
├── EvoRL.vcxproj(.filters)   Project file + logical grouping
├── CMakeLists.txt            Cross-platform build
├── src/
│   ├── Config.h/.cpp         All tuning parameters + curriculum globals
│   ├── Rng.h/.cpp            Thread-local random number generator
│   ├── MathUtils.h           Activations / Softmax
│   ├── World.h/.cpp          Environment: sensors, actions, reward
│   ├── Brain.h/.cpp          GRU network with self-adaptive mutation
│   ├── Evolution.h/.cpp      Genetic algorithm + speciation
│   ├── TrainingMonitor.h/.cpp Thread-safe status for dashboard
│   ├── HttpServer.h/.cpp     Minimal HTTP server (Winsock/POSIX)
│   ├── DashboardHtml.h/.cpp  Embedded dashboard page
│   └── main.cpp              Entry point
└── web/
    └── index.html            Dashboard source (for customization)
```

### Module Responsibilities

| Module | Task |
|--------|------|
| **Config** | Central constants. This is where you tune parameters. |
| **World** | The simulated world: 77 sensor values, reward shaping, anti-loop, early stopping. |
| **Brain** | Recurrent GRU network (77→96→4), mutation, crossover, similarity measure. |
| **Evolution** | Selection, speciation, novelty search, hall of fame, stagnation boosts. |
| **TrainingMonitor** | Mutex-protected snapshot + history, JSON serialization. |
| **HttpServer** | Serves `/` (dashboard) and `/api/status` (JSON) in its own thread. |

---

## Dashboard

The server runs in its own thread and does not interfere with training. It is only accessible **locally** (`127.0.0.1`) and provides two endpoints:

- `GET /` — the HTML dashboard
- `GET /api/status` — current status as JSON (metrics + history)

The dashboard polls every second and displays generation, progress, best/average fitness, food per episode, mutation σ, diversity, number of species, and two history charts.

You can change the port in `Config.h` (`WEB_SERVER_PORT`).

### Customizing the Dashboard

The dashboard is compiled as a string in `src/DashboardHtml.cpp` so the executable runs without external files. To edit it:

1. Modify `web/index.html` (easily testable in a browser).
2. Re-embed it — for example on Linux/macOS/Git-Bash:

   ```bash
   {
     printf '#include "DashboardHtml.h"\n\nconst char* DASHBOARD_HTML = R"EVORLHTML(';
     cat web/index.html;
     printf ')EVORLHTML";\n';
   } > src/DashboardHtml.cpp
   ```

   Alternatively, edit `DashboardHtml.cpp` directly.

---

## Notes

- **OpenMP** parallelizes population evaluation — significantly faster on multi-core CPUs. Default: all cores (`EVAL_THREADS = 0`).
- The final **console demo** of the best agent uses ANSI escapes to clear the screen. This is rendered correctly in modern terminals (Windows Terminal, VS Console).
- By default, training runs over many generations (`GENERATIONS` in `Config.h`). For a quick functionality test, reduce this value.

## License

This project is licensed under the **Creative Commons Attribution 4.0 International License (CC BY 4.0)**.

You are free to use, modify, and distribute this code.

See [LICENSE](LICENSE) for the full license text.
