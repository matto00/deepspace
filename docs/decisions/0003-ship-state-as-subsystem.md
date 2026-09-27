# 0003 — Ship state is a world subsystem wrapping a plain struct

**Date:** 2026-09-20
**Status:** Accepted

## Context

The ship has state that many things read: power draw, reactor output, installed
modules, and later fuel and hull integrity. It needs a home that every actor can
reach, that lives exactly as long as the level, and that stays testable as the
arithmetic grows.

## Decision

Two layers:

- **`FShipPowerState`** — a plain C++ struct. No `UObject`, no `UWorld`, no
  Unreal types beyond containers. Holds the arithmetic.
- **`UShipSubsystem`** — a `UWorldSubsystem` that owns an `FShipPowerState` and
  exposes it to gameplay. The authoritative ship state.

## Why a subsystem

A `UWorldSubsystem` is an object Unreal creates and destroys alongside each
world automatically. Compared with the alternatives:

- **An actor placed in the level** could be forgotten, duplicated, or deleted,
  and would carry a transform it has no use for. Actors that hold global state
  tend to become god-actors.
- **A singleton** outlives the world, so state leaks between play sessions in
  the editor, and needs manual lifetime management.
- **The subsystem** is reachable from anything with a world context via
  `UShipSubsystem::Get(this)`, has the right lifetime for free, and has no
  transform to tempt anyone into giving it one.

## Why the struct

A world subsystem needs a live `UWorld`, which makes it awkward to test headless.
The arithmetic is the layer that will accumulate the most complexity as ship
systems grow, so it is the layer that most needs to be trivially testable.
`DeepSpace.Ship.PowerState` exercises it with no world at all.

## The discipline

**Consumers ask the subsystem for state; they never store it.** `AShipConsole`
calls `GetReadout()`, which queries the subsystem fresh on every call, and the
HUD widget pulls from `GetCurrentPrompt()` rather than being pushed text. A
consumer holding no copy of the state cannot drift out of sync with it.

## Consequences

- Ship state has exactly one home, which keeps it from scattering across actors.
- The subsystem knows nothing about meshes, rooms, or the player, so ship layout
  and ship systems can evolve independently.
- Anything needing a reaction to state *changes* (rather than reading on demand)
  will need an event on the subsystem. Milestone 1 did not need one.

## Amendment — the ship holds ids, never systems (2026-09-25)

Navigation put the jump in `UShipSubsystem`, beside power and flight, as a
third plain struct: **`FShipNavState`**, pure and headlessly tested
(`DeepSpace.Ship.NavState`), which decides and holds nothing it could ask for.
The subsystem acts on what it decides. This ADR's two layers held without
change.

What needed recording is the boundary with the universe, which did not exist
when this was written. `UUniverseSubsystem` is the one authority on what
exists and on which system a position is in (ADR 0006). **`UShipSubsystem`
stores no universe data beyond ids**, all of them `FSystemId`:

- **the course**, the system it is steering for, held because a course is
  something the *ship* has, the way it has a throttle setting;
- **the last arrival**, which the tick resolves into an arrival point;
- **the visited set**, which the chart marks.

Never a system, a star, a position, a name, and above all never a current
system. Everything else is asked of the universe every time: the chart
(`GetChart`) regenerates on every call, the course's direction is asked of
`GetSystem(PlottedId)`, and which system the ship is in is
`GetSystemAt(ship position)`. A stored current system would be a second
answer that could disagree with where the ship is. `Initialize` calls
`Collection.InitializeDependency<UUniverseSubsystem>()`, which is how Unreal
orders one subsystem's start-up after another's, so the ship may ask from its
own `OnWorldBeginPlay` onward.

The consequence this ADR predicted -- that reacting to state *changes* would
need an event -- did not arrive. The sky and the counter-frame must rebuild
when the ship arrives somewhere, and they poll `GetJumpSerial()`, a number that
bumps on every arrival, as a cache key. It is never an answer: they still ask
what to draw. Nothing pushes, so nothing can be missed.

**Rejected: a separate `UNavigationSubsystem`** reading both. The engine's
power want and the jump's write into the flight state both live in
`UShipSubsystem`, so a third subsystem would need both pushed back through a
public API, and the flight state would have write paths in two places.
**Rejected: the nav state on an actor** (a jump component in engineering):
untestable without a world, and ship state scattered across actors is the
thing this ADR exists to prevent. Machinery for the jump can come later, as a
*view* of this state.

## Amendment — and the id of the body it has marked (2026-09-26)

The system map (`docs/superpowers/specs/2026-09-26-system-map-design.md`,
decisions 5 and 12) gave the ship a **target**: the world in this system the
pilot has marked. The clause above grows by one id: **the ship stores no
universe data beyond the id of the system it is steering for and the id of
the body it has marked**, an `FBodyId`, held in `FShipNavState` beside the
course. Its position, radius, name and kind are asked of procgen every time
(`ShipNav::TargetPlanet`, `UShipSubsystem::GetTargetView`), and a
`PlaceShip` into another system leaves an id that resolves to nothing there,
which draws nothing rather than the wrong world.

Since the developer's ruling of the same day, the course may name that same
body instead of a system -- the in-system jump. It is still an id, and still
the only one of its kind: at most one of the star course and the world course
is set, and a world course is always the target.

One transient that is the ship's own, not the universe's: where an in-system
jump's fold opened (`UShipSubsystem::FoldDeparture`), held for the length of
the fold because the arrival is on the line from there to the world, and the
ship coasts on through the fold far enough, at 1 c, to matter. It is the
ship's past position, not a fact about anything out there, and it is cleared
on arrival.
