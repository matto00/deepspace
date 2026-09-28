# CLAUDE.md — DeepSpace

Guidance for Claude Code when working in this repository.

## What this is

A procedurally generated space exploration game built in Unreal Engine 5.8,
emphasizing the scale of space: cruise, choose a destination, hyperjump, arrive.
The player lives aboard a ship they walk around inside and progressively upgrade.

Milestone 1 (walk the ship) is done, and so is the playable POC -- cruise,
choose, jump, arrive -- in `docs/superpowers/plans/2026-09-25-poc-build-plan.md`,
whose *Conflicts, resolved* table is binding on names and ownership. POC 2,
built after the second playtest, is the flight feel and the system map
(`docs/superpowers/specs/2026-09-26-flight-feel-design.md` and
`2026-09-26-system-map-design.md`, as amended by the developer's rulings at
the top of each, built in the order of
`docs/superpowers/plans/2026-09-26-poc2-build-order.md`).
`DeepSpace.Playtest.*` flies what the next playtest will try.

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
Tools/eyes.sh Eyes.WorldReliefParity   # a rendered check (Eyes.*): outside ./test.sh, through the lock, never -nullrhi

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

**Prove a test can fail with `Tools/mutate.sh`** before trusting it. A rendered check is proven the same way with
`MUTATE_RUNNER=Tools/eyes.sh`. It
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
- `Source/DeepSpace/Surface/` — the ground: `FWorldRelief`, pure, the one
  height function, from `Shaders/Private/WorldRelief.ush`, which `M_SkyBody`
  compiles too (the engine maps `/Project` to `Shaders/` by itself);
  `WorldReliefParams.h` is the plain data `FSkyBody::Relief` carries.
  Landing slice b adds `IGroundField` (`GroundField.*`, WorldRelief behind
  the flight's interface), the pure quadtree and tile builder
  (`TerrainQuadtree.*`, `TerrainTile.*`), and `AWorldGround`, which streams
  the nearest solid world's tiles off the game thread into pooled meshes on
  the counter-frame (*The ground*).
- `Source/DeepSpace/Sky/` — pure projection arithmetic behind `AShipSky`,
  which polls and stores nothing (*The sky*).
- `Ship/ShipFlightState.*`, `Ship/ShipNavState.*` — pure: the flight model
  with the drive, and the jump's decisions, including the target.
  `Ship/ShipDriveLever.*` (the notches, the ease, cruise's sweep) and
  `Ship/ShipFlightSurface.*` (the soft cap and the live ETA's law) are the
  pure arithmetic under them. `UShipSubsystem` owns and steps both (*The
  drive and the jump*).
- `Ship/ShipMapScreen.*`, `UI/SystemMap*`, `UI/TargetMarker.*`,
  `UI/ShipTargetOverlay.*` — the system map, the target's words and marks
  (*The system map and the target*).
- `Ship/ShipHum*`, `Ship/ShipLightingSubsystem.*`, `Ship/ShipNavScreen.*` —
  the hum, the lights and lamps, the chart chair.
- `Ship/ShipDressing*` — the pure dressing core, its rules and ini, the
  surface and keep-out markers, and `UShipDressingSubsystem` (*The dressing*).
- `Ship/ShipParts.*`, `Ship/ShipPartCatalogue.h` -- the pure parts core: the
  bays, the ratings and the stock ship as numbers, the catalogue's rules, and
  the plain loadout state. `UShipSubsystem` fits parts and derives the rated
  values from them (*Parts and bays*).

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
and nothing in the model changes on its own with time -- with one sanctioned,
bounded exception, the **starved sink** (landing decision 5): under a solid
world's drive floor, airborne, the boosters want to hold the ship against
gravity (`ds.Boosters.HoldWatts`, 150 W a g to 3 g, paid first inside their
share, `ShipPower::SplitBoosters`), and a hold short of watts becomes a sink
of at most `ds.Boosters.StarvedSink` (2 m/s), never while the vertical lever
asks a climb, ending always at rest on the ground at no cost. That want
exists only there, so **staying put is never taxed** anywhere a ship can be
parked: at any floor, between worlds, or (slice c) landed. If a change here
introduces a rate the player must keep up with, it has broken the anti-chore
principle -- say so rather than tuning it.

The reactor's output and every want are the fitted parts' ratings
(*Parts and bays*), and draws are booked by bay. **The engineering console
shows one nameplate per fitted part and nothing else**: no total drawn, no
headroom, no percentage, no tier, no comparison. The lived-in spec's
decision 11 made the reactor's readout a nameplate, and a total beside it is
a utilisation meter. The laptop's per-consumer watts are each consumer's own
share of its own want, never a total.

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

| Key | Does | Action |
|---|---|---|
| W / S | nose down / up | `IA_Attitude` |
| A / D | yaw to port / starboard | `IA_Attitude` |
| Q / Z | roll left / right | `IA_Attitude` |
| Shift / Ctrl | the live lever up / down | `IA_LeverUp`, `IA_LeverDown` |
| F | which lever is live: the drive's or cruise's | `IA_Drive` |
| X | all stop: both levers to STOP | `IA_Stop` |
| Space / C | the vertical lever: climb / sink, HOVER at zero (near a world) | `IA_VerticalUp`, `IA_VerticalDown` |
| Tab | the next world as the target, only on the zoomed map | `IA_CycleTarget` |
| E | sit, stand, and at the chart chair zoom the chart or the map | `IA_Interact` |

**`FShipFlightCommand::AttitudeRate` is a rotation vector about the body
axes: X roll, Y pitch, Z yaw** -- +Y puts the nose down, +Z swings it to
starboard, -X rolls right. The comments once said "X pitch, Y yaw, Z roll",
the key mapping followed them, and through two playtests W rolled the ship,
D pitched it and Z yawed it. `DeepSpace.Playtest.KeysTurnTheShip` now reads
`IMC_Default` and flies each key. `MaxAngularRate`'s 0.3 rad/s, meant for
roll, has always been yaw's; it stays there, pending a playtest.

**Two levers, both the ship's** (flight-feel decision 1), in
`FShipFlightCommand`: cruise's `Throttle` (-1..1) and the drive's
`DriveNotch`. The pawn keeps only what the keys did this frame -- held
attitude, whether a lever key is held, how many times each was pressed --
and hands it over with `UShipSubsystem::SetHelmInput`, gated on the pilot;
the ship moves whichever lever is live in its own tick. Presses are counted
(`Started`), never read from a level, so a tap released inside one frame is
still a tap. **Each lever keeps its setting across F** (set the drive to
0.1 c, drop to cruise to look round, F, and it is 0.1 c again); the HUD shows both,
the live one in ink. **X stops both**, and after it the lever starts again
from STOP: the next speed after a stop is a new choice. The lever is at
STOP, but a tap still counts from the ship (below), so while the ship is
still slowing after X one Shift catches it at the notch above where it is;
only once at rest is one Shift 20 km/s. That is the developer's ruling
(2026-09-26): after X, a tap catches the ship where it is. A key still held
through a stop, or from before sitting down (Shift is sprint too), moves
nothing until it is let go. In transit the helm is inert.

**The speed bands** (developer's ruling, 2026-09-27, after the third
playtest): cruise from rest to **20 km/s**, the drive from **20 km/s to
0.1 c** -- "this will balance the pace around and between planets". The
drive's bottom notch is cruise's top, so leaving the drive hands the ship
to cruise at a speed cruise can hold.

- **Cruise** is swept while a key is held (`ds.Cruise.Sweep`, 0.2 of the
  lever a second: five seconds from rest to full), stays where it is left,
  and has a **detent at zero**: sweeping down stops at rest, and going
  astern is a second, fresh press of Ctrl. **The lever reads on a log
  scale** (`ShipDriveLever::CruiseSpeed`): a position p ahead asks for 1 m/s
  x 20,000^p -- 1 m/s just off the detent, 141 m/s at half, 20 km/s at
  full -- so every tenth of the lever is the same x2.7, fine at a metre a
  second and coarse at kilometres. p = 0 is rest, exactly. **Astern is the
  same law mirrored, position for position, and the lever's astern travel
  ends where it reaches 200 m/s** (`FShipFlightLimits::AsternSpeed`,
  `CruiseAsternLimit`: 0.535 of the travel), so no part of the lever does
  nothing. It keeps its inertia, at **2 km/s^2**
  (`FShipFlightLimits::LinearAcceleration`): rest to 20 km/s is 10 s with
  the lever thrown and 13 s with Shift held, and it stops from 20 km/s in
  125 km on the braking curve. The setpoint controller closes any error
  smaller than a substep's 16.7 m/s in one substep, exactly, so a 1-10 m/s
  setting is reached without overshoot or hunting
  (`DeepSpace.Ship.FlightCruiseSettles`).
- **The drive** is STOP and eleven notches on a 1-2-5 series, 20, 50, 100
  ... 20,000 km/s and then 0.1 c: **0.1 c, and anything faster is a jump**
  (the 2026-09-27 ruling, replacing ruling 1's 1 c). A tap is one notch
  **counted from what the ship is doing, not from where the lever was**
  (`ShipDriveLever::TapDown`/`TapUp`): Ctrl always slows the ship and Shift
  always speeds it, from the first tap, even under the soft cap or while
  spooling. A hold repeats after 0.3 s at `ds.Drive.Sweep`, so STOP to 0.1 c
  is 3.7 seconds held. No reverse. The ship follows the lever eased in notch
  space (`ShipDriveLever::Ease`: 0.4 s, at most `ds.Drive.Response` notches a
  second), never overshoots, and never jumps upward; thin boosters slow the
  whole ease, never the top. It **arrives**: below
  `ShipDriveLever::ArriveNotches` (5e-3 of a notch, 100 m/s off STOP) it
  closes at the exponential's pace there, held steady, so a tap is on its
  notch, exactly, 2.5 s after it, and the reading then says its lever's
  label (on the way it is a moving reading, so the climb to the top reads in
  KM/S beside `DRIVE 0.1 C` and turns to `0.1 C` as it arrives). X from 0.1
  c passes cruise's top in 3.3 s and is at rest in 5.9 s, from 20 km/s in
  2.5 s; leaving the drive at 0.1 c spools down to cruise's top in the same
  3.3 s and lands on it, so a cruise lever at full sees no dip.

The actions and their `IMC_Default` bindings are built by
`Tools/setup_flight_input.py`, not by hand, and it assigns them on
`BP_DeepSpaceCharacter`'s defaults; re-running it replaces its own mappings,
fails rather than bind a key something else uses, and leaves the rest of the
context alone. `IA_Point` (the pointer's click) is
`Tools/setup_pointer_input.py`'s. Two traps it works around: Python has no
`InputActionFactory` (a new action is a duplicate of `IA_Look`), and UE 5.8
keeps the real mapping list in `default_key_mappings.mappings` -- the
context's own `mappings` is the older, empty one, and `map_key` writes to
*that*.

```bash
. Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
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
ini, so no ini edit can break an invariant. A world's relief is one of the
priors' draws (landing decision 3): `FPlanet::ReliefKm` is a Beta share
(`ReliefBeta*`) of a 1/g ceiling (`ReliefStrength*Km`, `ReliefTerrestrialFactor`),
and two caps are what no line can raise: `GenGuarantees::MaxReliefKm` (10 km)
and `MaxReliefRadiusFraction` (0.5% of the radius). No generated world is
small enough for the second to bind -- `RockyMassMin`'s world is ~2,750 km,
a 13.8 km cap -- so it guards hand-made worlds and moons to come, and
`DeepSpace.Universe.Relief` pins it on a made one.

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

**Below 50 km over a solid world the ground draws the body** (landing
decision 7). At 50 km the projection's magnification is exactly 1 -- the
proxy is the true sphere at its true place -- so the ground (`AWorldGround`,
*The ground*) takes the body there once its coarse cut is resident, gives it
back over 55 km, and always has it under the drive floor, where the proxy is
never drawn. The relief grows in: every tile's heights are scaled by one
morph fraction, 0 at 50 km to 1 at the drive floor, while the flight always
has the whole relief. The sky still projects the hidden proxy with its
rendered floor, so the depth stack is unchanged, and it copies that body's
look into the ground's material (`ShipSky::CopyBodyLook`), so the handover
has no brightness step.

**The sky polls and stores nothing** (conflict 2). `AShipSky` has no
`SetSystem` and no `SetInTransit`: nothing calls into it, and nothing needs to
find it. Every frame it asks `LocalSystem` (`Current`, `Serial`,
`InTransit`), which asks `UUniverseSubsystem` and `UShipSubsystem`. What it
keeps is a cache of its own drawing -- the proxies, keyed by the jump serial
and by the system's name and star position -- which it rebuilds whenever the
key stops matching. Its one write to the flight state is `ds.Sky.Goto`, a
one-shot placement for tuning. The counter-frame follows the same rule for
the course marker and the streaks.

**The GPU rounds a proxy's transform, and the ground amplifies it by R /
h.** GPU Scene stores every instance transform compressed
(`FCompressedTransform`, on for Vulkan SM5/SM6): the scale keeps 15
significant bits and the rotation is a 16-bit octahedral axis and a 15-bit
spin, so either comes back up to ~3e-5 off. A proxy's near side is its
centre minus its radius, both R / h times the near side, so that rounding
lands on the ground under the ship times R / h -- up to 3e-5 R / h of the
altitude, 1.6% at 10 km over a 5,400 km world and about 1.9% at an Earth's
or a Jupiter's floor, falling as the ship climbs (0.4% at 50 km over an
Earth); a different fraction every frame as the scale moves, and a sideways
slide of the face whenever the ship turned. That was
the playtest's "tearing" 10-15 km up. So `SkyProjection::Project` rounds each
proxy's scale *up* to one the GPU keeps exactly (`RenderableScale`, a
homothety 2^-14 larger at most, placed to match), and `AShipSky` draws every
proxy **unturned** -- an absolute rotation, the identity, and an absolute
scale, the view's -- while
`M_SkyBody` turns the face with the ship itself through `BodyAxisX` and
`BodyAxisY`, full-float parameters. Never give a proxy a rotation, and never
set its scale to anything but `FSkyBodyView::ProxyScale`.
`DeepSpace.Sky.ProxyOnTheGpu` runs the proxies through the engine's own
compression, fails if that compression ever stops rounding, and shows the
unrounded scale putting the ground about 1% off; it is the evidence the repo
keeps. (The fix was also judged on before/after offscreen renders of a 1 m
descent at 10 km, which were not kept.) `DeepSpace.Sky.MaterialContract`
evaluates `M_SkyBody`'s turn -- the direction the noise reads and the
normal the light meets -- under an oblique `BodyAxisX`/`BodyAxisY`, so a
graph that skips the turn or transposes it goes red.

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

**`M_SkyBody`'s face is one shared file.** Every band is
`Shaders/Private/WorldRelief.ush` -- the noise the C++ ground compiles too
(`Surface/WorldRelief.*`, landing decision 1) -- reached through one Custom
node as `/Project/Private/WorldRelief.ush`, a path the engine maps to the
project's `Shaders/` by itself at start-up (no project module does it;
`DeepSpace.Surface.ShaderMapping` holds the mapping). **A Custom node's HLSL
error is invisible headless**: the translator passes, no shader compiles
under `-nullrhi`, and every world draws grey with every test green.
`Tools/eyes.sh Eyes.WorldReliefParity` is what renders it: run it after any
edit to the `.ush`, and after re-authoring the sky's materials. It holds the
GPU's float to the same file in double at the measured float floor; a
mutant that only folds away in the shader compiler (`(x + c) - c`) proves
nothing there, since the GPU never sees it.

The materials are unlit, and the sun lights only the ship. The glass casts no
shadow, and its `M_SkyGlass` reflects the lit room through `MPC_Sky`'s
`InteriorLight` and `Veil`: lights off at the console and the stars come out.
`AShipSky`'s `Sun` is the only `DirectionalLight`, and `verify_level.py` fails
on any other, and on any `SkyLight`, `SkyAtmosphere`, cloud or fog. When a
planet crosses the sun, the deck darkens by the fraction covered
(`SunVisibleFraction`; the eclipse).

**A star's surface is honest to a ceiling**: `ds.Sky.StarSurface` x
min((T / T_sun)^4, 8) (`SkyProjection::StarWarmth`, flight-feel decision 9).
The 8x ceiling, a star of about 9,700 K, is also the half-float guard:
hotter stars differ only in colour.

**The dust is honest where it can be, and never slower than cruise**
(flight-feel decision 8): the counter-frame's motes stream past at the
ship's own speed up to `ds.Sky.DustKnee` (2 km/s), and above it at a *seen*
speed that rises on a log scale to `ds.Sky.DustTop` (3 km/s) at the drive's
top, 0.1 c, drawn up to `ds.Sky.DustStretch` (8) times long
(`ShipDust::SeenSpeed`, `Stretch`). Cruise's upper decade, 2 to 20 km/s,
runs through the log part: its top is seen at 2.2 km/s and drawn 1.6 times
long, exactly as the drive's bottom notch is. They never fade, and every
notch is seen at least as fast as cruise's top, so the drive never looks
slower than cruise. DustTop is 50 m
a frame at 60 Hz, under the spacing that strobes; at 30 Hz it is past it,
which is the playtest's question.

**The bodies are drawn with `SM_SkyBody`, never the engine Sphere.** The
Sphere is 32 segments round, and its polygon showed on the limb from
10,000 km down -- where the limb's curvature is how near the world is.
`SM_SkyBody` is an equal-angle cube sphere built from code
(`Sky/SkySphereMesh.h`, through `UDeepSpaceEditorScripting::BuildSkySphere`,
by `setup_sky_materials.py`): 224 cells a face at LOD 0, with six coarser LODs
switching at the screen size where each would stand half a 4K pixel inside
the limb. It is centre-origin with the Sphere's 50 cm radius, so the
projection is unchanged. `DeepSpace.Sky.BodyMesh` holds the asset to the
arithmetic. The counter-frame's points stay on the engine Sphere.

**A world's face and relief are one noise.** `M_SkyBody`'s detail bands are
simplex noise with its gradient: the value brightens the face (behind the
`surface_max_swing` clamp, the half-float guard) and the gradient tilts a
per-pixel normal -- the sphere's own, from object space, so no mesh facet
shows in the shading. Each band's height goes with its wavelength, so every scale the screen holds
has the same slope, and **the amplitude is the ground's** (landing decision
3): `ReliefScale` is `FWorldRelief::SlopeScale`, the world's drawn peak over
its radius and the sum's bound, so the orbit shades exactly the heights a ship
lands on -- Earth-like, 1/g, capped at 10 km -- and relief is data (the
priors), not a knob: `ds.Sky.Relief` and `ds.Sky.Craters` are retired, because
a knob that moved the height would move the ground under a landed ship.
Craters are summed compact kernels (`WR_CraterSum`), one per kept site, in
bands stepping by four (the count wider than D goes as D^-2); their albedo is
fewer in the basins, their height the same everywhere, scaled by the world's
`Cratering` (bare rock 1, ice 0.5, terrestrial 0.15, ocean and giants 0).
Giants keep a fixed cloud billow (`ShipSky::GiantReliefScale`); oceans are
flat. A giant's belts come from its day
(`FPlanet::DayHours`, log-normal about 12 h, drawn by the generator), by the
Rhines scale (`SkyLook::BeltPairs`). Relief shows where the light is low, as
real relief does: a world under a high sun still looks smooth.

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

- **The drive** is in-system: F at the helm (`SetDriveEngaged(Commander,
  bool)`, gated on the pilot) and its own lever (*Flying*). It is a lever: it
  stays set when the pilot stands up. Leaving it spools down to cruise's top
  at the lever's own pace (`SPOOLING DOWN`), never in one substep, and goes
  on down the soft cap while the cap holds the ship faster than cruise could
  brake from (the hold beats the braking curve above 12.8 km/s, 51.2 km up):
  cruise is handed a ship only on its own braking curve
  (`FShipFlightState::CruiseCanTakeOver`), never one that would meet the
  hard stop at speed. It has no
  inertia and reports no acceleration.
- **The jump** folds between stars, or to a world of this system:
  `SetJumpEngaged`, `IsJumpEngaged`, `EJumpState {Idle, Winding, Ready,
  Transit}`, `NavText::Jump`, and the HUD's `JUMP WINDING` / `JUMP READY` /
  `BETWEEN STARS` / `IN THE FOLD`. The decisions are the pure
  `FShipNavState`; `UShipSubsystem` acts on them. Its tunables are `ds.Nav.*`.

**The soft cap** (flight-feel decision 5, ruling 2): the lever sets the
speed, and **only when the nose's own ray meets a floor sphere** does
anything hold it back. Then it takes the whole speed, along the nose, so the
ship always goes where it points: when the floor is `ds.Drive.HoldSeconds`
(4 s) off at the present speed it binds, the distance falls by e every 4 s,
and below the braking knee -- 51.2 km at 12.8 km/s on full boosters, since
2 km/s^2 -- the ship comes down the braking curve on 80% of its boosters
(`ShipFlight::MaySpeed`) to rest on the floor. The curve is the *stepped*
one, v^2 / 2b + v dt / 2 = D, on which the speed falls exactly 80% of a
substep's thrust each substep all the way to rest, so a ship with inertia
can follow it; the continuous sqrt(2bD) asked more than the boosters have
in its last few substeps, and at 2 km/s^2 cruise met its floor at 50 m/s.
A path that misses is not touched: a ship at 0.1 c past a world holds 0.1
c. What the cap
holds, the ease follows, so letting go never snaps the speed up. The corner
says `HOLDING OFF` while it holds more than 5% off the lever and `AT THE
FLOOR` there. Cruise uses the braking curve alone, since it has inertia,
and its hard stop, for a slide after a turn, lands the ship on the floor
and slides it there.
Every body is a floor sphere, tested along the ray (`ShipFlight::RayToFloor`),
so nothing can be tunnelled through.

**The floor is where the sky stops being honest** (decision 6):
`UShipSubsystem::FloorFor`, the one function that answers it -- over a world
the larger of `ds.Flight.Floor` (10 km) and `SkyProjection::RenderedFloor`,
10.2 km over an Earth and 112 km over a Jupiter; over a **solid** world that
is taken **above its highest peak** (`FWorldRelief::MaxHeightCm`, landing
decision 10), so the drive never meets a summit; over a star
`ds.Flight.StarFloorRadii` of its radius. It is **the drive's** floor. Over
a solid world cruise and the vertical lever read the ground instead (*Landing*).
**The system's edge is a surface** too (conflict 10): an inside-out
floor sphere `ds.Flight.Floor` inside `InSystemRadiusLy`, so the drive
settles into it and never flies the ship out of its system, where
`GetSystemAt` would go empty under a sky still drawing the old one. You
leave a system by jumping. In transit there are no surfaces.

At 0.1 c the cap binds 120,000 km off (4 s) and the ship is on the floor
about 40 s later; from three light seconds out that is a little over a
minute. 1 AU at 0.1 c is 83 min and 30 AU is 41.6 h, which is why the
in-system jump exists; from an in-system jump's arrival an Earth's floor is
about 47 s away, a Jupiter's 2 min 46 s.

**The jump has three levers, each left where it is set**: the course (the
chart for a star, the map's `Jump here` or `ds.Nav.Plot target` for the
target, `ds.Nav.Plot` for either), the heading (the helm; the HUD's bearing
words, and the nose caret on the teal course marker), and engage (the chart,
the map's `Jump here`, or `ds.Nav.Engage`). One course: a new one of either
kind replaces the old, and an engaged jump stays engaged across it (the map
spec's open question). Engaged, the engine asks for `ds.Nav.WindingWant`
(380 W) and the charge winds at a rate the watts it actually gets scale.
Starved, it still winds at `ds.Nav.StarvedRate` of full; it never stops. It
asks for nothing otherwise, so staying put is never taxed. Once plotted,
engaged, charged, and within `ds.Nav.ConeDeg` of the nose, **the fold opens
by itself.** There is no confirm, and there must not be one. A final button
would make the player come back on the jump's schedule to service it, and a
game that makes you do that is telling you that you are behind: the
anti-chore principle's definition of a chore (developer's ruling). If it
feels like the game acting without you, the answer is a softer cue before
the fold -- the hum already rises as the jump winds -- never a confirm. For
the same reason **no screen shows the jump's charge** as a percentage, a bar
or a countdown: a charge that fills is a clock to watch, and waiting it out
is the jump's schedule, not the player's. That rule is about the charge. It
was read for a while as "no time on any screen", and the developer ruled it
back on 2026-09-26: a live time to arrival for an approach the player chose
is allowed, and the target line and the system map carry one (*The system
map and the target*).

**Every fold is an all stop, and every jump arrives at rest** (flight-feel
decision 4): the fold opening puts both levers to STOP, and the arrival,
`FShipFlightState::JumpTo` (the flight state's fourth write path, ADR 0005,
amended), zeroes the velocity and the drive's eased position -- load-bearing
whenever the fold is shorter than the 5.9 s the ease takes down from 0.1 c
(the default fold is 6 s; the jump tests hold it to 4 s so that it is). The fold lasts
`ds.Nav.TransitSeconds`, with streaks past the window. An interstellar
arrival is a translation onto the line from the departure point to the star,
at `max(ds.Nav.StandoffAU x sqrt(L), 1.5 x the outermost orbit)` (conflict
9), and lets go of the target. **An in-system jump** (map decision 12) is
the same fold to the target: plotted only while the ship is farther than
`NavStart::WorldReachFactor` (2) standoffs from it (`IsNearEnoughToFly`), it
arrives on the line from where the fold *opened* (`FoldDeparture`) at the
standoff that shows the world `ds.Nav.WorldStandoffDeg` (2 degrees) across,
outside every floor, with the target kept. Orientation is untouched either
way, so what you aimed at is where the nose is and the distant stars do not
move. The course is the only universe data the ship keeps, as an id (ADR
0003, amended). **No cooldown or limit on jumps is built**: the developer
wants one eventually, and the map spec's *Open questions* records why any
design must start from its tension with the anti-chore principle -- it is a
wait imposed on the player.

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
(`ds.Hum.CruiseHiss`). Under a solid world's drive floor the hiss also
follows the boosters' **hold**, in watts delivered (`ds.Hum.HoldHiss` x
watts / (3 x `ds.Boosters.HoldWatts`), never above cruise's hiss), so it is
silent wherever a ship can be parked. `ds.Hum.Volume` is the first knob if
it wears. Each air
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
cockpit, with `UNavigationWidget` built in C++. It shows where you are, the
six nearest systems, the jump as a word and the course as a bearing, and an
in-system course as `in this system`. Clicking a row plots it (again clears
it), and one toggle engages or stands down. That is all it does: aiming is
the helm's.

**The chair is a seat, not a lock** (map decision 13, ruling 4). E sits you
down facing the desk, and nothing is framed. Then E on the chart zooms the
chart, E on the map zooms the map (the same fitted framing either way), E
zoomed goes back to the seat, and E on neither stands up; the prompt says
which (`(E)  Chart`, `Map`, `Back`, `Stand up`, from
`AShipScreen::GetZoomPrompt`). A screen says what it allows by class, never
per instance: `IsZoomableFromChartChair`, `ZoomsOnSit` (the laptop, which
frames as it always did) and `IsDrivableSeated` (the map and the chart).
**The chart is used without zooming** (playtest note, 2026-09-27: "can't be used
without focusing"): seated in its chair, or at the helm (developer's ruling,
2026-09-27: "the chart is clickable from the helm too, for full parity with
the map"), look at a row or the toggle and click, as the pilot does the map;
zooming only brings it closer, and only from the chair. From the helm the
chart is two metres off and seen obliquely -- about 2.7 times minified
across and 1.5 down -- so its rows are as tall on screen as the map's but its
words are narrow and may shimmer; reading it is a playtest question.
**Tab on the zoomed map** cycles the target
outward (`CycleTarget`); anywhere else it does nothing -- at the helm too,
where the pilot clicks (developer's ruling, 2026-09-26). The chair's first
view is aimed from the *seated* eye (`ADeepSpaceCharacter::SeatedEyeOffset`,
held by `DeepSpace.Player.SeatedEyeIsPilotEye`), because E runs before the
frame's animation. `IsUsingScreen()` means a screen is framed and
`IsInScreenChair()` that the body is in a screen's seat. It keeps nothing it
could ask for, so a course plotted from the console shows here untold. The
jump's word is asked every frame; where the ship is, the rows and the course
cost a sector scan or a generated system, so they are asked again only when
something they depend on has changed (`UNavigationWidget::FAskedAt`: the jump
serial, transit, the plotted system, the ship's position past
`MovedFarEnoughCm`, its heading, `ds.Nav.RangeLy`, the priors). The chart is
in view from the helm and would otherwise pay for that every frame. The chair
beside the helm is not a second station (vision: shared presence, never
division of labour).

**It is laid out as the map is**: a canvas with every size a constant in
`NavigationWidget.cpp`, the map's sizes times 816 / 600, so text on the two
desk screens is one physical size. Its 816 x 576 is what the panel spans
from its chair's eye (about 970 x 660 screen pixels on the 4K display):
never minified where it is read, as the map is not at the helm. The title
names where the ship is at its right; the plotted mark has a column of its
own; the band at the bottom is the jump's word with the toggle level at its
right, then the course, wrapped at a set width to at most two lines.
**A `UButton` centres its content**: a row of columns inside one is laid out
at its own desired width, squeezed and centred, and no two rows line up --
the first chart's spacing fault, and the map's before it. Set the button
slot to `HAlign_Fill`. `DeepSpace.UI.ChartLayout` lays the tree out as Slate
does with no renderer (`SlatePrepass`, then `ArrangeChildren` down the tree,
which works under `-nullrhi`) and holds every word to the panel, to the room
it asks for and clear of every other, the columns to one x, a plot to moving
no name, and all of it again at the widest the chart can print -- each asked
of what makes it and measured in the chart's font, never typed in: the name
from `SystemNames::WidestName` (the tables' limit, 16 letters, wider than
anything in the corpus), the bearing from every direction `NavText::Bearing`
can word. It measures the span from the chart's own seat
(`GetUseTransform` and `SeatedEyeOffset`), so moving the chair back until
the chart is minified fails it, and holds every font size the chart sets to
one the map sets at the same centimetres on the glass.

It is placed by `build_hauler.py` (`place_nav_screen`, `hauler_nav_screen`)
from `NAV_SCREEN` in `hauler_layout.py`. **Its two seat tunables,
`UseDistanceCm` and `ViewDistanceCm`, are per-instance `UPROPERTY`s that
`place_nav_screen` sets**, so a nudge is an edit there and a level rebuild, not
C++. There is no seat height: every screen seats the body on the floor under
its chair, as the helm does, and the sitting idle lifts the hips onto the
chair. A `SeatHeightCm` survived that change for a while, moving nothing. Its `Reach` box sits *behind* the panel's face. A volume
enclosing the panel blocks the channel the pointer traces on, and the screen
draws perfectly and cannot be clicked. `DeepSpace.Ship.NavScreen` and
`DeepSpace.UI.NavigationScreen` spawn it before `World->BeginPlay()`, as every
screen test must.

**Sat at any screen, the view is fitted to it, never a fixed angle.**
`AShipScreen::FitFieldOfView` picks the field of view that shows the whole
panel, bezel included -- `BezelCm` is per axis, X each side and Y top and
bottom, because the laptop's lid is not evenly wider than its glass -- with
`ds.Screen.FrameMargin` (2%) clear at the edges of its tighter axis. That puts
the laptop at the 52 degrees the developer approved. It has to model the engine's aspect rule: under the
default `AspectRatio_MaintainYFOV` a camera's field of view is horizontal *at
the camera's 16:9*, and a narrower window loses width, so an angle that frames
a panel at 16:9 cuts its sides off at 4:3. `DeepSpace.Ship.ScreenFraming`
checks both screens through the engine's own projection.

**Standing up returns the body to where it stood, if that was floor.** Floor
means something under the spot from 5 cm above to 15 cm below the floor under
the seat (`AShipScreen::GetUseFloorZ`) -- never the height the feet were at,
since Jump is bound and a player can sit down from on top of the chair. The
spot must also fit a standing capsule. Otherwise it is the first spot, on
four rings 30 cm apart out to 1.2 m, twelve directions each and the side away
from the seat first, that is floor by the same measure, fits a standing
capsule, and can be reached by sweeping a standing capsule along the floor
from the remembered spot, ignoring whatever occupies that spot. The sweep
cannot step up, so a spot past a raised lip is refused. If no spot passes,
the body goes back where it stood and a warning is logged. Sitting and
standing each mark a camera cut (`SetGameCameraCutThisFrame`), so temporal AA
and motion blur do not smear the frame the view jumps
(`DeepSpace.Ship.ScreenStandUp`).


## The system map and the target

`AShipMapScreen` is the middle cockpit desk screen, on the centre line
between the helm and the chart chair (`MAP_SCREEN` in `hauler_layout.py`,
`place_map_screen`, 68 cm wide), drawing `USystemMapWidget` at 600 x 424 --
the size it is seen at from the helm, so every size in it is a helm pixel.
It is a warped-log orrery fixed to the universe's axes (`SystemMap::Fit`,
pure in `UI/SystemMapLayout.*`): the star, each world's ring and dot, the
ship, and a row per world with its surface distance; the rim is the arrival
standoff with a margin. It stores nothing it can ask for, and caches only
its drawing, keyed on what the drawing depends on.

**The warp bends every bearing but two.** A radial log warp is stretched
round each ring several times more than across it (`R / (r dR/dr)`, about
3x at an outer ring), so only a bearing straight toward or away from the
star, or between two points at the same distance from it, is drawn true;
any other is bent, median ~4 and up to ~50 degrees. The ship's glyph
therefore takes the same warp as the dots (on the dot at the world, closing
on it monotonically along any straight flight:
`DeepSpace.UI.SystemMap.StraightApproach`), and **its tick is the way the
glyph moves, never the nose's universe direction** (`SystemMap::MotionOnMap`,
the derivative of `Ship`'s own placement). Drawn in universe directions it
pointed up to 90 degrees off the glyph's motion and ~50 off the dot the nose
was on -- the playtest's "map does not track" (`.TickFollowsGlyph`, and
`.Bearing` for the two true bearings, which also holds the bent ones' spread
to those figures). Where the glyph is held -- pinned, or at the floor clear of
the star's disc -- it cannot move radially, and the tick is the way it would
move were it free, the warp continued past the hold (`WarpPxPerDex`): the true
derivative there flipped the tick 90 degrees for a degree of heading
(`.HeldTick`). The bent bearings stay, to be tried in play (developer's
ruling, 2026-09-27). **The ship is drawn top-down**: radius
and azimuth both from its position in the plane, the elevation the footer's
line, a ship over the pole held at the star's edge (the same ruling, which
reversed the spec's true-3D-distance radius: approaching from an interstellar
arrival, off the plane, that stepped the glyph away from the dot about 1 time
in 80). `.StraightApproach` flies from off the plane and from arrival points
too.

**The helm looks and clicks** (decision 2, ruling 4): E at the map sits
nobody down. Seated, `UpdatePointer` gates the pointer on a trace along the
view: live only while the first thing hit is a screen whose class says
`IsDrivableSeated` -- the map or the chart, from either seat -- and then
**handed the gate's own hit**
(`EWidgetInteractionSource::Custom`, `SetCustomHitResult`). The pointer's own
trace ignores only its pawn, and the helm's seated eye is *inside* the helm
seat's reach box, so it met the seat and never the map; the gate's trace
ignores the chair the body is in. The button is let go *before* the pointer
goes off, or the release is dropped and the next click is swallowed as a
repeat. A click on a dot, or a row, targets that world; on the target again
clears it (`SelectWorld`, the one seam both end at).

**The target is an `FBodyId` the ship holds** (`SetTarget`, `ClearTarget`,
`GetTarget`, `CycleTarget`), never a copy: its position, radius and name are
procgen's, asked every time. Nothing in the flight reads it -- no autopilot.
`GetTargetView(Here)` builds `TargetMarker::View` from the ship's own
position, attitude, velocity, the world's `FloorFor`, the braking and
`ds.Drive.HoldSeconds`, and **the HUD's target line and the map's band print
the same view** (`TargetMarker::Line`): `› Kessa II · 0.1° to starboard ·
1,496 THOUSAND KM · ETA 65 S · NIGHT SIDE`. The bearing's `dead ahead` is
the world's own disc, never under `TargetMarker::AheadFloor` (0.25 degrees),
not the jump's 8-degree cone -- so with an in-system jump plotted the jump
line and the target line can disagree by design (the map spec's *Two
bearings*).

**The ETA is live** (ruling 3): at 1 m/s or more, when the velocity's ray
meets the world's floor sphere, `ShipFlight::SecondsToFloor` of that
distance at the present speed under the cap's own law (its braking part on
the continuous curve, which the cap's stepped one undercuts by under 7 m/s,
about half a substep) -- so it counts down a
second a second and names the moment the ship arrives
(`DeepSpace.Playtest.EtaCountsDown`, `DeepSpace.Ship.Target`). In cruise over
a solid world -- and in DriveBelowFloor, which flies cruise -- it counts to
**the ground**, at every altitude, under the law the ship flies: inside the
near regime `ShipFlight::SecondsToGround` (the approach law and the skim
cap), above it the braking curve to where cruise stops, the hull's reach
short of the ray (`DeepSpace.UI.TargetMarker.GroundEta`). A cruising ship
now passes through the drive floor with nothing happening there, so
`FloorFor`'s floor is the ETA's only under the drive and its spool-down. On a path that
misses it says `PASSING <altitude> UP`; at rest, nothing. While the lever is
still spooling up it overstates. The bottom-left corner shows no time: it has
no destination.

**The marks** (decision 7) are `UShipTargetOverlay`, a child of the HUD's
canvas, drawn from geometry and never from brightness: teal corner ticks
round the target for anyone who can see it *through the glass*
(`TargetMarker::SeenThroughGlass`, a trace that must first meet an actor
tagged `ShipTags::Glass`, `Sky.Glass`; volumes the eye starts inside are
stepped past), at least `ds.HUD.TargetMinPixels` across, so a sub-pixel world
or four black pixels on the night side are still found; an edge chevron, for
the pilot only, when it is off the view; and, for the pilot with a target
and at 1 m/s or more, the prograde mark along the velocity. The pilot's nose caret
shows with a target or any course. At the .03 AU geometry the world is black
on black and only the bracket finds it (`DeepSpace.Sky.NightSideIsDrawn`;
look with `ds.Sky.Goto <world> 4.5e6 night`).

**The band's button** is the in-system jump's: `Jump here` plots the target
and engages in one press, so the in-system jump needs no second screen,
`Stand down` while the course is the target, and `Near enough to
fly`, disabled, inside the target's reach.

**Landing works from the nearest surface** and uses the target only to name
it (decision 9). The map has room for moons; procgen makes none.

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

## Parts and bays

The ship is fitted, not fixed
(`docs/superpowers/specs/2026-09-27-ship-wear-and-upgrades-design.md`).
Slice 1, the upgrade seam, is built. The install act, the save and wear are
not. **A part is a module in a bay, and it changes a number.** An upgrade is
a different number, never a different model.

- **Six fixed bays and two auxiliary slots** (`EShipBay`, `Ship/ShipParts.h`):
  reactor, drive, boosters, lights, life support, sensors, `Aux1` and `Aux2`.
  Each holds one part.
  - `UShipSubsystem::FitPart` swaps, and the displaced part joins the spares.
  - A part whose `Bay` is `None`, or with no id, is refused.
  - A core part is only ever swapped; `RemovePart` is for aux slots.
- **An empty bay reads the stock part and draws nothing**, so a bare test
  world is today's bare world. A played ship fits the six stock parts, from
  `BP_DeepSpaceGameMode`'s list, which is what `Tests/StockShip.h` installs.
  - **The stock ship is today's ship**: 1400 W, 620 W of draws, 1370 W at
    rest, 380 W winding in 45 s, 3 notches/s, 12 ly.
- **Rated values are derived, never stored.** `GetRatings()` is the stock
  numbers with every fitted part's ratings over them.
  - Each rating belongs to one bay, and no part rates a top speed.
  - A fit moves the supply and the lights' want at once.
  - **The boosters' want has one writer, `ApplyAllocation`.** Landing's
    hold adds to it there.
- **Draws are booked by bay** (`Bay.Lights`). This resolved the old clash
  between the `DA_Lights` draw and the `Power.Lights` consumer: the Lights
  part owns both.
  - Test loads go through `AddLoad`/`RemoveLoad` (`Load.<name>`). They are
    not parts.
- **The catalogue is `Tools/ship_parts.json`.** `Tools/setup_ship_parts.py`
  authors `Content/Ship/Parts/DA_*`, `DA_ShipCatalogue` and the game mode's
  list from it (editor closed, through the lock; the report is
  `Saved/setup_ship_parts.txt`). **Never edit a part asset by hand.**
  - `DeepSpace.Ship.Parts.Contract` holds the JSON, the assets and
    `FShipRatings::Stock()` equal.
  - `.CatalogueRules` holds decision 7 over every row:
    - every combination is whole at rest under the stock reactor;
    - every upgrade is at least as open as stock on every axis of its bay;
    - no part in a bay dominates another;
    - aux parts rate nothing and draw nothing at rest.

    **Parts widen what the ship can do; they never make it need more of
    anything.**
  - `FindPart` resolves ids through `DA_ShipCatalogue`, which `CatalogueAsset`
    in `[/Script/DeepSpace.ShipSubsystem]` names.
- **Four CVars override the fitted part.** `ds.Nav.RangeLy`,
  `ds.Nav.ChargeSeconds`, `ds.Nav.WindingWant` and `ds.Drive.Response` default
  to `-1`, the part's. 0 or more overrides it for the session
  (`ShipParts::Effective`, applied only in the ship's getters). Wherever this
  file quotes one of them as a number, read the fitted part's rating.
- **The console's nameplates**: one line per fitted part,
  `BAY  Name  figure  Words`, each figure the part's own. No CVar or live
  value moves a plate, and an empty slot has no line
  (`DeepSpace.Ship.Parts.NameplatesAreFacts`).
- **The state is plain** (`FShipLoadoutState`): bays by name, and spares with
  their own state. The wear fields stay zero until slice 4.
  `RestoreLoadout` falls back to the stock part for whatever it cannot name.

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
ds.Nav.ChargeSeconds 5      wind from cold in 5 s, not 45, for every jump after
ds.Nav.Engage               engage (ds.Nav.Engage 0 stands down); aim, and it fires by itself
```

The system and the drive, from the console:

```text
ds.Nav.Target               the worlds here, numbered by orbit (I is 1), the target marked
ds.Nav.Target 2             target world II (or a name, next, none)
ds.Nav.Plot target          the target as the jump's course (the map's Jump here, without the engage)
ds.Sky.Goto 3 4.5e6 night   the .03 AU question: 4,500,000 km beyond body 3, its star behind it
ds.Sky.Goto 3 10 dusk       10 km over ground where its star stands 10 degrees high: relief and craters in raking light
ds.Drive.Top 0.01           shorten the drive lever to 0.01 c for a session (never above 0.1 c)
```

`ds.Nav.Charge` fills the charge on the next tick, once. `ds.Nav.Clear` drops
the course. `ds.HUD 0` hides the HUD for an unadorned look. `ds.Dress.LivedIn`
and `ds.Dress.Seed` redress the ship where you stand (*The dressing*).

Parts, from the console, until there is somewhere to find them:

```text
ds.Ship.Install Reactor.TwinCore   fit a part by id or name; ds.Ship.Install Reactor.Stock puts it back
ds.Ship.Install Drive.QuickLever   the drive that follows its lever half as fast again
ds.Ship.Spares                     the spares aboard; 'give <part>' adds one, 'clear' empties them
ds.Ship.Describe                   every bay: its part, draw and ratings (a developer's line)
```

## Where each tunable lives

No value below has been settled by a playtest yet. Each is a
`TAutoConsoleVariable` read at use and never cached, so a playtest moves it
with no rebuild. **Write a settled value back as the default in the file
named.** Where a default comes from a header constant, the constant is the
default: change it there, and expect `./rebuild.sh --force` and the pure
tests that assert it.

| CVar | Default | Lives in |
|---|---|---|
| `ds.Nav.ChargeSeconds` | -1: the drive part's (stock 45 s, settled 2026-09-26) | `ShipSubsystem.cpp`; the part's number is in `Tools/ship_parts.json` |
| `ds.Nav.WindingWant` | -1: the drive part's (stock 380 W, settled 2026-09-26) | `ShipSubsystem.cpp`; the part's number is in `Tools/ship_parts.json` |
| `ds.Nav.StarvedRate` | 0.2 | `ShipSubsystem.cpp` |
| `ds.Nav.FoldDraw` | 0 W | `ShipSubsystem.cpp` |
| `ds.Nav.TransitSeconds` | 6 s | `ShipSubsystem.cpp`, from `FNavTuning` (`ShipNavState.h`) |
| `ds.Nav.ConeDeg` | 8 deg | `ShipSubsystem.cpp` |
| `ds.Nav.StandoffAU` | 2.4 AU | `ShipSubsystem.cpp`, from `NavStart::DefaultStandoffAU` (`NavStart.h`) |
| `ds.Nav.RangeLy` | -1: the sensors part's (stock 12 ly) | `ShipSubsystem.cpp`; the part's number is in `Tools/ship_parts.json` |
| `ds.Nav.PlaceAtStart` | 1 | `ShipSubsystem.cpp` |
| `ds.Nav.WorldStandoffDeg` | 2 deg (the world's width at an in-system arrival) | `ShipSubsystem.cpp`, from `NavStart::DefaultWorldStandoffDeg` (`NavStart.h`) |
| `ds.Drive.Top` | 0.1 c; clamped to [20 km/s, 0.1 c], so it can only shorten the lever | `ShipSubsystem.cpp`, from `ShipDriveLever::DefaultTopLight` (`ShipDriveLever.h`) |
| `ds.Drive.Response` | -1: the drive part's (stock 3 notches/s at full thrust) | `ShipSubsystem.cpp`; the part's number is in `Tools/ship_parts.json` |
| `ds.Drive.Sweep` | 3 notches/s, a held key after 0.3 s | `ShipSubsystem.cpp`, from `ShipDriveLever::DefaultSweep` |
| `ds.Cruise.Sweep` | 0.2 of the lever a second (the lever reads on a log scale) | `ShipSubsystem.cpp`, from `ShipDriveLever::DefaultCruiseSweep` |
| `ds.Drive.HoldSeconds` | 4 s; 0 or less is the braking curve alone | `ShipSubsystem.cpp`, from `ShipFlight::DefaultHoldSeconds` (`ShipFlightSurface.h`) |
| `ds.Flight.Floor` | 10 km (never under the sky's rendered floor) | `ShipSubsystem.cpp`, from `ShipFlight::DefaultFloorCm` |
| `ds.Flight.StarFloorRadii` | 1 | `ShipSubsystem.cpp`, from `ShipFlight::DefaultStarFloorRadii` |
| `ds.Land.GearClearance` | 150 cm | `ShipSubsystem.cpp`, from `ShipLanding::DefaultGearClearanceCm` (`ShipLanding.h`) |
| `ds.Land.TouchdownSpeed` | 0.5 m/s | `ShipSubsystem.cpp`, from `ShipFlight::DefaultTouchdownSpeed` |
| `ds.Land.ApproachSeconds` | 4 s, clamped to at least 0.5 s | `ShipSubsystem.cpp`, from `ShipFlight::DefaultApproachSeconds` |
| `ds.Land.SkimSeconds`, `.SkimFloor` | 2.5 s, 20 m/s | `ShipSubsystem.cpp`, from `ShipFlight` |
| `ds.Land.Regime` | 50 km (leaves over 55 km) | `ShipSubsystem.cpp`, from `ShipFlight::DefaultRegimeCm` |
| `ds.Land.DriveHandback` | 500 m | `ShipSubsystem.cpp`, from `ShipFlight::DefaultDriveHandbackCm` |
| `ds.Vertical.Top`, `.Sweep`, `.HeavyFloor` | 200 m/s, 0.25/s, 0.25 | `ShipSubsystem.cpp`, from `ShipVerticalLever` |
| `ds.Boosters.HoldWatts`, `.StarvedSink` | 150 W per g (cap 3 g), 2 m/s; both only under a solid world's drive floor | `ShipSubsystem.cpp` |
| `ds.Terrain.SplitFactor`, `.MaxTiles`, `.BuildTasks`, `.UploadsPerFrame`, `.Show` | 2.0, 2,500, 2, 4, 1 | `WorldGround.cpp`, from `TerrainQuadtree` |
| `ds.HUD.TargetMinPixels`, `.TargetEdgeInset` | 28, 48 (slate units) | `ShipTargetOverlay.cpp`, from `TargetMarker` (`TargetMarker.h`) |
| `ds.Nav.MarkerPixels`, `.StreakLength`, `.StreakSweep` | 6 px, 40, 5 | `ShipCounterFrame.cpp` |
| `ds.Sky.DustKnee`, `.DustTop`, `.DustStretch` | 2 km/s, 3 km/s, 8 | `ShipCounterFrame.cpp`, from `ShipDust` (`ShipCounterFrame.h`) -- a playtest gate: candidates knee {1, 2}, top {2.5, 3, 3.5}, stretch {4, 8, 16} |
| `ds.Sky.StarWarmthGamma` | 1 (honest T^4) | `ShipSky.cpp` -- TEMPORARY: 0.5 is the old compressed T^2; deleted with its test case once the glare is judged |
| `ds.Sky.Exposure`, `.ExposureMode`, `.ExposureRange` | 0.7 (estimate), 1, 1.5 | `ShipSky.cpp` |
| `ds.Sky.Radiance`, `.SunLux` | 3.0, 9.4 lux | `ShipSky.cpp` -- keep SunLux at pi x Radiance |
| `ds.Sky.FluxGamma`, `.PointPixels`, `.StarSurface` | 0.5, 2 px, 1000 | `ShipSky.cpp` |
| `ds.Sky.StarfieldFaint`, `.Mottle`, `.Veil`, `.Bloom` | 0.01, 0.35, 1.0, 0.675 | `ShipSky.cpp` |
| `ds.Sky.SurfaceDetail` | 0.3 | `ShipSky.cpp` |
| `ds.Hum.Volume`, `ds.Hum.CruiseHiss`, `ds.Hum.HoldHiss` | 1.0, 0.35, 0.35 | `ShipHumComponent.cpp` |
| `ds.HUD` | 1 | `ShipHUDWidget.cpp` |
| `ds.Screen.FrameMargin` | 0.02 | `ShipScreen.cpp` |
| `ds.Dress.LivedIn`, `ds.Dress.Seed` | 1, -1 (the world's own) | `ShipDressingSubsystem.cpp` |

Tunables that are not CVars: the universe's seed and priors, and the
dressing's rules (`Config/DefaultGame.ini`, above, reloaded with
`ds.Universe.ReloadPriors` and `ds.Dress.Reload`; write a settled dressing
number back into `ShipDressingRules.cpp`); room moods and practicals
(`hauler_layout.py`, a level rebuild); the chart's seat (`place_nav_screen`, a
level rebuild); every part's draw and rated values -- the reactor's supply,
each consumer's want, the boosters' 2 km/s^2, the drive's response and
charge, the array's range -- in `Tools/ship_parts.json`, authored into assets
by `Tools/setup_ship_parts.py` (*Parts and bays*). And, as named constants
with tests on them: the drive's notch table, `EaseSeconds` 0.4,
`RepeatDelaySeconds` 0.3, `ArriveNotches` and cruise's log floor
`CruiseFloorCmPerSecond` 1 m/s (`ShipDriveLever.*`); cruise's top 20 km/s and
its astern top 200 m/s (`FShipFlightLimits`, a header change); the 80%
braking margin
(`ShipFlight::BrakingMargin`); the 5% `HOLDING OFF` threshold
(`UShipHUDWidget::HoldingOffShown`); the map's `SystemMap::PickRadius` (14
px) and its 600 x 424 draw size; the chart's layout and 816 x 576
(`NavigationWidget.cpp`, held by `DeepSpace.UI.ChartLayout`); `TargetMarker::AheadFloor`, `NightSideLit`
and `MinSpeed`; `NavStart::WorldReachFactor` (2); the turn rates
(`FShipFlightLimits`, a header change).

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
