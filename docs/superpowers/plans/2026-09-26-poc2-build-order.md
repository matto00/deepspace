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

*As built:* until a source file outside `Tests/` names `AShipMapScreen`, the
map's width and draw-size tests report **PEND**, and `./test.sh` names every
pending test rather than counting it as passed; they become real checks, and
fail on a class that sets no `PanelWidthCm` or `DrawSizePixels`, the moment
1b's class is in the tree. The review of 1c found `PILOT_EYE` was a standing
eye; 1c therefore also touches `SkyTestWorld.h` (`PilotEye` is now the
measured seated eye, with `HelmSeat` and `PilotEyeBob`),
`FirstPersonBodyTest.cpp` (`DeepSpace.Player.SeatedEyeIsPilotEye`, which
measures it) and `Tools/props.py` (the port desk screen lowered so the nose
line from that eye meets the glass). The level rebuild that applies the
lowered screen is the same one owed after 1b.

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

**What 2a had to touch outside its lane.** Track A removed `DriveFloor`,
`AwayFromSurface` and `GetDriveRoom`, which the HUD read, so it edited
`UI/ShipHUDWidget.h/.cpp` and `Tests/ShipHUDAltitudeTest.cpp` (3a's in stage
3) and `Tests/ShipCounterFrameTest.cpp` (3b's) to keep them compiling: the
removals only, no new HUD behaviour. 3a and 3b therefore branch from the
integrated stage-2 result, never from stage 1, and own those files from
there. **A build of stage 2 alone is not for a playtest:** until 3a's motion
line lands, the HUD shows neither lever's notch nor SPOOLING DOWN, so the
pilot cannot read the lever they are moving.

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
The stepped case is the only check that the ETA and the soft cap agree, so
it must not be lost here: at 1 c from 0.2 AU, stepped through
`FShipFlightState`, the ETA falls one second a second to within 0.5 s; and a
stepped cruise slide, nose and velocity apart, pins that the ETA follows the
velocity's ray as it swings (2b pins the unstepped half).

*As built (3c):* the API the map coded against in stage 1 is
`UShipSubsystem::SetTarget`, `ClearTarget`, `GetTarget`, `ShipNav::TargetPlanet`
and `GetStandoffAU`; 3c added `CycleTarget`, `PlotTarget` (the in-system
course, which is only ever the target), `GetPlottedWorld`, `HasCourse`,
`IsNearEnoughToFly(Here, World)`, `GetWorldStandoffDeg`, and
`GetTargetView(Here)`, which fills `TargetMarker::View` with the ship's own
position, attitude, velocity, the world's `FloorFor`, the boosters' braking
and `ds.Drive.HoldSeconds` -- so **4b's HUD line should print
`TargetMarker::Line(*Ship->GetTargetView(Here))`**, as the map does, and
`ScreensAgree` then holds by construction. Four things differ from the map
spec as written:

- **The chart's test is `DeepSpace.UI.ChartInSystemCourse`**, not
  `DeepSpace.UI.NavigationScreen.InSystemCourse`: a child path would turn the
  chart's own `DeepSpace.UI.NavigationScreen` into a group that silently runs
  nothing. It lives in `InSystemJumpTest.cpp` with the other two.
- **The in-system arrival is on the line from where the fold *opened***,
  held for the fold as `UShipSubsystem::FoldDeparture` (ADR 0003's new
  amendment), not from where the ship is when it ends: the ship coasts on
  through the fold, about 130,000 km in half of it from 1 c, which moves an
  arrival worked out at the end off the line by hundreds of kilometres when
  the nose was a few degrees off the world's centre.
- **The spec's "a body of 100 km radius (where 10 x Floor governs)" is an
  arithmetic slip.** `R / sin(1 degree)` is 57.3 R, so the ten floors govern
  only under about 1.8 km of radius over a 10 km floor; a 100 km body is met
  5,700 km out, by the angle. `DeepSpace.Ship.InSystemJump` tests both a
  100 km body and a 1 km one.
- **Through an in-system fold the map draws the ship where it is**, not
  where it left from: it coasts about 0.001 AU, well under a pixel of the
  warped-log map, so nothing is held for the glyph. The footer says `In the
  fold.` and the drawing stands.

**The ETA the map prints is tested where the ship builds it.** A stepped
case in `DeepSpace.Ship.Target` flies the stock ship with `Tick` at 1 c from
0.25 AU onto a world, on fed boosters and again browned out (a quarter of the
braking), and holds `GetTargetView(Here)->EtaSeconds` to the flown arrival at
the floor within 0.5 s all the way down (0.07 s and 0.18 s off as run). So
the map's line and 4b's HUD line printing the same view agree by
construction, and the view itself is what is tested.

**Stage 3 alone is not a playtest build for the in-system jump.** The HUD is
4b's, and until 4b lands a pilot who presses `Jump here` at the helm sees
almost nothing of it there: `DriveLineText` reads only `GetPlottedSystem()`
and is blank on a world course, `ShowsNoseCaret` needs a star course so the
caret never shows, and **`PlaceLine` prints `NavText::Jump(Transit)`,
`BETWEEN STARS`, in an in-system fold** -- the words decision 12 says it must
not use. The chart and the map say the right things; the helm's glass does
not. 4b's list below names all three.

**A world course adds per-frame procgen, not yet measured.**
`LetGoOfNearWorldCourse` runs every tick and generates the system to fix the
world; `GetCourseDirection` generates it again for each caller (the nav step,
the chart, the counter-frame, 4b's HUD); and the map's band fixes the world
twice a frame (`GetTargetView`, `IsNearEnoughToFly`). Star courses already
did the same. Check the frame time with a world course plotted at the first
playtest; if it shows, let the band's two share one `FixWorld` a frame and
hold the fixed world for the tick.

`Eyes.MapFromHelm` was run again with a target, two new frames:
`home_target_ahead` (the target ring, `› Baemsekai III · dead ahead · 46
THOUSAND KM · ETA 16 MIN` while the drive spools up, and `Near enough to fly`,
disabled) and `home_target_far` (`Jump here`). In the implementer's reading
the ring, the line and the button all read at the helm's pixels; the
button's raised backdrop is barely distinguishable from the panel, and the
disabled words are dim by design. **The verdict is the developer's**, so the
file is kept until it is given, and deleted with it.

**The footer shares its row with that button, and now wraps short of it**
(`USystemMapWidget::JumpReserve`, 180 px: the widest button, `Near enough to
fly`, measured by Slate at 168 px, and a gap). `DeepSpace.UI.SystemMapScreen`
measures both under `-nullrhi` -- fonts are measured without a renderer --
and a third frame, `home_footer_longest`, shows the longest footer the map
writes (`Inside Baemsekai I's orbit. 34° above the plane.`, 368 px of the
408 it has) beside `Jump here`, on one line and clear of the button.

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

*As built (4a):* six things differ from the map spec as written.

- **Seated, the pointer is handed the gate's hit** (`EWidgetInteractionSource::Custom`
  and `SetCustomHitResult`), which decision 2 rejected on the premise that
  the two traces were the same. They are not: the pointer's own trace
  ignores only its pawn, and the helm's seated eye is *inside*
  `APilotSeat`'s reach box (40 x 35 x 65 cm about the seat, 0 to 130 cm up),
  so from the helm it met the seat and never the map. The gate's trace
  ignores the chair the body is in, and handing it over keeps the gate and
  the pointer one trace. `DeepSpace.Ship.MapFromHelm` fails without either.
  Decision 2 of the map spec is amended to say so, and flagged for the
  developer.
- **The button is let go before the pointer goes off.** The release goes
  through the pointer's virtual Slate user, which deactivating unregisters,
  so the old order (off, then release) dropped it and left the key held
  inside the component; its next press was swallowed as a repeat. With the
  gate this happens whenever the view slides off the map mid-press.
- **A screen's seat puts the capsule on the floor under it**, as the helm
  does, not on the cushion (`GetUseFloorZ`, not the use transform's height).
  The sitting idle lifts the hips itself; on the cushion the chart chair's
  eye was 1.8 m up. Nobody saw it while sitting always framed the screen and
  hid the body. `DeepSpace.Ship.ChartChair` holds the chair's seated eye to
  the helm's height. **`SeatHeightCm` is retired** with it: once the body sat
  on the floor under the seat it moved nothing, and a tunable that does
  nothing is a trap. It is gone from `AShipScreen`, `build_hauler.py`,
  `verify_level.py` and CLAUDE.md; the use transform is on the floor, and
  the cushion heights the stand-up test climbs are the props', in that test.
- **The chair's first view is aimed from the seated eye**, not the current
  one. E runs from input, before the frame's animation, so the head is still
  where the standing pose left it; aimed from there the view kept a standing
  pitch while the eye sank 40 cm, ended under the chart, and the next E
  stood the player up. `ADeepSpaceCharacter::SeatedEyeOffset` (19, -2, 125 cm
  from the seat's floor anchor) is the measured eye, held by
  `SeatedEyeIsPilotEye` and, as `SEATED_EYE`, by `test_placement.py`.
  `DeepSpace.Ship.ChartChair` sits a standing body and lets it settle.
- **The map's sightline is validated from the chart chair too**
  (`CHART_EYE`, `check_map_sightline`), since the chair clicks and zooms it.
- **`IsUsingScreen()` now means a screen is framed** (the laptop, or a zoom
  at the chart chair), which is what the HUD hides its dot for, and
  `IsInScreenChair()` says the body is in a screen's seat. So the HUD's dot
  shows in the chart chair unzoomed, where it is what the player aims with,
  with no edit to `ShipHUDWidget.cpp`. E's prompt reads `Chart`, `Map`,
  `Back` or `Stand up` (`AShipScreen::GetZoomPrompt`, a fourth virtual), and
  the HUD prints it as `(E)  Chart`.

### 4b. HUD target overlay and ETA (the map spec's track 5, with its `ShipSky` and `sky_probe` edits)

**Owns:** `UI/ShipTargetOverlay.h/.cpp` (new), `UI/ShipHUDWidget.h/.cpp` (the
overlay child, the target readout with its ETA, `ShowsNoseCaret` with a
target *or a world course* (it needs `GetPlottedSystem()` today), the jump
line naming a world and saying `IN THE FOLD` (`DriveLineText`, blank on a
world course today), and **`PlaceLine`, which in an in-system fold says `IN
THE FOLD` or nothing, never `BETWEEN STARS`** -- it prints
`NavText::Jump(EJumpState::Transit)` for any fold today),
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
  the ETA, the in-system jump, the chart chair's choice and Tab. Two checks
  in that playtest no headless test can make, named so they are not missed:
  - **Tab on the zoomed map, pressed on the keyboard.** It reaches the pawn
    through `IMC_Default`'s Tab mapping (`IA_CycleTarget`, Started), and
    only while the input mode is game-and-UI with a cursor -- where Slate's
    Tab focus navigation could take the key first (the map spec's *Risks*).
    `DeepSpace.Ship.ChartChair` calls `CycleTarget` directly. If Tab does
    nothing on the zoomed map, the spec's fallback applies.
  - **The dot in the chart chair.** Looking at the chart unzoomed the dot
    stays idle (the chart is not drivable seated, so the pointer is off),
    and only the prompt says `(E) Chart`; looking at the map the dot turns
    teal. The chair's two choices look different and behave the same. Whether
    that reads as "the chart is not a choice" is the playtest's call; the
    change, if wanted, is for the HUD to emphasise the dot whenever E's
    prompt names a zoom.
- **CLAUDE.md**, after the merges: the flight-feel spec's *Documentation*
  list, and the map spec's *The system map* section (the screen, the seated
  pointer, the chart chair and Tab, the in-system jump, the live ETA, the two
  bearings, the glass tag), its tunables, and `ds.Nav.Target` and
  `ds.Nav.Plot target` in *Playtest console*. The countdown sentence was
  scoped to the jump's charge already, with the specs' amendment.
- **Open, and not built:** the jump cooldown (the map spec's *Open
  questions*).

*As integrated, 2026-09-27:* every level and asset script was run on the
merge (`setup_flight_input`, `setup_pointer_input`, `setup_sky_materials`,
`validate_hauler`, `build_hauler`, `verify_level` -- PASS within 1.0 cm --
and `check_blueprints`, 5 of 5). `DeepSpace.Playtest.*`
(`Tests/PlaytestTest.cpp`) flies the playtest's steps end to end, each proved
able to fail with `Tools/mutate.sh`. One of them, `KeysTurnTheShip`, found
the attitude keys scrambled since the first flight: `AttitudeRate` turns
about X roll, Y pitch, Z yaw, the comments said X pitch, Y yaw, Z roll, and
the mapping followed the comments, so W rolled, D pitched and Z yawed. The
mapping and the comments are fixed; the 0.3 rad/s meant for roll stays on
yaw, for the playtest to judge. A temporary render check (not committed)
framed the in-system arrival from the helm's eye with the bracket and the
map, the .03 AU night side, and the glare at a red dwarf's and a Sun-like
star's arrival; the frames went to the developer with the playtest script.
