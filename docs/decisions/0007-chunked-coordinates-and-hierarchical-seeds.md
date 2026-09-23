# 0007 — Chunked coordinates, and seeds derived down a hierarchy

**Date:** 2026-09-22
**Status:** Accepted
**Builds on:** ADR 0005 (the ship is the origin), ADR 0006 (generation is C++ and runs at startup)

## Context

`docs/vision.md` takes scale literally: worlds at Earth's scale, approach
measured in minutes, no loading screens. The universe is a galaxy of many
systems, each with planets, each of which has a surface.

A flat coordinate is not capable of that. A double holds integers exactly to
about 9 × 10^15; in centimetres that is roughly **600 AU** — comfortably a
whole solar system, Neptune being 30 AU out, and nowhere near a galaxy, which
is about seven orders of magnitude further. Storing a galaxy-wide position in
one number is not a precision compromise, it is impossible.

ADR 0005 already removed the *rendering* half of this problem: the ship sits at
the world origin permanently and everything else is drawn relative to it, so
Unreal never sees a large coordinate. What remains is how the game represents
where things are, which is pure C++ arithmetic and therefore cheap to get right
and cheap to test.

## Decision

### Positions are chunked, at every level

A universe position is an **integer chunk index plus a local offset**, never a
single absolute number. The local offset is a double in centimetres and always
small; the chunk index is an integer with as much range as needed. Crossing a
chunk boundary rebases the offset and steps the index, so precision never
degrades with distance.

This applies at every level, down to planetary surfaces. A surface is not one
coordinate space; it is chunks, for the same reason and with the same
mechanism.

Illustrative sizing: with a chunk of 1 AU (1.496 × 10^13 cm), local offsets
keep sub-millimetre precision, and a galaxy roughly 6 × 10^9 AU across needs
about 33 bits of index. The exact chunk size is a tuning parameter; the
requirement is that local offsets stay far inside a double's exact range.

### Continuous traversal is always possible

Chunking must let the player cross any boundary **continuously** — between
bodies, between systems, between a planet's surface and space. The coordinate
system must never require a jump in order to avoid an overflow.

Hyperjump therefore exists as **gameplay**, not as a technical necessity. It is
fast travel gated on knowing where you are going; cruising there instead must
always be possible, however long it takes. The vision's ban on loading screens
follows from the same rule.

### Seeds derive down the hierarchy, deterministically

One root seed derives galaxy seeds; a galaxy seed derives its system seeds; a
system seed derives its bodies; a body derives its surface chunks. Derivation
is by **integer hashing only** — no floating point anywhere in it, because
floating point is where cross-platform determinism goes to die.

The same seed always yields the same result, everywhere, forever.

### The stored relation map is a cache, never the source of truth

What exists is *defined* by the generator. A stored map of
universe→galaxy→system relations is an index for finding things without
generating everything, and a place to hang what the player has changed — never
an authority on what is there.

This is the same rule ADR 0006 states for the generator, and it is stated again
because it fails the same way: the moment the map can disagree with the
generator, the project has two definitions of the universe, and the one on disk
will win quietly.

## Why this way

- **It makes galaxy scale free.** A hundred thousand systems cost the same as
  one, because a system is a seed and an index until the player arrives.
  Hardware pressure comes from how much is instantiated at once, which is a
  local budget and does not grow with the universe.
- **Isolated coordinate spaces are the only thing that survives.** Every game
  that has taken planetary scale seriously has arrived at some version of this,
  because there is no alternative that keeps precision.
- **Determinism is worth more than storage.** A deterministic hierarchy means
  a bug report is a seed, a playtest is reproducible, and a shared universe
  replicates an integer rather than a world.

## Consequences

- Universe position is a value type with an index and an offset, not an
  `FVector`. Everything that holds a position holds that type.
- Crossing a chunk boundary is an explicit operation with an explicit test.
  Rebasing arithmetic is where this class of system fails, so it is the part
  that most needs to be headlessly tested.
- Distances between distant things must be computed through the chunk index,
  not by subtracting offsets. A naive subtraction across chunks is silently
  wrong, and will look fine until something is far away.
- Seed derivation needs a chosen, documented integer hash, pinned by a test
  with known values so it cannot drift. Changing it invalidates every universe.
- **Flight spec decision 1 is superseded before implementation.** It specified a
  flat double `FVector` universe position resolving to about 1 AU. That is
  replaced by the chunked type. The spec's single conversion function is what
  makes this a contained change.
- "Stored on the server" currently means stored in the world's own record —
  there is no server. The wording anticipates one, and nothing should assume
  one exists.
