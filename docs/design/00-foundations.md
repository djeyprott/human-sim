# Phase 0 — Foundations

Status: **proposed** — under review in its pull request.

## Goal

A simulation core that ticks, saves, loads and records history, with no world
content yet. Every later phase plugs into it. At the end of this phase you can
run an empty world, watch a placeholder system fire events, browse those
events, inspect entities, and save/load — in the viewer and from a headless
command line.

## Decisions already made

| Topic | Decision |
|---|---|
| Language | C++20 |
| UI | Dear ImGui (docking branch) |
| Platform | Windows only, MSVC |
| Run modes | Both: unthrottled generation, then browse; or watch live with pause/speed controls |
| Start date | ~10,000 BCE, before farming |
| End of a run | None: a run continues until the user stops it |
| Player role | Observer. Optional god powers, off by default |
| Workflow | One branch + pull request per step; nothing is pushed to `main` directly |

## Repository layout

```
sim/          Simulation library. Pure C++20, no UI or OS dependencies.
app/          Viewer: Dear ImGui on Win32 + Direct3D 11.
cli/          Headless runner, e.g. `humansim generate --seed 42 --until 1500CE`.
tests/        Unit and determinism tests (doctest).
docs/design/  One design doc per phase.
```

- Built with CMake (presets for MSVC Debug/Release). Third-party code is pulled
  in with `FetchContent` and pinned to exact versions, so nothing has to be
  installed by hand beyond Visual Studio and CMake.
- **Graphics backend: Win32 + Direct3D 11** rather than GLFW + OpenGL. Since the
  project is Windows only, this is the most robust native choice, needs one
  fewer dependency, and Dear ImGui supports it first-class. (Changed from the
  earlier OpenGL suggestion because of the Windows-only decision.)
- `sim/` never includes anything from `app/`. The CLI and the viewer drive the
  same library, so generation speed is never limited by the UI.

## Data model

- **Tables, not a generic ECS.** Each kind of thing (cell, cohort, person,
  organization, …) has its own table stored as plain arrays
  (structure-of-arrays where it matters for speed). This is simple, fast for
  millions of rows, and easy to save.
- **Stable IDs.** Every entity has an ID made of an index and a generation
  counter. A dead person's ID keeps pointing at their history; it is never
  silently reused for someone else.
- **Entity kinds are typed.** A `PersonId` cannot be passed where an
  `OrgId` is expected.

## Time

- **Calendar:** proleptic Gregorian days, with years before 1 CE shown as BCE
  (no year zero). Internally, a single signed day counter.
- **Base step: one day.** Each system declares its own cadence:

| System (later phases) | Cadence |
|---|---|
| Climate | Seasonal |
| Populations, economy | Monthly |
| Organizations | Monthly |
| Historical figures | Event-driven: each person has an agenda queue and acts when something is due, not every tick |

- A scheduler runs whichever systems are due on each day, in a fixed order.
- **Generate and watch are the same loop.** Generation just runs it without
  throttling; the viewer can pause, change speed, or "run until year X".

## Determinism

- The same seed and the same build produce the same history, every time.
- **Random numbers are counter-based:** a value is computed from
  (world seed, system, entity, day, purpose) by a hash, instead of being drawn
  from shared streams. Parallel updates therefore cannot change results,
  whatever order threads finish in.
- No `/fp:fast`; `/fp:precise` everywhere. Parallel sums are combined in a
  fixed order.
- Guaranteed for one build on one machine type. Because the project is
  Windows/MSVC only, that covers every way it will be run.
- A determinism test runs the same seed twice and compares a hash of the full
  state.

## Multithreading

- A small job system runs per-cell and per-entity updates in parallel.
- Systems that read neighbours write into a second buffer that is swapped in
  afterwards, so results do not depend on update order.
- Phase 0 builds the job system; it gets real work from Phase 1 on.

## Event log

The event log is the history: the legends viewer reads it, and it is how we
debug.

Each event records:

- a unique ID and the day it happened
- where (a cell, when it has one)
- a type, plus that type's details
- participants, each with a role (e.g. *deceased*, *heir*, *killer*)
- causes: links to earlier events, or to a recorded condition such as
  "harvest at 40% of normal"
- importance: *trivial*, *notable*, *major* or *epochal*

Each entity keeps an index of the events it took part in, so a person's or
organization's life can be listed instantly.

**Keeping volume in check.** Runs are open-ended, so recording everything
individually would grow without limit. So:

- historical figures get full detail
- population-level change is recorded as aggregate events
  ("famine in the Nile valley, a third of the population died")
- trivial events can be dropped or thinned out by a setting

## History through time

- Every 10 simulated years a compact **snapshot** of map-level state is kept:
  per-cell population, culture, language, religion, controlling organization.
- Historical maps are drawn from these snapshots, so you can scrub through time
  without the whole world state being kept for every day.
- The interval is a setting.

## Save and load

- A versioned binary format: a header with a format version, then each table,
  the event log, and the snapshots, as separate sections.
- Loading an older version either upgrades it or refuses clearly; it never
  loads garbage.

## What Phase 0 delivers

- `sim/`: the time and scheduler, entity tables and IDs, random numbers, job
  system, event log, snapshots, save/load.
- A **placeholder test system** that fires dummy events with causes, so the
  whole pipeline works end to end. It is removed once real systems exist.
- `cli/`: generate a world to a given year and save it.
- `app/`:
  - time controls: pause, speeds, run until year X
  - event browser, filterable by date, type, importance and participant;
    following an event's causes back
  - basic entity inspector
  - save/load
- Tests: unit tests, the determinism test, and a save → load → compare test.
- **Continuous integration:** GitHub Actions on Windows builds Debug and
  Release and runs the tests on every pull request.

## Not in Phase 0

The map, climate, people and everything else on the roadmap. God powers get
their hook (a single place where interventions enter the simulation as
recorded events) but no actual powers.

## Open-ended runs

A run has no end date: it continues until the user stops it, and can be
resumed from a save. Because history can grow without limit:

- **The event log and snapshots stream to disk** in chunks (one chunk per
  century of simulated time), instead of being held in memory. The viewer loads
  the chunks it needs on demand. Memory use is therefore set by the size of the
  living world, not by how long the run has lasted.
- **Per-entity event indexes** live in the same chunks, so a person's or
  organization's history is still found quickly without scanning everything.
- A save is the current world state plus a reference to its history chunks,
  so saving stays fast however old the world is.
- Snapshot interval and event thinning are settings, not tied to a planned run
  length.

## Open questions

None at the moment.
