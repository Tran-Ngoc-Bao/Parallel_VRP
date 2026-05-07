# C++ Sequential Tabu Search – VRP with Drones

This is a C++17 sequential port of the Rust tabu search implementation located in
`../rust/`.  It produces identical CLI flags, JSON/CSV output formats, and algorithm
semantics.

## Dependencies (fetched automatically via FetchContent)

| Library | Version | Purpose |
|---------|---------|---------|
| [CLI11](https://github.com/CLIUtils/CLI11) | v2.3.2 | Command-line argument parsing |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | JSON serialization |

## Build

```bash
cd sequence-tabu-search/cpp
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Usage

### Run tabu search

```bash
./build/tabu_search run <problem_file> [options]
```

Key options (defaults in parentheses):

| Flag | Default | Description |
|------|---------|-------------|
| `--truck-cfg` | `problems/config_parameter/truck_config.json` | Truck configuration |
| `--drone-cfg` | `problems/config_parameter/drone_endurance_config.json` | Drone configuration |
| `-c/--config` | `endurance` | Energy model: linear/non-linear/endurance/unlimited |
| `--tabu-size-factor` | `0.75` | Tabu list size factor |
| `--adaptive-iterations` | `60` | Iterations per adaptive segment |
| `--adaptive-segments` | `7` | Segments before reset |
| `--strategy` | `adaptive` | Strategy: random/cyclic/vns/adaptive |
| `--fix-iteration` | (none) | Run exactly N iterations |
| `--reset-after-factor` | `125.0` | Reset period factor |
| `--max-elite-size` | `0` | Elite set size (0 = disabled) |
| `--penalty-exponent` | `0.5` | Penalty exponent |
| `--waiting-time-limit` | `3600.0` | Max customer waiting time (s) |
| `--trucks-count` | (from file) | Override truck count |
| `--drones-count` | (from file) | Override drone count |
| `--speed-type` | `high` | Drone speed config: low/high |
| `--range-type` | `high` | Drone range config: low/high |
| `--outputs` | `outputs/` | Output directory |
| `--disable-logging` | false | Suppress CSV log |
| `--dry-run` | false | Initialize only, no search |
| `--seed` | (random) | RNG seed for reproducibility |
| `-v/--verbose` | false | Print iteration progress |

### Evaluate an existing solution

```bash
./build/tabu_search evaluate <solution.json> <config.json>
```

## Output files

Three files are written to `--outputs` (default `outputs/`):

| File | Content |
|------|---------|
| `<problem>-<id>.csv` | Per-iteration log (cost, violations, tabu list, …) |
| `<problem>-<id>.json` | Full run summary (config, solution, elapsed time, …) |
| `<problem>-<id>-solution.json` | Solution routes only |
| `<problem>-<id>-config.json` | Serialized configuration |

## Design notes

- `std::shared_ptr<T>` replaces Rust `Rc<T>`
- CRTP (`Route<Derived>`) provides shared route operations (push/pop, inter_route, intra_route, …)
- `RouteHelper<T>` provides type-specific static dispatch (servable, make, single_route, …)
- Global `Config` singleton initialised once at startup via `set_global_config()`
- `std::atomic<double>` replaces `atomic_float::AtomicF64` for penalty coefficients
- `std::priority_queue` (min-heap) replaces Rust `BinaryHeap` (max-heap with reversed ordering)
- `std::set` replaces `BTreeSet`
- `std::discrete_distribution` replaces `WeightedIndex` for adaptive neighbourhood selection
- Neighborhoods: Move(1,0), Move(1,1), Move(2,0), Move(2,1), Move(2,2), 2-opt, Ejection-chain
