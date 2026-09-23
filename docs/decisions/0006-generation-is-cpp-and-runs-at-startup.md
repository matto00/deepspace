# 0006 — Generation is C++ and runs at startup

**Date:** 2026-09-22
**Status:** Accepted
**Supersedes the delivery mechanism of:** ADR 0004 (which stands on *why* to
generate, and on the validator/verifier discipline)

## Context

ADR 0004 established that level geometry is generated rather than hand-placed,
and that the generator is checked in two layers: a validator proving the plan
is sound, and a verifier proving the built level matches the plan. That
reasoning is unchanged and this ADR does not touch it.

What it did not settle is *when* generation runs. Today it runs at editor time:
`Tools/build_hauler.py` executes as a commandlet, spawns actors into
`Content/Maps/L_Hauler.umap`, and saves the level. The game then loads a map
that was generated earlier. Generation is a build step, not a game mechanism.

That is the wrong shape for a game whose premise is procedural. A generator
that only runs in the editor can produce exactly the ships a developer thought
to build, one at a time, by hand, with the editor open.

## Decision

**Procedural generation is C++ that runs as part of game startup.** Not an
editor commandlet, not a build step, not a pre-baked `.umap`.

**Python is for prototyping and testing only.** `Tools/*.py` keeps its value as
a research instrument — it runs in about a second with no editor, which makes
it the cheapest place to answer questions about the generator's behaviour — but
it is not the implementation and it does not ship. Nothing the game does at
runtime may depend on it.

## Why now, rather than later

Because retrofitting it is the expensive order. A project that hand-builds its
first ship and generates later discovers that everything downstream assumed a
fixed level: the pilot seat's coordinates, the console's mount, the player
start, the light budget, the material set. Each of those becomes a small
migration. Deciding it now means the seams are built as seams.

It is also the difference between procedural generation being **a core
mechanism** and being a feature bolted onto a hand-made game. The premise of
the game is that the universe is generated; a ship that is generated only on
the developer's machine does not deliver that.

## What this changes

- The generator becomes C++ in the runtime module, not Python in `Tools/`.
- `Content/Maps/L_Hauler.umap` stops being the ship. The map becomes a nearly
  empty world that the generator populates at startup.
- The pure geometric core of the Python (`floorplan`, `props`, `placement`,
  and the layout data) ports to a plain-C++ layer with no Unreal types beyond
  containers — the shape `FShipPowerState` and `FShipFlightState` already
  establish.
- Material authoring has **no runtime equivalent** — materials cannot be
  compiled at runtime, and static switch parameters cannot be set on a dynamic
  instance. Materials become pre-authored assets the generator selects and
  parameterises, rather than assets the generator creates.
- Mesh bounds must still never be assumed (CLAUDE.md: meshes disagree on pivot
  placement). `UStaticMesh::GetBoundingBox()` exists at runtime, but the mesh
  must be loaded first — so bounds should be baked into a data asset at cook
  time, with a test asserting the baked values match the live ones.
- Geometry should be instanced components on one actor rather than N actors.

## What must not be lost

ADR 0004's two layers are the reason the generated ship is trustworthy, and
they survive in a changed form:

- **The validator** checks a *plan* is sound and keeps its intent. It is pure,
  so it ports directly and should stay runnable offline over a corpus of seeds.
- **The verifier** checks the *built* result matches the plan. It becomes more
  necessary, not less: moving from actors to instanced components is exactly
  the kind of change that reintroduces the pivot bug in a new costume.

**There must remain exactly one generator.** If generation moves to C++ while
the validator stays in Python, the project has two generators that will drift,
and a validator that passes plans the game cannot build. That is the specific
failure ADR 0004 exists to prevent, reintroduced at a new seam.

A third check is needed that does not exist today: a **generator property
test** asserting that every seed yields a valid plan, and that the same seed
always yields the same plan.

## Consequences

- Startup does work it does not do today. Generation must be fast enough to
  not read as a loading screen — which `docs/vision.md` rules out.
- An intermediate artefact is implied: the generator produces a *plan*, and
  the plan produces geometry. Keeping that seam explicit is what lets the
  validator run offline and what keeps a saved ship stable when the generator
  changes later.
- Until this lands, `Tools/build_hauler.py` remains how the ship is built. This
  ADR sets the direction; it does not by itself delete anything.
