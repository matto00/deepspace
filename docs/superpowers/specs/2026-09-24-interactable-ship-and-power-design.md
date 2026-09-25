# The interactable ship: power, screens, and the engineering console

Status: approved 2026-09-24. Supersedes nothing; extends `FShipPowerState`.

## What this is for

The hauler currently has power *arithmetic* — a reactor output, a set of
draws, a headroom number — and nothing that can be done about any of it. This
makes the ship somewhere you live: the engineering console powers the lights,
power is split between lights, boosters and the engine, and the split is set
from the laptop on the galley table.

Read `docs/vision.md` first. Two rules bind this feature harder than any
other so far, because power management is the single easiest way to ruin the
game:

**The anti-chore principle.** Nothing may feel like a chore or a nuisance;
the test is whether the game ever tells the player they are *behind*. A power
system that must be tended, that decays, that drifts out of tune while you are
elsewhere, or that punishes you for not visiting a screen, fails. Allocation
is a **preference you set**, not an obligation you service.

**Shared presence, never division of labour.** One player must be able to run
the whole ship. Nothing here may require a second person, and nothing may
become "the engineer's job".

## Decisions

### 1. Screens are surfaces in the world, driven by a pointer

A `UWidgetComponent` renders real Slate onto a quad in the ship, and the
player drives it with a `UWidgetInteractionComponent` pointing along the view.
No fade, no fullscreen takeover, no pause: the screen is a thing in the room,
and walking away from it mid-edit is allowed and means nothing.

Chosen over a fullscreen menu because the whole register of the game is that
you are *aboard* somewhere, and over keyboard-driven highlights because
allocation is slider-shaped and a highlight-and-commit scheme makes a
continuous quantity feel like a form.

`UWidgetInteractionComponent` needs no OS cursor: it drives a virtual Slate
user (index 8+) from a world-space ray. Interaction keeps the existing reach
rules — the pointer is live only while a screen is within `InteractionRange`
and roughly faced, so the player cannot operate the console from across the
room.

**Because this is the riskiest part to get feeling right, it is built
first**, against a throwaway test screen, before any power UI exists. The
whole feature ships in one pass, but the pointer must be working and playable
early in that pass, not discovered at the end.

### 2. Under-powered systems degrade; they never fail

Every consumer has a **want** and receives a **share**. `Satisfaction =
Share / Want`, clamped to 0..1, and each consumer reads its own satisfaction
and behaves proportionally worse:

- **Lights** dim — intensity scales, and below roughly a third they warm and
  flicker rather than switching off.
- **Boosters** push softer — `LinearAcceleration` scales.
- **Engine** charges slower — affects time-to-ready, never reliability.

There is no cutoff, no alarm, no timer, and no failure state. A badly
allocated ship is a *dim, sluggish* ship, which the player reads immediately
and fixes if they care. This is the anti-chore principle made mechanical:
the ship never tells you that you are behind, it just feels how you set it.

Life support is deliberately **not** a consumer. The moment survival depends
on an allocation, the screen becomes a chore with a death timer attached.

### 3. Allocation is a stable split, not a live optimisation

The player sets a **weight** per consumer. Available reactor output is shared
out in proportion to weight, capped at each consumer's want; anything left
over by the cap redistributes to consumers still short, so setting a silly
split wastes nothing silently. Weights persist. Nothing drifts on its own.

A consumer that is switched off (lights off at the console) has want zero and
participates in no split, which is what makes "kill the lights to boost" a
real and immediate choice rather than a menu exercise.

### 4. The subsystem stays authoritative; screens are views

`FShipPowerState` grows the allocation model, stays a pure struct with no
Unreal types beyond containers, and stays headlessly unit-testable — it is
named in `CLAUDE.md` as the complexity sink and this is the complexity.
`UShipSubsystem` wraps it. The console, the laptop and every light **ask** the
subsystem; none of them store power state. Two screens editing the same
allocation must agree because they are both views of one value.

### 5. Generated actors are addressed by tag, never by name or index

`build_hauler.py` labels lights `hauler_<label>`. The builder additionally
applies an **actor tag** naming the consumer group (`Power.Lights`), and C++
finds them by tag. Names are for humans and indices change whenever the
layout does; a tag is a contract the generator can keep. This matters beyond
lights — it is how all generated geometry will be addressed once proc-gen
moves into C++ (ADR 0006).

## Shape of the work

1. **Pointer prototype.** `UWidgetInteractionComponent` on the character,
   gated by reach; one throwaway world screen with a button and a slider that
   visibly respond. Playable before anything else lands.
2. **Allocation in `FShipPowerState`.** Consumers, wants, weights, shares,
   satisfaction, redistribution. Pure C++, tested headlessly, no world.
3. **Consumers respond.** Lights dim by tag; boosters scale
   `LinearAcceleration`; engine charge rate scales. Each reads satisfaction
   from the subsystem every frame or on change — never caches it.
4. **The engineering console.** An interactable in engineering carrying a
   screen: lights on/off, and the current draw and headroom, read live.
5. **The laptop.** The screen on the galley table: the allocation editor, one
   weight control per consumer, showing each consumer's satisfaction as it
   changes.

## How this is verified

- `FShipPowerState` allocation is unit-tested headlessly: proportional split,
  the cap-and-redistribute rule, zero-want consumers excluded, weights summing
  to zero handled, satisfaction clamped. This is the layer bugs will live in.
- A world test asserts a light tagged `Power.Lights` actually changes
  intensity when the allocation changes, so the tag contract is guarded.
- A test asserts two screens bound to the same allocation report the same
  value, because "screens are views" is the property most likely to rot.
- Playtest, by eye: the pointer feels like pointing at a thing, dimming reads
  as dimming rather than as a bug, and nothing anywhere nags.

## Risks

- **The pointer feeling bad** is the main one, and why it is built first. If
  it cannot be made to feel good, the fallback is a fullscreen screen — that
  is a real design retreat and must come back to the user, not be taken
  quietly.
- **Slate on a world quad is easy to make unreadable.** Text must be sized
  against the real distance a player stands at, not against the editor
  preview.
- **Scope.** This is the whole feature in one pass, chosen deliberately. If
  it runs long, the cut line is the laptop: the console alone with lights
  responding is a shippable, coherent slice.

## Addendum, 2026-09-24: decisions taken during delivery

Both were raised by the implementation rather than buried in it, and both
were ratified by the user as built.

**The console has no power gate of its own.** `BP_ShipConsole` used to carry
`bIsPowered` and a "Power on" verb. That is a second copy of on/off state
sitting next to an authoritative one, which decision 4 forbids; removing it
leaves E at the console doing the same thing the screen's button does —
toggling the ship's lights. One switch, one source of truth. A console is a
*view*, and a view does not hold state.

**The engine's share charges a jump drive.** "Engine charges slower" implied
something to charge, and nothing existed, so `FShipFlightState` gained a 0..1
jump charge that fills at a satisfaction-scaled rate (90 s at full feed).
Nothing consumes it yet, which is deliberate rather than unfinished:
hyperjump is already in `docs/vision.md`, and charge-over-time is what the
engine's power share should mean. The consumer arrives with charts and
navigation. Until then the engine is a consumer whose effect is a number
climbing — visible on the laptop, doing nothing else.

Two further judgments are recorded here as flagged, not ratified, because
they can only be settled by eye:

- **Satisfaction is shown as watts and an unnumbered bar** — `"180 W of
  300 W"`, never `"60%"`, never a total, never a target, never a suggested
  split. This is the sharpest test the anti-chore principle gets: the vision
  forbids telling the player they are under-performing, while the spec
  requires showing satisfaction. If the laptop ever reads as a score, it has
  failed and the numbers go.
- **Consumer wants are constants** (300 / 450 / 500 W against a 1000 W
  reactor), deliberately over-subscribed so the split is always a real
  choice. Tunable in `ShipSubsystem.h`.
