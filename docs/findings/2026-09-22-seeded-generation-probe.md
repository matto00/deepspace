# Findings — how hard is seeded ship generation?

**Date:** 2026-09-22
**Method:** throwaway Python generator over the existing pure modules
(`floorplan`, `props`, `placement`), validated by a parameterised copy of
`validate_hauler.py`. Nothing in `Tools/` was modified. A parity check
confirmed the copied validator still passes the real hauler unchanged, so the
rates are not an artefact of a drifted validator.

**Corpora:** naive 10,000 seeds; six intermediate tiers of 500; final
configuration 10,000. Quote the 10,000-seed runs; the tiers under-count because
`check_reachability` returns early when the Player Start cannot stand
(`validate_hauler.py:236-238`), hiding every failure behind it.

## The headline

- **Naive random parameters: 0 valid ships out of 10,000.** Not one reached the
  voxel grid; all died in `PlanError` during plan construction.
- **With cheap by-construction fixes: 53%** (5,299/10,000).

So rejection sampling alone is hopeless, and rejection sampling *after*
constructing the cheap constraints correctly is comfortably affordable.

## What kills a naive ship (n=10,000, all fatal before validation)

| Failure | Share |
|---|---|
| door taller than its room | 44.7% |
| opening wider than the wall it sits in | 29.6% |
| window or seal on a non-exterior wall | 17.4% |
| prop leaves its room | 7.4% |
| window sill/head out of range | 0.7% |
| seal taller than its room | 0.2% |

All of it is arithmetic on the room table. **These states should be
unrepresentable in the generator, not sampled and rejected.**

## The two killers that are graph properties, not geometry

Neither is locally visible, and missing either costs roughly 90% of the corpus.

1. **Posture connectivity.** A 150 cm door between two standing rooms silently
   severs standing access to everything beyond it. A crouch-height room used as
   an *interior node* of the room graph does the same to its entire subtree.
   The rule that fixes it: crouch-height rooms must be leaves, or bypassed by a
   loop — which is exactly what the hauler's crawlway already is. This was the
   single largest killer before it was enforced.
2. **A guaranteed slide corridor.** Forcing a spine of at least 1300 cm and
   keeping props out of it moved the corpus from 7.2% to 53%.

## What is structural, and what genuinely needs measuring

**Structural — satisfiable by construction:**

- Every `PlanError` class above.
- **Hull sealing: zero failures in all 14,300 ships that reached the grid.**
  Walls are *derived* from interiors, so a leak is unreachable by construction.
  `check_hull` is now a regression guard against the old per-edge wall code,
  not a live constraint.
- **Connectivity.** All 352 disjoint-box failures came from two props,
  `counter` and `conduit`, which only touch anything when flush to a wall.
  Restricting the pool to self-supporting props took it to zero. This is prop
  metadata, not geometry search.
- **Keep-clear zones.** 99% failure at tier 1, zero once props are AABB-tested
  against zones the plan already derives.

**Genuinely measured — needs the voxel grid and rejection:**

- **Capsule-width reachability, 42.6% residual.** Hand-diagnosed on ~40 failing
  ships: mostly whole rooms or pockets *within* a room cut off by furniture,
  not the region point itself. A 90 cm door leaves only 3 of 9 cells standable,
  so clearance compounds non-locally. No cheap predicate caught this; the flood
  fill did.
- Crouch reachability (13.8%) by the same mechanism, and residual slide
  clearance (10.9%) even in a long prop-free corridor, because standability
  along the centreline depends on the adjoining rooms' walls and doors.

## Determinism — confirmed

Five repeats of each of five seeds gave exactly one distinct result per seed,
hashing both the resolved box list and the whole layout spec. Across the
10,000-ship corpus: 10,000 distinct box hashes, no collisions.

## Performance — one earlier assumption corrected

The proc-gen audit flagged `check_components`' O(n²) loop as a scale risk.
**Measured, it is 0.3% of runtime** — 0.0066 s on the hauler — and would need
roughly 2,000 boxes to matter. It is not the problem.

The real cost is two traversals, both O(cells) in pure Python: `check_hull`'s
3D air flood (~45%) and `standable`'s per-cell capsule test (~50%). Allocating
and filling the dense grid is 0.5%. Total ~1.5 s for the hauler, median 2.28 s
across generated ships (1.32M–9.71M cells).

## Latent problems in the existing modules

1. **`props.py` has no anchoring metadata.** `counter` and `conduit` are only
   valid flush to a wall; mid-room their parts float. Nothing in the data says
   so — it is encoded in `hauler_layout.py`'s hand-picked coordinates. Worse,
   `overhead_panel` at elevation 0 is a ceiling panel lying on the floor and
   **nothing catches it.**
2. **A room with no door at all is silently legal.** The plan builds it, the
   hull check passes (the flood starts elsewhere), and the component check
   passes (its walls touch its neighbours'). It is caught today only because
   every room happens to have a region. A generator must emit a region per room
   or it will ship sealed rooms.
3. **`Ship.regions` drops the room**, which is why the validator reaches back
   into module globals for `floor_hit`'s ceiling bound. A fourth field removes
   the coupling.
4. `standable()` assumes a flat floor across the capsule's footprint — its own
   comment says so. Multi-deck needs per-neighbour floor heights.
5. `KEEP_CLEAR`, `SLIDE_RUN`, `CONSOLE_WIDTH` and the movement contract are
   globals on one ship module; they are ship-independent.

## Honesty about the 53%

It is a **lower bound from a deliberately dumb placer**, and the bias is
consistent — reachability looks harder than a competent generator would make it:

- Furniture is scattered at uniform-random room-relative coordinates. Real
  furniture goes against walls, and mid-room props are exactly what creates the
  pockets dominating the residual 42.6%.
- Props are drawn uniformly from all 18 templates regardless of room.
- **Rooms are grown from a spanning tree, so "rooms share exactly one wall" was
  assumed, not measured.** That is a genuine gap.

The wall-aware placement tier was queued and not run, so the effect of placing
furniture against walls is unmeasured. No better figure is extrapolated here.

## What this means for the generator

Per ADR 0006, generation is C++ running at startup. The shape that follows:

- Build rooms from a graph with posture-aware doors; derive openings from
  measured wall spans; give props anchoring metadata; place furniture against
  walls. All of that is by construction.
- **Keep the validator.** The last constraint is not going to be designed away.
- Budget **2–3 attempts per ship** at runtime, and make the furnishing pass
  incremental and reversible rather than regenerating the whole ship on a
  rejection. Startup has to absorb that, and the vision forbids a loading
  screen.
