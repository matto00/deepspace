# DeepSpace — Navigation, the Jump, and Arriving

**Date:** 2026-09-25 (revised the same day after review; see *Review* at the end)
**Status:** Draft — design only, not implemented
**Follows:** Flight and the counter-frame (implemented); the interactable ship
and power (implemented)
**Lands after:** the procgen foundation and the sky (both drafted the same day;
see *Landing order*)
**Builds on:** ADR 0003 (ship state as a subsystem), ADR 0005 (the ship is the
origin), ADR 0006 (exactly one generator), ADR 0007 (chunked positions, seeds
down a hierarchy)
**Governed by:** `docs/vision.md`: *the premise*, *the anti-chore principle*,
*the cruise is when you live in the ship*, *shared presence, not division of
labour*

## Context

The game's premise is one sentence long: *cruise, choose somewhere to go,
jump, arrive.* Today the hauler can do the first word and nothing after it.
The player can sit at the helm, point the ship and set the throttle, and the
stars swing past the glass. There is nowhere to go, no way to choose it and no
way to get there, and every direction looks the same.

Three things this spec needs already exist, or will by the time it lands:

- **`FShipFlightState` already has a jump charge.** `ChargeJumpDrive` winds a
  0..1 value at a rate scaled by the engine's satisfaction, 90 s from cold at
  full feed. The power spec added it on purpose: *"inventing it later would
  mean inventing the consumer later too."* Nothing reads it yet.
- **Procgen says what is out there.** `UUniverseSubsystem` owns the one
  generator. It answers *which systems are near this position*
  (`GetSystemsNear`), *what is this system* (`GetSystem`), and *where are we*
  (`GetCurrentSystem`), and navigation is the one caller of
  `SetCurrentSystem`. Navigation generates nothing.
- **The sky draws what is out there.** `AShipSky` draws the local star at true
  size, the planets, and the neighbouring systems at their true directions,
  from an `FSkySystem`. It redraws when it is handed a new one
  (`SetSystem`), and it hides everything but the background when told the
  ship is between stars (`SetInTransit`). Navigation draws only the one thing
  the sky refuses to: a marker on the star you are steering for.

What is left for navigation is small, and that is the point: **choose a
system, aim at it, go, and tell the universe and the sky that you have
arrived.**

This is a **quick and dirty playable POC**. The aim is a loop the developer
can fly, so that the concept can be tested and picked apart. Where the correct
version would take three sessions, this spec picks the crude one and records
what it fakes and what the fake will cost later (see *Deliberate fakes*).

### What a hyperjump is, in the fiction

The drive **folds space along the line the ship is pointing down, toward a
star.** A fold needs mass at its far end, which gives it three limits, and each
limit is also a mechanic:

1. **You can only jump to a star.** Not to a point in space, and not to
   something you have not charted. This is ADR 0007's *"fast travel gated on
   knowing where you are going"*.
2. **You must be pointing at it.** The fold runs along the ship's own axis,
   so the helm aims the drive. It is the one thing the pilot has to do, and
   the throttle does not come into it.
3. **You arrive short of it.** The fold lets go at the edge of the star's
   well. The jump brings you to a system and never into one, so the approach
   the vision calls *"the moment the scale lands"* is still flown, later, at
   cruise.

Winding the fold takes power, and how much the drive gets out of the split
the player set decides how long the winding takes. That is where the power
split starts to matter.

## Landing order

The three sub-projects drafted on 2026-09-25 land in this order, and this
spec is written against the first two as they are specified:

1. **Procgen.** `GenSeed`, `FGalaxyGenerator`, `FSystemId`,
   `FStarSystemStub`, `UUniverseSubsystem`. Nothing else is usable without it.
2. **The sky.** `AShipSky`, the 10,000 km dome, the true-size sun, the
   neighbour stars, fixed exposure, and the `FSkySystem::FromSystem` adapter
   (written by the sky, since it lands after procgen). Its placeholder system
   and its `ds.Sky.Goto` / `ds.Sky.Approach` teleports are deleted when their
   owners arrive, as its own spec says.
3. **Navigation, pass A: the loop.** The state machine, the jump, the HUD,
   the course marker, the streaks, and a console chart.
4. **Navigation, pass B: the chart chair.** The nav screen in the starboard
   cockpit seat.

The lived-in ship (shipdetail) is independent of all of this, but it edits
`hauler_layout.py`, `props.py` and `build_hauler.py`, as pass B does. Whichever
of the two lands second rebases, and their editor windows are serialised like
every other editor step.

**Rejected: navigation before the sky, with a throwaway sun.** The first draft
planned its own `Primary` (an exaggerated sun) and `CatalogStars` (the
neighbours), and it could land first that way. Without the sky, though, a jump
changes the ship's position and nothing that can be seen, because the only
things that move are the near-field motes. A playtest of that loop would test
a teleport with streaks. Building a sun and a neighbour field that the sky
deletes a day later, just to make that playtest possible, is a poor trade.
The code tolerates a missing `AShipSky` (null checks, so tests run without
one), and that is the only concession.

## Goals

- A **jump** that is an operation on the flight state, decided by a small pure
  state machine, `FShipNavState`, that runs headless.
- A jump you **set up and walk away from**, the way the throttle already works.
- **Everything the pilot needs at the helm**: the bearing to the course in
  words on the HUD, a mark where the ship's nose is, and the marker on the
  star through the glass. The chart chair is visited once per jump, to choose
  and engage, and never to check.
- **Arrival that looks different**, by telling the sky the system changed.
  The sky does the drawing.
- A **nav screen** in the cockpit (pass B), a `UShipScreenWidget` subclass
  built in C++, that shows where you are and what you can reach, and has a
  single commit control. Until it exists, the same operations are console
  commands.
- **Every tunable is a console variable**, so a playtest moves numbers
  without a rebuild.
- Most of it covered by headless automation tests, the way flight and power
  are.

## Non-goals

These must not be designed in passing:

- **Generating anything.** The systems, their positions, their stars and their
  names are procgen's (ADR 0006). Navigation holds ids and asks.
- **Drawing anything but the course marker and the streaks.** The sun, the
  planets, the neighbours, sunlight through the glass and exposure are the
  sky's.
- **Planets, approach, landing, in-system travel, the frame handoff.** The jump
  puts you at the edge of a system, and cruise at 200 m/s will not carry you
  in. The sky's open question 5 (*nobody owns in-system travel*) is still
  open, and this spec does not take it.
- **Fuel, range upgrades, jump cost by distance, an economy of any kind.**
- **Proper names, lore, and who charted these stars.** Open questions 3 and 4
  in the vision. Procgen's names are used as they come.
- **A galaxy map.** The chart shows what is near. Nothing shows the whole.
- **Save/load.** Every session starts at home (see fakes).
- **Autopilot.** Nothing but the pilot turns the ship. This is the flight
  spec's rule and it is kept.
- **Sound, post-process, or new material authoring**, beyond assigning
  existing materials.

## Decisions

### 1. Navigation consumes procgen; it owns no universe and no "current system"

Navigation's only view of the universe is `UUniverseSubsystem`:

| Navigation needs | It asks |
|---|---|
| where the ship is | `GetCurrentSystem()`, which is procgen's, and nobody else's |
| what it can reach | `GetSystemsNear(ShipPosition, RangeLy × CmPerLightYear)` |
| where the course is | `GetSystem(PlottedId).Stub.Position` |
| the class and name for a row | the `FStarSystemStub` it got back |
| to record an arrival | `SetCurrentSystem(PlottedId)`, from exactly one place |

A course is an **`FSystemId`**, procgen's type. That is the one piece of
universe data the ship holds, and it holds it because a course is something
the *ship* has, in the way it has a throttle setting. Everything else is asked
for fresh, every time, and never stored (ADR 0003, and ADR 0007's *a cache,
never an authority*).

This needs one sentence of the procgen spec amended. It currently reads
*"`UShipSubsystem` does not learn anything about the universe."* It becomes
*"`UShipSubsystem` stores no universe data beyond the id of the system it is
steering for; it asks `UUniverseSubsystem` for everything else."* The intent
survives: there is still one authority on what is out there and where the
ship is in it. `UShipSubsystem::Initialize` calls
`Collection.InitializeDependency<UUniverseSubsystem>()`, which is how Unreal
orders one subsystem's startup after another's.

Two small requirements on procgen, both pure additions to `StarSystem.h`:
`operator==` and `GetTypeHash` for `FSystemId`, so ids can be compared and
kept in a `TSet`. If they are missing when navigation lands, navigation adds
them there. The file stays procgen's.

The chart range is `ds.Nav.RangeLy`, default **12 ly**, which is procgen's
own `NearbyRadius`, so the chart asks only for sectors the universe has
already cached. At procgen's density that is around thirty systems, and the
six nearest are typically within six or seven light years.

**Rejected (first draft): a navigation-owned `FStarCatalog`.** The first
draft defined its own hash (`DeepSpaceSeed::Derive`), its own 2^19-chunk
cells at a mean of 1.2, a discrete spectral table, `FStarSystemId`, a forced
G-class home, a constant galaxy seed, and a `Current` in `FShipNavState`.
Each of those disagreed with procgen, and the escape clause *"build it only if
procgen has not"* made it two plans in one. Its tests (a mean within 3% of
1.2, home is class G) would have been written against the version that was
going to be thrown away. That is ADR 0006's two-generator failure, planned in
advance. What home looks like, and how common each class is, are now procgen's
decisions, and one of them is on procgen's sign-off list.

**Rejected: a separate `UNavigationSubsystem`** that reads both subsystems and
leaves `UShipSubsystem` ignorant of ids. The drive's power want lives in
`UShipSubsystem::ApplyAllocation` and has to know whether the drive is
engaged, and the jump has to write the flight state, which only
`UShipSubsystem` may do. A third subsystem would need both of those pushed
back through a public API, which puts the flight state's write paths in two
places.

### 2. The jump is a three-lever commit, and it fires by itself

The throttle is a lever: you set it and it stays where you left it. The jump
works the same way, with three settings, each of which **persists when the
player stands up**:

| Lever | Set where | What it says |
|---|---|---|
| **Course** | the chart (pass B), or `ds.Nav.Plot` (pass A) | *where* |
| **Heading** | helm, W/A/S/D/Q/Z | *aim*, and the ship holds it once released (the assist has no drift) |
| **Engage** | the chart, or `ds.Nav.Engage` | *go* |

**Once all three are set and the drive is charged, the jump fires on its own.**
There is no final button and nothing to be present for. Engage the drive, bring
her round, then go to the galley and put the kettle on. The jump happens when
it happens. The streaks go past the galley window, and when you walk back
forward there is a new sun in the cockpit glass.

Alignment is a **cone of 8°** (`ds.Nav.ConeDeg`) around the ship's +X, which
is the forward `FShipFlightState::SubStep` already uses. Once the ship is
released it holds its heading exactly, so aligning and walking away is stable.

If the drive is charged and the ship is not aligned, **it holds at ready and
waits**, for as long as it takes. Nothing escalates.

After arrival the Engage lever **returns to off**, because it was a one-shot
"go" and the going is done. The course clears, since you are there. The
throttle stays where it was, so you arrive still under way.

**Rejected: jump from any heading.** It is crude, but it would make the helm
irrelevant to the loop the game is named for, and it would sever the one
relationship between flight and travel. **Rejected: the ship turns itself to
the course when engaged.** That would be an autopilot, and the flight spec's
trust rule is that nothing but the pilot moves the ship. **Rejected: a final
"Jump" press when ready.** It would make the player come back and service the
drive on its schedule, which is the anti-chore principle's definition of a
chore.

### 3. The helm has everything aiming needs

Aiming is the pilot's one job, so it must be possible from the pilot's seat
alone. The first draft put the bearing only on the chart, one chair over. The
mouse keeps looking while seated, so the pilot's view is not the ship's +X, and
a teal dot through the glass does not tell a free-looking head whether the nose
is within 8° of it. A solo player would have shuttled between chairs (read the
bearing, walk, guess, walk back, check), which is *aligning becomes hunting*
built in from the start. Worse, it would make a second person useful as a
navigator calling headings to the pilot. That is exactly the division of
labour the vision forbids, and two side-by-side chairs invite that reading.

So the helm gets three things, all of them C++ in files that already exist or
are already being changed:

- **The bearing, in words, on the HUD.** Whenever a course is plotted, the
  HUD's `DriveLine` carries it: `DRIVE READY · Kessa · 12° to port, 3° up`,
  and `DRIVE READY · Kessa · dead ahead` inside the cone. Inside the cone the
  degrees disappear, because once you are aligned there is nothing more to get
  right. It is computed from `Orientation.UnrotateVector(Dir)`, so it is
  relative to the ship and not to the head, and it updates as the ship turns.
  This goes in `ShipHUDWidget.cpp` only.
- **A nose mark.** A small caret on the HUD where the ship's +X meets the sky,
  drawn only while seated at the helm with a course plotted. The ship is the
  origin and its transform is identity (ADR 0005), so the nose is world +X:
  project `CameraLocation + 1e7 × FVector::ForwardVector` with
  `ProjectWorldLocationToScreen`, and hide the caret when that falls off
  screen. Being projected from infinity, it has no parallax: the caret sits on
  the course marker exactly when the ship is aligned, however the head is
  turned. The caret is built in `BuildScreen` and found again by name through
  `WidgetTree->FindWidget`, so `ShipHUDWidget.h` does not change.
- **The course marker through the glass** (decision 8), which is what the
  caret is put on.

**The overshoot fits inside the cone.** At cruise limits a full-rate turn
(0.20 rad/s) brakes at 0.25 rad/s² after release and carries on for
ω²/2α = 0.08 rad, 4.6°. Release as the HUD first says *dead ahead*, at the
cone's edge, and the ship stops about 3.4° short of centre. It is still
inside the cone, so the naive release works. That holds as long as the cone's
full width (16°) is more than the overshoot. `ds.Nav.ConeDeg` below about
2.5° breaks it, and a test pins the relationship (step A3).

With this in place the chart chair is visited **once per jump**, to choose and
engage, and the helm has everything else. Nothing is ever worth having a
second person call out.

**Rejected: a boresight tick on the fore glass, at the helm's eye line.** It is
more diegetic, like a gunsight scratched on the canopy. But it is layout work
with a level rebuild for every nudge of its height, it has parallax as the
camera bobs (5 cm of head movement at 155 cm from the glass is about 2°), and
it lines up only from the port seat. It is recorded as the successor if the
HUD caret feels like a game UI.
**Rejected (first draft): the bearing on the chart only**, with a helm readout
as a mitigation under *Risks*. For the reasons above.

### 4. The drive draws power only while it winds

This is how the power split starts to matter. Today the engine consumer
wants a flat 500 W at all times, so its share is taken from the lights and
boosters whether you are going anywhere or not. Worse, the charge winds up
continuously, so by the time you have chosen a destination the drive is
always ready and the split has done nothing.

Both change:

- **The charge winds only while Engage is on.** When Engage is off it holds
  where it is: no decay, no discharge, as the flight state already promises.
- **The engine's want follows the drive.** It is `ds.Nav.WindingWant`
  (default 800 W) while engaged and not yet charged, and **zero** otherwise:
  idle, holding, or charged and waiting on alignment. A want of zero takes
  part in no split (the power spec, decision 3), so an idle drive costs the
  ship nothing.

**A starved drive still gets there.** The charge rate is
`(StarvedRate + (1 − StarvedRate) × EngineFeed) / ChargeSeconds`, with
`ds.Nav.StarvedRate` defaulting to 0.2 and `ds.Nav.ChargeSeconds` to 90. That
is the same rule as `StarvedBoosterThrust`: a system that cannot work at all
is a failure state, and the whole model is that systems degrade and do not
fail. A weight of zero is a legitimate way to live. It means a bright ship and
a slow wind-up, and it always finishes.

**What the player notices while it winds depends on the split, and at the
default it is nothing.** The first draft claimed *"press Engage and the ship's
lights dip."* At the default split that is false, and the draft's own table
showed it. At 1 : 1 : 1 each consumer's proportional share of 1000 W is 333 W,
the lights want only 300 W, and so they stay capped at full whatever the
engine asks for. **No `WindingWant` can change that**: with three equal
weights, the lights' share never falls below a third of the reactor. What
drops is the boosters, from 450 W to 350 W, and a player walking the galley
cannot feel that.

How the ship is to live in while the drive winds, ordered by how bright the
galley is:

| Split (lights : boosters : engine weight) | The ship while the drive winds |
|---|---|
| 1 : 1 : 0 | Bright, and she handles as she always does. The drive gets only what its floor gives it. |
| 1 : 1 : 1 (the default) | Bright. She is a touch softer on the throttle (boosters at 78%), which you notice only at the helm. |
| 1 : 1 : 2 | The lights go down a little (83%). You notice if you are looking. |
| 1 : 1 : 4 | The galley is dim (55%) and she is sluggish (boosters at 37%). |
| lights switched off at the console | Dark, by choice, and full handling. The stars come out through the glass (the sky's fixed exposure). |

For the designer's sanity, and on no screen anywhere: the wind-ups run from
about 2 minutes (lights off) through 2¾ at the default to 7½ at 1 : 1 : 0.
The rows are ordered by the ship, not by how soon you leave, and none of them
is the correct one. That is the vision's *"divert it from the lights and you
walk your ship in the dark"*, with a reason to do it.

**So, at the default, the HUD word is the only signal**, `DRIVE WINDING`
until `DRIVE READY`, and this spec accepts that for the POC. The jump itself
is the diegetic event: streaks past whatever window you are near. The lights
dip at splits that lean on the drive, which is the honest version of the
first draft's claim. Nothing needs a cue that the drive is ready, because the
jump does not wait for you.

The playtest can try a dip at the default without a rebuild.
**`ds.Nav.FoldDraw`**, default 0 W, takes a fixed draw off the top of the
reactor while the drive winds, through the existing `AddDraw`/`RemoveDraw`.
At 350 W the pool shrinks to 650 W, and at 1 : 1 : 1 the lights drop to 72%
while every wind-up lengthens by about a third. Whether that reads as the ship
straining, or as a nuisance tax on every jump, is a question for eyes. It is
off by default, and it is on the sign-off list.

**Rejected: raising `WindingWant` until the lights dip at 1 : 1 : 1.** It
cannot be done (above). **Rejected: lowering the lights' weight while the
drive is engaged.** Weights are a preference the player set and they stay
set (`FShipPowerState`'s header says so). A drive that quietly re-weights the
player's split is the ship overriding its owner. **Rejected: the charge winds
continuously, as now.** The split would stop mattering after the first jump,
since everyone would find the drive already charged. **Rejected: charge time
scaling with jump distance.** It gives the player a second number to
optimise against the first, and it is a throughput problem, which the
vision's drift signal names by name.

### 5. The jump is an operation on the flight state; a pure state machine decides when

**`FShipNavState`**, in `Source/DeepSpace/Ship/ShipNavState.h/.cpp`, is a
pure struct next to `FShipPowerState` and `FShipFlightState`. It decides; it
does not act. **It holds no current system.** That is procgen's, and the
ship subsystem passes it in wherever it matters.

```cpp
enum class EJumpPhase : uint8 { Holding, Transit };
enum class ENavEvent  : uint8 { None, TransitBegan, Arrived };
enum class EDriveState : uint8 { Idle, Winding, Ready, Transit };   // for screens

/** Filled from the ds.Nav.* console variables every tick; the defaults are
 *  the starting values. Pure data, so the state machine never reads a CVar. */
struct FNavTuning
{
    double ConeRadians    = 8.0 * UE_DOUBLE_PI / 180.0;
    double TransitSeconds = 6.0;
};

struct DEEPSPACE_API FShipNavState
{
    bool Plot(const FSystemId& Id);          // false in transit
    void ClearPlot();
    const TOptional<FSystemId>& GetPlotted() const;

    void SetEngaged(bool bOn);               // ignored in transit
    bool IsEngaged() const;

    /** Charge and alignment are inputs, not state: the flight state owns the
     *  charge and the subsystem owns the geometry. */
    ENavEvent Step(double DeltaSeconds, double JumpCharge,
                   double OffBoresightRadians, const FNavTuning& Tuning);

    EJumpPhase GetPhase() const;
    double GetTransitProgress() const;       // 0..1
    int32 GetJumpSerial() const;             // bumps on every arrival
    const TOptional<FSystemId>& GetLastArrival() const;

    void MarkVisited(const FSystemId& Id);
    bool HasVisited(const FSystemId& Id) const;

private:
    TOptional<FSystemId> Plotted, LastArrival;
    TSet<FSystemId> Visited;
    bool bEngaged = false;
    EJumpPhase Phase = EJumpPhase::Holding;
    double TransitElapsed = 0.0;
    int32 JumpSerial = 0;
};
```

`Step` in `Holding` returns `TransitBegan` exactly when the drive is engaged,
a course is plotted, the charge is ≥ 1, and the ship is off boresight by no
more than the cone. In `Transit` it counts to `TransitSeconds`, then moves the
course into `LastArrival`, adds it to `Visited`, clears the course and Engage,
bumps the serial, and returns `Arrived`. Refusing to plot the system you are
already in is the subsystem's job, because only it can ask which system that
is.

`UShipSubsystem::Tick` becomes:

```cpp
ApplyAllocation(DeltaTime);     // engine want follows the drive; charge only while engaged
FlightState.Step(DeltaTime);
StepNavigation(DeltaTime);      // NavState.Step, then act on the event
```

On **`TransitBegan`** the subsystem calls `FlightState.SpendJumpCharge()`,
which zeroes the charge because the drive has opened the fold. For the
seconds of transit it releases attitude every tick, so the helm does nothing
while you are between stars.

On **`Arrived`** it reads `NavState.GetLastArrival()`, then:

- `Dir = (Destination.Position − ShipPosition).GetSafeNormal()`, through
  `FUniversePosition::operator-` and never through offsets.
- `Position = Destination.Position − Dir × Standoff`, with `ds.Nav.StandoffAU`
  defaulting to **2.4 AU**.
- `FlightState.JumpTo(Position)`. **A jump is a translation and nothing
  else.** Orientation, velocity and angular velocity are untouched.
- `Universe->SetCurrentSystem(Id)`.

**Why not turning the ship is correct.** The arrival point lies on the line
from the departure point to the star, so from the arrival point the star is in
exactly the direction it was in when the fold opened, and that direction was
inside the cone. The new sun is where the nose was pointing, within 8°, and
the distant dome, drawn through the inverse orientation, does not move at all.
Decision 8 depends on that dome not visibly changing. The throttle keeps you
moving, so you arrive still under way and heading for the star.

`JumpTo` is the **fourth write path** into the flight state, after
`SetFlightCommand`, `ClearPilot` and the tick. It is private to the subsystem
and called from exactly one place. The flight spec's claim that *"nothing else
can move the ship"* becomes *nothing else, and the drive*.

**Rejected (first draft): `Orientation = FRotationMatrix::MakeFromX(Dir)`, so
the sun is dead ahead.** `MakeFromX` builds the other two axes from world up,
so it throws away the ship's roll: after arrival the ship could be rolled by
anything up to 180° relative to before, and because the counter-frame carries
the inverse orientation, the whole distant dome would reappear spun about the
forward axis. It also broke the draft's own claim that rotating the velocity
was "limited to 8°, so this is honest". The planned test (destination within
1° of +X) could not have caught it.
**Rejected (the review's fix): rotate minimally,
`FQuat::FindBetweenNormals(Forward, Dir) × Orientation`, and apply the same
delta to the velocity.** It is correct, and it preserves roll. But it still
turns the dome by up to 8° in one frame, which is a visible jump in the one
thing that must not move. And it buys nothing: no design needs the star
exactly centred, and the pilot can centre it if they want. Not rotating is
both cruder and truer.
**Rejected: putting the state machine on an actor**, a drive component in
engineering, for instance. It would be untestable without a world, and ADR
0003 exists so that ship state does not end up scattered across actors. A
physical drive object can come later as a *view* of this state.
**Rejected: an instant jump with no transit.** A cut with no between is
exactly what a loading screen looks like. Six seconds is enough to register
that you have gone somewhere, and short enough that it is spectacle and not
waiting.

### 6. Every tunable is a console variable, read each tick

Linux has no Live Coding, and any header change costs `./rebuild.sh --force
--launch` (CLAUDE.md). The first draft named five tunables for the playtest
and put every one in a header as `static constexpr`, so every nudge would
have been a multi-minute rebuild. Instead, each is a `TAutoConsoleVariable`
in the `.cpp` that uses it, read at `Step`/`Tick` and never cached, with the
old constant as its default:

| CVar | Default | Read in |
|---|---|---|
| `ds.Nav.ChargeSeconds` | 90 | `ShipSubsystem.cpp`, passed to `ChargeJumpDrive` |
| `ds.Nav.WindingWant` | 800 W | `ShipSubsystem.cpp` |
| `ds.Nav.StarvedRate` | 0.2 | `ShipSubsystem.cpp` |
| `ds.Nav.FoldDraw` | 0 W | `ShipSubsystem.cpp` |
| `ds.Nav.TransitSeconds` | 6 | `ShipSubsystem.cpp`, into `FNavTuning` |
| `ds.Nav.ConeDeg` | 8 | `ShipSubsystem.cpp`, into `FNavTuning` |
| `ds.Nav.StandoffAU` | 2.4 | `ShipSubsystem.cpp` |
| `ds.Nav.RangeLy` | 12 | `ShipSubsystem.cpp` |
| `ds.Nav.MarkerPixels` | 6 | `ShipCounterFrame.cpp` |
| `ds.Nav.StreakLength` | 40 | `ShipCounterFrame.cpp` |

`FShipFlightState::JumpChargeSeconds` stays as the default of a new parameter,
`ChargeJumpDrive(double DeltaSeconds, double Satisfaction, double
SecondsFromCold = JumpChargeSeconds)`, so the pure struct never reads a CVar
and the existing flight tests are unchanged.

Commands, registered in `ShipSubsystem.cpp` with
`FAutoConsoleCommandWithWorldAndArgs`, the way procgen registers
`ds.Universe.*`, so they cost no header change:

- `ds.Nav.Near`: logs the chart, numbered, nearest first.
- `ds.Nav.Plot <n>` and `ds.Nav.Clear`.
- `ds.Nav.Engage [0|1]`.
- `ds.Nav.Charge`: fills the drive now. It sets a file-static flag that the
  next `Tick` consumes by calling `ChargeJumpDrive(ChargeSeconds, 1.0)`, so
  there is no new write path and no header change. With it, the developer can
  run the jump loop in seconds while playtesting. `ds.Nav.ChargeSeconds 3`
  does the same thing for every jump that follows.

Pass A's chart *is* these commands. They stay in pass B as debug tools, and
they are ungated, like `ds.Sky.*`.

At the end of the playtest, the settled values are written back as the
defaults in the `.cpp` files, which costs one rebuild.

### 7. The chart, and the chair it sits in (pass B)

**The mount.** The cockpit desk already carries three decorative screens
(`props.py`, `cockpit_desk`: cubes at y −85, 0 and +85). The starboard one, in
front of the starboard pilot seat that currently does nothing, becomes the
chart.

**`AShipNavScreen`**, in `Source/DeepSpace/Ship/ShipNavScreen.h/.cpp`,
subclasses `AShipScreen` the way `AShipLaptop` does. It has `bUsable = true`,
a `UBoxComponent` reach volume, and a `UInteractableComponent` whose handler
calls `UseScreen`. The prompt is **"Sit at the chart"**. The panel is 68 cm at
816×576 px (12 px/cm, the same density the laptop uses), with
`UseDistanceCm ≈ 100` and `SeatHeightCm ≈ 55`, which puts the body on the
chair, and `ViewDistanceCm ≈ 60`. These are `UPROPERTY` defaults set per
instance by `build_hauler.py`, so tuning them is a level rebuild and not a C++
one.

**`UNavigationWidget`**, in `Source/DeepSpace/UI/NavigationWidget.h/.cpp`, is
a `UShipScreenWidget` subclass whose tree is built in `BuildScreen`. It asks
the subsystem every frame and stores nothing, following the pattern
`UPowerAllocationWidget` set.

```
 NAVIGATION
 Here    Kessa · yellow star · visited
 ─────────────────────────────────────────
 ▸ Orvane      6.1 ly   red dwarf
   Tessik      7.4 ly   orange star      visited
   Dunmar      8.8 ly   red dwarf
   Aivel       9.0 ly   yellow-white star
   Brisk       9.7 ly   red dwarf
   Hollin     11.2 ly   red dwarf
 ─────────────────────────────────────────
 Drive     Winding.
 Course    Orvane — 30° to port, 5° down.        [ ENGAGE ]
```

- **Rows** are the six nearest charted systems, excluding the current one,
  each a `UButton` showing procgen's `Name`, the distance, and the class in
  words. Clicking a row plots it; clicking the plotted row clears it. The six
  handlers are six `UFUNCTION`s, `HandleRow0`…`HandleRow5`, because
  `UButton::OnClicked` carries no payload. It is crude and it cannot go wrong.
- **Drive** is a word, never a number: *Idle*, *Winding*, *Ready*,
  *Between stars*. There is **no percentage, no bar, no countdown and no
  ETA**, here, on the HUD, or in the console. A test enforces it.
- **Course** gives the same bearing words as the HUD. A bearing is a fact
  about the sky, not a target: nothing is late and nothing gets worse.
- **Engage / Stand down** is one toggle. It is disabled with no course, and
  pressing it with no course does nothing.

The words (bearing, class, drive) come from one pure file,
**`UI/NavText.h/.cpp`**: `NavText::Bearing(const FVector& ShipLocalDir,
double ConeRadians)`, `NavText::StarClass(EStarClass)` (procgen's enum), and
`NavText::Drive(EDriveState)`. The HUD, the chart and `ds.Nav.Near` all use
it, so the three can never word the same fact differently. It lands in pass A,
because the HUD needs it first.

Seams for tests, mirroring the laptop's: `RefreshFromShip()`,
`SelectRow(int32)`, `PressEngage()`, `GetRowText(int32)`, `GetHereText()`,
`GetDriveText()`, `GetCourseText()`.

So one player plots in the right-hand seat, engages, stands, and sits one
chair to the left to fly, once per jump. The seats are 140 cm apart.

**On company.** Two chairs and two screens look like two stations. They are
not. The chart is for choosing and the helm is for aiming, and the helm
carries everything aiming needs (decision 3), so nobody in the other chair has
anything to call out. A friend there is company. The vision's rule holds as
long as nothing ever *needs*, or even benefits from, both chairs being
occupied at once. After decision 3, nothing here does.

**Rejected, for the POC: a "lean in to the chart" key at the helm**, one seat
that toggles between flying and using the screen. It is the better long-term
answer, but it needs a new input action (a Python commandlet), header changes
to `ADeepSpaceCharacter`, and a character that is both `Seat` and
`UsedScreen` at once, which `PlaceCamera`, `SetViewLimits` and `UpdatePointer`
all assume cannot happen. That is three hours of state-combination bugs before
the loop can be tested. It is recorded as the likely successor.
**Rejected: a 2D chart of dots**, a canvas with the neighbourhood projected
onto a plane. It is what the screen ought to become, but the list answers
*"where am I, what can I reach"* for a playtest at a fraction of the layout
work. It is deferred, not dropped.

### 8. Arrival looks different because the sky redraws, and navigation tells it when

**The distant dome does not change across a jump.** Those stars stand for a
galaxy away, and over ten light years they would not move. This is the
vision's central principle turned into rendering: *scale is only felt in
contrast*. After a jump the sky's neighbour stars have shifted, a different
sun sits ahead at its true size and colour, the sunlight on the deck has
changed direction and tint, and the background has not moved at all. The
unmoved background is what makes the rest read as somewhere else. Decision 5's
translation-only jump is what keeps it unmoved.

Navigation adds three things to `AShipCounterFrame`, all C++, none of them a
constructor component:

- **`CourseMarker`**, a `UStaticMeshComponent` created at runtime with
  `NewObject` the first time a course is plotted, as the sky creates its body
  proxies. It uses the mesh already assigned to `DistantStars`, and a
  `UMaterialInstanceDynamic` of the sky's `M_SkyStar` with `Colour` set to the
  ship's teal accent. It sits at the course's **true direction** on the
  dome, `Sky->GetDomeRadius()` out, and is sized to `ds.Nav.MarkerPixels`
  **in pixels**: diameter = `MarkerPixels × Sky->GetPixelAngle() ×
  GetDomeRadius()`, as the sky spec requires. It is never a multiple of
  `DistantStarScale`. Being runtime, it adds nothing to the placed actor's
  saved hierarchy, so the level does not need rebuilding (CLAUDE.md, *changing
  a component's attachment does not move actors already placed*), and there is
  no new asset slot for `build_hauler.py`.
- **The serial check.** Each tick the counter-frame compares
  `Ship->GetJumpSerial()` with the serial it last built for. That is one
  integer, a cache of a pure function. When they differ it calls
  `RebuildStarfield()`, re-scattering the near field around the new position,
  which would otherwise lie light years behind, and then, if an `AShipSky` is
  attached, `Sky->SetSystem(FSkySystem::FromSystem(Universe->GetCurrentSystem(),
  Universe->GetSystemsNear(...)))`. On the transit's edges it calls
  `Sky->SetInTransit(true)` and `(false)`. The sky hides its bodies, its sun and
  its neighbours; the counter-frame hides `DistantStars` and the marker.
- **The transit**: the seconds of transit are the near field stretched into
  streaks. In `SyncToShip`, while `IsInTransit()`, each near-star instance is
  scaled along world +X (the ship's forward) by
  `1 + StreakLength × sin(π·progress)` and swept aft by a fake displacement,
  still wrapped into the field. No post-process, no material and no Niagara
  are needed: scaling instances is the whole effect.

The counter-frame finds the sky once, through the actors attached to it (the
sky attaches itself at `BeginPlay`), and keeps a `TWeakObjectPtr`. With no sky
present, as in most test worlds, every sky call is skipped, and the marker is
not created.

**Rejected (first draft): `Primary`, `CatalogStars`, and an optional
directional sunlight, all on the counter-frame.** These were an exaggerated
local sun at 0.9× a 120 m dome, the charted neighbours sized as 0.6–2.0×
`DistantStarScale`, and a light pointing away from the primary. The sky spec
supersedes all three: its `AShipSky` draws the sun at true size, draws the
neighbours as `NeighbourStars`, owns the one directional light, and moves the
dome to 10,000 km. Building them here would be work, tests and a
`verify_level` check for someone to delete. **Rejected: re-seeding the distant
dome per system.** It would look more different and be less true, and it
would spend the contrast the vision says is the only way scale is felt.

### 9. Where the ship starts

Procgen decides *which* system the game starts in (the nearest to the origin
with a planet). It leaves *where in it* the ship sits to navigation. The sky
spec has already designed the opening shot around a planet: 40,000 km from the
home planet on its sunward side, so the first thing through the cockpit glass
is an 18° world. Navigation adopts that shot rather than inventing a second
one. At `OnWorldBeginPlay`, `UShipSubsystem` places the ship 40,000 km from
planet 0 of `GetCurrentSystem()`, on its sunward side, facing it, and marks the
start system visited. If the sky has already written this placement when
navigation lands, navigation takes the code over and the sky's copy is
deleted, because one place decides where the ship starts.

A jump's arrival point (decision 5) is a different rule, and deliberately so:
you arrive at a system's edge, 2.4 AU from its star, and never at a planet.

**Rejected (first draft): the game opens at home's arrival point, 2.4 AU from
a forced yellow sun.** It conflicted with the sky's opening, and forcing the
home class is procgen's decision now.

This needs `UUniverseSubsystem::GetCurrentSystem()` to be safe to call from
another subsystem's `OnWorldBeginPlay`. Unreal does not order `OnWorldBeginPlay`
across subsystems, and procgen builds the start system in its own. The ask of
procgen is that `GetCurrentSystem()` builds the start system on first call if
it has not been built yet. It is a one-line guard, and it is listed under
*Risks* until procgen confirms it.

## The anti-chore audit

This feature is a charge that fills and a machine that waits for you, which
makes it the easiest place yet to break the principle by accident. So each way
it could go wrong is named here, along with where it is ruled out:

| The way it becomes a chore | Where it is ruled out |
|---|---|
| A countdown to watch | No numbers on drive state, anywhere (decision 7); enforced by test |
| A charge that leaks if you leave it | Charge never decays; disengaging holds it (decision 4) |
| A drive that must be tended while it winds | It needs nothing once engaged; it fires by itself (decision 2) |
| Being summoned when it is ready | No alarm, no chime, no prompt; it does not wait for you, so there is nothing to summon you for |
| A split with a right answer | Every split finishes; nothing shows the fastest; the table is ordered by the ship, not by time (decision 4) |
| A penalty for not travelling | An idle drive wants zero watts; staying put costs nothing |
| A failure state | Misaligned holds at ready; a starved drive still gets there |
| Hunting for alignment | Bearing words and a nose caret at the helm; the release overshoot fits inside the cone (decision 3) |
| A number to steer by becoming a target | The degrees disappear inside the cone; nothing rewards being closer than 8° |
| A crew job | The helm has everything aiming needs; nothing needs, or benefits from, both chairs at once (decisions 3, 7) |

The one open question this spec cannot answer is whether **a two-minute
wind-up in a ship with little yet to do aboard** reads as living in the ship
or as waiting. That depends on what the ship has in it, which is the
shipdetail sub-project's work. `ds.Nav.ChargeSeconds` exists so the playtest
can find out without a rebuild.

## Deliberate fakes, and what they cost later

- **Pass A's chart is the console.** Choosing somewhere by typing
  `ds.Nav.Plot 2` is not the game. *Cost:* none. Pass B replaces it, and the
  commands stay as debug tools.
- **The nose caret is a HUD mark, not a sight on the ship.** *Cost:* the glass
  tick in decision 3, as layout work, if the caret reads as a game UI.
- **Arrival is always 2.4 AU from the star, whatever the star.** From there an
  M dwarf, which is three destinations in four, is a small red disc of two or
  three pixels. *Cost:* a rule based on luminosity, which is one function in
  `StepNavigation`. The CVar lets the playtest find out whether it is needed.
- **There is nothing to do inside a system.** You arrive, and cruise at
  200 m/s will not take you to a planet. *Cost:* in-system travel, which is
  the sky's open question 5 and has no owner. This spec keeps it out of scope
  on purpose.
- **No save.** The visited set and the position are lost on quit. *Cost:*
  `FShipNavState` and procgen's current system are the whole of what to
  serialise, and both are small.
- **The nav chair.** Choosing and flying are two seats. *Cost:* the "lean in"
  successor in decision 7, roughly three hours.
- **Plot and Engage are ungated.** Anyone at the screen or the console can use
  them. *Cost:* netcode needs a commander argument, as `SetFlightCommand`
  already has.
- **Jump range is the flat 12 ly chart radius.** *Cost:* making range an
  upgrade means a different number, not a different model.
- **The fold draw, if adopted, is a draw that comes and goes.** The power
  model's draws are meant to be installed modules. *Cost:* if it stays, it
  becomes the drive module's own draw when modules gain states.

## Implementation outline

**Estimate: two sessions, about 16 hours.** Pass A is one long session, and it
ends with a playable loop: choose from the console, aim at the helm, walk
away, arrive. Pass B is a shorter session, and it ends with the chart in the
chair. Neither starts until procgen and the sky have landed.

Every C++ change in a pass is written first. The pass then runs one
`./rebuild.sh --force`, the commandlets and the tests, **in that order and
never alongside any other editor process**. Only the steps marked
**[editor]** touch the editor.

### Pass A: the loop

A1. **The pure layer.** `Ship/ShipNavState.h/.cpp` and `UI/NavText.h/.cpp`.
    `Tests/ShipNavStateTest.cpp`, `DeepSpace.Ship.NavState`:
    - engaging with no course never transits
    - an uncharged drive never transits
    - misaligned at full charge holds indefinitely (step 10 simulated minutes)
    - aligned, charged and engaged gives `TransitBegan` on that step
    - `Arrived` comes after `TransitSeconds`, with `LastArrival` set, visited,
      course and Engage cleared, and the serial +1
    - plot and engage are refused in transit
    - a changed `FNavTuning` is honoured on the next step

    `Tests/NavTextTest.cpp`, `DeepSpace.UI.NavText`:
    - `Bearing` says "dead ahead" inside the cone, with no digits
    - it names port, starboard, up and down correctly on the four axes, and
      says astern past 90°
    - `Drive` never contains a digit or `%` for any state

    *(1.5 h, written without the editor, compiled in A6)*
A2. **Flight state.** `ShipFlightState.h/.cpp` gains the `SecondsFromCold`
    parameter, `SpendJumpCharge()`, and `JumpTo(const FUniversePosition&)`,
    which changes the position and nothing else. `ShipFlightStateTest` gains:
    - `JumpTo` leaves the orientation, velocity and angular velocity
      bit-identical
    - `ChargeJumpDrive` with the default parameter matches today's rate

    *(0.5 h)*
A3. **Subsystem.** `ShipSubsystem.h/.cpp` gains:
    - the `UUniverseSubsystem` dependency and `FShipNavState`
    - `StepNavigation`, charge only while engaged, and the engine want
      following the drive (`EngineWant` goes, replaced by the CVar)
    - `ds.Nav.FoldDraw`, and every CVar and command in decision 6
    - an `OnWorldBeginPlay` override for the start placement (decision 9)
    - a C++-only read and write API: `GetChart()` (asks procgen every call),
      `PlotCourse(const FSystemId&)` (refuses the current system),
      `ClearCourse()`, `GetPlottedSystem()`, `SetDriveEngaged(bool)`,
      `IsDriveEngaged()`, `GetDriveState()`, `GetCourseDirectionShipLocal()`,
      `IsInTransit()`, `GetTransitProgress()`, `GetJumpSerial()`

    `Tests/ShipPowerConsumersTest.cpp` is updated: its charge assertions now
    engage the drive first, and its engine-satisfaction assertion expects an
    idle drive to want 0 W. New `Tests/ShipJumpTest.cpp`, `DeepSpace.Ship.Jump`,
    built in the same world-construction shape as `ShipPowerConsumersTest`,
    with a `UUniverseSubsystem` on its test seed:
    - an idle drive wants 0 W, and engaging raises the engine's want
    - at 1 : 1 : 4 the lights' satisfaction drops while winding and recovers
      at ready; at 1 : 1 : 1 it stays at 1 (this pins decision 4's admission)
    - a zero-weight engine still reaches full charge
    - after a full plot, align, engage and wait, three things hold:
      - the ship is within `Standoff` ± 1 km of the destination
        (`DistanceTo`)
      - the orientation is unchanged, so the up vector has moved by zero
      - the destination is within the cone of +X, and the charge is 0
    - after the same jump, the velocity is unchanged, the serial is +1, and
      `UUniverseSubsystem::GetCurrentSystem()` is the destination
    - plotting the current system is refused
    - the release overshoot at cruise limits, ω²/2α, is less than twice the
      default cone (decision 3's claim)
    - `ds.Nav.Charge` fills the drive on the next tick

    *(3 h)*
A4. **HUD.** `UI/ShipHUDWidget.cpp` only:
    - `PlaceLine` shows the current system's name and class, or
      *BETWEEN STARS* in transit
    - `DriveLine` shows the drive word and, with a course plotted, the name
      and bearing
    - the nose caret, found by name, projected and hidden as in decision 3,
      shown only while piloting with a course plotted

    Verified by eye in A7. The HUD has no test seam today, and adding one is a
    header change for a test of text that `NavTextTest` already covers.
    *(1 h)*
A5. **Counter-frame.** `ShipCounterFrame.h/.cpp` gains:
    - the runtime `CourseMarker` (a `UPROPERTY` pointer)
    - `BuiltForSerial` and the transit-edge flag
    - the weak sky pointer, the streaks in `SyncToShip`, and the serial check
      calling the sky

    Extend `Tests/UniverseFrameTest.cpp`, with no sky in the world:
    - after a jump, every near star is inside the field
    - in transit, the near stars are stretched and `DistantStars` is hidden
    - `BuiltForSerial` follows the serial

    The marker's direction and pixel size, and the sky calls, need an
    `AShipSky` in a headless world. They are checked by eye in A7, and the
    sky's own `ShipSkyTest` covers `SetSystem`. *(2 h)*
A6. **[editor] Build and test.** `./rebuild.sh --force` (headers changed),
    then `Automation RunTests DeepSpace` headless, then grep the log. No
    `build_hauler.py`: nothing generated changed shape, and the marker is
    runtime. `check_blueprints.py` is not needed either: nothing reflected was
    removed or renamed, because `EngineWant` was a `constexpr`, not a
    `UPROPERTY`. *(1 h, mostly waiting)*
A7. **[editor] Fly it.** `./launch.sh`, then run through the loop:
    - `ds.Nav.Near`, `ds.Nav.Plot 0`, `ds.Nav.Engage 1`
    - aim by the HUD words and the caret, and put the caret on the marker
    - stand up and walk to the galley
    - set `ds.Nav.ChargeSeconds 5` and repeat until it stops being
      interesting

    Check by eye:
    - the marker is visible through the glass at 6 px
    - the arrival reads as somewhere else
    - the dome did not move
    - the streaks read at the galley window

    Try `ds.Nav.FoldDraw 350` once. *(1 h)*

### Pass B: the chart chair

B1. **Layout.** `Tools/hauler_layout.py` gains
    `NAV_SCREEN = ("cockpit", (301, 285), 105, 0)`. That is just proud of the
    starboard desk screen's aft face, facing aft at yaw 0 like every other
    fixture, and in front of the starboard seat. The `Ship` namedtuple gains
    `nav_screen_location` and `nav_screen_yaw`. `Tools/test_placement.py`
    asserts that the mount resolves inside the cockpit, faces −X, and sits
    within 20 cm laterally of the starboard seat. `validate_hauler.py` is
    unaffected, since this is not a furniture box. *Runs:*
    `python3 Tools/test_placement.py && python3 Tools/validate_hauler.py`, no
    editor. *(0.5 h)*
B2. **Nav screen actor and widget.** `Ship/ShipNavScreen.h/.cpp` and
    `UI/NavigationWidget.h/.cpp`. Test `Tests/NavScreenTest.cpp`,
    `DeepSpace.UI.NavigationScreen`, with the actor spawned before
    `World->BeginPlay()` (CLAUDE.md, the `UWidgetComponent` rules):
    - rows list the chart in order and exclude the current system
    - `SelectRow` plots in the subsystem, and a second widget instance
      reports the same course (the `ShipScreensAgree` pattern)
    - Engage is refused with no course
    - `GetDriveText()` never contains a digit or `%`, in any state

    *(3 h)*
B3. **Level scripts.** `build_hauler.py` gains `place_nav_screen(actor_sub,
    ship)`, spawning `unreal.ShipNavScreen` labelled `hauler_nav_screen` with
    its seat tunables. `verify_level.py` checks that it sits within 1 cm of
    the layout and faces −X. *(0.5 h)*
B4. **[editor] Build, level, test.** In this order:
    1. `./rebuild.sh --force` (new classes)
    2. the `build_hauler.py` commandlet
    3. the `verify_level.py` commandlet
    4. the automation tests

    Run `check_blueprints.py` only if something was renamed or removed, and
    nothing is planned to be. *(1 h)*
B5. **[editor] Playtest and tune.** Tune in this order:
    1. whether the chair seats the body correctly (the three seat tunables;
       each change is a level rebuild)
    2. the cone
    3. `ChargeSeconds` and `WindingWant`
    4. `FoldDraw`: on or off?
    5. `TransitSeconds` and `StreakLength`
    6. `StandoffAU`
    7. `MarkerPixels`

    Everything after the chair is a CVar. Write the settled values back as
    defaults, rebuild once, and record what changed as an addendum here, the
    way the power spec did. *(1.5 h)*

## Risks

- **The landing order slips.** If navigation lands before the sky, the jump
  works and the arrival is invisible. Do not playtest it that way: the
  playtest would be judging a teleport with streaks.
- **Procgen's `GetCurrentSystem()` before its `OnWorldBeginPlay`.** Decision 9
  needs it safe to call early. If procgen does not add the guard, the start
  placement moves to the first `Tick`, behind a `bPlaced` flag, which is
  cruder but does not depend on the order.
- **The amended procgen sentence** (decision 1) needs the procgen author and
  the developer to agree. If they do not, the fallback is the rejected
  `UNavigationSubsystem`, which costs a public write path on the ship
  subsystem.
- **Honest priors meet a fixed standoff.** Three destinations in four are red
  dwarfs, and at 2.4 AU one is a two-pixel red point. The arrival may read as
  "nothing happened" even with the neighbours shifted and the sunlight
  changed. Mitigation: `ds.Nav.StandoffAU` in play, then a luminosity rule.
  The fix is **not** to flatten the class weights here; that is procgen's
  sign-off question.
- **The jump fires while you are elsewhere, and that may feel like the game
  acting without you.** It is the lever design taken literally. Stand down
  exists, but if the playtest finds it unwelcome, the answer is a softer
  diegetic cue before the fold. It is not a confirm button.
- **Waiting.** Covered in the audit: whether a wind-up is life aboard or dead
  time depends on content this spec does not add.
- **The HUD caret feels like a game UI** in a game whose screens are surfaces
  in the room. The glass tick is the recorded successor.
- **The laptop's engine row changes meaning.** It now reads idle whenever the
  drive is not winding (a want of zero is satisfaction 1, per
  `FShipPowerState`). That is honest, but it may surprise a player who
  remembers it drawing 500 W.
- **File contention.** `AShipCounterFrame` is edited by the sky and then by
  navigation, and the sky's edits land first. `hauler_layout.py`,
  `props.py` and `build_hauler.py` are edited by the lived-in ship and by
  pass B. Serialise them, and whichever lands second rebases.
- **The fold draw, turned on, may read as a tax.** It lengthens every
  wind-up. The playtest decides, and it is off until then.

## Review, 2026-09-25

A review of the first draft raised seven issues. Six were accepted and the
design was changed. For the seventh, the cut was taken in a different place
than proposed, and the reason is given. The superseded choices are kept in
place as rejected alternatives.

| # | Issue | Answer | Where |
|---|---|---|---|
| 1 | The helm could not tell whether it was aligned; the bearing was one chair over, and that invited a navigator | Accepted. The bearing goes in the HUD `DriveLine`, and the helm gains a nose caret. The overshoot is shown to fit inside the cone. The glass tick was rejected in favour of the caret (no parallax, no level rebuild per nudge) | Decision 3 |
| 2 | A second universe and a second current-system authority, conflicting with procgen | Accepted. Navigation consumes `UUniverseSubsystem`, the course is an `FSystemId`, and `Current` is gone from `FShipNavState`. The landing order is stated, and one procgen sentence is amended | Decision 1, *Landing order* |
| 3 | `Primary`, `CatalogStars`, the 120 m sizing and sunlight are superseded by the sky; start positions conflicted | Accepted. All three are cut, and navigation lands after the sky. The marker is sized in pixels, the serial check calls `SetSystem`/`SetInTransit`, and the start adopts the sky's opening | Decisions 8, 9 |
| 4 | Every tunable was a header `constexpr`, so each nudge was a rebuild | Accepted. Ten `ds.Nav.*` CVars live in the `.cpp` files, and `ds.Nav.Charge` fills the drive | Decision 6 |
| 5 | "The lights dip" was false at the default split, and the table ranked splits by time | Partly accepted. The claim is withdrawn and the default is admitted to have no diegetic cue. The table is reframed by the ship and times are on no screen. The review's first fix, raising `WindingWant`, **cannot work**: at three equal weights the lights' share never drops below 333 W against a 300 W want. Its second, lowering the lights' weight, overrides the player's preference. The honest lever is a draw off the top, which the playtest can try as `ds.Nav.FoldDraw` | Decision 4 |
| 6 | `MakeFromX` discards roll and snap-rolls the dome | Accepted, but not the proposed fix. The jump is a pure translation with **no rotation at all**, because the star is already within the cone from the arrival point. Minimal rotation would still jump the dome by up to 8° | Decision 5 |
| 7 | Three sessions or more, not a quick POC | Accepted, as two sessions and about 16 hours. The catalogue, `Primary`, `CatalogStars` and sunlight are cut. The chart moves to a second pass behind console commands, so the loop is playable after the first session. **The power coupling was kept**, against the review's suggestion to defer it: it is two lines in `ApplyAllocation` and a test update, and without it the drive is always charged by the time you have chosen, so the "engage and walk away" half of the loop, the part most worth poking at, never happens | *Implementation outline* |
