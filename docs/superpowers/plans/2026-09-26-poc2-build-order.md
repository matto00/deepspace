# DeepSpace — POC 2 Build Order: Flight Feel and the System Map

**Date:** 2026-09-26
**Builds:** `docs/superpowers/specs/2026-09-26-flight-feel-design.md` and
`docs/superpowers/specs/2026-09-26-system-map-design.md`, both as amended
with the developer's rulings of 2026-09-26 (the amendment at the top of each)
**Branch:** every stage merges onto `poc2/integrate`
**Replaces:** the flight-feel spec's *Seams: landing order* and the map
spec's *Build tracks*, which each described its own half

Four stages. Within a stage the tracks run in parallel worktrees and **no
file has two owners**; a stage merges only when all of its tracks have, and
the next stage branches from that merge. Each track builds against what the
stages before it made, never against a sibling in its own stage.

## Rules every track follows

- **One heavy Unreal process at a time**, machine-wide: `./build.sh`,
  `./test.sh` and every commandlet go through `Tools/ue_lock.sh`
  (`ue_locked ...`). Never call Build.sh, UBT or the editor directly, never
  pass parallelism flags, never background one. A build is about 25 s and the
  suite about 15 s: compile often.
- **Write the C++ first**, then build; after a header change that a
  Blueprint inherits (a `UPROPERTY`, a component, an input action), recompile
  and save `BP_DeepSpaceCharacter` and run `Tools/check_blueprints.py`.
- **Tests**: `./test.sh` (everything, the Python included) or
  `./test.sh DeepSpace.<Area>`. Screens are spawned before
  `World->BeginPlay()` in tests. Tests about the power split run on
  `Tests/StockShip.h`. Important tests are proved able to fail with
  `Tools/mutate.sh FILE 'old' 'new' FILTER` (exit 0 killed, 1 survived), after
  a commit.
- **Never commit** `Content/Maps/L_Hauler.umap` or `MI_Ship_*.uasset`
  (restore them with `git checkout` first). Do commit authored assets: the new
  input actions and `IMC_Default`, which `setup_flight_input.py` rewrites.
- Codebase rules hold throughout: gameplay logic in C++, never Blueprints;
  widget trees in C++; pure cores behind subsystems; state asked of its
  owner, never stored; generated actors by tag; distributions that describe
  the thing (ADR 0008); comments say what a choice believes.

## Stage 1: the pure seams, the map widget, the level

Nothing here depends on anything new. Three tracks.

### 1a. Flight track 0 (the flight-feel spec's Track 0, less 0.3)

**Owns:** `Source/DeepSpace/Ship/ShipDriveLever.h/.cpp` (new),
`Source/DeepSpace/Ship/ShipFlightSurface.h/.cpp` (new),
`Source/DeepSpace/Sky/SkyProjection.h/.cpp` (the `RenderedFloor` extraction
only), `Tests/ShipDriveLeverTest.cpp` (new), `Tests/ShipFlightSurfaceTest.cpp`
(new), `Tests/SkyProjectionTest.cpp` (the `RenderedFloor` cases only).

**Builds:** 0.1 the notch table to 1 c (STOP plus 18), the ease, taps, the
repeat, the cruise sweep; 0.2 `FFlightSurface`, `RayToFloor`, `MaySpeed`,
`Room`, and **`SecondsToFloor`**, the live ETA's law (ruling 3); 0.4
`SkyProjection::RenderedFloor`.

**Tests:** `DeepSpace.Ship.DriveLever`, `DeepSpace.Ship.FlightSurface` (with
the `SecondsToFloor` agreement case), `DeepSpace.Sky.Projection`'s floor cases.

### 1b. The map widget (the map spec's track 3)

**Owns:** `Source/DeepSpace/UI/SystemMapLayout.h/.cpp`,
`UI/SystemMapWidget.h/.cpp`, `UI/SystemMapView.h/.cpp`,
`Ship/ShipMapScreen.h/.cpp` (all new); `UI/NavText.h/.cpp` (`WorldKind`,
`WorldName` only); `Universe/UniverseSubsystem.h/.cpp` (`GetSystemIdAt`);
`Tests/SystemMapLayoutTest.cpp`, `Tests/SystemMapScreenTest.cpp` (new);
`Tests/NavTextTest.cpp` (the kind and name cases); `Tests/UniverseSubsystemTest.cpp`
(the `GetSystemIdAt` case); `Tests/Eyes/MapFromHelmEyesTest.cpp` (new,
temporary).

**Builds:** the warped-log layout, the pick, the painted orrery, the rows and
the title; the cache keyed on the system id, "between stars", the priors and
the standoff; `AShipMapScreen` with its size, widget class and `bUsable =
false`. `SelectWorld(int32)` exists as the one seam both pick paths reach,
with an empty body: the target it would set does not exist until stage 3. The
rim uses `NavStart::ArrivalStandoffAU(System, NavStart::DefaultStandoffAU)`
until stage 3 hands it `UShipSubsystem::GetStandoffAU()`.

**Tests:** `DeepSpace.UI.SystemMap.*`, `DeepSpace.UI.SystemMapScreen` (rows,
cache, `Between stars.`; its target cases are stage 3's),
`DeepSpace.Ship.MapScreen` (spawned before `BeginPlay`, not usable, 600 x
424; its seat virtuals are stage 4's). **Gate:** `Eyes.MapFromHelm`, run once
through the lock without `-nullrhi`, judged on rows, rings and title (the map
spec's decision 11). The file stays for stage 3's second run.

### 1c. The level scripts (the map spec's track 6)

**Owns:** `Tools/hauler_layout.py` (`MAP_SCREEN`, `MAP_SCREEN_WIDTH`,
`PILOT_EYE`), `Tools/build_hauler.py` (`place_map_screen` with
`view_distance_cm` 60; the `Sky.Glass` tag on glass boxes),
`Tools/placement.py` (`GLASS_TAG`), `Tools/verify_level.py`,
`Tools/validate_hauler.py`, `Tools/test_placement.py`, and
`Source/DeepSpace/Ship/ShipTags.h` (new: `ShipTags::Glass`).

**Builds and runs:** the layout, the checks, then, **after 1b has merged**,
one level rebuild through the lock (`build_hauler.py`), `verify_level.py`,
and `check_blueprints.py`. `test_placement.py` reads `ShipMapScreen.cpp`'s
width and `SkyTestWorld.h`'s `PilotEye`, so it passes only once 1b is in:
merge 1b, then 1c.

### Stage 1 ownership check

| File | Owner |
|---|---|
| `ShipDriveLever.*`, `ShipFlightSurface.*`, `SkyProjection.*`, `SkyProjectionTest.cpp` | 1a |
| `SystemMapLayout.*`, `SystemMapWidget.*`, `SystemMapView.*`, `ShipMapScreen.*`, `NavText.*`, `UniverseSubsystem.*`, their tests, the eyes test | 1b |
| the six Python tools, `ShipTags.h` | 1c |

No file twice.

## Stage 2: the flight state, the levers, the words

### 2a. Flight track A

**Owns:** `Ship/ShipFlightState.h/.cpp`, `Ship/ShipSubsystem.h/.cpp`,
`Player/DeepSpaceCharacter.h/.cpp`, `Ship/ShipHumComponent.cpp`,
`Tools/setup_flight_input.py`, `Tools/sky_probe.py`, the new
`Content/Input/Actions/IA_LeverUp`, `IA_LeverDown`, `IA_Stop`,
`IA_CycleTarget` and `IMC_Default`, `BP_DeepSpaceCharacter`; tests
`ShipFlightStateTest.cpp`, `ShipDriveTest.cpp`, `FlightInputTest.cpp`,
`SliceLoopTest.cpp`, `ShipJumpTest.cpp`, `NavStartTest.cpp` (the floor
rename), `HumComponentTest.cpp`.

**Builds:** 0.3 first (the flight state's new public surface: moved here from
Track 0, because nothing compiles against it before this stage makes it
real), then A1-A6: the eased drive, the ray cap on every surface, the
spool-down, cruise under the cap, `FloorFor`, the helm input, X, the fold's
all stop, **`JumpTo` at rest**, `ds.Drive.Top` default 1 c and never above,
and the hum's lever term. **Also `IA_CycleTarget` on Tab** and the
`CycleTargetAction` binding with an empty `CycleTarget()`, for the map spec's
decision 13, moved here from stage 4 so the Blueprint is recompiled and saved
once for all four new actions. Then `check_blueprints.py`.

**Tests:** as the flight-feel spec's table, at 1 c.

### 2b. Words and marker arithmetic (the map spec's track 2, less `ShipSky` and `sky_probe`)

**Owns:** `UI/NavText.h/.cpp` (`TargetBearing`, `Duration`, `Jump(EJumpState,
bool bInSystem)`), `UI/TargetMarker.h/.cpp` (new), `Tests/TargetMarkerTest.cpp`
(new), `Tests/NightSideTest.cpp` (new), `Tests/NavTextTest.cpp`.

**Builds:** `TargetMarker::View` (with the velocity, floor, braking and hold
it needs for the ETA), `Line` (with `ETA ...` and `PASSING ... UP`),
`ProgradeShipLocal`, `Place`, `SeenThroughGlass` on `ShipTags::Glass`. It
calls stage 1's `RayToFloor` and `SecondsToFloor`; every input is a
parameter, so nothing here waits on 2a.

**Tests:** `DeepSpace.UI.TargetMarker.View`, `.Bearing`, `.Place`, `.Eta`,
`DeepSpace.Ship.TargetSeenThroughGlass`, `DeepSpace.Sky.NightSideIsDrawn`,
`DeepSpace.UI.NavText`. `.Eta`'s case that steps `FShipFlightState` needs
2a's flight state: it is written here against the stage-1 law and gains the
stepped half in stage 3 (3c owns `TargetMarkerTest.cpp` then).

### Stage 2 ownership check

| File | Owner |
|---|---|
| `ShipFlightState.*`, `ShipSubsystem.*`, `DeepSpaceCharacter.*`, `ShipHumComponent.cpp`, `setup_flight_input.py`, `sky_probe.py`, the input assets, the Blueprint, their tests | 2a |
| `NavText.*`, `TargetMarker.*`, `TargetMarkerTest.cpp`, `NightSideTest.cpp`, `NavTextTest.cpp` | 2b |

No file twice. (`NavText.*` was 1b's in stage 1; a later stage may own it
again.)

## Stage 3: the HUD corner, the dust and the star, the target and the in-system jump

### 3a. Flight track B

**Owns:** `UI/ShipHUDWidget.h/.cpp`, `Tests/ShipHUDAltitudeTest.cpp`,
`Tests/ShipHUDSpeedTest.cpp` (new), and, for the `JumpLine` rename's calls,
`Tests/SliceLoopTest.cpp` and `Tests/ShipHUDBearingTest.cpp`.

**Builds:** `SpeedWords` to `1 C`, `MotionLine`, `AltitudeLine`, the
`DriveLine` to `JumpLine` rename. No time in this corner.

### 3b. Flight track C

**Owns:** `Ship/ShipCounterFrame.h/.cpp`, `Sky/SkyProjection.cpp` (the warmth
term), `Sky/ShipSky.cpp` (`StarSurface`, the temporary `StarWarmthGamma`),
`Tests/ShipCounterFrameTest.cpp`, `Tests/ShipCounterFrameJumpTest.cpp`,
`Tests/SkyProjectionTest.cpp`, `Tests/Eyes/StarGlareEyesTest.cpp` (new,
temporary).

**Builds:** the dust at a seen speed to 1 c, the streaks in field space, the
honest warmth with the ceiling; `Eyes.StarGlare` run once, frames to the
developer. The course marker needs no change for the in-system jump: it
follows `GetCourseDirection`, which 3c points at the world.

### 3c. Target state and the in-system jump (the map spec's track 1 and decision 12)

**Owns:** `Ship/ShipNavState.h/.cpp`, `Ship/ShipSubsystem.h/.cpp`,
`Ship/NavStart.h/.cpp`, `UI/NavigationWidget.h/.cpp`,
`UI/SystemMapWidget.h/.cpp`, `docs/decisions/0003-ship-state-as-subsystem.md`;
tests `Tests/ShipTargetTest.cpp` (new), `Tests/InSystemJumpTest.cpp` (new),
`Tests/ShipNavStateTest.cpp`, `Tests/NavStartTest.cpp`,
`Tests/SystemMapScreenTest.cpp`, `Tests/TargetMarkerTest.cpp` (the stepped
ETA case), and the eyes test's second run.

**Builds:** the target on `FShipNavState` and the subsystem; `CycleTarget`
and `NextTarget`; the world course (`PlotWorld`, the reach, letting go with
the target), `ArrivedAtWorld`, `NavStart::WorldArrivalPoint` and
`WorldStandoffCm`, `GetCourseDirection` for a world, `GetStandoffAU`,
`ds.Nav.Target`, `ds.Nav.Plot target`, `ds.Nav.WorldStandoffDeg`; the fold
keeping the target for a world course; the header comment in
`ShipNavState.h` scoped to the charge. **The chart** shows an in-system
course (this file was in no stage's list; it goes here, where nothing else
owns it). **The map** is wired to all of it: `SelectWorld`'s body, the target
ring, the band's target line and `Jump here`, `GetStandoffAU` in its scale
and key, `In the fold.`. Then `Eyes.MapFromHelm` again, with a target, and
the test file deleted with the verdict.

**Tests:** `DeepSpace.Ship.Target`, `DeepSpace.Ship.InSystemJump`,
`DeepSpace.Loop.InSystemJump`, `DeepSpace.UI.NavigationScreen.InSystemCourse`,
`DeepSpace.UI.SystemMapScreen`'s target cases, `.Eta`'s stepped case.

### Stage 3 ownership check

| File | Owner |
|---|---|
| `ShipHUDWidget.*`, `ShipHUDAltitudeTest.cpp`, `ShipHUDSpeedTest.cpp`, `SliceLoopTest.cpp`, `ShipHUDBearingTest.cpp` | 3a |
| `ShipCounterFrame.*`, `SkyProjection.cpp`, `ShipSky.cpp`, the counter-frame and projection tests, `Eyes.StarGlare` | 3b |
| `ShipNavState.*`, `ShipSubsystem.*`, `NavStart.*`, `NavigationWidget.*`, `SystemMapWidget.*`, ADR 0003, `ShipTargetTest.cpp`, `InSystemJumpTest.cpp`, `ShipNavStateTest.cpp`, `NavStartTest.cpp`, `SystemMapScreenTest.cpp`, `TargetMarkerTest.cpp`, `Eyes.MapFromHelm` | 3c |

No file twice. The first draft of this order had a collision here: the map
spec put its `Loop.Jump` additions in `SliceLoopTest.cpp`, which 3a owns this
stage for the rename. They moved to the new `InSystemJumpTest.cpp`.

## Stage 4: the seats, the pointer, the HUD overlay and its ETA

### 4a. Seats and pointer (the map spec's track 4 and decision 13)

**Owns:** `Ship/ShipScreen.h/.cpp` (`IsDrivableSeated`, `ZoomsOnSit`,
`IsZoomableFromChartChair`), `Ship/ShipNavScreen.h/.cpp`,
`Ship/ShipMapScreen.h/.cpp` (its overrides), `Player/DeepSpaceCharacter.h/.cpp`
(the seated pointer gate from either chair, `ZoomedScreen`, E's sit, zoom and
back and their prompts, `CycleTarget()`'s body); tests
`Tests/MapFromHelmTest.cpp` (new), `Tests/ChartChairTest.cpp` (new),
`Tests/ScreenReachableTest.cpp`, `Tests/NavScreenTest.cpp`,
`Tests/ScreenUseTest.cpp`, `Tests/ScreenFramingTest.cpp`,
`Tests/ScreenStandUpTest.cpp`, `Tests/ScreenPointerTest.cpp`, and the
`DeepSpace.Ship.MapScreen` virtual cases in `Tests/SystemMapScreenTest.cpp`.

**Runs:** the virtuals are not reflected and `ZoomedScreen` is a
`TWeakObjectPtr`, so no Blueprint layout changes; `check_blueprints.py` as
routine after the header change. No component changes on placed actors, so
no level rebuild.

### 4b. HUD target overlay and ETA (the map spec's track 5, with its `ShipSky` and `sky_probe` edits)

**Owns:** `UI/ShipTargetOverlay.h/.cpp` (new), `UI/ShipHUDWidget.h/.cpp` (the
overlay child, the target readout with its ETA, `ShowsNoseCaret` with a
target, the jump line naming a world and saying `IN THE FOLD`),
`Sky/ShipSky.h/.cpp` (`ds.Sky.Goto ... night`), `Tools/sky_probe.py`
(`--night`: moved here from stage 2, where 2a owns the file); tests
`Tests/TargetOverlayTest.cpp` (new), `Tests/ShipHUDNoseCaretTest.cpp`,
`Tests/ShipHUDBearingTest.cpp` (the jump line for a world course and `IN THE
FOLD`), `Tests/ShipScreensAgreeTest.cpp`, `Tests/ShipSkyTest.cpp`.

### Stage 4 ownership check

| File | Owner |
|---|---|
| `ShipScreen.*`, `ShipNavScreen.*`, `ShipMapScreen.*`, `DeepSpaceCharacter.*`, the screen and seat tests, `SystemMapScreenTest.cpp` | 4a |
| `ShipTargetOverlay.*`, `ShipHUDWidget.*`, `ShipSky.*`, `sky_probe.py`, the overlay, caret, bearing, screens-agree and sky tests | 4b |

No file twice.

## What moved, and why

Each move keeps one owner per file per stage, or puts work after what it
depends on.

1. **Flight Track 0's 0.3** (the flight state's new public surface) moved
   from stage 1 into 2a. Revision 2 declared it early with trivial bodies so
   Tracks B and C could compile before A; B and C are now a stage after A.
2. **Flight Track 0's 0.4** (`RenderedFloor`) stays in stage 1 in 1a, which
   then owns `SkyProjection.*` there; 3b edits `SkyProjection.cpp` a stage
   later.
3. **`UUniverseSubsystem::GetSystemIdAt`** moved from stage 3 (the target
   track) to 1b: the map's cache key needs it, and the map lands in stage 1.
   The stage-3 target track no longer touches `UniverseSubsystem.*`.
4. **`NavText::WorldKind` and `WorldName`** moved from stage 2 to 1b: the
   map's rows print them in stage 1. 2b owns the rest of `NavText` in stage 2.
5. **The map widget's target wiring** (`SelectWorld`'s body, the target ring,
   the band, `GetStandoffAU`) moved from stage 1 to 3c: the target it shows is
   made in stage 3. 1b builds the widget with the seam empty.
6. **`ShipTags::Glass`** moved out of `TargetMarker` into a new
   `Ship/ShipTags.h`, owned by 1c: `test_placement.py` holds the Python tag
   equal to the C++ one, and runs in stage 1.
7. **`IA_CycleTarget`** and its binding moved from stage 4 to 2a:
   `setup_flight_input.py` and the Blueprint are 2a's, and one recompile of
   `BP_DeepSpaceCharacter` for all the new input beats two. 4a fills the
   handler.
8. **`sky_probe.py --night`** and **`ds.Sky.Goto ... night`** moved to 4b:
   2a owns `sky_probe.py` in stage 2 and 3b owns `ShipSky.cpp` in stage 3.
9. **`DeepSpace.Loop.Jump`'s target case and the in-system loop** moved out
   of `SliceLoopTest.cpp` (3a's in stage 3) into `InSystemJumpTest.cpp`
   (3c's).
10. **`DeepSpace.UI.TargetOverlay`** moved out of `TargetMarkerTest.cpp`
    (2b's, then 3c's) into `TargetOverlayTest.cpp` (4b's), since the overlay
    is stage 4's.
11. **`UI/NavigationWidget.*`** was in no stage's list; the chart's in-system
    course line puts it in 3c.
12. **The map's seat virtuals** are 4a's, so `AShipMapScreen`'s overrides wait
    for stage 4 even though the class is stage 1's: a class cannot override a
    virtual that does not exist yet.

## After stage 4

- **Verdicts the developer gives:** `Eyes.StarGlare` (the warmth law and
  `StarSurface`, then the test and its CVar deleted), the dust knee's three
  questions, and the first playtest of the whole: the lever to 1 c, the cap,
  the ETA, the in-system jump, the chart chair's choice and Tab.
- **CLAUDE.md**, after the merges: the flight-feel spec's *Documentation*
  list, and the map spec's *The system map* section (the screen, the seated
  pointer, the chart chair and Tab, the in-system jump, the live ETA, the two
  bearings, the glass tag), its tunables, and `ds.Nav.Target` and
  `ds.Nav.Plot target` in *Playtest console*. The countdown sentence was
  scoped to the jump's charge already, with the specs' amendment.
- **Open, and not built:** the jump cooldown (the map spec's *Open
  questions*).
