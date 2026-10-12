# EvoRL — Neuroevolution with Live Dashboard

An evolutionary Reinforcement Learning agent: A population of recurrent **GRU networks** learns through a genetic algorithm to move in a grid world and collect food. Includes speciation, novelty search, and adaptive mutation. Real-time visualization via an embedded web dashboard.

**Keywords:** Genetic Algorithm, Neuroevolution, Reinforcement Learning, GRU Networks, NEAT, Speciation, C++, Live Dashboard

---

## 🚀 Quick Start (Visual Studio)

1. Open `EvoRL.sln`.
2. Set configuration to **Release | x64**.
3. Start with **F5** (or Ctrl+F5).
4. Open browser: **http://localhost:8080**

OpenMP, the C++17 standard, and `ws2_32.lib` are already configured in the project — no further setup required.

## 🔧 Alternative Build (CMake / Linux / macOS)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/EvoRL
```

By default the build uses `-march=native` (about 2× faster GRU forward pass on modern CPUs). If you
want a binary that runs on other machines, configure with `-DEVORL_NATIVE=OFF`.

### Command-line options

| Option | Meaning |
|--------|---------|
| `--generations N` | Number of generations (default `GENERATIONS` from `Config.h`) |
| `--port N` | Dashboard port (default 8080) |
| `--seed N` | Reproducible run — the result is independent of the number of threads |
| `--models DIR` | Folder for saved models (default `models`) |
| `--no-server` | Headless training without dashboard |
| `--no-wait` | Do not wait for ENTER at the end |
| `--no-demo` | Skip the console demo of the best agent |
| `--quiet` | One line per generation off |

`Ctrl+C` stops training cleanly (same as the dashboard's stop button).

### Tests

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

The test suite (`tests/tests.cpp`, no external dependencies) checks the optimized network against a
reference implementation, world invariants, replay/JSON handling, model persistence (including the old
`BRN1` format), the HTTP security checks, and that a seeded evolution run is reproducible.

---

## 📋 Project Structure

```
EvoRL/
├── EvoRL.sln                 Visual Studio solution
├── EvoRL.vcxproj(.filters)   Project file + logical grouping
├── CMakeLists.txt            Cross-platform build (+ tests)
├── src/
│   ├── Config.h/.cpp         All tuning parameters + thread-safe runtime config
│   ├── Rng.h/.cpp            Fast thread-local xoshiro256++ generator
│   ├── MathUtils.h           Activations, Softmax, vectorizable dot-product kernels
│   ├── World.h/.cpp          Environment: sensors, actions, reward, custom maps
│   ├── Brain.h/.cpp          GRU network with self-adaptive mutation
│   ├── Evolution.h/.cpp      Genetic algorithm + speciation
│   ├── Replay.h/.cpp         Episode → compact JSON replay
│   ├── ModelStore.h/.cpp     Atomic model persistence (BRN2, reads legacy BRN1)
│   ├── Json.h                Tiny JSON helpers for the API
│   ├── TrainingMonitor.h/.cpp Thread-safe status for dashboard + model library
│   ├── HttpServer.h/.cpp     Minimal HTTP server (Winsock/POSIX)
│   ├── DashboardHtml.h/.cpp  Embedded dashboard page
│   └── main.cpp              Entry point + command-line options
├── tests/tests.cpp           Unit / smoke tests (ctest)
└── web/
    └── index.html            Dashboard source (for customization)
```

### Module Responsibilities

| Module | Task |
|--------|------|
| **Config** | Central constants. This is where you tune parameters. |
| **World** | The simulated world: 77 sensor values, reward shaping, anti-loop, early stopping. Owns its RNG stream and a frozen copy of the runtime config. |
| **Brain** | Recurrent GRU network (77→84→4), mutation, crossover, similarity measure. |
| **Evolution** | Selection, adaptive speciation, novelty search, hall of fame, stagnation boosts, curriculum. |
| **Replay / ModelStore** | Replay recording and model files, shared by training and the dashboard API. |
| **TrainingMonitor** | Mutex-protected snapshot + history, model library, JSON serialization. |
| **HttpServer** | Serves the dashboard and the JSON API in its own thread. |

---

## 📊 Dashboard

The server runs in its own thread and does not interfere with training. It is only accessible **locally** (`127.0.0.1`).

| Endpoint | Purpose |
|----------|---------|
| `GET /` | the HTML dashboard |
| `GET /api/status` | current status as JSON (metrics + history) |
| `GET /api/replay` | replay of the current best agent |
| `GET` / `POST /api/config` | read / change reward and mutation parameters live |
| `POST /api/stop` | stop training |
| `GET /api/models` | model library (live best + saved models) |
| `POST /api/models/save` | save the current best model (`{"name": "..."}`) |
| `GET /api/models/{id}/replay` | replay of a model |
| `POST /api/models/{id}/load` | inject a model into the running population |
| `POST /api/playground/run` | run a model on a map you drew |

Requests with a foreign `Host` (DNS rebinding) or a foreign `Origin` (cross-site `POST`, e.g. to `/api/stop`)
are rejected with `403`. The dashboard polls every second and displays generation, progress, best/average fitness, food per
episode, mutation σ, diversity, number of species, and history charts.

You can change the port in `Config.h` (`WEB_SERVER_PORT`) or with `--port`.

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

## ✨ Features

- **Neuroevolution**: Genetic algorithm optimizes the weights of recurrent networks
- **GRU Networks**: Recurrent architecture for temporal learning
- **Adaptive Speciation**: The distance threshold is steered towards `SPECIES_TARGET` species
- **Novelty Search**: Rewards behavioral diversity, not just fitness
- **Adaptive Mutation**: Self-tuning mutation rates per individual, stagnation boosts
- **Curriculum**: Obstacles/food density ramp up over `CURRICULUM_GENS` generations
- **Reproducible**: `--seed` gives identical runs regardless of thread count
- **OpenMP Parallelization**: Multi-threaded fitness evaluation
- **Real-time Dashboard**: Live monitoring, model library and map playground
- **Cross-platform**: Windows (Visual Studio), Linux, macOS (CMake)

## 🛠 What changed in the rewrite

**Performance** (≈5× more simulated steps per second on the reference machine)
- GRU forward pass restructured into contiguous, auto-vectorizable dot products with a separate activation
  pass using a branch-free `exp` approximation (no libm calls in the hot loop)
- `World` keeps incremental counters (visited cells), caches the nearest food, and no longer locks a mutex
  per step; each world has its own RNG instead of a shared `std::mt19937_64`
- Brains are no longer randomly initialised just to be overwritten (crossover, loading), speciation no
  longer copies 165 KB representatives, the dashboard history JSON is serialised once per generation

**Correctness**
- The curriculum formula divided by `GENERATIONS` (10 000), so difficulty stayed ≈ 0 for 150 generations and
  then jumped to 1. It now ramps in 0.1 steps; records (best, hall of fame, stagnation) are reset on every step
  because fitness is only comparable within one difficulty level
- Episodes were effectively unbounded (`MAX_EPISODE_STEPS` was never used); training episodes are now capped
  as the config intended
- Offspring allocation per species used raw fitness, which flips sign when the total fitness is negative
  (frequent early on); fitness is now shifted before sharing
- The best individual of each generation survives unmutated (true elitism)
- The dashboard's *Mutation σ* field was not connected to anything; it now sets the starting mutation strength
- A frozen copy of the runtime config is used per generation, so live edits cannot skew a single comparison
- The playground no longer temporarily rewrites the global config while training is running
- Unbounded retry loops (food respawn, obstacle/agent placement) are capped, so a fully walled playground map
  cannot hang the server

**Server / robustness**
- Fixed process kill by `SIGPIPE` when a browser closes the connection, a hanging `accept()` on shutdown,
  and unbounded blocking on slow clients (I/O timeouts)
- Host/Origin checks, no more `Access-Control-Allow-Origin: *`, validated model ids and config values
- Model files are written atomically (`BRN2`: dimension-checked weights only); old `BRN1` files still load

## 📈 Use Cases

- Learning evolutionary algorithms and neuroevolution
- Experimental RL agent design
- AI agent visualization and monitoring
- Genetic algorithm optimization techniques

---

## 📝 Notes

- **OpenMP** parallelizes population evaluation — significantly faster on multi-core CPUs. Default: all cores (`EVAL_THREADS = 0`). Without OpenMP the program still builds and runs single-threaded.
- The final **console demo** of the best agent uses ANSI escapes to clear the screen. This is rendered correctly in modern terminals (Windows Terminal, VS Console).
- By default, training runs over many generations (`GENERATIONS` in `Config.h`). For a quick functionality test use `--generations`.
- Model replays in the dashboard run for at most `MAX_EPISODE_STEPS` steps, i.e. exactly what the fitness measures.

## 📄 License

This project is licensed under the **Creative Commons Attribution 4.0 International License (CC BY 4.0)**.

You are free to use, modify, and distribute this code.

See <https://creativecommons.org/licenses/by/4.0/legalcode> for the full license text.
