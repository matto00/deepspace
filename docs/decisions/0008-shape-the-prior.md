# 0008 — Shape the prior; do not sample and reject

**Date:** 2026-09-22
**Status:** Accepted
**Builds on:** ADR 0006 (generation is C++ and runs at startup)
**Evidence:** `docs/findings/2026-09-22-seeded-generation-probe.md`

## Context

The probe generated ships by drawing parameters uniformly at random and
validating the result. Uniform sampling produced **0 valid ships in 10,000**.
Adding by-construction fixes for the cheap arithmetic constraints brought that
to 53%.

It is tempting to read 53% as a fact about the constraints. It is not. **It is
a fact about uniform sampling.** The probe's own caveat says so: furniture was
scattered at uniform-random room-relative coordinates, and mid-room furniture
is precisely what creates the pockets that dominate the residual reachability
failures. It also drew props uniformly from all eighteen templates regardless
of room, producing a reactor in the galley and three beds in the corridor.

Those ships were rejected for being invalid. The more interesting problem is
that many of the ones that *passed* were not plausible either.

## Decision

**The generator samples from distributions shaped to the thing being built. It
does not sample uniformly and reject.**

Three rules follow:

1. **Invalid states are unrepresentable, not sampled.** Every failure class
   that killed a naive ship — a door taller than its room, an opening wider
   than its wall, a window on an interior wall, a prop outside its room — is
   arithmetic on the plan. The generator must not be able to express them.
2. **Distributions are not uniform.** Room dimensions come from plausible
   ranges, not flat ones. Doors come from a small posture-aware set of standard
   sizes. Furniture is placed against walls with an orientation, and drawn
   according to what a room is *for*.
3. **Rejection sampling is a floor, not a method.** It stays, because
   capsule-width reachability through a furnished ship genuinely requires
   measurement and cannot be designed away. But it catches the residue; it is
   not how ships are made.

## Why

**Cost.** ADR 0006 puts generation at startup and `docs/vision.md` forbids a
loading screen. Rejection sampling toward a desired distribution is expensive
exactly when the desired distribution is far from the sampled one — which is
the case here. Shaping the prior moves that cost from runtime to design time,
where it is paid once.

**Plausibility, which matters more.** Validity is a floor. A ship can be sealed,
connected and fully reachable and still read as random — and a procedurally
generated ship that reads as random undoes the premise, because
`docs/vision.md` says the starter ship should feel like somewhere a person has
been living. **Rejection sampling can only ever buy validity.** Plausibility has
to be in the prior; no amount of filtering puts it there.

This is also where the fiction and the mathematics agree. A ship is a machine
somebody built for a purpose, during a boom, on a route that has since thinned.
The prior should encode what its builder would have done. A generator whose
distributions reflect that produces ships that are both more likely to be valid
*and* more likely to be worth standing in.

**Variance belongs in the dimensions that matter** — what a ship is for, how it
is laid out, what its rooms say about who used it — not in dimensions where
variation only produces absurdity.

## Consequences

- `props.py`'s templates (and their C++ successors) need metadata they do not
  have: anchoring (wall-hung, floor-standing, ceiling), and room affinity. The
  probe found the absence of anchoring is already a latent bug — `counter` and
  `conduit` float when placed mid-room, and `overhead_panel` at elevation zero
  is a ceiling panel lying on the floor that nothing catches.
- Room graphs are built posture-aware from the start: crouch-height rooms are
  leaves or are bypassed by a loop, because a crouch room used as an interior
  node severs standing access to its whole subtree.
- The corridor spine is a structural decision the generator makes, not an
  outcome it hopes for.
- The validator keeps its job unchanged, and the generator property test from
  ADR 0006 — every seed yields a valid plan — becomes a realistic target rather
  than an aspiration.
- The 53% figure should not be quoted as a budget. It measures a deliberately
  naive placer. The real acceptance rate under a shaped prior is unmeasured,
  and the probe's untried wall-aware placement tier is the cheapest way to find
  out.

## Amendment — this applies to all randomness, not just layout (2026-09-22)

The rule above was written about ship layout. It generalises: **wherever this
project draws a random value, reach for the distribution that actually
describes the thing, rather than defaulting to uniform and correcting later.**

Uniform is the right answer surprisingly rarely. It describes "any value in
this range is equally likely", which is true of almost nothing in a world.
Reaching for it by habit is how generated content comes to feel generated.

Some defaults worth knowing, none of them exotic:

| Shape of the thing | Distribution |
|---|---|
| Time until the next event; gaps between arrivals | **Exponential** (memoryless) |
| How many events fall in a fixed interval | **Poisson** |
| Component wear and failure | **Weibull** — its shape parameter *is* early-life vs random vs wear-out failure |
| A magnitude produced by many multiplied factors — deposit sizes, settlement populations | **Log-normal** |
| Heavy-tailed counts where a few are enormous | **Power law / Pareto** |
| A bounded proportion, 0 to 1 — condition, purity, how worn | **Beta** |
| A sum of many small independent effects | **Normal** |

Weibull is worth calling out because it lands exactly on a decision already
made. `docs/vision.md` describes wear as visible early, absent in the middle,
and resurfacing late — which is the classic bathtub curve, and a Weibull shape
parameter expresses precisely that. The fiction and the standard reliability
model agree, so use the model.

Three constraints on this:

- **Determinism first.** ADR 0007 requires integer hashing and no floating
  point in seed derivation. Sampling a shaped distribution happens *after* a
  deterministic integer stream has been derived, never as part of deriving it.
- **Tails need bounds.** Log-normal and power-law tails will eventually produce
  a settlement of four million people or a corridor two kilometres long.
  Truncate deliberately and record the truncation; an unbounded draw is a bug
  waiting for a rare seed.
- **Say why in the code.** A line picking Weibull over uniform should say what
  it believes about the world. That comment is the design, and without it the
  next person reverts it to a uniform draw because it looks simpler.
