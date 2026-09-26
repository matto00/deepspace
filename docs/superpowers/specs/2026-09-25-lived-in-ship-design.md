# A ship that feels lived in

**Date:** 2026-09-25
**Status:** Draft, revised after review, then revised again on the
developer's ruling that the dressing generator is C++ — awaiting sign-off
(see *Review, 2026-09-25* and *Ruling, 2026-09-25* at the end)
**Follows:** The interactable ship (implemented), flight and the counter-frame
(implemented)
**Governed by:** `docs/vision.md` — *the starter ship is worn*, *scale is only
felt in contrast*, the anti-chore principle; ADR 0004 (generated, validated),
ADR 0006 (generation is C++ and runs at startup), ADR 0007 (seeds derive down
a hierarchy), ADR 0008 (shape the prior)

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
| 3 | Surface clutter: mugs, books, tools and tins on every surface, placed by Poisson/Beta | 5 | 10 | C++, surfaces from Python | yes |
| 4 | Ship hum: a reactor drone whose timbre follows the engine's feed and whose hiss follows the boosters' push; a constant air hiss | 5 | 5 | C++ | yes |
| 5 | Kick band and material wear: a dark scuffed skirting on every wall, faded and replaced panels | 3 | 2.5 | Python (band), C++ (wear) | yes |
| 6 | Lamp panels dim with the lights, so a browned-out room no longer has glowing ceiling panels | 3 | 1.5 | C++ | yes |
| 7 | Wall dressing: handrails split around doors, notes, a jacket on a hook, cable runs | 3 | 3.5 | Python (runs), C++ (notes, jacket) | yes |
| 8 | Floor-band clutter, and the pocket check it needs: crate stacks, boots by the bed, canisters | 2 | 4 | C++, bands from Python | yes |
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

**And revised again on the ruling** (see *Ruling, 2026-09-25*). Item 3 lost
the Python RNG mirror and its vectors, about 2 hours, and gained a C++
generator, its world test and the surface markers, about 4. It also changed
column: it now needs a compile. The dressing is new files only and touches
no existing header, so that compile is a plain `./build.sh` and needs no
`--force` rebuild of its own.

**Build order: 1 and 2, then 4 and 6 together as the first C++ batch, then 3
and 5.** Items 7 to 10 wait behind a second cut.

- **First cut, after 1, 2, 4 and 6 (about 12 h).** The ship is lit warm, room
  by room. It hums, the hum answers the laptop and the throttle, and its
  ceiling panels dim when its lights brown out. Nothing has been added to
  the floor, so nothing can break reachability. This is the least that is
  worth flying.
- **Second cut, after 3 and 5 (about 13 h more, and one compile).** There is
  clutter on every surface, scuffed skirting, and worn and replaced
  furniture. Surface clutter has no collision (decision 5), so this cut
  cannot make a pocket either.
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

### 1. Dressing is a generated layer: C++, run at world start, from surfaces the layout exports

The clutter is generated by C++ when the world begins play, like every other
generator in this game (ADR 0006). There is exactly one dressing generator
and it is in C++. It comes in two halves, the same split as `FShipPowerState`
and `UShipSubsystem`:

- **`ShipDressing`** (`Ship/ShipDressing.h/.cpp`) is the pure core. It is a
  function of three things: a list of surfaces, a seed, and the rules
  (decision 1c). It returns a *plan*, meaning which template goes where, in
  which colour, on which surface. It has no `UObject`, no world and no
  assets, so everything it decides can be tested headlessly. It is also the
  part the vision means by *what is replicated is the plan, never the
  geometry*.
- **`UShipDressingSubsystem`** (`Ship/ShipDressingSubsystem.h/.cpp`) is
  everything that touches Unreal. In `OnWorldBeginPlay` it finds the
  surfaces (decision 1b), asks the core for a plan, and turns the plan into
  instances (decision 1a).

```cpp
/** Somewhere things can rest, in its own frame: X across the surface, with
 *  the Back edge at -X or +X; Y along it; Z up from the resting plane. */
struct FDressSurface
{
    FName Room;              // selects the room stream
    FName Kind;              // "<prop>.<surface>": selects the λ and the mix
    int32 Ordinal = 0;       // the placement's n, as in prop_<name>_<n>
    FTransform ToWorld;      // yaw in 90° steps only
    FVector2D Size;
    EDressEdge Back;
    EDressUse Use;
    float Clear = 40.0f;     // the tallest thing that fits, stacks included
    TArray<FBox2D> Excludes; // surface-local; nothing may cover them
};

struct FDressItem
{
    int32 Surface;           // index into the surfaces Dress was given
    FName Template;
    FName Colour;            // NAME_None unless the template takes one
    FTransform World;        // footprint centre on the resting plane
};

namespace ShipDressing
{
    TArray<FDressItem> Dress(TConstArrayView<FDressSurface> Surfaces,
                             uint64 Seed, const FShipDressingRules& Rules);

    /** One draw per piece of furniture; decision 3's three buckets. */
    EDressWear Wear(uint64 Seed, FName Piece, const FShipDressingRules& Rules);
}
```

The seed is world-level, so two players aboard see the same mugs in the same
places (vision, *How social the game is*). It is `Ship = Derive(GetRootSeed(),
Label("ship"))`, then `DressSeed = Derive(Ship, Label("dressing"))`. The
intermediate `ship` seed is where a seeded layout will hang once the ship
generator is ported. The dressing is already under it, so that port does not
reshuffle the mugs. Without a `UUniverseSubsystem` the root falls back to a
constant, the same fallback the sky's `StarSeed` keeps. A different seed
gives a different life on the same floor plan. `LivedIn`, default 1.0,
scales every expected count at once. It is the knob for poking holes: at
0.3 the ship is freshly moved into, and at 2.0 it is squalid.

The walls and furniture are still built by `Tools/build_hauler.py` into
`L_Hauler`, which ADR 0006 allows until the layout is ported ("until this
lands, `Tools/build_hauler.py` remains how the ship is built"). The layout is
a plan written by hand and then resolved. It is not a seeded draw. The
dressing is the ship's first *seeded* generator, and a seeded generator is
exactly what ADR 0006 says must be C++ at startup.

**Rejected (second draft): `Tools/dressing.py`, baking the clutter into
`L_Hauler.umap` as a deliberate exception to ADR 0006.** The second draft
generated clutter in Python, on a line-for-line mirror of `FGenStream`, and
baked it into the level with the rest of the ship until a later port. The
case for it was the loop: a second per run, a 500-seed corpus with no editor
at all, and no restart per rule. **The developer refused the exception.**
The reasons it was wrong:

- A generator that only runs in the editor builds exactly the ships the
  developer thought to build, one at a time, with the editor open. That is
  ADR 0006's whole objection. "For now" is how the POC would have tested the
  wrong thing. The question this pass asks is whether a *generated* ship can
  read as lived in, and it should be answered with the generator the game
  will actually have.
- It was a second generator waiting to happen. The port was meant to
  reproduce the same ships "because `rng.py` is a literal mirror", which is a
  promise that two implementations will agree, kept only by vectors. ADR
  0006's rule is that there is one implementation, not two that agree.
- The mirror's cost lasted until the port. Every change to `FGenStream`'s
  algorithms meant regenerating vectors and fixing Python, in the same wave
  in which procgen owns that library.
- Its speed advantage was smaller than it looked. Every *look* at a Python
  dressing needed a level build by commandlet with the editor closed, and
  then the editor reopened. The C++ generator redresses a running session in
  place (decision 1c).

**Rejected (first draft): placing clutter by hand in `PLACEMENTS`.** It would
be faster for the first twenty items and wrong for the reason ADR 0006 gives:
a ship dressed by hand is dressed only on the developer's machine. More to
the point, hand placement cannot answer the question this pass is for, which
is whether a *generated* ship can read as lived in. If the rules cannot
produce that, the game needs to find out now.

### 1a. The generator is a world subsystem, and clutter is instances on one actor

`UShipDressingSubsystem : UWorldSubsystem` is a plain subsystem and does not
tick. It follows `UShipLightingSubsystem`: in `OnWorldBeginPlay` it finds what
the layout built by tag, and it exposes a public `Redress()`. That is public
for the same two reasons as `Refresh()`: a test spawns its own surfaces, and
the runtime ship generator will one day build the ship after this subsystem
already exists. In `Initialize` it calls
`InitializeDependency<UUniverseSubsystem>()` to get the root seed. It supports
game and PIE worlds only.

Why a subsystem:

- **It has to run with nothing placed.** ADR 0006 turns the map into a nearly
  empty world that the generators fill. A generator that is itself a placed
  actor needs the level to contain it, and a level built before the class
  existed would silently have no clutter.
- **There is one per world, and it can be found without a singleton.** The
  console commands (decision 1c) and the tests both need that.
- **It does not tick, on purpose.** Nothing in the dressing changes at
  runtime (decision 5), and a subsystem with no tick has nowhere to put a
  mechanism that could change it. The anti-chore promise is kept by the
  structure, not just by care.
- **It stores no ship state** (ADR 0003). It keeps weak pointers to what it
  spawned, so that `Redress` can remove them. The dressing is a snapshot.
  Nothing asks it anything.

**Rejected: putting it in `UShipSubsystem`.** That subsystem "knows nothing
about meshes, rooms, or the player", and clutter is nothing but meshes and
rooms. **Rejected: putting it in `UShipLightingSubsystem`.** That one answers
to power every frame, and the dressing never answers to anything.
**Rejected: an `AShipDresser` actor placed by `build_hauler.py`.** It fails
for the first reason above. A placed actor also keeps the component hierarchy
it was saved with (CLAUDE.md), which is one more thing to go stale when the
class changes.

**The plan becomes geometry in one place.** The subsystem spawns one actor,
tagged `Dress.Clutter`. It is transient, so it is never saved. It has a
`USceneComponent` root and one `UInstancedStaticMeshComponent` for each
*(mesh, material role)* pair the plan uses. That is ADR 0006's "instanced
components on one actor rather than N actors". The second draft listed this
as a fake, and this design gets it for nothing: about a dozen components and
150 to 250 instances.

- **Pivots are measured, never assumed.** Each instance's scale and offset
  come from its mesh's `GetBoundingBox()`, using exactly the arithmetic
  `build_hauler.spawn_box` uses (CLAUDE.md: `SM_Cube` has its origin at a
  corner, `SM_ChamferCube` at its centre, `SM_Cylinder` at its base). The
  level's furniture uses the same three meshes, so they are loaded with the
  level and their bounds are live. ADR 0006's baked-bounds asset is for a
  world with no furniture to load them, and it is not needed yet.
- **The root and every instance component are Movable.** A Static child of a
  movable root never has its world transform updated (CLAUDE.md), and a
  root spawned at runtime is an easy place to make that mistake. The ship
  never moves (ADR 0005), so Movable costs nothing.
- **Materials are selected, not made** (ADR 0006). Each role is
  `/Game/Materials/MI_Ship_<role>`, one of the `PANELLED` instances that
  `build_hauler.py` already authors (decision 12 adds the clutter roles). A
  role that does not resolve fails a test (decision 1d) rather than showing
  up in play as a mug in the default material.
- **Collision is set per component, from the band** (decision 5). Surface
  clutter is `NoCollision`.
- **Generating costs a few hundred draws and a few hundred instances**, so it
  does not read as a loading screen (ADR 0006's consequence, and the
  vision's rule).

**The same subsystem applies wear** (decision 12). `build_hauler.py` gives
every part whose role is `furniture` two tags: `Dress.Wear`, and
`Piece.<prop>_<n>` naming the placement it belongs to. The subsystem draws
one wear value per piece, from `Derive(Derive(DressSeed, Label("wear")),
Label("<prop>_<n>"))`, and when the bucket calls for it, it swaps all of that
piece's parts to `MI_Ship_furniture_faded` or `MI_Ship_furniture_replaced`.
A whole desk wears together. The draw is random, so it belongs to the one
generator. The kick band is not random, so it stays in `floorplan.py` with
the walls.

### 1b. The layout exports its surfaces as tagged markers, and the C++ finds them by tag

The layout knows where the shelves and counters are, and the C++ does not.
`SURFACES` (decision 2) is prop-local data in `props.py`, and only
`placement.py` knows where each prop ended up. So the layout exports every
surface as data, and the level carries that data: `build_hauler.py` spawns
one **`AShipDressingSurface`** per surface, tagged `Dress.Surface`, and
`UShipDressingSubsystem` finds them by that tag and reads them into
`FDressSurface` values. Of the two options, this is the first (the layout
exports data) delivered through the second's mechanism (generated actors
found by tag).

`AShipDressingSurface` (`Ship/ShipDressingSurface.h/.cpp`) is an `AActor`
with no logic. It has only tunables, which `build_hauler.py` assigns. That is
the use ADR 0002 allows, and `AShipHumSource` has the same shape. Its two
enums are `UENUM`s in `Ship/ShipDressingTypes.h`, a header holding nothing
else. The core includes it, and that is the only reflection the core sees.

| Property | What it carries |
|---|---|
| actor transform | the surface's centre on its resting plane, and the placement's facing as yaw (90° steps) |
| `Room` (`FName`) | the room, for the room stream (decision 4) |
| `Kind` (`FName`) | `<prop>.<surface>`, such as `counter.top` or `wall_rack.shelf_1`: which λ and which mix apply (decision 3) |
| `Ordinal` (`int32`) | the placement's `n`, the same `n` as in its `prop_<name>_<n>` labels |
| `Size` (`FVector2D`) | the extent, in the surface's frame |
| `Back`, `Use` | `Surface.back` and `Surface.use`, as the enums `EDressEdge` and `EDressUse` |
| `Clear` (`float`) | the tallest item that fits |
| `Excludes` (`TArray<FBox2D>`) | surface-local rectangles nothing may cover: the workbench's screen, the laptop, the chart |

`placement.resolve_surfaces(plan, placements)` computes these in pure Python,
from `SURFACES`, `ROOM_DRESSING` and the resolved placements, and
`hauler_layout.generate()` returns them as `ship.surfaces`. Two excludes are
computed there as well, because only the layout knows where the laptop and
the chart are. The laptop's is its footprint plus 15 cm, together with the
whole strip to the table edge on its user's side. The chart's comes from
plan conflict 16. The markers are labelled `hauler_surface_<prop>_<n>_<surface>`
for humans, and are rebuilt with everything else prefixed `hauler_`.

`verify_level.py` checks every marker against `ship.surfaces`: location
within 0.5 cm, yaw, size, clear, and the number of excludes. So the two
things that can drift apart, the layout and the level, are compared in the
place they are always compared, and the C++ reads only what the verifier has
already passed.

**The result does not depend on the order the markers are found in.** Each
surface's stream is derived from its own `Room`, `Kind` and `Ordinal`, never
from the order `TActorIterator` returns markers. The same level therefore
dresses the same way whatever order its actors load in. A test shuffles the
surfaces to prove it.

**Rejected: a JSON file of surfaces, written by the layout and read by the
C++.** It carries the same data, but through a second carrier. The level and
the file would be written by different steps, and on the day one is rebuilt
and the other is not, the clutter floats a metre off a table that has moved.
With markers, the furniture and its surfaces are written by the same
`build_hauler.py` run into the same map, and they cannot disagree without the
verifier failing. A file outside `Content/` would also not be staged by a
cook.

**Rejected: tagging only the furniture parts, and recovering each surface
from the part's bounds.** A surface is not the same as a part's top face.
The wall rack's shelves are clipped by its uprights. The cockpit desk's wings
are two surfaces on parts that also carry screens. And `clear`, `back`,
`use` and the excludes are not in any part. Recovering them from geometry
would be a second, worse copy of `SURFACES`, written in C++.

**Rejected: structured data in tag strings** (`Dress.Back.-x`,
`Dress.Clear.58`). That is parsing names, which is exactly what the tag rule
exists to avoid, and a typo fails silently.

**Rejected: porting `SURFACES` to C++ and letting the C++ work out where each
prop landed.** That is ADR 0006's port of `props` and `placement`, done for
one table ahead of all the others. It would give the project two resolvers
of where a prop is.

When the layout is ported to C++, the ship generator will hand
`ShipDressing::Dress` its surfaces directly. The markers, their verifier
check and `resolve_surfaces` are then deleted. The core does not change,
because it never knew where its surfaces came from.

### 1c. The tuning loop: the rules' numbers are data, and a running session redresses in place

**The cost, plainly.** Every change to how the dressing *decides* is now a
C++ change, and Linux has no Live Coding. The editor has to close, because
`./build.sh` refuses to run while it is open. Then the module builds, the
editor reopens and play starts again. The build itself is short: the plan
measured a cold build of the whole module at 38 s and the full automation
suite at 15 s. The restart around it is what costs, because it throws away
the session: where you were standing, what you had set, and what you were
looking at. That is the price of every rule tweak. The second draft's loop,
a second per run and a corpus with no Unreal process at all, is lost and
will not come back. The 500-seed corpus becomes an automation test run
through `./test.sh`, and it waits behind any other build or test on this
machine (`Tools/ue_lock.sh`).

What keeps the loop bearable is that nearly everything that gets tuned is a
number or a table, not a rule, and numbers are data:

- **`FShipDressingRules`** (`Ship/ShipDressingRules.h/.cpp`) is a pure
  struct holding every tunable, with its default in code. That covers the λ
  for each surface kind, the Beta shapes for position along and back, the mix
  for each surface kind, the colour weights, the stack chance and cap, the
  wear Beta and its bucket thresholds, and **the clutter templates
  themselves**. Each template is a list of parts (mesh, centre, size, role),
  which is `props.py`'s `Part` written in C++.
- **`UShipDressingConfig`** (`Ship/ShipDressingConfig.h/.cpp`) is a
  `UCLASS(Config = Game)` that mirrors the rules field by field. Each field is
  initialised from `FShipDressingRules{}`, so the defaults have one source,
  and a `ToRules()` copies them into the pure struct. The ini section is
  `[/Script/DeepSpace.ShipDressingConfig]` in `DefaultGame.ini`, and it lists
  only what is being tuned. This is exactly the shape of procgen decision 14,
  chosen for the same reason. The tables (mixes, colour weights, templates)
  mirror as arrays of small `USTRUCT` rows declared in the config's header,
  never in the rules' header, so the pure struct carries no reflection.
  *As built:* the tables are **overlays by name**, not whole mirrors. A
  `Kinds`, `Templates` or `Colours` row replaces the code's entry of the same
  name or adds a new one, and a `Kinds` row with no `Mix` keeps the code's
  mix, so moving one λ is one line. An ini array key replaces the whole
  array it names, so mirroring the tables whole would have made a one-mug
  tune restate the catalogue. What the ini may say is held to
  `DressRuleDomain` (`ShipDressing.h`): a Beta shape under 0.1, a λ over
  7.5, a mix naming no template, a part that does not rest on z = 0 or an
  unknown mesh refuses the whole read, and the rules in use stay, as
  procgen's priors do. `ds.Dress.Reload` shares `GameIniReload` with
  `ds.Universe.ReloadPriors`.
- **Guarantees stay in code, not in the ini.** These are the Poisson `Max` of
  8, the eight redraws before an item is dropped, containment inside
  `[half, L − half]`, the excludes, and `Clear`. No ini edit may be able to
  put a mug through a shelf.
- **Console commands.** These are registered in the `.cpp` with
  `FAutoConsoleCommandWithWorldAndArgs`, so they cost no header change:
  - `ds.Dress.Reload` re-reads the ini section and redresses the running
    world.
  - `ds.Dress.Redress [seed]` redresses, using another seed if one is given.
    With no argument it goes back to the world's own seed.
  - `ds.Dress.LivedIn <x>` is a `TAutoConsoleVariable<float>`. It is read at
    every dress and never cached, and setting it redresses.
  - `ds.Dress.Describe` logs every surface and its items, one line each, for
    the reading step.

`Redress` destroys the actor it spawned and restores the furniture's original
materials before it draws again, so it is idempotent. Redressing with the
same seed gives the same instances, and a test checks that.

That gives a loop in three tiers:

1. **A number or a table**: a λ, a weight in a mix, a Beta shape, a
   template's size, or a whole new template. This is an ini edit and
   `ds.Dress.Reload`. It takes seconds, happens inside the playtest, and is
   **faster than the Python loop it replaces**, which needed a level build
   for every look.
2. **A new surface or an excluded strip.** This is a Python change, then
   `build_hauler.py` and `verify_level.py`: a level build, with no compile.
3. **A new rule**, such as a sampler used differently, a new band, or stacking
   done another way. This needs a compile and a restart, and it is batched
   with any other C++ change waiting for one. If rule changes keep arriving
   more often than once a playtest, that is the signal to turn that rule
   into data, not to find some other way to shorten the loop.

The point of the first tier is that it never throws the session away. A
playtest can hold the ship still, stand at the galley table, and move one
number at a time.

**Rejected: a data asset for the rules.** It is a `.uasset`, so it cannot be
diffed, a collaborator cannot read it, and it can only be edited with the
editor open (ADR 0002). An ini is text that any agent can edit and review
while the editor is running. **Rejected: console variables for everything.**
They suit the two scalars you poke during a session, `LivedIn` and the seed.
A five-weight mix for each of eight surface kinds is a table, and forty
CVars would be a worse ini. **Rejected: templates in C++ source, rebuilt for every change.** A mug's
height is exactly the kind of number a playtest moves. It is no more a rule
than λ is.

### 1d. Tested headlessly, and more thoroughly than the Python could be

The core is pure, so most of its tests need no world. They follow the style
of `ShipPowerStateTest.cpp`, and use `Tests/DressingTestFixtures.h`: the
hauler's surface kinds and sizes typed in as literals, one surface with an
exclude, and one tall enough to stack on. That file is test data, not a
generator, which is the rule the sky's `SkyTestFixtures.h` follows (plan
conflict 4). If the layout's surfaces change shape, the fixture is updated by
hand. Nothing breaks if it lags, because containment is checked against each
surface's own size, whatever that size is.

- **`DeepSpace.Ship.Dressing.Determinism`**: the same surfaces, seed and
  rules give identical plans, compared exactly. Shuffling the surfaces gives
  the same items, compared keyed by *(Room, Kind, Ordinal)*. A different seed
  changes something. Removing the bunk's surfaces leaves every galley item
  exactly where it was, which is stream isolation (decision 4).
- **`DeepSpace.Ship.Dressing.Corpus`**: 500 seeds. For every seed, each
  item's footprint lies inside `[half, L − half]` of its surface, no two items
  on a surface overlap, none covers an exclude, none stands taller than
  `Clear` (stacks included), no surface holds more than its cap, and
  `LivedIn = 0` places nothing. This is ADR 0006's generator property test
  for the dressing. In C++ it takes milliseconds, not the minute the second
  draft budgeted.
- **`DeepSpace.Ship.Dressing.Shape`** checks that the priors are the ones
  decision 3 says. It tests the candidate draws before overlap rejection, so
  rejection cannot skew them. Across the corpus: the position along a `+y`
  surface has mean 1/3 ± 0.02 (Beta(2, 4)), the depth towards the back edge
  has mean 2/3 ± 0.02 (Beta(4, 2)), and wear comes out about 11% replaced and
  11% faded, each ± 3%. The corpus uses fixed seeds, so the test is
  deterministic, not flaky. It exists so that nobody quietly turns a Beta
  back into a `Unit()` because it looked simpler (ADR 0008's amendment).
- **`DeepSpace.Ship.Dressing.Catalogue`**: every template's parts have
  positive sizes and sit on or above its origin. Every part names a role
  whose `MI_Ship_<role>` loads. Every mesh loads. Every item in every mix
  fits on at least one surface of its kind; otherwise a mug taller than every
  shelf it may go on would be dropped silently, forever. The material check
  is the dressing's version of the sky's material contract test: a misspelt
  role fails here rather than rendering in the default material.
- **`DeepSpace.Ship.Dressing.World`**: a game world with three markers
  spawned and tagged, one untagged marker (to prove the tag is doing the
  work, as in `PowerConsumers`), and two tagged furniture parts belonging to
  one piece. Everything is spawned before `World->BeginPlay()`. Then:
  - there is exactly one `Dress.Clutter` actor;
  - its instance count equals the core's plan for the tagged markers only;
  - every component is query-only and blocks `ECC_Camera` alone (amended,
    decision 5);
  - **every instance's world bounds, measured from its mesh's bounding box
    and its instance transform, lie within its marker's surface and under
    its `Clear`**. This is the pivot bug's test in a new costume: ADR 0006
    warns that moving to instanced components is exactly where that bug
    comes back;
  - both furniture parts wear the same material;
  - calling `Redress()` twice leaves one actor and identical instance
    transforms.

What is not tested headlessly is whether the result reads as somebody's
ship. That is step 8's question.

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

Wall bands are derived and never listed. `placement.wall_bands(plan, room,
depth)` walks each wall of a room and subtracts every door and seal opening
widened by `KEEP_CLEAR`, the console's keep-clear, and every existing prop
footprint grown by 20 cm. What is left is a set of 1-D segments along which a
floor or wall item may be placed. They reach the C++ the way surfaces do, as
tagged markers (decision 1b), when item 8 lands.

Rooms declare what they accept in `ROOM_DRESSING` in `hauler_layout.py`.
`resolve_surfaces` exports a marker only for what a room accepts, so a room
that takes nothing has nothing for the C++ to find:

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

- **How many items on a surface: Poisson(λ × LivedIn, Max = 8), and the
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
  drawn as repeated `Chance(0.5)` rather than by a sampler of its own
  (decision 4). Each extra item on a stack is one more independent decision
  to put it there. A stacked item rests on the one below it, and the whole
  stack must fit under `Clear`.
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
  snapshot of how things stand when the game opens. It is drawn once per
  piece, by the subsystem at world start (decision 1a).

Collisions within a surface are the one place rejection stays, as a floor.
A candidate whose axis-aligned footprint overlaps an item already placed is
redrawn up to 8 times and then dropped. Positions are drawn inside `[half,
L - half]`, so an item cannot overhang its surface to begin with. Items
turn in 90° steps only, and books alternate 0° and 90° on a stack so they do
not look extruded. The original reason for the steps was that the validator
voxelises everything axis-aligned. That no longer applies to surface clutter,
which the validator never sees (decision 5); see *What is faked*.

### 4. The generator's randomness is `GenSeed` and `FGenStream`, and nothing else

The dressing calls procgen's library directly. It adds **no sampler that
`FGenStream` lacks**. Counts are `Poisson`, positions are `Beta`, what goes
where and in which colour is `Categorical`, and stack height is `Chance(0.5)`
repeated up to the cap: a geometric count built from something the stream
already has. `Poisson` at `Max` is procgen's, exactly as written (plan
conflict 11).

Streams are derived by label, exactly as in procgen decision 2. A room's
stream is `Derive(DressSeed, Label("dress." + Room))`, and a surface's is
`Derive(RoomStream, Label(Kind), Ordinal)`, so adding clutter to the bunk
does not reshuffle the galley. Labels built at runtime from `FName`s go
through the same `GenSeed::Label`, over their ASCII bytes. `resolve_surfaces`
rejects any room or kind that is not plain ASCII, so the hash cannot depend
on a text encoding.

The dressing has no known values of its own. Its priors will keep moving
through the POC, and a pinned "golden" plan would fail at every tune. The
part that must not drift is the RNG, and procgen's known-value tests already
pin that for everyone.

This keeps procgen decision 10 rather than making an exception to it. There
is no Python dressing generator and no Python mirror of the RNG.

**Rejected (second draft): the dressing drawing from `Tools/rng.py`, a
literal Python mirror of `GenSeed` and `FGenStream`, pinned to the C++ by
`Tools/rng_vectors.json`.** The mirror existed here so that a Python dressing
could draw from the C++ stream. With the Python dressing refused (decision
1), the lived-in ship reads neither file. The plan keeps both as an
independent cross-check on `FGenStream`'s known values. That is a check on
the library, not the basis of a generator, and nothing may be built on top
of it: a Python dressing drawing from `rng.py` would be exactly the twin
this ruling refused.

**Rejected (first draft): an RNG of the dressing's own.** That draft derived
streams by FNV-1a-hashing each path element into the seed, computed Beta
with integer parameters as the a-th order statistic of a + b − 1 uniforms,
and had a `poisson` with no `Max`. A second `Derive` and a second Beta beside
`FGenStream`'s would be two generators' worth of RNG, which ADR 0006 forbids.

**Rejected: adding integer Beta to `FGenStream` as a named sampler.** Every
Beta used here has parameters the Gamma route handles well. Two Betas in one
library invite someone to pick the wrong one by accident.

### 5. Clutter is static and inert. Only what the capsule can meet collides, and the validator models exactly that

Every clutter item is an instance on the dressing's one actor (decision 1a),
and nothing is simulated. Nothing can be picked up, and nothing moves when the
ship turns, which is correct while the ship only cruises (vision, *Flight
modes*). Collision follows where the item sits, and the subsystem derives it
from the band, per instance component. It is never listed per item:

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
  validator, which allows a rise of one cell, treats as walls. The catalogue
  test (decision 1d) requires every floor-band template to be at least 12 cm
  tall and at least 10 cm on each footprint axis, so nothing that collides is
  smaller than a cell.
- **The validator rasterises dressing conservatively.** Any cell that a
  colliding dressing box overlaps at all counts as solid, instead of only
  the cells whose centres fall inside it. Walls and furniture keep
  cell-centre sampling, so existing validation is unchanged. The effect is
  that the validator can only think the floor more blocked than it really
  is. It cannot miss a pocket. It can report one the game does not have,
  which is the safe way to be wrong.

`UShipDressingSubsystem` sets the collision profile and the step-up flag on
each instance component from its band, and `DeepSpace.Ship.Dressing.World`
checks both. The floor-band boxes the validator rasterises are the C++
generator's own, exported for it (decision 6), so the validator is checking
what the game builds. `validate_hauler.py`'s module docstring gains a
*Model* paragraph listing what the grid does and does not represent: colliding dressing,
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

**Amended, 2026-09-26 (slice 3 review): surface clutter blocks
`ECC_Camera` and nothing else.** The bullet above was wrong about the
camera. The capsule cannot reach a surface, but the eye can: the crouch
carries it about 30 cm past the capsule at about a metre up, level with the
workbench, the counter and the galley table, and standing at the cargo rack
it is inside a shelf-1 crate's band. With `NoCollision` the eye sweep in
`ADeepSpaceCharacter::PlaceCamera` passed straight through, and the view
could sit inside a toolbox. The sweep stopping at a mug is not a cost; it is
the sweep doing for clutter what it already does for walls. So clutter is
`QueryOnly`, ignores every channel, and blocks `ECC_Camera`. The laptop
cursor and the `E` trace (`ECC_Visibility`) still pass through it, and the
capsule (`ECC_Pawn`) still never meets it. `DeepSpace.Ship.Dressing.World`
checks the responses, and `DeepSpace.Player.CameraStaysOutOfClutter` leans a
crouched eye into the hauler's own clutter.

### 6. A new intent check: floor-band dressing may not create pockets

Region reachability only tests named points. Floor clutter can cut off a
corner of floor without failing any of them, and that corner will feel like
a bug the first time the player tries to walk into it.

The floor-band clutter is generated in C++, and the validator is Python, so
the seam is procgen decision 10's: Python only reads what the C++ wrote. An
automation test, `DeepSpace.Ship.Dressing.Export`, dresses the hauler's
exported bands for seeds 0 to 49 and writes each colliding item's world box
to `Saved/dressing_floor.json`. `validate_hauler.py --dressing` reads that
file. There is still one generator and still one validator, and the
validator checks exactly the boxes the game will build.

`validate_hauler.py` gains `check_dressing_pockets(ship, boxes)`. It builds
the grid twice, once from the bare ship and once with the exported boxes,
and floods standing reachability in both. A pocket is a cell that is standable and at floor level
in the dressed ship, was reached in the bare ship, and is not reached in the
dressed one. Cells that a dressing item occupies (conservatively, per
decision 5), or covers within the capsule's radius, are not standable and do
not count. Any pocket fails, and the message gives its location and the
nearest dressing label. The check costs one more grid build, about a second.

Because surface and wall dressing do not collide, only the floor band (item
8) can make a pocket. The check therefore lands with item 8, behind the
second cut. Each exported box carries its item's surface key, so the
failure message can name the nearest dressing without guessing.

Determinism, containment and exclusions are `DeepSpace.Ship.Dressing.Corpus`
(decision 1d), from the second cut on. Anchoring is a layout property, so it
stays in `test_placement.py`. Once item 8 lands, the export above also feeds
a full validation of seeds 0 to 49. That takes about a minute, in a single
Python process under `nice -n 19` with no pool. It is ADR 0006's generator
property test at full strength: every seed passes, and the same seed always
gives the same boxes. It is sized deliberately and is not a sweep sized to
the core count.

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
change. *That last claim was false; see Amendment 1 below. It keeps the
colour in one place, but the brown-out needed a C++ change all the same.*

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

**Amended, 2026-09-25: the drone follows the watts reaching the jump drive.**
`EngineFeed` is no longer the engine's satisfaction. Navigation makes an idle
engine want 0 W, and a zero want is fully satisfied, so a satisfaction-driven
drone would sit at full feed whenever the ship is idle and ignore the split.
`EngineFeed` is `clamp(GetConsumerShare(Engine) / ds.Nav.WindingWant, 0, 1)`
instead: the watts actually delivered, over what winding asks for (plan
conflict 8). Idle, the drone sits in its low register. It rises and brightens
as a winding jump draws power, and settles back once the jump is charged. The
developer accepted this, to be judged by ear in playtest. The voice's table
is unchanged. Only its input changes.

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
that `UShipDressingSubsystem` sets at world start from `Derive(Ship,
Label("clock"))`: a day count of 200 + Poisson(20, Max = 60), and a time of
day. It then advances with world time. A seeded draw belongs to the one
generator, not to the level builder. A clock is not a timer. Nothing in the
game is scheduled against it.

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
  `furniture_replaced`. `build_hauler.py` authors them as `PANELLED`
  instances like every other role, and tags each furniture part `Dress.Wear`
  and `Piece.<prop>_<n>`. The subsystem draws the bucket and swaps the
  material at world start (decision 1a). `Place` gains nothing: the bucket is
  a seeded draw, so the layout has no say in it.
- **New plain roles** for clutter: `ceramic`, `metal`, `rubber`, `paper`,
  `fabric_{olive,navy,rust,ochre}`, `paint_{red,yellow}`. Each is a
  `PANELLED` instance with its seam colour set equal to its surface colour.
  That is a plain colour with no new material asset. `build_hauler.py`
  authors them even though nothing in the level uses them, because the
  dressing selects them by name at runtime. That is exactly ADR 0006's
  runtime path: pre-authored instances, selected and parameterised.

**Rejected: a procedural grime material** (world-position noise, darker
towards the floor). It is the better-looking answer, but authoring a
material graph through `MaterialEditingLibrary` is fragile, and the kick
band gets most of the effect as data. Revisit when art is on the table.

### 13. Stretch goal: desire lines, where the floor is worn by where you walk

`placement.traffic(plan, grid)` runs a breadth-first shortest path between
every pair of standing regions on the validator's stand grid and counts how
many paths pass through each cell. Cells above the 80th percentile become thin
(0.5 cm) `floor_worn` boxes, merged by `merge_rectangles`. They have no
collision (decision 5), so they are outside the validator by rule. The first
draft said they were invisible because they were thinner than a cell. That
was true of these boxes, and it was the same reasoning that made the first
draft of decision 5 false. The result is wear that follows use, which is
the real history of a ship. It has no seed, so it is layout rather than
dressing, and it stays in Python with the walls until ADR 0006's port. This
is item 10: build it only if everything else has landed and time is left.

## What is faked, and what it costs later

- **Surfaces reach the C++ as markers in a level Python built** (decision
  1b). In ADR 0006's end state, the ship generator hands the dressing its
  surfaces directly. Cost later: delete `AShipDressingSurface`,
  `resolve_surfaces` and the verifier's marker check. `ShipDressing` does not
  change.
- **Clutter exists only in play.** The editor's own world never begins play,
  so the viewport shows bare surfaces, and seeing the dressing means pressing
  Play. Cost later: a redress in the editor world that spawns a transient
  actor, if the bare viewport is ever missed.
- **Clutter's assets are found by path string.** The meshes and the
  `MI_Ship_<role>` instances are loaded by path, which a cook cannot see.
  That is fine while the POC runs in the editor. Cost later: soft object
  references in `UShipDressingConfig`, or the materials directory listed in
  `DirectoriesToAlwaysCook`.
- **Items turn only in 90° steps.** Surface clutter no longer needs this.
  Nothing on a surface is voxelised, so a small yaw jitter (a truncated
  Normal of a few degrees) is one line in the core, and it is the first thing
  to try if stacks look too neat. Floor-band clutter keeps the 90° steps,
  because the validator voxelises it. Cost later, for floor bands: oriented
  boxes in the validator, or over-approximating with bounding boxes (the
  approach already used for cylinders).
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
- **Wear is a material swap on furniture baked into the level** (decision
  1a). The furniture comes from the level, and the dressing changes its
  material at world start. Cost later: none. When the ship generator builds
  the furniture itself, it takes the bucket from the dressing instead of
  swapping it.
- **Surface and wall dressing have no collision.** Cost later: picking
  things up, or clutter that slides during combat manoeuvring, needs the
  collision back and a physics model the validator does not have.
- **The hum's `EngineFeed` answers to a jump charge that nothing consumes
  yet** (interactable-ship addendum). The sound is honest about the share,
  even though the share's effect is still just a number climbing.
  Navigation's jump now consumes the charge, and decision 9's amendment
  moves the drone onto the watts it draws, so this fake ends when navigation
  lands.

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

**To the second cut** (about 13 h more, and one compile):

5. **Surfaces and markers** (item 3, first half; Python). `props.py`:
   `Surface`, `SURFACES` and `ANCHOR`. `placement.py`: the anchoring check,
   and `resolve_surfaces`, which computes the laptop's exclude and the
   chart's (plan conflict 16) and rejects non-ASCII rooms and kinds.
   `hauler_layout.py`: `ROOM_DRESSING`, and `ship.surfaces` returned by
   `generate()`. `build_hauler.py`: `place_surfaces`, spawning one
   `AShipDressingSurface` per surface, tagged `Dress.Surface`; the tags
   `Dress.Wear` and `Piece.<prop>_<n>` on every furniture part; and the
   clutter roles and both furniture buckets in `PANELLED`. `verify_level.py`:
   the marker check (decision 1b). `test_placement.py`: an anchoring test
   that fails on a floating counter; a check that every surface lies inside
   its prop's footprint at its prop's height; and a check that the galley
   table's surface carries the laptop's exclude. *No editor.* About 2.5 h.
   `place_surfaces` needs step 6's class compiled, so it joins the first
   level build after that compile.
6. **The dressing generator** (item 3, second half; C++). New files:
   `Ship/ShipDressing.{h,cpp}` (the core, `FDressSurface` and `FDressItem`);
   `Ship/ShipDressingRules.{h,cpp}` (every tunable with its default, and the
   clutter templates `mug`, `plate`, `bottle`, `tin`, `tablet`, `book`,
   `toolbox`, `cable_coil`, `part`, `canister`, `crate_small`, `boots` and
   `blanket`); `Ship/ShipDressingConfig.{h,cpp}`;
   `Ship/ShipDressingSurface.{h,cpp}`; and
   `Ship/ShipDressingSubsystem.{h,cpp}`, with the `ds.Dress.*` console and the
   wear swap. Tests: `Tests/ShipDressingTest.cpp` (Determinism, Corpus, Shape
   and Catalogue), `Tests/ShipDressingWorldTest.cpp`, and
   `Tests/DressingTestFixtures.h`. No existing header changes, and
   `DeepSpace.Build.cs` needs nothing, since instanced meshes are in
   `Engine`. So it compiles with `./build.sh`, not `--force`. Writing it
   needs *no editor* and takes about 7.5 h. Compiling it is **[editor]**.
7. **Wear** (item 5). `floorplan.py`: the kick band, with `test_floorplan.py`'s
   wall expectations updated to match. The furniture buckets need no work
   here: step 6 draws them, on the tags and roles step 5 adds. *No editor.*
8. **[editor] Compile, test, build, verify, playtest.** In this order:
   `./build.sh`; the automation run (`DeepSpace.Ship.Dressing.*` and
   everything already there); `validate_hauler.py`; the `build_hauler.py`
   commandlet; `verify_level.py`, which now checks the markers too;
   `check_blueprints.py`. Then play. Walk every room at `ds.Dress.LivedIn`
   0.3, 1 and 2, and try `ds.Dress.Redress 1`, `2` and `3`, all in one
   session with no level rebuild. Tune with ini edits and `ds.Dress.Reload`,
   and write the settled numbers back as code defaults once, at the end. The
   questions: does it read as somebody's ship, does anything look placed
   rather than left, and does the laptop's view stay clear? **Second cut.**

**Behind the second cut:**

9. **Wall dressing and floor bands** (items 7, 8). `placement.py`:
   `resolve_runs(plan, runs)` for handrails and cable runs, which splits a
   run at every opening (with a 10 cm margin), puts brackets every 120 cm,
   and labels boxes `prop_run_<name>_<n>_<k>` so the keep-clear check covers
   them. Runs are not seeded, so they stay layout. `placement.wall_bands`,
   exported by `build_hauler.py` as band markers tagged `Dress.Band`. In
   C++: the band dresser in `ShipDressing`, the `note`, `photo`, `jacket` and
   floor-band templates in the rules, and `DeepSpace.Ship.Dressing.Export`
   (decision 6). `validate_hauler.py --dressing`: conservative rasterisation
   of the exported boxes, `check_dressing_pockets`, and the *Model*
   paragraph, run over seeds 0 to 49 in a single process under `nice -n 19`.
   Writing: *no editor*. About 8 h.
10. **[editor] Compile, export, validate, walk.** `./build.sh`, then the
    export test, then `validate_hauler.py --dressing`, then the level build
    for the band markers. The question: does any floor clutter get in the way
    of walking?
11. **C++ batch B: readouts** (item 9). New: `Ship/ShipReadout.{h,cpp}` and
    `UI/ShipReadoutWidget.{h,cpp}`. In the layout tools, `Mount` gains
    `along`, and `validate_hauler.py` gains `check_mounts_on_wall`. Test: `DeepSpace.Ship.Readouts`. The speed readout's text
    matches `GetShipSpeed()` formatted, after a known velocity is set through
    the flight-command path. The reactor readout's text contains exactly one
    number. No readout's text contains `%`. The last two are the anti-chore
    rule written as a test. Writing: *no editor*. Then **[editor]**
    `./rebuild.sh --force`, spawn `READOUTS` from `build_hauler.py`, build,
    verify, `check_blueprints.py`, the automation run, and a look.
12. *(Stretch.)* **Desire lines** (item 10): `placement.traffic`, a
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
  likely source. The export and `validate_hauler.py --dressing` find them
  before the developer does. The fix is to shrink the band or lower λ, not to loosen
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

- **The markers and the furniture disagree.** Both are written by the same
  `build_hauler.py` run, and the verifier compares the markers with the
  layout. The failure that remains is verifying an old map against a new
  layout, which the risk above already rules out.
- **The rules keep changing shape.** If playtest after playtest asks for a
  different rule rather than a different number, every one of them is a
  compile (decision 1c). The answer is to turn that rule into data. The
  warning sign is a second rule change between two playtests.
- **`ds.Dress.Reload` has to read the file, not the cache.** Unreal reads
  `DefaultGame.ini` into `GConfig` at start-up, and reloading an object's
  config from `GConfig` alone would miss an edit made on disk since. The
  reload must re-read the file first. Procgen's `ds.Universe.ReloadPriors`
  has exactly the same need, and the two should share whichever mechanism
  is proven first. A test edits nothing on disk, so this is checked by hand
  the first time the loop is used: change one λ, reload, and see the count
  move.
- **A runtime `SetMaterial` on furniture saved as Static.** The wear swap
  changes the material of level actors whose mobility is Static. Setting a
  material is not a transform change, so it should be allowed. If it turns
  out to be refused or ignored, the fallback is for `build_hauler.py` to
  spawn furniture as Movable. That costs nothing here, because every light
  in the ship is already Movable and nothing is baked.
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
- Porting the *layout* to C++ (ADR 0006): walls, furniture, lights. The
  dressing is C++ from the start. The layout stays built by `build_hauler.py`
  until that port, and hands the dressing its surfaces as markers until then
  (decision 1b).

## Review, 2026-09-25

A review of the first draft raised six issues. All six were accepted, and the
design was changed for each. The superseded choices are kept in place as
rejected alternatives.

| # | Issue | Where it is answered |
|---|---|---|
| 1 | The reactor readout `1000 W out · 740 W drawn` is a utilisation meter, the vision's named failure | Decision 11: a nameplate showing only the rating, and a test that it carries one number |
| 2 | The hum's "load" is flat by construction under any split | Decision 9: engine feed drives the pitch and the third partial, and booster push drives the hiss |
| 3 | `rng.py` was a second derivation and sampler library that disagreed with `GenSeed`/`FGenStream` | Decision 4: a literal mirror, procgen's known values as vectors, and the exception to procgen decision 10 argued explicitly. *Since superseded by the ruling below: there is no mirror at all* |
| 4 | The cut line and the build order contradicted each other, and the hours were optimistic | *What is ranked*: hum first, two cuts, hours re-estimated, the single C++ batch rejected |
| 5 | "Solid in the grid is solid in the game" was false for sub-cell clutter and for step height | Decision 5: collision by band, no step-up, conservative rasterisation, and the validator's model written down |
| 6 | Two `HumVoice` assertions tested properties the synthesis did not have | Step 3: parameter trajectories, an analytic no-click bound on the tonal voice, and RMS asserted only for terms that exist |

## Ruling, 2026-09-25

The integrated plan asked the developer (its decision 5) to accept the
dressing's Python generator as a deliberate exception to ADR 0006. **The
exception was refused.** The dressing generator is C++, running at startup
like every other generator in the game, and there is exactly one of it.
`Tools/dressing.py` and the approach of baking clutter into the level are
rejected. This revision turns the clutter half of the spec into a C++ design
and leaves the rest alone.

| What changed | Where |
|---|---|
| Clutter is planned by `ShipDressing` and spawned as instances by `UShipDressingSubsystem` at world start. `dressing.py` and baking are the rejected alternative | Decisions 1, 1a |
| Surfaces reach the C++ as `AShipDressingSurface` markers, found by the `Dress.Surface` tag | Decision 1b |
| The cost of the tuning loop is stated, the rules are made data in the ini, and a running session redresses in place | Decision 1c |
| Headless tests: determinism, the corpus, the shape of the priors, the catalogue, and the world | Decision 1d |
| The dressing uses `FGenStream` directly. `rng.py` and `rng_vectors.json` are no longer the dressing's. They survive only as the plan's cross-check on `FGenStream` | Decision 4 |
| The wear buckets are drawn in C++ at startup. The kick band stays in `floorplan.py` | Decisions 1a, 12 |
| The pocket check reads the C++ generator's floor-band boxes | Decision 6 |
| The ship clock's origin is drawn by the dressing subsystem, not set by the builder | Decision 11 |
| The drone follows the watts reaching the jump drive (plan conflict 8, accepted by the developer) | Decision 9 |
| **Unchanged**: the lighting moods (7), the practicals (8), surfaces and bands (2), the distributions (3), collision by band (5), the lamp panels (10), the readouts apart from the clock's seed (11), and what wear looks like (12) | — |

## Amendments from building slice 1, 2026-09-25

A review of the moods branch found two things this spec got wrong.

**1. The brown-out is relative to the lamp's rating (decision 7).** The
subsystem lerped every light's `RatedColour` to one fixed amber, linear
(1, 0.62, 0.30). That was right when every lamp was a neutral-cool white. The
moods made the bunk (2700 K), galley (2900 K), crawlway (3200 K),
engineering (3600 K) and all three practicals *warmer* than that amber: the
bunk is (1, 0.386, 0.095) linear. So a starved bunk went whiter and bluer,
the opposite of a filament running cool. `UShipLightingSubsystem` now slides
each lamp down its own temperature instead. Red holds, and green and blue are
scaled by `Lerp(1, 0.62, Depth)` and `Lerp(1, 0.30, Depth)`, so a white lamp
lands on the old amber and a warm one goes deeper amber than it was.
`DeepSpace.Ship.BrownOutRunsWarmer` starves a 2700 K lamp and a 6200 K one
and checks that B/R, G/R and B/G all fall. Decision 10's lamp panels take
the same function applied to their own rated `Colour`, and not a fixed target.

**2. Lamps stand on dressing surfaces (decisions 1b, 2).** The practicals
put the desk lamp's base on `desk.top`, at world (948, -118), and the bench
lamp's base on `workbench.top`, at (788, 240). The counter moved into
`PRACTICALS` because its strip is a lamp. Two rules follow for step 5:

- `hauler_layout.PLACEMENTS` is the one complete list: `FURNITURE` plus every
  practical's `place`. `resolve_surfaces(plan, placements)` is given
  `PLACEMENTS`, never `FURNITURE`, or the galley counter, the ship's main
  dressing surface (affinity 4), and its `Ordinal` disappear.
  `test_placements_is_every_prop_lamps_included` holds `generate()` to it.
- `resolve_surfaces` computes an exclude for **every placement that rests on
  another prop's surface**: its footprint plus 15 cm, the same margin as the
  laptop's. That is the laptop's rule made general, not a list of special
  cases, so a lamp moved in the layout moves its exclude with it. Today it
  gives `desk.top` one exclude (the desk lamp's 14 cm base) and
  `workbench.top` a second one beside the screen's (the bench lamp's 10 cm
  base). `test_placement.py` gets a check that no surface marker's free area
  overlaps a practical's lowest part.
