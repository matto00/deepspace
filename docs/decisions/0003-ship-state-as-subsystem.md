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
