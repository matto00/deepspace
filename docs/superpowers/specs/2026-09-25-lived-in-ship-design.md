# A ship that feels lived in

**Date:** 2026-09-25
**Status:** Draft, revised after review — awaiting sign-off (see *Review,
2026-09-25* at the end)
**Follows:** The interactable ship (implemented), flight and the counter-frame
(implemented)
**Governed by:** `docs/vision.md` — *the starter ship is worn*, *scale is only
felt in contrast*, the anti-chore principle; ADR 0004 (generated, validated),
ADR 0008 (shape the prior)

## Context

The hauler is sound. `validate_hauler.py` passes: sealed, one piece, every
region reachable in its posture, the crawlway crouch-only, 12 m of slide run,
doors and console clear. It is also plainly a box somebody validated. Every
room is lit the same neutral-cool white at the same brightness, from the same
grid of ceiling panels. There is not one object in the ship smaller than a
bench. It makes no sound at all. Nothing in it says anyone has ever been
here.

`docs/vision.md` requires the opposite: *the starter ship is worn — the player
has been living in it for some time before the game starts, and it should read
that way.* It also names the ship as the small, known thing that the scale of
the universe is felt against. A ship that reads as a test level is not small
and known. It is only small.

This is a **dressing pass**, meaning props, clutter, wear, light colour, sound,
and the small readouts that are lit because the ship is on. It is quick and
dirty on purpose. The developer wants to fly the ship somewhere and see
whether the idea holds up, and a ship that feels like a home is half of what
that test is testing. Anything here that could be done properly later is
faked now, and each fake is listed with what it will cost.

Two rules from the vision bind this harder than it looks:

- **Nothing here may accumulate.** Mess that builds up and needs tidying, a
  plant that needs watering, a readout that drifts, dust that settles while
  you are away: each of these is a rate the player has to keep up with. The
  dressing shows the ship's *history*. It never becomes a list of jobs.
- **Clutter is a snapshot of one person's life.** It comes from a world-level
  seed, so two players aboard see the same mugs in the same places (vision,
  *How social the game is*). Nothing is "yours" versus "theirs". Nothing is a
  station.

### What is ranked, and how

The brief is cheapest-first. Every proposal gets a rough felt-effect score out
of 5 and an estimate in hours. The numbers are kept from the first draft so
that the decisions below can refer to them, but the order they are *built* in
is the implementation outline's, not the table's.

| # | Proposal | Felt | Hours | Where | Editor? |
|---|---|---|---|---|---|
| 1 | Lighting moods: colour temperature and brightness per room, lamp panels to match | 5 | 2 | Python | build only |
| 2 | Practical lamps: reading lamp, under-cabinet strip, bench lamp, the only shadow casters | 4 | 1 | Python | build only |
| 3 | Surface clutter: mugs, books, tools and tins on every surface, placed by Poisson/Beta | 5 | 8 | Python | build only |
| 4 | Ship hum: a reactor drone whose timbre follows the engine's feed and whose hiss follows the boosters' push; a constant air hiss | 5 | 5 | C++ | yes |
| 5 | Kick band and material wear: a dark scuffed skirting on every wall, faded and replaced panels | 3 | 2 | Python | build only |
| 6 | Lamp panels dim with the lights, so a browned-out room no longer has glowing ceiling panels | 3 | 1.5 | C++ | yes |
| 7 | Wall dressing: handrails split around doors, notes, a jacket on a hook, cable runs | 3 | 3 | Python | build only |
| 8 | Floor-band clutter, and the pocket check it needs: crate stacks, boots by the bed, canisters | 2 | 3 | Python | build only |
| 9 | Readouts: five small lit panels: ship clock, reactor rating, speed, cabin, airlock | 3 | 3 | C++ | yes |
| 10 | Stretch goal, *desire lines*: worn floor where the traffic between regions actually goes | 2 | 3 | Python | build only |

**The hours were revised upward on review.** The first draft added up to
about 21 hours and still called that "a couple of sessions". It was also
optimistic in two particular places. Item 3 covers the RNG mirror, its
vectors, surfaces for eight templates, thirteen clutter templates, the
anchoring check and a corpus, and 4 hours was out by about a factor of two.
Item 4 is the repository's first audio code on Linux, and `USynthComponent`
has not been exercised here at all. The totals below include the editor
steps. They come to about three to four sessions to the second cut, not two.

**Build order: 1 and 2, then 4 and 6 together as the first C++ batch, then 3
and 5.** Items 7 to 10 wait behind a second cut.

- **First cut, after 1, 2, 4 and 6 (about 12 h).** The ship is lit warm, room
  by room. It hums, the hum answers the laptop and the throttle, and its
  ceiling panels dim when its lights brown out. Nothing has been added to
  the floor, so nothing can break reachability. This is the least that is
  worth flying.
- **Second cut, after 3 and 5 (about 11 h more).** There is clutter on every
  surface, scuffed skirting, and worn and replaced furniture. Surface clutter
  has no collision (decision 5), so this cut cannot make a pocket either.
- **Behind the second cut: 7, 8, 9 and 10.** Floor-band clutter is where the
  reachability risk lives, so it and the pocket check travel together. The
  readouts are the second C++ batch, and that costs a second restart.

**Rejected (first draft): one C++ batch for items 4, 6 and 9, built after the
Python items.** It saved one restart, and in exchange it put the hum behind
everything else. The draft's cut line promised "a warm, cluttered, humming
ship" after item 5, but its own order built the hum sixth. A pass cut short
would have produced a cluttered, silent ship with handrails, which is not
what the cut line said. The only item scored 5 that needs the editor would
have been the one dropped. A second restart costs about fifteen minutes. That
is cheaper than a silent ship.

## Decisions

### 1. Dressing is a generated layer, computed from the plan, never hand-placed

A new module, `Tools/dressing.py` (no `unreal` import), takes the resolved
plan and a seed and returns a list of `Place` entries plus some wall-run
`Box`es. `hauler_layout.generate()` appends these to `PLACEMENTS` before
`resolve_props` runs. From there the dressing uses the path everything
already uses: the same containment check, the same `prop_<name>_<n>_<k>`
labels, the same keep-clear check, the same builder and verifier. No new actor
kind appears in the level for clutter.

The seed is `DRESSING_SEED` in `hauler_layout.py`. When ships become seeded
it will be derived from the ship seed (ADR 0007). A different seed gives a
different life on the same floor plan. `LIVED_IN = 1.0`, also in the layout,
scales every expected count at once. It is the knob for poking holes: at
`0.3` the ship is freshly moved into, at `2.0` it is squalid.

**Rejected: placing clutter by hand in `PLACEMENTS`.** It would be faster for
the first twenty items and wrong for the reason ADR 0006 gives: a ship dressed
by hand is dressed only on the developer's machine. More to the point, hand
placement cannot answer the question this pass is for, which is whether a
*generated* ship can read as lived in. If the rules cannot produce that, the
game needs to find out now.

**Rejected: dressing in C++ now.** ADR 0006 says generation belongs in C++ at
startup, and this pass will eventually be ported there. But the Python runs in a
second, can be checked against a corpus of seeds, and needs no restart. The
rules will change a lot before they settle, and every change in C++ would cost
a restart. See *What is faked*.

### 2. Clutter lives on surfaces and against walls. The middle of a room is never a candidate

This is ADR 0008's first rule applied to clutter. A furniture template gains
**surfaces**, rectangles where things can rest. Room walls gain **bands**,
strips of floor or wall where things can lean. Dressing samples only inside
these. A mug in the middle of the corridor is not something the generator
rejects. It is something the generator has no way to express.

`props.py` gets the metadata ADR 0008 said it lacked:

```python
Surface = namedtuple("Surface", "name at size back use clear exclude",
                     defaults=("centre", 40, ()))
# at: centre of the resting plane, prop-local. size: (x, y) extent.
# back: "+x" or "-x", the edge things get pushed against.
# use: "centre", "+y", "-y" -- where the person using it sits or stands.
# clear: tallest item that fits (to the next shelf, the upper cabinets...).
# exclude: prop-local (lo, hi) rectangles nothing may cover (the workbench's
#          screen, the laptop).

SURFACES = {
    "galley_table": [Surface("top", (0, 0, 79), (90, 180), "-x", "centre")],
    "counter":      [Surface("top", (0, 0, 90), (60, 300), "-x", "+y", clear=58)],
    "desk":         [Surface("top", (0, 0, 77), (60, 120), "-x", "-y")],
    "workbench":    [Surface("top", (0, 0, 90), (80, 160), "-x", "centre",
                             exclude=(((-40, -27), (-34, 27)),))],
    "cockpit_desk": [Surface("wing_port", (-50, -155, 80), (100, 50), "+x", "centre"),
                     Surface("wing_stbd", (-50, 155, 80), (100, 50), "+x", "centre")],
    "wall_rack":    [Surface("shelf_%d" % n, (0, 0, z), (50, 180), "-x", "centre", clear=70)
                     for n, z in enumerate((77.5, 152.5, 227.5))],
    "locker":       [Surface("top", (0, 0, 200), (60, 60), "-x", "centre", clear=45)],
    "airlock_bench":[Surface("seat", (0, 0, 45), (40, 120), "-x", "-y", clear=30)],
}

ANCHOR = {  # ADR 0008's missing metadata; resolve_props now enforces it
    "overhead_panel": "ceiling", "counter": "wall", "conduit": "wall",
    "pipe_run": "wall", "wall_rack": "wall", "note": "wall", "photo": "wall",
    "jacket": "wall",   # everything else: "floor" or "surface"
}
```

`placement.resolve_props` gains the anchoring check that ADR 0008 found
missing. A `"ceiling"` prop must have a part touching the ceiling. A `"wall"`
prop must have a part touching a wall face. This catches, as a `PlanError`,
the floating counter and the ceiling panel lying on the floor that the probe
found.

Wall bands are derived and never listed. `dressing.wall_bands(plan, room,
depth)` walks each wall of a room and subtracts every door and seal opening
widened by `KEEP_CLEAR`, the console's keep-clear, and every existing prop
footprint grown by 20 cm. What is left is a set of 1-D segments along which a
floor or wall item may be placed.

Rooms declare what they accept in `ROOM_DRESSING` in `hauler_layout.py`:

| Room | Surfaces | Floor band | Wall band |
|---|---|---|---|
| corridor | — | **none** (slide run) | handrails, notes |
| crawlway | — | **none** | cable runs only, ≤ 3 cm deep |
| cockpit | desk wings, light | none | one note |
| cargo_bay | shelves | crates, canisters, coils | — |
| engineering | workbench | canisters, parts | cable runs |
| galley | table, counter | none | notes, photo |
| bunk | desk, locker top, bed | boots, one crate | jacket, notes, photos |
| airlock | bench seat | boots | — |

The crawlway is 150 cm tall and so is the crouched capsule. It is 90 cm wide
against a 68 cm capsule. Anything on its floor or ceiling, and anything
deeper than 11 cm on its walls, breaks it. So nothing goes there except flat
cable runs. The validator would catch a mistake, but the table makes the
mistake impossible to write.

**Rejected: rejection-sampling positions over the whole floor.** The probe
measured this at 53% acceptance before intent checks, and ADR 0008 exists
because the results that *passed* were still implausible.

### 3. Where things fall: Poisson for how many, Beta for where, categorical for what

Every draw says in a comment what it believes about the world (ADR 0008's
amendment). Concretely:

- **How many items on a surface: Poisson(λ × LIVED_IN, Max = 8), and the
  placement loop stops early when the surface is full.** Objects get left on a surface one at a time and
  independently, which is what Poisson counts. λ comes from the surface's
  affinity: counter 4, galley table 2.5, bunk desk 3, workbench 5, each shelf
  2, cockpit wing 0.8, locker top 0.5, airlock bench 0.7.
- **Where along the surface: Beta, anchored at the `use` end.** People leave
  things near where they sit and push them away from where they work. `use =
  "+y"`: distance from the +y end is Beta(2, 4). `use = "centre"`: position
  is Beta(3, 3), which clusters towards the middle and thins at the edges.
- **How far back: Beta(4, 2) towards the `back` edge.** Things end up pushed
  against the wall, not balanced on the front lip.
- **Along a floor band: Beta(1, 3) from the nearer end of the segment.**
  Things get pushed into corners. The count is Poisson(λ_room × segment
  length / 100 cm).
- **Stack height, for crates and books: geometric, p = 0.5, capped at 3,**
  drawn as repeated `chance(0.5)` rather than by a sampler of its own
  (decision 4). Each extra item on a stack is one more independent decision
  to put it there. The stacks use the existing `Place.level`.
- **What: a weighted categorical per surface.** For example, the galley
  counter is `{mug: 4, plate: 3, tin: 3, bottle: 2, tablet: 1}` and the
  workbench is `{part: 4, cable_coil: 3, toolbox: 3, canister: 1}`.
  Uniform over the whole catalogue is what put the reactor in the galley.
- **Colour, for fabric and books: categorical and skewed towards the ship's
  issue colours.** `{olive: 5, navy: 3, rust: 2, ochre: 1}`. Most of what a
  hauler carries is drab. The few bright things are the personal ones.
- **Wear, per furniture placement: Beta(5, 2).** Wear is a bounded proportion,
  and most things aboard are well used. It is quantised into three material
  buckets. w < 0.5 (about 11%) is **replaced**: a newer panel in a slightly
  different colour, because it was swapped out. w > 0.9 (about 11%) is
  **faded**. Everything else is standard. Weibull is deliberately *not* used
  here. Weibull describes wear *over time*, and the dressing is a static
  snapshot of how things stand when the game opens.

Collisions within a surface are the one place rejection stays, as a floor.
A candidate whose axis-aligned footprint overlaps an item already placed is
redrawn up to 8 times and then dropped. Positions are drawn inside `[half,
L - half]`, so an item cannot overhang its surface to begin with. Items
turn in 90° steps only, because `props.rotate` does, and books alternate 0°
and 90° on a stack so they do not look extruded.

### 4. The generator's randomness is `GenSeed` and `FGenStream`, mirrored literally in Python

`Tools/rng.py` is a line-for-line mirror of the procgen foundation's
`GenSeed` and `FGenStream`. It is not a library of its own. It has the same
names, the same algorithms and the same order of draws:

- `mix(v)`: SplitMix64. Add `0x9E3779B97F4A7C15`, then apply the finaliser.
- `label(text)`: FNV-1a 64 over the ASCII bytes.
- `derive(parent, purpose, index=0) = mix(parent ^ mix(purpose ^ mix(index)))`.
- `Stream(seed)`. `next_u64` adds the golden constant to the state and
  returns the finalised state, so a stream's first output is `mix(seed)`. On
  top of that are `unit` (the top 53 bits × 2⁻⁵³), `unit_open`, `chance`,
  `normal` (Box–Muller with no cached pair), `gamma` (Marsaglia–Tsang, with
  shape < 1 handled by the U^(1/a) boost and the cube written as `v*v*v`, not
  `pow`), `beta(a, b) = X / (X + Y)` from two gammas, `poisson(mean, max)`
  (Knuth), and `categorical(weights)`.

The procgen spec's comment calls `Mix` "SplitMix64's finaliser". Its pinned
`Mix(0) = 0xE220A8397B1DCDAF` only comes out if the golden-ratio increment is
part of `Mix`. All four of its pinned values were reproduced this way while
this spec was being revised, and that comment should be corrected to match.

Streams derive by label, exactly as procgen decision 2 does. A room's stream
is `derive(DRESSING_SEED, label("dress." + room))`, and a surface's is
`derive(room_stream, label(surface_name), n)`. Adding clutter to the bunk
does not reshuffle the galley. Stack height (decision 3) is not a sampler of
its own. It is `chance(0.5)` repeated up to the cap, a geometric count built
from something the stream already has. Dressing adds **no sampler that
`FGenStream` lacks.**

`Tools/rng_vectors.json` holds three sections:

- `seed`: procgen's four known values, verbatim (`Mix(0)`, `Mix(1)`,
  `Label("star")`, `Derive(1, Label("system"), 0)`).
- `stream`: the first 16 values of `next_u64` from seeds 0, 1 and
  `0xDEADBEEF`.
- `samplers`: 32 draws each of `unit`, `beta(2,4)`, `beta(3,3)`, `beta(4,2)`,
  `beta(1,3)`, `poisson(2.5, 12)` and `categorical([4,3,3,2,1])`, from
  `derive(1, label("vectors"))`.

Doubles are stored as `float.hex` and compared exactly. CPython's `math`
calls the same glibc libm that C++'s `<cmath>` does, so on this one platform
the two agree to the bit. That is procgen decision 11's one-platform
determinism and no more. `test_dressing.py` checks the file now.
`DeepSpace.Universe.Stream` gains one case that reads the same file, and from
then on neither side can drift without the other's test failing.

The procgen spec names its algorithms but does not pin their exact forms:
when `NextU64` increments, which bits `Unit` keeps, Box–Muller's `cos` branch,
and what `Poisson` does at `Max`. `rng.py`'s docstring pins them, and those
pins become `FGenStream`'s contract. **Whichever of the two lands second
conforms to the first.** This spec proposes that `Poisson` at `Max` redraws
up to 16 times and then clamps, the same rule as `LogNormalBounded`. That is
the procgen owner's decision to take.

**Dressing is the exception to procgen decision 10, and it is not the thing
decision 10 rejects.** Decision 10 rejects a Python *twin of a C++
generator*, one that gets tuned while the C++ drifts away from it. There is
no C++ dressing generator. Python holds the only one, so there is still
exactly one generator (ADR 0006's rule), written in the wrong language for
now and listed as a fake below. The part that is mirrored is the RNG
library. It has no priors to tune, and vectors that both sides check pin it,
so it cannot drift silently. When dressing is ported, `dressing.py` and
`rng.py` are deleted. They do not stay behind as a tuning twin.

**Rejected (first draft): an RNG of the dressing's own.** That draft derived
streams by FNV-1a-hashing each path element into the seed. It computed
integer-parameter Beta as the a-th order statistic of a + b − 1 uniforms,
and its `poisson` had no `Max`. Each piece was exact and easy to port. But a
second `Derive` and a second Beta beside `FGenStream`'s would be two
generators' worth of RNG, which ADR 0006 forbids. The draft's
`rng_vectors.json` would also have checked a port that nobody would ever
write, because the port would use `FGenStream`.

**Rejected: adding integer Beta to `FGenStream` as a named sampler.** Every
Beta used here has parameters that the Gamma route handles well. Two Betas in
one library invites someone to pick the wrong one by accident.

**Rejected: `random.Random(seed)`.** It is deterministic within CPython, but
it is a Mersenne Twister with Python-specific seeding, and C++ could never
reproduce it. ADR 0007 requires derivation by integer hashing, so that a seed
means the same ship everywhere.

### 5. Clutter is static and inert. Only what the capsule can meet collides, and the validator models exactly that

Every clutter part is a `Static` mesh. Nothing can be picked up, and nothing
moves when the ship turns, which is correct while the ship only cruises
(vision, *Flight modes*). Collision follows where the item sits, and the
builder derives it from the band. It is never listed per placement:

- **Surface clutter and wall dressing have no collision** (`NoCollision`).
  That covers mugs, tins, tablets, books, the toolbox on the workbench,
  notes, photos, the jacket, cable runs, handrails and the worn floor of
  decision 13. The capsule cannot reach a tabletop, so nothing on a surface
  can change where the player walks. Keeping collision would cost something
  in play, because a mug on the galley table would sit in the laptop
  cursor's trace, the `E` interaction trace and the camera's sweep. Most of
  these items are also smaller than a 10 cm cell, so the grid would never
  have seen them anyway. They are now outside the validator by rule rather
  than by accident.
- **Floor-band clutter collides, and cannot be stepped on.** Crates,
  canisters, coils and boots are `BlockAll` with `CanCharacterStepUpOn =
  ECB_No`. `CharacterMovement`'s `MaxStepHeight` defaults to 45 cm. Without
  the flag, the player would climb onto boots and small crates that the
  validator, which allows a rise of one cell, treats as walls. A props test
  requires every floor-band template to be at least 12 cm tall and at least
  10 cm on each footprint axis, so nothing that collides is smaller than a
  cell.
- **The validator rasterises dressing conservatively.** Any cell that a
  colliding dressing box overlaps at all counts as solid, instead of only
  the cells whose centres fall inside it. Walls and furniture keep
  cell-centre sampling, so existing validation is unchanged. The effect is
  that the validator can only think the floor more blocked than it really
  is. It cannot miss a pocket. It can report one the game does not have,
  which is the safe way to be wrong.

`build_hauler.spawn_box` sets the collision profile and the step-up flag from
the band. `verify_level.py` checks both on every `prop_` actor that came from
dressing. `validate_hauler.py`'s module docstring gains a *Model* paragraph
listing what the grid does and does not represent: colliding dressing,
conservatively; non-colliding dressing, not at all; and a step of one cell
where the engine allows 45 cm. That last one is an existing disagreement over
furniture. The player can step onto the 45 cm airlock bench seat, and the
validator says they cannot. It makes the validator under-report what is
reachable, which is the conservative direction, and it is recorded under
*Risks* rather than fixed here.

**Nothing in the dressing ever changes at runtime.** No mess accumulates, no
dust settles, and nothing needs tidying. The anti-chore principle forbids a
ship that gets untidier while you fly, and the simplest way to keep that
promise is to have no mechanism that could break it.

**Rejected (first draft): all clutter solid, voxelised by cell centre, "what
is solid in the grid is solid in the game".** That claim was false, and the
draft's own decision 13 said why. Cell-centre sampling does not see a box
thinner than a cell, so mugs, notes, 3 cm cable runs and thin books would
have collided in the game and not existed in the validator. In the other
direction, the engine's 45 cm step would have let the player climb clutter
the validator treated as walls. The draft rejected no-collision to avoid two
views of the ship that disagree, and it had two such views anyway.

**Rejected: `NoCollision` on everything.** Walking through a crate on the
floor is the one place a missing collision is obvious, and the floor band is
where clutter meets the capsule.

**Rejected: keeping collision on surface clutter but ignoring
`ECC_Visibility` and `ECC_Camera`.** It fixes the traces but leaves a
collision body that nothing can ever touch. `NoCollision` says the same thing
more plainly.

### 6. A new intent check: floor-band dressing may not create pockets

Region reachability only tests named points. Floor clutter can cut off a
corner of floor without failing any of them, and that corner will feel like
a bug the first time the player tries to walk into it. `validate_hauler.py`
gains `check_dressing_pockets(ship)`. It builds the grid twice, once from the
bare ship (dressing excluded) and once dressed, and floods standing
reachability in both. A pocket is a cell that is standable and at floor level
in the dressed ship, was reached in the bare ship, and is not reached in the
dressed one. Cells that a dressing item occupies (conservatively, per
decision 5), or covers within the capsule's radius, are not standable and do
not count. Any pocket fails, and the message gives its location and the
nearest dressing label. The check costs one more grid build, about a second.

Because surface and wall dressing do not collide, only the floor band (item
8) can make a pocket. The check therefore lands with item 8, behind the
second cut. `generate()` returns the colliding dressing boxes' labels as
`ship.dressing`, so the validator can separate them without guessing.

A **corpus test**, `Tools/test_dressing.py`, checks 500 seeds for
determinism, containment, exclusions and anchoring from the second cut on,
which is cheap. Once item 8 lands, it also validates seeds 0 to 49 in full.
That takes about a minute, in a single process under `nice -n 19` with no
pool. This is ADR 0006's generator property test in miniature: every seed
passes, and the same seed always gives the same boxes. It is sized
deliberately and is not a sweep sized to the core count.

**Rejected: a warning instead of a failure.** Pockets are exactly the
residue that ADR 0008 says rejection is a floor for. A dressing that makes
one should be redrawn, by bumping the seed or λ, and should not ship.

### 7. Light colour is set as RGB computed from kelvin, not through `bUseTemperature`

Each room has a mood in `hauler_layout.py`:

```python
Mood = namedtuple("Mood", "kelvin scale")
ROOM_MOOD = {
    "cockpit":     Mood(4200, 0.45),  # dim, so the window is the brightest thing
    "corridor":    Mood(4600, 0.8),
    "cargo_bay":   Mood(5200, 1.0),   # a working hold
    "engineering": Mood(3600, 0.9),
    "galley":      Mood(2900, 0.85),
    "bunk":        Mood(2700, 0.6),
    "airlock":     Mood(6200, 0.9),   # clinical: the one room that is equipment
    "crawlway":    Mood(3200, 0.5),
}
```

`placement.Light` gains `colour` and `shadows` fields (defaults: neutral, no
shadows). `placement.kelvin_to_rgb(k)` is a pure function using the standard
curve-fit approximation. `resolve_lights` multiplies intensity by `scale` and
emits each room's lamp panels in a per-room role, `lamp_<room>`, whose
emissive colour is the same kelvin RGB × 6. `build_hauler.EMISSIVE` is then
built from `ROOM_MOOD` rather than listed by hand. `place_lights` writes
`light.colour` into `light_color` and stops using the `LIGHT_COLOUR` constant.

**Rejected: `bUseTemperature`.** `UShipLightingSubsystem` reads each light's
`RatedColour` from `GetLightColor()` and lerps it towards amber as the light
browns out. With temperature enabled, the engine multiplies a second tint on
top of that colour, so a warm room browning out would go doubly orange, and
the colour on screen would stop matching the colour the verifier can check.
Putting the colour in one place keeps the brown-out correct with no C++
change.

The cockpit being the darkest room is the central principle made literal.
*Scale is only felt in contrast*, and the contrast here is a dim, warm
interior against the stars outside.

### 8. Practical lamps are the only lights that cast shadows

Three `Practical` entries in the layout, each a small prop with a point light
at its head:

- a `desk_lamp` on the bunk desk (2700 K, radius 180),
- an under-cabinet strip on the galley counter (2900 K, radius 250; the
  counter template gains a 2 cm `lamp_galley` part under the uppers),
- a bench lamp over the engineering workbench (3600 K, radius 200).

They carry the `Power.Lights` tag, so they dim and brown out with the
allocation like every other light. They are the only lights with
`cast_shadows = True`. Three shadowed movable point lights cost little, and
shadows are what make a mug on a table look like it is sitting *on* the
table. The ceiling grid stays unshadowed fill, as it is now.

**Rejected: shadows on every light.** There are 31 overlapping movable
lights, and shadowing all of them costs real frame time for fill light that
reads worse. A lit room reads as a room because a few sources are strong and
directional.

### 9. The ship hums, and the sound is synthesised in C++ with no assets

`FShipHumVoice` in `Source/DeepSpace/Ship/ShipHumVoice.h` is a plain C++
struct with no `UObject`, in the style of `FShipPowerState`, so it can be
tested headlessly. It renders mono float samples from two inputs, and each
input comes from something the player's choices actually move:

- **`EngineFeed`**: `UShipSubsystem::GetConsumerSatisfaction(ShipPower::Engine)`,
  from 0 to 1. This is the engine's share over its want, which is the thing
  that charges the jump drive.
- **`Push`**: `GetLinearAcceleration()` over `FShipFlightState`'s rated
  `LinearAcceleration`, from 0 to 1. This is how hard the boosters are
  pushing right now. The subsystem already folds booster satisfaction into
  it ("boosters on a thin allocation push softer"), so the voice does not
  recompute that.

The voice itself:

| Term | Frequency | Level |
|---|---|---|
| Fundamental | 48 Hz × (1 + 0.04 × EngineFeed) | 0.30, constant |
| 2nd partial, two sines detuned ±0.07 Hz so it beats slowly and never loops | 2 × fundamental | 0.15 across the pair, constant |
| 3rd partial | 3 × fundamental | 0.04 + 0.20 × EngineFeed |
| xorshift32 noise through a one-pole low-pass | cutoff 400 + 1200 × Push Hz | 0.02 + 0.20 × Push |

Every level, frequency and cutoff target is smoothed by a one-pole with τ =
0.8 s, so moving a slider makes the ship *settle* into a new note instead of
jumping to it. The phases are accumulated, never recomputed from time, so
changing a frequency cannot click. Master gain is 0.125, which puts the
worst-case peak at about 0.114 (−19 dBFS).

What the player hears: move weight towards the engine on the laptop and,
unless the engine already has everything it wants, the drone rises a little
and brightens as its third partial comes up. Turn the lights off and the
power they free goes to the engine and boosters, and the drone brightens with
it. Push the throttle and the hiss opens up. With the boosters starved the
hiss is thinner at the same throttle, because the ship is pushing less. The
lights' own share is not in the voice at all. Lights are seen, not heard.

**Starved is a register, not a fault.** At `EngineFeed = 0` the voice is
lower and darker, and still complete. No term falls to silence, nothing is
detuned into dissonance, and there is no buzz, alarm or beep. A starved
engine is a way of living aboard (vision, *an allocation is a trade-off you
live with*), and the sound must not suggest that one setting is the right
one. If playtesting says a well-fed engine sounds *better* rather than
*different*, the third-partial term is the first thing to flatten.

`UShipHumComponent : USynthComponent` (AudioMixer module) overrides `Init`
(mono, the device's sample rate) and `OnGenerateAudio`. On the game thread,
`TickComponent` *asks* `UShipSubsystem` for the two inputs and writes them to
`std::atomic<float>` targets that the audio thread reads. It stores no ship
state (ADR 0003). There are two kinds, as `EShipHumKind`:

- **Reactor**: spatialised, falloff 1500 cm, placed at the reactor. It is the
  whole voice above.
- **Air**: noise only, very quiet, one per room at the ceiling, falloff 600
  cm. It is **constant**. Life support is deliberately not a power consumer
  (interactable-ship spec, decision 2), so air handling has nothing to
  follow, and a hiss that changed would be a signal that means nothing.

`AShipHumSource` is a C++ actor whose root is one `UShipHumComponent`, with
`Kind` exposed as a property. `build_hauler.py` spawns them from a
`HUM_SOURCES` list in the layout. That is asset and tunable assignment only,
per ADR 0002.

**Rejected (first draft): pitch following "load", the sum of consumer shares
over reactor output.** That sum is flat by construction. The wants (300, 450
and 500 W against 1000 W) are over-subscribed on purpose, and the surplus is
redistributed, so moving a weight changes *who* gets the power but not *how
much* is allocated. The drone would only have moved when the lights went off
or a module's draw changed, and the playtest question "does the hum settle
when the split changes" would have been answered no. It was also a total, the
same quantity decision 11 now refuses to display.

**Rejected: `EngineFeed` and `BoosterFeed` as two independent partials,
without thrust.** A booster's share means nothing while the ship is not
pushing, so a hiss that followed booster satisfaction at zero throttle would
be a signal about a setting rather than about the ship. `Push` already
carries booster satisfaction at the moment it matters.

**Rejected: a MetaSound.** A MetaSound source is a node graph in a
`.uasset`, which is ADR 0002's drift signal: the logic would be invisible to
git and to the collaborator. **Rejected: an engine sound asset** such as
`WhiteNoise` or `1kSineTonePing`. It is a fixed file with nothing to drive,
and it would still need C++ to follow the ship. **Rejected, for now:
footsteps.** They need either assets or an animation-notify pipeline, and
the drone does more for the ship per hour spent.

### 10. Lamp panels dim with the lights they belong to

Today a room browns out and the ceiling panels above it keep glowing at full
emissive. That reads as a rendering bug, and warmer lamp colours will make it
more obvious. `build_hauler.spawn_box` tags every `lamp_*` box `Power.Lamps`.
`UShipLightingSubsystem::Refresh` finds them by tag, gives each a
`UMaterialInstanceDynamic` from its material, and records the rated `Colour`
parameter. `Tick` then drives that parameter with the same `Scale` and
brown-out colour it already computes for the point lights. The tag is the
contract, never the label (CLAUDE.md, *addressed by tag*).

**Rejected: moving lamps into the point-light actors.** That is a bigger
change to the level's shape for no gain in play, and it would break the
verifier's box-by-box check.

### 11. Readouts are small, lit, read-only screens that stay on in the dark

`AShipReadout : AShipScreen` sets `bUsable = false` and draws a 240 × 80 px
panel 24 cm wide, which is 10 px/cm, the same density as the console. Its
widget is `UShipReadoutWidget : UShipScreenWidget`: one line of `BodySize`
text and one dim caption, updated in `NativeTick` no more than twice a
second. `EShipReadout` picks what it shows, and everything shown is real or
is honestly static:

| Where | Kind | Shows |
|---|---|---|
| galley, fore wall | `ShipClock` | `Day 214 · 06:42` |
| engineering, fore wall | `Reactor` | `Reactor 1000 W` (the rating, from `GetReactorOutput()`) |
| corridor, starboard wall by the cockpit | `Speed` | `12.4 m/s` |
| bunk, above the desk | `Static` | `Cabin 101 kPa` |
| airlock, beside the seal | `Static` | `Outer door sealed` |

Readouts run off their own feed, meaning they are not `Power.Lights`. With the
lights switched off, the ship goes dark and the readouts stay lit, and that is
how the player can see the ship is still on. None of them shows a percentage,
a target, a comparison, or a quantity next to its ceiling (anti-chore; see the
interactable-ship spec's addendum on watts).

**The reactor readout is a nameplate.** It shows one bare fact, the output
the ship's reactor is rated for, with no second number beside it. It changes
only if a module changes the reactor. It stays because it is a fact about the
ship, and it gives the laptop's per-consumer watts something to mean. Nothing
anywhere in the ship shows a total drawn.

**Rejected (first draft): `1000 W out · 740 W drawn`.** That is a
utilisation meter, 74% of the reactor by one division, with 260 W sitting
"unused". It is the vision's own named failure case ("the engines are at 73%
of potential ... invents an optimum"), on a wall the player walks past every
cruise. The addendum says satisfaction is shown as watts and *never a
total*. The draft cited that addendum while breaking it. **Rejected: a
lit/unlit reactor state only.** Every readout is lit while the ship is on, so
a panel that says "Reactor on" tells the player nothing the lit panel does
not.

`Mount` gains an optional `along` so a readout can be placed off-centre on a
wall. A new validator check, `check_mounts_on_wall`, fails if the cell behind
any mount is an opening rather than a solid wall.

The ship clock starts at `ClockOriginSeconds`, a `UPROPERTY` on the readout
that `build_hauler.py` sets from the dressing seed (a day count of
200 + Poisson(20, Max = 60), and a time of day), and advances with world time. A clock
is not a timer. Nothing in the game is scheduled against it.

**Rejected: `UTextRenderComponent`.** It is cheaper per instance, but its
default material is lit, so the text would not glow in a dark ship, which is
the whole point. Making it glow means writing a font-sampling material in
Python. The widget-component path is already proven here, with all four
traps written down in CLAUDE.md, and five more small render targets cost
almost nothing.

### 12. Wear is painted in by role, not modelled

- **Kick band.** In `floorplan.py`, each wall column's lowest segment becomes
  `(-SLAB, KICK, "kick")` with `KICK = 20` (on the grid), and the `wall`
  segment starts above it. The same applies under window sills. Doors are
  unaffected: their below-floor segment is already a threshold. `kick` is a
  dark, scuffed, grey-green panel. Every wall gets the dark skirting that
  boots and trolleys leave on real ones. This adds about 60 boxes.
- **Furniture buckets**, per decision 3: new roles `furniture_faded` and
  `furniture_replaced`. `Place` gains an optional `role` override, which
  `resolve_props` applies to parts whose role is `furniture`.
- **New plain roles** for clutter: `ceramic`, `metal`, `rubber`, `paper`,
  `fabric_{olive,navy,rust,ochre}`, `paint_{red,yellow}`. Each is a
  `PANELLED` instance with its seam colour set equal to its surface colour.
  That is a plain colour with no new material asset, so the ADR 0006 runtime
  path (pre-authored instances, selected and parameterised) is unaffected.

**Rejected: a procedural grime material** (world-position noise, darker
towards the floor). It is the better-looking answer, but authoring a
material graph through `MaterialEditingLibrary` is fragile, and the kick
band gets most of the effect as data. Revisit when art is on the table.

### 13. Stretch goal: desire lines, where the floor is worn by where you walk

`dressing.traffic(plan, grid)` runs a breadth-first shortest path between
every pair of standing regions on the validator's stand grid and counts how
many paths pass through each cell. Cells above the 80th percentile become thin
(0.5 cm) `floor_worn` boxes, merged by `merge_rectangles`. They have no
collision (decision 5), so they are outside the validator by rule. The first
draft said they were invisible because they were thinner than a cell. That
was true of these boxes, and it was the same reasoning that made the first
draft of decision 5 false. The result is wear that follows use, which is
the real history of a ship. This is item 10: build it only if everything
else has landed and time is left.

## What is faked, and what it costs later

- **Clutter is baked into `L_Hauler.umap` by a Python commandlet.** ADR 0006
  says generation runs in C++ at startup. Cost later: port `dressing.py` to
  the plain-C++ generator layer on top of `FGenStream`, and delete
  `rng.py`. Because `rng.py` is a literal mirror, the port reproduces the
  same ships, and `rng_vectors.json` is what shows that it does.
- **One `StaticMeshActor` per clutter part**, about 150–250 more actors. The
  ADR 0006 path is instanced components on one actor. Cost later: nothing
  extra, because this is the same migration the walls need anyway. If it
  shows up in frame time before then, clutter is the first thing to
  instance.
- **Items turn only in 90° steps.** Everything is axis-aligned, so the
  validator can voxelise it exactly. Stacks will look a little too neat.
  Cost later: small yaw jitter needs oriented boxes in the validator, or
  bounding-box over-approximation (the approach already used for
  cylinders).
- **Nothing can be picked up, and nothing moves when the ship manoeuvres.**
  This is fine at cruise. Combat manoeuvring (a later upgrade) will want
  clutter to slide, which means physics and a different validator model.
- **The ship clock's origin lives on a readout, not in the subsystem.** When
  navigation or jumps need ship time, it moves to `UShipSubsystem` and the
  readout asks for it.
- **Static readouts are text the builder sets.** "Cabin 101 kPa" is a
  constant. It becomes real only if cabin atmosphere is ever modelled, and the
  vision gives no reason it should be.
- **Wear is colour, not geometry**: no dents, no decals, no grime.
- **`Tools/rng.py` mirrors `FGenStream` in Python** (decision 4). Cost now:
  any change to `FGenStream`'s algorithms must regenerate
  `rng_vectors.json` and fix the mirror. Both sides' tests fail until that is
  done, which is the point. Cost later: deleting it, with `dressing.py`, when
  dressing is ported.
- **Surface and wall dressing have no collision.** Cost later: picking
  things up, or clutter that slides during combat manoeuvring, needs the
  collision back and a physics model the validator does not have.
- **The hum's `EngineFeed` answers to a jump charge that nothing consumes
  yet** (interactable-ship addendum). The sound is honest about the share,
  even though the share's effect is still just a number climbing.

## Implementation outline

Steps marked **[editor]** compile, run a commandlet, rebuild the level or
run automation tests. They are **serialised**: only one process may use the
editor at a time, and `./build.sh` refuses to run while the editor is open.
Everything else is pure Python or writing C++, and needs neither.

**To the first cut** (about 12 h):

1. **Lighting moods and practicals** (items 1, 2). `hauler_layout.py`:
   `ROOM_MOOD`, `PRACTICALS`. `placement.py`: `Light` gains `colour` and
   `shadows`, plus `kelvin_to_rgb` and per-room lamp roles. `props.py`:
   `desk_lamp`, and the counter's under-cabinet strip. `build_hauler.py`:
   `EMISSIVE` built from moods, and `place_lights` sets colour and shadows.
   Tests in `test_placement.py`: a 2700 K light has r > g > b, 6500 K is
   within 3% of white, lamp roles match rooms, and practicals carry the
   lights tag. *No editor.*
2. **[editor] Build and look.** Run `validate_hauler.py`, then the
   `build_hauler.py` commandlet and `verify_level.py` (now also checking
   each light's colour within 2/255 and the practicals' shadow flag). Then
   open the level and walk it. This is the earliest point the developer sees
   the pass, and it should already feel different.
3. **C++ batch A: the hum and the lamp panels** (items 4, 6).
   `DeepSpace.Build.cs`: add `AudioMixer` to the public dependencies. New:
   `Ship/ShipHumVoice.{h,cpp}`, `Ship/ShipHumComponent.{h,cpp}`,
   `Ship/ShipHumSource.{h,cpp}`. Changed: `Ship/ShipLightingSubsystem.{h,cpp}`
   (a `FShipLamp` array and `Power.Lamps`), and `ShipPower::Lamps` alongside
   `ShipPower::Lights`. Tests:
   - `DeepSpace.Ship.HumVoice`. The same seed and inputs give the same
     samples, and |x| ≤ 1 with no NaN at 44.1 and 48 kHz. **Smoothing**,
     tested on the parameter trajectory rather than the audio: step a target
     from 0 to 1, and the smoothed value is monotone, never overshoots, and
     is 0.632 ± 0.01 at 0.8 s. **No clicks**, on the tonal voice with noise
     disabled (a test-only `bNoise = false`): render 2 s, step `EngineFeed`
     from 0 to 1 at 1 s, and every |x[n] − x[n−1]| stays under the analytic
     bound gain × Σₖ (2π fₖ,max Aₖ,max + ΔAₖ / τ) / SR, × 1.01. Anything
     continuous is under it, and a phase reset or an unsmoothed step is over
     it, so there is no tuned ε. **Terms that follow inputs**: the tonal RMS
     at `EngineFeed = 1` exceeds the RMS at 0, and the noise-only RMS at
     `Push = 1` exceeds the RMS at 0. Each assertion tests a term the design
     actually has.
   - `DeepSpace.Ship.LampPanelsDim`: a tagged box's dynamic `Colour` falls
     when the allocation starves the lights.
   Writing the code: *no editor*. Compiling it: **[editor]**
   `./rebuild.sh --force`.
4. **[editor] Wire, check, listen.** `build_hauler.py` spawns
   `AShipHumSource`s from `HUM_SOURCES` and tags lamps `Power.Lamps`.
   `verify_level.py` checks their positions and the tags. Then, in order:
   build the level, verify, `check_blueprints.py`, the headless automation
   run (`DeepSpace`), then launch and listen. The questions: with the engine
   not yet fully fed, does the drone settle into a new note when weight moves
   to or from the engine on the laptop? Does switching the lights off
   brighten it? Does throttle-up sound like effort, and thinner with the
   boosters starved? Does a starved engine sound like a *different* ship, and
   not a *worse* one? Is the ship still quiet enough to be solitary?
   **First cut.**

**To the second cut** (about 11 h more):

5. **RNG and surfaces** (item 3, first half). `Tools/rng.py` as a mirror of
   `GenSeed` and `FGenStream` (decision 4), and `Tools/rng_vectors.json`, which
   begins with procgen's four pinned values. `props.py`: `Surface`,
   `SURFACES` and `ANCHOR`. `placement.py`: the anchoring check.
   `test_dressing.py`: the vectors, and an anchoring test that fails on a
   floating counter. *No editor.* About 4 h.
6. **Surface clutter** (item 3, second half). `props.py`: the clutter
   templates (`mug`, `plate`, `bottle`, `tin`, `tablet`, `book`, `toolbox`,
   `cable_coil`, `part`, `canister`, `crate_small`, `boots`, `blanket`), each
   with a band. `Tools/dressing.py`: `surfaces_of(plan, placements)`,
   `dress_surfaces`, and `dress(plan, placements, seed, lived_in) -> (places,
   boxes)`. The laptop's footprint, plus 15 cm and the whole strip to the
   table edge on its user's side, is an `exclude` on the galley table, so
   nothing sits between the seated view and the screen. `hauler_layout.py`:
   `DRESSING_SEED`, `LIVED_IN` and `ROOM_DRESSING`. `build_hauler.py`: the
   collision profile and step-up flag set from the band (decision 5).
   `test_dressing.py`: the 500-seed cheap corpus. *No editor.* About 4 h.
7. **Wear** (item 5). `floorplan.py`: the kick band (update
   `test_floorplan.py`'s wall expectations to match). `build_hauler.py`: the
   new plain roles and furniture buckets. `Place` gains `role`. *No editor.*
8. **[editor] Build, verify, playtest.** Validator, then build, then
   verifier, which now also checks the dressing's collision profiles. Walk
   every room at `LIVED_IN` 0.3, 1.0 and 2.0, and try three seeds. The
   questions: does it read as somebody's ship, does anything look placed
   rather than left, and does the laptop's view stay clear? **Second cut.**

**Behind the second cut:**

9. **Wall dressing and floor bands** (items 7, 8). `props.py`: `note`,
   `photo`, `jacket`. `placement.py`: `resolve_runs(plan, runs)` for
   handrails and cable runs, which splits a run at every opening (with a
   10 cm margin), puts brackets every 120 cm, and labels boxes
   `prop_run_<name>_<n>_<k>` so the keep-clear check covers them.
   `dressing.py`: `wall_bands` and `dress_floor_bands`. `validate_hauler.py`:
   conservative rasterisation of colliding dressing, `check_dressing_pockets`,
   and the *Model* paragraph. `hauler_layout.py`:
   `ship.dressing`. `test_dressing.py`: the 50-seed full-validation corpus
   (single process, `nice -n 19`). *No editor.* About 6 h.
10. **[editor] Build, verify, walk.** The question: does any floor clutter
    get in the way of walking?
11. **C++ batch B: readouts** (item 9). New: `Ship/ShipReadout.{h,cpp}` and
    `UI/ShipReadoutWidget.{h,cpp}`. In the layout tools, `Mount` gains
    `along`, and `validate_hauler.py` gains `check_mounts_on_wall`. Test: `DeepSpace.Ship.Readouts`. The speed readout's text
    matches `GetShipSpeed()` formatted, after a known velocity is set through
    the flight-command path. The reactor readout's text contains exactly one
    number. No readout's text contains `%`. The last two are the anti-chore
    rule written as a test. Writing: *no editor*. Then **[editor]**
    `./rebuild.sh --force`, spawn `READOUTS` from `build_hauler.py`, build,
    verify, `check_blueprints.py`, the automation run, and a look.
12. *(Stretch.)* **Desire lines** (item 10): `dressing.traffic`, a
    non-colliding `floor_worn` role, and a test that the worn cells sit on
    the corridor centreline and the galley–bunk path. Then **[editor]**
    build and verify.

## Risks

- **Clutter reads as scattered rather than left.** This is the main risk and
  the reason every distribution in decision 3 is anchored to a use point and a
  back edge. If step 8 says it still looks sprinkled, reduce λ before adding
  rules. A few well-placed things beat many plausible ones.
- **Pocket failures across seeds.** Only floor-band clutter can make one
  (decision 5), and the cargo bay's floor bands between containers are the
  likely source. The corpus test finds them before the
  developer does. The fix is to shrink the band or lower λ, not to loosen
  the check.
- **The hum becomes fatiguing.** A constant low drone across a two-hour
  session can wear on the player, which would make the sound a nuisance
  itself. Keep it at −18 dBFS or quieter, and make the air hiss the
  quietest thing in the mix. If the playtest says it wears, add a
  `ds.Hum` volume CVar before retuning the synthesis.
- **Audio under the headless test run.** `-nullrhi` runs may have no audio
  device, so the component is never exercised there. Only `FShipHumVoice` is
  tested headlessly, and whether the component sounds right is a playtest
  question, the same split the screen pointer uses.
- **Colour drift between the lamp panel and the light it belongs to.** Both
  come from `kelvin_to_rgb` of one mood, and the verifier checks the light.
  If they disagree on screen, the emissive ×6 is the suspect: tonemapping
  shifts saturated emissives.
- **Changing `counter`, `props.py` or the floor plan's columns renames boxes.**
  Every hand-placed change is lost anyway (ADR 0004), and the verifier keys
  on labels, so rebuild the level before verifying. Never verify an old map
  against a new layout.

- **The RNG contract spans two specs.** Procgen names its samplers but does
  not pin their exact forms. `rng.py` pins them, and whichever of the two
  lands second conforms to the first. If procgen lands first and chooses
  differently, `rng.py` follows procgen and the vectors are regenerated. The
  risk is small because the dressing's output is not yet anybody's saved
  state.
- **A well-fed engine may sound *better*, not *different*.** That would be a
  quiet optimum, which is the anti-chore failure, told by ear. Step 4 asks
  the question directly. The remedy is to flatten the third-partial term,
  not to add a meter.
- **The validator's step is one cell, and the engine's is 45 cm.** Floor
  clutter cannot be stepped on, but furniture can: the player can climb
  onto the 45 cm airlock bench seat, and the validator says they cannot.
  This errs towards under-reporting reach, the conservative direction. It
  predates this pass and is not fixed here. `CanCharacterStepUpOn = ECB_No`
  on furniture is the one-line fix if it ever matters.

## Non-goals

- Interactive clutter, picking things up, inventory.
- Anything that accumulates, decays, needs cleaning, or needs attention.
- Footsteps, music, and voice.
- Modelled props, imported textures, decals, Fab or Megascans assets.
- Porting the generator to C++ (ADR 0006). This pass shapes the rules the
  port will carry.

## Review, 2026-09-25

A review of the first draft raised six issues. All six were accepted, and the
design was changed for each. The superseded choices are kept in place as
rejected alternatives.

| # | Issue | Where it is answered |
|---|---|---|
| 1 | The reactor readout `1000 W out · 740 W drawn` is a utilisation meter, the vision's named failure | Decision 11: a nameplate showing only the rating, and a test that it carries one number |
| 2 | The hum's "load" is flat by construction under any split | Decision 9: engine feed drives the pitch and the third partial, and booster push drives the hiss |
| 3 | `rng.py` was a second derivation and sampler library that disagreed with `GenSeed`/`FGenStream` | Decision 4: a literal mirror, procgen's known values as vectors, and the exception to procgen decision 10 argued explicitly |
| 4 | The cut line and the build order contradicted each other, and the hours were optimistic | *What is ranked*: hum first, two cuts, hours re-estimated, the single C++ batch rejected |
| 5 | "Solid in the grid is solid in the game" was false for sub-cell clutter and for step height | Decision 5: collision by band, no step-up, conservative rasterisation, and the validator's model written down |
| 6 | Two `HumVoice` assertions tested properties the synthesis did not have | Step 3: parameter trajectories, an analytic no-click bound on the tonal voice, and RMS asserted only for terms that exist |
