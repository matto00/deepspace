# DeepSpace — Vision

**Date:** 2026-09-22
**Status:** Living document. Amend it when a decision contradicts it; do not
let it quietly go stale.

This records what the game is *for*, so that design decisions have something to
be measured against. It is deliberately about feeling and principle rather than
features. Specs in `docs/superpowers/specs/` describe what gets built; this
describes what those things are in service of.

## The premise

You live aboard a ship, alone, in a vast and mostly empty universe. You cruise,
you choose somewhere to go, you jump, you arrive. The ship is your home and
your body; the universe is indifferent to you. That indifference is the point.

The game does not open on a beginning. The starter ship is **worn** — the
player has been living in it for some time before the game starts, and it
should read that way. You are not a recruit being handed a vessel. You are
someone who already lives here.

## The central principle: scale is only felt in contrast

Vastness cannot be shown directly. A big number is not big; a long distance is
not long. Scale is felt only against something small and known.

The ship is that something. It is the only place in the game that is entirely
yours, entirely knowable, and entirely at human scale. Everything else is
larger than you and does not care. **Every design decision that trades away the
intimacy of the ship, or the indifference of the universe, is trading away the
thing the game is about.**

Consequences that follow from this and are not negotiable without revisiting it:

- **Planets are worlds, at Earth's scale.** Not levels with a planet-shaped
  skybox. Where you land determines what you can do there, the way it would on
  Earth — set down in the ocean for water, in a city to trade.
- **Approach takes time, and the time is the content.** A minute or two to
  close with a world, with the option to skim around it looking for somewhere
  worth landing. This is not a loading screen to be minimised. It is the moment
  the scale lands, and shortening it destroys the only thing that makes the
  arrival mean anything.
- **No loading screens.** Making contact with a world is continuous. Design for
  this from the start; retrofitting it is not realistic.
- **Most worlds are empty.** Desolate places good only for raw material are not
  filler — they are what makes a populated world feel like somewhere. A game
  where every planet is interesting has no vastness in it.

## Register

Somewhere between solitude, curiosity and a light, steady precarity.

- **Solitude, not loneliness.** Calm, vast, a little melancholy. The long
  cruise between places is a feature, not dead time.
- **Curiosity pulls you forward.** You go because you want to see what is
  there.
- **The ship needs tending, but you are never fighting for your life.**
  Routine maintenance, not a survival meter. The player should feel competent
  and at home, not harried.

### The anti-factory principle

Ship improvement and ship maintenance must not become an optimisation game.
The distinction is sharp and worth stating precisely:

> **Maintenance is care, not optimisation.** A coolant line you notice is
> sweating, and go fix because the ship is yours, is housekeeping. A power
> split you rebalance to hit a throughput target is a factory.

The same simulation can produce either. The difference is whether the game ever
tells you that you are *behind* — whether there is a rate to keep up with, a
number to maximise, a queue to feed. There must not be. Systems exist to give
the ship texture and to give you reasons to walk to a room, not to be solved.

**Drift signal:** if a design discussion starts talking about throughput,
efficiency, or optimal allocation, it has drifted. Say so.

## How social the game is

**Single-player is the real game.** One player can always run their ship.

Ships scale, and how you build yours decides what it needs. A ship built large
enough can be *easier* with company — but company means **shared presence, not
division of labour**. Friends aboard your ship are there to share the serenity,
not to staff stations. There are no roles, no crew jobs, no assigned tasks. The
moment someone is a resource to be allocated, the anti-factory principle has
been broken and the solitude the game is about has been traded for logistics.

**No netcode yet.** But the door stays open, and that has one concrete
architectural consequence, recorded here because it is cheap now and expensive
later:

> Two players must be able to stand in the same ship and see the same ship.
> Therefore a ship's seed must be authoritative and shareable — **world-level,
> not player-level**. Generation must be deterministic and reproducible from a
> seed, and what is replicated is the *plan*, never the geometry.

## Worlds and why you go to them

- **Desolate worlds** — raw material. Most of them. Empty, indifferent,
  occasionally beautiful.
- **Populated worlds** — trade in high-value goods and services. Rare enough to
  be an event.

Where you land on a populated world matters as much as which world it is. The
extremities of a real planet — ocean, wilderness, city — should be legible from
orbit and should determine what the landing is good for.

## What this is not

- Not a survival game. No starvation, no desperate scramble.
- Not a factory or logistics game. See the anti-factory principle.
- Not a combat game. Combat is not currently part of the vision; if it is ever
  added, it must not become the reason to play.
- Not a story-delivery game. Whatever narrative exists should be found, not
  told. *(See open questions — this is the least settled part of the vision.)*

## Open questions

These are genuinely undecided. Do not treat silence here as a decision.

1. **What role does story play?** The register and the world are settled; the
   narrative is not. Environmental storytelling fits the proc-gen premise best,
   but nothing has been chosen. Explore before committing.
2. **What is in-universe?** Who built these ships, why is anyone out here, what
   is being traded and to whom. Currently blank.
3. **Does the ship move while the player walks around inside it?** This is a
   technical fork with a design answer; see the sub-project on piloting.
4. **How does a ship "scale" such that it stays single-player-safe?** The
   principle is stated above; the mechanism is not designed.

## How to use this document

When a design decision is contested, check it against the central principle and
the register. If a feature makes the ship less intimate, the universe less
indifferent, or the player more harried, it is probably wrong even if it is
fun in isolation.

If a decision here turns out to be wrong, **change this document** rather than
working around it.
