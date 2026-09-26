# DeepSpace — The Playable POC: Integrated Build Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan wave by wave. Steps use checkbox (`- [ ]`) syntax for tracking. **At most three tracks run at once, and no track ever runs an editor step itself** — editor steps go to the queue (see *The editor queue*).

**Goal:** The developer starts in a lived-in hauler, walks to the helm, flies, picks
a star on the nav screen, aims at it, walks away while the jump winds, jumps, and
arrives somewhere that looks different — a new sun at its true size, neighbours
shifted, the sunlight on the deck a new colour — and can then fly the approach to a
planet in two minutes of exponential closing.

**Architecture:** Four designs, built together. Procgen is a pure C++ library
behind `UUniverseSubsystem`, the only authority on what exists and which system a
position is in. The sky is pure projection arithmetic behind one actor, `AShipSky`,
that polls and stores nothing. Navigation is a pure state machine, `FShipNavState`,
inside `UShipSubsystem`, which remains the only writer of the flight state. The
lived-in ship is a generated Python layer over the existing layout pipeline, plus
two small C++ additions (a synthesised hum and dimming lamp panels).

**Specs (read the one your track touches before starting):**
- `docs/superpowers/specs/2026-09-25-procgen-foundation-design.md` — *procgen*
- `docs/superpowers/specs/2026-09-25-the-sky-design.md` — *sky*
- `docs/superpowers/specs/2026-09-25-navigation-and-arrival-design.md` — *nav*
- `docs/superpowers/specs/2026-09-25-lived-in-ship-design.md` — *lived-in*

Where this plan and a spec disagree, **this plan wins**, because it is where the
four were reconciled. Every disagreement is in *Conflicts, resolved* with the
reason. The specs get a one-paragraph pointer to it in Wave 0; they are not
rewritten.

**Status:** Decisions answered; restructured into slices. See *Revision, 2026-09-25*.

## Revision, 2026-09-25 (supersedes the wave structure and the hours below)

Three things this plan assumed turned out false, and one decision changed.

**Agents can compile.** The plan treated the constraint as "one editor", and
so had three tracks write several thousand lines of Unreal C++ that nobody
compiled until Queue 1. The real constraint is one *heavy Unreal process* at
a time on this machine. UBT's mutex is keyed on the engine install, so it
already spans checkouts (`-waitmutex` queues rather than failing), and
`Tools/ue_lock.sh` now puts every build *and* headless test run behind one
flock in the git common directory. Each track therefore works in its own git
worktree under `.worktrees/`, builds with `./build.sh` and tests with
`./test.sh` there, and hands over a branch that compiles and passes. The
orchestrator's queue shrinks to integration: merge, `./rebuild.sh --force` on
main, the level build, the Blueprint check, and the developer playing.

**The queue timings were invented, and wrong by an order of magnitude.**
Measured on this machine: a cold build of the whole module in a fresh
worktree, 38 s; the full automation suite with editor start-up, 15 s. The
hour-denominated queue below does not describe anything real, and neither
does *Hours, honestly*. Nothing here is re-estimated; the next number written
down will be a measured one.

**It was ordered by layer, not by the question.** The POC exists to test
cruise, choose, jump, arrive. Work is now cut into three **slices**, each
ending playable, each a full vertical pass rather than a layer:

1. **Slice 1: the loop.** Procgen's systems, galaxy and `GetSystemAt`; the
   sky's pure layer, `AShipSky` and its materials; the drive, the jump, nav
   state, the opening placement and the `ds.Nav.*` commands; HUD bearing
   words; room moods and practical lamps. Destination chosen from the
   console. Ends with the developer flying cruise, drive, jump, arrive.
2. **Slice 2: choosing and hearing.** The chart chair (`AShipNavScreen`,
   `UNavigationWidget`), the hum as the jump's wind-up cue, lamp panels
   dimming, the nose caret, the glass veil, the eclipse, the procgen corpus and
   the priors ini.
3. **Slice 3: somebody's ship.** The clutter generator in **C++** (decision
   below), wear, the CVar write-back, CLAUDE.md and the ADR amendments.

All of cut-list items 1-7 stay in scope (developer, 2026-09-25); they sit in
Slices 2 and 3 so that none of them delays the first playable loop.

**Decision 5 was answered no.** ADR 0006 is not relaxed: the clutter generator
is C++ running at world start, like every other generator. `dressing.py`, the
Python-generated clutter baked into the level, and the ADR 0006 amendment in
Wave 3 Track B are withdrawn. See the revised lived-in spec. `Tools/rng.py`
and `rng_vectors.json` survive as a cross-check on `FGenStream`, no longer as
the basis of anything.

The other developer rulings: all of it is in scope with no mid-way
checkpoint; honest universe weights; the crude exponential drive as designed;
the hum follows the watts reaching the jump drive.

What survives unchanged from the plan below: *Done when*, *Global
constraints* (read "the editor queue" as "the integration queue"), *Conflicts,
resolved*, *Decisions taken in this plan*, and file ownership *within* a
slice. The wave-by-wave sections remain as the detailed specification of each
piece of work; the slices above say when each piece is done.

## Done when

The POC is playable when all of this is true in one session, launched with
`./launch.sh` and no console command:

1. The ship opens 40,000 km from a planet of the start system. The planet fills the
   cockpit glass, half lit, with the sun off to starboard and a patch of sunlight
   lying across the galley.
2. The ship is lit room by room in warm and cool moods, has clutter on its
   surfaces and scuffed skirting on its walls, and hums; the hum answers the
   throttle.
3. At the helm the in-system drive (`F`) closes on a planet exponentially; it
   resolves from a point to a disc with no visible moment of change.
4. At the chart chair the player plots one of the six nearest stars and engages.
5. Back at the helm the HUD gives the bearing in words, and the nose caret sits on
   the teal course marker when aligned.
6. The player walks to the galley; the jump fires on its own; streaks pass the
   window; the lights never flicker for it.
7. On arrival a different sun is ahead, the neighbour stars have shifted, and the
   background galaxy has not moved.
8. `Automation RunTests DeepSpace` is green, `validate_hauler.py` passes,
   `verify_level.py` passes, and `check_blueprints.py` passes.

**POC line A** is the same list with item 4 done from the console
(`ds.Nav.Plot`) and item 2 without clutter or wear. It is reached at the end of
Wave 2, and it is the first go/no-go playtest.

## Global constraints

- **All gameplay logic in C++.** Blueprints assign assets and expose tunables
  only (ADR 0002). Nothing in this plan edits a Blueprint graph.
- **One process uses the editor at a time.** That covers `./build.sh`,
  `./rebuild.sh`, `./launch.sh`, every `UnrealEditor-Cmd` commandlet, every
  automation run, and the developer playing in the editor. They form one queue.
- **No Live Coding on Linux.** A header change costs `./rebuild.sh --force`,
  roughly fifteen minutes with the restart. So header changes are **batched into
  one rebuild per wave**, and a track that finds it needs a header change after
  its wave's rebuild waits for the next wave rather than forcing an extra one.
- **Tunables are console variables**, read at use and never cached, so a
  playtest moves numbers without a rebuild. Settled values are written back as
  defaults once, at the end (Wave 3).
- **6c/12t desktop.** At most three authoring tracks. Anything multi-process
  (the dressing corpus, any sweep) runs as a single process under `nice -n 19`.
  When briefing a subagent, state that cap in the brief.
- **No modelling.** Meshes are `/Engine/BasicShapes/Sphere`, the prototype
  cubes, and procedural code. Materials are authored by Python commandlets.
- **Headless test verdicts are in the log**, not stdout:
  `grep "Test Completed" Saved/Logs/DeepSpace.log | tail`. If an editor is open,
  the log is `DeepSpace_2.log` — which should never happen, because the queue
  forbids it.

## The editor queue

This is the critical path. Pure C++ authoring, Python tools work, spec edits and
test writing parallelise freely across tracks; every step below marked
**[editor]** does not, and nothing makes it parallel.

**Rules.**

1. **One runner.** The orchestrating session runs every **[editor]** step. A track
   finishes by handing over a *ready for queue* note: which files changed,
   whether a header changed, and which queue steps its work needs.
2. **The order within a queue slot is fixed** and is written out per wave below.
   The standing order is: material commandlets → build → automation tests →
   input commandlets → `validate_hauler.py` (not editor, but gating) →
   `build_hauler.py` → `verify_level.py` → `check_blueprints.py` → the developer
   plays.
3. **A level build always follows the rebuild that changed a generated actor's
   components** (CLAUDE.md: a placed actor keeps the hierarchy it was saved
   with). `hauler_sky`, the hum sources and the nav screen are all new generated
   actors.
4. **A failed step holds the queue for the steps that depend on it.** While the
   owning track writes the fix, the runner may run any step that does not depend
   on the broken one (a material commandlet does not depend on a compile fix).
   It never runs a build over a half-fixed tree.
5. **The developer playing is a queue step.** While the editor is open for a
   playtest, no track's work can be compiled or built. Playtests are therefore
   placed at the end of each queue slot, and authoring for the next wave
   proceeds during them.

**Timing assumptions** used in every estimate: `./build.sh` of new files only,
10 min; `./rebuild.sh --force` with a restart, 15 min; one headless automation
run, 5 min; one commandlet, 3 min; a level build plus verify, 8 min. Each slot
also carries a fix-loop allowance, which is where the time really goes.

## Conflicts, resolved

Each row is a place where two or more specs disagreed. The resolution is binding
on the implementing track. *Yields* names the spec whose text is superseded.

| # | Conflict | Resolution | Yields |
|---|---|---|---|
| 1 | **Which system am I in.** Nav and the sky call `UUniverseSubsystem::GetCurrentSystem()` and nav calls `SetCurrentSystem(Id)` on arrival. Procgen's revision deleted both: the system is asked of a position. | **Procgen wins.** There is no `GetCurrentSystem` and no `SetCurrentSystem`. Everyone asks `GetSystemAt(ShipPosition)`. Arrival is `FlightState.JumpTo(Position)` and nothing else; because the standoff is far inside `InSystemRadiusLy` (0.25 ly), the answer changes by itself. Nav's risk about calling `GetCurrentSystem` before procgen's begin-play disappears, since every procgen query works from `Initialize`. | nav decisions 1, 5, 9; sky decision 7 |
| 2 | **Push or poll the sky.** Nav's counter-frame calls `AShipSky::SetSystem` and `SetInTransit`. The sky's revision removed both and polls `LocalSystem::Serial`. | **The sky wins.** `AShipSky` has no setters. `LocalSystem::Serial` is `UShipSubsystem::GetJumpSerial()`, `LocalSystem::InTransit` is `IsInTransit()`. The counter-frame's own serial check covers only its own things: re-scattering the near field, the course marker, hiding `DistantStars` in transit. | nav decision 8, step A5 |
| 3 | **Start placement, three owners.** Procgen places the ship from `UUniverseSubsystem::OnWorldBeginPlay` at `StartPosition()` (1.5 × the outermost orbit, marked for deletion by nav). The sky builds its placeholder around the origin and places nothing. Nav places the ship from `UShipSubsystem::OnWorldBeginPlay`, 40,000 km from planet 0 on its sunward side. | **One owner, written once: `UShipSubsystem::OnWorldBeginPlay`.** Procgen's placement, `bPlaceShipAtStart` and `FGalaxyGenerator::StartPosition` are never written. The framing is the sky's, not nav's sunward one (sunward gives a full disc, not a half-lit one): the **largest planet** of the start system 40,000 km dead ahead along the ship's +X, its star 90° to starboard. The geometry is a pure function, `NavStart::OpeningPlacement(const FStarSystem&) -> {FUniversePosition, FQuat}`, in `Ship/NavStart.h/.cpp`, so it is tested with no world. Escape hatch: `ds.Nav.PlaceAtStart 0`. | procgen decision 13; nav decision 9; sky decision 7 |
| 4 | **The sky's placeholder system**, a second generator tolerated until procgen lands. | **Not built.** Procgen's library compiles in the same queue slot as the sky's pure layer, so `LocalSystem::Current` is written against `UUniverseSubsystem` from the start, with `FSkySystem::FromSystem` as its adapter. The sky's tests use a hand-written constant fixture, `Tests/SkyTestFixtures.h` (a star, three planets, one moon: literals, no RNG), which is test data and not a generator. The starfield seed is `Derive(GetRootSeed(), Label("sky.starfield"))` from day one; `StarSeed` survives only as the no-universe fallback. | sky decisions 4, 7 |
| 5 | **Procgen's one visible change** (real neighbours as white dots on the 120 m shell) versus the sky's galaxy starfield and `NeighbourStars`. | **Procgen's step 5 is not built.** The sky's `NeighbourStars` are the real neighbours. Procgen's test "the first distant star is in the direction of the nearest stub" moves to `ShipSkyTest` as "`NeighbourStars` instance 0 points at the nearest stub". If the sky is ever cut, procgen's step 5 is the fallback (about 1 h). | procgen *The one thing on screen* |
| 6 | **Two blackbodies.** Procgen puts `StarColour` on `UUniverseSubsystem`; the sky has `SkyColour::Blackbody`. | **The sky's.** `UUniverseSubsystem::StarColour` is not written. | procgen *The seam* |
| 7 | **Two things called "the drive".** The sky's in-system drive has `SetDriveEngaged(APawn*, bool)`; nav's jump has `SetDriveEngaged(bool)`, `EDriveState`, and HUD words `DRIVE WINDING`/`DRIVE READY`. | **The in-system drive is "the drive"; the fold is "the jump".** Sky keeps `IA_Drive`, `ds.Drive.*`, `SetDriveEngaged(Commander, bool)`, `IsDriveEngaged()`. Nav becomes `SetJumpEngaged(bool)`, `IsJumpEngaged()`, `EJumpState {Idle, Winding, Ready, Transit}`, `GetJumpState()`, `NavText::Jump(EJumpState)`, and the HUD reads `JUMP WINDING` / `JUMP READY · Kessa · 12° to port`. Nav's CVars stay `ds.Nav.*`. | nav decisions 3, 5, 6, 7 |
| 8 | **The hum's `EngineFeed`** is engine *satisfaction*. Nav makes an idle engine want 0 W, and a zero want has satisfaction 1, so the hum would sit at full feed whenever the ship is idle and ignore the split entirely. | **`EngineFeed = clamp(GetConsumerShare(Engine) / ds.Nav.WindingWant, 0, 1)`**: watts actually delivered over what winding asks for. Idle, the drone sits in its low register; engage the jump and it rises and brightens as the split feeds it; charged, it settles back. That makes the hum the diegetic wind-up cue nav admits it lacks at the default split. **Needs the developer's ear: decision 4.** | lived-in decision 9 |
| 9 | **A fixed 2.4 AU standoff** makes three arrivals in four a two-pixel red dwarf (nav's own risk), and a G star with planets out to 5 AU would arrive among its planets. | **`Standoff = max(ds.Nav.StandoffAU × √L, 1.5 × outermost orbit)`**, `StandoffAU` default 2.4. Constant irradiance at arrival: an M dwarf at L = 0.01 is met at 0.24 AU as a ~15 px disc, a Sun-like star at 2.4 AU as ~7 px, and you always arrive outside the planets ("to a system, never into one"). One function in `StepNavigation`; the CVar stays. | nav decision 5, *Fakes* |
| 10 | **The drive can fly out of the system.** Room is the distance to the nearest surface, so leaving a planet the drive accelerates without bound, passes `InSystemRadiusLy`, and `GetSystemAt` goes empty while the sky's serial has not changed. | **The system's edge is a surface.** `LocalSystem::NearestSurfaceDistance` also counts `InSystemRadius − |ship − star|`, so the drive slows into the edge as it does into a planet. Under the drive the ship never leaves its system; you leave by jumping. During transit the subsystem sets room to 0, so the drive gives only cruise speed until `JumpTo` lands. | sky decision 8 |
| 11 | **`Poisson` at `Max`.** Procgen: Knuth, stop at `Max`. Lived-in proposed redraw-16-then-clamp. | **Procgen's, as written.** Its known values pin it, and at the means used the cap fires at most once in five thousand draws. `rng.py` mirrors it. | lived-in decision 4 |
| 12 | **`ShipFlightState.*` and `ShipSubsystem.*`** are edited by the sky (drive room, `bDrive`, `DriveTau`, `DriveFloor`, booster-stretched τ) and by nav (charge parameter, `SpendJumpCharge`, `JumpTo`, nav state, engine want). | **One owner: Wave 2 Track A** writes both specs' changes. The sky's additions are specified to the field; Track A implements them. | — |
| 13 | **`ShipCounterFrame.*`** is edited by the sky (starfield generator, radius, count, custom data, mote fade), by procgen (step 5, now not built) and by nav (course marker, serial, streaks). | **One owner: Wave 2 Track B.** | — |
| 14 | **`build_hauler.py`, `verify_level.py`, `hauler_layout.py`, `props.py`, `placement.py`, `floorplan.py`, `validate_hauler.py`** are edited by the lived-in ship (most of it), the sky (spawn `hauler_sky`, strip the template `DirectionalLight`, glass shadows and material, mote material) and nav pass B (the chart mount and actor). | **One owner in every wave: Track C.** The sky and nav hand Track C a function-level spec (`place_sky`, `place_nav_screen`, the verifier checks) and review the diff; they do not edit the files. | — |
| 15 | **When to read the exposure.** The sky sets `ds.Sky.Exposure` to "whatever auto exposure settles on in the galley" before anything changes. The lived-in moods change the galley's brightness. | **Read it after the moods are built (end of Queue 1) and before `AShipSky` exists (Queue 2).** Otherwise the fixed exposure is calibrated against a galley that no longer exists. | sky step 6 |
| 16 | **Clutter against the chart.** Lived-in dresses the cockpit desk wings; nav pass B turns the starboard desk screen into the chart. | The chart's panel footprint plus 15 cm, and the strip between it and the starboard seat, are an `exclude` on the cockpit desk — the same rule as the laptop's. | lived-in step 6 |
| 17 | **Procgen's requirements from nav**: `operator==` and `GetTypeHash` for `FSystemId`. | Written by procgen in Wave 1, in `StarSystem.h`. The procgen sentence nav asked to amend is amended: *"`UShipSubsystem` stores no universe data beyond the id of the system it is steering for."* | procgen *The seam* |

**Verified while planning:** procgen's whole known-value table (the hash rows,
the stream's first three outputs, `Unit`, the chain from root `20260925` to
sector `(0,0,0)` slot 0, and both Poisson counts) reproduces exactly from a
scratch Python SplitMix64/FNV-1a. The table has no typo, and Wave 0's `rng.py`
starts from a known-good reference.

## Decisions taken in this plan

These have an obvious default, so they are decided here and recorded rather
than put to the developer. Each can be reopened.

- **Circular, coplanar, frozen orbits** (procgen sign-off 6). Nothing on screen
  can show eccentricity.
- **Single-platform floating-point determinism** (procgen sign-off 4), as a
  recorded debt.
- **Designations for every planet, given names only for the inhabited**
  (procgen sign-off 3).
- **The jump fires by itself** once plotted, aligned, engaged and charged (nav
  decision 2). A final button is the anti-chore principle's definition of a
  chore. If it feels like the game acting without you, the answer is a softer
  cue before the fold, never a confirm.
- **`ds.Nav.FoldDraw` ships at 0 W.** Try 350 once in the Queue 2 playtest.
- **The nose caret on the HUD**, not a tick on the glass (nav decision 3).
- **Manual exposure**, with narrowed auto exposure as the ready fallback (sky
  decision 6).
- **The opening planet is the start system's largest.** The biggest thing in the
  window is the best first shot, and it is one line to change.
- **Lived-in items 7–10 are not in this plan**: wall dressing, floor-band
  clutter with its pocket check, the readouts, desire lines. They are behind the
  lived-in spec's own second cut, and the readouts would cost a third rebuild in
  the wave that can least afford it. See *After the POC*.
- **`M_SkyBand`** (the Milky Way glow) is not in this plan. The sky marks it
  "optional, cut first".
- **The drive state survives a jump.** It is a lever, and levers stay where you
  left them. Arriving with the drive on means closing on the new star.

## File ownership

Owners are per wave; nobody else edits the file in that wave. "—" means untouched
in that wave.

| File | W0 | W1 | W2 | W3 |
|---|---|---|---|---|
| `Universe/GenSeed.*`, `GenStream.*`, `GenPriors.h`, `UniverseUnits.h` | A (headers) | A | — | — |
| `Universe/StarSystem.h`, `StarSystemGenerator.*`, `GalaxyGenerator.*`, `SystemNames.*`, `SystemDescription.*` | A (`StarSystem.h`) | A | — | — |
| `Universe/ProcGenPriorsConfig.*`, `UniverseSubsystem.*`, `Config/DefaultGame.ini` | A (header) | A | — | — |
| `Sky/SkySystem.*`, `SkyProjection.*`, `SkyColour.*`, `SkyStarfield.*`, `SkyMaterialContract.h` | A (headers) | B | — | — |
| `Sky/LocalSystem.*` | A (header) | B (null-world branch) | B (live branch, adapter) | — |
| `Sky/ShipSky.*` | — | — | B | B (write-back) |
| `Ship/ShipFlightState.*`, `Ship/ShipSubsystem.*` | — | — | A | B (write-back) |
| `Ship/ShipNavState.*`, `Ship/NavStart.*`, `UI/NavText.*` | — | — | A | — |
| `UI/ShipHUDWidget.cpp` | — | — | A | — |
| `Ship/ShipCounterFrame.*` | — | — | B | B (write-back) |
| `Player/DeepSpaceCharacter.*` | — | — | B | — |
| `Ship/ShipNavScreen.*`, `UI/NavigationWidget.*` | — | — | — | A |
| `Ship/ShipHumVoice.h/.cpp`, `ShipHumComponent.*`, `ShipHumSource.*`, `DeepSpace.Build.cs` | — | — | C | — |
| `Ship/ShipLightingSubsystem.*` | — | — | C | — |
| `Tools/setup_sky_materials.py`, `Tools/sky_material_contract.json`, `Tools/sky_probe.py` | — | B | — | — |
| `Tools/setup_flight_input.py` | — | — | B | — |
| `Tools/rng.py`, `Tools/rng_vectors.json` | B | C | — | — |
| `Tools/procgen_corpus.py` | — | A | — | — |
| `Tools/hauler_layout.py`, `props.py`, `placement.py`, `floorplan.py`, `dressing.py`, `validate_hauler.py`, `build_hauler.py`, `verify_level.py`, their tests | — | C | C | C |
| `docs/superpowers/specs/*` (pointer paragraphs) | C | — | — | — |
| `CLAUDE.md`, `docs/decisions/*` | — | — | — | B |

Tests belong to the track that owns the code under test.

---

## Wave 0 — Contracts (no editor)

**Why a wave for this:** three tracks are about to write code against each
other's types. Freezing the declarations first means Wave 1 can be written in
parallel and compiled once.

**Hours:** 1.5 h wall, about 4 h of effort.

### Track A — interface headers (1.5 h)

- [ ] Write the declarations, not the bodies, for everything that crosses a
      track boundary, exactly as the specs give them with this plan's
      resolutions applied: `GenSeed.h`, `GenStream.h`, `GenPriors.h`,
      `UniverseUnits.h`, `StarSystem.h` (with `FSystemId` `operator==` and
      `GetTypeHash`), `UniverseSubsystem.h` (no `StarColour`, no placement, no
      `bPlaceShipAtStart`), `SkySystem.h` (no `Placeholder`), `LocalSystem.h`
      (`Current` returns `FSkySystem` **by value**, since procgen caches
      nothing and a reference would need a cache), `SkyMaterialContract.h`.
- [ ] Write, as a comment block at the top of `ShipNavState.h` (not compiled
      into anything yet), the `UShipSubsystem` additions Wave 2 will make, with
      conflict 7's names: `SetJumpEngaged`, `GetJumpState`, `GetJumpSerial`,
      `IsInTransit`, `GetTransitProgress`, `GetCourseDirectionShipLocal`,
      `PlotCourse`, `ClearCourse`, `GetPlottedSystem`, `GetChart`,
      `SetDriveEngaged(APawn*, bool)`, `IsDriveEngaged`.

### Track B — the RNG reference (1.5 h)

- [ ] `Tools/rng.py`, a literal mirror of `GenSeed` and `FGenStream` (lived-in
      decision 4), with `Poisson` per conflict 11.
- [ ] `Tools/rng_vectors.json`: procgen's whole known-value table (not only the
      four rows the lived-in spec lists), the first 16 `next_u64` from seeds
      0, 1 and `0xDEADBEEF`, and the 32-draw sampler sections.
- [ ] `Tools/test_rng.py`: the table reproduces exactly; doubles compared as
      `float.hex`. *Runs:* `python3 Tools/test_rng.py`.

### Track C — spec pointers (1 h)

- [ ] Add a short *Integration, 2026-09-25* paragraph to each of the four specs,
      pointing at this plan's conflict rows by number. Do not rewrite spec text;
      the specs record why, and this plan records what was reconciled.

**Queue 0:** empty.

---

## Wave 1 — Pure layers and first light

**Hours:** 9.5 h authoring (wall), then 5 h of queue. About 31 h of effort.

### Track A — procgen, whole (9.5 h, no editor)

Procgen spec steps 1–4 and 7, with the resolutions above. Skip step 5.

- [ ] Seed, stream, priors, units, and `GenSeedTest.cpp` / `GenStreamTest.cpp`
      with the known-value table. `GenStreamTest` also reads
      `Tools/rng_vectors.json` (lived-in decision 4), so the two sides cannot
      drift. *(1.5 h)*
- [ ] Star systems, names, descriptions, `StarSystemGenerationTest.cpp`. *(3 h)*
- [ ] `FGalaxyGenerator` with `StartSystem` and **without** `StartPosition`;
      `GalaxyGeneratorTest.cpp`. *(2 h)*
- [ ] `ProcGenPriorsConfig`, `UniverseSubsystem` with `ResolveSeed` and the
      `ds.Universe.*` commands, the ini sections, `UniverseSubsystemTest.cpp`
      **minus** the placement assertions (they move to Wave 2's jump test).
      *(2 h)*
- [ ] `ProcGenCorpusTest.cpp` and `Tools/procgen_corpus.py`, the Python checked
      against a hand-made TSV. *(1 h)*

### Track B — the sky's pure layer and materials (8 h, no editor)

Sky spec steps 1, 2 and 4.

- [ ] `Tools/sky_probe.py`, run against a real start system's numbers as the
      spec gives them rather than the placeholder's. *(0.5 h)*
- [ ] `SkySystem` (with `FromSystem`), `SkyProjection`, `SkyColour`,
      `SkyStarfield` (on `FGenStream`), `LocalSystem` null-world branch and
      `NearestSurfaceDistance` including the system edge (conflict 10),
      `SkyMaterialContract.h` + JSON. Tests: `SkyProjectionTest.cpp`,
      `SkyStarfieldTest.cpp`, `LocalSystemTest.cpp`, with
      `Tests/SkyTestFixtures.h` (conflict 4). *(4.5 h)*
- [ ] `Tools/setup_sky_materials.py`: `M_SkyBody`, `M_SkyStar`,
      `M_SkyStarfield`, `M_SkyGlass`, `MPC_Sky`, reading the contract JSON.
      *(3 h)*

### Track C — the lived-in ship's Python, first cut and clutter (8.5 h, no editor)

Lived-in steps 1, 5 and 6. Track C's Python never waits on the queue; whatever
is ready joins the next level build.

- [ ] Moods and practicals (items 1, 2): `ROOM_MOOD`, `PRACTICALS`,
      `kelvin_to_rgb`, per-room lamp roles, `EMISSIVE` from moods,
      `place_lights` colour and shadows; tests in `test_placement.py`. **Do
      this first** — Queue 1 needs it. *(2.5 h)*
- [ ] Surfaces, `ANCHOR`, the anchoring check in `resolve_props`;
      `test_dressing.py` vectors and anchoring. *(2 h)*
- [ ] Clutter templates, `dressing.py`, `DRESSING_SEED`, `LIVED_IN`,
      `ROOM_DRESSING`, collision by band in `build_hauler.spawn_box`, the
      500-seed cheap corpus (single process, `nice -n 19`). *(4 h)*
- [ ] *Runs, no editor:* `python3 Tools/test_floorplan.py && python3
      Tools/test_placement.py && python3 Tools/test_dressing.py && python3
      Tools/validate_hauler.py`.

### Queue 1 (5 h, strictly in this order)

1. **[editor]** `setup_sky_materials.py` commandlet. Depends on no new C++;
   may run the moment Track B hands it over, even before Tracks A and C
   finish. *(0.25 h + 0.75 h allowance: Python material authoring is the sky's
   named fiddliest risk; if a node cannot be made from Python, hand-author that
   one material and put its graph in the script's docstring.)*
2. **[editor]** `./build.sh` — new files only, no existing header touched, so no
   `--force`. *(0.25 h + 0.75 h compile fixes)*
3. **[editor]** `Automation RunTests DeepSpace`. The new suites are
   `DeepSpace.Universe.*` and `DeepSpace.Sky.{Projection,Starfield,LocalSystem,MaterialContract}`;
   everything existing must stay green. *(0.25 h + 0.75 h fix loop)*
4. **[editor]** `Automation RunTests DeepSpace.Universe.Corpus` → the developer
   reads `Saved/procgen_describe.txt` and `python3 Tools/procgen_corpus.py`
   output. Do the systems read as places or as rolls? *(0.25 h + reading)*
5. `validate_hauler.py` → **[editor]** `build_hauler.py` → **[editor]**
   `verify_level.py`, with the moods and practicals (and clutter, if Track C
   has it). *(0.5 h)*
6. **[editor, developer]** Open the level. Walk it: does it already feel
   different? Then, **before anything else changes**, read the galley's
   auto-exposure EV from the HDR visualisation and hand it to Wave 2 Track B as
   `ds.Sky.Exposure`'s default (conflict 15). Run `ds.Universe.Describe` and
   `ds.Universe.Near 15` in play. *(0.75 h)*

**Tracks during Queue 1:** Wave 2 authoring may start as soon as Queue 1 step 3
is green, since Wave 2 compiles against the Wave 1 code.

---

## Wave 2 — The wiring: one header batch, one rebuild

Every change to an existing header in the whole POC, except the nav screen's
new classes, lands here, so there is one `./rebuild.sh --force` instead of three.

**Hours:** 10 h authoring (wall), then 7.5 h of queue. About 34.5 h of effort.

### Track A — the ship's simulation core (9 h, no editor)

Owns `ShipFlightState.*`, `ShipSubsystem.*`, `ShipNavState.*`, `NavStart.*`,
`NavText.*`, `ShipHUDWidget.cpp`.

- [ ] **Flight state**, both specs: the sky's `SetDriveRoom`, `bDrive`,
      `DriveTau`, `DriveFloor`, velocity along the nose with the drive on and
      the clamp on disengage; nav's `SecondsFromCold` parameter,
      `SpendJumpCharge`, `JumpTo`. Tests in `ShipFlightStateTest.cpp` from both
      specs. *(2 h)*
- [ ] **`FShipNavState`, `NavText`** (nav A1, with conflict 7's names).
      `ShipNavStateTest.cpp`, `NavTextTest.cpp`. *(1.5 h)*
- [ ] **`NavStart::OpeningPlacement`** (conflict 3) and a pure test: the largest
      planet is 40,000 km ± 1 km along +X of the returned orientation, and the
      star is within 5° of its +Y. *(0.5 h)*
- [ ] **`UShipSubsystem`**: `InitializeDependency<UUniverseSubsystem>()`; the
      nav state and `StepNavigation`; the standoff rule (conflict 9); engine
      want following the jump; charge only while engaged; every `ds.Nav.*` CVar
      and command; `OnWorldBeginPlay` placement behind `ds.Nav.PlaceAtStart`;
      the drive's room from `LocalSystem::NearestSurfaceDistance` (0 in
      transit); `SetDriveEngaged(Commander, bool)` gated on the pilot;
      boosters stretching τ; `ds.Drive.Tau`, `ds.Drive.Floor`. Before writing
      the placement, grep every test that calls `World->BeginPlay()` for an
      assumption about the ship's absolute position. *(3 h)*
- [ ] **Tests**: `ShipPowerConsumersTest.cpp` updated (engage before charge
      assertions; idle engine wants 0 W). New `ShipJumpTest.cpp` as nav A3
      specifies, except that arrival is checked as
      `GetSystemAt(ShipPosition)->Stub.Id == destination` (conflict 1) and the
      standoff against conflict 9's rule; plus procgen's moved placement
      assertion. *(1 h)*
- [ ] **HUD** (`ShipHUDWidget.cpp` only): `PlaceLine`, `DriveLine` with the
      jump word and bearing, the nose caret found by name. *(1 h)*

### Track B — the sky actor, the frame, the input (8 h, no editor)

Owns `ShipSky.*`, `LocalSystem.*`, `ShipCounterFrame.*`,
`DeepSpaceCharacter.*`, `setup_flight_input.py`.

- [ ] **`AShipSky`**: constructor components, runtime proxies, `SyncToShip`,
      the sun, exposure with Queue 1's EV as the default, the veil into
      `MPC_Sky`, `ds.Sky.*` CVars, `ds.Sky.Goto`. **No `SetSystem`, no
      `SetInTransit`.** *(3 h)*
- [ ] **`LocalSystem` live branch**: `Current` from
      `FromSystem(*GetSystemAt(ShipPosition), GetSystemsNear(...))`; `Serial`
      and `InTransit` from `UShipSubsystem`. *(0.5 h)*
- [ ] **Counter-frame**, both specs: `SkyStarfield` into `DistantStars` with
      custom data, the new radius and count, the mote fade; nav's runtime
      `CourseMarker` sized in pixels from `GetPixelAngle()` ×
      `GetDomeRadius()`, `BuiltForSerial` re-scattering the near field, the
      streaks, hiding `DistantStars` and the marker in transit. *(3 h)*
- [ ] **Character**: `DriveAction` `UPROPERTY` and its handler calling
      `SetDriveEngaged`. **`setup_flight_input.py`**: `IA_Drive` on `F`,
      checked against `IMC_Default` for clashes, and `drive_action` on the CDO.
      *(1 h)*
- [ ] **Tests**: `ShipSkyTest.cpp` (with conflict 5's neighbour assertion, and
      the opening planet's subtense from the pilot seat checked against
      `2·asin(R/d)` of the real start planet), `ShipCounterFrameTest.cpp` for
      the new radius and count, `UniverseFrameTest.cpp` extensions from nav
      A5. *(0.5 h)*

### Track C — the hum, the lamps, and every level script (10 h, no editor)

Owns the hum files, `ShipLightingSubsystem.*`, `DeepSpace.Build.cs`, and every
level tool.

- [ ] **The hum** (lived-in item 4) with conflict 8's `EngineFeed`:
      `AudioMixer` in `DeepSpace.Build.cs`, `FShipHumVoice`,
      `UShipHumComponent`, `AShipHumSource`, `HumVoiceTest` exactly as lived-in
      step 3 specifies. **This is the repository's first audio code on Linux**:
      spike `USynthComponent` producing any tone first, before writing the
      voice. *(5 h)*
- [ ] **Lamp panels dim** (item 6): `Power.Lamps`, `FShipLamp`, the dynamic
      `Colour`, `LampPanelsDimTest`. *(1.5 h)*
- [ ] **Level scripts for everyone**: `place_sky` (spawn `hauler_sky`, assign
      mesh, materials, MPC; `DirectionalLight` into `SPACE_STRIPS`; glass
      `cast_shadow = False` and `M_SkyGlass`; `M_SkyStar` on the motes),
      `HUM_SOURCES` and their spawning, `Power.Lamps` tags; `verify_level.py`
      checks for all of it (sky spec step 6, lived-in step 4). *(1.5 h)*
- [ ] **Wear** (item 5): kick band in `floorplan.py` with `test_floorplan.py`
      updated, `furniture_faded`/`furniture_replaced`, the plain clutter roles.
      *(2 h)*

### Queue 2 (7.5 h, strictly in this order)

1. **[editor]** `./rebuild.sh --force`. Headers changed on the flight state, the
   subsystem, the counter-frame, the character and the lighting subsystem; new
   classes for the sky actor and the hum. *(0.25 h + 1.5 h compile fixes; this is
   the biggest batch in the plan, and the allowance is honest about it)*
2. **[editor]** `Automation RunTests DeepSpace`, fix and repeat. *(0.25 h +
   1.75 h fix loop)*
3. **[editor]** `setup_flight_input.py` commandlet (needs `DriveAction`
   compiled). *(0.25 h)*
4. `validate_hauler.py` → **[editor]** `build_hauler.py` → **[editor]**
   `verify_level.py`. Must follow step 1: `hauler_sky` and the hum sources are
   new generated actors. *(0.5 h)*
5. **[editor]** `check_blueprints.py`; recompile and save
   `BP_DeepSpaceCharacter`; `strings -a
   Content/Blueprints/BP_DeepSpaceCharacter.uasset | grep -i DriveAction`.
   *(0.5 h)*
6. **[editor, developer] POC line A playtest.** `./launch.sh`. In order:
   - the opening shot, the sunbeam, the band of the galaxy;
   - the approach flown with `F`, then walked during (sky *What needs eyes*
     1–2, 7);
   - lights off at the console: do the stars come out through the veil;
   - `ds.Nav.Near`, `ds.Nav.Plot 0`, `ds.Nav.Engage 1`; aim by words and
     caret; walk to the galley; listen to the hum as it winds (decision 4);
     the jump; the arrival (nav A7);
   - `ds.Nav.ChargeSeconds 5` and repeat until it stops being interesting;
     `ds.Nav.FoldDraw 350` once;
   - throttle and the hiss; a starved engine as a different ship, not a worse
     one (lived-in step 4).
   Tune by CVar; write the settled numbers down for Wave 3's write-back. *(3 h)*

**Go/no-go.** If the loop does not hold up here, Wave 3's chart chair is the
wrong next spend; the developer decides (decision 1).

---

## Wave 3 — The chart chair, and closing out

**Hours:** 3 h authoring (wall), then 5 h of queue. About 13 h of effort.

### Track A — the nav screen (3 h, no editor)

- [ ] `AShipNavScreen`, `UNavigationWidget` exactly as nav decision 7 and step
      B2, with conflict 7's names and `NavText::Jump`. `NavScreenTest.cpp`,
      spawning before `World->BeginPlay()`.

### Track B — write-back and records (3 h, no editor)

- [ ] Write Queue 2's settled CVar values back as defaults in the `.cpp` files
      (and `ShipFlightLimits`), so Queue 3's single rebuild carries them.
      *(1 h)*
- [ ] `CLAUDE.md`: sections for the universe (the seed, `GetSystemAt`, the
      console commands), the sky (the homothety, the material contract, the
      exposure), the jump and the drive (the two levers, their CVars), the hum.
      ADRs: amend 0005 (a fourth write path, `JumpTo`), 0006 (the dressing's
      Python generator, if decision 5 accepts it), 0003 (the course id on
      `UShipSubsystem`). *(2 h)*

### Track C — the chart mount and the second cut (2 h, no editor)

- [ ] `NAV_SCREEN` in `hauler_layout.py`, `test_placement.py` assertions,
      `place_nav_screen` and its verifier check (nav B1, B3). *(1 h)*
- [ ] The chart's clutter exclude (conflict 16); finish anything left of the
      clutter and wear. *(1 h)*

### Queue 3 (5 h, strictly in this order)

1. **[editor]** `./rebuild.sh --force` (new classes; defaults written back).
   *(0.25 h + 0.5 h)*
2. **[editor]** `Automation RunTests DeepSpace`. *(0.25 h + 0.5 h)*
3. `validate_hauler.py` → **[editor]** `build_hauler.py` → **[editor]**
   `verify_level.py`. *(0.5 h)*
4. **[editor, developer]** Seat the chart chair: the three seat tunables are
   per-instance `UPROPERTY` values set by `build_hauler.py`, so each nudge is a
   level rebuild (8 min), not a C++ one. *(1 h)*
5. **[editor, developer]** The whole *Done when* list, from a cold launch, with
   no console. Then walk the dressed ship at `LIVED_IN` 0.3, 1.0 and 2.0 and two
   other seeds (each a level rebuild): does it read as somebody's ship? *(1.5 h)*
6. **[editor]** Any final write-back: one more rebuild and test run. *(0.5 h)*

---

## Hours, honestly

| Wave | Authoring (wall) | Queue | Wave wall-clock | Effort |
|---|---|---|---|---|
| 0 — Contracts | 1.5 | 0 | 1.5 | 4 |
| 1 — Pure layers, first light | 9.5 | 5 | 14.5 | 31 |
| 2 — The wiring | 10 | 7.5 | 17.5 | 34.5 |
| 3 — Chart chair, close-out | 3 | 5 | 8 | 13 |
| **Total** | **24** | **17.5** | **≈ 42** | **≈ 82** |

About **80 hours of effort**, of which **about 42 are on the critical path** and
**17.5 are the editor queue**, which no number of tracks shortens. **POC line A**
arrives after about 34 hours of critical path. The four specs' own estimates
add to roughly 72 hours (procgen about 12 by this plan's reckoning, sky 20, nav
16.5, lived-in 23 to its second cut); integration saves about 4 (no
placeholder, no procgen step 5, no nav-to-sky calls, three rebuilds merged into
two) and costs about 10 more than it saves in fix-loop allowances, because the
specs estimated each step's happy path and the lived-in spec already had to
double one of its own. Treat 80 as the planning number and 42 as the
calendar.

For one developer supervising, that is five or six long sessions.

## Cut list — drop in this order

Each saves the hours shown. Nothing on the list breaks an item above it.

1. **The eclipse** (`DiscOverlapFraction`, `SunVisibleFraction`). Parking behind a
   planet no longer darkens the ship. *0.5 h.*
2. **The corpus TSV and `procgen_corpus.py`.** Keep `procgen_describe.txt`; lose
   the histograms. *1 h.*
3. **The priors ini** (`ProcGenPriorsConfig`, `ds.Universe.ReloadPriors`). Every
   prior tune becomes a rebuild. *1 h.*
4. **The glass veil** (`M_SkyGlass` veil term, `MPC_Sky`). Keep the glass casting
   no shadow. "Lights off and the stars come out" stops being true. *1.5 h.*
5. **Wear**: kick band and furniture buckets. *2 h.*
6. **Practical lamps.** Moods stay. *1 h.*
7. **The nose caret.** The bearing words stay; aiming is by words and the marker.
   *0.5 h.*
8. **Surface clutter** and the RNG mirror behind it. The ship is warm, lit and
   humming, with bare surfaces. *About 7.5 h across Waves 0–1.*
9. **The hum.** The jump winds silently; the HUD word is the only cue. *5 h, and
   it removes the only audio risk.*
10. **The chart chair** (nav pass B). The POC chooses from `ds.Nav.Plot`; *Done
    when* item 4 becomes a console command. *About 6 h, and most of Queue 3.*

**Never cut:** the known-value tests (every universe rests on them), the material
contract test (a misspelt parameter fails silently otherwise), the in-system
drive (without it, arriving has nothing to approach), the sunlight through the
glass, and the single rebuild per wave.

## After the POC (not in this plan)

Lived-in items 7–10 (wall dressing, floor-band clutter and the pocket check,
readouts, desire lines); `M_SkyBand`; the "lean in to the chart" key; the glass
boresight tick; a proper in-system drive and its owner (sky open question 5);
eccentricity by the design procgen already wrote; save/load of the visited set
and position; porting the dressing to C++ (ADR 0006).

## Decisions for the developer

The few that genuinely need you, most load-bearing first. Everything else is in
*Decisions taken in this plan*.

1. **Where is the POC line?** *Options:* (a) all three waves, about 42 h of
   calendar; (b) stop at POC line A, end of Wave 2, about 34 h, choosing from the
   console with no clutter; (c) POC line A with cut-list items 1–7 taken, about
   30 h. *Recommended:* plan all three, but treat the Queue 2 playtest as a real
   go/no-go and decide Wave 3 after flying it. *Why it matters:* it sets the
   spend before you have poked at the concept, and the chair and the clutter are
   the two things most likely to be worth less than they cost if the loop itself
   does not hold up.
2. **How honest is the universe?** Real neighbourhood weights (three suns in
   four are red dwarfs, gas giants about one system in ninety), the nearest
   system with a planet as home, and about one system in sixteen inhabited? Or a
   forced Sun-like home, or weights flattened towards yellow suns? *Recommended:*
   honest weights, nearest-with-a-planet home, `InhabitedChance = 0.08`; this
   plan's luminosity-scaled standoff (conflict 9) already fixes the "two-pixel
   red dot" arrival without touching the priors. *Why it matters:* every
   playtest judgement is made on whichever universe you pick, and a flattened
   one is a different game. All three are ini lines, so it is cheap to change
   but expensive to have judged wrongly.
3. **Is the crude in-system drive the approach you want to judge?** A toggle
   (`F`) that closes a tenth of the remaining distance every 1.5 s — two minutes
   from 1 AU to orbit, no inertia, 34 c at 1 AU, slowed by whatever surface is
   nearest (including, now, the system's edge). *Recommended:* yes, as the POC's
   in-system travel, with the nearest-body rule. *Why it matters:* the sky spec
   says it plainly — once you have flown two-minute exponential approaches, that
   is what the game is. The alternative is a destination-limited drive, which
   needs navigation's target and more time.
4. **Should the hum be the jump's wind-up cue?** Conflict 8 changes the lived-in
   hum so the drone follows watts delivered to the jump drive: low and dark when
   idle, rising as the split feeds a winding jump, settling when charged.
   *Recommended:* yes. *Why it matters:* at the default split nothing else aboard
   says the jump is winding, and this is ambient rather than an alarm — but a
   drone that settles when the drive is charged could read as being summoned,
   which is the anti-chore line. It needs your ear in the Queue 2 playtest; the
   fallback is the lived-in spec's satisfaction-driven hum, which is deaf to the
   split while idle.
5. **Accept the dressing's Python generator as a deliberate exception to ADR
   0006?** Clutter is generated by `Tools/dressing.py` and baked into the level,
   on an RNG mirrored line for line from `FGenStream` and pinned by shared
   vectors, until it is ported to C++. *Recommended:* yes, recorded in ADR 0006
   as an exception with a deletion condition. *Why it matters:* it is an
   architecture rule, and CLAUDE.md asks for those to be changed deliberately.
   The alternative is porting dressing to C++ now (many more hours, a rebuild
   per rule tweak) or no clutter at all (cut-list item 8).
