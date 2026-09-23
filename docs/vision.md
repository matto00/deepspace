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
- **The ship needs tending, but tending is never a chore.** Routine
  maintenance, not a survival meter. The player should feel competent and at
  home, not harried. The objection is to obligation, not to danger — see the
  anti-chore principle.
- **The cruise is when you live in the ship.** Long transits are not dead
  time because you spend them on your feet — walking the deck, tending
  something, watching the stars go by from the galley. A cruise the player
  can only watch from a chair is a loading screen with a chair in it. This is
  why open question 5 is a constraint rather than a trade-off.

### The anti-chore principle

This is the load-bearing rule of the whole document, and it is broader than it
first appears. **Nothing in this game may feel like a chore or a nuisance.**
Not maintenance, not travel, not pirates, not anything added later.

The test is whether the game ever tells you that you are *behind* — whether
there is a rate to keep up with, a number to maximise, a queue to feed, or an
interruption to be serviced. A thing the player *chooses* to attend to is
content. The same thing, demanded on the game's schedule, is a chore.

Two cases, both of which have already come up:

> **Maintenance is care, not optimisation.** A coolant line you notice is
> sweating, and go fix because the ship is yours, is housekeeping. A power
> split you rebalance to hit a throughput target is a factory.

> **A threat is an event, not an interruption.** Pirates you choose to fight
> or to run from are content. Pirates that turn up on a timer and must be
> dealt with before you can get on with what you were doing are a nuisance,
> however well they are simulated.

Note what this rule does *not* say. It does not say the game must be safe, or
gentle, or without stakes. Danger is fine. Difficulty is fine. Being thrown
across the compartment when the ship banks hard is fine and desirable. What is
not fine is obligation.

**Drift signal:** if a design discussion starts talking about throughput,
efficiency, optimal allocation, or how often something should spawn, it has
drifted. Say so.

## How social the game is

**Single-player is the real game.** One player can always run their ship.

Ships scale, and how you build yours decides what it needs. A ship built large
enough can be *easier* with company — but company means **shared presence, not
division of labour**. Friends aboard your ship are there to share the serenity,
not to staff stations. There are no roles, no crew jobs, no assigned tasks. The
moment someone is a resource to be allocated, the anti-chore principle has
been broken and the solitude the game is about has been traded for logistics.

**No netcode yet.** But the door stays open, and that has one concrete
architectural consequence, recorded here because it is cheap now and expensive
later:

> Two players must be able to stand in the same ship and see the same ship.
> Therefore a ship's seed must be authoritative and shareable — **world-level,
> not player-level**. Generation must be deterministic and reproducible from a
> seed, and what is replicated is the *plan*, never the geometry.

## Flight modes

Flight has (at least) two modes, and the distinction is a design tool as much
as a fiction:

- **Cruise** -- gentle. Modest directional and rotational rates. This is the
  default, and it is what most of the game is. The player walks the ship
  comfortably while it flies.
- **Combat** -- fast. Much higher directional and rotational rates, enough
  that anyone on their feet is thrown around. **A later ship upgrade**, not
  something the player starts with.

Making combat manoeuvring an upgrade is deliberate and load-bearing: it means
the early game never produces violent motion at all, so the hard problem of
bodies being thrown around a rotating interior can be solved later, against a
ship that has earned it, rather than blocking flight from existing.

## How ships wear

Ships are real machines and they age. Components degrade -- but the timescale
is the whole design, and it is deliberately lopsided:

- **Early game:** degradation is visible almost immediately, because it is how
  the ship tells you it has a history. A starter ship that never needed
  anything would read as new. This is also how the player learns that
  components can be improved at all.
- **Mid game:** effectively absent. Players upgrade components long before
  they wear out, so repair rarely comes up. This is intended, not a gap.
- **Late game:** upgrading slows down, so wear starts to surface again on its
  own. Here it is a mild retaining pressure -- something to come back to,
  infrequent enough that it is never overbearing.

**Combat damages components.** That is a separate path from wear and it is an
event, which is exactly what the anti-chore principle wants.

### The boundary this sits on

Late-game wear is the one place the anti-chore principle is deliberately run
close to its limit, so the limit is worth stating rather than discovering.

The principle's test is whether the game tells the player they are *behind* --
whether there is a **rate to keep up with**. Wear measured in tens of hours
does not create a rate; it creates occasional events that happen to have a slow
cause. Wear measured in tens of minutes creates a rate, and at that point it is
a survival meter with a longer fuse.

So: **decay may be slow enough to be an occasional event, and never fast enough
to be a schedule.** If a design ever needs the player to check something
periodically to avoid a consequence, it has crossed the line, however gentle
each individual check is. A chore is not made acceptable by being rare; it is
made acceptable by the player choosing when to do it.

## Inhabitants

**The universe has people in it.** NPCs are a definite part of the game, not a
maybe. They are what makes a populated world populated, and they are the most
likely source of whatever danger exists.

This does not contradict the solitude the game is about. Solitude is the
default and the texture of the long cruise; people are the exception that
makes the default legible. A universe with nobody in it is not lonely, it is
just empty.

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
- Not a factory or logistics game. See the anti-chore principle.
- Not, currently, a combat game -- but this is genuinely undecided rather than
  ruled out. See open questions.
- Not a story-delivery game. Whatever narrative exists should be found, not
  told. *(See open questions — this is the least settled part of the vision.)*

## Open questions

These are genuinely undecided. Do not treat silence here as a decision.

1. **Is there combat, and what shape is it?** The likely direction is space
   pirates the player either fights or evades -- NPC at first, possibly other
   real players eventually. Undecided, and deliberately left as a viable
   option rather than adopted. The binding constraint if it happens: it must
   pass the anti-chore principle. Pirates that read as a recurring tax on
   travel would be worse than no pirates at all.
2. **If there are hostile players, what happens to "shared presence"?** The
   social model above is friends sharing a ship. Player pirates are hostile
   strangers, which is a different game with different requirements -- trust,
   authority over ship state, consequences for loss. Both can coexist, but
   only deliberately. Do not let PvP arrive as a side effect of adding
   pirates.
3. **What role does story play?** The register and the world are settled; the
   narrative is not. Environmental storytelling fits the proc-gen premise best,
   but nothing has been chosen. Explore before committing.
4. **What is in-universe?** Who built these ships, why is anyone out here, what
   is being traded and to whom, and who the NPCs are. Currently blank.
5. **How does the player stay aboard a manoeuvring ship?** The *design* half
   is settled and is a constraint: **the player is never locked out of the
   interior while the ship is flying.** Being sent to a seat whenever the ship
   turns was considered and rejected. The *frame* half is now settled too --
   the ship is the origin and the universe moves around it, so the interior
   never moves at all; see ADR 0005. What remains open is the feel: at what
   point does a manoeuvre take the player off their feet, and how is losing
   and regaining footing made legible rather than confusing.
6. **How does a ship "scale" such that it stays single-player-safe?** The
   principle is stated above; the mechanism is not designed.

## How to use this document

When a design decision is contested, check it against the central principle and
the register. If a feature makes the ship less intimate, the universe less
indifferent, or the player more harried, it is probably wrong even if it is
fun in isolation.

If a decision here turns out to be wrong, **change this document** rather than
working around it.
