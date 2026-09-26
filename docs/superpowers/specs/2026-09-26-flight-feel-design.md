# DeepSpace — Flight Feel: Two Levers, a Soft Cap, and Speed You Can See

**Date:** 2026-09-26
**Status:** Revision 3 — amended with the developer's rulings (below), not
implemented
**Answers:** the developer's second playtest, notes 1, 2 and 4 and its
"mainly" paragraph, under rulings A and B (below). Note 3, the system map, is
a sibling spec (`2026-09-26-system-map-design.md`); the seams with it are in
*Seams with the system map*.
**Follows:** the sky (implemented; its decision 8, the drive, is replaced
here), navigation and arrival (implemented; the jump is untouched but for
one line, decision 4)
**Builds on:** ADR 0002 (logic in C++), ADR 0003 (ship state as a
subsystem), ADR 0005 (the ship is the origin)
**Governed by:** `docs/vision.md`: *Approach takes time, and the time is the
content*, *the anti-chore principle*, *the cruise is when you live in the
ship*; CLAUDE.md: *Flying*, *The drive and the jump*, *The sky*
**Built in the order of:** `docs/superpowers/plans/2026-09-26-poc2-build-order.md`

## Amendment, 2026-09-26: the developer's rulings

The developer read revision 2 and the sibling map spec together and ruled on
both sign-off lists. The rulings are binding and override anything below that
disagrees. Every decision they change has been revised in place, and each
superseded choice is kept beside it as a rejected alternative, as the
revisions before this one were.

1. **The drive tops out at 1 c.** Verbatim: "Top out at 1c, anything faster
   should be a jump (we should eventually limit the amount of jumps before a
   cooldown period. 1c already feels somewhat like quite the stretch from
   realism. there can be an option to 'jump to planet' within a system". The
   lever is now STOP plus eighteen notches on the 1-2-5 series, 1 km/s up to
   and including 1 c (decision 3), a tap per notch counted from the ship's
   present speed, eased (decisions 3 and 4). The approach tables are
   recomputed for it (decision 5). **At 1 c, 1 AU takes 8 min 19 s and 30 AU
   takes 4 h 9 min**; the median leg from an arrival to the innermost world
   (0.2 AU) is 100 s, the 95th-percentile leg (2.5 AU) 21 minutes, and the
   longest procgen makes (138 AU) 19 hours. That is why the **in-system
   jump** exists: with a world targeted, the existing jump machinery carries
   the ship to a standoff above it. It belongs to the sibling spec (its
   decision 12). This spec's share is that every jump, interstellar or
   in-system, is an all stop and arrives at rest (decision 4). The jump
   cooldown the ruling mentions is future work and is not built; the sibling
   spec records it as an open question, with its tension against the
   anti-chore principle.
2. **The soft cap as specified** (decision 5): only when the nose's ray meets
   a floor sphere, reach-it-in-4-s, finishing on the braking curve, with the
   floor at the sky's rendered floor (decision 6), about 10 km over an Earth,
   which is where landing will later take over.
3. **A live ETA.** Verbatim: "Live ETA, i don't recall ruling out a countdown
   in this regard. This would be good." The developer is right. The
   no-countdown rule was made for the **jump's charge**, a filling bar the
   player waits on, and CLAUDE.md had generalised it to every screen. That
   sentence is scoped back to the charge. The target line and the map (the
   sibling spec's decision 6) show a live time to arrival at the present
   speed, computed with this spec's own cap law, the new pure
   `ShipFlight::SecondsToFloor` (Track 0). Decision 7 is revised: the
   bottom-left corner still shows no time, because it has no destination.
4. **Picking a target: both, and the seats change.** The sibling spec's
   (its decisions 2, 4 and 13). This spec's share is the input action the
   copilot's Tab needs, `IA_CycleTarget`, which Track A builds with the lever
   actions so `BP_DeepSpaceCharacter` is recompiled once, not twice.
5. **Everything else as recommended**: each mode keeps its lever across F; X
   stops both; after X the lever restarts from STOP (decision 1); every jump,
   interstellar and in-system, arrives at rest with both levers at STOP
   (decision 4); the dust knee law, judged in play (decision 8); star surfaces
   honest T^4 with the 8x ceiling, with `StarSurface` chosen live from frames
   (decision 9).

The build order both specs follow is
`docs/superpowers/plans/2026-09-26-poc2-build-order.md`. This spec's Track 0
is stage 1, Track A is stage 2, and Tracks B and C are stage 3.

### Revision 2

A review of the first draft found the soft cap unstable, and it was. The
first draft cut only the *closing* part of the velocity and left the
sideways part at the lever's speed. Once that cap binds, the sideways part
swings the target across the view at v sin(theta) / r, which grows without
limit as r shrinks: an aim error of a thousandth of a degree (a tenth of a
pixel) at 10 c from 0.2 AU closed to a million kilometres and threw the ship
out past the world at the lever's speed. The ship chose its own path, which
is the complaint. It also held back ships that would have missed, against
ruling B's "would hit". Decision 5 is rewritten: **the cap applies only when
the nose would meet a world, and then to the whole speed**, so the ship always
goes where it points. The same review found the speed could snap up when the
cap let go, the ship could tunnel through a body it was not nearest to, a
lever left set flew the arrival into the star, and several states were
unspecified. Each is fixed where it arises below. The superseded choices are
kept as rejected alternatives.

## Context

The second playtest, verbatim:

> 1. speed drops off a little too much on the approach. to the point where
> landing starts to take too long (never actually tried to land because of
> this, at 100km out i started moving less than 1km/s and was slowing down).
> 2. illusion of moving quickly is stronger at 200 m/s than in drive mode,
> this makes navigating very confusing.
> [3. the system map -- the sibling spec]
> 4. some stars are too bright, where looking dead ahead means you can't see
> anything else ahead of you, this is realistic but can maybe be dialed back
> slightly.
> mainly, aside from high-level system navigation, we should focus on dialing
> the speed control and how that feels to the user. right now it seems
> largely out of control, and over controlled. i want to be able to speed up
> / slow down / stop more deliberately, and the auto-slow down is too strong.

The developer then ruled, and these are binding here:

- **A. Two modes, both with a lever.** Cruise and the drive stay separate, F
  toggles between them, and the drive gets its own speed lever instead of the
  automatic approach. The player sets the speed in both.
- **B. Near bodies: a soft cap you can see.** The lever sets speed; only when
  the ship would hit a surface within a few seconds does a cap hold it back,
  and the HUD says so. Much gentler than now, and it lets the ship come down
  low, where landing (the next sub-project, not this one) will take over.

"Out of control, and over controlled" is one complaint, not two. The ship's
speed is chosen by something other than the player (out of control), and
that something overrides what the player asks for (over controlled). Every
decision below is measured against the same test: **a change of speed is one
the player asked for, is felt, arrives predictably, and never fights them.**
And a second test, which the first draft failed: **the ship goes where the
nose points.**

### What the code does today

Designed against the code, not the sky spec's account of it. Where the two
differ, the code is what is described.

- **There is no drive lever.** `FShipFlightCommand` has one `Throttle`, -1..1,
  and a `bDrive` flag. F flips the flag, and the flag changes what the one
  throttle *means*: `speed = Throttle x max(200 m/s, Room / DriveTau)` along
  the nose, and if the closing part exceeds `|Throttle| x Room / DriveTau` the
  *whole* velocity is scaled down to fit (`ShipFlightState.cpp`, `SubStep`).
  Half throttle doubles tau. So the lever is a fraction of a speed the
  distance chooses, and at 1 AU full throttle is 34 c whether or not the
  player wanted 34 c.
- **The lever lives on the pawn.** `ADeepSpaceCharacter::Throttle` is swept
  there at the `ThrottleSweepRate` `UPROPERTY` (0.5 a second, four seconds
  stop to stop) from a held `IA_Throttle` axis (Shift and Ctrl, `Triggered`
  and `Completed`), and pushed into the ship every frame through
  `SetFlightCommand`. The ship's command is a mirror of a copy the pawn
  keeps, against CLAUDE.md's "consumers ask the subsystem for state; they
  never store it". Two levers per mode cannot live there.
- **The approach never arrives.** Closing is held to Room / 15 s, so the room
  falls by e every 15 s all the way down and reaches the 100 km floor only
  asymptotically. The developer's "under 1 km/s at 100 km" is the drive
  closing at Room / 15 s = 1 km/s with 15 km of room: 115 km up, about 218 s
  after leaving a typical 0.2 AU arrival. The HUD's `DRIVE FLOOR` needed a 5%
  band (`ds.HUD.FloorBand`) only because the floor is never reached.
- **It is tunnel-proof only by accident.** Speed is a fraction of the room to
  the *nearest* surface, so one substep can never cross any surface. Remove
  that and the drive can cross a moon beyond a giant in one substep.
- **The room is measured against the nearest surface only**, by
  `LocalSystem::NearestSurfaceDistance` and a six-probe gradient
  (`ShipDrive::AwayFromSurface`) that is zero inside a body, where the drive
  then gives cruise speed. `UpdateDriveRoom` asks `LocalSystem::Current`, which
  also generates the thirty neighbours it throws away.
- **Disengaging clamps the speed to 200 m/s in one substep** (`SetCommand`).
- **The faster you go, the slower it looks.** The only motion cue is 300
  motes wrapped in a cube of half-extent 400 m (`NearFieldRadius`), which fade
  linearly to nothing by `ds.Sky.MoteFadeSpeed`, 2 km/s. At 200 m/s they are
  90% bright and a mote crosses the field in four seconds. Under the drive
  they are gone, and all that is left is a disc growing by a few percent a
  second. The transit streaks are the same instances, placed through
  `Flight.UniverseToWorld(NearStarPositions[i])`.
- **The "too bright" star is the local star, and mostly the red dwarfs.**
  Background and neighbour stars top out near 0.6 in the sky's units; the
  star's disc is `StarSurface x Compress((T / T_sun)^4, FluxGamma)`, T^2 at
  the default gamma of 0.5 (`SkyProjection.cpp`). The jump arrives with the
  star dead ahead, at a standoff (2.4 AU x sqrt(L)) chosen so that every
  star's irradiance is the same. The disc's solid angle is honest and its
  surface is compressed, so what bloom spreads, surface x solid angle, goes
  as T^-2. Over the 10,000-system corpus (`Saved/procgen_corpus.tsv`, radius
  M^0.8 as the generator makes it), a red dwarf's arrival glare is a median
  **3.6x** a Sun's (max 4.4x), a K's 2.1x, a G's 1.2x, an A's 0.7x. Three
  systems in four are red dwarfs.
- **Notes 3 and 4 are partly one symptom.** In a red-dwarf system every
  planet sits within a few degrees of that star. At 0.03 AU an Earth is 0.16
  degrees across, six pixels at 103 degrees on 4K, and "heading straight at"
  it with the star behind it puts that disc's night side inside a halo 3.6
  times a Sun's.

## Goals

- The player chooses the speed, in both modes, and the ship holds it. The
  drive's fastest is 1 c; anything faster is a jump (ruling 1).
- The ship goes where the nose points, in the drive always, in cruise once
  the boosters have caught up with a turn.
- Speeding up, slowing down and stopping are each one deliberate act, with a
  response the player can predict before they make it.
- Near a surface, the only thing that overrides the lever is a cap the HUD
  names, which binds only when the nose is on a surface the ship would reach
  within a few seconds, and lets the ship come down to where landing will
  begin.
- Motion reads as motion at every speed, and the drive never looks slower
  than cruise.
- No star's glare hides the sky around it, and the local star is still, by
  far, the brightest thing there is.

## Non-goals

- **The jump.** Not redesigned. The only change it sees from this spec is
  that every fold is an all stop and every arrival is at rest (decision 4):
  one line in the existing `TransitBegan` case and one in `JumpTo`. The
  in-system jump (ruling 1) is the sibling spec's decision 12, and reuses
  both unchanged.
- **Landing.** Below the floor (decision 6) is landing's. This spec's job is
  to hand over without deciding anything landing will want to decide.
- **The system map, the in-system target, its bracket and the prograde mark.**
  The sibling spec.
- **Inertia under the drive.** It still has none, and still reports zero
  acceleration (sky decision 8's reason stands: nothing is thrown about yet).
- **Combat rates.** Cruise's limits are unchanged.

## Decisions

### 1. Two levers, both on the ship; each keeps its place across F; X stops everything

**The lever positions move from the pawn into `FShipFlightCommand`**: the
cruise lever (`Throttle`, -1..1, as now) and the drive lever (`DriveNotch`, a
whole number, 0 = STOP). The pawn keeps only what is happening at the keys
this frame -- attitude, whether a lever key is held, and how many times each
was pressed (decision 3) -- and hands it to the ship with
`UShipSubsystem::SetHelmInput(Commander, const FHelmInput&)`, gated on the
pilot like everything else. The ship moves whichever lever is live, in its
own tick. Shift and Ctrl move the live lever; F chooses which lever is live.
In transit the helm is inert: the ship drops lever input as it already drops
attitude.

**Each mode keeps its own lever across F.** Set the drive to 1 c, drop to
cruise to look around at 50 m/s, press F, and the drive is at 1 c again. F is
the key a pilot presses often and in passing, and a toggle that forgot its
setting would make the drive lever a button that resets whenever you let go
of it. What makes it predictable rather than surprising is the HUD, which
always shows **both** levers' settings, the live one in ink and the other
dim (decision 7), so the speed F will spool to is on screen before F is
pressed; and the response, which never jumps upward (decision 4).

**X is all stop: both levers to STOP at once.** The developer asked for a
stop by name. One key, in either mode, and the ship comes to rest in a known
few seconds, and stays at rest if F is pressed afterwards. X is free in
`IMC_Default` (bound today: W A S D Q Z, Shift, Ctrl, F, E, C, Space, the
left mouse button), and it is the genre's convention: Elite binds its
zero-throttle there. It is a new `IA_Stop`, built and clash-checked by
`Tools/setup_flight_input.py` like `IA_Drive`.

**After X, the lever starts again from STOP.** Resuming the drive to 1 c
after an all stop is a six-second hold of Shift, watching the readout
climb (decision 3). That is accepted, not an oversight. A stop is a
deliberate act, used rarely, and it usually happens near something, where
the next speed the pilot wants is a new choice rather than the old one; a
hold is how a lever is set, and the readout shows the speed as it goes.

**Rejected: X stops only the live lever.** Stop, press F, and the other
lever's speed resumes: a stop that does not stop. **Rejected: engaging the
drive always starts it at its bottom notch.** It would make F forget, so the
common act (drop to cruise to look, then back) would cost a six-second hold
every time, where X's six seconds follow a rare, deliberate stop. The first
draft rejected this as "a chore", which its own X contradicted; the true
difference is how often each happens. **Rejected: a double-tap of Shift
after X restores the notch before the stop.** A hidden gesture, and a second
meaning for a key whose tap means one notch. **Rejected: keeping the lever
on the pawn.** A second lever would be a second copy of ship state on an
actor, and a second pilot sitting down would bring their own.

**Cost to change:** low. Which lever survives what is a few lines in the
subsystem's sweep; the tests pin whichever is chosen. Ruled as recommended
(ruling 5).

### 2. The cruise lever stays as it is, with a detent at zero

The playtest has nothing against cruise: 200 m/s, swept at 0.5 a second,
chased under the boosters' 40 m/s^2, is the walkable pace the flight spec
set, and the developer's one remark about it is that it *feels* fast (note
2's problem is the drive's, decision 8). It keeps its range, its reverse,
its rate (`ds.Cruise.Sweep`, which replaces the pawn's `ThrottleSweepRate`)
and its inertia.

One change, for "slow down and stop more deliberately": **sweeping down
through zero stops at zero.** Going astern is a second, fresh press of Ctrl.
A held Ctrl that carried a cruising ship through rest and into reverse is the
lever acting past what was asked.

### 3. The drive lever: a stop and 18 notches, 1 km/s to 1 c, and a tap moves the ship one notch

*Revised by ruling 1: the lever ends at 1 c, where revision 2 ran on to
100 c.*

The drive lever is a row of notches. Position 0 is **STOP**. Positions 1 to
18 are a 1-2-5 series, in km/s to 2,000 and in fractions of light from 0.01,
up to and including light itself:

```
STOP  1 2 5 10 20 50 100 200 500 1,000 2,000 KM/S
      0.01 0.02 0.05 0.1 0.2 0.5 1 C
```

Every step is x2 or x2.5 (2,000 km/s to 0.01 c is x1.5), so each notch is
the same *felt* step anywhere on the lever. The readout for a notch is
exactly its label (decision 7), so a speed the player sets is a number they
can come back to.

- **A tap of Shift or Ctrl is one notch, counted as a press, not read from a
  level.** The pawn binds `Started` on the lever keys and counts presses, and
  the ship applies each one; a press and release inside one 30 Hz frame is
  still one notch. A hold repeats after 0.3 s at `ds.Drive.Sweep`, 3 notches
  a second: STOP to 1 c is six seconds held, and a decade of speed is one
  second.
- **A tap moves the ship one notch from what it is doing, not from where the
  lever was.** Ctrl sets the lever to the notch below the ship's present
  speed, if that is lower than one notch down from the lever; Shift, the
  notch above, if that is higher than one notch up. In steady flight the two
  are the same thing. They differ in exactly the three places where a lever
  the player cannot feel would otherwise swallow taps: under the soft cap
  (lever at 1 c, ship held to 22 km/s: Ctrl gives 20 km/s at once, not twelve
  taps of nothing), while spooling up (Ctrl stops the climb where it is), and
  while spooling down after X (Shift stops the fall where it is). **Ctrl
  always slows the ship, and Shift always speeds it, from the first tap.**
  Formally, with p the ship's eased position (decision 4):
  `down: notch = max(0, min(notch - 1, ceil(p) - 1))`,
  `up: notch = min(top, max(notch + 1, floor(p) + 1))`.
- **No reverse.** Backing away from something at a kilometre a second is
  not a thing a pilot wants; turning round is. Ctrl at STOP does nothing.
- **The bottom, 1 km/s, is five times cruise's top.** It is the first speed
  at which cruise would be a nuisance: 100 km at 1 km/s is a minute and a
  half. Anything slower is cruise's, and the two ranges meet with no gap the
  response cannot bridge (decision 4).
- **The top, 1 c, is the developer's** (ruling 1): "anything faster should
  be a jump", and 1 c "already feels somewhat like quite the stretch from
  realism". Over 10,000 generated systems the leg from the arrival standoff
  to the innermost world is a median 0.2 AU (95th percentile 2.5 AU, longest
  138 AU). At 1 c the median leg is 100 s, the 95th percentile 21 minutes and
  the longest 19 hours; **1 AU is 8 min 19 s, and 30 AU, a Neptune's orbit,
  is 4 h 9 min**. So the drive is for the approach and the short leg, and the
  long leg is the **in-system jump's** (the sibling spec's decision 12), which
  folds the ship to a standoff above a targeted world. The approach itself is
  the soft cap's (decision 5), and its last part takes about a minute from
  any notch that crosses the leg in less: from 0.2 AU at 1 c the floor is
  reached in 165 s, the last 64 of them under the cap. `ds.Drive.Top` (in c,
  default 1, clamped to at most 1) removes the notches above a lower top for
  a playtest that wants the lever shorter; it can never lengthen it.

**Rejected: a continuous logarithmic lever.** A tap would move it by however
long the key happened to be down, the readout would say "37.2 KM/S", and a
speed could never be returned to. **Rejected: a tap always moves the lever
one notch from where it is** (the first draft). Under a hold, the lever can
be ten notches above what the ship is doing, and slowing down deliberately
is then ten taps of nothing: the over-control the developer named.
**Rejected: detecting a tap from the held level** (the first draft's
`FNotchSweep(Input, PreviousInput)`). A press and release inside one frame
reaches the ship as 0 and moves nothing, which at 30 Hz is a common tap.
**Rejected: a top of 100 c** (revision 2's recommendation). It crossed the
median leg in a second and the longest in eleven and a half minutes, so the
drive alone covered everything procgen makes. The developer ruled it out:
1 c is already a stretch from realism, and anything faster should be a jump.
What it covered above 1 c is now the in-system jump's. It also made capture
harsh (decision 5: 93 c to 2.9 c in one substep at a tenth of a degree),
which the 1 c top mostly removes. **Rejected: a top of 10 c** (the first
draft's alternative), for the same ruling. Revision 2's objection to it, that
the lever would stop covering what procgen makes, is answered by the jump,
not by the lever. **Rejected: 1,000 c.** Nothing needs it.

**Cost to change:** low in code (a table in `ShipDriveLever.cpp`). What the
player learns is the table. Ruled (ruling 1).

### 4. The ship answers the lever eased, at the lever's own pace, and never jumps up

**The drive's speed follows the lever in notch space**: its eased position p
moves toward the lever's notch at `clamp((notch - p) / 0.4 s, -R, +R)`,
`R = ds.Drive.Response` (3 notches a second), times the boosters' thrust
fraction. Speed is the notch table read at p: geometric between notches,
linear from STOP to the first. So:

- one tap settles to 95% of its new speed in 1.2 s, with no overshoot;
- **R equals the hold's sweep rate**, so while Shift is held the ship
  accelerates in step with the lever, and on release it settles within a
  second;
- all stop from 1 c (notch 18) is under cruise's top in 6.3 s and at rest in
  about nine; from 0.1 c, 5.3 s and eight; cruise's all stop from 200 m/s is
  five. The two stops feel alike;
- starved boosters slow the response, not the top: a quarter thrust takes
  four times as long to reach any notch, and gets there. That replaces tau's
  stretch (sky decision 8) with the same degradation, and it never becomes a
  readout of lost potential (the anti-chore principle's 73% case).

**What the soft cap holds, p follows.** While the cap (decision 5) holds the
ship below `SpeedAt(p)`, p is set to `PositionOf(held speed)` every substep.
So when the cap lets go -- the nose turned off the world, or the ship
climbed -- the speed rises from where the ship actually was, at R notches a
second like any other change: from 25 km/s to 1 c in about four seconds,
never in one substep. **Speed never rises faster than R in notch space,
anywhere.** It can fall faster in exactly one case, the substep in which the
nose first meets a world the ship would reach within the cap's few seconds
(decision 5, *capture*), and then only as far as the cap requires.

**Leaving the drive is a state: spooling down.** F pressed with the ship
above cruise's top does not clamp. It makes the cruise lever live at once,
and the ship spools down on the drive's easing, along the nose, until it
reaches 200 m/s; there it becomes an ordinary cruising ship, which chases
its lever from there under inertia. Every case in it is pinned
(`DeepSpace.Ship.FlightDrive`):

| During the spool-down | What happens |
|---|---|
| the HUD | names cruise and its lever, and says `SPOOLING DOWN` (decision 7) |
| Shift, Ctrl | move the cruise lever, which is live from the press of F |
| the soft cap | applies throughout, and p follows it as above |
| F again | resumes the drive from the present p, with the drive lever where it was |
| X | both levers to STOP; the spool carries on down, and cruise then brakes to rest |
| attitude | the velocity stays along the nose, as in the drive |

From 1 c the spool takes about six seconds and cruise's braking five more.

**Engaging the drive starts p at the ship's present forward speed.** Neither
toggle has a frame in which the speed jumps.

**Every fold is an all stop, and every jump arrives at rest.** On
`TransitBegan` both levers go to STOP, in the same case that already spends
the charge and releases attitude: one line. The ship eases down inside the
fold, where its speed was never visible (the streaks are drawn from the
transit's progress). From 1 c that ease takes about nine seconds, longer than
the six-second fold, so the arrival does not rely on it: **`FShipFlightState::
JumpTo` also sets the velocity and p to zero**, the second line. The ship
comes out at rest exactly, with the mode it went in with, and the first thing
the pilot does after any jump is choose a speed. Both jumps take this path:
the interstellar one, and the sibling spec's in-system jump (its decision
12), which reuses `TransitBegan` and `JumpTo` and changes neither. Without
it, a drive lever left at 1 c would fly the ship at the star during an
arrival -- the arrival point puts the nose on it -- and park it at the star's
floor, where the disc fills the view: note 4 at its worst. After an
in-system jump it would fly the ship straight down onto the world the jump
had just framed two degrees across: the approach, the part the player flies,
taken from them.

**Rejected: instant.** It is honest to the drive's lack of inertia and it is
exactly what "out of control" describes: F at full lever was 200 m/s to 34 c
in one substep. **Rejected: easing in speed rather than notches.** A time
constant in m/s is right for one decade and wrong for the other four.
**Rejected: letting p run on above a hold** (the first draft). The lever's
speed returned "at once" when the cap let go: at 10 c a turn of a fraction
of a degree jumped the speed by five orders of magnitude in one substep.
**Rejected: the fold leaves the levers alone and holds p at cruise's top**
(the first draft). It is the dive into the star described above. **Rejected:
the fold disengages the drive.** It changes the mode as well as the speed,
and the cruise lever left at 200 m/s would carry the ship on after the
arrival; STOP on both is one rule and says what it means. **Rejected:
relying on the ease alone to reach rest inside the fold** (revision 2, which
said the ship "eases to rest inside the six-second fold"). It does not from
1 c, and did not from 100 c either: the ship would come out still moving.

**Cost to change:** low: two CVars and one function; the fold's two lines
set the first moment after every jump, and were ruled as recommended
(ruling 5).

### 5. The soft cap: only when the nose is on a surface, and then the whole speed

**The rule, under the drive.** Every surface in the system is a sphere at its
floor (decision 6): each body at radius R + floor, and the system's edge as an
inside-out sphere at EdgeRadius - floor about the star. Cast the nose's ray
from the ship. If it meets one or more of them, let d be the distance along
the ray to the nearest meeting. Then

```
speed <= MaySpeed(d) = min( max( d / N, sqrt(1.6 a d) ), d / step )
        N = ds.Drive.HoldSeconds = 4 s,  a = the boosters' present acceleration
```

and **the cap is on the whole speed, along the nose.** If the ray meets
nothing, there is no cap and the lever is the speed.

- *Only when it would hit, literally.* The drive's velocity is along the
  nose, so the ray is the ship's path: the cap binds exactly when the path
  meets a surface within N seconds at the lever's speed. A path that misses a
  world by a kilometre above its floor is not touched, bit for bit.
- *The ship goes where the nose points.* Nothing is taken sideways, so the
  target does not drift in the view as the ship slows, whatever the aim
  error: a nose held still flies a straight line to the point it was on.
- *It arrives.* Under the cap d falls by e every four seconds until d / N
  meets the braking curve, `sqrt(1.6 a d)`: 1.0 km from the floor at 256 m/s
  with full boosters. From there it brakes as cruise does, at 80% of what the
  boosters have, and comes to rest on the floor about eight seconds later
  with no step in speed. `d / step` is the last guard: no substep can carry
  the ship past a surface, at any speed or frame chop, including a two-second
  hitch (the flight state has every sphere and tests each substep's own ray,
  so nothing is dead-reckoned).
- *Every surface, not the nearest.* The ray is tested against every body and
  the edge, every substep: about ten ray-sphere tests, cheap. A moon beyond a
  giant, or a planet while the star is nearer, caps the ship the moment the
  nose is on it, and can never be crossed in one substep. This replaces
  `NearestSurfaceDistance` and `AwayFromSurface` in the flight path; both stay
  for the HUD's altitude line and the room.
- *At the floor, and below it.* A ship on a floor sphere whose nose points
  below its horizon meets it at d = 0 and holds still; above the horizon the
  ray leaves the sphere and the lever is free. A ship *under* a floor (the
  floor CVar raised in play, or rounding) is treated the same way: it may
  climb at the lever's speed and may not descend. There is no zero-gradient
  case and nowhere the ship is held at a fixed speed: inside or under
  anything, it climbs out at whatever the lever asks.
- *Capture.* When the nose first comes onto a world's disc at a speed the cap
  does not allow -- steering onto a world at 1 c from a few million kilometres
  -- the speed drops to the cap in that substep. That is the one place speed
  changes faster than R: always down, only on the pilot's own aim, and only
  as far as the rule requires. After capture p follows the cap (decision 4),
  so nothing leaps back up if the nose flutters off the limb and on again.
- *Skimming.* At the floor, a nose just above the horizon flies a tangent at
  the lever's speed; a nose grazing just below it meets the sphere a horizon
  away and is capped to that distance over N. The lower you skim, the more
  gently you may point down. That is the vision's "skim around it looking for
  somewhere worth landing", and it is landing's door (decision 6).

What it does to an approach. `python3` against the rule above, 120 Hz
substeps, an Earth (floor 10.2 km), the lever set from STOP at t = 0 and the
ship spooling up at R, full boosters, **with the lever topped at 1 c (ruling
1)**. Recomputed for this amendment from revision 2's own scripts with the
notch table cut at 1 c, pure Python in one process. Neither script is a
repo tool; Track A's `Tools/sky_probe.py`, whose drive law becomes this
decision's (A5), is what reproduces these rows in the repo. With the nose
fixed on the world:

| From | Lever | Cap binds after | At 100 km up | Floor reached |
|---|---|---|---|---|
| 0.2 AU (median arrival) | 1 c | 102 s | 139 s, 22.5 km/s | **165 s** |
| 0.2 AU | 0.1 c | 999 s | 1,027 s | 1,053 s |
| 1 AU | 1 c | 501 s | 539 s | 564 s |
| 2.4 AU (Sun-like arrival) | 1 c | 1,199 s | 1,237 s | 1,263 s |
| 250,000 km | 1 c | 5 s | 37 s | 62 s |
| 250,000 km | 1,000 km/s | 249 s | 264 s | 290 s |
| 365,000 km: an in-system jump's arrival over an Earth, 2 degrees across | 1 c | 5 s | 38 s | **64 s** |
| 4.0 million km: the same over a Jupiter (floor 112 km) | 1 c | 15 s | -- | 79 s |
| 0.2 AU, a Jupiter | 1 c | 102 s | -- | 165 s |
| 0.2 AU, quarter thrust | 1 c | 118 s | 156 s | 187 s |

**The last part of every approach is the cap's, and it is about a minute**:
from the moment the cap binds (d = vN, 1.2 million km at 1 c) to the floor is
64 s at 1 c and 54 s at 0.1 c, whatever the leg. Everything before it is the
lever crossing the leg at its own speed, and at 1 c that is the leg's length
in light-seconds: 100 s for the median, 21 minutes for a Sun-like arrival.
Revision 2's table (86 s from 0.2 AU at 10 c, 98 s from 2.4 AU at 100 c) is
superseded; it was bought with speeds the developer ruled out. What makes
the long legs short again is the in-system jump (the sibling spec's decision
12), whose arrival is the two rows above that take a minute.

Against today's drive from 0.2 AU: under 1 km/s at 115 km after 218 s, and
never at the floor. The last 90 km now take 26 s, and the ship comes through
100 km at 22 km/s rather than leaving it at 1.

**With aim error**, the nose fixed off the world's centre. From 0.2 AU at
1 c: 0.001 and 0.01 degrees reach the floor in the same 165 s as a perfect
aim (the ray still meets the sphere, and the ship flies to where it does);
0.1 degrees misses the sphere and passes 46,000 km up at 1 c, untouched --
exactly as the uncapped ship would, and 1 degree passes 516,000 km up. From 250,000 km at 1 c, anything up to a
degree still meets the world and lands on its floor in 62 s. No case ends
farther than it started except by missing, and none leaves at a speed the
pilot did not set.

**A pilot who keeps the bracket centred** (the nose turning toward the world
at up to 0.2 rad/s with a steady bias): at 1 c from 0.2 AU, **every bias up
to 5 degrees is captured** and reaches an Earth's floor in 155-165 s. The
capture substep drops the speed from 1 c to 0.61 c at half a degree (725,000
km out), to 0.15 c at 2 degrees (x6.7) and to 0.035 c at 5 degrees (x29); at
0.1 degrees there is no drop worth the name. On a Jupiter everything up to 2
degrees is captured with no drop at all. The same holds from an in-system
jump's arrival: every bias to 5 degrees reaches the floor in 57-67 s. The
rule of thumb, which the pilot learns by flying: capture is smooth when the
disc is bigger than the aim error at N seconds out, `v < R / (N x aim)`:
for a half-degree aim, about 0.6 c on an Earth and 7 c on a Jupiter. Under a
1 c top, a Jupiter is always under it, and an Earth is at most a factor of
two over for a pilot within half a degree. Revision 2's 100 c dropped 32-fold
at a tenth of a degree (93 c to 2.9 c at 3.5 million km); the ruled top
removed most of capture's abruptness along with the speed. The ship does
what it was told in every case.

**Under cruise**, which has inertia, the same rule sets the assist's
*target*: the target speed along the commanded direction (the nose, or aft
astern) is held to `MaySpeed(d)` for that direction's ray, so a cruising
ship brakes to rest on the floor. At 200 m/s the full-thrust ship starts
braking 625 m above it, a starved one 2.5 km up. Because cruise can slide
after a turn, a hard stop backs it up: no substep ends inside a floor sphere,
and a velocity that would take it in loses its inward part at the sphere. The
80% braking margin means the hard stop never binds on a straight approach;
it is there for a turn made while drifting in. Cruise never tells the player
anything else.

**The system's edge stays a surface** (plan conflict 10), on exactly the
same rule. From inside, every ray meets the edge's sphere; at 1 c the cap
binds 1.2 million km (0.008 AU) short of an edge 15,800 AU out, and the ship settles 10 km
inside it and stays in its system: `GetSystemAt` never goes empty under a sky
still drawing the old one. You leave by jumping.

**Rejected: capping only the closing component, toward the nearest surface**
(the first draft). It is unstable: with the sideways speed left at the
lever's, any real aim error grows as the ship closes, and a keyboard helm at
0.25 rad/s^2 cannot aim to the thousandth of a degree it needs at 10 c. The
ship then chooses its own path and speed, and the only flyable approach is a
low notch: note 1 again. It also binds on paths that would miss, braking and
deflecting a ship ruling B says must be left alone. Its reason for rejecting
a whole-speed cap -- oblique approaches stall and nothing can skim -- came
from today's speed = room / tau, not from capping the whole speed only when
the path meets the world. **Rejected: today's whole-velocity cap against the
nearest surface's room.** It is the whole speed, but it binds whatever the
heading, so it is the automatic slow-down the developer called too strong.
**Rejected: a margin sphere, binding on near misses too.** It would soften
capture, and it holds back a ship that would not hit. **Rejected: easing the
capture.** Any delay at capture spends the few seconds the cap exists to
keep; the drop is what "hold back" means at that speed. If playtest finds it
harsh, the successor is a capture that falls at a fast finite rate above a
hard `d / 0.25 s` bound, not a softer rule. **Rejected: a pure exponential
with no braking finish.** It never arrives, and "never arrives" is note 1.
**Rejected: the first draft's 200 m/s crawl to the floor.** It stops from
200 m/s in one substep; the braking curve comes to rest. **Rejected: N = 15 s**
(today's tau). It is the auto slow-down the developer called too strong. N = 3
or 5 is a reasonable-person difference and a CVar; 4 s is "within a few
seconds".

**Cost to change:** low in code, but landing's approach is built on it.
Ruled as specified (ruling 2).

### 6. The floor is where the sky stops being honest, and it is landing's door

**The floor over a planet or moon is `max(ds.Flight.Floor, SkyProjection::RenderedFloor(R))`**:
the larger of 10 km and the sky's own nearest drawn altitude, 1.6e-3 of the
body's radius (`FSkyViewParams::MinRenderedAltitude` and
`MinRenderedAltitudeOfRadius`, extracted into one function both callers
ask). That is 10.2 km over an Earth, 112 km over a Jupiter. Over the
system's edge it is `ds.Flight.Floor`.

The reason is one sentence: **below the sky's floor a proxy stops growing**,
so a ship that flew lower would see a world that no longer gets nearer. The
drive takes the ship exactly as low as the picture stays true, and no lower.
It also means `SM_SkyBody`'s LOD 0, sized for 1.6e-3 of the radius, never
shows a facet at the floor, and that the sky already draws the world at its
true size there (`DeepSpace.Loop.Drive` holds it).

**Over a star the floor is one stellar radius**, `ds.Flight.StarFloorRadii`
= 1: the disc then fills 60 degrees of the view, and the rest of the sky is
still there. The renderer's floor, 1.6e-3 R (1,100 km over a Sun), would park
a ship where the photosphere is the whole window, which is note 4 as a
destination. With the fold an all stop (decision 4) nobody reaches a star's
floor without flying at it, but a lever left on in the galley should not end
there. The render check (decision 9) takes a frame at this floor.

**Both modes obey it.** Cruise below the floor would be the same untrue
picture at 200 m/s, and a cruise lever left on toward a world while the
player is in the galley would fly through it. In the POC, the lowest the
ship goes is the floor.

**The room** (`GetRoom()`, for the HUD and the tests) is the minimum over
every surface of its distance less its own floor, never negative. It is no
longer what the cap reads.

**The handoff.** At the floor the ship holds, still free to move along the
surface or away. Landing takes over there, and this spec constrains it in
only two ways, both cheap to undo: there is one function that answers "how
low may the ship go over this body" (the subsystem's `FloorFor`), which
landing may replace or lower as it takes over drawing the ground; and the
cap is a pure function of the spheres, the ship's position and the direction
of travel (`ShipFlight::MaySpeed`, `ShipFlight::RayToFloor`), which landing
may keep, replace, or apply only above its own regime.

**Rejected: keeping 100 km.** The developer's "landing starts to take too
long" is from 100 km: at cruise's 200 m/s it is eight minutes down.
**Rejected: a flat floor for every body.** A flat 10 km would take a giant
below the altitude its proxy can be drawn at. **Rejected: letting cruise
below the floor.** It would look broken rather than low, and landing owns it.
**Rejected: the renderer's floor over a star** (the first draft): above.

**Cost to change:** the numbers are CVars. Where landing begins is the
expensive part. Ruled as specified (ruling 2): this is where landing will
later take over.

### 7. What the HUD says: the speed, both levers, and why they differ -- and no time in this corner

*Revised by ruling 3: a live ETA exists, on the target line and the map; this
corner still has none, because it has no destination.*

The bottom-left corner already reads "how fast and how far from anything,
together". It keeps its two lines and says more on each.

**The motion line** (ink, 12, then dim): the ship's speed, the live mode and
its lever's setting in ink; then, dim, the other lever's.

```
142 M/S  ·  CRUISE 200 M/S                    ·  DRIVE 1 C
80 M/S  ·  CRUISE ASTERN 100 M/S              ·  DRIVE STOP
12.4 KM/S  ·  DRIVE 50 KM/S                   ·  CRUISE 200 M/S
0.37 C  ·  DRIVE 1 C                          ·  CRUISE STOP
0.42 C  ·  CRUISE 100 M/S  ·  SPOOLING DOWN   ·  DRIVE 1 C
STATIONARY  ·  DRIVE STOP                     ·  CRUISE STOP
```

The dim part is what F would do. From cruise with the drive left at 1 c,
`· DRIVE 1 C` is on screen before F is pressed, so the toggle is never a
surprise. The lever is always named by the speed it asks for, never as a
notch number or a fraction of its travel: "7 / 18" is a gauge, and a gauge
is a thing to fill. The line is two text blocks in a horizontal box, ink and
dim, both from one pure `MotionLine` that returns the pair.

**The altitude line** (dim, 10, above it): as now, plus one word for what the
floor or the cap is doing.

```
2,310 KM ABOVE Kessa IV  ·  HOLDING OFF     the cap is taking speed away
10.2 KM ABOVE Kessa IV  ·  AT THE FLOOR     the lever pushes down; this is as low as it goes
3,400 AU TO THE EDGE  ·  HOLDING OFF
10 KM TO THE EDGE  ·  AT THE EDGE
```

`HOLDING OFF` shows while the cap holds the ship more than 5% below the
*lever's* speed (not p's, which follows the cap and so would flicker at the
threshold every substep); `AT THE FLOOR` while the ship is on a floor with
the nose into it and the lever above STOP. Both are asked of the flight state
(`GetHold()`), the one thing that knows what it capped this step. The
altitude names the nearest surface, as now; the cap's word refers to
whichever surface the nose is on, which near a moon may not be the one
named. The HUD no longer works any of it out from the orientation and the
throttle (`DriveHoldsAtFloor` goes, and `ds.HUD.FloorBand` with it: the floor
is now reached, so there is nothing to round up to). No colour changes and
nothing blinks: it is a fact about the ship, stated like the others.

**Units that change with scale** (`UShipHUDWidget::SpeedWords`, public and
pure beside `AltitudeWords`, on its rule: each unit takes over exactly where
the last would round up to its own threshold): whole M/S under 1 km/s; KM/S
to a tenth under 100, whole and grouped to 0.01 c; then C to a hundredth
under 1, and `1 C` at the top, which is as fast as the drive goes (ruling 1).
Every notch reads as its label.
Distance keeps `AltitudeWords` (M, KM, THOUSAND KM, AU).

**No time appears in this corner.** The corner describes the ship: its
speed, its levers, and how far it is from the nearest thing. A time to
arrival needs a destination, and the corner has none: the nearest surface is
often not where the pilot is going. **The live ETA the developer ruled for
(ruling 3) belongs to the target**, and is on the target line and the map
(the sibling spec's decision 6). It is computed with this spec's own law,
`ShipFlight::SecondsToFloor` (Track 0), so the number the pilot reads is the
approach the ship will fly: held at the present speed until the cap binds,
then the cap, then the braking curve. Once the lever has settled it agrees
with the flown approach to 0.11 s over the median leg at 1 c, and so counts
down a second a second.

**Rejected: no time anywhere** (revision 2's recommendation, its sign-off 5).
It argued that a number counting down to an arrival is a clock to watch, and
that it scores the lever setting and invites minimising it. The developer
ruled otherwise: "Live ETA, i don't recall ruling out a countdown in this
regard. This would be good." The no-countdown rule was made for the jump's
charge, a bar the player must wait out before anything can happen, and it
had been generalised past its reason; CLAUDE.md's sentence is scoped back to
the charge. An ETA for an approach the player chose, at a speed they can
change at any moment, with nothing happening at zero, is information about
their own choice. **Rejected: a time to surface in this corner**, beside the
altitude. The nearest surface is often not the destination, and two times on
screen would disagree.

**`DriveLine` becomes `JumpLine`.** The HUD's top-right member and
`DriveLineText` show the jump's words. With drive words now on the HUD, a
member named for the drive that shows the jump is conflict 7's swap waiting
to happen.

**Rejected: showing only the ship's speed.** The lever is persistent and
invisible; without its setting on screen, a toggle to a lever set an hour ago
is a surprise. **Rejected: showing only the live lever** (the first draft).
The same surprise, one F away. **Rejected: a "SLOWING FOR Kessa IV" warning
in the accent colour.** The cap is not an alarm, and the accent is the
course's.

### 8. Dust that is honest where it can be, and never slower than cruise

**What the dust can carry, and what it cannot.** Optic flow tells the eye
*that* it is moving, which way, and roughly how fast within a decade or so.
It cannot tell 0.1 c from 1 c at any honest scale, and above a few
kilometres a second the 400 m field cannot even show the true speed: its
motes are about 120 m apart, and past half that per frame (3.6 km/s at 60 Hz)
they step and strobe. So the dust's job is limited, and stated: **moving,
which way, and never slower-looking than cruise.** Which notch the ship is at,
and whether it is closing on its destination, are read from numbers: the
speed on the motion line, and the target's distance and live ETA on the sibling spec's
target line, whose digits roll at a rate proportional to speed and so change
visibly at every notch. That is the navigation cue for note 2's "very
confusing", and the dust does not pretend to be it.

**The law.** The field is drawn at its fixed size and streamed at a **seen
speed**:

```
seen(v) = v                                            v <= K          (honest)
seen(v) = K x (DustTop / K) ^ (ln(v / K) / ln(Top / K))   above
          K = ds.Sky.DustKnee = 2 km/s,  DustTop = ds.Sky.DustTop = 3 km/s
          Top = the drive's top, 1 c (ruling 1; 5.2 decades above the knee)
```

Honest up to 2 km/s: cruise exactly as today, and the drive's first two
notches at their true 1 and 2 km/s, **five and ten times cruise's top**, which
is the whole of note 2's fix -- the drive now looks faster than cruise from
its first notch, where today the dust is gone. Above the knee the seen speed
climbs slowly to 3 km/s at the top, 1 c (50 m a frame at 60 Hz, under the
strobe limit), and each mote stretches along the velocity from 1 at the knee to
`ds.Sky.DustStretch` = 8 times its width at the top, on the same log scale.
Past the knee the dust says "faster still", by decades, and no more.

- **The field is stored in field space**, a cube in universe axes, advanced
  by the seen velocity each frame; motes wrap as now, and are placed in
  world space through the ship's rotation only. At or below the knee this is
  exactly today's honest parallax, for translation and rotation alike. Above
  it nothing is at a universe position, and nothing needs to be.
- **It streams along the true velocity**: along the nose in the drive, and
  along cruise's slide after a turn.
- **It stays drawn at 400 m**, far inside the 50 km near edge of the sky's
  proxy band, so dust is in front of every world at every speed.
- **The transit streaks move to field space with it.** They are the same
  instances, placed today through `Flight.UniverseToWorld(NearStarPositions[i])`,
  so the transit path is re-expressed on the field-space positions: the
  stretch `1 + ds.Nav.StreakLength x sin(pi x progress)` and the sweep are
  applied exactly as now, and `DeepSpace.Ship.CounterFrameJump` pins that the
  streak shape is unchanged. In transit the seen speed is not used.

**Why it never reads as the jump.** The fold's streaks stretch to 41 times,
sweep five field-widths past the window, and are the only thing outside: the
dome, the marker and every body are gone. The drive's dust stretches at most
eight times, never sweeps, and flies under the whole sky.

**This law is a playtest gate, not a decided default.** Three questions, in
play, with the CVars live: does the drive's first notch read as faster than
cruise's top? does anything above the knee read as faster than the knee? does
anything read as the fold? Candidates: `DustKnee` {1, 2} km/s, `DustTop` {2.5,
3, 3.5} km/s, `DustStretch` {4, 8, 16}. The defaults above are the starting
point, and the verdict is the developer's.

**Rejected: the first draft's power law from cruise's top**, seen = 200 m/s x
5^(...). It mapped eight decades onto 0.7, so adjacent notches differed by 7%,
below what the eye can tell, and it made the drive's first notch look like
230 m/s where honest dust at 1 km/s would not yet strobe. **Rejected: a high
seen top (20-30 times cruise) carried by the stretch.** 4-6 km/s strobes a
120 m spacing at 60 Hz, and a stretch long enough to hide that is a streak a
quarter of the field long: the fold. **Rejected: honest dust re-scattered at
a spacing proportional to speed.** The real positions at 10 c are millions of
kilometres out: behind every planet proxy and past the dome. **Rejected:
keeping the fade and relying on the planets' parallax.** That is today, and
note 2.

**Cost to change:** low: three CVars and one pure function. Ruled as
recommended (ruling 5): the knee law, judged in play.

### 9. The local star: honest warmth, a ceiling, and the developer's eyes

**The star's surface goes as T^4, uncompressed, up to a ceiling**:
`StarSurface x min((T / T_sun)^4, MaxStarWarmth)`, `MaxStarWarmth = 8` (a
star at 1.68 times the Sun's temperature, about 9,700 K; hotter stars differ
above it only in colour). This amends sky decision 2, which compressed the
warmth term with irradiance. The compression was for ranges too wide to show,
and a star's disc is never in danger of being invisible: honest, the dimmest
star in the corpus is still at least 30 times the brightest planet in its own
sky at the default `StarSurface`, and the test pins 20 times over the test
seed's first sectors, leaving the generator's priors room to move. What
honesty buys is the thing note 4 is about: **the arrival standoff equalises
irradiance, so with honest surfaces the glare at arrival is the same for
every star** -- between 0.85 and 1.5 times a Sun's for every class over the
corpus (the eight B stars, held by the ceiling, lower), where today it is
0.4 to 4.4. No kind of sun is "the bright ones".

That alone takes a red dwarf's arrival glare down by a median 2.1 stops and
leaves a Sun's where it is. Whether a Sun's glare needs the "slightly" too
is the developer's taste, and is chosen **in play**: `ds.Sky.StarSurface` is
already a live CVar (default 1,000; candidates 1,000, 700, 450), and the
warmth law is behind a temporary live `ds.Sky.StarWarmthGamma` (0.5 today's
compressed, 1 honest) until the verdict. **`ds.Sky.Bloom` is not moved**: it
is what makes a two-pixel star read as bright, and the dome would lose it.

The ceiling is also the half-float guard sky decision 2 worried about: at the
ceiling, 1,000 x 8 x `ds.Sky.Radiance` 3 at the galley's exposure is about
30,000 in scene colour, under half of 65,504, where honest T^4 for the
hottest B star the priors make (41x) would be past it.

**The render check (for the developer's eyes, not a guard).** A temporary
automation test, `Eyes.StarGlare`, named outside `DeepSpace.` so `./test.sh`
never runs it, and deleted once judged. It builds `SkyTestWorld::FSkyWorld`
with the full 3,000-star dome, and for three systems from the test seed's
neighbourhood -- the start system (a red dwarf), the nearest K, the nearest G
-- renders three shots: the arrival (`NavStart::ArrivalPoint`, nose on the
star), the note-3 shot (0.03 AU from the innermost world, nose on it, star
behind), and the star's floor (one stellar radius up, nose on the star).
Each under the warmth law {compressed, honest-with-ceiling} and `StarSurface`
{1,000, 700, 450}: 54 frames. Plus one at the new floor over the opening
planet, since the ground at 10 km has never been looked at.

It renders through a `USceneCaptureComponent2D` at `SkyTestWorld::PilotEye`,
103 degrees, 1920 x 1080, `SCS_FinalColorLDR`, eight captures per frame so
TSR settles, and writes `Saved/Eyes/StarGlare/<system>_<shot>_<law>_<surface>.png`
and a `report.txt` with, per frame, the peak scene value and the fraction of
the view brighter than the brightest planet disc in it. One run, through the
lock, never with `-nullrhi`:

```bash
. Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" -ExecCmds="Automation RunTests Eyes.StarGlare" \
    -TestExit="Automation Test Queue Empty" -unattended -nopause -nosplash -NoLiveCoding \
    -RenderOffScreen -FORCELOGFLUSH
```

**What the frames are for.** A scene capture at 1080p does not necessarily
share the viewport's eye adaptation or its bloom at 4K, so the frames are a
side-by-side of the candidates under identical conditions, not a claim about
what the player sees. They go **to the developer**, with the candidate CVar
values beside each, and the developer makes the call in play with the live
CVars. Then the chosen law is written without its CVar, the chosen
`StarSurface` becomes the default, and the test file is deleted in the same
commit.

**Rejected: lowering `StarSurface` alone.** It dims every sun alike and keeps
red dwarfs, the suns the player meets most, 3.6 times brighter than the rest.
**Rejected: lowering bloom.** It dims the glint of every point with the glare
of the one disc. **Rejected: a glare limiter that dims a disc as it grows.**
It makes surface brightness depend on distance, which the sky's decision 2
forbids for good reason: the approach would dim as it grew. **Rejected: the
orchestrator judges the frames** (the first draft). "Dialed back slightly" is
the developer's taste.

**Cost to change:** one line and one number. The look of three suns in four
changes. Ruled as recommended (ruling 5): honest T^4 with the 8x ceiling,
`StarSurface` chosen live from the frames.

## Seams with the system map

The sibling spec owns the middle cockpit screen, the in-system target, its
bracket through the glass, the prograde mark, the target line on the HUD and
its live ETA, the seats, and the in-system jump. This spec owns the
bottom-left corner, the words for speed and distance, and the flight law the
ETA is computed with.

- **Words, committed.** `SpeedWords` and `AltitudeWords` are public and pure
  on `UShipHUDWidget`, owned by this spec's Track B, and nothing moves to
  `NavText`. The map spec's revision already calls `AltitudeWords` where it is
  ("the flight-feel spec keeps them public and pure there, and nothing
  moves"); the reviewer's premise that the map moved it behind
  `NavText::SurfaceDistance` described that spec's first draft. One home,
  stated identically in both specs, is what removes the merge conflict.
- **Build order.** Both specs follow one staged order,
  `docs/superpowers/plans/2026-09-26-poc2-build-order.md`, which replaces
  revision 2's landing order here. This spec's Track 0 is stage 1, alongside
  the map widget and the level scripts; Track A is stage 2, alongside the
  map's words and marker arithmetic; Tracks B and C are stage 3, alongside the
  map's target state and in-system jump; the map's seats and HUD overlay are
  stage 4. Two items of this spec move: Track 0's 0.3 (the flight state's new
  surface) goes into Track A, and Track A builds the map spec's
  `IA_CycleTarget` with its own input actions.
- **Time.** Ruled (ruling 3): a live ETA on the target line and the map,
  computed by this spec's `ShipFlight::SecondsToFloor` and `RayToFloor`, and
  no time in this spec's corner (decision 7).
- **The in-system jump.** The map spec's decision 12. It reuses this spec's
  all stop on `TransitBegan` and its at-rest `JumpTo` (decision 4) and changes
  neither; its arrival standoff, two degrees across the world, is far outside
  every floor `FloorFor` returns, and it asks `FloorFor` for the guard. From
  that arrival the drive at 1 c reaches an Earth's floor in 64 s and a
  Jupiter's in 79 s (decision 5's table).
- **The drive does not steer, and does not read the target.** The target is a
  bracket and a bearing; the player aims. The cap reads every surface the
  nose is on, never the chosen one. The map spec's recommendation -- bind the
  cap only when the velocity's ray meets the floor sphere -- is now this
  spec's rule (decision 5).
- **What "dead ahead" can promise.** The map's target line says `dead ahead`
  within `max(angular radius, 0.25 degrees)`. Under decision 5 the ship
  arrives only if the nose ray meets the world's floor sphere, which at
  0.03 AU is 0.08 degrees, so `dead ahead` there can still pass the world by
  three radii at the lever's speed. This spec exposes the test the cap uses,
  `ShipFlight::RayToFloor(Surface, From, Direction)`, pure, in Track 0. The
  recommendation to the map spec: take it, and say something only when the
  nose would meet the world (for example `on the world` in place of `dead
  ahead`), so the words tell the pilot when the drive will bring them down.
  **Taken, through the ETA**: the target line shows a time only when the
  velocity's ray meets the target's floor sphere, and `PASSING ... UP` when
  it closes on a path that misses (the map spec's decision 6).
- **The prograde mark.** Under the drive the velocity is now always along the
  nose, so the mark and the caret coincide; they part only in cruise's slide
  after a turn, at under 200 m/s, which the dust already shows. So the mark
  need not show always: with a target only, as the map spec has it. Ruled
  (ruling 5): the prograde mark shows with a target.
- **Numbers the map quotes.** This amendment's, from decision 5's table:
  165 s from 0.2 AU at 1 c, 62 s from 250,000 km, 64 s (an Earth) and 79 s (a
  Jupiter) from an in-system jump's arrival. Revision 2's 86 s from 0.2 AU at
  10 c, and the first draft's 76 s, are superseded.

## The anti-chore audit

| The way it becomes a chore | Where it is ruled out |
|---|---|
| A countdown the player must wait out | The jump's charge is still a word, never a bar or a countdown (CLAUDE.md, scoped to the charge by ruling 3). The live ETA the developer ruled for counts down an approach the player chose and can change at any moment, and nothing happens at zero (the map spec's decision 6); this corner shows none (decision 7) |
| A lever that must be ridden | Levers stay where left, in both modes, across F and standing up (decision 1) |
| Taps that do nothing | A tap always moves the ship one notch from what it is doing (decision 3) |
| A cap that nags | One dim word, only while it is holding the ship off; no alarm, no colour (decisions 5, 7) |
| A cap that steers | It binds only on the nose's own path and takes only speed; the ship goes where it points (decision 5) |
| A speed with a right answer | Every notch is a round speed; none is marked; nothing shows the lever as a fraction (decisions 3, 7) |
| Lost potential on a thin split | Starved boosters slow the response, never the top, and nothing shows the difference (decision 4) |
| A failure state near a world | Nothing can cross a surface at any speed; at or under a floor the ship holds and may always climb (decisions 5, 6) |
| Being kept from the ground | The floor is the sky's, one function, and landing's to lower (decision 6) |
| A surprise on arrival | Every fold is an all stop and every jump arrives at rest; the pilot chooses the first speed after every jump (decision 4) |
| A wait with a chair in it | The approach's last part is the cap's, about a minute at 1 c; a leg that would take longer than the player wants at 1 c is the in-system jump's (the map spec's decision 12); and the lever holds while they walk away |
| A crossing that is only waiting | At 1 c, 30 AU is four hours, so the drive is not the only way across: the in-system jump folds to a targeted world, and nothing makes the player use it or scores whether they did |

## Deliberate fakes, and what they cost later

- **The drive has no inertia and reports no acceleration.** Unchanged from
  sky decision 8. Capture is the sharpest case: many notches in one substep.
  *Cost:* the day anything reads acceleration for the body, the drive's
  easing is where a felt spool would go, and capture is where it would bite.
- **Dust above the knee is a representation** (decision 8). *Cost:* none
  until something needs a mote to be somewhere, which nothing does.
- **The floor is the renderer's, not the world's**, and the star's is one
  radius for the eyes' sake. *Cost:* landing's first decision, and one
  function to replace.
- **Heat and radiation.** The ship can park one stellar radius above a star
  with nothing said. *Cost:* whatever the ship's wear model eventually says
  about it.

## Implementation outline

**Estimate: about 14 hours**, in four tracks with disjoint files, staged by
`docs/superpowers/plans/2026-09-26-poc2-build-order.md` together with the map
spec's work: **Track 0 is stage 1, Track A stage 2, Tracks B and C stage 3.**
Revision 2 had Track 0 declare the flight state's new surface with trivial
bodies so that B and C could compile before A landed; with B and C now a
whole stage after A, that is not needed, and 0.3 moves into A (below). Every
C++ change in a track is written first; the track then runs its build, the
commandlets it names, and its tests, through the lock, never alongside
another editor process.

### Track 0: the seams (serial, ~2 h)

0.1 **`Ship/ShipDriveLever.h/.cpp`**, pure, no UObject, no CVar:
    `NotchCount(TopCmPerSecond)`, `NotchSpeed(int32)`, `SpeedAt(double
    Position)`, `PositionOf(double Speed)`, `Ease(Position, Target, Dt,
    MaxRate)` with `EaseSeconds = 0.4`; `TapDown(Notch, P)` and `TapUp(Notch,
    P, Top)` (decision 3's rule); `FNotchRepeat` (a hold repeats after
    `RepeatDelaySeconds = 0.3` at a rate, fed a held flag, never a level
    difference); `SweepCruise(Throttle, bHeldUp, bHeldDown, PressesDown, Dt,
    Rate)` with the detent at zero.
    `Tests/ShipDriveLeverTest.cpp`, **`DeepSpace.Ship.DriveLever`**:
    - the table is the 1-2-5 series above, strictly increasing, STOP is 0,
      and every step is x1.5 to x2.5
    - `SpeedAt(PositionOf(v)) == v` to 1e-9 across the range, and `SpeedAt`
      is continuous and monotonic, linear below notch 1
    - `NotchCount` at 1 c is 19 positions (STOP and 18 notches), drops
      notches above a lower top, and never adds one above 1 c whatever top
      it is given
    - `TapDown` from a lever ten notches above p lands one notch below p;
      from steady flight it is one notch down; at STOP it stays; `TapUp`
      mirrors it and stops at the top
    - a 1 s hold repeats 1 + 3 x 0.7 = 3 times (rounded down), the same at
      30 and 144 Hz
    - `Ease` never overshoots, settles a one-notch step to 95% in 1.2 s, and
      moves at most MaxRate per second
    - `SweepCruise` stops at zero from either side and needs a fresh press
      to cross it
0.2 **`Ship/ShipFlightSurface.h/.cpp`**, pure: `FFlightSurface {FUniversePosition
    Centre; double Radius; double Floor; bool bInsideOut;}`;
    `ShipFlight::RayToFloor(const FFlightSurface&, const FUniversePosition&
    From, const FVector& Direction) -> TOptional<double>` (the distance along
    the ray to the floor sphere; 0 when on or under it and heading in; unset
    when the ray does not meet it; for the inside-out edge, the far root);
    `ShipFlight::MaySpeed(double D, double BrakingAccel, double HoldSeconds,
    double Step)`; `ShipFlight::Room(TConstArrayView<FFlightSurface>, From)`.
    `Tests/ShipFlightSurfaceTest.cpp`, **`DeepSpace.Ship.FlightSurface`**: a
    ray at the centre, at the limb (just in, just out), from on the sphere
    above and below its horizon, from under it, from inside the edge; the
    edge's far root; `MaySpeed` continuous and monotonic in D, equal to D / N
    far out, to the braking curve near in, and never more than D / step.
    **`ShipFlight::SecondsToFloor(double D, double Speed, double
    BrakingAccel, double HoldSeconds)`** (ruling 3), the live ETA's law: the
    ship holds `Speed` until `MaySpeed(d)` falls to it (d1 = Speed x N, or
    Speed^2 / (1.6 a) below the braking knee), then d falls by e every N
    seconds to the knee (d2 = 1.6 a N^2, 1,024 m at full boosters), then it
    brakes, 2N seconds from the knee: `(D - d1) / Speed + N ln(d1 / d2) + 2N`.
    Infinite at rest. `DeepSpace.Ship.FlightSurface` gains: it agrees with a
    small integrator of `MaySpeed` at 120 Hz to 0.5 s from every second of a
    settled approach, for 0.2 AU and 250,000 km at 1 c (the amendment's
    simulation gives 0.11 s); it is monotonic in D and in Speed; it is
    infinite at zero speed.
0.3 **Moved to Track A (stage 2)** by the build order: nothing compiles
    against it before A makes it real, so it is declared and implemented
    there, in one pass. It is **`FShipFlightState`'s new public surface**: `FShipFlightLimits` loses
    `DriveTau` and `DriveFloor`, and gains `HoldSeconds`, `DriveTop`,
    `DriveResponse`; `FShipFlightCommand` gains `int32 DriveNotch`;
    `SetDriveRoom` becomes `SetSurfaces(TArray<FFlightSurface>)`,
    `GetDriveRoom` becomes `GetRoom()`; new `EFlightMode {Cruise, Drive,
    SpoolingDown}` and `GetMode()`; `EFlightHold {Free, HoldingOff, AtFloor}`,
    `GetHold()`, `GetHeldFraction()` (against the lever's speed),
    `GetLeverSpeed()` (what the live lever asks, cm/s, signed for cruise
    astern), `GetOtherLeverSpeed()`, `GetDrivePosition()` (p).
    `ShipDrive::AwayFromSurface` is deleted with its test cases.
0.4 **`SkyProjection::RenderedFloor(double RadiusCm, const FSkyViewParams&)`**,
    extracted from `Project`, which calls it. Stays in Track 0 (stage 1):
    Track C edits `SkyProjection.cpp` again in stage 3, a stage later. `DeepSpace.Sky.Projection`
    gains: 10 km for small bodies, 1.6e-3 R for large, and `Project` draws a
    body at exactly `R + RenderedFloor` when the ship is below it.

### Track A: flight, the levers and the input (~6 h; stage 2)

Owns `Ship/ShipFlightState.*`, `Ship/ShipSubsystem.*`,
`Player/DeepSpaceCharacter.*`, `Ship/ShipHumComponent.cpp`,
`Tools/setup_flight_input.py`, `Tools/sky_probe.py`, the new input action
assets, and the tests listed. It does 0.3 first (moved here from Track 0).

A1. **`FShipFlightState::SubStep`**: the drive per decisions 3-5 (eased p;
    speed along the nose; `MaySpeed` of the nearest `RayToFloor` over every
    surface, every substep; p set to `PositionOf` the held speed; `LastHold`
    and the held fraction recorded; zero acceleration reported); the
    spool-down state per decision 4's table; cruise's target speed along the
    commanded direction held to `MaySpeed`, and the hard stop at every floor
    sphere; engaging starts p at the forward speed. `SetCommand` no longer
    clamps on disengage. **`JumpTo` also zeroes the velocity and p**, so every
    jump arrives at rest (decision 4); it is the one arrival path both jumps
    take.
A2. **`UShipSubsystem`**: `SetHelmInput(Commander, const FHelmInput&)` with
    `FHelmInput {FVector Attitude; bool bUpHeld, bDownHeld; int32 UpPresses,
    DownPresses;}`, `AllStop(Commander)`, `SetDriveLever(Commander, Notch)`
    (absolute, for tests), all pilot-gated and inert in transit;
    `SetFlightCommand` kept as the absolute cruise setter; the tick applies
    presses (`TapUp`/`TapDown` against the live p, or `SweepCruise`) and the
    hold's repeat, then `ApplyAllocation` sets `DriveResponse =
    ds.Drive.Response x thrust`, `HoldSeconds`, `DriveTop`, and
    `UpdateSurfaces` builds the list from `LocalSystem::Here` (not `Current`:
    the neighbours are never surfaces) with `FloorFor` each body --
    `max(ds.Flight.Floor, SkyProjection::RenderedFloor(R))` for planets and
    moons, `ds.Flight.StarFloorRadii x R` for the star, `ds.Flight.Floor` for
    the edge -- before `Step`; an empty list in transit. `TransitBegan` also
    sets both levers to STOP. `ClearPilot` zeroes held lever input as it
    releases attitude. CVars `ds.Drive.Top` (default 1 c, clamped to at most
    1: ruling 1), `.Response`, `.Sweep`,
    `.HoldSeconds`, `ds.Flight.Floor`, `ds.Flight.StarFloorRadii`,
    `ds.Cruise.Sweep`, as `TAutoConsoleVariable`s read at use (a per-frame
    `FindConsoleVariable` fails `test.sh`); `ds.Drive.Tau` and `ds.Drive.Floor`
    go.
A3. **`ADeepSpaceCharacter`**: `Throttle`, `GetThrottle()` and the
    `ThrottleSweepRate` `UPROPERTY` go (tests ask the ship); `ThrottleAction`
    becomes `LeverUpAction` and `LeverDownAction`, each bound on `Started`
    (count a press) and `Triggered`/`Completed` (held), handed over once a
    frame in `SetHelmInput` and the counts zeroed; new `StopAction`
    (`IA_Stop`), `Started` -> `AllStop`; `PressStop()` and `TapLever(int32)`
    for tests beside `PressDrive()`. **For the map spec (its decision 13):**
    `CycleTargetAction` (`IA_CycleTarget`), bound on `Started` to a
    `CycleTarget()` that does nothing until stage 4 fills it in. It is built
    here so the Blueprint is recompiled once for every new input, not again
    in stage 4.
A4. **`ShipHumComponent`**: the hiss's lever term is the live lever's
    fraction -- `|Throttle|` in cruise, `p / (NotchCount - 1)` under the drive.
A5. **`Tools/setup_flight_input.py`**: `IA_LeverUp` (Boolean) on `LeftShift`
    and `IA_LeverDown` (Boolean) on `LeftControl` replace `IA_Throttle` and its
    two mappings; `IA_Stop` (Boolean) on `X`; `IA_CycleTarget` (Boolean) on
    `Tab`, for the map spec; all clash-checked like `F` and assigned on
    `BP_DeepSpaceCharacter`'s defaults. **`Tools/sky_probe.py`**:
    the drive law becomes decision 5's, so its table matches the game.
A6. **[editor]** `./rebuild.sh --force`; `setup_flight_input.py`;
    recompile and save `BP_DeepSpaceCharacter` (two `UPROPERTY`s it saved,
    `ThrottleSweepRate` and `ThrottleAction`, are gone, and four input
    actions are new: ADR 0002's second amendment); `check_blueprints.py`;
    then the tests.

### Track B: the HUD (~2 h; stage 3)

Owns `UI/ShipHUDWidget.*`, the HUD tests, and, this stage only,
`Tests/SliceLoopTest.cpp` for the rename's one call.

B1. Public pure `SpeedWords` beside `AltitudeWords`; `MotionLine(const
    FShipFlightState&)` returning the ink and dim parts; `AltitudeLine(Cm,
    Surface, bEdge, EFlightHold)`; the motion line as two text blocks in a
    horizontal box; the corner asks `GetHold()` and `GetMode()`;
    `DriveHoldsAtFloor` and `ds.HUD.FloorBand` go; `DriveLine` and
    `DriveLineText` become `JumpLine` and `JumpLineText` (and
    `SliceLoopTest`'s one call: a one-word edit, now a stage after Track A's
    rewrite of the same file, so there is nothing to coordinate).

### Track C: the dust and the star (~4 h, including the render check; stage 3)

Owns `Ship/ShipCounterFrame.*`, `Sky/SkyProjection.cpp` (the warmth term),
`Sky/ShipSky.cpp`, their tests, and the temporary eyes test.

C1. **Dust**: `ShipDust::SeenSpeed(v, Knee, DustTop, Top)` and
    `ShipDust::Stretch(v, Knee, Top, MaxStretch)`, pure, in
    `ShipCounterFrame.h`; `NearStarPositions` becomes field-space offsets,
    advanced by the seen velocity along the true velocity and placed through
    the ship's rotation; the transit streaks re-expressed on them, shape
    unchanged; `FadeMotes` and `ds.Sky.MoteFadeSpeed` go (the dynamic material
    copy stays: the course marker is tinted from it); `ds.Sky.DustKnee` 2 km/s,
    `ds.Sky.DustTop` 3 km/s, `ds.Sky.DustStretch` 8. The top is asked of the
    ship (`GetLimits().DriveTop`), never stored.
C2. **Star**: the warmth term honest to `MaxStarWarmth` in `Project`, behind
    the temporary live `ds.Sky.StarWarmthGamma`.
C3. **[editor, eyes]** `Tests/Eyes/StarGlareEyesTest.cpp`, run once as in
    decision 9. The frames go to the developer; the chosen law and
    `StarSurface` are written in and the test and its CVar deleted.

### Tests that pin today's drive, and what replaces them

| Test | Pins today | Becomes |
|---|---|---|
| `DeepSpace.Ship.FlightDrive` (`ShipFlightStateTest.cpp`) | room falls by e per tau; settles onto the 100 km floor; half throttle doubles tau; starved quadruples it; disengage clamps to 200 m/s; backing closes no faster | Rewritten (A). **The lever:** a notch's speed is its table speed once settled; the ease never overshoots; STOP comes to rest; no reverse. **The cap binds only on the path:** a trajectory whose undisturbed miss altitude is above the floor is **bit-identical** to the same flight with no surfaces; with the nose fixed, velocity stays along the nose to 1e-9 through the whole approach. **Aim error:** from 0.2 AU at 1 c with 0, 0.01, 0.1 and 1 degree off, the ship either reaches the floor or passes at its undisturbed miss distance, and never ends farther than it started except by passing. **Arrival:** from STOP, 0.2 AU at 1 c reaches the floor within 170 s and 250,000 km at 1 c within 65 s; **never below any floor at any frame chop, including a 2 s hitch**; comes to rest on the floor with no substep's speed change larger than the braking curve's. **Every surface:** at 1 c past a giant toward its moon, and toward a planet while the star is nearer, at every frame chop from 30 to 144 Hz and a 2 s hitch, no substep ends inside any body or floor sphere. **Under and inside:** a ship placed under a floor, or inside a body, climbs out at the lever's speed and cannot descend. **p follows the hold:** in notch space p never rises faster than `DriveResponse`, including across a release by a turn off the limb at 1 c and by a climb; it falls faster only on the capture substep. **Spool-down:** each row of decision 4's table. `GetHold` says `HoldingOff` then `AtFloor`, and never flickers under a steady hold; the edge caps like a body; starved thrust slows the response four times and leaves the top alone; attitude untouched and zero acceleration reported (kept). **The top:** no notch and no speed above 1 c whatever `ds.Drive.Top` says. **At rest after a jump:** `JumpTo` leaves velocity and p at zero |
| same file, cruise | "drive off, the room changes nothing" | Cruise brakes to rest on the floor, never below, at full and quarter thrust; a turn made while drifting in hits the hard stop and slides, never enters; far from anything cruise is exactly today's |
| `DeepSpace.Ship.Drive` (`ShipDriveTest.cpp`) | gating; tau stretched by thrust; `ds.Drive.Tau`/`.Floor` read at use; room = nearest surface less floor; room/e per tau through the subsystem | Gating kept, for `SetHelmInput`, `AllStop` and `SetDriveLever` too, and inert in transit; each lever survives F and standing up; X stops both; a tap under a hold slows the ship at once; `DriveResponse` is `ds.Drive.Response x thrust`; the new CVars read at use; `FloorFor` is `max(ds.Flight.Floor, RenderedFloor)` for an Earth and a giant, `StarFloorRadii x R` for the star, and `ds.Flight.Floor` at the edge; `GetRoom` is the minimum over surfaces of distance less that surface's floor |
| `DeepSpace.Player.FlightInput` | the pawn's `Throttle` sweeps at 0.5/s and has stops; F toggles | The ship's cruise lever sweeps at `ds.Cruise.Sweep` and holds; the detent at zero; under the drive a tap is one notch and a hold repeats; **a press and release injected inside one frame moves one notch**; X stops both; a non-pilot moves nothing |
| `DeepSpace.Loop.Drive` (`SliceLoopTest.cpp`) | twenty tau to the 100 km floor, e^-10 at ten tau; sixty tau outward stops short of the edge | From the opening shot, lever from STOP to 1 c: never farther, never shrinking, never below the floor, **at the floor within 70 s** (54 s for an Earth and 64 s for a Jupiter at the opening distance), drawn at its true size there to 1e-3; placed 0.05 AU inside the edge and flown out at 1 c for 60 s (uncapped it would cross 0.12 AU), it never leaves its system and settles 10 km inside. Plus the same approach with the opening shot's nose 0.01 degrees off the centre: the same floor, the same time to within a second |
| `DeepSpace.Loop.Jump` | **nothing of the drive** (it steers at throttle 0 and never engages it) | The `JumpLine` rename; plus: with the drive lever at 1 c when the fold opens, the ship arrives at rest, velocity exactly zero, with both levers at STOP. The in-system jump's arrival is pinned the same way in the map spec's `DeepSpace.Loop.InSystemJump` |
| `DeepSpace.Ship.Jump` (`ShipJumpTest.cpp`) | "between stars the drive has no room" | `GetRoom()` rename (still 0 between stars: no surfaces); plus: in transit the levers do not move |
| `DeepSpace.Ship.CounterFrame` | motes 90% at cruise, dark and hidden past 2 km/s under the drive | The dust is shown at every speed; `SeenSpeed` is the identity to the knee, continuous there, strictly increasing, and `DustTop` at the top; no mote is drawn beyond `NearFieldRadius` at any speed; stretch is 1 at or below the knee and at most `DustStretch` outside transit; the field streams along the velocity, not the nose, in cruise's slide |
| `DeepSpace.Ship.CounterFrameJump` | the streaks along forward, wrapped in the field; round the ship after the jump | Kept, on field-space positions; plus: at 0.25, 0.5 and 0.75 of a transit every mote's shown transform equals today's formula for the same field position (the streak shape is unchanged) |
| `DeepSpace.UI.HUDAltitude` | `DRIVE FLOOR` and its band; `DriveHoldsAtFloor`'s cases | The words for each `EFlightHold`, and `AT THE EDGE`; a ship at the floor's height with the drive off, or leaving, says neither; the corner ticked against the flight state draws what the seam gives |
| `DeepSpace.Ship.NavStart` | the opening shot is above `DriveFloor` | Above `FloorFor` the opening world (a rename) |
| new `DeepSpace.UI.HUDSpeed` | -- | `SpeedWords` at every boundary (999 M/S, 1.0 KM/S, 99.9, 100 KM/S, 2,000 KM/S, 0.01 C, 0.99 C, 1 C at the top); every notch reads as its label; the motion line's six shapes above, **the dim part naming the other lever in every mode, and `SPOOLING DOWN` during the spool**; no line in this corner ever holds a number of seconds or minutes (the ETA is the target line's, ruling 3) |
| `DeepSpace.Sky.Projection` | the star's surface as compressed T^2 | Honest T^4 below the ceiling and the ceiling above it; over the generated systems of the test seed's first sectors, the star's surface is at least 20x the brightest planet surface in its sky; the hottest star at the default knobs stays under half of half-float's maximum |

Nothing in `Tools/test_*.py` reads the drive; they are run unchanged.

### Tunables

| CVar | Default | Lives in | Replaces |
|---|---|---|---|
| `ds.Drive.Top` | 1 c, and never above it (ruling 1) | `ShipSubsystem.cpp` | -- |
| `ds.Drive.Response` | 3 notches/s, x the boosters' thrust | `ShipSubsystem.cpp` | `ds.Drive.Tau` |
| `ds.Drive.Sweep` | 3 notches/s held (after 0.3 s) | `ShipSubsystem.cpp` | -- |
| `ds.Drive.HoldSeconds` | 4 s | `ShipSubsystem.cpp` | -- |
| `ds.Flight.Floor` | 10 km (and never under the sky's own) | `ShipSubsystem.cpp` | `ds.Drive.Floor` (100 km) |
| `ds.Flight.StarFloorRadii` | 1 | `ShipSubsystem.cpp` | -- |
| `ds.Cruise.Sweep` | 0.5 /s | `ShipSubsystem.cpp` | the pawn's `ThrottleSweepRate` |
| `ds.Sky.DustKnee`, `.DustTop`, `.DustStretch` | 2 km/s, 3 km/s, 8 (a playtest gate) | `ShipCounterFrame.cpp` | `ds.Sky.MoteFadeSpeed` |
| `ds.Sky.StarSurface` | 1,000 until the developer's verdict | `ShipSky.cpp` | -- |
| `ds.Sky.StarWarmthGamma` | temporary, until the verdict | `ShipSky.cpp` | -- |
| `ds.HUD.FloorBand` | removed | -- | -- |

Constants, not CVars: the notch table, `EaseSeconds` 0.4 and
`RepeatDelaySeconds` 0.3 (`ShipDriveLever.cpp`); `MaxStarWarmth` 8
(`SkyProjection.h`); the 5% `HOLDING OFF` threshold (`ShipHUDWidget.cpp`);
the 80% braking margin, one constant for both modes (`ShipFlightSurface.cpp`).

### Documentation, after the merges

CLAUDE.md's *Flying* (X, the two levers, the detent, a tap from the ship's
speed), *The drive and the jump* (decisions 3-6 replace the tau paragraph;
the drive tops out at 1 c and anything faster is a jump; the edge sentence
stays; every fold is an all stop and every jump arrives at rest), *The sky* (the warmth
term, the dust's knee), and *Where each tunable lives* (the table above). Sky
decisions 2 and 8 get an amendment note pointing here.

## Risks

- **Capture may feel like a wall.** Steering onto a world above the smooth
  limit drops many notches in one substep. Under the 1 c top it is much
  milder than revision 2's (at most x1.6 for a pilot within half a degree of
  an Earth, x29 at five degrees; nothing on a Jupiter), but it is still
  abrupt. The HUD names it and the ship does what the nose said. The playtest
  decides; the successor
  is a fast finite fall above a hard `d / 0.25 s` bound (decision 5), not a
  cap on paths that miss.
- **Flying past may surprise.** At 1 c from 0.2 AU a nose held still a tenth
  of a degree off passes an Earth 46,000 km up. A pilot who keeps the bracket
  centred is captured at every bias up to five degrees, so passing needs a
  nose left alone. That is the ship doing what it was told, and it is the
  price of a cap that does not steer; the sibling spec's bracket and bearing
  let the pilot see it coming, and its target line now says it outright:
  `PASSING 46,000 KM UP` where an arriving path shows its ETA.
- **The drive is slow across a system, on purpose.** At 1 c, 30 AU is four
  hours. Without the in-system jump the long legs would be unflyable in
  practice; the build order lands the jump in stage 3, one stage after the
  lever, so no build that plays has one without the other for long.
- **The dust above the knee may read as nothing.** It is deliberately a small
  range; the gate in decision 8 asks the question, and the motion line and
  the target's distance carry the rest.
- **Sub-frame taps and Enhanced Input.** `Started` on a Boolean action should
  fire for a press released inside the same frame; `FlightInput` injects one
  to prove it. If it does not, the fallback is `InputComponent->BindKey` on
  the two keys, whose pressed events are counted per event, for taps only.
- **HOLDING OFF flickers** where a turn hovers at the 5% threshold against
  the lever. Hysteresis is one more constant if seen.
- **Honest warmth changes three suns in four.** Red dwarfs get dimmer and,
  below the tone-mapper's shoulder, redder. That may be exactly right, or may
  read as dull; the frames show both laws side by side, and the CVar lets the
  developer try both in play.
- **The Blueprint.** Removing `ThrottleSweepRate` and `ThrottleAction` leaves
  saved values in `BP_DeepSpaceCharacter`; A6's recompile, save and
  `check_blueprints.py` are not optional.
- **The seam with the map spec.** Both touch `ShipHUDWidget.*`,
  `ShipSubsystem.*` and `DeepSpaceCharacter.*`; the build order puts each
  file in one track per stage (`docs/superpowers/plans/2026-09-26-poc2-build-order.md`).

## Decisions needing sign-off -- all ruled, 2026-09-26

Each of these is expensive to reverse, constrains landing, or is a choice a
reasonable person could make differently. Everything else above has an
obvious default and is decided. **The developer has ruled on every item**
(the amendment at the top); each item keeps its question as it was put, with
the ruling after it.

1. **The drive lever: STOP plus 24 notches on a 1-2-5 series, 1 km/s to
   100 c, a tap per notch, counted from the ship's present speed.**
   *Alternatives:* a continuous log lever; a shorter range (to 10 c) or
   longer (to 1,000 c); a bottom at 500 m/s; taps counted from the lever's
   own position. *Recommended:* as specified. *Cost to change later:* low in
   code (a table, one CVar for the top, one function per tap), but the notches
   are what the player learns and what every speed readout says; changing
   them after playtests resets what the developer has calibrated by feel.
   **Ruled (ruling 1): the top is 1 c.** STOP plus 18 notches, 1 km/s to 1 c,
   a tap per notch counted from the ship's present speed, eased; anything
   faster is a jump, including a new in-system jump (the map spec's decision
   12).
2. **The soft cap binds only when the nose's ray meets a surface's floor
   sphere, and then caps the whole speed along the nose, to that distance
   over N = 4 s, finishing on the boosters' braking curve.** *Alternatives:*
   cap only the closing component toward the nearest surface (the first
   draft: unstable, and binds on misses); cap the whole velocity against the
   nearest surface's room whatever the heading (today's rule: the auto
   slow-down); a margin sphere that also binds on near misses (softer
   capture, but holds back ships that would not hit); a longer N (6-8 s: a
   softer, slower final approach). *Recommended:* the ray rule at 4 s. *Cost
   to change later:* low in code (two pure functions); but landing's approach
   is designed on top of whichever it is. This one hands landing a ship that
   goes where it points and can come down anywhere its nose is, at a speed
   set by time to the floor; the others hand it a ship that is steered or
   slowed by the ground. **Ruled (ruling 2): as specified.**
3. **The floor is the sky's rendered floor -- 10 km over an Earth, 112 km
   over a giant, one stellar radius over a star -- and cruise obeys it too,
   so the POC never goes lower.** *Alternatives:* a flat floor (10 km, or
   keep 100 km); the floor for the drive only, cruise free below; the star's
   floor at the renderer's 1.6e-3 R (1,100 km over a Sun, the disc filling
   the view) or at ten radii. *Recommended:* as specified. *Cost to change
   later:* the numbers are CVars; the rule sets where landing begins, and
   landing will lower both this and the sky's floor together. **Ruled
   (ruling 2): as specified; this is where landing will later take over.**
4. **Star surfaces go as honest T^4 with a ceiling at 8x, amending sky
   decision 2; the developer picks `StarSurface` in play.** *Alternatives:*
   keep the compressed T^2 and lower `StarSurface` alone (every sun dimmer,
   red dwarfs still 3.6x the rest); lower bloom (dims every point with the
   disc). *Recommended:* honest with the ceiling. *Cost to change later:* one
   line; but it is the look of three suns in four, and every later judgement
   of a red-dwarf system is made on it. **Ruled (ruling 5): honest with the
   ceiling; `StarSurface` chosen live from the frames.**
5. **No arrival time anywhere -- not on the HUD, not on the system map.**
   *Alternatives:* the map spec's coarse, static words for a lever setting
   (`at 10 C: about a minute`, never recomputed as the distance falls); a live
   ETA; a time-to-surface under the cap. *Recommended:* none for this build,
   with the static words as the fallback if a pilot cannot choose a notch
   without them. *Cost to change later:* cheap either way; but it sets the
   precedent both specs share, so it should be ruled once for both (the map
   spec's item 3). **Ruled (ruling 3): a live ETA**, on the target line and
   the map, at the present speed; none in this spec's corner. The
   no-countdown rule is scoped back to the jump's charge in CLAUDE.md.
6. **The dust is honest to 2 km/s, then a compressed representation, and it
   is a playtest gate.** *Alternatives:* honest dust with today's fade,
   leaving speed to the planets and the HUD; a wider compressed range carried
   by long streaks (which reads as the fold). *Recommended:* the knee, with
   the three questions of decision 8 answered in play. *Cost to change
   later:* low (one function, three CVars); it is here because it is the one
   place the design knowingly shows something untrue, and because it cannot
   carry per-notch speed and says so. **Ruled (ruling 5): the knee law,
   judged in play.**
7. **Each mode keeps its own lever across F, X stops both, and after X the
   lever starts again from STOP.** *Alternatives:* the drive resets to its
   bottom notch when engaged; X stops only the live lever; a double-tap of
   Shift after X restores the notch before it. *Recommended:* as specified,
   accepting a seven-second hold to resume 10 c after an all stop (six to
   resume 1 c, under ruling 1). *Cost to change later:* low; it is on the
   list because it is exactly the feel the developer asked to be able to
   control, and they may want it otherwise. **Ruled (ruling 5): as
   specified.**
8. **The fold is an all stop: the ship comes out of every jump at rest, both
   levers at STOP.** *Alternatives:* only the drive lever to STOP (a cruise
   lever left on carries the ship on at up to 200 m/s); the fold disengages
   the drive; the levers carry across (the first draft: a drive lever left on
   flies the arrival into the star and parks at its floor). *Recommended:*
   all stop. *Cost to change later:* one line in `TransitBegan`; it is here
   because it sets the first moment in every system, the vision's arrival,
   and it is the one change this spec makes to the jump. **Ruled (ruling 5):
   every jump, interstellar and in-system, arrives at rest with both levers at
   STOP**, made exact by `JumpTo` zeroing the velocity (decision 4).
