# human-sim

An observer simulator of human history from roughly 10,000 BCE. Climate shifts
in cycles, populations grow, migrate and mix, languages, cultures and religions
form and spread, and historical figures pursue their own goals inside states,
tribes, guilds, banks and gangs. Afterwards the history can be browsed in a
Dwarf Fortress-style legends viewer.

It runs on a real Earth map or on randomly generated worlds.

## Status

**Phase 0 (Foundations).** The simulation core runs, records history with
causes, and saves/loads, but the world has no content yet: a placeholder
system of "wanderers" walking between camps exercises the pipeline until real
systems arrive.

Design docs live in [`docs/design/`](docs/design/), one per phase. Every phase
is agreed on as a design doc before any code is written, and every step lands
as a pull request.

## Building (Windows)

Requirements: Visual Studio 2022 or newer with the C++ workload, and CMake
3.24+ (Visual Studio's bundled CMake works). Dependencies (Dear ImGui, doctest)
are downloaded automatically.

```
cmake --preset msvc
cmake --build --preset release
ctest --preset release
```

Or open the folder in Visual Studio, which picks up `CMakePresets.json`.

Outputs:

- `build/msvc/app/Release/humansim_viewer.exe`: the viewer
- `build/msvc/cli/Release/humansim.exe`: the command-line runner

## Using it

**Viewer.** *World → New world* picks a seed and start year. The **Time**
window runs the simulation (pause, speeds up to "Max", or "Run until year").
**Events** lists history with filters; selecting an event shows who took part
and what caused it, and causes can be followed back. **Entities** lists
everyone who ever lived, with their life story. *World → Save as* writes a
`.hsim` file plus a `.hsim.history` folder next to it; keep the two together.

**Command line.**

```
humansim generate --seed 42 --until 1CE --out world.hsim
humansim info world.hsim
humansim events world.hsim --from 5000BCE --min major --limit 20 --causes
humansim entity world.hsim wanderer 5
humansim continue world.hsim --until 1000CE
```

Years accept `10000BCE`, `1500CE`, `1500`, or astronomical numbering (`-9999`).

## Layout

```
sim/          Simulation library (pure C++20, no UI)
app/          Viewer (Dear ImGui, Win32 + Direct3D 11) and its headless smoke test
cli/          Command-line runner
tests/        Unit, determinism and save/load tests
docs/design/  Design docs, one per phase
```
