# DeepSpace — Ship Wear and Upgrades

**Date:** 2026-09-27
**Status:** Approved by the developer, 2026-09-27: every item of
*Decisions needing sign-off* as recommended. Slice 1 (the upgrade seam)
implemented per `docs/superpowers/plans/2026-09-27-wear-slice-1-upgrade-seam.md`;
slices 2-4 not implemented.
**Answers:** the developer's rulings on wear and upgrades, recorded below
**Follows:** the interactable ship and power (implemented; its decision 2,
"under-powered systems degrade, they never fail", is the law every deficit
here obeys), the lived-in ship (implemented; its decision 11 made the reactor
readout a nameplate that "changes only if a module changes the reactor",
and its decision 12 gave the ship the faded and replaced materials this
spec reuses), navigation and arrival (implemented; "making range an upgrade
means a different number, not a different model"), flight feel
(implemented; its fake "the ship can park one stellar radius above a star
with nothing said. Cost: whatever the ship's wear model eventually says"),
landing (approved, not built; its starved sink is the precedent for the
amendment in decision 15)
**Depends on:** nothing unmerged for slice 1, which goes before landing
(b) and amends its plan. Slices 2-4 go after landing (c) (*Seams with work
in flight*).
**Builds on:** ADR 0002 (logic in C++, Blueprints only assign assets), ADR
0003 (ship state as a subsystem; amended here), ADR 0006 (generation is C++
and runs at start-up), ADR 0007 (hierarchical seeds), ADR 0008 (shape the
prior)
**Governed by:** `docs/vision.md`: *the anti-chore principle* (all four of
its canonical cases), *how ships wear* and *the boundary this sits on*,
*how social the game is* (shared presence, never division of labour; seeds
world-level, not player-level), *what this is not* (not a factory or
logistics game); CLAUDE.md: *Screens, the pointer, and power*, *The drive
and the jump*, *The dressing*, *Randomness*

## The developer's rulings, 2026-09-27

These are binding. Everything below is built on them; where a decision goes
past them, it says so and is on the sign-off list.

1. **The first slice is the upgrade seam.** Modules gain effects: a bay,
   and rated values. The reactor becomes a module (`DA_Reactor_Stock`,
   1400 W). The clash between the `DA_Lights` draw and the
   `ShipPower::Lights` consumer is resolved. **The stock ship keeps exactly
   today's numbers**: 1400 W supply, 1370 W at rest, a 45 s charge, 12 ly.
   Two example upgrades (for instance a reactor at 1800 W and a nav array
   at 20 ly) are installable by console command and shown as nameplates on
   the engineering console: name, rating, words; **never a percentage, a
   tier or a comparison**. Headless-testable. Later slices: (2) physical
   bays and the install act, (3) persistence, (4) wear events.
2. **An upgrade is numbers that open things up.** It rewrites rated values
   so as to widen what the player can do. Each bay has a few distinct parts,
   not a ladder. **No screen compares a part with a better one.**
3. **Slots: fixed named bays** (reactor, drive, boosters, lights, life
   support, sensors/nav) **plus a couple of open auxiliary slots** for
   optional extras. Chosen knowingly over the warning that aux slots under
   a shared reactor invite a best combination. This spec keeps the choice
   and mitigates the risk, and the mitigation is on the sign-off list.
4. **The install act (slice 2):** walk to the bay's housing in its room and
   press E; a camera cut, as sitting down is; anyone aboard can do it; no
   bay is anyone's station.
5. **The wear clock:** seeded history at world start for the early game;
   afterwards a part ages **per jump and per stress the player chose** (for
   instance parking within a stellar radius of a star); Weibull (shape k
   about 3, scale about 150 jumps, illustrative). **Cruising, idling,
   landed and parked never age anything.** Symptoms arrive as discrete
   events, never as erosion. This needs a deliberate amendment to "nothing
   in the model changes on its own with time" (CLAUDE.md,
   `ShipPowerState.h`), recorded as the landing spec recorded its starved
   sink.
6. **The wear effect is also a small deficit with a floor** (chosen over
   "different, not worse"): a worn part is measurably worse (for instance a
   booster eases at 0.9 of its rate), never below a floor, and never
   touching a top speed (the landing and drive rulings). The spec checks
   this against the jump-charge rule (no charge display; a longer charge is
   a longer wait the game imposes) and the lived-in spec's "quiet optimum"
   warning, keeps the ruling, mitigates it (the free, quick repair below),
   and puts the specific deficits on the sign-off list. Symptoms also have
   character: a look, a sound, a quirk.
7. **Repair is free**, by hand, at the part: hold E for a few seconds. It
   clears the symptom, and the housing takes the dressing's *replaced*
   material. Upgrading the part also clears it. Anyone aboard can do it.
8. **Sourcing, for now, is console and debug only** (`ds.Ship.Install`,
   `ds.Ship.Spares`) until landing exists. Later: discrete named parts,
   never ore counts.
9. **Saving is its own slice, after the install act**: a minimal
   `USaveGame` for the ship (flight state and course, the part in each bay,
   spares, symptoms and ages, weights, the lights switch), world-level and
   not player-level. Module state is kept plain and serialisable from slice
   1.

## Context

### What exists, and what does not

- **A module is a power draw with a name.** `UShipModuleDataAsset`
  (`Ship/ShipModuleDataAsset.h`) is a `UPrimaryDataAsset` -- an Unreal
  asset that holds data and no logic, saved as a binary `.uasset` -- with
  `ModuleId`, `DisplayName`, `PowerDraw` and an unread `Mesh`. Its header
  says it exists so that "upgrades and procedural generation should later
  write data, not code".
- **Three modules, all draws, none with an effect.** `DA_LifeSupport`,
  `DA_Lights` (`ModuleId "Lights"`, "Interior Lighting") and `DA_Sensors`
  in `Content/Ship/Modules/`, 620 W together. Nothing reads that life
  support or sensors are installed. They were made by hand in the editor;
  no script authors them.
- **The lights are booked twice.** `DA_Lights` is a draw off the top, and
  separately `ShipPower::Lights` (`"Power.Lights"`, also an actor tag in
  the level) is a consumer with a 300 W want. Two things are called
  Lights; one of them is invisible to the allocation screen.
- **The loadout.** `ADeepSpaceGameMode::StartingModules` holds soft
  pointers filled in the C++ constructor and overridable in
  `BP_DeepSpaceGameMode`; `BeginPlay` installs each through
  `UShipSubsystem::InstallModule`, which calls `FShipPowerState::AddDraw`
  keyed on `ModuleId` and refuses a duplicate id. Nothing stops ten sensors
  under ten ids, or a ship with none. `Tests/StockShip.h` installs the
  *Blueprint's* list, because three times a claim about the split held in a
  bare test world and not in play.
- **Every rated value is a constant or a CVar.** `DefaultReactorOutput`
  (1400 W, "Becomes a module later"), `LightsWant` (300), `BoostersWant`
  (450) are `static constexpr` in `ShipSubsystem.h`; `FShipFlightLimits::
  Cruise()` supplies 2 km/s^2 and the ruled tops (cruise 20 km/s, drive
  0.1 c); `ds.Nav.ChargeSeconds` (45, settled), `ds.Nav.WindingWant` (380,
  settled), `ds.Nav.RangeLy` (12) and `ds.Drive.Response` (3) are
  `TAutoConsoleVariable`s -- Unreal console variables, read at use. At rest
  the stock ship asks 620 + 300 + 450 = 1370 W of 1400 W: whole at rest,
  the 2026-09-26 ruling that `DeepSpace.Ship.JumpCanWindAtFullSpeed` holds.
- **The engineering console** (`AShipConsole`, `UEngineeringConsoleWidget`)
  shows a lights toggle and a three-line readout,
  `REACTOR {0} W` / `DRAWN {1} W` / `SPARE {2} W`
  (`UEngineeringConsoleWidget::GetReadoutText`, the rating, `GetPowerDraw()`
  and `GetPowerHeadroom()`). `AShipConsole::GetReadout`, a `BlueprintPure`
  no C++ calls, carries an older form of the same, `DRAW {0} W / {1} W` /
  `SPARE {2} W`. Both show a total drawn next to the rating, which the
  lived-in spec's decision 11 ruled out ("Nothing anywhere in the ship shows
  a total drawn"; `1000 W out · 740 W drawn` rejected as "a utilisation
  meter"). The readout predates that ruling and was never brought into line
  (decision 9 does it). `DeepSpace.Ship.ScreensAgree` asserts the readout
  changes when the lights are toggled. No screen lists installed modules;
  `GetInstalledModules` is read only by tests.
- **The dressing's wear is a snapshot.** One Beta(5, 2) draw per furniture
  piece at world start, bucketed into standard, faded (about 11%) and
  replaced (about 11%), painted with `MI_Ship_furniture_faded` and
  `_replaced`. `UShipDressingSubsystem` never ticks, on purpose; its rules
  say Weibull "is wear over time, and the dressing is a snapshot".
- **Nothing is saved.** No `USaveGame` exists. Every session starts at the
  opening placement with the stock loadout, default weights and the lights
  on.
- **Nothing in the ship model changes with time**, except the landing
  spec's starved sink (approved, not built), recorded there as "the one
  sanctioned exception".

### What the vision asks of this, in its own words

Early: "degradation is visible almost immediately, because it is how the
ship tells you it has a history ... also how the player learns that
components can be improved at all." Mid: "effectively absent. Players
upgrade components long before they wear out." Late: "wear starts to
surface again on its own ... a mild retaining pressure". And the boundary:
"decay may be slow enough to be an occasional event, and never fast enough
to be a schedule ... A chore is not made acceptable by being rare; it is
made acceptable by the player choosing when to do it."

The design below gets that curve from structure rather than tuning: stock
parts start old (seeded history), new parts start young (a fresh Weibull
life, so upgrades outpace wear), and when upgrades stop, ages accumulate
again. No part of it is scheduled against time.

## Goals

- A part is a module in a bay, and it changes a number. An upgrade is a
  different number, never a different model.
- The stock ship is bit-for-bit today's ship: every existing test green on
  the same figures.
- No loadout has a right answer: every combination of parts is whole at
  rest, no part needs another, and no screen compares, totals or ranks
  parts.
- Wear the player finds, by ear and eye, at a place in the ship, and fixes
  for free when they choose -- never a rate, never a list, never a warning.
- State that is plain, world-level and serialisable from the first slice,
  so the save is a slice, not a rewrite.

## Non-goals

- Any economy: currency, trade, salvage yields, crafting, refining. Parts
  come from the console until landing and a sourcing spec exist.
- Combat damage. The vision makes it a separate path and an event; it
  shares nothing with the wear clock here (*Open questions*).
- Combat flight limits and assist-off as upgrades (the flight spec's
  "different set of numbers"): the boosters bay can carry them later; not
  in these slices.
- A jump cooldown. The system map spec lists "a wear cost carried by a
  later wear model" as one shape for it; this spec deliberately does not
  take that up (*Open questions*).
- Multiplayer replication. The state is shaped for it (world-level,
  derived from the root seed plus the ship's own events); nothing is
  replicated.
- New art. Housings are primitives with the ship's existing materials.

## Decisions

### 1. A part is a module in a bay: six fixed bays and two auxiliary slots

`EShipBay`, a `UENUM` (Unreal's reflected enum, visible to data assets and
the save): `None`, `Reactor`, `Drive`, `Boosters`, `Lights`, `LifeSupport`,
`Sensors`, `Aux1`, `Aux2`. **`None` is first on purpose**: a part whose
`Bay` was never set (a module made with `NewObject` in a test, or an asset
the script missed) reads `None`, and `FitPart` refuses a `None` part by
name, rather than defaulting it into the reactor bay and displacing the
reactor. Each bay holds exactly one part; a part declares the bay it fits
(`Aux` parts fit either auxiliary slot). "Module" and
"part" name the same thing: the class stays `UShipModuleDataAsset`, because
renaming a `UCLASS` strands every asset saved against the old name unless a
redirect is added to `DefaultEngine.ini`, and the rename buys nothing.

Fitting a part into an occupied bay **swaps**: the displaced part joins the
ship's spares, so every swap can be undone. The duplicate-id rule becomes
"one part per bay". The six core bays map one to one onto what the ship
already models: the reactor supplies, the drive is the `Engine` consumer
and the in-system drive's ease, the boosters are the `Boosters` consumer
and cruise's acceleration, the lights are the `Lights` consumer, life
support and sensors are today's two draws.

**Why fixed bays.** Free installation under a power budget is a knapsack
problem, and a knapsack has an optimum: the vision's "configuration to
converge on". Fixed bays with one part each leave nothing to pack. The aux
slots are the ruled exception, handled in decision 8.

**Rejected: bays by count** (two booster slots, three sensor slots). It
reopens packing inside a bay. **Rejected: free install** (Elite's model).
Listed so the rejection is recorded.

**Cost to change:** medium. Bays are saved keyed by the bay's name, never
by position (decision 11), so adding a bay anywhere in the enum later is
cheap; removing one needs a save migration.

### 2. What each bay rates, and the stock ship is today's ship

A part carries a **draw** (watts off the top, as today) and a small set of
**rated values**, each owned by exactly one bay:

| Bay | Rated values | Stock part | Stock numbers |
|---|---|---|---|
| Reactor | `ReactorWatts` | `DA_Reactor_Stock` (new) | 1400 W, no draw |
| Drive | `WindingWant`, `ChargeSeconds`, `DriveResponse` | `DA_Drive_Stock` (new) | 380 W, 45 s, 3 notches/s, no draw |
| Boosters | `BoostersWant`, `LinearAcceleration` | `DA_Boosters_Stock` (new) | 450 W, 2 km/s^2, no draw |
| Lights | `LightsWant` | `DA_Lights_Stock` (was `DA_Lights`) | 300 W, and today's `DA_Lights` draw |
| Life support | (none) | `DA_LifeSupport_Stock` (was `DA_LifeSupport`) | today's draw |
| Sensors / nav | `RangeLy` | `DA_Sensors_Stock` (was `DA_Sensors`) | 12 ly, and today's draw |
| Aux 1, Aux 2 | (none: verbs, decision 8) | empty | -- |

The three existing draws keep their values exactly (they sum to 620 W);
the authoring script reads them from the assets before it rewrites
anything, and fails if they do not sum to 620.

**No part rates a top speed.** Cruise's 20 km/s, the drive's 0.1 c and
astern's 200 m/s are ruled, and thrust already "never lowers a top"
(`FShipFlightLimits::DriveThrust`). A part that raised a top would make
the stock ship's top read as lacking; a part that lowered one would be the
vision's "73% of potential". Tops stay in `FShipFlightLimits::Cruise()`.

**Life support rates nothing and is never a consumer.** The power spec
refused it as a consumer because "the moment survival depends on an
allocation, the screen becomes a chore with a death timer attached". It
stays a draw and a place in the ship; its parts differ in draw and
character only.

**The pure core.** `Ship/ShipParts.h` (new, no `UObject`): `FShipPartSpec`
(id, bay, draw, a `TMap<EShipRating, double>` of the ratings it sets) and
`FShipRatings` with `static FShipRatings Stock()` -- the table's stock
numbers as named constants, moved here from `ShipSubsystem.h`
(`DefaultReactorOutput`, `LightsWant`, `BoostersWant`) and from the CVar
defaults. `ShipParts::RatingsOf(const TArray<FShipPartSpec>& Fitted)` starts
from `Stock()` and applies each fitted part's ratings. Pure, headless.

**`UShipSubsystem` derives, never stores, the rated values.** On every fit
it recomputes `FShipRatings` and pushes it into the power state
(`SetReactorOutput`, the consumer wants, `SetLightsOn` reading the rated
lights want rather than the constant); `ApplyAllocation` builds `Rated`
from `FShipFlightLimits::Cruise()` with `LinearAcceleration` and
`DriveResponse` from the ratings; the charge reads `ChargeSeconds` and
`WindingWant` from them; `GetChart` reads `RangeLy`.
`GetChartRangeLy` and `GetWindingWant` stop being `static` (they now
depend on the ship), a `GetChargeSeconds()` joins them, and
`UNavigationWidget::FAskedAt`, already keyed on the range, keeps working
unchanged. Every static caller moves to the instance in slice 1:
`ShipSubsystem.cpp:152` (the `ds.Nav.Near` log), `UI/NavigationWidget.cpp:355`,
`Ship/ShipHumComponent.cpp:90` (`EngineFeed`, which has the ship in hand),
`Tests/ShipJumpTest.cpp:234` and `Tests/JumpWindsTest.cpp:93,95`
(*Parallel tracks*, track S).

**Cost to change:** low for the numbers (data); medium for the bay-rating
ownership table, which the catalogue test enforces.

### 3. A bay is never empty in effect: an empty bay reads the stock part

A test world has no game mode and so fits nothing. Today such a ship runs on
the 1400 W constant with no draws. If the reactor became *only* a module, a
bare world's ship would have 0 W and every consumer would starve: a
failure state, and a hundred tests would move. So **an empty core bay reads
`FShipRatings::Stock()` for its ratings and draws nothing**. A bare test
world is exactly today's bare test world; a played ship fits the six stock
parts and gets exactly today's played numbers.

Removing a core part without a replacement is refused (`RemovePart` exists
only for aux slots); a swap is one call, so there is never a frame with no
reactor. A part's ratings never reach below stock by accident, because a
missing rating in a part means "stock", never zero.

**Rejected: the reactor rating left as a constant beside a reactor
module.** Two sources for one number. **Rejected: bare test worlds fit the
stock parts automatically.** It changes every bare-world test's draws at
once, which is exactly the class of drift `StockShip.h` exists to make
explicit.

**Cost to change:** low.

### 4. The lights clash: one part in the Lights bay owns both halves; draws are booked by bay

Today the string `Lights` is a draw (`DA_Lights`, `ModuleId "Lights"`) and
`Power.Lights` is a consumer. They are separate maps in `FShipPowerState`,
so nothing collides in code, but a person reads two Lights and cannot tell
which one the allocation screen shows.

Resolution: **the Lights bay holds one part, `DA_Lights_Stock`, which owns
both** -- a standing draw (the fittings, today's `DA_Lights` watts, off the
top) and the rated want of the `Power.Lights` consumer (300 W). Draws are
booked in `FShipPowerState` **by bay** (`ShipBay::DrawKey(EShipBay::Lights)
== "Bay.Lights"`), never by part id, so a swap is `RemoveDraw` and
`AddDraw` under one key and two parts in one bay cannot both draw. Part ids
become `<Bay>.<Name>` (`Lights.Stock`, `Reactor.Stock`, `Reactor.TwinCore`,
`Drive.QuickLever`); the authoring script rewrites the three existing
assets' `ModuleId`s. The consumer ids (`Power.*`) do not change: they are
tags on actors in the level, and renaming them needs a level rebuild for
nothing.

**Rejected: fold the `DA_Lights` draw into the consumer's want.** Tidier,
but it moves watts from off the top into the split, so the jump winds
differently at the stock loadout: ruling 1's "exactly today's numbers"
forbids it. It is on the sign-off list as the alternative, since a later
tidy-up may want it.

**Cost to change:** low.

### 5. The catalogue is a JSON contract; the data assets are authored from it

A `.uasset` is binary: not diffable, not reviewable, and a settled number
written into one is invisible to git. So the source of truth is
**`Tools/ship_parts.json`**: one row per part (id, bay, display name,
words, draw, ratings, and later its character, decision 17).
**`Tools/setup_ship_parts.py`**, run under the editor commandlet (the
editor run headless, with a Python script, as `setup_sky_materials.py` is),
creates or updates `Content/Ship/Parts/DA_*.uasset` from it, moves the
three existing assets there, and sets `BP_DeepSpaceGameMode`'s
`StartingModules` to the six stock parts -- the Blueprint's saved list
overrides the C++ constructor's, and `StockShip.h` reads the Blueprint, so
a stale Blueprint list would leave play on the old three modules while
every C++ default looked right.

**How the subsystem finds a part by id.** The script also writes one more
asset, **`DA_ShipCatalogue`** (a `UShipPartCatalogue`, a new
`UPrimaryDataAsset` subclass in `Ship/ShipPartCatalogue.h` holding
`TArray<TSoftObjectPtr<UShipModuleDataAsset>> Parts`, every row of the JSON
in catalogue order), and `UShipSubsystem` carries one ini line naming it:
`UPROPERTY(Config) FSoftObjectPath CatalogueAsset`, under
`[/Script/DeepSpace.ShipSubsystem]` in `DefaultGame.ini`, as the universe
seed is read. The subsystem loads the catalogue synchronously on first ask
(`LoadSynchronous`, which works under `-nullrhi` and in a bare test world,
since it needs neither a game mode nor the asset registry's scan) and
answers `FindPart(FName Id)`; `ds.Ship.Install`, the save's restore and
`FitPart` by id all go through it. An id it does not find is unknown, and
that is the whole test for the restore's fallback. **Rejected: an
AssetManager primary-asset scan** (`PrimaryAssetTypesToScan`). No scan is
configured in this project, the scan is asynchronous in the editor, and a
headless test would have to wait on it; one soft-pointer list has none of
that. `DeepSpace.Ship.Parts.CatalogueResolves` checks, in a bare world,
that every JSON id resolves through `FindPart` and that nothing else does.

`DeepSpace.Ship.Parts.Contract` holds three sides equal, as
`DeepSpace.Sky.MaterialContract` does for the sky: the JSON, the loaded
assets, and `FShipRatings::Stock()` against the stock rows. A settled
number is written back into the JSON and the script re-run.

**Rejected: parts as ini rows** (as the priors are). Hot-reloadable, but
ruling 1 names data assets, a part will later carry a mesh reference for
its housing, and the ini path has its own config-cache trap.

**Cost to change:** low; the JSON is the one place.

### 6. The CVars become session overrides of the fitted part

`ds.Nav.RangeLy`, `ds.Nav.ChargeSeconds`, `ds.Nav.WindingWant` and
`ds.Drive.Response` are how a playtest moves these numbers today, and two
of them are settled. Once a part rates them, a CVar that still held the
number would be a second source. So **their defaults become `-1`, meaning
"the fitted part's rating"**; any value of 0 or more overrides the part for
the session, as today. The settled 45 s and 380 W move into `ship_parts.json`
and `FShipRatings::Stock()`. `ds.Drive.Top` is unchanged: it only ever
shortens the lever and no part rates it. The one rule for reading them is a
pure helper, `ShipParts::Effective(double Rated, float CVar)` (the CVar if
0 or more, else the rating), and the subsystem's getters
(`GetChartRangeLy()`, `GetWindingWant()`, `GetChargeSeconds()`, the drive's
response) are the only place it is applied.

**Tests that read these CVars' raw values move in slice 1**, because after
the change a raw read is `-1`: `HumComponentTest.cpp:203,207,223` (divides
by the want, then sets it to twice the raw value, `-2`, which would mean
"the part"), `SliceChooseTest.cpp:89` (a want of `-1` makes the expected
feed 0), `JumpWindsTest.cpp:117` (the advertised charge), `NavScreenTest.cpp:
109-110` and `ChartLayoutTest.cpp:416-422` (the chart's radius, `-12` ly).
Each reads the effective value from the subsystem instance instead
(`GetWindingWant()`, `GetChargeSeconds()`, `GetChartRangeLy()`), or, where
the test has no ship, `ShipParts::Effective(FShipRatings::Stock().RangeLy,
CVar)`. Their numbers do not change; only where they read them from. Tests
that *set* one of these CVars to 0 or more (`FScopedCVar` in
`ChartRefreshTest`, `ShipDriveTest`, the quick-charge tests) keep working
unchanged, since any value of 0 or more still overrides; the scope restores
`-1`.

**Rejected: the CVar as a multiplier on the part.** A playtester typing
`ds.Nav.RangeLy 15` expects 15 ly, not 15 times something. **Rejected:
retire the CVars.** Playtests move these, and the rule is that every
tunable moves with no rebuild.

**Cost to change:** low; the tunables table in CLAUDE.md changes.

### 7. Every loadout is whole at rest, and no part needs another

Ruling 2's "no ladder" and ruling 3's aux risk meet in one guarantee,
enforced over the whole catalogue by a pure test:

- **Every combination** of catalogue parts, one per bay (aux included), is
  whole at rest under the **stock** reactor: draws + lights want + boosters
  want at most 1400 W. So no part needs a bigger reactor, no two parts
  exclude each other, and there is no combination to solve for. The split
  still bites when the jump winds, as designed (2026-09-26).
- **Every non-stock part is at least as open as stock on every axis of its
  bay** (the axes below), because ruling 2 says an upgrade widens.
- **Within a bay, no non-stock part dominates another**: none is at least
  as open on every axis and more open on one. That is what "a few distinct
  parts, not a ladder" means in arithmetic: each opens something different.
  Two parts with the **same numbers** do not dominate each other, so a part
  that differs from another only in character (its hum, its words, later
  its look) is legal. That is deliberate, and it is how a bay with one
  axis holds more than one part.
- **A core part rates only its own bay's values** (decision 2's table); an
  aux part rates none.

**What "open" means, per axis.** Every watt figure a part asks for is more
open when lower; every capability is more open when higher. The **draw is
an axis** in every bay (a lower draw leaves more for the split, and is
exactly as much an upgrade as more supply):

| Bay | Axes (more open is) | What decision 7 therefore allows |
|---|---|---|
| Reactor | `ReactorWatts` (higher); draw (lower; stock draws none) | one non-stock supply figure, since a single axis orders every pair; beyond it, parts that differ only in character at that figure |
| Drive | `DriveResponse` (higher), `ChargeSeconds` (lower), `WindingWant` (lower), draw (lower) | several: a quicker lever, a lower-want winding, a lower draw, as long as none beats another on every axis |
| Boosters | `LinearAcceleration` (higher), `BoostersWant` (lower), draw (lower) | several, trading acceleration against want and draw |
| Lights | `LightsWant` (lower: the same light at full feed for fewer watts), draw (lower) | two axes, so a lower-want part and a lower-draw part can both exist; **no brighter part**, since brightness is satisfaction and a higher want breaks wholeness |
| Life support | draw (lower) | one non-stock draw figure; beyond it, character only |
| Sensors / nav | `RangeLy` (higher), draw (lower) | two axes: a longer array and a lighter one |

This is a design constraint, not only a test: a lights part that wanted 350
W would break wholeness at the stock reactor and is therefore not a legal
part. The consequence is written down so nobody discovers it by a failing
test: **parts widen what the ship can do; they never make the ship need
more of anything.** The honest consequence for the reactor and life
support bays is that each holds **one** numerically different part and
then variants in character; "a few distinct parts" there means a few
characters. A bay the catalogue holds no non-stock part for is refreshed by
repair alone (decision 16 says what that does to the mid game).

The two example upgrades pass: the twin-core reactor (1800 W) makes the jump
wind at full with everything whole (1800 - 620 = 1180 W for 300 + 450 +
380 = 1130 W); the **quick-lever drive** (`Drive.QuickLever`,
`DriveResponse` 4.5 notches a second, every other number stock) makes the
drive follow the lever half as fast again, which the pilot feels at the
helm on every change of notch, and X brings the ship to rest sooner. It
replaces the ruling's other example, a 20 ly nav array, which ruling 1
gave "for instance": the chart chair shows the **six nearest** systems
(`UNavigationWidget::RowCount`), and at procgen's density those sit within
six or seven light years (the navigation spec), so a 20 ly array would
change nothing the player sees at the chair and show only in
`ds.Nav.Near`. The choice, and the alternative of keeping the array with a
chart that shows beyond six rows, is on the sign-off list. One consequence
for the developer: with the 1800 W reactor fitted, **the stock split never
bites at all** (decision 8's aux verbs are what reopen it). That is ruling
2 doing what it says, and it is noted on the sign-off list.

**Cost to change:** low to relax later; expensive to impose after a
catalogue has grown without it.

### 8. The auxiliary slots: verbs, not numbers (ruling 3's mitigation)

The warning the developer overrode is real: two open slots under one
reactor invite "which two extras are best", a small knapsack. The ruling
keeps the slots; **these rules keep the knapsack out**:

1. **An aux part adds a verb or a character, never a competing number.** It
   rates nothing in decision 2's table, so it never competes with a core
   part or another aux part on the same axis. Illustrative, not built:
   a floodlamp that lights the ground under a landed ship; a long-focus
   telescope that the chart chair's zoom can look through; a galley still;
   a radio that plays the local star's noise. Things to *do*, not things to
   *have more of*.
2. **An aux part wants nothing at rest.** Its power exists only while its
   verb is in use -- the engine's pattern ("an idle drive costs the ship
   nothing") -- booked as a draw off the top while used. So every
   combination is whole at rest (decision 7) and staying put is never
   taxed.
3. **One of a kind.** The two slots cannot hold two of the same part, so
   nothing stacks.
4. **No combined readout.** Nothing shows what the aux slots total, add or
   draw together; the nameplates list each fitted part alone.
5. **An empty aux slot is not shown.** A listed "Auxiliary -- empty" is a
   gap to fill, which is a to-do list in one line. The console lists what is
   fitted and nothing else.
6. **Aux parts do not wear** (decision 15): the late-game symptom cadence is
   the six core bays', and does not grow with extras.

**Where each rule is enforced.** `FitPart` enforces what is structural at
runtime: the part's bay matches (an `Aux` part into an aux slot, never
`None`), and one of a kind across the two slots. Rules 1 and 2 (rates
nothing, wants nothing at rest) are properties of a *part*, not of a fit,
so they are enforced over the catalogue, at authoring time, by
`.CatalogueRules` over the JSON and `.Contract` over the assets; a runtime
refusal would only repeat that check on the same data. Rules 4 and 5 are
the console's, held by `.NameplatesAreFacts`.

**Test loads are not parts.** The hog tests (`LampPanelsDimTest`,
`ShipSkyTest`) make a module in code with a standing draw of several
hundred watts to starve the ship. That is exactly what rule 2 forbids a
part, so they do not go through the bays: `UShipSubsystem` keeps a plain
draw seam, `AddLoad(FName, float Watts)` / `RemoveLoad(FName)`, booked in
`FShipPowerState` under `Load.<name>`, with no console command, no
nameplate and no save, and documented in the header as a seam for tests
(as `ds.Nav.FoldDraw` already books a draw that is not a part, directly in
the power state). `InstallModule`/`RemoveModule` are retired: the game mode
and the tests that fitted stock modules through them
(`JumpWindsTest.cpp:85,152`, `SliceChooseTest.cpp:451`) call `FitPart`, and
the hogs call `AddLoad` (*Parallel tracks*, track S).

Slice 1 builds the slots, the rules and their catalogue test; it ships no
aux part (the two examples are core). The first aux part comes with its
verb, in its own spec.

**Rejected: aux parts that raise a core rating** (a sensor booster, a
capacitor). That is the knapsack. **Rejected: aux draws from the split**
(a new consumer). One more player weight is one more knob to tune toward
an optimum -- the landing spec's reason for refusing a hold consumer.

**Cost to change:** low in code; each relaxed rule reopens the risk the
developer was warned about.

### 9. Nameplates on the engineering console

`UEngineeringConsoleWidget` gains a list below the lights toggle: **one line
per fitted part, in bay order**, each `BAY  Name  Rating  Words`:

```text
REACTOR       Twin-core reactor          1,800 W          Hums a fifth low. Came off a salvage tug.
DRIVE         Fold drive                 4.5 notches/s    Takes the lever before you have let go of it.
BOOSTERS      Booster cluster            2 km/s²          Quiet until you lean on them.
LIGHTS        Interior lighting          300 W            Warm, a little amber in the corridor.
LIFE SUPPORT  Air plant                  180 W            Somebody taped the filter door shut.
NAV           Survey array               12 ly            Older than the hull, and it shows.
```

(Names, words and the life support figure are placeholders; *Open
questions*, in-universe. The format below is not.)

**The format, per bay, and where each number comes from.** Every number is
the fitted part's own, read from the part (never from a CVar, the flight
limits, the power state's live values, or wear), so a playtest override or
a symptom never moves a plate:

| Bay | The one number | Source | Format |
|---|---|---|---|
| Reactor | its supply | the part's `ReactorWatts` | `{0} W`, grouped thousands |
| Drive | how quickly it follows the lever | the part's `DriveResponse` | `{0} notches/s`, at most one decimal |
| Boosters | its thrust | the part's `LinearAcceleration` | `{0} km/s²`, at most one decimal |
| Lights | what the lights ask | the part's `LightsWant` | `{0} W` |
| Life support | its standing draw | the part's `PowerDraw` | `{0} W` |
| Sensors / nav | its reach | the part's `RangeLy` | `{0} ly` |
| Aux | none | -- | name and words only |

- **The drive shows its response, not a top.** No part rates a top
  (decision 2), so a top on the drive's plate would read `0.1 c` for every
  drive part, and a shortened `ds.Drive.Top` would make the plate a second,
  wrong source. Never the charge either: no screen shows the jump's charge,
  and a rated charge time beside it would be a clock by another name.
- **Never a percentage, a tier, a comparison, a condition or a symptom.**
  No "Mk", no "I/II/III", no "upgraded", "basic", "stock", "improved",
  "better", "standard"; no "%" anywhere; no second number on a line. A
  symptom is found in the room, by ear and eye (decision 17), never listed
  on a screen, because a list of symptoms is a list of jobs.
- **Words are character and history**, never quality. From slice 4 a
  repaired part's words gain its own history (`Rebuilt by hand, twice.`),
  which records what the player did, not what the part lacks.
- The console reads the subsystem every tick, as the readout already does;
  it stores nothing.

**The existing readout goes, in slice 1.** Today the console shows
`REACTOR {0} W` / `DRAWN {1} W` / `SPARE {2} W` (*Context*). The lived-in
spec's decision 11, approved and binding, made the reactor readout a
nameplate and said "Nothing anywhere in the ship shows a total drawn",
rejecting `1000 W out · 740 W drawn` as a utilisation meter. Parts make the
old readout worse, not better: the reactor's nameplate would repeat the
REACTOR line; with the twin core it would read about 1800 / 1370 / 430
SPARE, a larger "unused" figure waiting to be spent; and once an aux verb
draws off the top while in use, SPARE becomes the aux slots' combined
budget, which aux rule 4 forbids. So **slice 1 removes the DRAWN and SPARE
lines**, and the REACTOR line *is* the reactor's nameplate, the first line
of the list. `AShipConsole::GetReadout` (a `BlueprintPure` no C++ calls, but
which `BP_ShipConsole` may) keeps its signature and returns the reactor's
nameplate line, so the Blueprint's reference holds; `check_blueprints.py`
runs after. The laptop's per-consumer watts are untouched: each consumer's
own share of its own want, never a total. `DeepSpace.Ship.ScreensAgree`'s
assertion that "the readout changed" with the lights moves to the laptop's
lights row, which already shows `0 W of 0 W` when the lights are off; the
console's text is asserted *unchanged* by the toggle apart from the
switch's own state.

`DeepSpace.Ship.Parts.NameplatesAreFacts` holds the rules above against
every row of the catalogue, and against the widget's rendered text for the
stock ship and for each example upgrade fitted:

- each fitted part's line matches `^<BAY>\s+<name>\s+<number> <unit>\s+<words>$`,
  one number, with the unit of its bay's row in the table, and the number
  equal to the part's own rating (aux: no number);
- no forbidden word, no `%`, no empty aux line, no second number;
- **no text on the console shows a total drawn or a headroom**: the words
  `DRAWN`, `DRAW` and `SPARE` do not appear, and neither do the rounded
  values of `GetPowerDraw()` or `GetPowerHeadroom()` when they differ from
  every rating on the list (the test sets a hog through `AddLoad` so they
  do);
- `ds.Nav.RangeLy 15`, `ds.Drive.Response 1` and `ds.Drive.Top 0.05`
  change no plate.

**Cost to change:** low (a widget).

### 10. The console commands, and spares

Until landing and a sourcing spec exist (ruling 8), parts come from the
console. All in `ShipSubsystem.cpp`, beside `ds.Nav.*`:

- `ds.Ship.Install <part>` -- fits a catalogue part (by id or display
  name) into its bay: the first spare with that id if there is one, else a
  new one conjured for the purpose; the displaced part becomes a spare.
  Aux parts go to the first free aux slot, or replace `Aux1` if both are
  full.
- `ds.Ship.Spares` -- lists the spares. `ds.Ship.Spares give <part>` adds
  a new one; `ds.Ship.Spares clear` empties the list.
- `ds.Ship.Describe` -- every bay: part, draw, ratings, and from slice 4 its
  age and symptom. **This is a developer's console line, not a screen in
  the ship**; it may print ages because no player sees it.

Spares are a plain list in `UShipSubsystem`. There is no inventory
screen, no carry limit and no weight: a spare is not an object to manage
(not a logistics game). From slice 2 the housing is where a spare is fitted
(decision 13).

**A part is one particular part, and it keeps its own wear state wherever
it is.** Three rules, stated here because they decide whether hold-E
repair matters at all:

1. **A spare keeps its state.** A part taken out of its bay goes to the
   spares with its age, its life, its symptom and its repairs (decision
   11's `FShipPartState`), and does not age there: only fitted core parts
   age (decision 15). A symptomatic part taken out and fitted again comes
   back with its symptom.
2. **Only a new part draws a new life at a fit.** A part that has never
   been fitted -- conjured by `ds.Ship.Install` or `Spares give`, later
   sourced -- draws its first life when it is first fitted. A spare being
   refitted draws nothing: it resumes. Every fresh life, whether a new
   part's first or a repair's, takes the bay's next life index
   (`FShipBayState::LivesDrawn`, incremented at every draw), so a new part
   never redraws the U, and so the life, of the part it replaced.
3. **Swapping out and back is not a repair.** Fitting a *different* part
   clears the bay's symptom, because the bay now holds a part that has none
   (ruling 7, "upgrading the part also clears it"); the symptom stays with
   the part that had it. With one spare, two E presses at a housing put
   the original part back, symptom and all. The only way to clear a part's
   own symptom is to repair it (decision 18).

`DeepSpace.Ship.Parts.SpareKeepsItsState` (slice 4) holds all three: out
and back returns the symptom, age and life bit-identical; a fit of a new
part increments `LivesDrawn` and draws a different U from the part it
displaced; the displaced part does not age across a fold.

**Cost to change:** low.

### 11. State is plain and serialisable from slice 1

The fitted state is a `USTRUCT` -- Unreal's reflected struct, which the
save system and the network layer can serialise field by field without
hand-written code, and which is still a plain value a headless test can
build and compare:

```cpp
USTRUCT() struct FShipPartState {        // one particular part, fitted or spare
    UPROPERTY() FName PartId;          // None in a bay: the stock part's ratings (decision 3)
    UPROPERTY() double AgeJumps = 0;   // slice 4
    UPROPERTY() double LifeJumps = 0;  // slice 4: the age at which its symptom arrives
    UPROPERTY() bool bHasLife = false; // slice 4: false until first fitted (decision 10)
    UPROPERTY() FName Symptom;         // slice 4: None, or one of the bay's symptoms
    UPROPERTY() int32 Repairs = 0;     // slice 4: history for the words and the look
    UPROPERTY() bool bOriginal = false;// slice 4: one of the ship's seeded stock parts (decision 19)
};
USTRUCT() struct FShipBayState {
    UPROPERTY() FName Bay;             // the EShipBay's name, never its position
    UPROPERTY() FShipPartState Part;
    UPROPERTY() int32 LivesDrawn = 0;  // slice 4: lives this bay has drawn, for the seed
};
USTRUCT() struct FShipLoadoutState {
    UPROPERTY() TArray<FShipBayState> Bays;     // one per bay, found by Bay
    UPROPERTY() TArray<FShipPartState> Spares;  // each keeps its own state
};
```

**Bays are keyed by name, not position.** A `TArray` indexed by the enum
would shift every saved aux index the day a core bay is added before
`Aux1`; each entry names its bay instead, and the restore finds bays by
name, reports an unknown one, and gives any bay the save lacks its stock
part.

Ids, not object pointers: a saved pointer to an asset that has since moved
loads as null, while an id that no longer names a part can be reported and
fall back to the stock part. The wear fields exist from slice 1 and stay at
zero until slice 4, so slice 3's save needs no migration when wear arrives.
`DeepSpace.Ship.Parts.StateRoundTrips` writes it through Unreal's own
struct serialiser into memory and reads it back equal.

**Cost to change:** medium once saves exist in the wild.

### 12. The bays in the hauler (slice 2)

Each core bay gets a **housing**: a primitive panel or cabinet in the room
the part belongs to, generated like everything else in the hauler. Layout
data in `Tools/hauler_layout.py`, a new `BAYS` list beside `HUM_SOURCES`
and `PRACTICALS`:

| Bay | Room | Where | Why there |
|---|---|---|---|
| Reactor | engineering | the existing `reactor` placement is the housing | it already is the reactor |
| Drive | engineering | aft wall, beside the pipe run | the drive is the reactor's neighbour |
| Boosters | cargo bay | aft wall | the ship's thrust structure is aft |
| Lights | corridor | a fuse panel, starboard wall | every room's lights pass the corridor |
| Life support | airlock | the aft wall beside the suit lockers | air handling lives by the seal |
| Sensors / nav | cockpit | aft wall, port side, behind the chart chair | the array feeds the chart |
| Aux 1, Aux 2 | cargo bay | two racks on the port wall | extras are stowed where cargo is |

Placements are proposals for the developer to walk (sign-off). Rules the
validator enforces (`validate_hauler.py`, `test_placement.py`): every
housing reachable standing; none inside a seat's reach box or any
keep-clear zone; none in the crawlway (it would block the crouch-only
intent); none needs two people or a second housing.

`place_bays` in `build_hauler.py` spawns `AShipBayHousing` actors tagged
**`Ship.Bay.<Bay>`** (addressed by tag, never by name); `verify_level.py`
holds each to the layout. Housings are **not** tagged `Dress.Wear`: their
look belongs to the bay's state (decision 19), and two writers of one
material is how a look goes wrong silently.

**Cost to change:** low: layout data and a level rebuild.

### 13. The install act (slice 2)

Standing at a housing, the prompt says what E does there
(`AShipScreen::GetZoomPrompt`'s pattern, through `UInteractableComponent`):

- **`(E)  Fit <spare>`** when a spare for this bay is aboard. E cuts the
  view to a framed shot of the housing (a camera cut, marked with
  `SetGameCameraCutThisFrame` so temporal anti-aliasing does not smear the
  jump, exactly as sitting does), the part swaps -- the housing's panel
  changes, the reactor's hum changes its voice (decision 17) -- and the
  view holds until any movement key cuts back. **No timer, no bar, no
  animation to wait out.** While framed, each further E fits the next spare
  for the bay in catalogue order, the displaced part joining the spares, so
  pressing on returns to the start; E never cuts back, since after a swap
  there is always a next spare and one key must not mean two things.
- **`(hold E)  Repair`** when the fitted part has a symptom (slice 4,
  decision 18).

**The input, exactly.** `IA_Interact` is bound on `Started` only today
(`DeepSpaceCharacter.cpp:395`), so a tap and a hold cannot be told apart
when E goes down. The action and its trigger in `setup_flight_input.py` are
left alone; the character gains `Completed` and `Canceled` bindings for
`IA_Interact` and decides at a housing by how long E was held, as the
levers already count presses:

| At a housing where | Prompt | E pressed | E released before `ds.Ship.TapSeconds` (0.3 s) | E held to `ds.Ship.RepairSeconds` (3 s) | E released between |
|---|---|---|---|---|---|
| the part is well, a spare is aboard | `(E)  Fit <spare>` | fits, at once | -- | -- | -- |
| the part is well, no spare | none | nothing | -- | -- | -- |
| the part has a symptom, no spare | `(hold E)  Repair` | the work's sound starts | nothing | repairs | nothing |
| the part has a symptom, a spare is aboard | `(hold E)  Repair` above `(E)  Fit <spare>` | the work's sound starts | **fits the spare** (the bay's symptom clears, since the bay now holds a well part; the symptomatic part goes to the spares with its symptom, decision 10) | repairs, and fits nothing | nothing |

So E never does two things from one gesture: a tap fits, a hold repairs,
and at a well housing E acts on the press as every other E in the ship
does. Everywhere that is not a housing, E stays on `Started`, unchanged.
Slice 2 builds the first two rows (fitting on the press); slice 4 adds the
release path and the last two. `DeepSpace.Ship.BayHousing.TapOrHold`
(slice 4) drives each row through the bindings: a symptomatic housing with
a spare, tapped, fits the spare and leaves the removed part symptomatic in
the spares; held, repairs and leaves the spares untouched; released at 1 s,
changes nothing.

**The framed shot is a path of its own in the character.** Framing today
exists only for screens: `PlaceCamera` and the pointer are gated on
`IsUsingScreen()`, and `ResolveFramingView` fits a panel. The housing gets
a sibling path: `AShipBayHousing::GetFramingView()` returns a fixed eye
(a point `FramingDistanceCm` in front of the housing's face, at standing
eye height, looking at its centre) and a field of view; the character
holds `FramedHousing` (a weak pointer, nothing else), and while it is set
`PlaceCamera` puts the camera there instead of at the head, calling the
existing `MarkCameraCut()` on entering and on leaving. Any `IA_Move`
input clears it. The body does not move, sit or lock; the
pointer is untouched. `DeepSpaceCharacter.*` is therefore slice 2's, owned
by track H and serialised after landing (b) and (c) (*Parallel tracks*).

**Anyone aboard can do either.** A housing is not a seat: the body is not
parked, the pointer is not captured, and nothing is locked to whoever
started. No bay is anyone's station, and no act needs two people. The
engineering console is **not** where parts are fitted: one screen for every
swap would read as the engineer's station (the vision's "shared presence,
never division of labour").

**Rejected: fitting from the console screen.** Cheap, but the part is never
touched and the console becomes a station. **Rejected: an animation of the
swap.** The developer has no animation pipeline for it, and a wait is a
wait; the cut is the act.

**Cost to change:** low.

### 14. The save (slice 3)

`UShipSaveGame : USaveGame` -- Unreal's class for a bundle of `UPROPERTY`s
written to `Saved/SaveGames/<slot>.sav` by `UGameplayStatics::
SaveGameToSlot` -- holding exactly ruling 9's list: the flight state
(universe position, orientation, velocity, both levers), the course and
the target (as ids), `FShipLoadoutState` (parts, spares, ages, lives,
symptoms, repairs), the consumer weights, the lights switch, and a format
version.

- **World-level.** The slot is named for the universe seed
  (`Ship_<seed>`), never for a player; two players aboard one universe
  share one ship.
- **Saved at events, never on a timer, and never asked.** After a fit, a
  repair, a fold's arrival, a change of weights or lights, and on
  `EndPlay`. Never during transit (the last save stands; a quit mid-fold
  resumes at the departure point, at rest). There is no save prompt and no
  "unsaved" warning: remembering to save is a chore.
- **Restored in `UShipSubsystem::OnWorldBeginPlay`**, before
  `NavStart::OpeningPlacement`, which then runs only when there is no save.
  The game mode hands its stock loadout to the subsystem
  (`SetStockLoadout`) rather than fitting it, and the subsystem decides:
  the save if one exists, else stock. An id that no longer names a part
  falls back to that bay's stock part, logged by name.
- **`ds.Ship.NewShip`** deletes this universe's slot and reopens at the
  opening placement; `-NewShip` on the command line does the same at start.
- The dressing needs nothing saved: it is regenerated from the root seed.
  The housings' looks come from the bays' saved state.

**Rejected: save in slice 1.** It roughly doubles the slice and touches
the flight state (ADR 0005) and the universe. **Rejected: a player-profile
save.** The vision makes the ship world-level.

**Cost to change:** medium; the format is versioned from the first write.

### 15. The wear clock (slice 4): one Weibull life per part, advanced by folds and chosen stresses

**The model.** Each core part's life is **drawn once, as an age**, when the
life starts (fitted, or repaired): `LifeJumps = λ (-ln U)^(1/k)`, the
Weibull's inverse CDF, with U from the part's own seed (below). k = 3 and
λ = 150 jumps (ruling 5's illustrative numbers): k above 1 is wear-out, a
hazard that rises with age, which ADR 0008 names for "component wear and
failure". The part's age advances only at the events below; when the age
reaches `LifeJumps`, **one symptom arrives** (decision 17), drawn from the
bay's list. Nothing is rolled per frame or per step: one draw per life
means the clock is a comparison, and a comparison cannot drift.

**What ages a part, and nothing else:**

- **A fold**: every fitted core part ages at each fold's arrival: one jump
  for an interstellar fold, and `InSystemFoldWeight` of one (ini, **0.25**
  until the cadence is measured, below) for an in-system fold. Both are
  jumps, so ruling 5's "per jump" holds; the weight is there because the
  in-system fold is how the player gets round a system (1 AU at 0.1 c is
  over 80 minutes), and at full weight it would make the late-game cadence
  a tax on moving about. The choice is on the sign-off list. One fold is one
  jump whatever its distance, so exploring far is not taxed per light year.
- **A chosen stress**: bringing the ship down to a star's floor adds
  `StarHeatJumps` (10) to the sensors and life support. The band is
  **relative to the floor**, never an absolute radius, so a playtest that
  moves `ds.Flight.StarFloorRadii` moves the band with it: with `h` the
  altitude over the star's surface and `F = UShipSubsystem::FloorFor(star)`,
  the stress arms when `h` falls **to within 1.1 F** and re-arms only after
  `h` has been beyond **2 F**. It counts **once per visit**: parking there
  for an hour costs what passing through does. Arming is a crossing: a ship
  that *starts* inside the band -- placed there by `ds.Sky.Goto`, or
  restored there by the save -- counts as already armed for that visit and
  is not charged. **Arming is perceptible when it happens**: the hull
  ticks as it takes the heat (a one-off sound at the sensors' housing and
  the airlock's, the two parts it ages) and the glass shimmers once
  (`M_SkyGlass`'s `Veil` pulsed through `MPC_Sky`, back within a second).
  So the first visit teaches it, and every later approach to the floor is a
  known choice rather than a hidden charge; staying costs nothing more,
  and the cue says so by not repeating. This pays the flight-feel spec's
  star-heat IOU.
- **Nothing else.** Not cruise, not the drive in-system, not idle, not
  landed, not parked, not time in any form, not distance. The subsystem
  advances ages only inside the fold's arrival and the stress's arming;
  there is no tick path that touches them.

**A worn part never gets worse.** A part has at most one symptom. While a
symptom is present, its clock stops. Ignoring every symptom forever
converges on a ship where each core part has one symptom and each deficit
sits at its floor -- a bounded worst case, reached and then left alone.
Nothing escalates, cascades, fails or spreads.

**Repair and upgrade start a fresh life.** A repair clears the symptom,
zeroes the age and draws a new life; a new part starts at age 0 with a
life drawn at its first fit. Each draw takes the bay's next life index
(`LivesDrawn`, then incremented). A spare refitted resumes the life it had
(decision 10): swapping is not a repair. Without that, a k = 3 hazard would rise after
every repair and symptoms would come faster and faster: ETS2's "permanent
wear" treadmill.

**Tails, bounded deliberately.** A fresh life is clamped to [0.25 λ,
2.5 λ]: no part shows a symptom within 37 jumps of being fitted or
repaired (1.6% of draws are moved up to that floor; the belief is that
early-life faults are not modelled -- a fitted part was fitted well); the
upper clamp moves 2 in 10^7 and is stated only so the tail is bounded.

**Determinism, world-level.** `WearSeed = Derive(Root, Label("wear"))`, and
each life's U from `Derive(Derive(WearSeed, BayLabel), LivesDrawn)`, the
bay's count of lives drawn so far, so no two lives in one bay share a U. The same
universe, the same sequence of fits, repairs, folds and stresses gives the
same symptoms on every machine: what would replicate is the event log, the
plan, never the state.

**The rules are data.** `[/Script/DeepSpace.ShipWearConfig]` in
`DefaultGame.ini` (`WeibullShape`, `ScaleJumps`, `InSystemFoldWeight`,
`StarHeatJumps`, `StartAgeA`, `StartAgeB`), read through `UShipWearConfig`, reloaded from
disk by `ds.Ship.Wear.Reload` through `GameIniReload::RereadFromDisk` (the
config-cache trap, again). The guarantees -- one symptom per part, the
clock stopped while symptomatic, the life clamp, every deficit's floor,
`MinHoursPerSymptom` -- are constants in `ShipWear.h`, so no ini edit can break them.

**What the cadence is, in jumps** (k = 3, λ = 150, simulated): a new part
shows a symptom within 50 jumps 3.6% of the time and within 100 jumps 26%
(mean life 134 jumps). A late-game ship whose six core parts are all on
repaired lives settles at about one new symptom somewhere in the ship per
22 weighted jumps.

**The cadence is gated in hours, because the vision's boundary is in
hours**: "Wear measured in tens of hours does not create a rate ... tens of
minutes creates a rate." A figure in jumps says nothing until it is
multiplied by folds an hour. So slice 4 is **not done** until:

1. a playtest has measured **folds per hour**, interstellar and in-system
   separately (`ds.Ship.Describe` prints the ship's fold counts, and the
   session's length is the playtest's), and the figures are written into
   the ini's comments beside `ScaleJumps` with the playtest's date;
2. `.LopsidedCurve` asserts, at those measured rates, that the late phase
   brings **no more than one new symptom per 10 hours** of play
   (`MinHoursPerSymptom`, a constant in `ShipWear.h`, not the ini, so no
   tune can break it); and
3. λ and `InSystemFoldWeight` are set to satisfy it. Until then λ = 150
   and the weight 0.25 are placeholders the implementation carries, and the
   career test's hour assertion is the gate that refuses them if they fail.

At an illustrative ten minutes a fold, one symptom per 22 jumps would be
one every 3.7 hours: a schedule, and the gate would refuse it. λ is one ini
line.

**The amendment.** CLAUDE.md says of the ship model: "nothing in the model
changes on its own with time". Wear is ship state changing without the
player's hand at that moment, so it is recorded as the **second sanctioned
exception**, beside the landing spec's starved sink, and bounded as
tightly: it advances **only at a fold's arrival and at the arming of a
chosen stress**, never with time; **at most one symptom per part**, and the
clock stops while it is present; **every deficit has a floor** and none
touches a top, the charge, the range, a want or the reactor's rating
(decision 17); and **every symptom clears for free, at the part, whenever
the player chooses**. `FShipPowerState` itself is untouched: its comment
("no value in here changes on its own with time") stays literally true, and
gains a line pointing at `ShipWear` as where the exception lives
(*Documentation*).

**Rejected: play hours.** Parts would age while the player stands in the
galley, taxing the cruise the vision calls the point of the game.
**Rejected: distance flown.** A per-kilometre tax on exploring (ETS2).
**Rejected: a continuous condition that erodes.** "A survival meter with a
longer fuse." **Rejected: a per-fold roll** (each fold a small chance). It
is the same hazard computed a worse way: many draws where one suffices,
and a per-step probability that depends on how the steps are cut.
**Rejected: symptoms that compound.** A part that gets worse the longer it
is ignored is a rate to keep up with.

**Cost to change:** low for the numbers (ini); the event list (folds,
stresses) is the design, and adding a time-driven source would break the
amendment's bound.

### 16. Seeded history: the stock ship starts old

The early game's "visible almost immediately" comes from world start, not
from the clock. Each stock part begins with an age of `StartFraction x λ`,
where `StartFraction ~ Beta(5, 2)` -- a bounded proportion of a part-life,
the same shape as the dressing's wear draw (mean 0.71: the ship has lived
most of a part-life before the game begins). Its life is drawn from the
full Weibull as in decision 15. **If the life is shorter than the starting
age, the symptom is already present at world start**; otherwise the part
has `LifeJumps - Age` left. Comparing an unconditioned draw with the start
age *is* the conditional distribution, so nothing is rejected and resampled
(ADR 0008: shape the prior).

Simulated: **32% of stock parts start with a symptom** (about two of the
six on an average ship), and a stock part with none shows one within 30
jumps a further 21% of the time. So the starter ship has a history on the
first walk-through, gains a symptom or two over its first dozens of jumps,
and every part the player replaces arrives young: the mid game's absence
falls out of upgrading, and the late game's return falls out of stopping.

**That holds bay by bay, and only where the catalogue has a part to fit.**
Decision 7 allows a non-stock part in every core bay (in the reactor and
life support, one numeric figure and then character variants), but it
does not make the catalogue hold one. A bay with no part but its stock one
keeps its seeded, old part until it is repaired, and after the first repair
it is on a fresh life like any other. So in such a bay the mid game's
quiet comes from repair, not from upgrading; the vision's curve is then
the player's first repair, which is free and chosen, rather than a
purchase. That is acceptable against the vision's "players upgrade
components long before they wear out", which describes a catalogue that
has upgrades, and it is recorded so nobody reads the curve as guaranteed
for a bay the catalogue leaves empty. Whether each core bay gets a part
before slice 4 ships is *How many parts per bay* in *Open questions*.

Seeded parts are marked `bOriginal` (decision 11): the ship's own
fittings, which is what decision 19's faded look shows.

Seeded from `WearSeed` and the bay, world-level: two players aboard one
universe inherit the same history, as they inherit the same mugs.

**Cost to change:** low (ini: `StartAgeA`, `StartAgeB`).

### 17. Symptoms and deficits, per bay

A symptom is **one discrete thing** with a look, a sound or a quirk, and at
most one small numeric deficit. The lists are data in `ship_parts.json`
(per bay), drawn from categorically at arrival.

| Bay | Character (one of, drawn) | When and where it is perceived | Deficit while present | Floor |
|---|---|---|---|---|
| Reactor | a slow beat in the drone (two partials a hair apart); a tick in the housing as the jump winds; a stutter in the drone as the fold opens | from the reactor | **none** | -- |
| Drive | the drive clicks on every change of notch the ease makes | at every step of the in-system ease, for as long as it eases, sounding from the drive's housing | the in-system drive's ease at **0.9** of its response | 0.85 |
| Boosters | a thump as they take up; a hiss under the thrust while they push | whenever they accelerate the ship (cruise, or the drive's ease), for as long as they do, from the boosters' housing | cruise's acceleration and the drive's ease at **0.9** of rated (the ruling's example) | 0.85 |
| Lights | a faint mains buzz with every lit lamp, loudest at the fuse panel | for as long as the lights are on and dimmed, which is whenever they are lit; each lamp buzzes, and the fuse panel most | the lights at **0.9** of their brightness at full feed | 0.85 |
| Life support | an air handler in one room rattles; a draught through a vent | from that room | **none** | -- |
| Sensors / nav | the chart's rows ghost for a moment when they change | at the chart | **none** | -- |

**A symptom is perceived where and when its deficit acts, and it points
at its housing.** Any symptom that carries a deficit has a character the
player can perceive **at the moment and the place the deficit applies**
(at the helm while the ship eases, for the drive and the boosters; in any
lit room, for as long as the dimming lasts, for the lights), and that
character **sounds from its housing's position** (spatialised, with an
attenuation long enough to carry from engineering and the cargo bay to the
helm), so a player who notices the ship easing a little slower hears which
way the cause is and walks to it, rather than walking every housing in
turn looking for a faded one. Without that, a deficit felt at the helm and
a cue heard elsewhere would ask the player to patrol: "check something
periodically to avoid a consequence", which the vision says crosses the
line however gentle each check is. Hence the table: the drive's "stutter as
the fold opens" is heard at the fold, not while the ease is slow, so it
moved to the reactor, which carries no deficit; the boosters' "hiss that
lingers after a stop" became a hiss while they push; the lights' "flicker
as they come up, then settles" left a dimming with nothing to hear or see
once it settled, so it became a buzz that lasts as long as the dimming. A
character with no deficit (reactor, life support, sensors) may sound at
any moment, since there is no consequence to connect it to.

The rule is data and tested: each character row in `ship_parts.json`
carries `When` (`OnEase`, `OnThrust`, `WhileLit`, or, for a symptom with
no deficit, `Any`) and `Where` (`Housing`, or for the lights
`LampsAndHousing`), and **`.SymptomIsWhereItActs`** (slice 4) holds, over
every row, that a deficit's `When` is the deficit's own trigger and its
`Where` includes the housing, and, in a test world, that easing with a
worn drive plays the drive's sound at the drive housing's position on each
step of the ease. A character option that fails is not drawable.

**What no deficit may touch, and why:**

- **The jump's charge, its winding want, the starved rate, the cone.** No
  screen shows the charge, because a charge that fills is a clock, and
  waiting it out is the jump's schedule, not the player's. A worn drive
  winding in 50 s instead of 45 s would be exactly that wait made longer by
  the game: the ruling's jump-charge check, answered by keeping every drive
  deficit off the charge. The drive's deficit is its in-system ease.
- **Any top speed.** Ruled (landing, drive): thrust never lowers a top.
- **The range.** A range is a ceiling; a worn array showing fewer stars is
  the vision's "73% of potential" on the chart.
- **The reactor's rating, and any want.** The nameplate never drops, and
  a deficit in the split would make the allocation screen the place a
  symptom shows: the one place the vision most fears a right answer.
- **Life support**, ever. Not a survival game.

**Floors, as guarantees.** Each deficit is a scale in `FShipWearScales`
from the pure `ShipWear::Scales(Bays)`, never below `DeficitFloor` (0.85,
a constant in `ShipWear.h`). Where two symptoms scale the same rate (a worn
drive and worn boosters both slow the drive's ease), the product is floored
at 0.85, not 0.81. **The power model's floor is never breached**: the
boosters' final thrust is `max(StarvedBoosterThrust, wear x power thrust)`,
so a worn, starved ship still pushes at a quarter.

**The quiet-optimum check** (the lived-in spec: "a well-fed engine may
sound better, not different ... the anti-chore failure, told by ear").
Ruling 6 makes worn parts measurably worse, so a whole ship *is* an
optimum; the ruling accepts that, and this spec keeps it small and cheap:

- the deficits are three, each 10% at most, on how the ship *eases*, never
  on what it can *reach*;
- a symptom never grows (decision 15), so ignoring it costs nothing more
  tomorrow than today;
- the fix is free, takes a few seconds, needs no part, no spare and no
  trip, can be done by anyone aboard at any time, and is never asked for;
- **nothing shows it**: no condition, no list, no HUD word, no alarm, no
  console line in the ship. The player learns a part is tired by hearing
  the click, the thump or the buzz where and when it matters, and finds
  the part by following the sound to its housing (the rule above), never
  by walking every housing in turn;
- **the part's own voice is character, never quality**: reactor parts
  carry a hum fundamental as character (the twin core a fifth lower),
  never a louder, smoother or cleaner voice, so *choosing* a part is never
  choosing the better-sounding one.

**This does not avoid the quiet optimum, and says so.** Every symptom's
character is a defect -- a rattle, a beat, a click, a thump, a buzz -- and
a repair removes it. Removing a defect is what "sounding better" means, so
the lived-in spec's warning applies here directly: a whole ship does sound
better than a worn one, and that is an optimum told by ear. Ruling 6 made
worse-plus-character a deliberate choice, and the defect sound *is* the
intended cue: without it the player would not find the part. So the spec
relies on the mitigations above -- small, floored, free, never growing,
never asked for -- and not on any claim that the design has removed the
optimum. Whether a player hears a clean ship as a target to keep is the
playtest's question (*Risks*), and its remedy is quieter symptoms or
smaller deficits, never a meter.

The specific deficits (which bays, 0.9, the 0.85 floor, the three bays
with none) are on the sign-off list, as ruling 6 asks.

**Rejected: a deficit on the charge** (the ruling's own illustration of the
risk). **Rejected: worn parts draw more watts.** It couples wear to the
split. **Rejected: deficits on every bay.** Reactor, life support and
sensors each have no deficit that does not hit a ceiling, the split or
survival.

**Cost to change:** low for the numbers; the "never touches" list is the
guarantee and is tested as one.

### 18. Repair

**Hold E for `ds.Ship.RepairSeconds` (3 s) at the housing** of a
symptomatic part. While held, the body stays where it stands, the view does
not cut (the player is working, not framing), and the sound of the work
plays at the housing. Letting go early loses nothing and saves nothing:
there is no partial repair to track. At the end: the symptom clears, the
age zeroes, a new life is drawn (decision 15), `Repairs` counts up, the
housing takes `MI_Ship_furniture_replaced`, and the part's words gain its
history. **Free**: no spare, no kit, no material. **Anyone aboard.**
Fitting a different part clears the bay's symptom too, because the bay then
holds a part without one; the symptom stays with the removed part in the
spares, and refitting it brings it back (decision 10). A repair writes the
save (slice 3's events; asserted in slice 4's `.RepairSaves`, since repair
does not exist until slice 4).

**No progress bar.** Three seconds is a gesture, and a filling bar is a
small clock.

**Rejected: a repair that needs a common spare.** A stock of spares to
keep topped up is a queue to feed. **Rejected: repair by replacement
only.** Late-game wear would then have no answer short of an economy that
does not exist.

**Cost to change:** low.

### 19. Housings: history in the dressing's materials, a symptom in a look of its own

The dressing's faded material means **history** on furniture: a snapshot at
world start, about 11% of pieces, never changing and never fixable. If a
housing were faded *while a symptom is present*, the same look would mean
"needs doing" there, and either the player would learn that faded means a
job and read every faded chair as broken, or the housing's fade would tell
them nothing. So the two meanings get two looks:

- **History, in the dressing's materials, never changing with a symptom.**
  A housing holding one of the ship's seeded original parts (`bOriginal`)
  is **faded** (`MI_Ship_furniture_faded`), as old furniture is; a part
  fitted new is **standard**; a part that has ever been repaired is
  **replaced** (`MI_Ship_furniture_replaced`), as ruling 7 says. These are
  facts about the part's past, exactly what faded and replaced mean on the
  furniture, so faded never means "needs doing" anywhere in the ship.
- **A symptom, in the housing's own geometry.** Each housing has an access
  panel as a separate primitive; **while a symptom is present the panel
  stands ajar**, a hand's width, and closes when the symptom clears. No
  lamp, no colour, no glow: a lit indicator would be a warning, which the
  anti-chore audit forbids. The ajar panel is found by walking to the sound
  (decision 17), and confirms the place rather than advertising it from
  across the ship.

The writer is `AShipBayHousing`, asking `UShipSubsystem` for its bay's
state, never the dressing subsystem, which stays tickless and stateless. A
replaced housing is the player's own history in the ship, which lasts once
slice 3 saves it. **Rejected: faded while symptomatic** (the first draft):
one look with two meanings.

**Cost to change:** low.

### 20. Sourcing, later: discrete named parts

When landing and a sourcing spec exist, parts come from the world as named
things -- "a reactor coil off a wreck on Kessa II", a nav array traded on a
populated world -- never as ore counts, recipes or refining chains (the
vision: "not a factory or logistics game"). Nothing in slices 1-4 assumes
more than a part id arriving in the spares list; `ds.Ship.Spares give` is
the stand-in for whatever that becomes.

## Seams with work in flight

- **Landing slices (a)-(c)** are approved and planned in one plan,
  `docs/superpowers/plans/2026-09-27-landing-slice-1.md` (untracked; it
  covers all three slices), and not built. Landing's track S owns
  `ShipSubsystem.*`, `ShipPowerState.*`, `ShipHum*`, `DeepSpaceCharacter.*`,
  `setup_flight_input.py` and the Playtest tests in slice (b), and slice
  (c)'s single tree takes S's files for C4-C6; its track F owns
  `hauler_layout.py` and `test_placement.py` in slice (b).

**One ordering rule per slice:**

| This spec's slice | Order against landing | Why |
|---|---|---|
| 1 | **before landing (b)** | small and self-contained; landing (a) touches none of its files. It includes a task that amends the landing plan (below). |
| 2 | **after landing (c) is merged** | both edit `hauler_layout.py` and `test_placement.py` (`GEAR`/`BELLY` against `BAYS`), `DeepSpaceCharacter.*` (Space and C against the housing's E, its release path and the framed shot) and `ShipHum*` |
| 3 | after slice 2 | -- |
| 4 | after slice 3, and so after landing (c) | `.NeverWithTime`'s landed case needs landing (c)'s LANDED state; the star stress reads `FloorFor`, which landing changes over worlds (not over stars) |

**Slice 1 amends the landing plan.** The plan was written and checked
against `main` at 202703c, and its slice (b) steps patch literal text that
slice 1 changes. Task S1-S4 (plan tasks 23-26) replace "the three lines
from `const float BoosterFeed = ...` to `const float Thrust = ...`" and
write `PowerState.SetWant(ShipPower::Boosters, BoostersWant + HoldWant)` and
`SplitBoosters(..., HoldWant, BoostersWant)`, but slice 1 moves
`BoostersWant` out of `ShipSubsystem.h` into `FShipRatings` and rewrites
those lines in `ApplyAllocation`. And the landing plan's `ShipHum*` and any
step naming `GetWindingWant()` statically, `InstallModule`, `LightsWant`,
`DefaultReactorOutput` or a raw read of the four CVars (decision 6) will
not apply either. So slice 1 carries one more task, owned by track S and
merged with it: **re-plan every landing step that names text slice 1
changes**, found by grepping the plan for those names, against the
post-slice-1 tree, and update the plan in place (plans are "updated in
place as reality contradicts them"). The amendment fixes one rule for the
boosters want: **one writer, `ApplyAllocation`, sets it, as the rated
boosters want plus `HoldWant`**; `FitPart` does not push the boosters want
itself but marks the ratings changed, and `ApplyAllocation` writes the want
from them on its next pass, so a fit and a hold never write it in the same
frame from two places.
- **`ShipHum*` is a seam in slice 1 as well as slice 2**:
  `ShipHumComponent.cpp:90` calls `UShipSubsystem::GetWindingWant()`
  statically, and slice 1 makes it an instance call. Landing (b) then
  rebases on it, through the amended plan.

## The anti-chore audit

| Thing | Could it tell the player they are behind? | Why not |
|---|---|---|
| Upgrades | a ladder to climb, a tier you are on | a few distinct parts per bay, none dominating another (tested); no tier, "Mk" or comparison anywhere; the stock part is never named as stock |
| The loadout | a configuration with a right answer | fixed bays, one part each; every combination whole at rest; no part needs another (tested) |
| The aux slots | a best combination (the warning the ruling overrode) | verbs, not numbers; nothing wanted at rest; one of a kind; no combined readout; an empty slot is never shown; they never wear |
| The nameplates | a readout of lost potential | one number, the part's own rating, read from the part and moved by no CVar or symptom; never a percentage, a condition, a symptom or a comparison (tested) |
| The console's readout | a utilisation meter; the aux slots' combined budget | DRAWN and SPARE removed in slice 1 (the lived-in ruling); the REACTOR line is the reactor's nameplate; no total drawn or headroom anywhere on the console (tested) |
| Spares | an inventory to manage | a list, no limit, no weight, no screen; each spare keeps its own state, so swapping is never a disguised repair |
| The wear clock | a rate to keep up with | advances only at folds and chosen stresses; nothing while cruising, idle, landed or parked; one symptom per part, and the clock stops while it is present; gated at no more than one new symptom per 10 hours of late play at measured fold rates |
| A symptom | a problem that grows; a patrol to find it | never escalates, cascades or fails; its deficit sits at a floor and stays there; a deficit's symptom is heard where and when the deficit acts, from its housing, so it is followed, never searched for (tested) |
| A housing's look | faded meaning "needs doing" | faded and replaced are history only; a symptom shows as a panel ajar, never a lamp |
| The deficits | a quiet optimum, a tax on the jump | three, each at most 10%, on easing only; never the charge, a top, the range, a want or the reactor; the fix is free and quick (sign-off) |
| Seeded history | a mess to clear before playing | two symptoms or so on the starter ship, all harmless, all optional to fix; history, not a task list |
| Repair | a chore on the game's schedule | free, three seconds, anyone, whenever the player chooses; never asked for, never warned about |
| The star stress | a hazard with a timer; a hidden penalty | once per visit, however long the stay; relative to the floor; a tick and a shimmer when it arms, so an approach is a known choice |
| The install act | a station, a job | anyone aboard, any housing, no seat, no lock; not at the console |
| The save | remembering to save | saved at events, never asked, never warned |
| Wear while away | the ship decays when not played | nothing ages without a fold; a saved ship loads exactly as it was left |

## Deliberate fakes, and what they cost later

- **Parts come from the console.** Cost: a sourcing spec, after landing.
- **A fold ages every core part equally.** Real wear would load parts
  differently per fold. Cost: per-bay fold weights in the ini.
- **One stress exists** (star heat). The flight-feel IOU is paid; others
  (a hard landing, a heavy-world hover) are not modelled. Cost: an event
  and a bay list each.
- **Symptoms are sound and material swaps**, no geometry: no leaks, no
  sparks, no decals (the lived-in spec's "wear is colour, not geometry").
  Cost: art.
- **Housings are primitives.** Cost: art, with no code change.
- **The hum's reactor voice is a pitch offset**, not a resynthesis. Cost: a
  voice per part in `FShipHumVoice`.

## Implementation outline

### New files

- `Ship/ShipParts.h/.cpp` -- `EShipBay`, `EShipRating`, `FShipPartSpec`,
  `FShipRatings::Stock()`, `ShipParts::RatingsOf`, `ShipParts::Validate`
  (decision 7's rules), `ShipParts::Effective`, `ShipBay::DrawKey`;
  `FShipPartState`, `FShipBayState`, `FShipLoadoutState` (slice 1).
- `Ship/ShipPartCatalogue.h` -- `UShipPartCatalogue`, the soft-pointer list
  (decision 5) (slice 1).
- `Tools/ship_parts.json`, `Tools/setup_ship_parts.py` (slice 1).
- `Content/Ship/Parts/DA_*.uasset` and `DA_ShipCatalogue` -- generated by
  the script (slice 1).
- `Ship/ShipBayHousing.h/.cpp` -- `AShipBayHousing`: a mesh, an
  `UInteractableComponent`, a tag; asks the subsystem, stores nothing
  (slice 2).
- `Ship/ShipSaveGame.h/.cpp` -- `UShipSaveGame` (slice 3).
- `Ship/ShipWear.h/.cpp` -- pure: `WearSeed`, `DrawLife`, `StartingHistory`,
  `Advance`, `Scales`, the guarantees; `Ship/ShipWearConfig.h/.cpp` -- the
  ini (slice 4).
- Tests: `ShipPartsTest.cpp`, `ShipLoadoutTest.cpp` (slice 1);
  `ShipBayHousingTest.cpp` (slice 2); `ShipSaveTest.cpp` (slice 3);
  `ShipWearTest.cpp`, `ShipWearCorpusTest.cpp`, `ShipSymptomsTest.cpp`
  (slice 4).

### Changed files

- `Ship/ShipModuleDataAsset.h` -- `Bay`, `Words`, `Ratings` (slice 1);
  `Character` (hum fundamental, slice 2). A header change:
  `./rebuild.sh --force`, then `check_blueprints.py`.
- `Ship/ShipSubsystem.*` -- bays in place of `InstalledModules`
  (`GetInstalledModules` kept as a read-only view for the tests that use
  it); `FitPart`, `RemovePart` (aux only), `FindPart`, spares, `AddLoad`/
  `RemoveLoad` (decision 8), `InstallModule`/`RemoveModule` retired; the
  `CatalogueAsset` config line; rated values derived;
  `GetChartRangeLy`/`GetWindingWant` non-static and `GetChargeSeconds`;
  the commands (slice 1);
  save and restore (slice 3); ages at the fold's arrival, the stress, the
  scales in `ApplyAllocation`, repair (slice 4).
- `Core/DeepSpaceGameMode.*` -- the six stock parts, through `FitPart`;
  from slice 3 hands the loadout to the subsystem instead of fitting it.
- `Config/DefaultGame.ini` -- `[/Script/DeepSpace.ShipSubsystem]`
  `CatalogueAsset` (slice 1).
- `Tests/StockShip.h` -- fits by bay. The hog tests (`LampPanelsDimTest`,
  `ShipSkyTest`) use `AddLoad`/`RemoveLoad` (decision 8).
- The tests that read the four CVars raw or call the static getters
  (decisions 2 and 6): `HumComponentTest`, `NavScreenTest`,
  `ChartLayoutTest`, `SliceChooseTest`, `JumpWindsTest`, `ShipJumpTest`;
  and `SliceChooseTest`/`JumpWindsTest` for `InstallModule`;
  `ShipScreensAgreeTest` for the readout (decision 9). All slice 1, their
  numbers unchanged.
- `UI/EngineeringConsoleWidget.*`, `Ship/ShipConsole.*` -- nameplates;
  DRAWN and SPARE removed (slice 1).
- `UI/NavigationWidget.cpp` -- the range from the instance.
- `Ship/ShipHumComponent.cpp` -- `GetWindingWant()` from the instance
  (slice 1).
- `Ship/ShipLightingSubsystem.cpp` -- the lights' wear scale and the
  buzz symptom (slice 4).
- `Ship/ShipHum*` -- the reactor's fundamental (slice 2); the symptom
  voices (slice 4).
- `Player/DeepSpaceCharacter.*` -- the housing's E and the framed shot
  (`FramedHousing` in `PlaceCamera`, `MarkCameraCut`) (slice 2); the
  `Completed`/`Canceled` bindings for `IA_Interact` and the tap-or-hold
  decision at a housing (slice 4).
- `Tools/hauler_layout.py`, `placement.py`, `props.py`, `build_hauler.py`,
  `verify_level.py`, `validate_hauler.py`, `test_placement.py` -- `BAYS`
  (slice 2).
- `Config/DefaultGame.ini` -- `[/Script/DeepSpace.ShipWearConfig]` (slice 4).

### CVars and commands

| Name | Default | Slice |
|---|---|---|
| `ds.Nav.RangeLy`, `ds.Nav.ChargeSeconds`, `ds.Nav.WindingWant`, `ds.Drive.Response` | -1: the fitted part's rating (decision 6) | 1 |
| `ds.Ship.Install <part>`, `ds.Ship.Spares [give <part> \| clear]`, `ds.Ship.Describe` | commands | 1 |
| `ds.Ship.NewShip` (and `-NewShip`) | command | 3 |
| `ds.Ship.RepairSeconds`, `ds.Ship.TapSeconds` | 3 s, 0.3 s | 4 |
| `ds.Ship.Wear.Age <bay> <jumps>`, `ds.Ship.Wear.Symptom <bay> [symptom]`, `ds.Ship.Wear.Reload` | commands, for tests and playtests | 4 |

## Tests

All headless, pure where possible, siblings with no children (a test path
with children becomes a group and silently stops running), and **each
proven able to fail with `Tools/mutate.sh`** before it is trusted, then a
rebuild (the mutant was the last thing built).

**Slice 1:**

- `DeepSpace.Ship.Parts.StockIsToday` -- pure: `FShipRatings::Stock()` is
  1400 W, 300 W, 450 W, 2 km/s^2, 380 W, 45 s, 3 notches/s, 12 ly; on
  `StockShip::Install`: supply 1400, rest load 1370, the chart's radius 12
  ly, a full charge in 45 s at full feed.
- `.Contract` -- JSON, assets and `Stock()` agree.
- `.CatalogueResolves` -- in a bare world with no game mode, every JSON id
  resolves through `FindPart`, and an id not in the JSON does not.
- `.OnePerBay` -- a fit swaps; the displaced part is a spare; swapping back
  restores every rating exactly; a second part cannot draw in one bay; a
  part whose `Bay` is `None` is refused and displaces nothing.
- `.EmptyBayIsStock` -- a bare world's reactor, wants and draws are today's.
- `.RatingsFollowParts` -- the twin core gives 1800 W and a jump that winds
  at full with the lights whole; the quick-lever drive gives a response of
  4.5 notches a second, and from 0.1 c an X brings the ship to rest in
  less time than the stock drive does, measured through the flight state.
  (If the sign-off keeps the long array instead: under a pinned universe
  seed, the test itself finds a system between 12 and 20 ly of the start
  and asserts it is in `GetChart()` with the array fitted and not with the
  stock one; and, since the chair shows six rows, it asserts on the
  chart widget's rows, not on `GetChart()`, whatever the chart change
  that sign-off brings.)
- `.CatalogueRules` -- over every row: every combination whole at rest;
  every upgrade at least as open as stock on every axis of its bay,
  draw included (decision 7's table); no dominance within a bay, with
  equal numbers not dominating; core parts rate only their bay; aux parts
  rate nothing and want nothing at rest; no two aux parts alike.
- `.NameplatesAreFacts` -- decision 9's format per bay, read from the
  widget's rendered text and asserted against each part's own rating; no
  forbidden word, no `%`, no empty aux line, never the charge; **no total
  drawn and no headroom anywhere on the console**; no CVar moves a plate.
- `.Commands` -- `ds.Ship.Install` and `ds.Ship.Spares` through the console
  manager in a test world.
- `.CVarOverrides` -- `-1` reads the part; `15` reads 15.
- `.StateRoundTrips` -- `FShipLoadoutState` through the struct serialiser;
  and a state whose bays are listed in a different order restores the same
  loadout (bays keyed by name).
- Unchanged in their numbers and green: `JumpCanWindAtFullSpeed`,
  `ShipBrownOut`, `ShipPowerAllocation`, `LampPanelsDim`, `JumpWinds`,
  every `StockShip` test.

**Slice 2:** `test_placement.py` and `validate_hauler.py` (each housing in
its room, reachable standing, outside every seat's reach and keep-clear,
not in the crawlway, tagged once); `verify_level.py` (tags, one per bay;
the access panel a separate component); `DeepSpace.Ship.BayHousing.FitsSpare`
(spawned before `World->BeginPlay()`, as every widget-bearing or traceable
actor must be: E fits the spare on the press, the ratings change, the
camera cut is marked, the camera sits at `GetFramingView()`, E again fits
the next spare and cycles back, a movement input cuts back and marks a
cut); `.AnyoneAboard` (a second character fits at the same housing);
`.NotAStation` (the body is not seated, the pointer not captured);
`DeepSpace.Playtest.WalkAndFit` (through `IMC_Default`: walk from the
galley to engineering, fit the twin core).

**Slice 3:** `DeepSpace.Ship.Save.RoundTrip` (every field of ruling 9
saved and loaded into a fresh world equal, spares' own state included);
`.WorldLevel` (the slot is the seed's); `.UnknownPartFallsBack`;
`.NeverInTransit`; `.SavedAtEvents` (a fit, an arrival, a weights change
and a lights change each write; ten minutes of cruise writes nothing;
repair does not exist yet and is slice 4's `.RepairSaves`); `.NewShip`.

**Slice 4:**

- `DeepSpace.Ship.Wear.KnownValues` -- the inverse CDF at fixed U, to 1e-9;
  the seed derivation pinned.
- `.Deterministic` and `.WorldLevel` -- two subsystems on one root and one
  event log agree on every symptom.
- `.NeverWithTime` -- an hour each of cruise, drive, idle, landed (landing
  (c)'s LANDED state, merged before slice 4 by the ordering rule) and
  parked at a floor: every age unchanged.
- `.PerFold` -- every core part ages exactly one per interstellar arrival
  and `InSystemFoldWeight` per in-system arrival; aux and spares never.
- `.StarHeatOncePerVisit` -- ten minutes at a star's floor adds 10 once;
  leaving beyond 2 F and returning adds 10 again; with
  `ds.Flight.StarFloorRadii 2` the band moves with the floor; a ship placed
  inside the band by `ds.Sky.Goto` is not charged; the arming cue (the
  hull tick at both housings, the `Veil` pulse) fires once at arming and
  not while staying.
- `.OneSymptomPerPart` -- the clock stops while a symptom is present.
- `.FloorsHold` -- every combination of symptoms: every scale at least
  0.85; boosters never below `StarvedBoosterThrust` when starved.
- `.NeverTouches` -- every symptom: the charge time, winding want, starved
  rate, cone, tops, range, reactor rating, wants and every nameplate are
  bit-identical.
- `.SymptomIsWhereItActs` -- decision 17's rule over every character row,
  and the worn drive's click at the drive housing's position on each step
  of an ease.
- `.RepairClears` and `.UpgradeClears` -- the symptom gone, a fresh life
  no shorter than 0.25 λ, the housing replaced and its panel closed.
- `.RepairSaves` -- a repair writes the save.
- `DeepSpace.Ship.Parts.SpareKeepsItsState` -- decision 10's three rules.
- `DeepSpace.Ship.BayHousing.TapOrHold` -- decision 13's table, row by
  row, through the `IA_Interact` bindings.
- `.HistoryNotSymptom` -- a housing's material is faded for an original
  part, standard for a new one and replaced once repaired, and does not
  change when a symptom arrives or clears; the panel is ajar exactly while
  a symptom is present.
- `DeepSpace.Ship.Wear.Corpus` -- over 10,000 seeds: stock parts starting
  symptomatic 32 ± 3%; the distribution of starting symptoms per ship
  reported.
- `.LopsidedCurve` -- a scripted career over 1,000 seeds, built from
  **test-only fixture parts, one per core bay, each of which passes
  `ShipParts::Validate` together with the real catalogue** (so the curve
  asserted is one a legal catalogue can produce), fitted in bay order,
  reactor first: stock for 20 jumps, then one upgrade every 20 jumps for
  120 jumps (six upgrades, one per core bay), then none for 400. The
  career **repairs every symptom at the fold after it arrives**, so no
  clock stays stopped. Phases: early is jumps 0-40, mid 40-140, late
  140-540. **A symptom counts in the phase of the fold it arrives at;
  symptoms present at world start are reported separately and counted in
  no phase.** Symptoms per 100 jumps are reported per phase and asserted
  early > mid < late; and, at the measured folds per hour (decision 15),
  the late phase's rate is asserted at no more than one symptom per
  `MinHoursPerSymptom` (10) hours.
- `DeepSpace.Playtest.RepairByHand` -- walk to a symptomatic housing, hold
  E for 3 s, the symptom gone; release at 2 s, nothing changed; at a
  symptomatic housing with a spare, a tap fits the spare and the removed
  part keeps its symptom.

**Tests that pin today and will move, all in slice 1, their numbers
unchanged:** `JumpWindsTest` and `ShipSkyTest` read `GetInstalledModules`
(kept as a view); `ShipPowerStateTest` books draws as
`LifeSupport`/`Lights` (pure, left alone: the power state does not care
what a key means); `HumComponentTest`, `NavScreenTest`, `ChartLayoutTest`,
`SliceChooseTest` and `JumpWindsTest` read the four CVars raw (decision 6);
`ShipJumpTest` and `JumpWindsTest` call the static getters (decision 2);
`JumpWindsTest` and `SliceChooseTest` call `InstallModule`, and the hog
tests `InstallModule`/`RemoveModule` (decision 8); `ShipScreensAgreeTest`
asserts the removed readout changes (decision 9). Each change goes in the
commit that makes it, with the reason in the message.

## The four slices

### Slice 1: the upgrade seam

`ShipParts`, the catalogue JSON and its script, the catalogue asset and
`FindPart`, the six stock parts and two upgrades (`Reactor.TwinCore` 1800
W, and `Drive.QuickLever` 4.5 notches a second, or the long array if the
sign-off keeps it), bays and spares in the subsystem, rated values derived,
the lights clash resolved, the CVar sentinels and the tests that read them,
the aux slot rules and `AddLoad`, nameplates on the engineering console
with DRAWN and SPARE removed, the commands, the plain state, and the
amendment of the landing plan (*Seams*).

**Done when:** `./build.sh` and `./test.sh` are green with slice 1's tests,
each mutate-proven; `check_blueprints.py` passes; `strings -a
Content/Blueprints/BP_DeepSpaceGameMode.uasset | grep -E
'DA_(Lights|LifeSupport|Sensors)\b'` finds none of the old asset names and
finds the six `Content/Ship/Parts/` stock parts (the Blueprint's saved
`StartingModules` overrides C++); every pre-existing test passes with its
numbers unchanged, the files listed under *Tests that pin today and will
move* changed only in where they read a value; in a headless run,
`ds.Ship.Install Reactor.TwinCore` makes `GetReactorOutput()` 1800 and
`ds.Ship.Install Reactor.Stock` makes it 1400 again, and `ds.Ship.Install
Drive.QuickLever` makes the drive's response 4.5 and `ds.Ship.Install
Drive.Stock` makes it 3 again, each command restoring its own bay's number;
`.NameplatesAreFacts` passes on the widget's rendered text; the landing
plan's affected steps are re-planned against the merged tree. Nothing a
player walks past has changed except the console's list and the removal
of DRAWN and SPARE.

### Slice 2: the bays and the install act

`BAYS`, the housings with their access panels, the level rebuild,
`AShipBayHousing`, fit at the housing on the press with the framed shot and
the camera cut (the character's housing path), the reactor parts' hum
fundamental. Starts after landing (c) is merged (*Seams*).

**Done when:** the layout and level checks pass; the slice 2 tests are
green and mutate-proven; the developer walks to each housing, fits the twin
core in engineering and hears the drone drop, and judges the placements.

### Slice 3: the save

`UShipSaveGame`, the restore in `OnWorldBeginPlay`, the events that save,
`ds.Ship.NewShip`.

**Done when:** the slice 3 tests are green and mutate-proven; the developer
fits the long array, flies somewhere, quits, reopens, and the ship is where
it was with the array fitted and the weights as set; `-NewShip` gives the
opening placement.

### Slice 4: wear events

`ShipWear`, the ini, seeded history, ages at folds (in-system weighted)
and the star stress with its cue, symptoms and their character where their
deficits act, the deficits and floors, tap-or-hold at a housing and hold-E
repair, the housings' history and panels, the CLAUDE.md amendment.

**Done when:** the slice 4 tests are green and mutate-proven; a playtest
has measured folds per hour and `.LopsidedCurve`'s hour gate passes at
that rate with λ and `InSystemFoldWeight` as set (decision 15); the corpus
and the career are reported to the developer; a fresh universe's starter
ship has its seeded symptoms, which the developer finds by walking the ship
and repairs by hand; `ds.Ship.Wear.Symptom` produces each bay's symptoms
for judging by ear and eye.

## Parallel tracks and file ownership

Each track is a worktree under `.worktrees/`, building with `./build.sh`
and testing with `./test.sh` behind `Tools/ue_lock.sh`. Agents are briefed
with the machine's cap: at most 3-4 workers, `nice -n 19`. **Every changed
file has exactly one owner per slice.**

| Track | Slice | Owns | Waits on |
|---|---|---|---|
| **D: parts data** | 1 | `Ship/ShipParts.*` (**first**, as the header S builds on), `Ship/ShipPartCatalogue.h`, `Ship/ShipModuleDataAsset.*`, `Tools/ship_parts.json`, `Tools/setup_ship_parts.py`, `Content/Ship/Parts/*` (with `DA_ShipCatalogue`), `BP_DeepSpaceGameMode` (through the script only), `Tests/ShipPartsTest.cpp` | -- |
| **S: subsystem and console** | 1 | `Ship/ShipSubsystem.*`, `Core/DeepSpaceGameMode.*`, `Config/DefaultGame.ini` (the `CatalogueAsset` line), `Ship/ShipHumComponent.cpp`, `Tests/StockShip.h`, `UI/EngineeringConsoleWidget.*`, `Ship/ShipConsole.*`, `UI/NavigationWidget.cpp`, `Tests/ShipLoadoutTest.cpp`, the hog tests (`LampPanelsDimTest.cpp`, `ShipSkyTest.cpp`), `Tests/HumComponentTest.cpp`, `NavScreenTest.cpp`, `ChartLayoutTest.cpp`, `SliceChooseTest.cpp`, `JumpWindsTest.cpp`, `ShipJumpTest.cpp`, `ShipScreensAgreeTest.cpp`, and the landing plan's amendment | D's `ShipParts.h`; landing (b) not started |
| **L: layout** | 2 | `Tools/hauler_layout.py`, `placement.py`, `props.py`, `build_hauler.py`, `verify_level.py`, `validate_hauler.py`, `test_placement.py` | landing (c) merged |
| **H: housings** | 2 | `Ship/ShipBayHousing.*`, `Ship/ShipHum*` (fundamental), `Player/DeepSpaceCharacter.*` (the housing's E and framed shot), `Ship/InteractableComponent.*` if its prompt needs a change, `Tests/ShipBayHousingTest.cpp`, the Playtest test | landing (c) merged; L's tag names (agreed first) |
| **P: persistence** | 3 | `Ship/ShipSaveGame.*`, `Ship/ShipSubsystem.*`, `Core/DeepSpaceGameMode.*`, `Tests/ShipSaveTest.cpp` | slice 2 merged |
| **W: wear core** | 4 | `Ship/ShipWear.*`, `Ship/ShipWearConfig.*`, `Config/DefaultGame.ini`, `Tests/ShipWearTest.cpp`, `Tests/ShipWearCorpusTest.cpp` | slice 3 merged (and so landing (c)) |
| **Y: symptoms** | 4 | `Ship/ShipSubsystem.*`, `Ship/ShipHum*`, `Ship/ShipLightingSubsystem.*`, `Ship/ShipBayHousing.*`, `Player/DeepSpaceCharacter.*`, `Sky/ShipSky.*` (the arming cue's `Veil` pulse), `Tests/ShipSymptomsTest.cpp`, `Tests/ShipBayHousingTest.cpp` (`TapOrHold`, `HistoryNotSymptom`) | W's `ShipWear.h` |

Merge order: D's header, then D and S together into slice 1; L then H for
slice 2; P alone for slice 3; W's header, then W and Y for slice 4.
`ShipSubsystem.cpp` has one owner per slice (S, P, Y in turn), and every
other track reaches it through pure headers.

## Documentation changes

- **CLAUDE.md, *Screens, the pointer, and power*: the power paragraph is
  amended.** "Nothing in the model changes on its own with time" gains its
  second sanctioned exception (after landing's starved sink): the wear
  clock, and why it is bounded -- only at a fold's arrival and at a chosen
  stress's arming, one symptom per part with the clock stopped, every
  deficit floored and off every top, the charge, the range, the wants and
  the reactor, and every symptom cleared for free whenever the player
  chooses.
- CLAUDE.md: a new section, *Parts, bays and wear* -- the bays and what
  each rates, an empty bay reads stock, the catalogue JSON and its script,
  decision 7's rules, the aux rules, nameplates, the housings and the
  install act, the save, the clock, symptoms and repair; the *Architecture*
  list (`ShipParts`, `ShipWear`, `ShipBayHousing`, `ShipSaveGame`); the
  tunables table (the four CVars' `-1`, `ds.Ship.RepairSeconds`,
  `ds.Ship.TapSeconds`, the wear ini with `InSystemFoldWeight`, the
  catalogue's ini line); the playtest console (`ds.Ship.*`); and, in
  *Screens, the pointer, and power*, that the engineering console shows
  nameplates and no total drawn.
- `ShipPowerState.h`: the class comment gains a line -- the reactor's
  output comes from the reactor part, and time-driven change is not in
  here; it lives in `ShipWear`, and why.
- `ShipSubsystem.h`: the reactor's "becomes a module later" comment and the
  want constants move to `FShipRatings::Stock()`, with the 2026-09-26
  reasoning kept beside them.
- ADR 0003, amended: the ship's parts, spares and wear state live in
  `UShipSubsystem` over pure cores, and are saved from there,
  world-level.
- A new ADR 0009: *The ship's save is world-level and event-driven* (slot
  by seed, saved at events, never asked).
- The navigation spec's fake ("range ... a different number") and the
  flight-feel spec's star-heat fake: marked paid, with a pointer here.
- The lived-in spec: housings wear its materials but are written by the
  bays, not the dressing.

## Risks

- **`BP_DeepSpaceGameMode` keeps the old list.** Its saved
  `StartingModules` overrides C++, and `StockShip.h` reads it. Mitigated:
  the script writes it, `.StockIsToday` runs on `StockShip::Install`, and
  `strings` on the asset is in the done-when's check.
- **Python may have no factory for a data asset**, as it had none for an
  input action. Fallback: duplicate an existing module asset and rewrite
  its fields, the `IA_Look` trick.
- **Renamed `ModuleId`s** strand anything that looked a module up by its
  old id. Only tests do (`Test.*` hogs, which are made in code); a grep in
  slice 1 confirms.
- **Python may have no factory for the catalogue asset either.** The same
  fallback: duplicate and rewrite. The catalogue is also one more asset the
  script can leave stale; `.Contract` and `.CatalogueResolves` compare it
  with the JSON.
- **The quiet optimum** (decision 17). Not avoided by design: every
  symptom is a defect and a repair removes it, so a whole ship sounds
  better. The ruling accepts a small optimum; the playtest judges whether a
  click and a 10% softer ease make a whole ship feel like a target. The
  remedy is quieter symptoms or a lower deficit, never a meter.
- **The cadence is in jumps, and nobody knows jumps per hour.** If late
  play folds often, one symptom per 22 jumps reads as a schedule. Slice 4
  is gated on a measured folds-per-hour figure and an hour assertion
  (decision 15); until then in-system folds weigh 0.25.
- **Symptom sounds carried to the helm may be too quiet, or too present.**
  The attenuation that lets the drive's click reach the helm from
  engineering is a tuning question for the playtest; the rule that it
  sounds from the housing is not.
- **If the sign-off keeps the 20 ly array:** a 20 ly chart scans about 4.6
  times the volume, and the first ask after a fold may hitch. That choice
  adds `DeepSpace.Ship.Parts.ChartAskCost`, which times the first
  `GetChart()` after a fold at 20 ly under a pinned seed and reports it,
  failing above one frame at 60 Hz. With the quick-lever drive as the
  example, the chart is unchanged and there is nothing to measure.
- **Two writers of a housing's material.** Mitigated by never tagging
  housings `Dress.Wear` and by `verify_level.py` checking it.
- **Save format drift.** Versioned from the first write, ids not pointers,
  unknown parts fall back to stock.
- **Collision with landing's slices** on the subsystem, character and
  layout. Serialised, never side by side (*Seams*).

**Ruled on the plan, 2026-09-27:** the HUD's `SPARE` line goes with the console's, for the same reason (sign-off 11).

## Decisions needing sign-off

Each is expensive to reverse, goes past a ruling, or is a choice a
reasonable person could make differently.

1. **The aux-slot mitigation (ruling 3)**: aux parts add verbs, never a
   rating; want nothing at rest and draw off the top only while used; one
   of a kind; no combined readout; an empty slot is never shown; aux parts
   never wear. *Alternatives:* aux parts that raise core ratings (the
   knapsack the developer was warned of); aux draws as a new consumer in
   the split (a new knob); aux slots shown when empty. *Recommended:* as
   specified. *Cost to change later:* low in code; each rule relaxed
   reopens the risk.
2. **The specific deficits (ruling 6)**: drive ease 0.9, boosters'
   acceleration and ease 0.9, lights' brightness 0.9; **no deficit** on the
   reactor, life support or sensors; a floor of 0.85 on any one rate with
   the power model's quarter-thrust floor never breached; **no deficit on
   the jump's charge**, winding, starved rate or cone, any top, the range,
   any want or the reactor's rating. **Every deficit's symptom is
   perceived where and when the deficit acts, and sounds from its
   housing** (decision 17's rule, tested), which moved the drive's fold
   stutter to the reactor and made the lights' symptom a lasting buzz.
   *Alternatives:* the charge at 50 s (the ruling's own risk case: a
   longer wait the game imposes); 0.8 deficits; deficits on all six bays
   (each of the other three hits a ceiling, the split or survival);
   character only (the rejected "different, not worse"); no deficit on the
   lights (their dimming is the hardest to hear). *Recommended:* as
   specified. *Cost to change later:* low (numbers); the "never touches"
   list is the guarantee.
3. **Every combination of parts is whole at rest under the stock reactor,
   and no part needs another.** It forbids, for instance, a brighter lights
   part that wants 350 W. *Alternatives:* wholeness per loadout (parts may
   need a bigger reactor: a dependency to solve); no rule. *Recommended:*
   the rule. *Cost to change later:* low to relax; expensive to impose on a
   grown catalogue.
4. **Upgrades are at least as open as stock on every axis of their bay,
   the draw counted as an axis (lower is more open) and every want too,
   and no two non-stock parts in a bay dominate each other; parts with
   equal numbers do not dominate, so character-only variants are legal**
   (ruling 2's "not a ladder" in arithmetic; decision 7's table). The
   honest consequence: the reactor and life-support bays hold one
   numerically different part each, then variants in character; a bay the
   catalogue leaves without a part is refreshed by repair alone.
   *Alternatives:* free trade-offs (a part better in one way and worse in
   another); a ladder; draw not an axis (then a lower-draw part would be
   either illegal or a strict upgrade outside the rules). *Recommended:* as
   specified. *Cost to change later:* low.
5. **With the twin core fitted, the stock split no longer bites**: the jump
   winds at full with everything whole. *Alternatives:* a smaller example
   upgrade (1600 W: the split still bites, less); keep it and let future
   aux verbs reopen the trade-off. *Recommended:* 1800 W as ruled. *Cost
   to change later:* a JSON line.
6. **An empty core bay reads the stock part**, so bare test worlds are
   unchanged; a core part cannot be removed, only swapped.
   *Alternatives:* bare worlds fit stock parts automatically; an empty
   reactor bay is 0 W. *Recommended:* as specified. *Cost to change
   later:* low.
7. **The lights clash**: one Lights part owns both the fittings' draw and
   the consumer's want; draws booked by bay (`Bay.Lights`); part ids
   `<Bay>.<Name>`; consumer ids unchanged. *Alternatives:* fold the draw
   into the want (changes the split at stock, against ruling 1); rename the
   consumer (a level rebuild). *Recommended:* as specified. *Cost to change
   later:* low.
8. **Parts authored from `Tools/ship_parts.json` by script, held by a
   contract test.** *Alternatives:* hand-made assets; ini rows.
   *Recommended:* the JSON. *Cost to change later:* low.
9. **Four CVars default to -1, "the fitted part's"**, overriding it when
   set; the settled 45 s and 380 W move into the catalogue.
   *Alternatives:* a multiplier; retire them. *Recommended:* the sentinel.
   *Cost to change later:* low.
10. **Nameplates**: one line per fitted part, one number read from the
    part's own rating in decision 9's per-bay format (reactor watts, drive
    response in notches a second, boosters' acceleration, lights' want,
    life support's draw, the array's range; aux none), never moved by a
    CVar or a symptom; words as character and history; no symptom, no
    empty aux line; forbidden words tested. *Alternatives:* the drive shows
    its top (a ship fact no part rates, the same on every drive part, and
    wrong under `ds.Drive.Top`); the drive shows no number; show the charge
    time on the drive (a clock); list symptoms on the console (a job list).
    *Recommended:* as specified. *Cost to change later:* low.
11. **The console's `REACTOR` / `DRAWN` / `SPARE` readout loses DRAWN and
    SPARE in slice 1, and its REACTOR line becomes the reactor's
    nameplate** (and `AShipConsole::GetReadout` returns that line). This
    brings the console into line with the lived-in spec's approved decision
    11 ("Nothing anywhere in the ship shows a total drawn"), which parts
    would otherwise break twice over: a bigger reactor reads as a bigger
    SPARE waiting to be spent, and an aux verb in use makes SPARE the aux
    slots' combined budget (aux rule 4). `ScreensAgree` moves its "the
    readout changed" check to the laptop's lights row. *Alternatives:*
    leave it (contradicts an approved ruling); reduce it to `SPARE z W`
    (still a headroom). *Recommended:* remove. *Cost to change later:* one
    line.
12. **Part names and words are placeholders** ("Twin-core reactor",
    "Fold drive"), since who builds ships is the vision's open
    question 4. *Alternatives:* bare functional names only. *Recommended:*
    placeholders with character. *Cost to change later:* JSON lines.
13. **Housing placements** (decision 12's table), to be walked.
    *Alternatives:* all housings in engineering (one room becomes the
    engineer's). *Recommended:* each part in the room it serves. *Cost to
    change later:* layout data and a rebuild.
14. **The install act and its input** (decision 13's table): at a well
    housing E fits on the press and cuts to the housing; movement cuts
    back; further E presses cycle the spares in catalogue order. At a
    symptomatic housing the gesture decides: released within 0.3 s fits
    the spare (the removed part keeps its symptom), held 3 s repairs,
    released between does nothing. `IA_Interact` gains `Completed` and
    `Canceled` bindings in the character; its trigger is unchanged, and E
    everywhere else stays on the press. *Alternatives:* at a symptomatic
    housing offer only the repair, and fitting once it is well (simpler
    input, but an upgrade then first asks for a repair of the part being
    thrown out); a separate hold action with a Hold trigger in
    `setup_flight_input.py` (a second binding for one key); a short swap
    animation (a wait); a menu of spares at the housing. *Recommended:* as
    specified. *Cost to change later:* low.
15. **The save: slot per universe seed; saved at a fit, a repair, an
    arrival, a weights or lights change, and on quit; never in transit;
    never asked.** *Alternatives:* a manual save; a timed autosave (a
    clock); a player-profile slot. *Recommended:* as specified. *Cost to
    change later:* medium once saves exist.
16. **One Weibull life per part, drawn once as an age; k = 3, λ = 150
    jumps; clamped to [0.25 λ, 2.5 λ].** *Alternatives:* a per-fold roll;
    k below 1 for stock parts (early-life faults). *Recommended:* as
    specified. *Cost to change later:* low (ini).
17. **Every fold ages every core part: an interstellar fold by one, an
    in-system fold by `InSystemFoldWeight`, 0.25 until the cadence is
    measured.** Both are jumps, so ruling 5's "per jump" stands; the weight
    keeps the in-system jump, the player's way round a system, from being
    the late game's main source of symptoms. *Alternatives:* every fold at
    one (ruling 5 read literally; the first draft's recommendation); an
    in-system fold ages nothing (interstellar only). *Recommended:* 0.25
    until slice 4's measurement, then whatever satisfies the hour gate.
    *Cost to change later:* low (ini).
18. **The star-heat stress**: when the ship comes within 1.1 x the star's
    floor altitude (`FloorFor`), once per visit (re-armed beyond 2 x),
    adds 10 jumps to sensors and life support; the band moves with
    `ds.Flight.StarFloorRadii`; a ship placed or restored inside the band
    is not charged; **arming is marked in the world**, a hull tick at the
    two housings and one shimmer of the glass, so the first visit teaches
    it and later approaches are known choices. Since the drive brings a
    ship at a star to rest on that floor, every approach to the floor
    arms it; the cue is what makes that a choice rather than a hidden
    charge. *Alternatives:* arm only on a deliberate act below the default
    rest (the lever held against the cap), with no cue; per unit of time
    spent there (a rate); other bays; no cue (a silent charge).
    *Recommended:* as specified. *Cost to change later:* low.
19. **A worn part never gets worse**: one symptom per part, and its clock
    stops while the symptom is present. *Alternatives:* symptoms stack (a
    treadmill); the clock runs on (a second symptom queued behind the
    first). *Recommended:* as specified. *Cost to change later:* low.
20. **Repair starts a fresh life.** *Alternatives:* repair keeps the age (a
    rising hazard after every repair: permanent wear). *Recommended:*
    fresh life. *Cost to change later:* low.
21. **Seeded history: stock parts start at Beta(5, 2) of a life; about a
    third start symptomatic** (two of six on an average ship).
    *Alternatives:* a fixed count of starting symptoms; none, and early
    wear from a low-k clock instead. *Recommended:* as specified. *Cost to
    change later:* low (ini).
22. **Aux parts do not wear.** *Alternatives:* they wear, character only.
    *Recommended:* no wear. *Cost to change later:* low.
23. **The amendment to "nothing in the model changes on its own with
    time"**, as worded in *Documentation*, recorded as the second
    sanctioned exception. *Alternatives:* amend the vision instead.
    *Recommended:* CLAUDE.md and the power header, as the landing spec did.
    *Cost to change later:* low; it is words that bound code.
24. **The second example upgrade is `Drive.QuickLever` (response 4.5
    notches a second), not the ruling's "for instance" 20 ly nav array.**
    The chart chair shows the six nearest systems, which sit within six or
    seven light years, so a 20 ly array changes nothing the player sees at
    the chair; the quick lever is felt at the helm on every change of
    notch. The `Sensors` bay still rates `RangeLy`, so a range upgrade
    stays "a different number, not a different model". *Alternatives:*
    keep the 20 ly array as ruled and accept it shows only in
    `ds.Nav.Near`; keep it and change the chart to show beyond six rows
    (a chart design question of its own, and a timing test for the wider
    scan). *Recommended:* the quick lever now; the array when the chart has
    a way to show range. *Cost to change later:* a JSON row.
25. **A part keeps its own wear state wherever it is** (decision 10): a
    spare keeps its age, life and symptom and does not age; refitting it
    resumes that life; only a new part draws a new life at a fit, from the
    bay's next life index; swapping out and back is not a repair, and
    fitting a *different* part clears the bay's symptom (ruling 7).
    *Alternatives:* spares are bare ids and every fit starts a fresh life
    (then two taps with one spare is a free, instant repair that makes
    hold-E pointless); every fit counts as a repair. *Recommended:* as
    specified. *Cost to change later:* medium once saves hold spares'
    state.
26. **A housing's material is history only** (faded for an original part,
    standard for a new one, replaced once repaired) **and a symptom shows
    as the housing's access panel ajar**, so faded never means "needs
    doing" anywhere in the ship. *Alternatives:* faded while symptomatic
    (the first draft: one look with two meanings); a lamp on the housing (a
    warning light). *Recommended:* as specified. *Cost to change later:*
    low.
27. **Slice 4 is gated in hours**: not done until a playtest has measured
    folds per hour and the career test shows no more than one new symptom
    per 10 hours of late play at that rate (`MinHoursPerSymptom`, a
    constant). λ is held until then. *Alternatives:* ship λ = 150 and
    measure afterwards (risks shipping a schedule); gate at a different
    number of hours. *Recommended:* 10 hours, the bottom of the vision's
    "tens of hours". *Cost to change later:* a constant.
28. **Test loads are not parts**: the hog tests' standing draws go through
    a plain `AddLoad`/`RemoveLoad` seam, `InstallModule`/`RemoveModule` are
    retired, and the aux rules on draw and ratings are enforced over the
    catalogue, not at each fit. *Alternatives:* hogs fitted into `Aux1`
    with the aux rules catalogue-only (a standing draw in an aux slot, the
    one shape the rules exist to forbid); `InstallModule` kept as a shim.
    *Recommended:* as specified. *Cost to change later:* low.
29. **Order against landing**: slice 1 before landing (b), carrying a task
    that re-plans the landing steps it invalidates (one writer of the
    boosters want: `ApplyAllocation`, rated plus `HoldWant`); slices 2-4
    after landing (c). *Alternatives:* landing (b) first and slice 1
    rebased on it (no plan amendment, but slice 1 waits on all of landing
    (a) and (b)). *Recommended:* as specified. *Cost to change later:*
    low before either starts.

## Open questions

- **Sourcing.** Where named parts come from once landing exists: salvage on
  desolate worlds, trade on populated ones, and what either costs. The
  vision's open question 4 (who builds ships, what is traded) is blank.
- **Combat damage.** A separate event path per the vision; whether it uses
  the same symptoms and repair, or its own.
- **The jump cooldown** the developer eventually wants (system map spec).
  This spec keeps wear out of it; if a cooldown ever uses wear, it would
  make the fold a thing to ration, which is the tension that question
  starts from.
- **The first aux verb.** A floodlamp for a landed ship is the nearest
  candidate once landing (c) is built.
- **Combat flight as a part.** The boosters bay could carry the combat
  limits and assist-off; or it is its own bay, which changes decision 1.
- **How many parts per bay.** Decision 7 caps the shape, not the count.
  In particular: whether every core bay gets at least one non-stock part
  before slice 4 ships, since a bay without one is refreshed by repair
  alone (decision 16), and the career test runs on fixture parts until it
  does.
- **Late-game cadence in hours.** Slice 4's gate needs a playtest that
  measures folds per hour, interstellar and in-system, before λ and the
  in-system weight are settled (decision 15).
- **How the chart shows range.** A range upgrade means nothing at the
  chair while it shows the six nearest systems. Rows beyond six, a filter,
  or a second page are each a chart design question, and the long array
  waits on it (sign-off 24).
- **How far a symptom carries.** The attenuation that lets the drive's
  click reach the helm from engineering without the ship sounding like a
  workshop is a playtest question.
- **Spares with company aboard.** One ship-wide list is world-level and
  right for shared presence; whether a visitor can bring a part aboard is a
  multiplayer question.
