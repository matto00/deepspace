# DeepSpace — The System Map and the Target

**Date:** 2026-09-26
**Status:** Amended with the developer's rulings (below), after a revision
following review the same day. Not implemented. *Review record*, at the end,
lists what changed and where the review was not followed.
**Answers:** the second playtest's note 3, under the developer's ruling C
**Sibling:** `docs/superpowers/specs/2026-09-26-flight-feel-design.md`
("Flight Feel: Two Levers, a Soft Cap, and Speed You Can See"). That spec
answers notes 1, 2 and 4 and the playtest's "mainly" paragraph, under rulings
A and B: the drive lever (its decisions 1-4), the soft cap (5), the floor (6),
the speed words (7), the motion cue for note 2, dust at a seen speed that
grows with speed and never fades (8), and the star glare of note 4 (9). The
two specs edit some of the same files; see *Build tracks*.
**Follows:** navigation and arrival (implemented), the sky (implemented), the
lived-in ship (implemented)
**Builds on:** ADR 0002 (C++ first), ADR 0003 (ship state as a subsystem, the
course as an id), ADR 0005 (the ship is the origin)
**Governed by:** `docs/vision.md`: *scale is only felt in contrast*, *the
anti-chore principle*, *shared presence, not division of labour*
**Built in the order of:** `docs/superpowers/plans/2026-09-26-poc2-build-order.md`

## Amendment, 2026-09-26: the developer's rulings

The developer read this spec and the flight-feel spec together and ruled on
both sign-off lists. The rulings are binding and override anything below that
disagrees. Every decision they change has been revised in place, with each
superseded choice kept beside it as a rejected alternative, as the review
revision did. Two decisions are new (12, 13) and so is *Open questions*.

1. **The drive tops out at 1 c, and there is an in-system jump.** Verbatim:
   "Top out at 1c, anything faster should be a jump (we should eventually
   limit the amount of jumps before a cooldown period. 1c already feels
   somewhat like quite the stretch from realism. there can be an option to
   'jump to planet' within a system". At 1 c, 1 AU is 8 min 19 s and 30 AU is
   4 h 9 min (the flight-feel spec's decision 3). So, with a world targeted,
   **the existing jump machinery -- plot, engage, charge, align, the fold
   opening by itself, transit, arrival at rest -- carries the ship to it**,
   arriving at a standoff above that world instead of at a star: new
   **decision 12**. The fold opening no longer clears the target when the jump
   is in-system (decision 5). **The jump cooldown is future work and is not
   built**; it is recorded in *Open questions* with its tension against the
   anti-chore principle, because it is a wait imposed on the player.
2. **The soft cap as specified** in the flight-feel spec (its decision 5): it
   binds only when the nose's ray meets a floor sphere. That was this spec's
   recommendation (decision 9), and it is adopted.
3. **A live ETA.** Verbatim: "Live ETA, i don't recall ruling out a countdown
   in this regard. This would be good." The developer is right: the
   no-countdown rule was about the **jump's charge**, a filling bar the player
   waits on, and CLAUDE.md had over-generalised it. That sentence is scoped
   back to the charge, and **the target line, on the HUD and the map, carries a
   live time to arrival at the current speed**: decision 6, rewritten.
4. **Picking a target: both, and the seats change.** Verbatim: "Both, key
   cycles targets when the map is focussed (an option from sitting in either
   seat (i.e., copilot doesn't auto lock to jump menu, they can choose either
   jump menu or map and that will zoom the screen. pilot just has look and
   click control of map. When map is zoomed, (Tab) switches between planets
   in the system." So: **at the helm** the pilot looks at the map and clicks
   it, live, without sitting down at it (decision 2, unchanged in substance).
   **In the chart chair**, the copilot seat, sitting no longer locks onto the
   chart: the seated player chooses the chart or the map, and the chosen
   screen zooms, with the existing fitted framing. **While the map is zoomed,
   Tab cycles the system's worlds as the target.** New **decision 13**;
   decisions 1, 2, 4 and 10 are revised.
5. **Everything else as recommended**: the map's warped-log radial scale
   (decision 3); the bracket seen by anyone who can see the target through the
   glass (decision 7); landing works from the nearest surface and uses the
   target only to name it (decision 9); the prograde mark shows with a target
   (decision 7).

The build order both specs follow is
`docs/superpowers/plans/2026-09-26-poc2-build-order.md`, which replaces this
spec's *Build tracks* (kept, marked superseded, for the reasoning).

## Context

The developer's second playtest, note 3, verbatim:

> we need a map of a system once we enter it, finding a new planet is very
> difficult, this should be on the middle screen in the cockpit (I am
> convinced that a planet wasn't rendering because i couldn't see anything at
> .03AU when i think i was heading straight to it).

And the ruling that answers it:

> **C. THE SYSTEM MAP: SEE IT, PICK A TARGET, GET A MARKER**, on the middle
> cockpit screen. Top-down star, planets, orbits, the ship. Selecting a body
> makes it the in-system target: a bracket on it through the glass, and its
> bearing, distance and name on the HUD. The player still flies there
> themselves. No autopilot.

What the ship has today, read from the code rather than the specs:

- **Nothing names a planet except the altitude line.** The HUD's bottom-left
  corner says `0.030 AU ABOVE Kessa II`: the distance to the *nearest*
  surface and whose it is (`UShipHUDWidget::AltitudeLineText`, printing
  `Here.Bodies[Nearest.Body].Id` from `LocalSystem::NearestSurface`). The
  nearest surface **includes the star**, body 0, whose id is the system's
  name, so the same line reads `0.030 AU ABOVE Kessa` when the star is
  nearest. The line says nothing about direction, and the nearest world is
  not necessarily the one ahead. That line is very likely where the
  developer's ".03AU" came from, and nothing records whether it named a
  world or the star.
- **The only marker through the glass is the course's**, a teal point on the
  dome at the plotted *star* (`AShipCounterFrame::SyncCourseMarker`), with the
  HUD's nose caret, an ink ring, laid on it when aligned. Both belong to the
  jump. **The caret shows only while a course is plotted**
  (`UShipHUDWidget::ShowsNoseCaret` requires `GetPlottedSystem()`), and
  arriving clears the course (`FShipNavState::Step`, `Plotted.Reset()`). So
  in a system the ship has just arrived in, nothing on screen shows where
  the nose points. At the helm the mouse keeps looking, so the view's centre
  is the pilot's head, not the ship's nose.
- **The only bearing words are the jump's.** `NavText::Bearing` prints whole
  degrees and says `dead ahead` inside the jump's cone, `ds.Nav.ConeDeg`, 8
  degrees. That cone is the right tolerance for starting a fold toward a
  star. It is the wrong tolerance for flying onto a disc of 0.16 degrees:
  from 0.03 AU (4.5 million km), 8 degrees off the nose misses the world by
  about 630,000 km, roughly 100 Earth radii.
- **The cockpit desk has three screens and one of them is real.**
  `props.py`'s `cockpit_desk` carries three `screen` parts, each 6 x 70 x 50 cm,
  at desk-local y -85, 0 and +85. The starboard one is covered by
  `AShipNavScreen`, the chart (`NAV_SCREEN = ("cockpit", (301, 285), 105, 0)`).
  The port one, dead ahead of the helm, and the **middle one, on the ship's
  centre line between the two chairs**, are plain grey boxes.
- **The pointer is off at the helm.** `ADeepSpaceCharacter::UpdatePointer`
  deactivates it whenever the player is seated: *"a pointer live at the helm
  would fight both"* the controls and E.
- **Procgen already has the id this needs.** `FBodyId {System, Planet,
  Moon = -1}` exists in `StarSystem.h`, `FPlanet::Id` is filled with the
  orbit index (`StarSystemGenerator.cpp`), and nothing reads it yet. There
  are no moons: the shape has room for them and the generator makes none.
- **The level cannot be loaded by a test.** `L_Hauler` is regenerated on
  main and never committed from a branch (`HaulerDressingMarkers.h`), so the
  tests that need the hauler's geometry spawn it from the layout's own
  export rather than loading the map.

### The .03 AU mystery: what can be said, and what cannot

At 0.03 AU (4.5 million km) an Earth subtends 0.163 degrees. The helm's view
is 103 degrees across; on a 3840-pixel-wide display that is 26.7 px a degree,
so the Earth is **4.3 px across**. `FSkyViewParams` draws anything under 2 px
as a point and resolves it into a shaded disc between 2 and 4 px, so at 0.03
AU a world is a resolved disc of about four pixels.

Nothing recorded which body the reading named or where the nose pointed, so
the account below is a set of hypotheses, not a finding:

1. **The night side, in the glare (the leading hypothesis).** "Heading
   straight to it" from an arrival means flying inward, toward the star, and
   a world between the ship and its star shows its night side. `M_SkyBody` is
   Lambert-shaded, and the disc-averaged Lambert phase
   (`SkyProjection::LambertPhase`) is 0.015 at a 150-degree phase angle and
   0.0006 at 170. Four black pixels on a black sky are invisible. In a
   red-dwarf system (three in four) the world also sits within a few degrees
   of the star, inside the glare of note 4, which the flight-feel spec
   measures at a median 3.6 times a Sun's at arrival.
2. **The reading was altitude above the star.** In a red-dwarf system,
   0.03 AU above the star is inside or near the inner orbits. Then the line
   named no world at all, and the world the developer meant could have been
   anywhere, at any distance.
3. **The nearest world was not the one ahead.** The line named a world
   0.03 AU away, while the one the nose was on was farther, a sub-pixel point
   in the glare.
4. **The nose was not on it.** With no caret in-system, and a head that
   turns with the mouse, "heading straight to it" was a judgement made by
   eye against a view whose centre is the head. Anything up to many degrees
   off would have looked straight ahead.

All four have the same remedy, which is what ruling C asks for: a marker
drawn from geometry rather than brightness, a nose caret whenever there is
something to aim at, and a bearing precise enough to aim with. What can be
tested headless is only the first hypothesis's premise: that the proxy is
drawn at the right size and place, and dark (decision 8). Whether four dark
pixels can be *seen* is a question for eyes (decision 8's `ds.Sky.Goto ...
night`).

## Goals

- A **map of the system the ship is in**, on the middle cockpit screen,
  readable from the helm without getting up: the star, every world and its
  orbit, the ship, and the target.
- **Picking a target from the helm**, on the map, with the hand already on
  the mouse.
- **A bracket on the target through the glass**, legible whether it is a
  sub-pixel point, four black pixels on the night side, or a disc filling
  the view, and an arrow at the edge of the view when it is not in it.
- **Something to aim with**: the nose caret whenever a target resolves, a
  prograde mark showing where the ship is actually going, and a bearing that
  says `dead ahead` only when the nose is on the world.
- **The target's name, bearing and distance on the HUD**, in the words the
  HUD already uses, and **a live time to arrival** when the ship's path will
  bring it down on the world (ruling 3).
- **A jump to a world within the system**, on the jump the ship already has
  (ruling 1), for the legs the 1 c drive makes long.
- **A chart chair that is a seat, not a lock**: sit, then choose the chart or
  the map, and Tab through the worlds on the map (ruling 4).
- **Tests that show the proxy in the .03 AU geometry is drawn at the right
  size and place, and dark**, and a one-line way to look at it by eye.
- Every tunable a console variable or a named, tested constant, and most of
  it headless-testable.

## Non-goals

- **Autopilot, or anything else that flies the ship toward the target.**
  Ruling C. The target changes what is drawn and said, and nothing in the
  flight state reads it. The in-system jump (decision 12) is not an
  exception: it is the fold, which the player plots, engages and aims, and it
  lets go two degrees from the world, at rest, leaving the approach to them.
- **The drive, its lever, the soft cap, the floor, and the motion cue.** The
  flight-feel spec, decisions 1-8. This spec changes no speed.
- **The star brightness of note 4.** The flight-feel spec, decision 9. This
  spec notes only that the target most likely to be lost is lost in exactly
  that glare.
- **Landing.** The next sub-project. This spec leaves it an id to read and
  says what it must not assume (decision 9).
- **Moons.** Procgen makes none. The map and the id have room for them
  (decision 3) and nothing more.
- **A galaxy map.** The chart shows what is near. The map shows *here*.
- **The interstellar jump.** Untouched, except that its fold opening lets go
  of the target (decision 5). The in-system jump reuses its machinery and
  changes none of it (decision 12).
- **A limit on jumps, or a cooldown.** Future (ruling 1); see *Open
  questions*.

## Decisions

### 1. The middle desk screen becomes `AShipMapScreen`, read and used from the helm and the chart chair

*Revised by ruling 4: the chart chair can now zoom the map (decision 13).*

**Which screen.** The middle of `cockpit_desk`'s three `screen` parts: desk-
local (20, 0, 105), so cockpit (305, 200), world (1715, 0, 105), with its aft
face at cockpit x 302. It is on the ship's centre line, between the helm (the
port pilot seat, cockpit (175, 130)) and the chart chair (the starboard seat,
(175, 270)). The port screen, dead ahead of the helm, stays decorative; the
developer named the middle one, and it is the one both chairs can see.

**How it becomes a screen.** As the chart did: an `AShipScreen` subclass
whose origin is the centre of its glass, placed by `build_hauler.py` from a
layout tuple shaped like `NAV_SCREEN`:

```python
MAP_SCREEN = ("cockpit", (301, 200), 105, 0)   # 1 cm proud of the prop's aft face
MAP_SCREEN_WIDTH = 68                          # AShipMapScreen's PanelWidthCm
```

The panel is 68 cm wide, filling the prop's 70 x 50 cm face with a centimetre
of bezel all round. It stands 1 cm proud of the prop for the chart's reason:
the prop blocks `Visibility`, and a panel sunk into it hands every trace to
the prop.

**Drawn at the resolution it is seen at: 600 x 424 px, not the chart's 816 x
576.** The helm's eye (`SkyTestWorld::PilotEye`, (1585, -70, 170) as first written;
see the correction below) is 158 cm
from the panel's centre, 29 degrees to starboard and 24 degrees down, well
inside the seated view limits (100 degrees of yaw, 70 of pitch). From there
the 68 cm panel spans 68 x cos 29° / 158 = 0.376 rad, **21.6 degrees, about
575 screen pixels** on the 4K display at 26.7 px a degree. Its 48 cm height
spans 48 x cos 24° / 158 rad, **15.9 degrees, about 424 pixels**. A widget
component's render target is sampled without mips, so the chart's 816 x 576
would be minified 1.42 times at the helm. Every 1-2 px ring would shimmer or
drop out as the head moves, and text would lose strokes. At 600 x 424
(8.8 px/cm) the panel maps close to one widget pixel to one screen pixel at
the helm (0.96 across, 1.0 down). **Every size in decision 3 is therefore a
size in helm pixels.** Standing close, the panel is magnified instead: soft,
never aliased.

> **Correction (stage 1c review, 2026-09-26): the helm's eye was a standing
> one.** `PilotEye` (1585, -70, 170) is where a *standing* character's eyes
> are. `DeepSpace.Player.SeatedEyeIsPilotEye` now measures the seated eye --
> the real character in a helm seat, the sitting idle, `PlaceCamera` -- and
> it is **(1604, -72, 125)**: 19 cm forward of the seat, 45 cm lower. From
> there the panel's centre is **131 cm** off, 34 degrees to starboard and 9
> down; the panel spans **24.8 degrees, about 661 px**, across and **20.8
> degrees, about 556 px**, high. So at 600 x 424 the map is *magnified* at
> the helm, 1.10 times across and 1.31 up -- softer, but magnification does
> not shimmer, which was the reason for leaving 816 x 576 (now minified only
> 1.23 and 1.04). Whether the draw size should rise (the panel's 68:48 at
> ~788 x 556 would map one to one) is stage 1b's call, settled by
> `Eyes.MapFromHelm`, which captures from `PilotEye` and so from the right
> eye now. The same measurement showed the nose line from the real eye met
> the port desk screen before the glass; the port screen is now 30 cm tall,
> its top 15 cm under the eye (see `Tools/props.py`), pending the
> developer's word on the level.

**Rejected: the chart's 816 x 576, with fatter strokes** (rings of 2.5-3 px,
dots of 10 px or more). That treats the symptom. Minified 1.42 times with no
mips, text still loses strokes, and every size then needs a divisor to
reason about. Rendering at the seen resolution removes the problem where it
starts and costs nothing: the draw size is a per-class default.

**Read from the helm, not sat at.** The pilot is at the helm when they need
the map: flying, looking for the world they are heading for, with the mouse
already turning their head. So `AShipMapScreen` is **not `bUsable`**: E at it
does not sit anyone down, it has no chair of its own, and from the helm
nothing changes the camera. It is driven the way the engineering console is,
by the view-aimed pointer: standing, within the player's 250 cm reach, as
every screen is; and **seated, from either chair**, which is new (decision
2). **From the chart chair it can also be zoomed** (decision 13, ruling 4):
the chair beside it frames it as it frames the chart, on the seated player's
choice.

**Rejected: a sit-down screen like the chart and the laptop.** There is no
chair in front of the middle screen, and the chairs either side are the helm
and the chart's. A sit-down map would send the pilot out of the helm, which is
where the map is needed, and would make "look at the map" a trip. Reading the
map would stop being a glance. (The ruling's zoom is not this: it is the chart
chair's, reached from a chair the copilot is already in, and the helm still
never leaves the glass.)

**Rejected: the port screen, dead ahead of the helm.** It is nearer to the
pilot's line of sight, 1.5 m straight ahead, and would be a little easier to
read. But the developer named the middle screen, and the middle screen can be
read from both chairs. That matters, because two people in the cockpit should
share one map rather than each have their own.

### 2. Seated, the pointer drives the map, and only the map

*Ruled (ruling 4): "pilot just has look and click control of map". At the
helm this decision is unchanged in substance. The same gate now also serves
the chart chair while it is not zoomed (decision 13), so the virtual is named
for a seat rather than for the helm.*

`AShipScreen` gains one virtual, not a property:

```cpp
/** Whether the pointer reaches this screen from a seat -- the helm, or the
 *  chart chair while nothing is zoomed. Only a screen meant to be glanced at
 *  and touched while flying says yes. A class decision, never a per-instance
 *  one: no edit in the level can make the chart drivable from the helm. */
virtual bool IsDrivableSeated() const { return false; }
```

`AShipMapScreen` overrides it to return true. (This review revision called
it `IsDrivableFromHelm`; the rename is the only change, and it is made before
any code exists.) Because it is not a
`UPROPERTY`, no placed instance in `L_Hauler` can change it, and
`DeepSpace.Ship.MapScreen` holds that the map says yes and the chart, the
laptop and the engineering console say no.

`UpdatePointer`'s seated branch stops deactivating the pointer outright and
gates it instead. It runs a line trace from the eye along the view on the
pointer's own channel (`Visibility`), out to `InteractionRange`, ignoring the
pawn. If the first thing hit is the widget component of an `AShipScreen`
whose `IsDrivableSeated()` is true, the pointer is active, with its usual
`World` source and aim. Anything else deactivates it as today and releases
the left button. The gate decides only *whether* the pointer is on. Its own
trace uses the same channel, eye, direction, range and ignored pawn, so it
finds the same widget. This is the code path that already works and is
tested for a standing player.

This keeps the old comment's reason and drops only its overreach. The pointer
is on the left mouse button, which nothing at the helm is bound to. E is
still "stand up", and the flight keys are still the ship's. What the rule
protected against was the pointer finding *any* screen in reach from the
seat. The chart is 2 m from the helm's eye, inside the 250 cm reach, and it
is sized to be read from 60 cm in its own chair. It stays unreachable from
the helm, because it does not say yes.

The HUD's dot already turns teal over a screen (`IsPointingAtScreen`), so the
pilot sees they can click without anything new.

**Rejected: a `Custom` interaction source, handed the gate's hit with
`SetCustomHitResult`.** It was the first draft. The gate's trace and the
pointer's own are the same trace, so handing one to the other adds an engine
path this project has never used and buys nothing.

**Rejected: a per-instance `bDrivableFromHelm` `UPROPERTY`.** It was the first
draft too. An `EditAnywhere` flag can be changed on one placed instance, which
would make the chart drivable from the helm with nothing to catch it. It is
also a reflected-layout change for no benefit.

**Rejected: a helm key that cycles the target** (say T, with the map
following). It is attractive, since cycling while looking out of the glass
would show the bracket hopping from world to world and answer "which one is
that?" directly. But it adds an input action to `setup_flight_input.py`,
takes a key from a helm with little room left (the flight-feel spec takes X),
and makes the map a readout of something picked elsewhere rather than the
place where picking happens, which is the opposite of the ruling. It stays
available as an addition. *Decisions needing sign-off*, item 1. **Ruled
(ruling 4): "Both", with the key placed where this objection does not bite.**
The key is Tab, and it cycles only while the map is zoomed at the chart chair
(decision 13), so it takes no key from the helm, and the map it cycles on is
the one in front of the player, so picking still happens at the map. At the
helm, look and click only: "pilot just has look and click control of map".

**Rejected: widening `InteractionDistance` until the map is inside it and the
chart outside.** That excludes the chart by one of two distances, 1.6 m and
2.0 m, and any layout change silently breaks it. The virtual is an explicit
decision on the class that owns it.

### 3. The map: a warped logarithmic orrery, fixed to the universe's axes

`USystemMapWidget`, a `UShipScreenWidget` built in C++. Its layout arithmetic
is a pure namespace, `SystemMap` (`UI/SystemMapLayout.h`), that takes an
`FStarSystem` and a few pixel sizes and returns positions, so all of it is
tested with no world. The orrery itself is painted by a small C++ widget,
`USystemMapView` (`NativePaint`: rings as line strips, dots and the star as
rounded boxes, the ship glyph as lines). It picks with the pure
`SystemMap::Pick` (decision 4) and says `NativeIsInteractable() == true`, so
the pointer counts it as a control and the HUD's dot turns teal over it.

**Top-down, fixed to the universe's axes.** Planets are circular and coplanar
in the universe XY plane (`FPlanet::SemiMajorAxisAU`, `PhaseRad`), so looking
down universe -Z shows every orbit as a true circle, with no projection error.
Universe +X points up the glass and +Y to the right. Seen from above in
Unreal's left-handed axes (X fore, Y starboard, Z up), that is the right way
round. Putting +X to the right, the maths-textbook habit, would draw every
system as its mirror image. The map never turns.

**Rejected: heading-up**, the map rotated so that the ship's nose points up
the glass. That only means anything for a ship yawing in the plane, and this
ship pitches and rolls freely. A heading-up top-down map spins whenever the
pilot rolls. Steering is what the HUD's bearing words, the caret and the
bracket are for. The map's job is *where things are*, and a map that holds
still is one the eye learns.

**Radius: logarithmic, warped so that no two orbits touch.** The 10,000
systems nearest home (`Saved/procgen_corpus.tsv`) have innermost orbits from
0.003 to 3.6 AU, outermost from 0.003 to 92 AU, up to `GenGuarantees::
MaxPlanets` = 12 worlds, and neighbouring orbits as close as 8% apart (0.033
decades). A linear map puts Mercury 5 px from the Sun when Neptune is on the
rim. A pure log map keeps ratios, but in the tightest systems it lays two
rings 3 px apart. So, with the star's disc radius `StarPx` (7) and the rim
`RimPx` (116):

1. **Knots.** Each orbit's radius `a_i`, in log10, is a knot. So is `r_in`,
   half the innermost orbit, which maps to the edge of the star's disc; and so
   is `r_rim`, which maps to the rim.
2. **Log placement.** Ring `i` goes where a log map puts it:
   `rho_i = StarPx + (log a_i - log r_in) / (log r_rim - log r_in) x (RimPx - StarPx)`.
3. **The gap is derived, so it always fits.**
   `MinRingGap = floor((RimPx - StarPx) / (MaxPlanets + 1))` = floor(109 /
   13) = **8 px**. Twelve rings, the gap inside the first and the gap outside
   the last need 13 x 8 = 104 px of the 109 available, so the worst system
   procgen can make fits by construction, not by luck. (Amended in delivery:
   the first cut divided by `MaxPlanets` for 9 px, left no gap outside the
   last ring, and a crowded system's outermost ring sat on the rim. The
   segment from the outermost orbit to the rim, which holds the arrival, was
   then drawn in no pixels, and the ship stood still on the outermost ring
   for the first leg of every approach.)
4. **The warp, two passes.** *Outward:* each ring is placed at
   `max(rho_i, rho_(i-1) + MinRingGap)`, the first at no less than
   `StarPx + MinRingGap`. *Inward:* the outermost ring is clamped to `RimPx -
   MinRingGap`, and each ring inside it to no more than the ring outside it less
   `MinRingGap`. Point 3 guarantees the inward pass never pushes the first
   ring into the star. Afterwards every gap is at least `MinRingGap`, every
   ring is inside the rim, and the order is kept. (The first draft scaled all
   gaps back proportionally when the pushes overran the rim. That breaks the
   minimum gap it exists to keep.)
5. **Everything else goes through the same warp**: a piecewise-linear map in
   log radius through the knots. The ship between orbits II and III is drawn
   between rings II and III, and crosses a ring on the map in the frame it
   crosses that orbit in space. The ship's azimuth is exact. Its radius is
   exact at every orbit and monotonic between them.
6. **Inside `r_in`, the ship is pinned to the star's edge.** No segment maps
   the space between the stellar surface and `r_in`, because the star's disc
   already stands for it (and in the tightest red-dwarf systems `r_in` lies
   inside the star). A ship there is drawn just outside the disc, at its true
   azimuth, and the footer says `Inside Kessa I's orbit.` The altitude line
   gives the true distance.

`r_rim` is `NavStart::ArrivalStandoffAU(System, ds.Nav.StandoffAU) x 1.25`,
the arrival point with a margin. The standoff is never less than 1.5 times the
outermost orbit (`NavStart::ArrivalStandoffAU`), so every world and every
arrival lands on the map. The scale is a function of the system alone. It
does not rescale as the ship flies, which would be disorienting. Beyond the
rim, out toward the system's edge, the ship is pinned to the rim, and the
footer says `Beyond the map.`

**The ship's radius is its true distance from the star, and its azimuth is
its position projected into the plane.** Arrivals come in along the line
from wherever the ship left, so the ship can be well above the plane. Taken
from the projection, a ship 2 AU over the pole would be drawn on the star.
Taken from the true distance, it is drawn 2 AU out, and the footer says
`18° above the plane` whenever the ship is more than 5 degrees off it. For
worlds, which are all in the plane, the two readings are identical.

**Rejected: zoomable linear**, with a scroll wheel or buttons. It is the
honest scale. But it needs a second control on a screen used at arm's length
from the helm, and it has a zoom level that is *wrong* for whatever the pilot
wants next. A map that must be driven before it can be read is not a glance.

**Rejected: ordinal**, equally spaced rings by orbit index, like a metro map.
It is always legible. It also throws away the one structural fact a system
has, inner rock huddled close and giants far out, which the warped log keeps
wherever it is legible. *Decisions needing sign-off*, item 2. **Ruled
(ruling 5): the warped log.**

**What each thing shows**, in helm pixels:

| Thing | Drawn as | Label |
|---|---|---|
| Star | a filled disc, 14 px, `SkyColour::Blackbody` of its temperature | none; the title names the system |
| Orbit | a 2 px ring in `Dim` | none |
| World | a dot on its ring at its true phase, 7 px for rock and ice, 10 px for a giant, **capped at its local gap less 2 px** (the gap to the ring, star edge or rim either side), in its sky colour (`FSkyBody::Colour`, so the map's world is the window's) | its numeral, `IV`, outside its dot, away from the star |
| Target | an `Accent` ring round its dot, the dot's size plus 6 px, capped at twice the local gap less 2 px | its row carries the chart's `›` mark |
| Ship | a 9 px ring with a 6 px tick along its nose, projected into the plane; ring only when the nose is within 20 degrees of vertical | none |
| Moon (when procgen has them) | a 4 px dot at a fixed offset round its planet's dot, since a moon's orbit is sub-pixel at any system's scale | listed indented under its planet |

The colour comes through `LocalSystem::Here(System)`, the pure adapter the HUD
already uses, as body `i + 1` (`FSkySystem::FromSystem` puts the star first,
then planets innermost first). A test holds that order.

**The panel**, 600 x 424:

```
+----------------------------------------------------------------------+
| SYSTEM                                         Kessa · red dwarf     |
|  +--------------------------+   › II   terrestrial        0.214 AU   |
|  |          .  III          |     I    barren             0.340 AU   |
|  |     .   ( I )   o II     |     III  ice                0.521 AU   |
|  |       (   *   )          |                                        |
|  |     <ship>               |                                        |
|  +--------------------------+                                        |
|  › Kessa II · 0.1° to port · 0.214 AU · ETA 2 MIN · NIGHT SIDE       |
|  18° above the plane                                  [ Jump here ]  |
+----------------------------------------------------------------------+
```

- **Title** row, y 6-32, size 14.
- **The orrery**, a 256 px square at x 6-262, y 36-292, centre (134, 164),
  the rim at 116 px, leaving 12 px outside it for the outermost numeral.
- **The list**, x 272-594 (322 px), one 24 px row per world in orbit order,
  12 rows at most (y 36-324). Columns: the numeral, always, in 40 px (34
  cut `VIII`); the kind in 94, or an inhabited world's given name in its
  place; the distance to its surface right-aligned in 176. (Amended in
  delivery: the first cut put a given name in the numeral's column, clipped
  to `Hald`, and the orrery labels every dot by numeral, so exactly the
  inhabited worlds could not be matched to their dots. An inhabited world is
  temperate, so the kind it gives up is terrestrial or ocean all but always;
  `Halden · Kessa II` in full is the target line's.) The widest distance `AltitudeWords` prints,
  `1,496 THOUSAND KM`, is 17 characters, mostly capitals and digits, which
  run wide: about 180 px at size 14. That width is why the orrery is 256 px
  and not larger.
- **The band**, y 330-418: the target line (decision 6) in size 14, wrapping
  to at most two lines (y 330-370), then the footer in size 13 at the left
  and, at the right on the same row, the one button decision 12 adds, 24 px
  tall and 120 px wide (y 374-398): `Jump here` with a target that can be
  jumped to, `Stand down` while the course is the target, disabled with
  `Near enough to fly` inside the target's reach, and absent with no target.

Slate sizes fonts in points at 96 DPI, so size 14 has an em of 18.7 widget
pixels. At the helm that is **about 18 screen pixels, with capitals about
13 px tall**. A row is **24 screen pixels, 0.9 degrees**, tall. (The first
draft claimed 44 px for a 3 cm row. The right figure for that design was
about 26 px: 1.09 degrees x 26.7 px a degree x cos 24° for the tilt.) Whether
this is comfortably legible from the helm is **a gate before the map
merges**, not a known quantity (decision 11). The sizes are constants in
`SystemMapWidget.cpp`.

Kinds are `NavText::WorldKind`: `barren`, `terrestrial`, `ocean`, `ice`, `gas
giant`, which is procgen's taxonomy in lower case. Distances are
`UShipHUDWidget::AltitudeWords`, the altitude line's own words, called where
they are. The flight-feel spec keeps them public and pure there, and nothing
moves. Between stars the map says `Between stars.` and draws nothing. A system
with no worlds (187 in 10,000) draws its star and says `Nothing orbits Kessa.`

**It stores nothing it could ask for.** The target, the ship's position and
its orientation are asked of `UShipSubsystem` every frame. Generating the
system costs a generation, so the map keeps **a cache of its own drawing**,
the chart's `FAskedAt` pattern: the rings, the dots, and each world's universe
position (for the row distances). The cache is **keyed on what the drawing
depends on, and nothing else**: the id of the system the ship is in, whether
it is between stars (in a star jump's transit; an in-system jump's fold is
not, since the system and its drawing are unchanged by it: decision 12), the
priors, and `ds.Nav.StandoffAU`. The id is asked every
frame of a new `UUniverseSubsystem::GetSystemIdAt(Where)`, which runs the stub
search `GetSystemAt` already runs (`FGalaxyGenerator::FindSystemAt`) and stops
before generating the system. A `PlaceShip` into another system, from the
console or a test, changes the id without a jump, so the jump serial adds
nothing to the key. The ship's position is not in the key: the layout does not
depend on where the ship is, and the chart's `MovedFarEnoughCm` exists only
because the chart sorts stars by distance. Planet positions are frozen
(`FPlanet::PhaseRad`: *nothing orbits yet*), so the cache is exact. When
worlds move, the key gains time. The ship's glyph, the row distances and the
target mark are recomputed from the cache every frame, and none of that
generates anything.

### 4. Picking is a click on a world, and again clears it -- or Tab, on the zoomed map

*Revised by ruling 4: "Both". A click, from the helm, standing, or the chart
chair; and Tab, while the map is zoomed at the chart chair (decision 13).*

Each world can be clicked in two places: on the orrery and on its row. Both
end at one seam, `USystemMapWidget::SelectWorld(int32 Orbit)`, which targets
or clears as the chart's `SelectRow` does:

- not the target: `Ship->SetTarget(FBodyId{Here, Orbit, -1})`;
- already the target: `Ship->ClearTarget()`.

**The row** is a `UButton` 24 px tall across the list's full 322 px. It is
the reliable target.

**The orrery** picks by the nearest dot. `SystemMap::Pick(const FMapLayout&,
FVector2D Point, float MaxRadius)` returns the orbit whose dot centre is
nearest the click, if it is within `PickRadius` (14 px), and nothing
otherwise. Every point of the orrery belongs to at most one world, the
Voronoi cell of its dot clipped to a 14 px disc, so two worlds at similar
azimuths on rings 8 px apart split the space between them at the midpoint
instead of sharing it. Ties go to the inner world. The star is not a pick:
a click within its disc picks nothing, even when a first ring is within
14 px. (The first draft gave each dot a 40 px hit square. Squares that large
overlap wherever rings are closer than 40 px, which is most systems, and
which world a click chose was undefined.)

A click on the orrery is **a press and a release on it**, as a row's button
is: the press arms it, the release picks where it lands, and leaving the
orrery between the two lets it go. A press made on a drawing that the same
frame replaces (the system changed, or the priors) picks nothing, on the
orrery or a row: the orbit it names is an index into what was on the glass.
(Amended in delivery: the first cut picked the orrery on the press and the
rows on the release, and resolved a press against whatever the refresh drew.)

**The star cannot be targeted.** It is the brightest thing in any sky, so a
marker would never find it for anyone. `FBodyId` has no form that names a
star, and giving it one changes a procgen type for nothing. Landing on a star
is not a thing either.

**Picking is not pilot-gated**, like the course. Anyone standing at the map
can pick, and a second player in the chart chair sees the same mark. It is
one ship with one target (vision: shared presence). The HUD line shows the
target to everyone aboard.

**Tab**, while the map is zoomed at the chart chair, targets the next world
outward from the target, wrapping from the outermost to the innermost, and
the innermost when there is no target. It goes through
`UShipSubsystem::CycleTarget()`, which asks the pure
`ShipNav::NextTarget(const FStarSystem&, const TOptional<FBodyId>&)`, and so
lands on the same `SetTarget` as a click. It never clears: a click on the
target does. Decision 13 has the rest.

**Console**, beside `ds.Nav.Plot` and named with it because it is navigation:
`ds.Nav.Target` lists the worlds, numbered by orbit; `ds.Nav.Target <n|name>`
targets one; `ds.Nav.Target next` cycles as Tab does; `ds.Nav.Target none`
clears.

### 5. The target is an `FBodyId` the ship holds, beside the course

**Who owns it:** `FShipNavState`, the pure navigation state, holds
`TOptional<FBodyId> Target` next to `Plotted`. `UShipSubsystem` fronts it the
way it fronts the course:

```cpp
/** False in transit, for a body not in the system the ship is in (asked of
 *  its position), and for an orbit that system does not have. */
bool SetTarget(const FBodyId& Id);
void ClearTarget();
/** As held. Resolve it against the system in hand (ShipNav::TargetPlanet)
 *  before drawing it: a PlaceShip into another system leaves an id that
 *  names nothing here, and that must draw nothing rather than the wrong
 *  world. */
TOptional<FBodyId> GetTarget() const;
```

and `ShipNav::TargetPlanet(const FStarSystem& Here, const FBodyId&)` returns
the planet, or nothing if the id's system is not `Here`'s or its orbit is out
of range.

**An id, never a copy** (ADR 0003, amended for the course): the planet's
position, radius, name and kind are asked of procgen every time. ADR 0003's
amendment grows by one clause: *the ship stores no universe data beyond the id
of the system it is steering for **and the id of the body it has marked**.*
Since ruling 1 the course may name that same body instead of a system
(decision 12); it is still an id, and still the only one of its kind.

**How it clears:** by clicking it again, by `ds.Nav.Target none`, and **when
a star jump's fold opens** (`FShipNavState::Step`, on
`ENavEvent::TransitBegan` with a star course). It does **not** clear on
arrival near the world, at the floor, when the pilot stands up, when the
drive is toggled, at all stop, or **when an in-system jump's fold opens**:
that jump's destination is the target, and the ship does not leave the
system (decision 12, ruling 1). A target is a setting
at the helm, and like the levers it stays where it is left.

**What happens on a jump:** leaving a system lets go of what was marked in
it. A jump within the system keeps it, and arrives with the world it went
to still marked. The new system arrives with no target, and nothing picks one for the
player. There is no "nearest world" auto-mark and no suggestion (see *The
anti-chore audit*).

**Rejected: clearing on arrival rather than when the fold opens.** The effect
is the same for play, since the sky is hidden between stars and nothing
draws the target. But a target held through the transit is an id for a
system the ship has already left, and it only fails to draw because every
reader happens to resolve it first. Letting go when the ship leaves the system
is when the id stops meaning anything.

**Rejected: `UShipSubsystem` storing the target itself.** Every other
navigation decision lives in the pure `FShipNavState`, and the clear-on-fold
rule is the state machine's own, so it belongs there and is tested headless
with the rest of the jump.

**Rejected: every fold clears the target** (this spec before ruling 1, when
every fold left the system). An in-system jump would let go of its own
destination as it set out for it.

**Two words that never swap** (plan conflict 7, extended): **the course** is
where the jump folds to: a star, chosen at the chart, or -- since ruling 1 --
the target itself, chosen with the map's `Jump here` (decision 12). **The
target** is the world in this system that the pilot has marked, chosen at the
map. A star course and the target are independent: either can be set without
the other, and neither clears the other, except that a star jump's fold
opening clears the target. An in-system course is never independent: it *is*
the target, and changing or clearing the target lets it go. The HUD's private
`ETarget` enum, which names what the crosshair dot is over, is left alone;
the new types are named `UShipTargetOverlay`, `FTargetView` and `FTargetMark`,
so no existing name changes.

### 6. The HUD says where the target is, precisely enough to aim with, and when the ship will get there

*Revised by ruling 3: a live time to arrival, where this spec recommended none.*

A new readout under the jump line in the top-right corner (`Margin + 40`),
`Dim`, 10 pt, the same as the jump line (`JumpLine`, after the flight-feel
spec's rename of `DriveLine`):

```
› Kessa II · 12° to port, 3.0° up · 0.214 AU · PASSING 0.031 AU UP
› Kessa II · 0.1° to starboard · 1,496 THOUSAND KM · ETA 65 S · NIGHT SIDE
› Kessa II · dead ahead · 38,000 KM · ETA 50 S
› Kessa II · 40° to port · 0.214 AU
```

- `›` is the chart's `PlottedMark`: the one you chose.
- The **name** is the designation, or the given name followed by the
  designation (`Halden · Kessa II`) for an inhabited world.
- The **bearing** is from the ship's nose, in ship axes, not from the
  free-looking head. It is a new `NavText::TargetBearing(ShipLocalDir,
  AheadRadians)`, in the jump's words, with two differences:
  - **`dead ahead` means the nose is on the world**: off-boresight no more
    than `AheadRadians = max(angular radius, TargetMarker::AheadFloor)`,
    with the floor 0.25 degrees. At 0.03 AU an Earth's angular radius is
    0.08 degrees, so the floor governs: 0.25 degrees there is 19,600 km, 3
    Earth radii, and the world is already inside the bracket's 28-unit
    minimum. Close in, the disc itself governs: a world filling 30 degrees
    of sky is dead ahead anywhere on its face.
  - **Degrees to a tenth under 10 degrees**, whole above: `3.0° up`, `0.4° to
    starboard`, `12° to port`. The jump's whole degrees suit a cone of 8; they
    cannot aim at a disc of 0.16.
  The jump line keeps `NavText::Bearing` and its cone, unchanged. The jump
  aligns with a star within 8 degrees; flying onto a world is a different
  tolerance, and one threshold for both was the first draft's mistake.
- The **distance** is to the surface, worded by `UShipHUDWidget::
  AltitudeWords`, the altitude line's own measure and words. When the target
  is the nearest world the two lines agree to the digit.
- **`NIGHT SIDE`** is appended when less than 0.15 of the world's visible
  disc is lit, `(1 + cos alpha) / 2 < 0.15`, a phase angle past about 134
  degrees: *it is there, and you are looking at its dark side*.

The line is composed once, by `TargetMarker::Line(const FTargetView&)`, and the
HUD and the map print that one string (held equal in
`DeepSpace.Ship.ScreensAgree`). It shows for anyone aboard, seated or not,
like the jump line. With no target, or a target that does not resolve here,
it is empty rather than a dash: the corner does not grow a placeholder for
something the player never asked for.

**Rejected: the jump's cone as the `dead ahead` threshold**, the first draft,
chosen so that "the pilot reads one set of words for both levers". It is the
failure the developer described: 8 degrees off from 0.03 AU is a hundred
Earth radii of miss, labelled `dead ahead`.

**The time to arrival** (ruling 3: "Live ETA, i don't recall ruling out a
countdown in this regard. This would be good."). The line ends, before
`NIGHT SIDE`, with one of two things, or with neither:

- **`ETA <time>`** when a target resolves, the ship is not in transit, it
  moves at 1 m/s or more, and **its velocity's ray meets the target's floor
  sphere** -- `ShipFlight::RayToFloor`, the flight-feel spec's own test of
  whether the cap will bring the ship down on a surface. Under the drive the
  velocity is along the nose, so the ETA's appearing *is* the answer to "will
  the drive bring me down on it?", the words the flight-feel spec asked this
  one for (its *Seams*).
- **`PASSING <altitude> UP`** when the ship is closing on the target on a path
  that misses: the closest approach of the velocity's ray, less the radius, in
  `AltitudeWords`. A time for a path that does not arrive would be a time to
  nowhere, and it would jump the moment the nose came onto the world.
- Neither when the ship is at rest, opening on the target, or in transit.

**The value is the flight state's own law, at the current speed**:
`ShipFlight::SecondsToFloor(D, Speed, BrakingAccel, HoldSeconds)` (the
flight-feel spec's Track 0), with D the distance along the ray to the
target's floor sphere, the braking the boosters have, and `ds.Drive.HoldSeconds`.
The ship is taken to hold its present speed until the cap binds, then to
follow the cap to the braking knee, then to brake. Once the lever has settled
it agrees with the flown approach to 0.11 s over the median leg at 1 c (the
flight-feel spec's decision 5 simulation), so it counts down a second a
second, and it changes the moment the lever or the aim does. While the ship
is still spooling up it overstates, because the speed it is computed at is
still rising: "at the current speed" is what the developer asked for, and it
is honest about the present.

**Words**, `NavText::Duration(Seconds)`: whole seconds under 100 s (`ETA 52
S`), whole minutes under an hour (`ETA 12 MIN`), hours to a tenth under two
days (`ETA 4.2 H`), whole days above (`ETA 3 D`, which is the drive's
lowest notch, 1 km/s, across 260 thousand km). The days have no ceiling and
no grouping: cruise's 200 m/s across 0.2 AU is `ETA 1731 D`, and a 1 m/s
creep at the same world about `ETA 346000 D` (see *Open questions*, "The ETA's long
end"). It ticks each second only in the last hundred seconds of an
approach, and is calm before. The map prints the same line (`ScreensAgree`).

**What it does not do.** It is never the jump's: the charge still has no
number, bar or countdown (CLAUDE.md, scoped back to the charge by this
ruling), and an engaged in-system jump shows the jump's words, not a time to
the fold. It counts down an approach the player chose, at a speed they can
change at any moment, and nothing happens when it reaches zero but the ship
arriving where the player sent it.

**Rejected: no time anywhere** (this spec's recommendation, sign-off 3a,
argued here at length before the ruling). It drew the line between units of
time and units of distance: a distance is a fact about where the world is,
while a time to go is the approach turned into a readout of how much of it is
left, a function of the lever and so a score for the setting. The developer
ruled that the rule it leaned on, CLAUDE.md's "no percentage, bar or
countdown", was about the jump's charge, and that a live ETA "would be good".
The charge is a wait before anything can happen; an approach is something the
player is doing. **Rejected: coarse, static words for the lever setting**
(sign-off 3b: `at 10 C: about a minute`, set when the lever or the target
changed and never recomputed). It was the fallback that told the choice
without ticking; the ruling asked for live. **Rejected: distance over speed.**
Under the cap the speed is the distance over four seconds, so it would read a
steady `4 S` through the whole last minute. **Rejected: an ETA for any
heading, from the closing speed.** It would jump discontinuously when the nose
came onto the world and the cap took over, and it would promise an arrival on
a path that passes.

**Rejected: a `closing` / `opening` word.** It is a fact rather than a clock,
and harmless. But the prograde mark (decision 7) shows it better, and it would
be a third word on a line that should stay short. (Since the ruling, the ETA
and `PASSING` say more than it would have, and only while closing.)

### 7. The bracket, the caret and the prograde mark: HUD space, from geometry, seen through the glass

**Drawn in HUD space, not in the world.** A new C++ widget,
`UShipTargetOverlay`, is a child of the HUD's canvas, built in
`UShipHUDWidget::BuildLayout` and placed every frame from `NativeTick`, which
hands it the `FStarSystem` it has already asked for (`Here`). It paints in
`NativePaint`, with lines and no texture. What it draws, and where, is decided
by the pure `TargetMarker::Place` (`UI/TargetMarker.h`), tested headless.

**Where:** the target's direction from the ship's origin, in ship axes, which
are world axes (ADR 0005), laid out from the camera at 100 km exactly as the
nose caret is (`CaretDistance`), then projected with
`UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition`. The sky's proxy
is scaled about the ship's origin and sits at least 50 km out, so the eye's
17.6 m offset in the hull moves it under half a pixel from this direction
(`FSkyViewParams::NearProxy`). The bracket lands on the drawn disc.

**Size, legible at every scale:**

- **A point or a few pixels** (every world for most of an approach): four
  corner ticks round a square of `ds.HUD.TargetMinPixels`, 28 slate units, so
  a sub-pixel world, and four black pixels on the night side, get a mark you
  can find across the glass.
- **A disc**: the square grows to the disc's projected diameter plus 6 units
  a side. The disc's pixel radius is found by projecting a second point, the
  centre offset along the camera's right by `tan(angular radius)` at the same
  depth, so perspective off the view's centre is accounted for.
- **A disc filling the view**: once the square would be wider than 80% of the
  view's shorter side, the bracket is not drawn. The world is its own
  marker, and a bracket round the whole glass says nothing. The HUD line
  still names it.

**Off the view: an edge chevron, for the pilot only.** When the direction
projects outside the view, or is behind the camera (where the projection
fails, and the direction is taken in view space instead), a small `Accent`
chevron sits `ds.HUD.TargetEdgeInset` (48 units) inside the view's edge,
pointing toward the target. The nose caret hides off-view, because *a caret
at the edge would claim the nose is there*. A chevron is a different shape
from the bracket and makes no claim about where the target is, only which way
to look. It is drawn for the pilot alone. The pilot is the one turning a head
and a ship toward it, and a chevron following a walker around the galley would
be noise.

**The nose caret shows whenever there is something to aim at.**
`UShipHUDWidget::ShowsNoseCaret` becomes `ShowsNoseCaret(Ship, Viewer, Here)`:
the viewer is the pilot, the ship is not in transit, and *either* a course is
plotted *or* the target resolves in `Here`. `PlaceNoseCaret` moves after
`NativeTick`'s one `GetSystemAt`, so it is handed the system already asked
for. The caret is still the ship's nose and nothing else: aligned, it sits in
the target's bracket exactly as it sits on the course marker. Without this
the pilot has a bracket showing where the world is relative to their head,
and nothing showing where the ship points.

**The prograde mark shows where the ship is going.** The nose is not the
direction of travel. In cruise the ship has inertia, so after a turn it slides
until the boosters catch up. Under the drive the velocity is always along the
nose (the flight-feel spec's revision 2, which dropped the closing-only cut
this paragraph first answered), so there the mark sits on the caret; they part
in cruise's slide after a turn, and in the moment after a drive toggle. So
while a target resolves, the pilot also sees an ink ring with three short ticks (top,
left, right: the flight-sim convention, and a different shape from the caret's
plain ring and the bracket's corners). It is placed along the ship's velocity,
taken into ship axes (`Orientation.Inverse() x GetVelocity()`), projected from
the camera like the caret, and hidden under 1 m/s and off the view. Bracket
on caret means *pointed at it*. Bracket on prograde means *going to it*. It
is shown only with a target because its question is "will I get to what I
marked". Whether it should show always is a flight readout, the flight-feel
spec's call: *Decisions needing sign-off*, item 6. **Ruled (ruling 5): with a
target, for the pilot.**

**Through the glass, never through a wall.** The HUD is drawn over everything,
so a bracket drawn from direction alone would sit on the cockpit's back wall
when the pilot looked round, and on the bunk's ceiling for someone lying
down. So the bracket is drawn only when `TargetMarker::SeenThroughGlass(World,
Eye, Direction, Viewer)` holds: a `Visibility` trace from the eye, along the
direction, for 30 m (ignoring the viewer), that hits nothing or hits an actor
tagged **`Sky.Glass`** first. Glass boxes get that tag in `build_hauler.py`,
and `verify_level.py` holds every `glass` box to it. With that rule the bracket
is shown to **anyone who can see the target**: the pilot, someone standing at
the fore glass, someone in the galley whose window happens to face it. Below
the sill, behind the desk, or above the window head, it is hidden, exactly as
the world itself is, and the HUD line still gives the bearing. The dressing's
clutter blocks only `ECC_Camera`, so a mug never hides the bracket. The caret,
the prograde mark and the chevron are the pilot's aiming aids and are not
occluded: the helm looks out of the glass by construction.

**Night side:** the bracket is drawn from geometry and never from brightness,
so a black disc is bracketed like a lit one, and the HUD line adds `NIGHT
SIDE` (decision 6). No synthetic limb or glow is drawn on the world. The sky
is honest about the dark, and the bracket is what makes the dark findable.

**Colour and shape:** `Accent` corner ticks. The course marker is a teal
*point*, the nose caret an ink *ring*, the prograde mark an ink *ring with
ticks*, so the target's teal *corners* read as "something you chose", and as
a different thing from any of them.

**Rejected: an in-world marker**, a component on the counter-frame like the
course marker. It would be occluded by the hull for free. But it cannot be
held to a minimum pixel size and grow with a disc without being re-sized
against the pixel angle every frame, it cannot be an edge chevron at all, and
it needs a new material through the sky contract. The trace buys the
occlusion for one line of code.

**Rejected: pilot-only, like the caret**, with no trace. That would be
simpler. But the developer found the need standing at the glass as much as
sitting at the helm, and the trace makes "wherever it can be seen" safe.
*Decisions needing sign-off*, item 4. **Ruled (ruling 5): anyone who can see
the target through the glass.**

### 8. The .03 AU question: what a test can show, and a way to look

**Headless: `DeepSpace.Sky.NightSideIsDrawn`**, in the sky's test world
(`SkyTestWorld.h`, spawned before `BeginPlay`). It checks the leading
hypothesis's premise, not visibility. One fixture, an `FStarSystem` with a Sun
and an Earth at 1 AU, is the only source: its `FSkySystem` is derived through
`LocalSystem::Here`, never built by hand, so the two cannot disagree. The ship
is placed with `PlaceShip` 0.03 AU outside the Earth on the sun-planet line,
offset sideways by 0.002 AU, so that the Earth is 3.7 degrees off the sun's
centre and seen at a 176-degree phase angle. The sky is drawn through
`AShipSky::DrawFrom`, with the view's pixel angle set to 103 degrees over
3840 px. It asserts:

- the Earth's proxy exists, is visible, and is not hidden in game;
- its drawn angular radius is its true one (`PointBlend` 0: resolved), at 4
  px or more across at that pixel angle;
- its projected direction is within a pixel of the true one, and it lies
  inside the depth band;
- its `Phase` is under 0.02 and the brightness it writes is under 2% of what
  it writes at full phase from the same distance;
- the star is within 5 degrees of it.

In words: *the proxy is drawn, the right size, in the right place, next to the
sun, and dark.* Then, with no subsystem and no `SetTarget` (the fixture has no
procgen id for the subsystem to accept), it calls `TargetMarker::View` directly
on the same `FStarSystem`, for `FBodyId{Fixture.Stub.Id, 0}`, and asserts that
the view's direction and angular radius are the proxy's to a pixel, and that
`TargetMarker::Line` ends `NIGHT SIDE`. It does not claim anything reaches the
screen: under `-nullrhi` nothing does.

**Pure, to show the numbers:** `Tools/sky_probe.py --night` prints, for the
same geometry, the pixels, the phase angle, the Lambert phase and the
separation from the star, beside the existing approach table. It is pure
Python, runs in a second, and makes the 4.3 px and 0.015 above something
anyone can recompute.

**By eye, which is the only way to settle visibility:** `ds.Sky.Goto <body>
<km> night` places the ship on the world's anti-sun side, facing it
(`ShipSky::GotoPlacement` gains the side). Then `ds.Nav.Target <body>`
brackets it. **There is no real-render probe of the sky in this repo's
guards**: every automated test runs under `-nullrhi`. So whether four black
pixels are *visible* is a question for the developer's eyes, and this is the
one-line way to ask it.

### 9. What this leaves for landing, and what landing must not assume

- The target is an `FBodyId`, held until changed or until the fold opens,
  including at the floor. Landing may read it, as "the world the pilot
  means", without asking for anything new.
- **Nothing in flight reads the target**: not the throttle, not the drive's
  lever, and not the soft cap. That is ruling C, and it is enforced by the
  target living in `FShipNavState`, which the flight state never sees. What
  the cap *does* read is the flight-feel spec's decision (its decision 5 and
  sign-off 2), and this spec takes no position on it beyond the
  recommendation below.
- Landing should **not** require a target. A pilot skimming a world looking
  for somewhere worth landing (vision: *approach takes time*) has not
  necessarily marked it. Whether landing reads the nearest surface or the
  target is landing's decision. This spec's recommendation is the nearest
  surface, with the target used only to name the landing on the HUD.
  *Decisions needing sign-off*, item 5. **Ruled (ruling 5): landing works
  from the nearest surface and uses the target only to name it.**
- The port desk screen stays free, and it is directly ahead of the helm. If
  landing needs a screen at the helm, that is the one.

**A recommendation to the flight-feel spec, recorded here because the map
makes it matter.** *Adopted*: the flight-feel spec's revision 2 made it its
rule (its decision 5), and the developer ruled that rule as specified
(ruling 2). The paragraph is kept for its reasoning; its numbers are that
spec's first draft's, at 10 c. Ruling B says the cap acts "only when the ship would hit a
surface within a few seconds": that is time to impact along the velocity. The
flight-feel spec's decision 5 is already much nearer that than today's drive.
It takes no tangential speed, and it binds only within N = 4 s. But it caps
the component of velocity toward the *nearest* surface whether or not the
ship's path meets it. A pilot crossing the system toward a marked world, who
passes a nearer one, would be braked by a world they are not heading into. At
10 c (3 million km/s) on a line that misses a world by 50,000 km, at
1 million km out the radial part is capped to room / 4 s, 250,000 km/s,
while the tangential part is about 150,000 km/s. The ship slows about tenfold
for a pass that would never touch. The map is what will send pilots on those
lines. The recommendation: bind the cap only when the velocity ray meets the
body's surface-plus-floor sphere within N seconds. The closing-only cut and the
crawl to the floor stay as that spec has them. This is the flight-feel spec's
call, not this one's.

### 10. Two screens, two jobs, one ship

*Revised by rulings 1 and 4: the map can send the jump to its target, and
the chart chair zooms either screen.*

The chart and the map sit side by side on one desk and are not two stations.

| | The chart (starboard) | The map (middle) |
|---|---|---|
| Question | where next, among the stars | where things are, here |
| Chooses | the course, a star (`FSystemId`) | the target, a world (`FBodyId`), and a jump to it (decision 12) |
| Used | once a jump, from the chart chair, zoomed | many times a system: glanced at and clicked from the helm or the chart chair, zoomed from the chart chair |
| Marker | teal point on the dome, nose caret | teal corners on the world, nose caret, prograde mark, edge chevron |
| HUD | the jump line | the target line, with its ETA |

Neither needs a second person, and nothing on one is better with somebody at
the other (vision: *shared presence, not division of labour*). A solo pilot
does both jobs, and a friend in the chart chair is company who can also see
the map. The map does **not** show a star course's direction, and the chart
does not show the system's worlds. Each answers one question completely, and
a screen that half-answers the other's question invites checking both.

**Where the two jobs meet: the in-system jump.** The map plots it, because it
is a world and the map is where worlds are; the chart shows it as the
course, with its engage toggle, because it is the jump (decision 12). It is
one course and one lever seen from two screens, not two jumps, and neither
screen needs the other to use it.

### 11. Legibility from the helm is a gate before the map merges

Decision 3's sizes come from arithmetic, and arithmetic cannot say whether
text is comfortable. So before the map's track merges, a temporary render
check, **`Eyes.MapFromHelm`**, is judged by eye. It is modelled on the
flight-feel spec's `Eyes.StarGlare`: named outside `DeepSpace.` so
`./test.sh` never runs it, run once through the lock without `-nullrhi`, and
deleted in the commit that records the verdict. It spawns an `AShipMapScreen`
at the layout's position in `SkyTestWorld::FSkyWorld` before `BeginPlay`,
places the ship in two systems (the start system with a world targeted, and
the most crowded system among the first 2,000 stubs of the test seed), and
captures from `PilotEye` toward the panel's centre. The capture is 30 degrees
across at 800 px, the helm's 26.7 px a degree on 4K, `SCS_FinalColorLDR`,
written to `Saved/Eyes/MapFromHelm/`.

The orchestrator judges the frames: every row and the target line read, and
no ring shimmers into its neighbour. If they fail, the knobs in order are the
text size constants, then the port screen (decision 1). If a widget component
turns out not to draw into a scene capture in a test world, the check cannot
be made that way, and the orchestrator surfaces it to the developer as the
one question for their first playtest rather than merging on the arithmetic
alone.

**Not followed: making this the developer's playtest before merging.** On this
project the developer approves at the design and delivery then runs to done
(the approval workflow). A merge that waited on a playtest would put a check-in
in the middle of delivery. A render the orchestrator judges keeps the gate
inside delivery. The developer's playtest remains the last word, as it is for
everything.

**Staged, by the build order.** The map widget lands in stage 1, before the
target exists (stage 3), so the check runs twice: in stage 1 on the rows,
rings and title, which is the gate for merging the map; and once more in
stage 3, before the target track merges, with a world targeted, to judge the
target ring, the band's two lines with an ETA, and the `Jump here` button.
The test file is deleted with the second verdict.

### 12. The in-system jump: the same fold, to a world

*New, from ruling 1: "anything faster should be a jump ... there can be an
option to 'jump to planet' within a system".*

**What it is.** With a world targeted, the jump can be sent to it. It is the
existing jump, not a second one: the same three levers (course, heading,
engage), the same charge wound at the same rate by the same watts, the same
cone, the fold opening by itself with no confirm, the same six-second
transit, and the same arrival by translation, at rest with both levers at
STOP (the flight-feel spec's decision 4). Two things differ: the course names
a world rather than a star, and the arrival is a standoff above that world.

**Why it exists.** At 1 c a system is large. The median leg from an arrival
to the innermost world is 100 s, but 1 AU is 8 min 19 s, the 95th-percentile
leg 21 minutes, 30 AU 4 h 9 min, and the longest leg procgen makes 19 hours
(the flight-feel spec's decision 3). The drive tops out where the developer
put it; the long leg is the jump's.

**Sending it: `Jump here`, on the map.** With a target that resolves, the
map's band shows one button (decision 3's layout). `Jump here` plots the
target as the course *and* engages the jump, in one press. Pressed again it
reads `Stand down`, and clears the course, which stands the jump down, as
clearing does on the chart. One press does both because the in-system jump is
chosen where the map is used, from the helm, and the chart that engages is
out of the helm's reach (decision 2). A plot that still needed the chart
would send the pilot out of the helm, into the other chair and back, for
every hop. It is still the one engage lever the chart's toggle moves, and the
chart shows it. Console: `ds.Nav.Plot target` plots the target as the
course, and `ds.Nav.Engage` engages, as they do for a star.

**One course.** `FShipNavState` holds a course that is a star *or* a world:
`TOptional<FSystemId> Plotted` stays for a star, beside a new
`TOptional<FBodyId> PlottedWorld`, and at most one is set. `Plot(FSystemId)`
clears the world; `PlotWorld(FBodyId)` clears the star. The latest choice
wins, as a second row clicked on the chart does. `GetPlotted()` is unchanged,
so every reader of a star course is unchanged; `GetPlottedWorld()` and
`HasCourse()` are new. The ship still stores no universe data beyond ids
(ADR 0003's clause, decision 5).

**An in-system course is always the target.** `PlotWorld` is refused unless
the id is the target, and changing or clearing the target lets an in-system
course go (and so stands it down). The bracket and the jump can never name
different worlds.

**Refused when near.** `PlotWorld` is refused, and the button is disabled
with the words `Near enough to fly`, while the ship is within twice the
world's standoff of its centre (`NavStart::WorldReachFactor` = 2, about
730,000 km for an Earth). An in-system course that the ship flies inside that
distance is let go, as if it had arrived, with the charge unspent. Inside its
standoff the fold would carry the ship backward, and from twice it the drive
at 1 c is there in a little over a minute.

**Alignment.** The same cone, `ds.Nav.ConeDeg`, round the direction from the
ship to the world's centre, which `UShipSubsystem::GetCourseDirection` now
returns for an in-system course. So the HUD's jump line, the nose caret and
the counter-frame's teal course point follow the world with no change of
their own: the point sits inside the target's bracket, which is right,
because the world is both.

**The arrival.** `NavStart::WorldArrivalPoint(From, Planet, FloorCm,
StandoffDeg)`: on the line from the departure point to the world's centre, at
`WorldStandoffCm = max(R / sin(StandoffDeg / 2), R + 10 x Floor)` from the
centre, with **`ds.Nav.WorldStandoffDeg` = 2**: the world is met as a disc 2
degrees across, 53 px on 4K, within the cone of the nose, lit as its phase
is. It is the interstellar arrival's rule applied to a world: on the line, so
the world is where the nose was and nothing turns; a standoff chosen for
what the arrival looks like rather than a distance; and *to a world, never
onto it*, so the approach is still the player's. Two degrees is 57 radii:
365,000 km from an Earth's centre, 4.0 million km from a Jupiter's, always
far outside the floor the flight-feel spec defines (its decision 6: 10.2 km
and 112 km), which the `10 x Floor` term guarantees for a body of any size.
From there the drive at 1 c reaches an Earth's floor in **64 s** and a
Jupiter's in **79 s**; at 0.1 c, 67 s and 187 s (the flight-feel spec's
decision 5 table). If the point falls inside any other body's floor sphere
(a moon, once procgen makes them), it moves out along the line until it does
not.

**Through `FShipNavState` and the subsystem.**

- `Step` decides as now: the fold opens when engaged, with a course, charged
  and aligned. On `TransitBegan` it clears the target **only for a star
  course**: an in-system jump keeps its target (decision 5). On arrival it
  clears the course and engage and bumps the serial for both. For a star it
  records `LastArrival` and marks it visited, as now, and returns `Arrived`;
  for a world it does neither, and returns a new **`ENavEvent::ArrivedAtWorld`**.
- `UShipSubsystem` acts on `ArrivedAtWorld` with
  `FlightState.JumpTo(NavStart::WorldArrivalPoint(...))`, as it acts on
  `Arrived` with `ArrivalPoint`. `TransitBegan` is one case for both: the
  charge spent, attitude released, both levers to STOP; and `JumpTo` zeroes
  the velocity (the flight-feel spec's decision 4).
- **The charge is one charge.** An in-system jump spends it, and whatever
  jump follows winds from empty. That is the only limit on jumps this build
  has; see *Open questions*.

**What the HUD and the map say.**

- The jump line: `JUMP WINDING · Kessa II · 12° to port`, `JUMP READY ·
  Kessa II · dead ahead`: `NavText::Jump` with the world's name, in the
  jump's own cone words, since it is the jump's cone. In the fold it reads
  `IN THE FOLD`, where a star jump reads `BETWEEN STARS`, which an in-system
  fold is not (`NavText::Jump(EJumpState, bool bInSystem)`).
- The target line is empty in the fold, as for any transit, and the bracket
  is hidden with the sky.
- The map: the target's row carries `›` as ever; the band's button reads
  `Stand down` while the course is the target; through the fold the ship's
  glyph stays drawn where it left from and the footer says `In the fold.`
  (the cache key's "between stars" is false for this fold, decision 3).
- On arrival the target line reads the world at two degrees, with no ETA:
  the ship is at rest, and the first thing the pilot does is set a lever.

**What it does to the interstellar chart.** The chart still lists and plots
only stars, and it is where the in-system course shows as the jump's course:
its course line reads `› Kessa II · in this system · 12° to port`, none of its
star rows carries the mark, and its toggle engages or stands down the
in-system jump as it would a star's. Plotting a star there replaces an
in-system course, as `Jump here` replaces a star course. Its `FAskedAt` keys
on the course, star or world. The chart's rows, range, bearings, visited
marks and everything else about stars are unchanged, and an in-system
arrival marks nothing visited.

**Rejected: plot on the map, engage on the chart**, the interstellar jump's
split. For a world chosen at the helm, every hop would be a trip to the other
chair and back. **Rejected: the chart lists the target as a row to plot.** A
second place to plot the same thing, on the screen the helm cannot reach.
**Rejected: an in-system jump with its own charge, cone or transit.** The
ruling names the existing machinery, and a second set of rules is a second
set to learn. **Rejected: arriving at the opening shot's framing, 18 degrees
across** (40,000 km over an Earth). The world fills the glass on arrival and
the floor is 54 s away: the approach, which the vision says is the content,
is mostly skipped. **Rejected: 0.5 degrees across.** 70 s to an Earth's
floor, but 119 s to a Jupiter's, and a world of 13 pixels: an arrival that
looks like not having arrived. **Rejected: a fixed distance.** A giant would
be met filling the view and a small world as a point. **Rejected: arriving
at the floor, or close enough to land.** It would be autopilot for the one
part the player is meant to fly (ruling C). **Rejected: the star as an
in-system destination.** The star cannot be targeted (decision 4), and the
interstellar arrival is already a standoff from it.

**Cost to change:** low. The standoff is a CVar and the reach a constant; the
course is one more optional in `FShipNavState`; the words are two.

### 13. The chart chair: sit, then choose the chart or the map; Tab on the zoomed map

*New, from ruling 4.* Verbatim: "Both, key cycles targets when the map is
focussed (an option from sitting in either seat (i.e., copilot doesn't auto
lock to jump menu, they can choose either jump menu or map and that will zoom
the screen. pilot just has look and click control of map. When map is zoomed,
(Tab) switches between planets in the system."

**The helm is decision 2, unchanged.** The pilot looks at the map and clicks
it, live, never sitting down at it and never zooming it. Tab does nothing at
the helm.

**The chart chair**, the copilot seat (the starboard `pilot_seat`, cockpit
(175, 270), which is the chart's use transform), **no longer locks onto the
chart.** Today E at the chart sits the player in its chair *and* frames the
chart, the mouse becoming a cursor. Now it sits them, and stops there:

- **Seated, not zoomed:** the view is the player's, within the seated limits
  (100 degrees of yaw, 70 of pitch), as at the helm, and the pointer is gated
  as at the helm (decision 2), so the map can be clicked by looking at it.
- **E zooms the screen the view is on.** On the chart or the map, E frames it
  with the existing fitted framing (`AShipScreen::GetViewTransform` and
  `FitFieldOfView` for that screen, `ds.Screen.FrameMargin` clear): the mouse
  becomes a cursor over it, and the body stays in the chair. The prompt names
  what E will do: `Chart`, `Map`. On neither, E stands up (`Stand up`), as
  today.
- **Zoomed, E goes back to the seat** (`Back`), unzoomed. Choosing is one
  press, switching screens is two and a glance, and leaving is two.
- **Zoomed on the map, Tab cycles the target** (below).

The chart's framing is exactly the one it has now. The map's is its own:
`place_map_screen` sets its `ViewDistanceCm` to 60, the chart's, and the
camera moves square-on to the map, 70 cm to port of the chair, and back. Both
moves mark a camera cut, as sitting does. Magnified from 60 cm the map's 600
x 424 is soft, never aliased (decision 1).

**How the classes say it.** `AShipScreen` gains two virtuals beside decision
2's, each a class decision no placed instance can change: `ZoomsOnSit()`
(true by default, so the laptop still frames when you sit at it -- it has one
screen and its own reason for a sit-down; `AShipNavScreen` says false) and
`IsZoomableFromChartChair()` (the chart and the map say true). The pawn keeps
`UsedScreen` (whose chair the body is in) and gains `ZoomedScreen` (what is
framed, or nothing), a `TWeakObjectPtr`, not a `UPROPERTY`, so
`BP_DeepSpaceCharacter`'s layout does not change; every framing function
reads the second. Which screen E zooms is found by the pointer gate's own
trace (decision 2). Standing up returns the body exactly as now
(`GetUseFloorZ`, the standing-spot search).

**Tab cycles the target while the map is zoomed.** `IA_CycleTarget` on Tab,
`Started`, built by `setup_flight_input.py` and clash-checked like the others
(Tab is free in `IMC_Default`); the flight-feel spec's Track A builds it with
its own actions so the Blueprint is recompiled once. The pawn acts on it only
while its `ZoomedScreen` is an `AShipMapScreen`, calling
`UShipSubsystem::CycleTarget()`, which asks the pure `ShipNav::NextTarget`:
the next world outward from the target, wrapping from the outermost to the
innermost, the innermost when there is none, nothing in a system with no
worlds, and (when procgen makes them) moons after their planet. It never
clears. It is not pilot-gated, like every pick. Cycling away from the world
an in-system course is set to lets that course go (decision 12), which the
button, turning back to `Jump here`, shows at once.

**Rejected: zoomed, E stands up** (today's one press to leave). Then the only
way to the other screen is to stand up and sit down again, which is the
lock-on the ruling removes. **Rejected: a click on an unzoomed screen zooms
it.** On the map a click is a pick; it cannot mean both. **Rejected: zooming
by looking alone** (a dwell). A view that changes because the head rested
there is the game acting without the player. **Rejected: Tab at the helm.**
"pilot just has look and click control of map." **Rejected: Shift+Tab to
cycle back.** Shift is the lever at the helm, and a dozen worlds go round in a
few presses; it is an addition if wanted. **Rejected: a second, larger map
widget for the copilot.** It is the same panel, framed: one map, one ship.

**Cost to change:** low. Which press does what is a few lines in the pawn;
the virtuals are one line each.

## Open questions

### The jump cooldown (future; not built)

The developer: "we should eventually limit the amount of jumps before a
cooldown period". Nothing in this build limits jumps beyond the one charge
every jump spends and winds again (decision 12). The question is recorded
here because its answer pulls against the vision's anti-chore principle, and
whoever designs it should start from that tension rather than find it:

- **A cooldown is a wait the game imposes.** Shown, it is exactly what the
  no-countdown rule still forbids -- the rule this amendment scoped *to* the
  jump's charge, a bar the player must wait out before anything can happen.
  Not shown, it is a jump that silently will not open, which reads as broken.
- **A limit counted in jumps is a budget**, and a budget spent in-system is an
  interstellar jump the player cannot make. That is the game telling the
  player they are *behind*, the principle's own test, and at worst it
  strands them.
- **The charge is already a limit that never blocks.** Every jump costs a
  wind-up, and nothing is counted. Any cooldown should first be asked whether
  it is anything more than a longer charge.

Constraints any design should meet: the interstellar jump is never
unavailable for long enough to strand the player; no countdown and no bar
for it; no confirm; nothing that reads as falling behind. Shapes worth
evaluating then: a charge that winds slower after back-to-back jumps and
recovers while the ship cruises (the limit felt as the wind-up, never
counted); a limit on in-system jumps only; a wear cost carried by a later
wear model. **Cost of deferring:** none. `FShipNavState` has room for any of
them, and nothing built here assumes jumps are unlimited.

### The ETA's long end (for the developer; not tuned)

`Duration` prints whole days with no upper bound and no digit grouping, so a
slow approach across a system reads `ETA 1731 D` (cruise's 200 m/s across
0.2 AU) or about `ETA 346000 D` (a 1 m/s creep), beside a distance worded as
`THOUSAND KM`. It is honest, and the developer has not seen it. Whether a
four-to-six digit day count reads as information or as a clock telling the
player they are *behind* is the anti-chore test, and the developer's call.
Shapes worth weighing: leave it; group the digits (`1,731 D`); past some
number of days drop the time and let the distance stand, as `PASSING` does
for a miss. `DeepSpace.UI.TargetMarker.Eta` pins the present words, so any
change is deliberate.

### Two bearings for one world (for the stage 3 and 4 playtests)

With an in-system jump plotted to the target, the jump line reads the
8-degree cone (`JUMP READY · Kessa II · dead ahead`) while the target line
reads the tight floor (`› Kessa II · 5.0° to port`). Decision 12 sanctions
it: the cone is where the fold will open, the floor is where the world is.
But it is the same shape of confusion the developer reported ("heading
straight to it"). Watch for it in play. One option, needing an amendment: on
a world course the jump line says `in the cone` rather than `dead ahead`.

### An engaged jump whose course changes kind (for the developer; not ruled)

Plotting a course never touches whether the jump is engaged, so replacing a
course keeps an engaged jump engaged. Star over star it always has; the
in-system jump adds star over world and world over star. The case that
matters: the pilot presses `Jump here` at the helm, and the copilot, at the
chart, presses a star row. The result is an engaged interstellar jump that,
charged, opens by itself as soon as the nose passes within 8 degrees of that
star -- and the fold takes the target with it. That is a way to leave the
system nobody chose as such, the opposite of the jump being deliberate. Two
answers, both small: **(a)** a course that changes kind stands the jump down
(`FShipNavState::Plot` and `PlotWorld` clear `bEngaged` when the other kind
was plotted), so the player who plotted the new course engages it; or **(b)**
keep it engaged, as star over star is, and record here that one engage lever
serves whichever course is plotted. Until it is ruled the build does (b), and
`DeepSpace.Ship.InSystemJump` and `DeepSpace.UI.ChartInSystemCourse` pin it,
so either answer is a deliberate change to those two lines.

## The anti-chore audit

- **Nothing on the map or the HUD says the player is behind.** No count of
  worlds seen or unseen, no "visited" marks on worlds (unlike the chart's
  systems; a world list with ticks becomes a list to finish), no ranking, no
  suggested target, no auto-target on arrival.
- **One clock, and it is the player's.** The live ETA (decision 6, ruling 3)
  counts down an approach the player chose, at a speed they can change at any
  moment, and nothing happens at zero but the arrival they asked for. It is
  never the jump's: the charge is still a word, with no number, bar or
  countdown. Nothing on the map or the HUD shows a lever as a fraction to
  fill.
- **Nothing is demanded.** The map does nothing on its own, and the target
  never expires except when the ship leaves the system, which the player
  chose.
- **The in-system jump is an offer, not an obligation.** Every world can be
  flown to at 1 c; the jump shortens the long legs. Nothing suggests it,
  counts its use or scores a flight against it, and it arrives two degrees out
  and at rest, so the approach is still the player's (decision 12).
- **The seats are not stations.** The chart chair zooms either screen, the
  helm clicks the map, and either player can do everything alone (decision
  13; vision: shared presence, never division of labour).
- **The cooldown is where this audit will be tested next.** A future limit
  on jumps is a wait imposed on the player; *Open questions* records the
  constraints it must meet before it is built.
- **Precision is not a chore.** Tenths of a degree are an aid to aiming,
  offered only when there is something to aim at. Nothing scores alignment
  or rewards holding it, and `dead ahead` arrives as soon as the nose is on
  the world.
- **Drift check:** this spec does not mention throughput, efficiency or spawn
  rates.

## Deliberate fakes, and what they cost later

- **Frozen orbits.** The map's cache is keyed on no time. When worlds move,
  the key gains the time the positions were asked for, and the dots move.
  This costs one field.
- **No moons.** The shape exists in `FBodyId` and in the map's table.
  Procgen adds them, and the map draws them as decision 3 says.
- **The map is drawn for a 4K display at the helm.** Its 600 x 424 matches the
  developer's display and the helm's seat. On a 1440p display it is minified
  about 1.5 times, as the first draft was on 4K. *Cost:* the draw size becomes
  a function of the viewport the day anyone plays on another display.
- **The trace uses the level's glass tag.** A ship built without
  `build_hauler.py` has no tagged glass, and shows no bracket. That is a safe
  failure, and `verify_level.py` catches it.

## Implementation outline

### New files

| File | What |
|---|---|
| `Source/DeepSpace/Ship/ShipMapScreen.h/.cpp` | `AShipMapScreen : AShipScreen`: `PanelWidthCm` 68, `DrawSizePixels` 600 x 424, bezel 1 cm, `bUsable = false`, `SetWidgetClass(USystemMapWidget)` (stage 1); `IsDrivableSeated()` and `IsZoomableFromChartChair()` overridden true (stage 4, once `AShipScreen` has them). No `Reach` box and no interactable: nothing sits you down here; the chart chair zooms it (decision 13). |
| `Source/DeepSpace/UI/SystemMapLayout.h/.cpp` | `namespace SystemMap`, pure: `MinRingGap(FMapPixels)` (derived, decision 3), `FMapScale Fit(const FStarSystem&, double StandoffAU, FMapPixels)`, the knots and the two-pass warp; `FVector2D Place(const FMapScale&, const FUniversePosition&)`; `FMapShip Ship(scale, position, orientation)` (glyph centre, nose angle or none, pinned inside or beyond, elevation in degrees); `FMapLayout Layout(const FStarSystem&, scale)` (rings, dots with their capped sizes, numerals, the target ring's size rule); `TOptional<int32> Pick(const FMapLayout&, FVector2D, float MaxRadius)`. |
| `Source/DeepSpace/UI/SystemMapWidget.h/.cpp` | `USystemMapWidget : UShipScreenWidget`. `BuildScreen` (the title, a `USystemMapView`, the row `UButton`s, the band), `RefreshFromShip()` public, `SelectWorld(int32)`, `FAskedAt` keyed as decision 3 says, with `GetLayoutAsked()` for tests, and getters for each row's text and for the target line, like the chart's. |
| `Source/DeepSpace/UI/SystemMapView.h/.cpp` | `USystemMapView : UUserWidget`: paints the orrery from an `FMapLayout` and the ship's `FMapShip`; `NativeOnMouseButtonDown` asks `SystemMap::Pick` and calls back `SelectWorld`; `NativeIsInteractable()` true. |
| `Source/DeepSpace/UI/TargetMarker.h/.cpp` | `namespace TargetMarker`, pure except `SeenThroughGlass`: `AheadFloor` (0.25 degrees); `TOptional<FTargetView> View(const FStarSystem&, const FBodyId&, const FUniversePosition&, const FQuat&, const FVector& Velocity, double FloorCm, double BrakingAccel, double HoldSeconds)` (ship-local direction, centre and surface distance, angular radius, `AheadRadians`, lit fraction, night side, name, and the ETA or the passing altitude, decision 6); `FString Line(const FTargetView&)`, which ends with `ETA ...` or `PASSING ... UP` as decision 6 says; `FVector ProgradeShipLocal(const FVector& Velocity, const FQuat&)`; `FTargetMark Place(bool bProjected, FVector2D Centre, float RadiusPx, FVector ViewSpaceDir, FVector2D ViewSize, float MinPx, float Inset, bool bPilot, bool bSeenThroughGlass)`; `bool SeenThroughGlass(const UWorld*, FVector Eye, FVector Dir, const AActor* Viewer)`, the one world query. |
| `Source/DeepSpace/Ship/ShipTags.h` | `namespace ShipTags`: `Glass` (`Sky.Glass`), the C++ side of `placement.py`'s `GLASS_TAG`, in its own header so the level scripts (stage 1) and `TargetMarker::SeenThroughGlass` (stage 2) share it. |
| `Source/DeepSpace/UI/ShipTargetOverlay.h/.cpp` | `UShipTargetOverlay : UUserWidget`, hit-test invisible: `PlaceFor(const UShipSubsystem&, const TOptional<FStarSystem>& Here)` projects the target and the prograde direction, asks `SeenThroughGlass` and `TargetMarker::Place`; static `ShowsTargetMark(Ship, Viewer, Here)` and `ShowsPrograde(Ship, Viewer, Here)`; `NativePaint` draws the corners, the chevron or the prograde mark; `GetLastMark()` for tests. |
| `Source/DeepSpace/Tests/SystemMapLayoutTest.cpp` | `DeepSpace.UI.SystemMap.Scale`, `.Warp`, `.TwelveWorldsFit`, `.ShipOnTheWarp`, `.Pick` |
| `Source/DeepSpace/Tests/SystemMapScreenTest.cpp` | `DeepSpace.UI.SystemMapScreen`, `DeepSpace.Ship.MapScreen` |
| `Source/DeepSpace/Tests/ShipTargetTest.cpp` | `DeepSpace.Ship.Target` |
| `Source/DeepSpace/Tests/TargetMarkerTest.cpp` | `DeepSpace.UI.TargetMarker.View`, `.Bearing`, `.Place`, `.Eta`, `DeepSpace.Ship.TargetSeenThroughGlass` (stage 2) |
| `Source/DeepSpace/Tests/TargetOverlayTest.cpp` | `DeepSpace.UI.TargetOverlay` (stage 4: moved out of `TargetMarkerTest.cpp`, which is stage 2's) |
| `Source/DeepSpace/Tests/InSystemJumpTest.cpp` | `DeepSpace.Ship.InSystemJump`, `DeepSpace.Loop.InSystemJump`, `DeepSpace.UI.NavigationScreen.InSystemCourse` (stage 3; its own file, because `SliceLoopTest.cpp` and `NavScreenTest.cpp` are other tracks' in stages 3 and 4) |
| `Source/DeepSpace/Tests/ChartChairTest.cpp` | `DeepSpace.Ship.ChartChair` (stage 4) |
| `Source/DeepSpace/Tests/NightSideTest.cpp` | `DeepSpace.Sky.NightSideIsDrawn` |
| `Source/DeepSpace/Tests/MapFromHelmTest.cpp` | `DeepSpace.Ship.MapFromHelm` |
| `Source/DeepSpace/Tests/Eyes/MapFromHelmEyesTest.cpp` | `Eyes.MapFromHelm`, temporary (decision 11), deleted with its verdict |

### Changed files

| File | Change |
|---|---|
| `Ship/ShipNavState.h/.cpp` | `TOptional<FBodyId> Target`; `SetTarget`, `ClearTarget`, `GetTarget` (false in transit); `TOptional<FBodyId> PlottedWorld`, `PlotWorld` (only the target), `GetPlottedWorld`, `HasCourse`; `Step` clears the target on `TransitBegan` for a star course only, and returns `ArrivedAtWorld` for a world course; `ShipNav::TargetPlanet`, `ShipNav::NextTarget`. The header comment's "no percentage, no bar and no countdown anywhere" is scoped to the charge, as CLAUDE.md now is. |
| `Ship/ShipSubsystem.h/.cpp` | `SetTarget` (checks the system here and the orbit), `ClearTarget`, `GetTarget`, `CycleTarget`; `PlotTarget` and the in-system course (refused inside `WorldReachFactor` standoffs, let go when flown inside them, let go when the target changes); `GetCourseDirection` for a world; `ArrivedAtWorld` handled with `NavStart::WorldArrivalPoint`; `static float GetStandoffAU()` (for the map's rim, as `GetChartRangeLy` is for the chart); `ds.Nav.Target` (with `next`), `ds.Nav.Plot target`, `ds.Nav.WorldStandoffDeg`. |
| `Ship/NavStart.h/.cpp` | `WorldStandoffCm(Radius, FloorCm, StandoffDeg)`, `WorldArrivalPoint(From, Planet, FloorCm, StandoffDeg)`, `WorldReachFactor` = 2 (decision 12). |
| `UI/NavigationWidget.h/.cpp` | the course line for an in-system course (`› Kessa II · in this system · 12° to port`), no row marked; `FAskedAt` keyed on the course, star or world (decision 12). |
| `Universe/UniverseSubsystem.h/.cpp` | (stage 1, with the map, whose cache key needs it) `TOptional<FSystemId> GetSystemIdAt(const FUniversePosition&) const`: the stub search without the generation. |
| `Ship/ShipScreen.h` | `virtual bool IsDrivableSeated() const`, `virtual bool ZoomsOnSit() const`, `virtual bool IsZoomableFromChartChair() const` (decisions 2, 13). Not reflected, so no Blueprint's saved layout changes; still a header change, so `./rebuild.sh --force`, then `check_blueprints.py` as routine. |
| `Ship/ShipNavScreen.h/.cpp` | `ZoomsOnSit()` false and `IsZoomableFromChartChair()` true: sitting at the chart no longer frames it (decision 13). |
| `Player/DeepSpaceCharacter.h/.cpp` | `UpdatePointer`'s seated branch (decision 2), from either chair; `ZoomedScreen` (a `TWeakObjectPtr`, not reflected), E's seat, zoom and back, and their prompts (decision 13); `CycleTarget()`'s body, whose binding the flight-feel spec's Track A builds. No new component and no new `UPROPERTY` in stage 4, so `BP_DeepSpaceCharacter` is not invalidated by it. |
| `UI/ShipHUDWidget.h/.cpp` | the overlay child and the target readout in `BuildLayout`; `NativeTick` places both, and the caret, from the `Here` it already asks for; `ShowsNoseCaret` gains `Here` and the target clause; the jump line names a world course and says `IN THE FOLD` for an in-system fold (decision 12). No renames, and no word functions move. |
| `UI/NavText.h/.cpp` | `WorldKind(EPlanetKind)`, `WorldName(const FPlanet&)` (stage 1, for the map's rows); `TargetBearing(ShipLocalDir, AheadRadians)`, `Duration(Seconds)`, `Jump(EJumpState, bool bInSystem)` (stage 2). `Bearing` is unchanged. |
| `Sky/ShipSky.h/.cpp` | `ds.Sky.Goto`'s optional `night`; `ShipSky::GotoPlacement` takes the side. |
| `Tools/hauler_layout.py` | `MAP_SCREEN`, `MAP_SCREEN_WIDTH`, `PILOT_EYE`; `Ship` gains `map_screen_location`, `map_screen_yaw`. The dressing needs no exclude: `cockpit_desk` exports only its wings, and the middle of the desk is not a surface. |
| `Tools/build_hauler.py` | `place_map_screen` (`hauler_map_screen`, with `view_distance_cm` 60, the chart's, for the chart chair's zoom); the `Sky.Glass` tag on `glass` boxes (`GLASS_TAG` in `placement.py`, mirrored in C++ as `ShipTags::Glass`, held equal by `test_placement.py` as the dressing's tags are). |
| `Tools/verify_level.py` | `check_map_screen` (class, label, yaw, position against the layout; the only `AShipMapScreen`); every glass box tagged. |
| `Tools/validate_hauler.py` | `check_map_sightline`: the air from the helm's eye to the map's glass is empty, sampled every 10 cm, as `check_chart` is for the chart chair. `check_helm_glass`: from `PILOT_EYE`, a ray along the nose leaves the hull through a `glass` box before any other, and a ray aft meets a non-glass box first. |
| `Tools/test_placement.py` | the map is in the cockpit facing aft, on the centre line, 1 cm proud of the middle desk screen and level with it, and its width is the one the C++ draws (it reads `ShipMapScreen.cpp`); `PILOT_EYE` equals `SkyTestWorld::PilotEye` (it reads `SkyTestWorld.h`). |
| `Tools/sky_probe.py` | `--night` (stage 4: the flight-feel spec's Track A owns the file in stage 2). |
| `Tools/setup_flight_input.py` | `IA_CycleTarget` (Boolean) on Tab, built in stage 2 by the flight-feel spec's Track A with its lever actions, so the Blueprint is recompiled once. |
| `CLAUDE.md` | a *The system map* section after *The chart chair*: the screen and its draw size, the helm pointer rule, the chart chair's choice and Tab, the in-system jump, the live ETA, the course/target words, the two bearings, the glass tag; the tunables table; `ds.Nav.Target` in *Playtest console*. |
| `docs/decisions/0003-*.md` | the amendment's one clause (decision 5). |

### CVars and commands

| Name | Default | Lives in |
|---|---|---|
| `ds.HUD.TargetMinPixels` | 28 (slate units) | `ShipTargetOverlay.cpp` |
| `ds.HUD.TargetEdgeInset` | 48 (slate units) | `ShipTargetOverlay.cpp` |
| `ds.Nav.Target [n\|name\|next\|none]` | command | `ShipSubsystem.cpp` |
| `ds.Nav.Plot target` | command, extended: the target as the course (decision 12) | `ShipSubsystem.cpp` |
| `ds.Nav.WorldStandoffDeg` | 2 (the world's angular diameter at an in-system arrival) | `ShipSubsystem.cpp` |
| `ds.Sky.Goto <body> <km> [night]` | command, extended | `ShipSky.cpp` |

Everything else is a named constant with a test on it: `SystemMap::MinRingGap`
(derived, 8 px), `PickRadius` (14 px), the rim margin (1.25), the draw size
(600 x 424), `TargetMarker::AheadFloor` (0.25 degrees), the 10-degree switch
from tenths to whole degrees, the 80% hide, the 0.15 night-side fraction, the
1 m/s prograde threshold, the 30 m glass trace, `NavStart::WorldReachFactor`
(2), the `10 x Floor` standoff guard, and `NavText::Duration`'s boundaries
(100 s, an hour, two days).

### Tests

Pure, no world:

- **`DeepSpace.UI.SystemMap.Scale`**: the innermost orbit sits on its log
  radius; the arrival standoff maps inside the rim; the map is monotonic in
  radius; azimuth is exact; the same system gives the same scale wherever the
  ship is.
- **`.Warp`**: a pair of orbits 8% apart is drawn `MinRingGap` apart; a system
  with no tight pair is drawn as the pure log map, unwarped; twelve orbits
  crowded against the rim are pulled inward by the second pass and keep every
  gap.
- **`.TwelveWorldsFit`**: `MinRingGap` x (`MaxPlanets` + 1) fits between the
  star's edge and the rim; `MaxPlanets` worlds at the corpus's tightest
  spacing, at either end of the log range, keep every ring at least
  `MinRingGap` from its neighbour and the outermost at least `MinRingGap`
  inside the rim; every dot and target ring is within its capped size,
  against a local gap the test works out from the rings itself; neighbours in
  line never touch; the arrival is drawn clear of the outermost ring by the
  ship glyph's radius, and a ship coming in from it moves inward on the map at
  every step to the outermost orbit.
- **`.ShipOnTheWarp`**: a ship at a world's position is drawn on its dot; a
  ship at an orbit's radius is drawn on its ring; **a ship between the star's
  surface and `r_in` is drawn just outside the star's disc and reports it is
  inside the innermost orbit**; beyond the rim it is pinned and says so; 2 AU
  over the pole it is drawn 2 AU out and reports 90 degrees above the plane.
- **`.Pick`**: a click on a dot picks it; two dots 9 px apart split at the
  midpoint (4 px from A picks A, 5 px picks B), and an exact tie picks the
  inner; a click 15 px from every dot picks nothing; a click on the star picks
  nothing, even with a ring within 14 px.
- **`DeepSpace.Ship.Target`** (on `FShipNavState` and `UShipSubsystem`): set,
  clear, set again, which replaces; refused in transit; refused for another
  system's world and for an orbit the system lacks; cleared by a star
  jump's fold opening and **kept through an in-system jump's**; kept through
  arrival at the floor, standing up, drive toggles, all stop and a replotted
  star course; `TargetPlanet` resolves nothing after a `PlaceShip` into
  another system. `NextTarget`: from none to the innermost, outward one at a
  time, the outermost wraps to the innermost, nothing in a system with no
  worlds, and it never returns none from a set target in a system with
  worlds; `CycleTarget` lands on the same `SetTarget` a click does.
- **`DeepSpace.UI.TargetMarker.View`**: direction and distances against hand
  arithmetic; lit fraction at 0, 90 and 180 degrees; `NIGHT SIDE` exactly past
  the threshold; `AheadRadians` is the floor for a small far world and the
  angular radius for a near one.
- **`.Bearing`**: **a target 3 degrees off the nose does not read `dead
  ahead`** (`3.0° to port`); 0.2 degrees off a world of 0.08 degrees' angular
  radius reads `dead ahead`; 0.4 degrees reads `0.4° to port`; 12.4 degrees
  reads `12° to port`; a disc of 5 degrees' angular radius, 3 degrees off,
  reads `dead ahead`; `NavText::Bearing` with the jump's cone is unchanged
  (the existing `NavTextTest` stands).
- **`.Place`**: a sub-pixel world gets the minimum bracket, centred; a disc
  gets a bracket its diameter plus padding; past 80% of the short side there
  is no bracket; off the view, and behind the camera, the pilot gets a chevron
  on the inset edge pointing the right way, and a walker gets nothing;
  occluded, there is no bracket. These are the pure function's cases, fed
  their inputs directly; nothing here claims a HUD placed them.
- **`.Eta`** (ruling 3): an ETA exactly when the velocity's ray meets the
  target's floor sphere at 1 m/s or more, and its value is
  `ShipFlight::SecondsToFloor` of that distance and speed; `PASSING` exactly
  when closing on a path that misses, at the ray's closest approach less the
  radius; neither at rest, when opening, or in transit; **the ETA falls by one
  second a second, to within 0.5 s, along a settled approach stepped through
  `FShipFlightState` at 1 c from 0.2 AU** (the flight state's own law, not a
  copy of it); `Duration` at 99 S, 2 MIN, 59 MIN, 1.0 H, 47.9 H, 2 D.

In a world:

- **`DeepSpace.Sky.NightSideIsDrawn`**: decision 8.
- **`DeepSpace.UI.TargetOverlay`**, the projection-free halves only, as
  `DeepSpace.UI.NoseCaret` does (a headless world has no viewport to project
  into): the built HUD has the overlay on its canvas, built hidden;
  `ShowsTargetMark` and `ShowsPrograde` for the pilot and a walker, in transit,
  with an unresolved target, and under 1 m/s; the overlay's world points are
  the target's direction and the velocity's from the camera; with no camera
  it stays hidden whatever the ship says. `DeepSpace.UI.NoseCaret` gains: with
  a target and no course the caret shows for the pilot; with neither it does
  not.
- **`DeepSpace.Ship.TargetSeenThroughGlass`**, the glass trace, against a
  spawned fixture, because the level cannot be loaded (see *Context*): a box
  tagged `Sky.Glass` ahead of `PilotEye` and an untagged wall box aft, both on
  `Visibility`, and a clutter-like box that blocks only `ECC_Camera`. Forward
  through the glass is seen; aft into the wall is not; through the clutter box
  is seen; through an untagged box ahead is not; the viewer's own pawn never
  blocks. That the real hull's glass is where this assumes is held by
  `validate_hauler.py`'s `check_helm_glass` on the layout, and by
  `verify_level.py`'s tag check on the built level.
- **`DeepSpace.UI.SystemMapScreen`**: the placed map's widget is a
  `USystemMapWidget`; one row per world; `SelectWorld` targets and a second
  call clears; a target set from the console shows on the map untold; the
  layout is asked once while nothing changes, **not again when the ship
  moves 100 AU within the system**, and again the frame the system id, the
  priors or `ds.Nav.StandoffAU` changes (`GetLayoutAsked`, as
  `ChartAsksOnChange` does); between stars it shows `Between stars.`, and
  **in an in-system fold it keeps the system and says `In the fold.`**; the
  band's button reads `Jump here` with a target, `Stand down` while the course
  is the target, is disabled with `Near enough to fly` inside the reach, and
  is absent without a target; pressing `Jump here` plots the target and
  engages, pressing `Stand down` clears the course. (The target cases arrive
  in stage 3, with the target.)
- **`DeepSpace.Ship.MapScreen`**: spawned before `BeginPlay` (it builds its
  collision there); not usable; its draw size is 600 x 424; drivable seated
  and zoomable from the chart chair; the chart is zoomable but not drivable
  seated, and does not zoom on sit; the laptop zooms on sit and is neither;
  the engineering console is none of them.
- **`DeepSpace.Ship.MapFromHelm`**: a seated pilot looking at the map has an
  active pointer whose own last hit is the map's widget component; looking at
  the chart, the pointer is inactive; the left button is released on looking
  away. As with `ScreenPointer`, it checks up to the surface, since the Slate
  hit-test grid is empty under `-nullrhi`.
- **`DeepSpace.Ship.ScreenReachable`** gains the map, traced from the helm's
  eye.
- **`DeepSpace.Ship.ScreensAgree`** gains the target line: the HUD's readout
  and the map's are one string.
- **`DeepSpace.Loop.Jump`** gains: a target picked before the jump is gone on
  arrival, and one can be picked in the new system. (In stage 3 this case is
  written in `InSystemJumpTest.cpp`, beside the in-system loop, because
  `SliceLoopTest.cpp` is the flight-feel spec's Track B's that stage.)
- **`DeepSpace.Ship.InSystemJump`** (decision 12, on `FShipNavState`,
  `NavStart` and `UShipSubsystem`): `PlotWorld` refused unless the id is the
  target, and inside `WorldReachFactor` standoffs; a star course and a world
  course replace each other; changing, cycling or clearing the target lets
  an in-system course go; the fold opens only engaged, charged and within the
  cone of the world's direction; on `TransitBegan` the charge is spent, both
  levers go to STOP and the target is kept; on `ArrivedAtWorld` the ship is
  on the line from its departure to the world's centre at `WorldStandoffCm`
  to a centimetre, the world is within the cone of the nose, the velocity is
  exactly zero, the course and engage are cleared, the serial is bumped, and
  `LastArrival` and the visited set are untouched; the arrival is outside
  every floor sphere, for an Earth, a Jupiter and a body of 100 km radius
  (where `10 x Floor` governs); a course the ship flies inside the reach is
  let go with the charge unspent; `GetCourseDirection` points at the world.
- **`DeepSpace.Loop.InSystemJump`**: from the opening shot, target the
  outermost world through the map widget's seam, press `Jump here`, fill the
  charge with `ds.Nav.Charge`, turn onto the world, and the fold opens by
  itself; the jump line says `IN THE FOLD` in transit; the ship arrives at
  rest, two degrees from the world, with the target still set; then the
  drive lever set from STOP to 1 c reaches the world's floor within 85 s
  (64 s for an Earth and 79 s for a Jupiter at that standoff).
- **`DeepSpace.UI.NavigationScreen.InSystemCourse`**: the chart's course line
  names an in-system course with `in this system`, no row carries the mark,
  its toggle stands the jump down and engages it again, and plotting a star
  row replaces the in-system course.
- **`DeepSpace.Ship.ChartChair`** (decision 13): E at the chart sits the
  player unzoomed, with the view free within the seated limits; E looking at
  the chart zooms it with the chart's fitted field of view, and E again
  returns unzoomed; E looking at the map zooms the map with its own fitted
  field of view, the body unmoved; E looking at neither stands up, onto the
  floor as `ScreenStandUp` holds; the laptop still zooms on sit; the prompt
  reads `Chart`, `Map`, `Back` and `Stand up` in their states; Tab zoomed on
  the map cycles I, II, ... and wraps; Tab zoomed on the chart, unzoomed, or
  at the helm changes nothing. Every screen spawned before `BeginPlay`.

Python: `test_placement.py` and `validate_hauler.py` as above;
`verify_level.py` after the level rebuild. Temporary: `Eyes.MapFromHelm`
(decision 11), run in stage 1 and again in stage 3.

## Build tracks

**Superseded by the build order**,
`docs/superpowers/plans/2026-09-26-poc2-build-order.md`, which stages both
specs' work together and was checked against every file's owner there. In
its terms: this spec's track 3 (the map widget) and track 6 (the level) are
stage 1, and track 3 also takes `NavText::WorldKind`/`WorldName` and
`UUniverseSubsystem::GetSystemIdAt`, which its rows and cache key need
before stage 2 and 3 exist; track 2 (words and marker arithmetic) is stage 2,
less its `ShipSky.*` and `sky_probe.py` edits, which move to stage 4; track 1
(target state), with the whole of decision 12, is stage 3, and it also wires
the stage-1 map widget to the target (its `SelectWorld`, the target ring and
the band); tracks 4 (the pointer, now with decision 13's seats) and 5 (the
HUD) are stage 4. The tracks below are kept as the reasoning the build order
started from.

Worktrees, with file ownership disjoint within this spec. The flight-feel
spec's tracks are named as it names them: **0** (its shared seams, landing
first and alone), **A** (`ShipFlightState.*`, `ShipSubsystem.*`,
`DeepSpaceCharacter.*`, `ShipHumComponent.cpp`, `setup_flight_input.py`,
`sky_probe.py`), **B** (`ShipHUDWidget.*`), **C** (`ShipCounterFrame.*`,
`SkyProjection.cpp`, `ShipSky.cpp`, its eyes test). Where both specs touch a
file, the order is stated. If this spec is approved and that one is not, the
orders reverse, and the conflicts named are all there are.

1. **Target state**: `ShipNavState.*`, `ShipSubsystem.*` (target, console,
   `GetStandoffAU`), `UniverseSubsystem.*` (`GetSystemIdAt`),
   `ShipTargetTest.cpp`, the ADR 0003 clause. Flight-feel A also edits
   `ShipSubsystem.*`, for the levers and the floor. Land after A: the target is
   about twenty lines there and touches none of A's.
2. **Words and marker arithmetic**: `NavText.*`, `TargetMarker.*`,
   `TargetMarkerTest.cpp`, `NightSideTest.cpp`, then `ShipSky.*` (Goto
   `night`) and `sky_probe.py --night`. `NavText` and `TargetMarker` depend
   only on `FBodyId`, which exists, and can start at once. The `ShipSky.*`
   edit lands after flight-feel C, which owns `ShipSky.cpp`, and the
   `sky_probe.py` edit after flight-feel A, which rewrites its drive law.
3. **The map**: `SystemMapLayout.*`, `SystemMapWidget.*`, `SystemMapView.*`,
   `ShipMapScreen.*`, `SystemMapLayoutTest.cpp`, `SystemMapScreenTest.cpp`,
   then `Eyes.MapFromHelm`. It builds against track 1's signatures (fixed by
   this spec) and merges after it, and only once decision 11's check has
   passed. No flight-feel file.
4. **The helm pointer**: `ShipScreen.h`, `DeepSpaceCharacter.cpp`,
   `MapFromHelmTest.cpp`. Flight-feel A edits `DeepSpaceCharacter.*` for the
   lever input and all stop. Different functions, but the same file: land
   after A.
5. **The HUD**: `ShipTargetOverlay.*`, `ShipHUDWidget.*`, the `ScreensAgree`,
   `TargetOverlay`, `NoseCaret` and `TargetSeenThroughGlass` tests.
   Flight-feel B rewrites the bottom-left corner and renames `DriveLine` to
   `JumpLine` in `ShipHUDWidget.*`: land after B, and build against its
   names. Depends on tracks 1 and 2.
6. **The level**: `hauler_layout.py`, `build_hauler.py`, `placement.py`,
   `verify_level.py`, `validate_hauler.py`, `test_placement.py`. No
   flight-feel file. The Python can go in parallel with everything, but the
   level rebuild runs once, through the lock, after tracks 3 and 4 have
   compiled. Then `verify_level.py`, then `check_blueprints.py` (nothing is
   removed, so it should pass untouched).

Tracks 3 and 6, and the pure half of track 2, can run alongside the
flight-feel spec's tracks from the start. The rest waits on the flight-feel
merges named above.

## Risks

- **Text from 1.6 m.** Decision 11 gates it. If the render check cannot be
  made, it becomes the first playtest's question.
- **The HUD's frame cost.** The overlay adds one line trace and three
  projections a frame. The system is the one `NativeTick` already generates,
  handed down rather than asked again. The map adds one stub search a frame
  (`GetSystemIdAt`), and no generation.
- **A tight system's labels.** The warp separates rings but not numerals at
  similar azimuths. Numerals are placed outside their dot, away from the
  star, and may overlap in the rare system where they collide. The list is
  the authority.
- **Two ring marks near the nose.** With the caret, the prograde mark and the
  bracket all on one small world, the centre of the view has three marks in
  it. Their shapes differ, and they only coincide when everything is right,
  which is the moment they can be read at once. If it reads as clutter, the
  prograde mark hides within `AheadRadians` of the caret.
- **A widget component in a scene capture.** Decision 11 assumes it draws.
  If it does not, see decision 11.
- **Tab and Slate's focus navigation.** While a screen is zoomed the input
  mode is game-and-UI, and Slate's default navigation config moves keyboard
  focus on Tab. If a focused widget takes Tab before Enhanced Input sees it,
  the cycle does nothing. `DeepSpace.Ship.ChartChair` drives the pawn's
  handler directly, so it cannot catch this; the playtest can. The fallback
  is to give the zoomed screen's input mode no focusable widget, or to turn
  off tab navigation in the viewport's navigation config while zoomed.
- **The in-system jump could make flying optional.** From anywhere to a
  world is a 45 s wind, a turn and a six-second fold. That is the ruling's
  intent for long legs, and the arrival two degrees out keeps the last minute
  the player's. If playtest shows nobody flies a leg any more, the knobs are
  `WorldReachFactor` and the charge, and the cooldown question (*Open
  questions*) is where the real answer lives.
- **The ETA while spooling up overstates.** Computed at the present speed, it
  starts high and falls faster than a second a second until the lever
  settles, about six seconds from STOP to 1 c. That is "at the current speed"
  taken literally; if it reads as wrong, the successor computes it at the
  lever's speed during the spool, one argument.

## Decisions needing sign-off -- all ruled, 2026-09-26

**The developer has ruled on every item** (the amendment at the top). Each
keeps its question as it was put, with the ruling after it.

1. **How the pilot picks a target from the helm.**
   - *(a) Look at the map and click* (recommended): the pointer is live at the
     helm for screens whose class says `IsDrivableFromHelm`, and only the map
     does.
   - *(b) A helm key cycles the target*, with the map showing the pick; no
     pointer at the helm.
   - *(c) Both.*
   - *(d) Walk to the map*: no helm pointer, and picking only standing.

   **Cost of changing later:** low. (b) is an input action in
   `setup_flight_input.py` plus a `CycleTarget` on the subsystem, and adds to
   (a) without undoing it. Going back from (a) is one branch in
   `UpdatePointer`.

   **Ruled (ruling 4): (c) both, with the key where the map is zoomed.** At
   the helm, look and click (a). In the chart chair the seated player
   chooses the chart or the map and the chosen screen zooms; while the map is
   zoomed, Tab cycles the target (decision 13).

2. **The map's radial scale.**
   - *(a) Warped log* (recommended): log radius with a derived minimum ring
     gap, and the ship through the same warp.
   - *(b) Ordinal*: rings evenly spaced by orbit index, always legible, no
     sense of the system's shape.
   - *(c) Zoomable linear*: honest, and it must be driven before it can be
     read.

   **Cost of changing later:** low. The scale is one pure namespace, and the
   widget draws whatever it returns. Only its tests change.

   **Ruled (ruling 5): (a), the warped log.**

3. **Time on a readout.** One ruling for this spec and the flight-feel spec
   (its item 5). The criterion either way is *no number in units of time that
   runs down*. Distances stay (decision 6).
   - *(a) No time anywhere* (recommended for this build): the flight-feel
     lever already brings every arrival to the floor in 55-190 s at the
     notches a player will use.
   - *(b) Coarse, static words for the lever setting*, on the map only:
     `at 10 C: about a minute`, from the flight state's own approach law,
     set when the lever or the target changes and never recomputed as the
     distance falls. It describes the choice and never ticks.
   - *(c) A live ETA* on the target line and the map.

   **Cost of changing later:** low for (b) (a pure `ApproachSeconds` beside
   the cap, and one clause of `TargetMarker::Line`), trivial for (c). It is
   on this list for the precedent, and because the developer's complaint was
   that approaches take too long: (b) is the honest answer if, under the new
   lever, they still cannot tell what a notch will cost them. (c) crosses the
   line CLAUDE.md draws, and once one countdown exists the rule no longer
   holds anywhere.

   **Ruled (ruling 3): (c), a live ETA**, on the target line and the map, at
   the current speed (decision 6). The developer: "i don't recall ruling out
   a countdown in this regard". The line CLAUDE.md drew was about the jump's
   charge, and it is scoped back to the charge there; that is the answer to
   "once one countdown exists the rule no longer holds anywhere": the rule
   holds where it was made.

4. **Who sees the bracket.**
   - *(a) Anyone, wherever the target can be seen through the glass*
     (recommended): an occlusion trace and a `Sky.Glass` tag; the edge chevron,
     caret and prograde mark for the pilot only.
   - *(b) The pilot only*, like the nose caret: no trace, no tag.
   - *(c) An in-world marker*, occluded for free, with no minimum size and no
     chevron.

   **Cost of changing later:** low for (a) and (b), which differ by a
   predicate. High for (c), which needs a new material through the sky
   contract.

   **Ruled (ruling 5): (a), anyone who can see the target through the glass.**

5. **Whether landing reads the target.** This spec constrains the next one.
   - *(a) Landing works from the nearest surface and uses the target only to
     name it* (recommended): a pilot can land on anything they have flown
     down to, marked or not.
   - *(b) Landing requires a target*: the pilot marks a world to land on it.
   - *(c) The target also shapes the soft cap*, so it holds for the marked
     world and not for one passed on the way.

   **Cost of changing later:** medium. (b) or (c) turns the target from a
   marker into an input to flight. That crosses ruling C's "the player still
   flies there themselves", and the rule that nothing in flight reads
   `FShipNavState`'s target is then given up once and has to be defended at
   every later feature. The problem (c) would solve, being braked by a world
   you are passing, is better solved without the target, by the cap binding
   only on a path that meets a surface (decision 9's recommendation to the
   flight-feel spec).

   **Ruled (ruling 5): (a), the nearest surface, the target only to name it.**

6. **When the prograde mark shows.**
   - *(a) With a target, for the pilot* (recommended, this spec): its question
     is "will I get to what I marked".
   - *(b) Always, for the pilot*, as a flight readout. That belongs to the
     flight-feel spec, which names a prograde tick as its successor if sliding
     under the cap confuses.
   - *(c) Never*: the flight-feel spec's dust streams along the true velocity
     and shows the direction without a marker.

   **Cost of changing later:** low. It is one predicate,
   `ShowsPrograde`. It is here because the two specs meet at it, and it should
   have one owner.

   **Ruled (ruling 5): (a), with a target, for the pilot.**

New with the rulings, and decided here rather than put back to the
developer, since each follows from a ruling and is cheap to change: the
in-system jump's standoff (2 degrees across) and reach (twice it), its one
`Jump here` press that plots and engages, the chart chair's E (zoom the
screen looked at; zoomed, back to the seat), and the ETA's `PASSING` for a
path that misses. Each carries its rejected alternatives in decisions 6, 12
and 13.

## Review record

**Amended in place with the developer's rulings, 2026-09-26** (the amendment
at the top): decisions 1, 2, 4, 5, 6, 7, 9 and 10 revised; decisions 12 (the
in-system jump) and 13 (the chart chair) and *Open questions* (the cooldown)
new; the sign-offs marked ruled; *Build tracks* superseded by the build
order. Superseded choices are kept as rejected alternatives.

Revised in place after a review on the day of drafting. Superseded choices are
kept above as rejected alternatives.

- **The sibling spec was said not to exist.** It does:
  `2026-09-26-flight-feel-design.md`, written the same day, a few minutes
  after this one, and untracked beside it. It owns the lever in both modes,
  the soft cap, the floor, note 2's motion cue (its decision 8: dust at a
  seen speed that grows with speed, replacing `ds.Sky.MoteFadeSpeed`'s fade)
  and note 4 (its decision 9). The fault the review found was real: this spec
  pointed at it by a description and not by its path. The header, the
  non-goals and the build tracks now name it and its tracks, and every claim
  here about the drive was checked against it.
- **Dead ahead and the missing nose.** Fixed: a target-specific threshold
  from the body's angular radius with a 0.25-degree floor, tenths of a degree
  under 10, the caret whenever a target resolves, and a prograde mark
  (decisions 6, 7).
- **The soft cap's input.** The claim that ruling B "keys on the nearest
  surface" is removed. This spec now says only that nothing in flight reads
  the target, and it records a separate recommendation to the flight-feel spec
  (decision 9).
- **The .03 AU account** is now four hypotheses with one leading, and the
  goal is what a headless test can show (Context, decision 8).
- **Tests.** The overlay and night-side tests no longer project or target
  through the subsystem. The glass trace has its own test. **Not followed:**
  testing the trace "in a loaded `L_Hauler` world, as `DressingHaulerTest`
  does". That test does not load the level, and no test can: the level is
  regenerated on main and never committed from a branch
  (`HaulerDressingMarkers.h`). The trace is tested against a spawned fixture,
  and the real glass's placement by `validate_hauler.py` and
  `verify_level.py`.
- **Legibility arithmetic.** Corrected, and answered at its cause by drawing
  at the seen resolution (decision 1). The gate is a render check the
  orchestrator judges, **not** the developer's playtest, because on this
  project delivery runs to done once the design is approved (decision 11).
- **The warp and hit areas.** The gap is derived, the overrun is clamped by an
  inward pass, and picking is by nearest dot (decisions 3, 4).
- **Time.** The criterion is restated, the inconsistency owned, and a
  non-countdown option added to the sign-off (decision 6, item 3).
- **Churn.** Dropped: no `ETarget` rename, and `AltitudeWords` stays where it
  is (decision 5, the changed-files table).
- **The custom interaction source.** Dropped (decision 2).
- **The cache key.** The ship's position is gone from it. **Partly not
  followed:** the key holds the system id, not the jump serial. The serial
  adds nothing once the id is asked every frame, and the id catches a
  `PlaceShip` into another system that the serial would miss (decision 3).
- **The per-instance flag.** Replaced by a class-level virtual, with a test
  (decision 2).
- **The ship inside `r_in`.** Pinned to the star's edge, with a footer line
  and a test (decision 3).
