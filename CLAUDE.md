# CLAUDE.md — DeepSpace

Guidance for Claude Code when working in this repository.

## What this is

A procedurally generated space exploration game built in Unreal Engine 5.8,
emphasizing the scale of space: cruise, choose a destination, hyperjump, arrive.
The player lives aboard a ship they walk around inside and progressively upgrade.

Milestone 1 (walk the ship) is done. The current work is **the playable POC**
-- cruise, choose, jump, arrive -- in
`docs/superpowers/plans/2026-09-25-poc-build-plan.md`, whose *Conflicts,
resolved* table is binding on names and ownership.

**Read `docs/vision.md` before proposing any design.** It records what the game
is *for* — the register, the principle that scale is only felt in contrast, and
two rules that are easy to break by accident. The first is the **anti-chore
principle**: nothing may feel like a chore or a nuisance, the test being
whether the game ever tells the player they are *behind*. It rules out
optimisation loops and threats-on-a-timer alike, and it does **not** rule out
danger or difficulty. The second is that company aboard a ship is shared
presence, never division of labour. A feature can be fun in isolation and
still be wrong against both. If a decision contradicts the vision, change the
vision deliberately rather than working around it.

The developer knows general game development but is new to Unreal and has no
art or modeling experience. **Explain Unreal-specific concepts rather than
assuming them.**

## Engine

Unreal Engine 5.8.2 lives at `~/UnrealEngine/UE_5.8`, outside this repo.

**Always launch via `unreal-editor`**, never the raw binary — the wrapper forces
XWayland, without which the editor renders blurry on the 4K display. See
`docs/decisions/0001-engine-and-toolchain.md`, which also records the several
places Epic's Linux documentation is wrong for the precompiled binary.

## Commands

```bash
./build.sh          # canonical compile check — run after EVERY C++ change
./rebuild.sh        # clean rebuild; use when the editor says the module is stale
./rebuild.sh --force --launch   # close the editor, rebuild, reopen it
./launch.sh         # open DeepSpace: rebuilds first only if C++ is stale; focuses an open editor
unreal-editor DeepSpace.uproject    # open the project

# Regenerate IDE project files (after moving files or adding modules)
~/UnrealEngine/UE_5.8/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh \
    -project="$PWD/DeepSpace.uproject" -game -vscode
```

**`./build.sh` refuses to run while the editor is open**, because a build
there is worse than useless. The editor holds `libUnrealEditor-DeepSpace.so`
mapped, so UBT emits numbered hot-reload copies (`-0001`, `-0002`, …) and
leaves `Binaries/Linux/UnrealEditor.modules` pointing at the original — the
build *succeeds*, and the editor goes on running code that no longer matches
the source. The symptom is never a build error; it is something inexplicable
in play. An hour went into "the camera shakes when I move the mouse" before a
stray `-0001.so` turned out to be the whole story, and the identical symptom
a session earlier had appeared to heal itself only because the next launch
loaded a clean library. `./rebuild.sh` refuses to run while the editor is open
(`--force` kills it), clears the stray libraries, builds, and verifies the
manifest names a library newer than the newest source.

**The restart cannot be avoided on Linux for most C++ changes.** Live Coding,
Unreal's in-place patcher, is Windows-only — its build rule is gated on `Win64`
and no Linux binary ships. Linux has only the older Hot Reload, which handles
edits *inside function bodies* but not reflection changes: a new `UPROPERTY`,
`UFUNCTION`, component or class changes a layout that Blueprints were already
built against. Treat any header change as needing `./rebuild.sh --force --launch`.

**Parallel work goes in git worktrees under `.worktrees/`**, one per track,
each building with `./build.sh` and testing with `./test.sh` in its own tree.
The limit is not "one editor" but one *heavy Unreal process* on this machine,
and `Tools/ue_lock.sh` enforces it: every build and headless test run, in
every worktree, waits on one flock in the git common directory. Never call
`Build.sh`, `UnrealEditor-Cmd` or UBT directly from parallel work; go through
`./build.sh`, `./test.sh`, or `ue_locked`. Measured here: a cold build of the
module in a fresh worktree, ~25-40 s; the whole suite with start-up, ~15 s.

**Unity builds are off** (`bUseUnity = false` in `DeepSpace.Build.cs`). A
unity blob merges the anonymous namespaces of the files it concatenates, and
UBT's *adaptive* unity compiles git-modified files on their own -- so a dirty
tree built green and the same code committed failed, and four worktrees that
were each green broke together at the merge. Off, green in a worktree means
green merged.

**Prove a test can fail with `Tools/mutate.sh`** before trusting it. It
checks everything that has made a mutation silently prove nothing here -- the
text not found, the mutant not compiling, the library not rebuilt -- before
reading a verdict, and restores the file. Rebuild afterwards: its last build
held the mutant. `./test.sh` itself exits non-zero if no tests ran, or if
the log shows a world torn down without `EndPlay` or a console variable
looked up by name every frame -- both have been left by runs whose every
test passed. A test path that has children becomes a group node and
silently stops running, so compare the tests that ran against those defined
when a count looks off.

Run automation tests headlessly (preferred — no UI clicking, works over SSH):

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" \
    -ExecCmds="Automation RunTests DeepSpace" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -NoLiveCoding

# The verdict lands in the log, not on stdout:
grep "Test Completed" Saved/Logs/DeepSpace.log | tail
```

`-nullrhi` skips the renderer entirely, which is why this works without a
display. Use `-TestExit`, not `; Quit` — tests run asynchronously, so `Quit`
exits before the queue drains and the run reports nothing at all rather than
failing visibly. Tests can also be run from the editor via **Tools → Session Frontend →
Automation**, filter `DeepSpace`.

## The rule that matters most

**All gameplay logic lives in C++. Blueprints only assign assets and expose
tunable values.**

Blueprints are binary `.uasset` files: not diffable, not mergeable, unreadable
to Claude. Logic placed there is invisible to both git history and to any AI
collaborator, and Blueprint-heavy projects are where hobby games hit performance
walls.

**Drift signal:** if gameplay logic starts appearing in `Content/`, the plan has
been abandoned. Say so.

## Architecture

- `Source/DeepSpace/Ship/ShipPowerState.*` — pure C++ power arithmetic, no
  Unreal types beyond containers. Headlessly unit-testable. The complexity sink.
- `Source/DeepSpace/Ship/ShipSubsystem.*` — `UWorldSubsystem` wrapping the above;
  authoritative ship state. Knows nothing about meshes, rooms, or the player.
- `Source/DeepSpace/Ship/InteractableComponent.*` — a component, deliberately
  not a class hierarchy, bolted onto anything interactable.
- `Source/DeepSpace/Player/DeepSpaceCharacter.*` — first-person pawn and the
  interaction trace.
- `Source/DeepSpace/Universe/` — procgen: pure generators behind
  `UUniverseSubsystem`, the one authority on what exists (*The universe*).
- `Source/DeepSpace/Sky/` — pure projection arithmetic behind `AShipSky`,
  which polls and stores nothing (*The sky*).
- `Ship/ShipFlightState.*`, `Ship/ShipNavState.*` — pure: the flight model
  with the drive, and the jump's decisions. `UShipSubsystem` owns and steps
  both (*The drive and the jump*).
- `Ship/ShipHum*`, `Ship/ShipLightingSubsystem.*`, `Ship/ShipNavScreen.*` —
  the hum, the lights and lamps, the chart chair.
- `Ship/ShipDressing*` — the pure dressing core, its rules and ini, the
  surface and keep-out markers, and `UShipDressingSubsystem` (*The dressing*).

Consumers **ask** the subsystem for state; they never store it. That discipline
is what keeps ship state from scattering across actors.

## Version control

`.uasset`/`.umap` go through **Git LFS** — configured before the first binary
asset existed. Never hand-edit them.

`Binaries/`, `Intermediate/`, `Saved/`, `.vscode/`, `Makefile`, and
`*.code-workspace` are generated and gitignored.

## Docs

- `docs/vision.md` — what the game is for; the measure other docs answer to
- `docs/superpowers/specs/` — design docs (the why)
- `docs/superpowers/plans/` — implementation plans, updated in place as reality
  contradicts them
- `docs/decisions/` — short ADRs

## Screens, the pointer, and power

Screens aboard the ship are **surfaces in the room**, not menus: a
`UWidgetComponent` renders real Slate onto a quad and
`ADeepSpaceCharacter`'s `UWidgetInteractionComponent` drives it along the
view. Nothing pauses and nothing goes fullscreen.

**Widget trees are built in C++**, in `Source/DeepSpace/UI/`, by overriding
`UShipScreenWidget::BuildScreen`. There are no Widget Blueprints and there
should not be: a screen carries gameplay-visible logic, and ADR 0002 is why
that cannot live in a `.uasset`.

Four things about `UWidgetComponent` that cost time and are easy to hit again:

- **Its quad's normal is +X**, while every wall-mounted fixture in this ship
  faces −X at yaw 0 (`placement.resolve_mount`). `AShipScreen::ConfigurePanel`
  turns the panel round once, centrally. Mount a screen with the same yaw as
  anything else and it will be right.
- **A Static child of a movable root never has its world transform updated.**
  A screen on a static sub-component draws in the right place and leaves its
  collision body at the origin: visible, unclickable, and no warning anywhere.
- **It builds its widget and its collision body in `BeginPlay`.** Spawn a
  screen into an already-running world and it is never traceable. A test must
  spawn before `World->BeginPlay()`.
- **It cannot be hit-tested under `-nullrhi`**: the widget is created through
  `UGameInstance`, and the Slate hit-test grid is only filled once painted.
  `DeepSpace.Ship.ScreenPointer` therefore checks everything up to and
  including the surface the ray lands on, and stops there. Which control the
  ray lands on is a playtest question.

Power is allocated, not merely summed. `FShipPowerState` holds installed
module **draws** (off the top) and **consumers** that divide what is left in
proportion to a weight the player sets, capped at each one's want with the
surplus redistributed. Consumers degrade and never fail: lights dim and brown
out, boosters push down to a quarter thrust, the jump drive charges slower.
**There is deliberately no cutoff, no alarm, no timer and no failure state**,
and nothing in the model changes on its own with time. If a change here
introduces a rate the player must keep up with, it has broken the anti-chore
principle -- say so rather than tuning it.

Generated actors are addressed **by tag, never by name or index**.
`Tools/build_hauler.py` tags the lights `Power.Lights`, the same string as
`ShipPower::Lights`, and `UShipLightingSubsystem` finds them that way.
`Tools/verify_level.py` checks the tag *and* that the lights are Movable --
a Static light is baked and can never dim.

## Randomness

**Reach for the distribution that describes the thing; do not default to
uniform.** Uniform means "every value in this range is equally likely", which
is true of almost nothing in a world, and reaching for it by habit is how
generated content comes to feel generated. Exponential for gaps between events,
Poisson for counts in an interval, Weibull for wear and failure, log-normal for
magnitudes built from many factors, Beta for bounded proportions. Bound the
tails deliberately, and say in a comment what the choice believes about the
world. See ADR 0008, which also explains why the generator shapes its prior
rather than sampling and rejecting.

## Generated level geometry

`Content/Maps/L_Hauler.umap` is **generated, not hand-edited**. The source of
truth is `Tools/hauler_layout.py`: a floor plan of rooms, doors, windows and
seals, plus furniture placements. `Tools/floorplan.py` derives the walls,
`Tools/props.py` holds the furniture templates, `Tools/placement.py` resolves
everything room-relative into world space, and `Tools/build_hauler.py`
realises it in the editor. Actors the script owns are prefixed `hauler_` and
are rebuilt on every run, so hand-placed changes to them are lost. Materials
are rebuilt too: tune them in `build_hauler.py`, not in the editor.

```bash
python3 Tools/test_floorplan.py && python3 Tools/test_placement.py
python3 Tools/test_dressing_markers.py  # after a surface change: python3 Tools/dressing_markers.py
python3 Tools/validate_hauler.py        # no editor needed, ~1s
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/build_hauler.py" \
    -unattended -nopause -nosplash -NoLiveCoding
```

`validate_hauler.py` voxelises the ship at 10 cm and checks soundness — the
plan is self-consistent, the hull is sealed, everything is one connected piece
— and **intent**: every region is reachable in its posture by a capsule with
width, the crawlway can *only* be entered crouching, the corridor keeps a 12 m
clear run for sliding, and no furniture blocks a door or the console. A ship
can be perfectly playable and still fail on intent; that is the point.
Reachability is bounded to each room's floor, because roofs are standable and
connected — the milestone 1 validator could reach rooms across the roof.

`Tools/verify_level.py` then measures the *built* actors and compares them to
the layout. Keep both: the validator checks the layout is sound, the verifier
checks the level matches it. An early build placed every box half its own size
off because `SM_Cube`'s pivot is at its minimum corner rather than its centre,
and the layout validated perfectly throughout — only measuring built actors
catches that. **Meshes do not agree on pivot placement** — `SM_Cube` is corner-origin,
`SM_ChamferCube` centre-origin, `SM_Cylinder` base-centre, and the engine
`Sphere` used for stars is centre-origin — so never assume one; read the
bounding box. `build_hauler.py` scales and offsets every mesh from its
measured bounds for exactly this reason.

`unreal.log` output does not reach stdout under the commandlet; these scripts
write to `Saved/hauler_build.txt` and `Saved/verify_level.txt`.

## Flying

The helm is keyboard-only on purpose: the mouse keeps looking, so the pilot's
head turns independently of the ship and a turn reads as *the ship* turning.
W/S pitch, A/D yaw, Q/Z roll, Shift/Ctrl throttle, F the in-system drive. The
throttle is a lever, not a button — input sweeps it and it stays where it is
left, which is what makes a cruise something you set and walk away from.

`IA_Attitude`, `IA_Throttle`, `IA_Drive` and their `IMC_Default` bindings are
built by `Tools/setup_flight_input.py`, not by hand, and it assigns them on
`BP_DeepSpaceCharacter`'s defaults; re-running it replaces its own mappings
and leaves the rest of the context alone. Two traps it works around:
Python has no `InputActionFactory` (a new action is a duplicate of `IA_Look`),
and UE 5.8 keeps the real mapping list in `default_key_mappings.mappings` —
the context's own `mappings` is the older, empty one, and `map_key` writes to
*that*.

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/setup_flight_input.py" -unattended -nopause -nosplash -NoLiveCoding
```

## The universe

`UUniverseSubsystem` (`Source/DeepSpace/Universe/`) is the only authority on
what exists. Behind it, `FGalaxyGenerator` and `FStarSystemGenerator` are pure
functions of (root seed, priors, id), and the subsystem holds the root seed and
nothing else: no cache, no generator (built per query), no start system, no
current system. Every query works from `Initialize`, so another subsystem's
`OnWorldBeginPlay` can ask in any order.

**The seed lives in `Config/DefaultGame.ini`**, under
`[/Script/DeepSpace.UniverseSubsystem]`, `UniverseSeed=20260925`. A
`UPROPERTY(Config)` is a C++ field that Unreal fills from the ini when the
object is created. The C++ default is **0 on purpose**: zero is nobody's
universe, so a section that silently fails to load shows up as the wrong
universe rather than passing for the right one. `-UniverseSeed=` on the command
line overrides it (decimal or `0x` hex); a `UPROPERTY(Config)` does not read
the command line by itself, so `ResolveSeed` does. Start-up logs `Universe
seed N (0x...)`, and a bug report is that number.

**"Which system am I in" has exactly one answer:
`GetSystemAt(ship position)`**, the nearest system whose star is within
`InSystemRadiusLy` (0.25 ly), empty between stars. There is no
`GetCurrentSystem` and no `SetCurrentSystem`, and there must never be (plan
conflict 1): a current system stored anywhere is a second answer that can
disagree with where the ship is. Arrival is a translation, and the answer
changes by itself because it is asked of the position. `GetStartSystem` is the
nearest system to the origin with a planet; under honest weights that is
usually a red dwarf. Where the ship *starts* is not the universe's to decide
(conflict 3): `UShipSubsystem::OnWorldBeginPlay` places it once, through
`NavStart::OpeningPlacement` -- the largest planet dead ahead and its star to
starboard. The distance is 40,000 km *per Earth radius* of that planet
(`NavStart::OpeningDistanceCm`), so an Earth opens at 40,000 km and a Jupiter
at about 450,000 km: the shot is framed as an angle, the same ~18 degree world
for any planet, because a gas giant held at a fixed 40,000 km would open with
the ship inside it. `ds.Nav.PlaceAtStart 0` leaves the ship where it is.

**The priors are data.** Every number the generator draws with is a line in
`[/Script/DeepSpace.ProcGenPriorsConfig]` of `DefaultGame.ini`, read through
`UProcGenPriorsConfig`; what each one means is in `Universe/GenPriors.h`. The
weights are **honest** (developer's ruling): three suns in four are red dwarfs.
To tune, edit the ini and type `ds.Universe.ReloadPriors` in the running game.
No rebuild, no restart; it lists what changed, and the universe re-rolls
around a ship that does not move. A value its sampler cannot take refuses the
*whole* section, keeps the priors in use, and names the line. The guarantees
(the Hill floor, the mass cap, the kind thresholds) are deliberately not in the
ini, so no ini edit can break an invariant.

The trap it works around: **Unreal reads every ini once, at start-up, into a
config cache, and `ReloadConfig()` re-reads the cache, not the file.** It hands
back the numbers the session began with, with no warning.
`UProcGenPriorsConfig::ReloadFromIni` force-reloads the file into the cache
first. Any other reload-from-ini command needs the same.

Console, all in `UniverseSubsystem.cpp`:

- `ds.Universe.Describe` -- the system the ship is in, or the start system if
  it is between stars. `ds.Universe.Describe <seed>` describes another
  universe's home, and `ds.Universe.Describe <seed> <x> <y> <z> <slot>` any
  system of it, under this session's priors.
- `ds.Universe.Near [ly]` -- systems within range of the ship, nearest first
  (default 12).
- `ds.Universe.ReloadPriors` -- as above.

**The corpus** is for reading, not correctness. `./test.sh
DeepSpace.Universe.Corpus` writes `Saved/procgen_corpus.tsv` (the 10,000
systems nearest home, a row per planet) and `Saved/procgen_describe.txt` (home,
its twelve neighbours, and the homes of seeds 1-12). Then `python3
Tools/procgen_corpus.py` reads the TSV. The columns are
`Tools/procgen_corpus_contract.json`'s, and the test fails if the file stops
fitting it. The question is whether systems read as *places* or as *rolls*;
the invariants are `DeepSpace.Universe.SystemGeneration`'s. The known-value
tests (`DeepSpace.Universe.Seed`, `.Stream`) pin the hash and the stream that
every universe rests on; never weaken them to make a change pass.
`Tools/rng.py` survives only as a cross-check on `FGenStream`, and is the basis
of nothing.

## The sky

`AShipSky` (`Source/DeepSpace/Sky/`, placed by `build_hauler.py` as
`hauler_sky`) draws everything outside the glass that is *somewhere*: the local
star, its planets and moons, the neighbouring stars, the one sun that lights
the deck, and the exposure.

**Distant bodies are projected, never placed.** Unreal's world, even with
large world coordinates, ends 44 million km from the origin
(`UE_LARGE_HALF_WORLD_MAX`), short of 1 AU, and the GPU still draws in 32-bit
floats relative to the camera. The real distances cannot be geometry. So each
body is drawn as a *homothety*, a uniform scaling centred on the ship: the
true sphere, scaled toward the ship until it lands 50 km to 125,000 km out
(`SkyProjection.h`). Direction, angular size and phase survive the scaling
exactly; only the distance changes, and distance is the one thing the eye
cannot see. The 50 km near edge is set by parallax: the eye can be 17.6 m from
the ship's origin, which at 50 km is under a 4K pixel. Points at infinity sit
on a dome at 250,000 km (`AShipSky::DomeRadius`), behind every body. A
body is a point until it resolves, and the resolve is a blend in the material,
so there is no moment of change.

**The sky polls and stores nothing** (conflict 2). `AShipSky` has no
`SetSystem` and no `SetInTransit`: nothing calls into it, and nothing needs to
find it. Every frame it asks `LocalSystem` (`Current`, `Serial`,
`InTransit`), which asks `UUniverseSubsystem` and `UShipSubsystem`. What it
keeps is a cache of its own drawing -- the proxies, keyed by the jump serial
and by the system's name and star position -- which it rebuilds whenever the
key stops matching. Its one write to the flight state is `ds.Sky.Goto`, a
one-shot placement for tuning. The counter-frame follows the same rule for
the course marker and the streaks.

**The material contract.** Every parameter name C++ drives lives in
`Sky/SkyMaterialContract.h`, mirrored by `Tools/sky_material_contract.json`,
which `Tools/setup_sky_materials.py` reads to author `M_SkyBody`, `M_SkyStar`,
`M_SkyStarfield`, `M_SkyGlass` and `MPC_Sky`. **`SetScalarParameterValue` on
a misspelt name fails silently**: the symptom is a planet that never resolves
and no error anywhere. `DeepSpace.Sky.MaterialContract` holds the header, the
JSON and the loaded assets to exactly the same names. Add a parameter on all
three sides, then run the authoring commandlet (editor closed, through the
lock):

```bash
. Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" \
    -unattended -nopause -nosplash -NoLiveCoding      # report: Saved/setup_sky_materials.txt
```

The materials are unlit, and the sun lights only the ship. The glass casts no
shadow, and its `M_SkyGlass` reflects the lit room through `MPC_Sky`'s
`InteriorLight` and `Veil`: lights off at the console and the stars come out.
`AShipSky`'s `Sun` is the only `DirectionalLight`, and `verify_level.py` fails
on any other, and on any `SkyLight`, `SkyAtmosphere`, cloud or fog. When a
planet crosses the sun, the deck darkens by the fraction covered
(`SunVisibleFraction`; the eclipse).

**Exposure is fixed, and the sky owns it.** Auto exposure is Unreal's
simulated eye adaptation: it rescales the picture toward a middle grey, which
would make a lit galley and a dark cockpit look alike and wash out the
difference between a star and a world. `AShipSky`'s unbound
`UPostProcessComponent` overrides it, at priority 10 so it beats any volume in
the level. `ds.Sky.ExposureMode`:

- `0` -- the engine's own auto exposure, untouched: for *reading* the scene.
- `1` -- manual at `ds.Sky.Exposure` EV100 (the design, and the default).
- `2` -- auto exposure held within `ds.Sky.ExposureRange` stops of
  `ds.Sky.Exposure`: the fallback if manual feels dead.

**`ds.Sky.Exposure`'s default, 0.7, is an estimate.** Nobody has read it off a
rendered galley yet. To read it, stand in the galley in play, type
`ds.Sky.ExposureMode 0`, then `ShowFlag.VisualizeHDR 1` (in an editor viewport:
Show -> Visualize -> HDR (Eye Adaptation)). Take the average scene EV100 it
reports, set `ds.Sky.Exposure` to it, and go back to `ds.Sky.ExposureMode 1`. A
scene at exactly that EV looks the same in modes 1 and 2
(`ShipSky::ManualExposureBias`, checked in `DeepSpace.Sky.ShipSky`). Read it
after the room moods change, never before, or the exposure is calibrated
against a galley that no longer exists.

## The drive and the jump

**Two levers, and two words that never swap** (conflict 7):

- **The drive** is in-system: **F** at the helm (`IA_Drive`), which calls
  `UShipSubsystem::SetDriveEngaged(Commander, bool)`, gated on the pilot like
  `SetFlightCommand`. Engaged, the ship flies along the nose as the throttle
  asks, but may *close* on the nearest surface no faster than the room left
  over `ds.Drive.Tau` (15 s, stretched by thin boosters): at full throttle, a
  tenth of the remaining room every 1.5 s. So an approach is exponential, a
  planet grows from a point to a disc with no moment of change, and the ship
  settles `ds.Drive.Floor` (100 km) up. Leaving is unlimited, and
  disengaging clamps the speed back to cruise. It is a lever: it stays engaged
  when the pilot stands up, and survives a jump.
- **The jump** folds between stars: `SetJumpEngaged`, `IsJumpEngaged`,
  `EJumpState {Idle, Winding, Ready, Transit}`, `NavText::Jump`, and the HUD's
  `JUMP WINDING` / `JUMP READY` / `BETWEEN STARS`. The decisions are the pure
  `FShipNavState`; `UShipSubsystem` acts on them. Its tunables are
  `ds.Nav.*`.

**The system's edge is a surface** (conflict 10).
`LocalSystem::NearestSurfaceDistance` counts the distance to the edge
(`InSystemRadiusLy`) as well as to every body. So the drive slows into the
edge as it does into a planet, and never flies the ship out of its system, where
`GetSystemAt` would go empty under a sky still drawing the old one. You leave a
system by jumping. In transit the drive's room is 0, so it gives only cruise
speed.

**The jump has three levers, each left where it is set**: the course (the
chart, or `ds.Nav.Plot`), the heading (the helm; the HUD's bearing words, and
the nose caret on the teal course marker), and engage (the chart, or
`ds.Nav.Engage`). Engaged, the engine asks for `ds.Nav.WindingWant` (800 W) and
the charge winds at a rate the watts it actually gets scale. Starved, it still
winds at `ds.Nav.StarvedRate` of full; it never stops. It asks for nothing
otherwise, so staying put is never taxed. Once plotted, engaged, charged, and
within `ds.Nav.ConeDeg` of the nose, **the fold opens by itself.** There is no
confirm, and there must not be one. A final button would make the player come
back on the jump's schedule to service it, and a game that makes you do that
is telling you that you are behind: the anti-chore principle's definition of a
chore (developer's ruling). If it feels like the game acting without you, the
answer is a softer cue before the fold -- the hum already rises as the jump
winds -- never a confirm. For the same reason no screen shows a percentage, a
bar or a countdown: a number that fills is a clock to watch.

The fold lasts `ds.Nav.TransitSeconds`, with streaks past the window, and the
helm does nothing between stars. Arrival is `FShipFlightState::JumpTo`, the
flight state's fourth write path (ADR 0005, amended): a translation and nothing
else, onto the line from the departure point to the star, at
`max(ds.Nav.StandoffAU x sqrt(L), 1.5 x the outermost orbit)` (conflict 9).
Orientation is untouched, so the new sun is where the nose was and the distant
stars do not move. The course is the only universe data the ship keeps, as an
id (ADR 0003, amended).

## The hum and the lamps

**The hum** is synthesised, not sampled. `FShipHumVoice` is the pure
synthesis. `UShipHumComponent` is a `USynthComponent`, Unreal's component for
audio generated in code on the audio thread. Every tick it asks the ship for
two numbers and posts them across an atomic mailbox, keeping no copy.
`AShipHumSource` is one point the hum comes from: one reactor, and one air
handler per room, from `HUM_SOURCES` in `hauler_layout.py`. The reactor's
drone follows **`EngineFeed = clamp(GetConsumerShare(Engine) /
ds.Nav.WindingWant, 0, 1)`**: watts delivered, never satisfaction (conflict
8). An idle engine wants 0 W, and a zero want reads as fully satisfied, so a
hum on satisfaction would sit at full whenever the ship is idle. On watts, it
idles low, rises and brightens as the jump winds, and settles when charged,
which makes it the jump's wind-up cue. The hiss follows the boosters
(`ds.Hum.CruiseHiss`). `ds.Hum.Volume` is the first knob if it wears. Each air
source seeds its noise from where it stands: two at one point would hiss the
same noise and comb into a whistle, and `test_placement.py` forbids it.
Headless, the mixer is real, so `DeepSpace.Ship.HumComponent` proves samples
are pulled; whether it *sounds* right is a playtest question.

**The lamps.** Room moods (`ROOM_MOOD`: a colour temperature and a scale per
room) and practical lamps (`PRACTICALS`) are layout data in
`hauler_layout.py`, built into the level. The glowing panels of every
`lamp_<room>` role are tagged **`Power.Lamps`**, and `UShipLightingSubsystem`
drives their `M_ShipEmissive` `Colour` alongside the `Power.Lights` lights, so
a starved room's panels dim and brown out with its lights rather than shining
on. A panel's rated glow is read from the material the level gave it, never
from the dynamic instance, which holds whatever was last written.

## The chart chair

`AShipNavScreen`, an `AShipScreen` over the starboard desk screen in the
cockpit, with `UNavigationWidget` built in C++. E sits you down at it; it shows
where you are, the six nearest systems, the jump as a word and the course as a
bearing. Clicking a row plots it (again clears it), and one toggle engages or
stands down. That is all it does: aiming is the helm's. It keeps nothing it
could ask for, so a course plotted from the console shows here untold. The
jump's word is asked every frame; where the ship is, the rows and the course
cost a sector scan or a generated system, so they are asked again only when
something they depend on has changed (`UNavigationWidget::FAskedAt`: the jump
serial, transit, the plotted system, the ship's position past
`MovedFarEnoughCm`, its heading, `ds.Nav.RangeLy`, the priors). The chart is
in view from the helm and would otherwise pay for that every frame. The chair
beside the helm is not a second station (vision: shared presence, never
division of labour).

It is placed by `build_hauler.py` (`place_nav_screen`, `hauler_nav_screen`)
from `NAV_SCREEN` in `hauler_layout.py`. **Its three seat tunables,
`UseDistanceCm`, `SeatHeightCm` and `ViewDistanceCm`, are per-instance
`UPROPERTY`s that `place_nav_screen` sets**, so a nudge is an edit there and a
level rebuild, not C++. Its `Reach` box sits *behind* the panel's face. A volume
enclosing the panel blocks the channel the pointer traces on, and the screen
draws perfectly and cannot be clicked. `DeepSpace.Ship.NavScreen` and
`DeepSpace.UI.NavigationScreen` spawn it before `World->BeginPlay()`, as every
screen test must.

## The dressing

Somebody's things on the ship's surfaces -- mugs on the counter, books on the
bunk desk, crates on the rack -- and the wear on its furniture. It is
generated in **C++ at world start**, never baked by Python (ADR 0006,
amended: the exception was refused), and the lived-in spec's decisions 1-1d
are its contract.

- **The layout exports where things may rest.** `build_hauler.py`'s
  `place_surfaces` spawns one `AShipDressingSurface` per surface
  `resolve_surfaces` finds (`ROOM_DRESSING` in `hauler_layout.py` says which
  kinds each room takes), tagged **`Dress.Surface`**; `place_keep_outs` spawns
  an `AShipDressingKeepOut` for every door's and the console's keep-clear
  zone, the slide run and the crawlway, tagged **`Dress.KeepOut`**. Every
  furniture part is tagged **`Dress.Wear`** and **`Piece.<prop>_<n>`**, so a
  whole desk wears together. The markers carry data and no logic (ADR 0002),
  and `verify_level.py` holds each one to the layout. The tag strings live in
  `ShipDressingTags` (`Ship/ShipDressingTypes.h`) and `placement.py`, and
  `test_placement.py` reads the C++ to hold them equal.
- **The pure core plans.** `ShipDressing::Dress` (`Ship/ShipDressing.h`) is a
  function of surfaces, keep-outs, a seed and `FShipDressingRules`
  (`Ship/ShipDressingRules.h`): Poisson for how many, Beta for where along
  and how far back, a categorical for what and in which colour, a geometric
  pile. `DressGuarantees` is what no rule may move -- containment, the clear,
  the excludes, the keep-outs, nothing below `MinRestHeightCm` -- so no tune
  can put a mug through a shelf or onto the floor.
- **`UShipDressingSubsystem` draws it.** In `OnWorldBeginPlay` it gathers the
  tagged markers, asks the core for a plan under
  `ShipDressing::DressSeed(root)` -- the universe's root seed, so two players
  aboard one universe see one set of mugs -- and spawns it as
  `UInstancedStaticMeshComponent`s on one transient actor tagged
  **`Dress.Clutter`**: Movable, query-only and solid to `ECC_Camera` alone
  (the crouch leans the eye ~30 cm past the capsule, into what stands on a
  workbench or a rack, and the eye sweep is what keeps the view out of it;
  `Visibility` and `Pawn` pass through), each instance scaled and offset
  from its mesh's measured bounds (the pivot trap, again). It swaps worn
  pieces to `MI_Ship_furniture_faded` / `_replaced`. The clutter's
  `MI_Ship_<role>` materials are authored by `build_hauler.py` (`PLAIN`), so a
  missing one means the level build has not run. **It never ticks**: nothing
  accumulates, nothing is ever to tidy (the anti-chore principle, kept by
  structure). The editor world never begins play, so the viewport shows bare
  surfaces; press Play to see it.
- **The rules are data.** `[/Script/DeepSpace.ShipDressingConfig]` in
  `DefaultGame.ini`, read through `UShipDressingConfig`, lays ini lines over
  the code's defaults: scalars by name, and `+Kinds`, `+Templates` and
  `+Colours` rows that replace the entry of the same name (a `Kinds` row with
  no `Mix` keeps the code's mix). A read outside `DressRuleDomain` is refused
  whole and names the line. `ds.Dress.Reload` re-reads the file from disk
  through `GameIniReload::RereadFromDisk` -- the same path as
  `ds.Universe.ReloadPriors`, for the same config-cache trap -- and redresses
  every running world.

Console:

- `ds.Dress.LivedIn` -- scales every surface's mean count: 0.3 freshly moved
  in, 1 lived in, 2 squalid, 0 bare. Setting it redresses in place.
- `ds.Dress.Seed <root>` -- dress as that root's universe would; -1 for the
  world's own. Setting it redresses. `ds.Dress.Redress [root]` does the same
  as a command.
- `ds.Dress.Reload` -- as above. `ds.Dress.Describe` -- every surface and
  what is on it, one line each.

The tests: `DeepSpace.Ship.Dressing.*` hold the core to its rules on
`Tests/DressingTestFixtures.h` (hand-typed, allowed to lag) and the subsystem
to its plan with probe markers. `DeepSpace.Dressing.Hauler.*` dress the
hauler's *real* surfaces, spawned from `Tools/dressing_markers.json`, which
`python3 Tools/dressing_markers.py` writes from `generate()` and
`test_dressing_markers.py` holds equal to it: **after any change to the
layout's surfaces, rerun the script**, or that test fails. `DeepSpace.Loop.Jump`
flies the whole loop with the ship dressed. Whether it reads as somebody's
ship is a playtest question: walk it at `ds.Dress.LivedIn` 0.3, 1 and 2, and
at `ds.Dress.Seed` 1, 2 and 3.

## Playtest console

The backtick key opens Unreal's console in play (in the editor, the
Output Log's `Cmd` box takes the same input). Console variables (CVars) set
there last for the session only. The whole loop, with no chair and no
walking:

```text
ds.Universe.Describe        where am I: the system the ship is in
ds.Sky.Goto 1 40000         40,000 km over body 1, facing it (0 is the star; index or name)
ds.Nav.Near                 the chart, numbered, nearest first
ds.Nav.Plot 0               plot row 0
ds.Nav.ChargeSeconds 5      wind from cold in 5 s, not 90, for every jump after
ds.Nav.Engage               engage (ds.Nav.Engage 0 stands down); aim, and it fires by itself
```

`ds.Nav.Charge` fills the charge on the next tick, once. `ds.Nav.Clear` drops
the course. `ds.HUD 0` hides the HUD for an unadorned look. `ds.Dress.LivedIn`
and `ds.Dress.Seed` redress the ship where you stand (*The dressing*).

## Where each tunable lives

No value below has been settled by a playtest yet. Each is a
`TAutoConsoleVariable` read at use and never cached, so a playtest moves it
with no rebuild. **Write a settled value back as the default in the file
named.** Where a default comes from a header constant, the constant is the
default: change it there, and expect `./rebuild.sh --force` and the pure
tests that assert it.

| CVar | Default | Lives in |
|---|---|---|
| `ds.Nav.ChargeSeconds` | 90 s | `ShipSubsystem.cpp`, from `FShipFlightState::JumpChargeSeconds` (`ShipFlightState.h`) |
| `ds.Nav.WindingWant` | 800 W | `ShipSubsystem.cpp` |
| `ds.Nav.StarvedRate` | 0.2 | `ShipSubsystem.cpp` |
| `ds.Nav.FoldDraw` | 0 W | `ShipSubsystem.cpp` |
| `ds.Nav.TransitSeconds` | 6 s | `ShipSubsystem.cpp`, from `FNavTuning` (`ShipNavState.h`) |
| `ds.Nav.ConeDeg` | 8 deg | `ShipSubsystem.cpp` |
| `ds.Nav.StandoffAU` | 2.4 AU | `ShipSubsystem.cpp`, from `NavStart::DefaultStandoffAU` (`NavStart.h`) |
| `ds.Nav.RangeLy` | 12 ly | `ShipSubsystem.cpp` |
| `ds.Nav.PlaceAtStart` | 1 | `ShipSubsystem.cpp` |
| `ds.Drive.Tau` | 15 s | `ShipSubsystem.cpp`, from `FShipFlightLimits::DriveTau` (`ShipFlightState.h`) |
| `ds.Drive.Floor` | 100 km | `ShipSubsystem.cpp`, from `FShipFlightLimits::DriveFloor` (`ShipFlightState.h`) |
| `ds.Nav.MarkerPixels`, `.StreakLength`, `.StreakSweep` | 6 px, 40, 5 | `ShipCounterFrame.cpp` |
| `ds.Sky.MoteFadeSpeed` | 2000 m/s | `ShipCounterFrame.cpp` |
| `ds.Sky.Exposure`, `.ExposureMode`, `.ExposureRange` | 0.7 (estimate), 1, 1.5 | `ShipSky.cpp` |
| `ds.Sky.Radiance`, `.SunLux` | 3.0, 9.4 lux | `ShipSky.cpp` -- keep SunLux at pi x Radiance |
| `ds.Sky.FluxGamma`, `.PointPixels`, `.StarSurface` | 0.5, 2 px, 1000 | `ShipSky.cpp` |
| `ds.Sky.StarfieldFaint`, `.Mottle`, `.Veil`, `.Bloom` | 0.01, 0.15, 1.0, 0.675 | `ShipSky.cpp` |
| `ds.Hum.Volume`, `ds.Hum.CruiseHiss` | 1.0, 0.35 | `ShipHumComponent.cpp` |
| `ds.HUD` | 1 | `ShipHUDWidget.cpp` |
| `ds.Dress.LivedIn`, `ds.Dress.Seed` | 1, -1 (the world's own) | `ShipDressingSubsystem.cpp` |

Tunables that are not CVars: the universe's seed and priors, and the
dressing's rules (`Config/DefaultGame.ini`, above, reloaded with
`ds.Universe.ReloadPriors` and `ds.Dress.Reload`; write a settled dressing
number back into `ShipDressingRules.cpp`); room moods and practicals
(`hauler_layout.py`, a level rebuild); the chart's seat (`place_nav_screen`, a
level rebuild); the reactor rating and each consumer's want
(`UShipSubsystem`'s `static constexpr`s, a header change).

## The player's body

The body's animations come from Mixamo FBX in `SourceArt/Mixamo/`, retargeted
onto the mannequin by script. Run in this order after adding or changing a
clip, with the editor closed:

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/import_animations.py" -unattended -nopause -nosplash -NoLiveCoding
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/setup_character.py" -unattended -nopause -nosplash -NoLiveCoding
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/check_anim_heights.py" -unattended -nopause -nosplash -NoLiveCoding
python3 Tools/check_anim_blueprints.py
```

- **Blend spaces built from Python blend nothing** unless
  `unreal.DeepSpaceEditorScripting.rebuild_blend_space` is called: setting
  their samples never builds the interpolation table. `import_animations.py`
  does this; any new script that edits a blend space must too.
- **The movement contract lives in `Tools/movement_contract.json`**, read by
  the layout and by the `DeepSpace.Player.MovementContract` test. Change it
  there; each side fails its own test if the other no longer fits.
- `ABP_DeepSpaceBody` is hand-wired and may only wire (ADR 0002); the import
  script updates its blend spaces in place, never deleting them, so its
  references survive a re-import.

### The camera is not attached to the head

It looks like it is, but `ADeepSpaceCharacter::PlaceCamera` positions it every
frame instead, because **the animation does not know about the ship**. Attached
to the `head` bone it goes wherever the clip puts it: the retargeted crouch
idle carries the head **57 cm** from the capsule's axis against a **34 cm**
capsule, so crouching against a wall put the view outside the hull.

`PlaceCamera` follows the head exactly sideways, damps only the vertical (bob
is vertical; a horizontal lag lets the body lean into frame), applies the
forward eye offset in the view's **yaw only** (swung with pitch it dives into
the neck when you look down), and sweeps a sphere out from the capsule's axis
so the eyes stop at a wall with more than the 10 cm near plane to spare.

Two guards, and they check different things:

- `check_anim_heights.py` checks every clip's peak head height against its
  posture's capsule, so a taller clip fails there rather than in play.
- `DeepSpace.Player.CameraStaysInsideWalls`, `.CameraStaysOutOfClutter` and
  `.CameraDoesNotDiveWhenLookingDown` check the placement itself. The sweep is
  on `ECC_Camera`, so anything the eye can lean into must block that channel:
  the dressing's clutter does, and blocks nothing else.

Sideways reach is deliberately *not* guarded per clip: heads do leave the
capsule, and the sweep is what handles it.

### Changing a component's attachment does not move actors already placed

`SetupAttachment` runs in the constructor, so it shapes *new* instances. An
actor already saved into `L_Hauler` keeps the hierarchy and the component
transforms it was serialised with. Re-parenting the laptop's screen off its
scaled lid fixed the class and changed nothing in the level: the placed
laptop still had its screen under the lid, still inheriting a non-uniform
scale of `0.054 x 0.015`, and still unclickable. The C++ was right and the
ship was wrong for another hour.

**After any change to a generated actor's components, rebuild the level**
(`Tools/build_hauler.py`) — it respawns everything prefixed `hauler_`, which
is what actually applies the new constructor. Then `Tools/verify_level.py`.

### A Blueprint can be broken while everything else passes

C++ builds, tests pass, the level validates — and the editor still refuses to
play, because a Blueprint holds dangling references to something C++ removed.
`BP_ShipConsole` shipped exactly like that: its graph drove a
`TextRenderComponent` that a cleanup script deleted, and the four compile
errors surfaced only when a human pressed Play. Worse, a Blueprint reports
the status it was *saved* with, so reading `status` without compiling first
says everything is fine.

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/check_blueprints.py" -unattended -nopause -nosplash -NoLiveCoding
```

`check_blueprints.py` compiles each one and then asks, and exits non-zero if
any fail. **Run it after removing or renaming any C++ component, `UPROPERTY`
or `BlueprintImplementableEvent`.** It scopes to the content this project
authored: Epic's template variants ship 24 Blueprints that have never
compiled against this C++, and a permanently red guard is the same as none.

### Changing the character's C++ components invalidates its Blueprint

`BP_DeepSpaceCharacter` stores a template for every native component it
inherits, holding the values from when it was last saved. Add, remove or
rename a component in C++ and the asset is stale — and the symptom can hide.
Removing `CameraArm` left the saved camera template carrying the old
`bUsePawnControlRotation = false`, so the *first* editor session after the
change could not look around; the next one, reinstanced, was fine.

After any such change, recompile and save the Blueprint, then check:

```bash
strings -a Content/Blueprints/BP_DeepSpaceCharacter.uasset | grep -i CameraArm
```

See ADR 0002's second amendment.
