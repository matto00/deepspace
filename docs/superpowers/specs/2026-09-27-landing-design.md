# DeepSpace — Landing, Slice 1: Fly Down and Set Down

**Date:** 2026-09-27
**Status:** Approved by the developer, 2026-09-27: every item of
*Decisions needing sign-off* as recommended. Not implemented.
**Answers:** the third playtest, and the developer's rulings on landing
recorded below
**Follows:** flight feel (implemented; its decision 6 named the floor
"landing's door"), the system map (implemented; its decision 9 set what
landing may and may not assume), the sky (implemented; its non-goals said
landing "replaces the proxy with real terrain in the ship's frame")
**Depends on (in flight, unmerged):** `feat/speed-bands` (cruise 0-20
km/s on a log lever at 2 km/s^2, drive 20 km/s-0.1 c) and
`fix/surface-artifacts` (the 10-15 km tearing: root cause found, fix in the
worktree; see *Context*)
**Builds on:** ADR 0002 (logic in C++), ADR 0003 (ship state as a
subsystem), ADR 0005 (the ship is the origin; amended here), ADR 0006 (one
generator, C++), ADR 0007 (chunked coordinates, hierarchical seeds), ADR
0008 (shape the prior)
**Governed by:** `docs/vision.md`: *planets are worlds, at Earth's scale*,
*no loading screens*, *approach takes time, and the time is the content*,
*the anti-chore principle*; CLAUDE.md: *Flying*, *The drive and the jump*,
*The sky*, *Screens, the pointer, and power*

## The developer's rulings, 2026-09-27 (third playtest)

These are binding. Everything below is built on them; where a decision
below goes past them, it says so and is on the sign-off list.

1. **Landing slice 1 is fly down and set down.** Below the 10 km floor the
   pilot flies by hand to the surface, touches down, and the ship rests on
   the ground with the landscape through the windows; then takes off again.
   The ship can be walked while landed. **No going outside.** Solid surfaces
   only: rock, ice, and the land of terrestrial worlds. **Oceans and gas
   giants keep the floor.**
2. **Descent is flown on vertical thrust keys** -- not an auto-flare, not a
   hover mode. The keys set a **climb or sink rate**: a third lever, a
   vertical speed the boosters hold against gravity. At zero the ship
   hovers, and keeps hovering if the pilot stands up. Proposed keys: Space
   up, C down, at the helm, to be confirmed free.
3. **The ground always catches you.** The soft cap carries through to the
   surface: whatever the pilot does, the ship eases to a gentle touchdown.
   No crashes, no damage. At touchdown the ship settles onto the ground's
   slope in the last few metres, and the pilot flies attitude all the way.
4. **Real gravity, everywhere, from every body** -- the star and every world,
   inverse-square, at real masses. **The boosters always hold every lever
   against gravity**, so at rest the ship stays put anywhere. Gravity is felt
   as effort: booster load in the power split, the hum, slower climbs on
   heavy worlds, a gentle sink if the boosters are starved (and the ground
   still catches).
5. **One height function**, in C++ (`WorldRelief`), from the body's procgen
   seed. The orbital material and a real terrain mesh near the ship both read
   it: cube-sphere quadtree tiles that subdivide as the ship descends, built
   asynchronously. **Flight collision is an analytic height query, not
   physics meshes.** What you saw from orbit is what is there.
6. **Relief is Earth-like**: mountains to about 8-10 km on rock and ice,
   lower on terrestrial worlds, varying by world.
7. **Black sky.** Atmosphere is its own later sub-project.
8. **The drive keeps its 10 km floor.** Below it, cruise and the vertical
   lever; their floor is the ground plus gear clearance.
9. **Worlds do not spin** in this slice.
10. **HUD below 10 km:** altitude above ground, vertical speed, LANDED.
11. **Three playable slices:** (a) the shared height function, orbital look
    unchanged; (b) terrain, gravity and the vertical lever: fly down to hover
    over real ground; (c) touchdown and LANDED.
12. **Also ruled today, built separately** (`feat/speed-bands`): cruise 0-20
    km/s on a log lever from about 1 m/s, detent at zero, astern capped at
    200 m/s, 2 km/s^2; the drive 20 km/s to 0.1 c in 11 notches; the dust's
    top at 0.1 c. **This spec is written against those numbers.**
13. **Workflow:** this spec ends with *Decisions needing sign-off*; the
    developer approves it before any code.


**Ruled later the same day, on the plan:** the height is normalised by the *measured* maximum of the detail sum (at least 200,000 samples), under a smooth hard cap at `PeakCm`, not by the proven worst case, which is about three times loose and would have drawn peaks at a third of the Earth-like heights of ruling 6. `MaxHeightCm` is still exactly `PeakCm`.

**Ruled after the spike (R1), same day:** parity is judged per footprint and per term. Values, and every term at the coarser footprints (1/12, 1/96, 1/768), are held to 1e-3; at the two finest footprints (1/3072, 1/12288) slopes are held to the measured float floor, 5e-3, since there every float evaluation -- the engine's own nodes included -- differs from double by 1e-3 to 5e-3 at those noise coordinates (spike: shared vs engine 3.11e-3, C++ double vs engine 3.51e-3; the coarse band bit-exact). The step mask counts only held crater sites, and bisectors only where a held site lies within 1.5 radii (0.467% left out, under the 1% cap).

**Ruled at R2, same day (all 18 bands):** the float floor reaches past the R1 ruling, since the crater offsets put even the coarsest crater band near noise coordinate 8,000; the engine's own nodes miss double by as much. Parity is held to the *measured* floor: every value and every detail term at 1/12, 1/96 and 1/768 to 1e-3; crater slopes to 5e-3 from 1/96 down; at 1/12288 detail values to 1.5e-3 and detail slopes to 8e-3; the step mask's cap is 1% *per crater band*. **Slice (b) then tightens it at the root**: the shared file keeps each band's lattice offset as an exact integer part apart from its fraction, so the GPU tracks the double C++ far below these floors at the ground's close-up footprints, the look unchanged; the parity tolerances tighten with it. **The DeepSpaceShaders module is removed** (UE 5.8 maps `/Project` to the project's `Shaders/` itself at PreInit, `LaunchEngineLoop.cpp:2557`); a test holds that the engine's mapping finds `WorldRelief.ush`.

**The measured floor as a rule (applying the R2 ruling, same day):** a term's C++-vs-GPU tolerance is 1e-3, or, where the engine's own nodes miss double by more at that footprint and term, 1.25 x that engine-vs-double error. The shared file against the engine's nodes stays held to the ruled table; failing that is a port bug and stops the build. (R2 found detail slope at 1/768, 1.18e-3 with the engine's nodes at 1.04e-3, and crater albedo at 1/12288, 1.01e-3 with the engine's at 1.38e-3.)

**The port-bug test, restated (R4, same day):** two float evaluations can each sit at the floor on opposite sides, so the shared file is not judged against the engine's nodes directly. It is a port bug only if the shared file misses double by more than 1.25 x what the engine's nodes miss it by, at any footprint and term (R4 on Baemsekai IV, 1/768 detail slope: shared vs double 1.27e-3, engine vs double 1.25e-3; shared vs engine 1.01e-3 -- the floor, not a bug). Shared vs engine is still printed.

**Confirmed by the developer (same day):** the ruled table stays the minimum under the 1.25x rule (the ratio is a necessary condition for a port bug, not a sufficient one), and after R5 retired the engine's nodes their measured distance from double is frozen as `EngineFloor` constants per footprint and term. Slice (a) merged as `8cd12ab`, kept; the developer's in-play look at Baemsekai III, IV and V (and IV at dusk) is carried to the next playtest.






## Context

### What exists, and what does not

- **Nothing integrates gravity.** `FShipFlightState` changes velocity only
  by the drive's set (`DriveSubStep`, no inertia) or cruise's chase
  (`CruiseSubStep`, a vector-limited change of `LinearAcceleration x dt`).
  `LastLinearAcceleration` is kinematic, and the hum reads it.
- **The flight law knows only spheres.** `FFlightSurface {Centre, Radius,
  Floor, bInsideOut}`; `ShipFlight::RayToFloor` is ray-to-sphere;
  `MaySpeed = min(max(D/Hold, sqrt(2 x 0.8 x A x D)), D/Step)`. The floor
  over a world is `UShipSubsystem::FloorFor` = max(`ds.Flight.Floor` 10 km,
  `SkyProjection::RenderedFloor` = 1.6e-3 R), measured from the mean-radius
  sphere: 10.2 km over an Earth, 112 km over a Jupiter.
- **There is no height function.** The orbital relief exists only as
  material nodes in `M_SkyBody` (authored by `Tools/setup_sky_materials.py`):
  a coarse GradientALU continent band (albedo only), 12 simplex detail bands
  at 24..49,152 cycles per radius, 6 Voronoi crater bands at 12..12,288, all
  functions of `D = normalize(LocalPosition)` in the body's axes, seeded by
  `FSkyBody::SurfaceSeed` (derived from the system seed under label
  `"sky.surface"`, quantised to float offsets in [0, 256)). The "height" is
  a normal tilt, `ds.Sky.Relief (0.2) x weight x value / frequency` in
  radius units. **Read as a height it is 3-5 times the ruled relief**: the
  coarsest detail band alone is 0.2 x 0.5 / 24 R, about 26 km on an Earth,
  and the largest crater band about 74 km deep.
- **The flight side cannot tell land from sea.** `FSkyBody` has no mass, no
  `EPlanetKind`, and maps Ocean to `Rocky` with `Cratering 0`. The masses
  exist (`FStar::MassSolar`, `FPlanet::MassEarth`); so does
  `EPlanetKind {Barren, Terrestrial, Ocean, Ice, GasGiant}`.
- **Surface gravity is already implied by procgen.** Rocky radius is
  `MassEarth^0.28`, so g = M^0.44 g_E: 0.27 g at 0.05 M_E, 3.3 g at 14.9 M_E.
  Measured on the corpus (`Saved/procgen_corpus.tsv`): Barren median 9.6
  m/s^2, max 32.1; Terrestrial median 10.4, max 32.0; Ice median 12.0, max
  31.9. At a star's floor (one stellar radius up, so 2 R from the centre,
  where g is a quarter of the surface value) the Sun pulls 68.5 m/s^2 (7 g),
  home's Baemsekai (0.10 M_sun, 0.16 R_sun) about 268 m/s^2 (27 g), and the
  smallest dwarfs (0.08 M_sun, about 0.1 R_sun) about 550 m/s^2 (56 g).
  **The home system is close-packed**: Baemsekai's star alone pulls 0.31 g at
  world III (0.014 AU) and 3.9 g at world I (0.004 AU)
  (`Saved/procgen_describe.txt`).
- **No runtime mesh plugin is in use.** `ProceduralMeshComponent` ships with
  UE 5.8, enabled by default, not experimental; DeepSpace does not depend on
  it yet. Nanite cannot be built at runtime (its builder is editor-only).
- **Space and C are bound**, to `IA_Jump` and `IA_Crouch`. Seated, both are
  inert (movement is disabled; `ToggleCrouch` returns early when seated).
  `Tools/setup_flight_input.py` refuses any helm key already bound unless it
  is in `SHARES_WITH_WALKING` (today `{LeftShift, LeftControl}`).
- **The hull.** Floor at Z = 0 (the ship's origin is on the deck plane),
  a 10 cm slab under it, so the belly is at Z = -10 cm. The hull spans about
  X -810..1760 cm, Y -400..500 cm: 26 m by 9 m. There is no landing gear.

### The 10-15 km tearing, and what it means for the handover

`fix/surface-artifacts` has the root cause (committed on its branch as
`037e848`, in `.worktrees/surface-artifacts`, not yet merged). It ships no
render harness: the rendered parity probe this spec needs is its own
deliverable (decision 1, track R). **GPU Scene stores every
primitive's transform compressed** on this platform
(`FCompressedTransform`: a float translation, a 16-bit octahedral rotation
with a 15-bit spin, and the scale as a 15-bit mantissa under a shared
exponent). A sky proxy's near side is its centre minus its radius, two
numbers each R/h times the near side's distance, so a 2^-15 scale error
moves the ground by 3e-5 R/h -- 1.6 % at 10 km over a 5,400 km world, a
different 1.6 % every frame -- and the same fraction of rotation slides the
face sideways as the ship turns. The fix (`SkyProjection::RenderableScale`)
rounds each proxy's scale to one the GPU keeps exactly and moves the centre
to match, and draws proxies with an identity world rotation, turning the
face in the material through two new parameters, `BodyAxisX` and
`BodyAxisY`.

Two lessons carry into this spec. **No component may carry a large scale
or a long lever arm under a rotation**: terrain tiles are scale 1 with
vertices local to a nearby pivot (decision 6). And **the handover belongs
at 50 km, where the projection's magnification is exactly 1** (decision 7):
below it the proxy is magnified, and magnification is what amplified the
error.

A note for Unreal newcomers: a *component* is a piece attached to an actor
(a mesh, a light); its *transform* is location, rotation and scale. *GPU
Scene* is the renderer's copy of every component's transform on the
graphics card, and it is that copy, not the one the C++ holds, that the
picture is drawn from.

## Goals

- Fly down from orbit to hover over real ground and set down on it, with no
  loading screen and no jump at the orbit-to-ground handover, on any rock,
  ice or terrestrial world procgen makes. Level-of-detail pops below the
  handover are measured in 4K pixels and judged (sign-off item 16).
- One height function, so the ground under the landed ship is the ground
  seen from orbit, and the flight, the mesh, the HUD and the material agree
  by construction.
- Gravity that is real in the sums and felt as effort, never as a failure.
- A third lever that behaves like the other two: ship state, persistent,
  eased, stopped by X, spent on held keys.
- A ground that always catches, gently, from any combination of levers,
  thrust and frame rate.

## Non-goals

- Going outside; walking on the ground; a reference-frame handoff (ADR
  0005's deferred item stays deferred).
- Atmosphere, sky colour, clouds, weather, day and night by rotation.
- Landing on oceans or gas giants; water; sea level on terrestrial worlds
  (*Open questions*).
- Taxiing on the ground; landing gear as a visible mechanism; ship
  shadows on the ground; terrain shadowing the deck.
- Moons (procgen makes none), spinning or orbiting worlds.
- Damage, crashes, fuel, any failure state.

## Decisions

### 1. `WorldRelief`: one pure C++ function, and the material compiles the same source

**The height function** is `FWorldRelief` in a new
`Source/DeepSpace/Surface/` directory, pure (no `UObject`, no `UWorld`, no
CVar), constructed from `FWorldReliefParams`:

```cpp
struct FWorldReliefParams
{
    FVector3d SeedOffset;   // ShipSky::SurfaceSeed's xyz, quantised exactly as the GPU gets it
    double    RadiusCm;     // mean radius: the datum heights are measured from
    double    PeakCm;       // this world's drawn peak relief (decision 3); 0 for oceans and giants
    double    Cratering;    // how much of its craters it keeps (today's LookOf)
    EGround   Ground;       // None or Solid (decision 13)
};

class FWorldRelief
{
    // Every evaluation takes a footprint, cm: the material's own fade,
    // saturate(1 - footprint x frequency) per band, made explicit. 0 is every
    // band (the flight); a tile passes its vertex spacing, the material its
    // pixel footprint. Parity is only ever claimed at a stated footprint.
    double Height(const FVector3d& D, double FootprintCm = 0) const;   // cm above the datum, D unit, body axes
    double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm = 0) const;
    FFaceTerms Face(const FVector3d& D, double FootprintCm) const;      // continent, detail, crater albedo terms
    double OmittedBoundCm(double FootprintCm) const;        // analytic bound on what the fade removed
    double MaxHeightCm() const; double MinHeightCm() const; // analytic bounds, never sampled
    double MaxSlope() const;                                // Lipschitz bound, for the ray march
    double FinestWavelengthCm() const;                      // the band limit (below)
};
```

`FWorldReliefParams` and `EGround` live in their own header,
`Surface/WorldReliefParams.h` (plain data, no functions), owned by track P
so that `FSkyBody` can carry them before `FWorldRelief` exists (*Parallel
tracks*).

`D` is the unit direction from the body's centre in **universe axes**,
which are the body's axes because worlds do not spin (ruling 9). That is
exactly the frame `M_SkyBody` already works in.

**How GPU and C++ are held equal: one source file, compiled twice.** The
noise core -- the integer hash, gradient noise, the simplex noise with its
gradient, Voronoi, and the band sums -- is written once, in
`Shaders/Private/WorldRelief.ush`, in the subset of syntax that is both
valid HLSL (the shader language) and valid C++, with a `WR_REAL` macro that
is `float` in HLSL and `double` in C++ and a handful of shims (`frac`,
`floor`, vector types). `Source/DeepSpace/Surface/WorldRelief.cpp`
`#include`s it, so a syntax change that breaks C++ fails `./build.sh`.
`M_SkyBody` stops using the engine's built-in Noise, VectorNoise and
Voronoi nodes and calls the same file through one **Custom node** (a
material node whose body is HLSL text) whose include path is
`/Project/Private/WorldRelief.ush`. The shader compiler finds
`/Project/...` because UE 5.8 maps `/Project` to the project's `Shaders/`
itself, at PreInit (`LaunchEngineLoop.cpp:2557`), and
`DeepSpace.Surface.ShaderMapping` holds that the mapping finds the file.
(As planned, a second module, `DeepSpaceShaders`, made that mapping at
`PostConfigInit`; it was found redundant and removed at R2 -- *Ruled at
R2*, above.)

The file is a **rewrite, not a line-for-line port**, of the engine's own
functions (`Rand3DPCG16` from `RandomPCG.ush`; `GradientNoise3D_ALU`,
`JacobianSimplex_ALU` and `VoronoiNoise3D_ALU` from `Random.ush`). Those
use HLSL-only constructs -- swizzles (`f.yzx`, `rand.xxx`), `float3x4` and
`float4x3` with `mul()` and row indexing, `step()`, implicit vector/scalar
arithmetic -- that plain C++ cannot compile without a full vector-type
emulation. So the file is written in a **scalar, no-swizzle subset**: every
vector operation spelled out per component, matrices as named scalars, and
only `frac`, `floor`, `step`, `saturate`, `min`, `max`, `abs`, `sqrt`
shimmed in C++. Every symbol is prefixed `WR_` (`WR_Rand3DPCG16`, ...):
`Common.ush` already includes `Random.ush` into every material, so an
unprefixed copy would redefine the engine's functions and fail to compile in
the Custom node. What must match today's engine nodes is the **arithmetic**,
operation for operation, so that in slice (a) the material draws what it
draws today; the integer hash matches bit-exactly, the rest in float to the
last bits. That equality is not claimed by construction: it is proven
numerically by the rendered parity test below.

**The spike comes first.** Slice (a)'s first task, time-boxed to two
working days, is one detail band and one crater band in the file, compiled
into `./build.sh` and into a Custom node, rendered by the parity test
against today's engine nodes. **Go:** within the numeric tolerance below.
**No-go**, and the fallback is taken: the include is dropped for the
pasted text (if the `/Project` mapping is the obstacle), or, if the
subset itself cannot reach parity, the engine nodes stay in `M_SkyBody` and
the C++ is an independent port held to them by the same rendered test
(sign-off item 1's third alternative).

**Precision and the band limit.** In float, a unit direction resolves about
0.4 m on an Earth, and the material's finest band is 130 m (49,152 cycles
per radius), so the GPU never evaluates anything the float cannot hold. The
C++ evaluates the same bands **plus finer ones the GPU never sees**, down to
`FinestWavelengthCm` = 500 (5 m), for the terrain's vertices. Nothing in
`WorldRelief` is finer than 5 m, so the finest tiles (0.6 m vertex spacing,
decision 6) never alias it and the flight never catches on a bump nobody can
see. Coarser tiles evaluate with their vertex spacing as the footprint, so a
km-spaced tile never samples 5 m bands into random vertex error.

**Rejected: a separate HLSL re-implementation beside the C++.** Two
generators; ADR 0006 forbids it, and nothing would hold them equal.
**Rejected: bake WorldRelief into cubemap textures the material samples.**
Six 2048^2 faces is 5 km per texel on an Earth and about 100 MB per world;
it cannot carry the fine bands and filtering changes the look, which slice
(a) must not. (Baking the coarse bands later as an *optimisation* of the
same function is not ruled out.) **Rejected: pasting the file's text into
the Custom node from `setup_sky_materials.py`**, with a test comparing
hashes. It avoids the extra module, but a Custom node's body is the inside
of one function, so the helper functions need awkward wrapping, and a
pasted copy is one regeneration away from drift. It is the fallback if the
module proves troublesome. **Rejected: keep the engine's noise nodes and
write an independent C++ port with a parity test.** The same port, with
the HLSL side left as a black box; the shared file makes the equality
structural.

**How the equality is tested** (headless tests cannot run shaders, since
they run under `-nullrhi`, with no GPU):

- *Structural:* the C++ build compiles the file (drift fails the build).
- *Headless:* `DeepSpace.Surface.WorldRelief.KnownValues` pins the hash and
  the noise at fixed inputs, computed by hand from the HLSL formulas;
  `DeepSpace.Sky.MaterialContract` asserts `M_SkyBody`'s Custom node
  includes `/Project/Private/WorldRelief.ush` and calls the documented
  entry point, and that the contract lists its parameters.
- *Rendered, a new test:* `Eyes.WorldReliefParity`, in
  `Source/DeepSpace/Tests/Eyes/` beside `MapFromHelmEyesTest` and run like
  it: named outside `DeepSpace.` so `./test.sh` never runs it, run through
  the lock with `-RenderOffScreen` and never `-nullrhi` (under which it
  errors). Unlike the map's, it is a **guard, not a look**: it asserts
  numbers, and slice (a)'s and (b)'s done-when run it. A `Tools/eyes.sh`
  wraps the command so it is one line. It draws a debug material, `M_SkyReliefProbe`
  (authored by `setup_sky_materials.py`: UV -> D over a fixed patch, a
  fixed footprint, outputting the raw face terms and the slope, untonemapped),
  into an `RTF_RGBA32f` render target with `DrawMaterialToRenderTarget`,
  reads it back, and compares with C++ `Face(D, Footprint)` and the gradient
  at the same D and footprint: **max absolute difference 1e-3 in face and
  slope units, over 256 x 256 samples at each of five footprints**. The same
  test rendered today's graph (kept for slice (a) as `M_SkyReliefProbeLegacy`,
  the engine nodes) against the new one: that was "the orbital look
  unchanged", as a number, proven at R4 and recorded in the test's header.
  The legacy probe was then retired as slice (a) closed; the engine nodes'
  last measured distance from double, per footprint and term on both the
  barren world and the giant, is recorded in the test as the floor the
  rule holds the GPU to. Because it
  must compile the Custom node to draw anything, it also catches the grey
  material.
- *Eyes:* the developer flies to three worlds.

**A known trap.** A Custom node HLSL error is invisible to the headless
suite: under the commandlet only the HLSL translator runs, no shader
compiles. A typo ships the default grey material with every test green --
the same failure class as a misspelt `SetScalarParameterValue`. That is why
the parity test renders, and why slice (a)'s done-when runs it.

**Cost to change:** high once terrain exists. Every consumer (flight,
tiles, HUD, material) calls this one function; changing the mechanism is a
rewrite of the material and a re-proof of parity.

### 2. The seed stays the face's; the relief amplitude is a new draw

**`FSkyBody::SurfaceSeed` keeps its derivation** (`GenSeed::Derive(System
seed, Label("sky.surface"), orbit index)`) and its quantisation to float
offsets, so every world keeps the face it has. It is already an integer-hash
derivation, as ADR 0007 requires; its comment ("a look, not a fact") is
rewritten: it is now the seed of the ground. The derivation moves into
`Universe/` as a named function (`GenSeed::SurfaceSeed(SystemSeed, Index)`)
so terrain never re-derives it by hand.

**The peak relief is a drawn fact on `FPlanet`**, `ReliefKm`, from its own
stream `Derive(PlanetSeed, Label("relief"))`. A new label moves no existing
draw, so the known-value tests (`DeepSpace.Universe.Seed`, `.Stream`) are
untouched, and so is every other number of every world.
`FStarSystemGenerator::PlanetSeed(SystemSeed, Index)` becomes public for it.

**Rejected: re-seed faces from the procgen spec's reserved
`Derive(PlanetSeed, Label("surface"))` hook.** Tidier, but it changes every
world's face once, which slice (a) is ruled not to do. It remains possible
later; nothing pins today's seed but the look.

**Cost to change:** low now; after the first landing playtest, a re-seed
moves every place the developer has been.

### 3. Relief is physical, derived from gravity, drawn as a proportion, capped by a guarantee

What physics decides is derived; only the free parameter is drawn (ADR
0008, procgen decision 5). Mountains are limited by what the crust can hold
up, which goes as 1/g (Earth's 8.8 km at 1 g, Mars's 22 km at 0.38 g):

- **Ceiling:** `H_ceiling = Strength_kind / g` (g in Earth g), with priors
  `ReliefStrengthRockKm = 9`, `ReliefStrengthIceKm = 9`, and a terrestrial
  erosion factor `ReliefTerrestrialFactor = 0.7`.
- **Guarantee:** `GenGuarantees::MaxReliefKm = 10` -- no ini edit can put a
  peak higher (ruling 6's ceiling), and none above 0.5 % of the radius.
- **Draw:** `ReliefKm = min(H_ceiling, MaxReliefKm, 0.005 x R) x Beta(A, B)` -- a
  bounded proportion of what the crust could hold, which is what Beta is for.
  Barren and Ice skew high (old, unrelaxed crust): Beta(5, 2), mean 0.71.
  Terrestrial skews lower (weather): Beta(3, 2), mean 0.6. So a 1 g
  terrestrial world peaks around 3.8 km, a 1 g barren one around 6.4 km, and
  a light barren one (under 0.9 g) around 7.1 km: its ceiling is the 10 km
  cap, and it reaches Beta's share of that, never the cap itself.
- Oceans and gas giants: 0, `Ground = None`.

Every number but the guarantee is a line in `DefaultGame.ini`
(`[/Script/DeepSpace.ProcGenPriorsConfig]`), in the `DS_GEN_PRIORS` list,
with domain checks in `GenPriorDomain::Refusals` (Beta shapes >= the
existing minimum, strengths > 0). Two new corpus columns,
`surface_gravity_g` and `relief_km`, go into
`Tools/procgen_corpus_contract.json`, `procgen_corpus.py` and the corpus
test together, so the priors can be read across 10,000 worlds before
anyone flies down.

**The spectrum.** `Height(D) = PeakCm x S(D) / S_max`, where `S` is today's
band sum (each band's height its value over its frequency, so every scale
has the same slope) plus the crater terms, and `S_max` its analytic bound.
`MaxHeightCm()` is therefore exactly `PeakCm`. The continent band stays
albedo only in this slice.

**Craters must become continuous to be a height.** Today each crater band is
Voronoi F1 -- the nearest site only -- with a hash keeping 60 % of sites. The
jittered sites can be 0.48 cells apart, but the profile reaches 0.525 cells
(q < 1.5 at radius 0.35), so where a kept crater's bowl or rim meets the
bisector with a dropped neighbour the height jumps from the profile to 0: a
cliff. The material never shows it, because it uses only the slope; as a
height it would break `MaxSlope`, the ray march, `.Gradient` and
`.SlopeBound`, and put vertical steps in the mesh. So in `WorldRelief`
**a crater band is a sum of compact kernels, one per kept site, over the
3 x 3 x 3 cells around the sample** (the profile unchanged, reaching 0 with
zero slope at q = 1.5; a dropped site contributes 0; overlapping craters
add). With jitter at most 0.26 cells, a 3 x 3 x 3 neighbourhood sees every
site within 0.74 cells, past the profile's 0.525, so the sum is continuous
and differentiable. It costs 27 site hashes a band instead of 8.

**Slice (a) keeps today's craters** for the look, in the material only:
`Height` in (a) is the detail bands alone (nothing reads it yet but tests).
**Slice (b) switches both** the material and `Height` to the summed kernels,
with the flattening; the orbital craters change slightly where two overlap
or one was clipped at a bisector. Sign-off item 2.

**The orbital look in slice (a) versus (b).** Ruling 11 says slice (a)
leaves the orbital look unchanged; ruling 6 makes relief Earth-like, about
a third of what the material's slopes imply; and ruling 5 says what you saw
from orbit is what is there. They cannot all hold at once, so this spec
**orders** them: slice (a) moves the material onto `WorldRelief` with
today's amplitudes (`ds.Sky.Relief` still live, look provably unchanged);
slice (b), when the ground becomes real, switches the amplitude to
`PeakCm` and derives the orbital normal from the true gradient. **The
orbit's relief shading will visibly flatten at that switch** -- same
continents, same craters in the same places, gentler light and shade.
Before/after frames go to the developer before (b) merges. Sign-off item 2.

**Rejected: keep today's slopes and make mountains 26-50 km.** Against
ruling 6, and it puts peaks through the drive's floor. **Rejected: a flat
height per kind.** A light world and a heavy world would have the same
mountains, which reads as generated. **Rejected: an orbit-only
exaggeration factor.** It keeps the look by making the orbit lie about the
ground, against ruling 5.

**Cost to change:** medium. The priors are ini lines (`ds.Universe.ReloadPriors`);
the law (1/g, Beta) is code with tests; the guarantee is what the drive
floor rests on (decision 10).

### 4. Gravity: real, everywhere, summed from every body; the boosters hold it

**Constants.** `UniverseUnits.h` gains the IAU 2015 nominal values,
`GMSunCm3PerS2 = 1.3271244e26` and `GMEarthCm3PerS2 = 3.986004e20` (their
ratio agrees with the existing `EarthMassSolar`).

**The data.** `FSkyBody` gains `GravParam` (GM, cm^3/s^2), `Ground`
(decision 13) and `Relief` (`FWorldReliefParams`), filled only in
`FSkySystem::FromSystem`, the one adapter from procgen to sky and flight.
Its header's own comment allows the drive's view of what is near; these
are that. `UShipSubsystem::UpdateSurfaces` builds, from the same
`LocalSystem::Here()` it already reads, an array of `FGravityWell {Centre,
Mu}` and hands it to the flight state beside the surfaces. In transit there
are no wells, as there are no surfaces.

**The law.** `ShipFlight::GravityAt(Wells, Position)` -- pure, the
inverse-square sum, every separation through `FUniversePosition`'s
`operator-` (double, chunked, 20 um resolution at any range). Known-value
tests, derived from the constants actually used: 979.85 cm/s^2 at an
Earth's surface (`GMEarthCm3PerS2 / CmPerEarthRadius^2`, the equatorial
6,378.1 km), 0.593 cm/s^2 from the Sun at 1 AU.

**The boosters hold it: one law.** **The velocity is unaffected by
gravity.** Neither substep adds `g dt`; the chase and the drive work on the
velocity exactly as today, and the boosters' proper acceleration is what
they did plus what they held:
`GetThrustAcceleration() = GetLinearAcceleration() - GetLocalGravity()`.
The hold is therefore **not budget-limited**, which the ruling needs: at
the floor of the smallest red dwarfs the pull (about 550 m/s^2) exceeds
starved thrust (500 m/s^2), and "the boosters always hold" cannot be a
budget that sometimes loses. The chase budget is spent on the lever's change
only, so `FlightCruiseFloor`'s braking arithmetic is untouched. The drive,
which sets velocity with no inertia, is untouched too.

This makes gravity invisible in the trajectory, and that is the ruling:
the levers mean what they say anywhere. Gravity is felt in four places,
each designed (decision 5).

**New getters on `FShipFlightState`:** `GetLocalGravity()` (the vector, the
sum over every well: the star and every world, ruling 4),
`GetThrustAcceleration()` (as above), beside the kinematic
`GetLinearAcceleration()`, which keeps its meaning so the hum's "changing"
term does not double-count. A pure test holds that a full-lever
acceleration beside a 3 g world follows the same path with and without
wells.

**Rejected: gravity as a real force the boosters must out-thrust.** On
speed-bands it is under 2 % of thrust and imperceptible; on main's 40 m/s^2
it would make starved boosters fail on half the worlds, a failure state.
**Rejected: gravity only near worlds.** The ruling says everywhere, and the
sum is cheap.

**Cost to change:** low in code (one pure function, one line in the
substep); the effort model below is what a playtest tunes.

### 5. Gravity felt as effort: watts, the hiss, slower climbs, and a starved sink the ground catches

**Where effort is felt: under a solid world's drive floor, airborne.** The
boosters hold gravity everywhere (decision 4), but the *effort* of holding
it -- watts, the hiss, the sink -- is paid only where the pilot chose to fly
down to: **under a solid world's drive floor (decision 10), while not
Landed**. Call it *under the floor*. Everywhere else the hold is free: at
rest AT THE FLOOR, where every drive approach ends; at an ocean's or a
giant's floor, where the ship can never land to get relief; at a star's
floor; between worlds; and at the opening placement. So the 2026-09-26
ruling that the stock ship is whole at rest (`ShipSubsystem.h`: 1370 W of
1400 W) and the rule that staying put is never taxed both hold unchanged
everywhere a ship can be parked. The want ramps in over the first kilometre
under the floor, so crossing it is not a step.

**Power.** Under the floor the boosters' want becomes `BoostersWant (450 W)
+ HoldWant`, with `HoldWant = ds.Boosters.HoldWatts (150) x min(g / g_E, 3)`,
where g is the magnitude of the **total** gravity at the ship (every body,
ruling 4 -- the star included, which at home matters). It is set in
`ApplyAllocation` beside the engine's want, only when it changes by more
than a watt. It is one consumer, not a new one: a new consumer would add a
player weight, one more knob to tune toward an optimum.

What the split really delivers (1400 W less 620 W of modules leaves 780 W;
default weights all 1.0; lights capped at their 300 W want, the surplus
redistributed):

| Hovering over | g held | HoldWant | Boosters get | Hold fed | Manoeuvre thrust |
|---|---|---|---|---|---|
| Baemsekai IV (home, barren) | about 0.9 g (0.84 world, 0.09 star) | ~135 W | 480 of ~585 W | all | 0.76 |
| Baemsekai III (home, barren) | up to 2.0 g (1.66 world, 0.31 star) | ~300 W | 480 of ~750 W | all | 0.40 |
| a 3 g world | 3 g | 450 W | 480 of 900 W | all | 0.25 (the floor) |
| a 3 g world, **jump winding** | 3 g | 450 W | 260 of 900 W | 0.58 | 0.25 |

At the default split **the lights stay whole while hovering**: the hold
comes out of the boosters' share, not the lights'. What the pilot sees is
the allocation screen's boosters line (`BOOSTERS 480 W of 585 W`): effort
as watts, never as a percentage. (This once named the HUD's `SPARE` at zero
too; the wear plan removed `SPARE` from the HUD, ruled 2026-09-27, so the
boosters line is the cue.) Manoeuvre
thrust falling to 0.76 or 0.25 changes acceleration only (2 km/s^2 to 1.5
or 0.5), which near the ground is imperceptible; the top is never lowered
by thrust (below).

Inside the boosters' share the hold is **paid first**:
`HoldFed = min(1, Share / HoldWant)` (1 when `HoldWant` is 0), and the
manoeuvring thrust keeps its 0.25 floor on what is left. This is a pure
`ShipPower::SplitBoosters(Share, HoldWant, ManoeuvreWant)` beside
`FShipPowerState`, tested headlessly.

**The hum.** `ShipHum::Push` gains a hold term,
`ds.Hum.HoldHiss (0.35) x HoldWattsDelivered / (3 x ds.Boosters.HoldWatts)`,
so it reaches cruise's hiss (`ds.Hum.CruiseHiss`, 0.35) only at the 3 g cap
and never exceeds it: a 1 g hover hisses at a third of cruise. It is read in
**watts delivered**, never satisfaction (conflict 8's trap), and shares
`HoldWant`'s scope, so it is silent everywhere a ship can be parked: it
swells as the ship descends under the floor, holds while it hovers, and
falls silent at touchdown or on climbing back above the floor. `EngineFeed`
is unchanged.

**Slower climbs on heavy worlds.** The vertical lever's climb top is
`VerticalTop x max(ds.Vertical.HeavyFloor (0.25), min(1, g_E / g))`, with g
as above. **It does not depend on the booster share**: thrust scales the
lever's acceleration, never its top, which is the drive's existing rule
(`FShipFlightLimits::DriveThrust`: "the top is never lowered by it", the
anti-chore principle's lost-potential case). So the power split is never a
climb-rate knob, and there is nothing to rebalance toward. Climb tops and
climb-outs from the ground to a drive floor 20 km above it (the worst case,
decision 10):

| g | Climb top | Ground to a 20 km drive floor |
|---|---|---|
| 1 g or less | 200 m/s | 100 s |
| 2 g (Baemsekai III) | 100 m/s | 3 min 20 s |
| 3.3 g (the heaviest solid world) | 61 m/s | 5 min 30 s |
| 4 g or more (only beside a close red dwarf) | 50 m/s (the floor) | 6 min 40 s |

Sinking is never slowed by gravity. `DeepSpace.Playtest.HeavyWorldStillClimbs`
flies the 3.3 g case starved (booster weight 0) and asserts the climb top.
The climb-out on the heaviest worlds is sign-off item 6.

**The starved sink.** Only **under the floor** (the same scope), a hold
shortfall becomes a bounded bias on the vertical rate:
`SinkBias = ds.Boosters.StarvedSink (2 m/s) x (1 - HoldFed)`, added to the
lever's asked rate **only while that rate is hover or sink**. While the
lever asks a climb, no bias is applied: a starved ship always lifts, so a
starved landed ship can always take off (without this, take-off's first
0.1 m/s ask would be out-sunk and re-land the ship at once). The descent
cap (decision 10) brings a sinking ship to a gentle touchdown, and LANDED
relieves the split (HoldWant 0). **Everywhere else the boosters hold
whatever the split**, including a booster weight of 0: a ship parked
between worlds, or at any floor, while the jump winds never drifts
(`DeepSpace.Playtest.ParkedShipNeverDrifts`: starved, ten minutes, position
unchanged, at a solid world's drive floor and between worlds).

**The sink is the model changing with time, and that is an amendment.**
CLAUDE.md says of power: "nothing in the model changes on its own with
time". The sink is the one sanctioned exception, and it is bounded: only
under a solid world's drive floor, at most 2 m/s, never while climbing, and
it ends, always, at rest on the ground at no cost. The power paragraph is
amended to say so (*Documentation*).

**A consequence the developer should rule on.** A hover set by the pilot,
and ruled to survive the pilot standing up, can be undone by a power
choice: on a heavy world, engaging the jump (or raising the lights' weight)
splits the 780 W three ways and starves the hold -- at 3 g, 0.84 m/s of
sink, so a hover at 500 m lands itself in about ten minutes; on Baemsekai
III, about 0.25 m/s. Worlds under about 1.7 g never sink at default weights
with the jump winding. As ruled ("a gentle sink if the boosters are
starved"), this is the trade-off the allocation is: you chose the jump over
the hover. The alternative is to pay the hold **off the top**, before the
split, like a module's draw: the jump and the lights could never starve it,
but then nothing in the stock ship ever could, and the ruled sink would
never happen. Sign-off item 7.

**Why this passes the anti-chore test.** The sink is not a rate the player
must keep up with: it ends, always, in a landing at rest that costs
nothing. Hovering is taxed only under the floor, by the pilot's choice to
go there; landed and parked are never taxed. The HUD shows the sink only as
`SINKING 2 M/S`, never as a warning or a shortfall.

**Rejected: HoldWant everywhere g is held.** It taxes rest at every drive
floor, forever over oceans and giants (which can never land to get relief),
and at home, where Baemsekai's own pull at world III is 0.31 g: it would
break the 2026-09-26 reactor ruling. **Rejected: grow the reactor by the
1 g hold.** It keeps the ruling while hovering but still taxes rest at
every floor, and at a star's floor the 3 g cap outruns it. **Rejected: the
sink as physics (thrust minus g).** On speed-bands starved boosters
out-hold every world 15 times over, so it would never happen; on main they
would fail on half the worlds, uncatchably near a star. **Rejected:
HoldWant in proportion to g over the rated acceleration.** 1 g against 2
km/s^2 is 2 W: invisible. The watts are deliberately perceptual, decoupled
from the rated thrust. **Rejected: a sink anywhere.** Between worlds
nothing catches it. **Rejected: the climb top scaled by the booster
share.** It makes the split a throughput knob (vision: "a power split you
rebalance to hit a throughput target is a factory").

**Cost to change:** low: CVars and a pure split, tuned in play against
`ShipLightingSubsystem`'s brown-out at 1/3.

### 6. The terrain: a cube-sphere quadtree of pooled mesh tiles, built off the game thread

**Meshes.** `UProceduralMeshComponent` (PMC), the engine's runtime mesh
component: add `ProceduralMeshComponent` to `DeepSpace.Build.cs`. One
component per tile, **pooled**. Every tile has one fixed topology: a 33 x
33 vertex grid (32 x 32 cells, 2,048 triangles) plus a skirt (a short
curtain hanging from each edge, hiding cracks between tiles of different
detail). Because the topology never changes, a pooled component is
recycled with `UpdateMeshSection`, which sends only new vertex data to the
GPU; `CreateMeshSection` (which rebuilds the whole render proxy) is paid
once per pooled component. No collision: `bCreateCollision = false`,
`NoCollision`; nothing is cooked.

**What each tile carries, and what it must not do.** Positions (float,
local to the tile's pivot, below). The normal is carried in **float UV
channels** (UV1.xy and UV2.x, `FVector2f` each), not PMC's normal, which
it narrows to an 8-bit `FPackedNormal` (about 0.45 degrees a step: after
the flattening, relief tilts are a few degrees, and the unlit Lambert near
the terminator would posterise). No vertex colours: the face is evaluated
per pixel (decision 9). The tile's band limit (its vertex spacing) reaches
the material as **custom primitive data** (`SetCustomPrimitiveDataFloat`),
so no tile needs its own material instance. And every tile is set
`CastShadow = false`, `bAffectDistanceFieldLighting = false`,
`bAffectDynamicIndirectLighting = false` and `bVisibleInRayTracing = false`:
the project enables Virtual Shadow Maps (`r.Shadow.Virtual.Enable=1`), PMC
reports shadow relevance from `CastShadow` (default true), and a thousand
tiles moving every frame would invalidate the Sun's cached shadow pages
every frame and shadow the deck, which is a non-goal.
`DeepSpace.Surface.GroundActor` asserts all four.

**PMC's two costs, measured as the first task of (b).** PMC keeps a CPU copy of every
section (`FProcMeshVertex`, about 150 bytes a vertex with its double
position and normal), which is how tests read back what was uploaded --
and at the tile counts below that is **250-400 MB**. And it draws on the
dynamic path (`bDynamicRelevance = true`): every tile's mesh batch is
rebuilt in every pass, every frame. Both are gated in slice (b)'s first
day. If either fails, the fallback is a small custom primitive,
`UTerrainTileComponent`, with its own scene proxy on the static draw path
and no CPU copy beyond a test hook; the pure core is unchanged by the
swap. Sign-off item 15.

**Measured, the first day of (b)** (`Eyes.TerrainBudget`, this machine's RTX 4070 Ti SUPER, 4K):
PMC's CPU copies 508.4 MB at 2,500 tiles; 2,200 tiles cost 68.86 ms of a 4K capture over the
empty scene; moving every tile 1.17 ms of game thread and 108.36 ms more to draw; one
`UpdateMeshSection` 0.013 ms. **Verdict: CUSTOM PRIMITIVE.** Three of the four budgets failed:
the CPU copies (508.4 MB against 400; `FProcMeshVertex` is 148 bytes a vertex and a tile has
1,221, so this one is arithmetic, not timing), the draw (68.86 ms against 6) and the draw after
moving every tile (108.36 ms against 2); only the move's game thread (1.17 ms against 2) passed.
A second run agreed to within 1 ms on the draw (69.47, 105.86) and exactly on the copies.
`UTerrainTileComponent` (Task T5) replaces PMC in `AWorldGround`. The draw numbers are so far
past the budget that T5 must measure its static path through the same gate before T6 builds on
it: a draw cost that is the GPU's, not the dynamic path's, would fail the custom primitive too.

**The frame.** `AWorldGround` (new, `Source/DeepSpace/Surface/`) attaches
to `AShipCounterFrame`, identity relative, and is spawned by
`build_hauler.py` as `hauler_ground` (tagged `Sky.Ground`). Each tile's
pivot is the double-precision point on the sphere at the tile's centre,
placed every tick at `UniverseToWorld(pivot)`, computed through
`FUniversePosition` in doubles. Vertices are local to the pivot, in cm,
scale 1. PMC narrows vertices to float (64 cm resolution at an Earth
radius), so local vertices are required: a 19 m tile keeps sub-millimetre
precision.

**Tiles inherit the counter-frame's rotation; the proxies do not.** After
`fix/surface-artifacts` merges, `AShipSky`'s proxies use absolute rotation
(`SetUsingAbsoluteRotation`, identity world rotation) and turn their face in
the material through `BodyAxisX`/`Y`, so "as `AShipSky` does" no longer
describes the ground, and this spec does not claim it. The difference is
the lever arm. A proxy is a sphere whose far side is the whole magnified
radius from its pivot, so GPU Scene's compressed rotation (about 2e-5 rad)
slid its face by kilometres; and a sphere is rotation-invariant, so it could
give the rotation up. A tile is not rotation-invariant (its vertices are
real ground in universe axes, and the ship's orientation must turn them),
and its lever arm is its own half-edge: the same 2e-5 rad is under a
millimetre on a 19 m tile under the ship, and a few metres on a 300 km
level-5 tile hundreds of kilometres away, under a pixel. So the tiles
inherit the counter-frame's rotation, and their normals (in the UV
channels) are in **tile-local space, which is universe axes**; the
material carries them to world space with the engine's local-to-world
transform, where `LightDirection` already is.

**The quadtree.** Six cube faces on the same equal-angle mapping as
`SM_SkyBody` (`tan` of the angle, `Sky/SkySphereMesh.h`), split into
quadrants recursively. A node at level L spans 90/2^L degrees, about
10,019 km / 2^L on an Earth. `MaxLevel = ceil(log2(R x pi/2 / (32 x
TargetSpacing)))` with `TargetSpacing` 1 m: **level 19** on an Earth
(ceil(18.26); 19 m tiles, 0.6 m vertex spacing), fewer on small worlds.

**The LOD rule** is CDLOD's (continuous distance-dependent LOD), measured
**from the ship's origin**, not the camera, so it is the same in tests and
in play (the eye is never more than 17.6 m off it). Split a node when the
ship is closer than `ds.Terrain.SplitFactor` (2.0) x its edge length,
measured to the node's box. **The box's vertical extent is the node's own
height range**, taken from its parent tile's built vertices over the
node's area and widened by `OmittedBoundCm(parent spacing)` -- never the
world's whole relief shell, which would refine everything under the
highest peak's height to `MaxLevel` wherever the real ground is. The root
faces, with no parent, use the analytic bounds. Because the boxes are no
longer a symmetric shell, **a 2:1 neighbour constraint is enforced
explicitly** (a split forces any neighbour more than one level coarser to
split). **Horizon culling:** a node whose box lies wholly below the horizon
as seen from the ship, for its own top height, is dropped.

**What that costs, honestly.** A simulation of this rule (an Earth,
equal-angle faces, split factor 2, 2 km local peaks for the horizon, run
for this review) gives about **180 tiles a level: about 860 tiles at 50
km, 1,100 at 10 km, 1,600 at 1 km and 2,200 at 1.5 m**, which at 33 x 33 is
**2-4.5 M triangles**. (The draft's 150-300 tiles was 5-10 times too low: a
CDLOD level's leaves fill an annulus several edges wide, not a ring.) At
split factor 1.5 with 65 x 65 tiles it is 530-1,300 tiles but 4-10 M
triangles. `DeepSpace.Surface.Quadtree` computes these counts per altitude,
and the budgets below are revised from it on slice (b)'s first day.

**`MaxTiles` (2,500) is a ceiling, not a target.** A cut that would exceed
it coarsens **the farthest levels first**, lowering their split factor a
step at a time; the ship's own chain, root to `MaxLevel`, is never
coarsened. `ds.Terrain.Describe` says when the cap is binding.

**Async build.** `TerrainTile::Build(const FWorldRelief&, FTileKey)` is
pure: positions from `Height(D, spacing)` (the tile's vertex spacing as the
footprint, so a coarse tile never samples fine bands into vertex noise),
normals from the analytic gradient of the same band-limited height (no
finite differences). It runs on `UE::Tasks::Launch` (Unreal's task system:
work handed to worker threads) with **at most 2 builds in flight**
(`ds.Terrain.BuildTasks`, the machine's cap: never sized to the core
count). The game thread applies at most `ds.Terrain.UploadsPerFrame` (4)
finished tiles a frame. Estimated cost: 3-8 us a sample (more with the
summed craters, decision 3), 5-10 ms a tile, 200-400 tiles a second on two
workers.

**What the build must keep up with.** Under the drive floor the skim cap
(decision 10) holds horizontal speed to at most 0.4 x AGL a second, so the
ground flows past at the same apparent rate at every height, and CDLOD is
scale-invariant: each level's ring shifts by at most about one tile edge a
second, some 30 new tiles a second per level near the ship, **100-200 a
second in all**, inside the build rate. Descent at the full 200 m/s takes
250 s from 50 km through about twelve levels. Above the drive floor the
drive may run tangentially at up to 0.1 c, which no streaming can follow;
there the cut may lag and the proxy stands in (decision 7), which is safe
because the drive never meets the ground.

**Never a hole, and never the proxy under the floor.** A node is drawn only
when resident; a parent stays drawn until all four children are. The chain
from the root to level 8 under the ship is prefetched from 1,000 km up, so
the coarse cap is resident well before the handover. **Under the drive
floor some real tile is always drawn under the ship, never the proxy**: the
prefetch and the skim cap make it so, and slice (b)'s done-when proves it
at the full sink and the skim cap's top. Residency is a **performance
requirement, tested; it never gates the ship's motion** (a sink or speed
that waits on streaming is a loading screen expressed as flight). Under
the ship, whenever AGL is under 1 km, the drawn ground must be within
`GearClearance / 10` (15 cm) of the analytic ground
(`DeepSpace.Surface.GroundActor`, and a playtest flight at the skim cap).

**Budgets** (CVars, read at use): `ds.Terrain.SplitFactor` 2.0,
`.MaxTiles` 2,500, `.BuildTasks` 2, `.UploadsPerFrame` 4. Header constants
with tests: 32 cells a tile, 1 m target spacing. `ds.Terrain.Describe`
prints the cut per level with resident and pending counts; `ds.Terrain.Show
0` hides the ground for comparison.

**Seams and pops.** Skirts in slice 1, each hanging the tile's maximum edge
interpolation error plus its `OmittedBoundCm` plus 50 % (neighbours at
different levels carry different bands). A split still pops by what the
child's finer bands add: at split factor 2 that is about slope / 64
radians, a few 4K pixels. `DeepSpace.Surface.Tile` computes the largest
vertex jump at a split in 4K pixels at the split distance, and slice (b)
reports it. Geomorphing (vertices easing between levels) is sign-off item
16.

**Rejected:** `UDynamicMeshComponent` (full half-edge topology, heavier than
a grid needs); GPU displacement by World Position Offset (float precision,
untestable headless, and a second evaluation of the height that could drift
from the flight's); `VirtualHeightfieldMesh` and Landscape (planar);
Nanite (its builder is editor-only); RealtimeMesh (not installed; a
third-party dependency); one PMC with many sections (a new section rebuilds
every tile's proxy); boxes from the world's relief shell (every level to
`MaxLevel` under peak height).

**Cost to change:** medium. The pure core (quadtree, tile build) survives a
change of mesh component; the component choice is one class.

### 7. The orbit-to-ground handover is at 50 km, where the proxy is the true sphere, and the relief grows in below it

`SkyProjection` draws the nearest body with its near side at `NearProxy`,
50 km, magnified by k = 50 km / altitude. **At 50 km altitude k = 1**: the
proxy *is* the true mean-radius sphere at its true place. Below 50 km over
a solid world that body is **drawn by `AWorldGround`** and its proxy
hidden, so there is no scale seam and no cross-fade of two surfaces at one
depth (which would flicker, "z-fight").

**The relief grows in: a morph, not a swap.** The proxy is a smooth sphere
whose relief is only a normal tilt; the ground has up to 10 km of real
relief. Swapped at 50 km, a 5 km peak 45 degrees off nadir would jump by
about 3 degrees, hundreds of 4K pixels, and the horizon and limb would
change shape: the whole landscape would jump. So **every tile's heights
are scaled by one morph fraction M**, a smoothstep of the ship's altitude
over the datum: **M = 0 at the handover (50 km)**, where the tiles are
exactly the proxy's sphere, and **M = 1 at and under the drive floor**
(decision 10: 10 km above the highest peak, at most 20 km over the datum).
M is one number for the whole cut, so the ground rises in shape, not by
pieces; its normals and shading are the full relief's throughout (decision
9), exactly as the orbit's are, so what was seen from orbit is what grows
in. Over the 30-40 km of descent the morph takes (150-200 s at the full
sink), the mountains rise under the ship. **The flight always uses the
full relief**: the ship can reach the ground only under the drive floor,
where M is 1, and above the floor the ship is at least 10 km above every
peak, so the drawn and the flown ground cannot visibly disagree where it
matters. A pure test holds that tile heights equal the sphere at the
handover altitude and the full relief at the drive floor. Sign-off item 8.

**The depth stack is unchanged.** The projection still computes the hidden
proxy, **with `RenderedFloor`'s clamp kept**, so every other body starts
beyond that proxy's far side (about 63,750 km from 10 km over an Earth),
far past the terrain's farthest point (a horizon under 1,100 km even from
100 km up), and no star or world can draw in front of a mountain. Lifting
the clamp for the hidden proxy would put its centre at `NearProxy /
NearFactor` -- about 2e8 km at 1.5 m altitude -- and stack the star past
the starfield dome. `SkyProjectionTest` gains a case at 1.5 m above ground
asserting the star's proxy stays within `FarProxy`.

**The switch waits for the ground, above the floor only.** The proxy is
hidden once the coarse cut (the cut truncated at level 8) is
resident; hysteresis: the terrain takes over under 50 km and gives back
over 55 km, so hovering at the line does not flicker. If the coarse cut is
late -- only the drive, tangential at up to 0.1 c, can outrun it -- the
proxy stays drawn (k slightly above 1, today's sky, correct to a few
pixels), and the switch happens later at whatever M is by then: a pop of
the morph fraction, bounded, and only above the drive floor. **Under the
drive floor the proxy is never drawn over a solid world** (decision 6's
guarantee).

**Above 50 km, the proxy's limb stays a smooth sphere.** A 10 km peak on the
horizon from 100 km subtends about half a degree; accepted as a deliberate
fake (*Deliberate fakes*).

**The flight's floor and the drawn floor split.** `FloorFor` stays the
drive's (decision 10); `RenderedFloor` keeps its meaning for every body's
projection and stacking, and for oceans and giants still stops the drawn
proxy growing. Solid worlds under 50 km are drawn by the ground instead.
`SkyProjectionTest`, `SkyBodyMeshTest`'s premise (LOD 0 sized at 1.6e-3 R)
and `Tools/sky_probe.py` are updated in the same change.

**Depends on `fix/surface-artifacts`.** Its scale rounding and identity
proxy rotation make the proxy steady down to 50 km and below, which is
what makes a seamless swap possible. Slice (b)'s sky work starts after it
merges.

**Rejected: hand over at the drive floor (10 km).** At 10 km, k is about 5;
the terrain would have to be magnified too, inheriting the tearing, or
jump in depth at the swap. **Rejected: a higher handover (150 km).** Real
limbs from higher, at roughly twice the tiles; possible later by one
constant if the smooth limb reads wrong. **Rejected: displace the proxy by
the relief.** A magnified proxy displaced on the GPU by World Position
Offset is the float-precision and tearing problem again, and a second
evaluation of the height. **Rejected: a residency-gated sink.** Slowing the
ship until tiles arrive is a loading screen expressed as flight.

**Cost to change:** low (constants: the handover and the morph band) once
the true-scale branch exists.

### 8. The vertical lever: a third lever, Space and C, live in cruise near a world

**What it is.** `FShipFlightCommand` gains `Vertical` (-1..1), ship state
like `Throttle` and `DriveNotch`: it persists across F, across the pilot
standing up, and **at zero the ship hovers** -- and keeps hovering with
nobody at the helm (ruling 2). Every place that builds a command from
`GetCommand()` carries it over (the trap noted at `ShipFlightState.h:85-89`
that zeroes a lever on every attitude input).

**Its law** mirrors speed-bands' cruise lever (`ShipDriveLever::CruiseSpeed`),
in a new pure `ShipVerticalLever` beside it: a log scale from
`VerticalFloor` 0.1 m/s just off zero to `VerticalTop` 200 m/s at full,
the same in both directions, so it is as fine at 0.5 m/s, where touchdown
is, as it is coarse at 100 m/s. Zero is a place, exactly: the **detent**.
Held, it sweeps at `ds.Vertical.Sweep` (0.25 of the lever a second: rest to
full in 4 s, a decade of rate every 1.3 s); sweeping down from a climb
stops at HOVER, and sinking needs a fresh press of C (and climbing from a
sink a fresh Space). The ship follows the lever through cruise's chase, so
it has cruise's inertia; the climb top of decision 5 applies (by gravity,
never by the booster share).

**Where it is live.** Whenever the ship is flying on cruise -- cruise
live, or the drive live but held under a solid world's drive floor
(decision 10's `DriveBelowFloor`) -- **in the near regime**: within
`ds.Land.Regime` (50 km) above the nearest world's cruise floor (the ground
over a solid world; the floor sphere over an ocean or a giant), or below
it. The regime has **hysteresis**: the ship enters it under 50 km and
leaves it over 55 km, so a ship at the line is never in and out on
alternate substeps. "Up" is radial from that world's centre, exact while
worlds neither spin nor orbit. Outside the regime, or while the drive is
flying the ship, the keys still move the lever (it is a lever) but the ship
ignores it, and the HUD shows it dim. Over an ocean or a giant it brings
the ship to hover at the floor sphere, which is a uniform rule and costs
nothing (no hold is paid there, decision 5).

**How cruise composes with it in the regime.** The cruise target becomes
**the nose's horizontal projection x the cruise lever's speed, plus up x
the vertical rate**. Pitching the nose down to look at the ground no longer
dives the ship: "zero is hover" holds for any cruise setting, and the
ship still goes where it points, in plan. **Across the top of the regime
(40-50 km) both terms blend**, by one weight w that is 1 at 40 km and 0 at
50 km: the horizontal projection blends back to along the nose, and the
vertical term blends out, so there is no hard line where either lever
changes meaning; at cruise's low-speed inertia a hard line would snap.

**What that means at the top.** A ship climbing on the vertical lever with
cruise at STOP slows through 40-50 km and comes to rest in the band (at
most 50 km), with no stall and no oscillation: the weight is continuous
and the regime's hysteresis keeps it from flickering. The motion line says
`CLIMB 200 M/S · ABOVE THE GROUND'S REACH` once w is 0, so the dim lever is
explained. To go higher the pilot uses cruise (nose up, lever forward) or
the drive, which is how the ship leaves a world anyway. Likewise a sink
asked above the regime with cruise at STOP moves nothing until cruise
brings the ship into the band. `DeepSpace.Ship.Landing.RegimeTop` flies
both: a climb at 200 m/s from 30 km comes to rest under 50 km and never
reverses, and a climb with cruise set nose-up passes through 50 km with no
step in velocity. Sign-off item 5.

**Keys.** Space (up) and C (down) are free while seated: movement is
disabled and crouch is gated. New actions `IA_VerticalUp` (SpaceBar) and
`IA_VerticalDown` (C), built by `Tools/setup_flight_input.py` like
`IA_LeverUp`; `SpaceBar` and `C` join `SHARES_WITH_WALKING`, with a comment
that seated they fly and standing they jump and crouch. `FHelmInput` gains
`bVerticalUpHeld`, `bVerticalDownHeld`, `VerticalUpPresses`,
`VerticalDownPresses`. The pawn's `IsSeated` gate on Jump is made explicit
(and covers the chart chair, `IsInScreenChair`). **The vertical keys are
always the vertical lever's**, never switched by F.

**Spent holds.** A Space or C held from before sitting down (Space is jump,
C crouch, both likely in the hand) moves nothing until released, as
`bAwaitingFirstHands` already does for Shift and Ctrl. **X sets the vertical
lever to HOVER** with the other two to STOP: "all stop" means the ship
holds where it is. After X a fresh press counts from what the ship is doing
(the 2026-09-26 ruling): with the ship still sinking, one C catches it at
the lever position nearest its present rate. **The fold's all stop sets it
to HOVER too**, and `JumpTo` zeroes it.

**Tools and tests setter.** `SetVerticalLever(Commander, double)`, pilot-
gated like `SetDriveLever`; added to the write-path list
(`ShipSubsystem.h:244-248`) and to `DeepSpace.Ship.FlightAuthority`.

**Rejected: live everywhere with "up" from the gravity direction.** Near a
star it would be a lever that does nothing perceptible, and between worlds
"up" swings as the strongest body changes. **Rejected: the cruise target
along the nose in the regime.** Looking down dives the ship, contradicting
"at zero the ship hovers". **Rejected: notches.** A rate wants fine control
near zero; the drive's notches are for speeds that differ by factors.

**Cost to change:** low in code; it is on the list because it is the feel
of every landing.

### 9. The terrain shades exactly as the orbit does

`M_SkyGround` (new), authored by `setup_sky_materials.py` from the same
JSON contract, mirrored in `SkyMaterialContract.h`, held by
`DeepSpace.Sky.MaterialContract`. **Unlit**, with `M_SkyBody`'s own law: the
same `LightDirection`, `Colour`, `Brightness` (with `FluxGamma`), the 1.5
disc gain, the terminator's smoothstep and the face clamp, and **the same
per-pixel surface as the orbit**:

- **The face is evaluated per pixel** from the shared `WorldRelief.ush`,
  exactly as `M_SkyBody` does, with the same footprint fade from the
  pixel's `DDX`/`DDY`. `Mottle`, `Detail` and the clamp apply with the same
  parameters, so those albedo CVars stay live and match the orbit. No
  vertex colours.
- **The normal is the tile's plus the pixel's.** The tile's normal (from
  the float UV channels, decision 6) carries the gradient of every band the
  tile's vertices resolve (wavelength at least twice the vertex spacing).
  The material adds, per pixel, the slope of **only the bands finer than
  that and no finer than 130 m** (the orbit's finest), footprint-faded as
  the orbit fades them; the tile's band limit arrives as custom primitive
  data. So a distant km-spaced tile shows every band the orbit showed at
  that distance, and nothing is counted twice.
- **Precision:** per-pixel bands stop at 130 m, as the orbit's do, where a
  float unit direction (about 0.4 m on an Earth) is already proven by the
  orbit. Bands finer than 130 m exist only in the vertices, which resolve
  them only near the ship: a mid-distance tile lacks 5-130 m detail finer
  than its spacing. The orbit never showed that detail either, so it is
  not lost at the handover; it is detail the ground has not got yet
  (*Deliberate fakes*).

Result: at 50 km both sides of the swap evaluate the same bands at the same
footprint, so there is no brightness step and no blur at the handover.
`Eyes.WorldReliefParity` (decision 1) gains a case that renders
`M_SkyBody` and `M_SkyGround` over the same patch from 50 km and compares
them to the same 1e-3 tolerance.

`verify_level.py`'s sky check still holds: no new light, no sky light, no
atmosphere or fog (ruling 7).

**`ds.Sky.Relief` and `ds.Sky.Craters` are retired as live knobs in slice
(b).** Once relief is ground, a knob that moves the height would move the
ground under a landed ship. Amplitude is data (priors), reloaded with the
world.

**Rejected: a lit terrain under the one Sun.** It would receive the ship's
shadow, but the sun is `SunLux = pi x Radiance` against the disc's 1.5
gain: a 33 % brightness step at the handover without a ruled taper, and the
shadow cascades do not reach an 800 km cap. Sign-off item 14.

**Cost to change:** medium: a material and a contract, not the geometry.

### 10. Floors: the drive stays above the peaks, cruise and the vertical lever go to the ground

**The drive's floor.** `FloorFor` stays the drive's floor and the one
function the in-system jump's standoff, `FixWorld` and the drive's ETA
read. Over a solid world it becomes **`max(10 km, RenderedFloor(R))` above
the world's highest peak** (`WorldRelief::MaxHeightCm`), so no summit is
ever within 10 km of the drive: 10.2 km over a flat Earth, up to about 20
km over one at the 10 km cap. Oceans, giants and stars are unchanged, as is
the edge (`EdgeFloor`, still `ds.Flight.Floor`). It also moves, by up to 10
km, where an in-system jump's arrival must be "outside every floor" and
where the drive's ETA ends; the 2-degree arrival standoff is thousands of
kilometres out, so no arrival moves in practice, but `InSystemJumpTest`'s
floor numbers do. Sign-off item 3.

**One surface, two floors.** Today every body's `FFlightSurface` has one
`Floor`, and cruise both ray-casts against that sphere and hard-stops at it
("under a floor already, it may climb and may not descend"). Left as it
is, a cruise ship under a solid world's raised drive floor could never
descend: the whole landing path would be blocked. So `FFlightSurface`
carries **two floors, explicitly**:

- `Floor` -- the **drive floor**, the sphere, as today.
- `Ground` -- optional: the body's `FWorldRelief` and the clearance; set
  only for `EGround::Solid` bodies.

and each substep reads exactly one of them:

| | Surface without a `Ground` (ocean, giant, star, the edge) | Surface with a `Ground` (a solid world) |
|---|---|---|
| `DriveSubStep` (`NearestOnPath`, the soft cap) | the sphere | **the sphere only** |
| `CruiseSubStep` (ray, braking cap, hard stop) | the sphere | **the ground only, at every altitude**, never the sphere |
| `FloorClearance`, `GetRoom`, the HUD's altitude | the sphere | the ground |

Fixtures that build `FFlightSurface` without a ground behave exactly as
today. New pure tests: a cruise ship at 5 km over a solid world descends on
C to the ground (`.CruiseUnderDriveFloor`); a cruise ship at 30 km, nose
down, flies to the ground and rests on it.

**Under the drive's floor the drive does not take the ship: an explicit
mode.** `EFlightMode` gains **`DriveBelowFloor`**, entered whenever the
drive is live and the ship is under a solid world's drive floor (F pressed
at 500 m, or the drive left engaged when the pilot dropped under the floor
in cruise). Its rules:

- The ship flies `CruiseSubStep`, on the cruise lever and the vertical
  lever (decision 8: the vertical lever is live).
- **Shift and Ctrl move the cruise lever**, the lever the ship is actually
  flying on, and the HUD shows cruise's word in ink beside `DRIVE ABOVE
  THE FLOOR` and the dim drive notch. The drive notch keeps its setting.
- `DrivePosition` is **held** at `PositionOf(the ship's forward speed)`
  every substep, never eased toward the notch while hidden, so when the
  drive takes over it starts from what the ship is doing.
- **The drive takes over** only when the ship is above the floor by
  `ds.Land.DriveHandback` (500 m) **and** the nose's ray does not meet the
  floor sphere within the drive's hold distance (the nose at or above the
  tangent); then from its held position, at its own ease. Once the drive
  flies the ship, its own soft cap can bring it to AT THE FLOOR but never
  under it, so the two modes cannot alternate: no limit cycle, and
  `GetMode`, the HUD and the hum cannot flip every substep.

This closes a hole the research found: `RayToFloor` returns no hit for a
ray along or out of a sphere the ship is already under, so a drive engaged
at 500 m with a level nose would fly at 20 km/s and more tangentially
through mountains it never sees. F is never refused. Tests
(`.DriveUnderFloor`, `.DriveTakesOverAbove`) cross the floor both ways.
Sign-off item 4.

**The cruise floor is the ground plus gear clearance.** Over solid ground
the floor is `WorldRelief::Height + ds.Land.GearClearance` at the origin
(decision 11 makes it footprint-wide). The clearance (150 cm) is the
ship's origin above the ground at rest on flat ground: 10 cm of slab and
140 cm of notional gear under the belly. It must exceed the mesh's error
under the ship (decision 6 bounds it at 15 cm) so the drawn ground and the
flight's never visibly disagree. It is its own CVar: `ds.Flight.Floor` is
not reused, since it also sets the system's edge.

**The ground query.** `ShipFlight::GroundAt(Relief, Position)` gives the
altitude above ground at a point; `ShipFlight::RayToGround(Relief, From,
Dir, Clearance)` marches a ray against `Height + Clearance` with steps
bounded by `MaxSlope` (sphere tracing: step by the distance that the slope
bound proves is clear), skipped entirely unless the ray enters the sphere
of `R + MaxHeight + Clearance`, at most 64 steps. Both are pure and
deterministic, independent of what the mesh has streamed.

- **An exhausted march is a hit** at its last proven-clear distance: never
  "no hit". So the cap always brakes for what it could not see past, and
  the hard stop is never the first thing to find a ridge.
- **Far steps may be band-limited**: a step of length s may evaluate
  `Height(D, s)` with the clearance widened by `OmittedBoundCm(s)`: cheaper
  and still conservative.
- **How far it must see is bounded by the skim cap** (below): the most any
  cap needs is the braking distance at the skim cap's top, about 2.5 x AGL
  (125 km at 50 km AGL), and `.RayToGround` asserts the 64 steps reach it at
  the `MaxSlope` `.SlopeBound` measures (if they do not, the step count is
  raised in (b), before anything else).
- **Cost:** marched **once a frame** per direction, at the frame's first
  substep; each later substep reduces the proven-clear distance by the
  distance flown along the ray (conservative), and marches again only if
  the direction has turned by more than a degree. Per substep only the
  footprint's eight points are sampled (decision 11). At 3-8 us a sample
  that is about 0.3-0.6 ms a frame at 60 Hz; a 2 s hitch (240 substeps)
  costs 240 x 8 samples, about 10-15 ms, once. Measured in slice (b).

**Three caps over solid ground, in the regime**, all pure, in
`ShipFlightSurface`, all weighted in across the regime's top by decision
8's w:

- **The skim cap.** Horizontal speed is at most
  `max(ds.Land.SkimFloor (20 m/s), AGL / ds.Land.SkimSeconds (2.5 s))`:
  20 km/s at 50 km (cruise's top, so the cap is slack at the regime's top
  and there is no step), 2 km/s at 5 km, 200 m/s at 500 m, 20 m/s at 50 m
  and below. The ground then flows past at the same apparent rate at every
  height -- which is how low flight reads, and exactly what bounds tile
  streaming (decision 6) and the ray's lookahead. A pilot who wants 20
  km/s climbs for it. `HOLDING OFF` says when it holds the lever back by
  more than 5 %, as the soft cap does. Sign-off item 9.
- **Along the ground (the soft cap, carried through).** The horizontal part
  of the cruise target is capped along its own direction by the ray to
  `ground + clearance` at the ship's height, with the approach law below.
  A ridge ahead slows the ship to rest against it, as a world does from
  orbit; the ship still goes where it points.
- **Down (the descent cap).** The downward radial speed is capped at
  `ShipFlight::GroundApproachSpeed(FootprintClearance)` (decision 11:
  the least height of any footprint point above the ground), whatever the
  levers and the starved sink ask.

**The approach law**, with a knee the boosters can follow:

    D1 = 0.8 x A x N^2
    GroundApproachSpeed(D) = max(TouchdownSpeed,
                                 D <= D1 ? D / N
                                         : sqrt((D1 / N)^2 + 2 x 0.8 x A x (D - D1)))

with `TouchdownSpeed` 0.5 m/s, A the boosters' present acceleration, and N
= **`ds.Land.ApproachSeconds` (4 s, clamped to at least 0.5 s)** -- its
own CVar, never `ds.Drive.HoldSeconds`, whose documented "0 or less is the
braking curve alone" would otherwise divide by zero or give back the
57 m/s catch. The exponential branch demands a deceleration of D/N^2, which
at D1 is exactly the braking curve's 0.8 A; above D1 the braking parabola
is tangent to it. So the demanded deceleration never exceeds 0.8 A anywhere
and **an inertial ship can follow the law all the way down** -- the trap
`CruiseSubStep`'s comment records for the drive's hold (whose knee demands
1.6 A) does not recur. At full thrust D1 is 25.6 km; starved, 6.4 km.
`MaySpeed` takes the **larger** of the hold and braking terms, which at 2
km/s^2 allows 57 m/s at a metre off the ground: a catch that looks like an
impact. The approach law takes the smaller, so the last hundreds of metres
are an exponential ease with a 4 s time constant, and contact is at 0.5
m/s. Its `D / Step` bound is kept: no substep crosses the surface.
`SecondsToGround` integrates this same curve, so the ETA is exact.

**How long the descent takes** depends on how far above the local ground
it starts: at the full 200 m/s sink the cap binds under 800 m, the ease
from 800 m to 2 m takes 24 s and the last 2 m 4 s, so
**`t = (H - 800 m) / 200 m/s + 28 s`**: 74 s from 10 km above the local
ground, 124 s from 20 km. The drive floor is 10 km above the highest peak,
so from the floor a descent takes **75-125 s** depending on the ground
below: the vision's "a minute or two".

**The hard stop, extended -- for the ground only.** After the caps and
after the attitude integration, if any footprint point is under the ground
(terrain rising faster than the ray foresaw, a `PlaceShip`, a priors
reload, or the pilot pitching a corner down, decision 11), the velocity
into the ground normal is removed and **the origin is lifted along up by
the deepest penetration**: a ship under the ground may always climb, and is
never pushed down. This reverses, **for the ground alone**, the existing
rule that a ship under a floor "is never lifted: a ship does not teleport
because a number changed". The reason is that the ground is drawn: a ship
under it would sit visibly inside rock. **Sphere floors keep "never
lifted"**: raising `ds.Flight.Floor` in play still teleports nothing.
`.HardStopLiftsGroundOnly` tests each.

**The invariant**, `DeepSpace.Ship.Landing.GroundAlwaysCatches`:

- **No footprint point ends a substep more than 1 cm under the ground**, at
  every frame chop.
- **Contact speed** is defined as the speed of the lowest footprint point
  along the ground normal under it, at the substep it first comes within 1
  cm of the ground. **For contacts reached by the levers, it is at or under
  `TouchdownSpeed`.** Horizontal speed is not part of it (the skim cap and
  the settle bound that, decision 11).
- **In the lever sweep, the hard stop never fires**: the caps must foresee
  every contact. Hard-stop lifts are tested separately
  (`.HardStopLiftsGroundOnly`: `PlaceShip` under the ground, a priors
  reload, a nose pitched down at 1.5 m), where the lift is at most the
  rotation's own rate (0.2 rad/s x the hull's 17.6 m reach: 3.5 m/s).
- **The sweep grid**, on a cheap fixture relief with the real `MaxSlope`
  (two crossed sines), full product: cruise lever {STOP, 1 m/s, 200 m/s,
  2 km/s, 20 km/s, astern 200 m/s} x vertical {HOVER, SINK 0.5, 5, 200
  m/s, CLIMB 200 m/s} x start {drive floor, 5 km, 500 m, 20 m, 2 m over
  flat; 2 m over a 15-degree slope, a crater rim, a ridge} x frame chop
  {1/240, 1/60, 1/20, 0.5, 2 s} x thrust {1, 0.25} x starved sink {0, 2
  m/s}; each flight to rest or 150 s. Then a pairwise-covering subset on
  the real relief of Baemsekai IV and III. Budget: under 30 s.

**Rejected: `MaySpeed` as it is, down to the ground.** The 57 m/s catch.
**Rejected: the approach law with `min(D/N, braking)` and no knee.** It
demands 1.6 A at the crossover; an inertial ship rides above it, and
`HOLDING OFF` and the ETA misreport there. **Rejected: capping the whole
velocity by the ray along it.** At grazing angles it slows horizontal
flight during a gentle descent, which reads as the ground dragging the
ship. **Rejected: lower `FloorFor` to the ground.** The drive would meet
mountains at 0.1 c. **Rejected: F refused under the floor.** A refused key
is the game saying no; the explicit mode says what the ship is doing
instead. **Rejected: a skim cap from residency.** Motion that waits on
streaming is a loading screen.

**Cost to change:** medium. The laws are small and pure; landing's feel
rests on them.

### 11. Touchdown: the ship settles onto the slope on three feet, and LANDED holds it

**The footprint** is built in **slice (b)**, not (c), because the descent
cap and the hard stop read it from the first hover: measured at the origin
alone, a 15-degree slope under a 26 m hull puts a corner 3 m into the
ground. It is eight points in ship space, as layout data: **four gear
feet** at the gear's foot height, `Z = -GearClearance` (-150 cm), about
(-700, -300), (-700, 400), (1600, -100), (1600, 200) cm, and **four belly
corners** at the belly, `Z = -10` cm, at the hull's plan corners.
`GEAR` and `BELLY` in `Tools/hauler_layout.py`, mirrored as
`ShipLanding::GearFeet` and `ShipLanding::BellyCorners`, held equal by
`test_placement.py` reading the C++ (as it does the dressing tags).

**`FootprintClearance`** is the least height of any footprint point above
the ground under it: 0 when a foot touches. The descent cap, the hard stop,
the invariant and contact all read it (decision 10).

**`EGroundContact {Airborne, Settling, Landed}`** lives in
`FShipFlightState` and changes only inside `SubStep`: **no new write path**.

**Resting on three feet.** Four rigid feet almost never touch a real
surface at once: the relief has bands down to 5 m with the same slope at
every scale, and the gear spans 23 m by 7 m, so the four ground heights
are almost never within a centimetre of one plane. A rigid hull rests on
three. So the **rest plane** is defined, not fitted: take the ground point
under each foot; of the two ways to split the four into triangles (along
either diagonal), the **upper** one -- the facets with the other point on
or below them -- has exactly one facet whose triangle contains the
origin's projection. That facet's plane is the rest plane, and its three
feet are the tripod; the fourth foot hangs above the ground by however
much the ground falls away under it (below the belly, unseen from inside).

- **Settling**: `FootprintClearance` is under `ds.Land.SettleBand` (8 m,
  more than the 3.4 m corner drop on a 15-degree slope) and the ship is
  descending. A corrective pitch and roll rate turns body +Z toward the
  **rest plane's normal**, at most `ds.Land.SettleDegPerSec` (4 deg/s), in
  proportion to `1 - FootprintClearance / SettleBand`. The pilot's
  attitude input is **added** to it, never suspended, and yaw is the
  pilot's alone: the pilot flies attitude all the way (ruling 3). The
  horizontal speed is capped at `FootprintClearance / ds.Land.ApproachSeconds`
  in this band while descending, so the ship does not skid in; hovering in
  the band keeps the skim cap's speed. A pilot holding pitch or roll
  against the settle can keep it from finishing; then the ship rests on
  whatever feet touch, the descent at rest, still Settling, and LANDED
  comes when the pilot lets go.
- **Landed**, entered **only from Settling, with the vertical lever not
  asking a climb**: no foot more than 1 cm under the ground; the tripod's
  three feet within 2 cm of it; the origin's projection inside the
  tripod's triangle; the descent at rest. Then velocity and angular
  velocity are exactly zero, the orientation is held on the rest plane,
  `HoldWant` drops to 0, the HUD says LANDED. **Touchdown sets the cruise
  lever to STOP and the vertical lever to HOVER**: landing is a stop.
  Attitude and cruise keys do nothing while landed: **no taxiing**.
- **Take-off**: a fresh press of Space (the vertical lever above zero)
  releases to Airborne. Holding it from before landing does not: the
  hold is spent at touchdown, so a ship does not bounce. **A starved ship
  always lifts**: the starved sink is never applied while the lever asks a
  climb (decision 5), and Landed cannot be re-entered while it does.
  `DeepSpace.Playtest.StarvedShipTakesOff`: booster weight 0, landed, hold
  Space, and the ship climbs.

**Rotation near the ground.** The pilot can pitch a corner down at any
height. After the attitude integration, if any footprint point is under
the ground, the hard stop lifts the origin by the deepest penetration
(decision 10): at 0.2 rad/s the nose rises at most 3.5 m/s, the ship
levering itself up on its own gear, never through it.
`GroundAlwaysCatches` sweeps attitude inputs at hover heights of 0-8 m and
asserts on every footprint point.

**The settle and contact are tested on real relief**, not only planes:
`.Settle` and `.ContactOnce` land on 200 sampled patches of Baemsekai IV's
and III's ground, including crater rims and the steepest slopes the
sample finds, and assert the tripod, the rest plane and LANDED each time.

`PlaceShip` and `JumpTo` reset contact to Airborne. **A priors reload under
a landed ship** re-seats it on the new ground along the radial (the ground
hard stop's lift, applied while Landed) if the world is still Solid; if the
reload made it an ocean or a giant, contact becomes Airborne and the ship
holds where it is, under the new floor sphere, which it may climb out of
and is never lifted by (the sphere rule). A tuning tool may do that; the
world is what the priors say.

**The settle is a new orientation writer.** `JumpTo` deliberately never
touches orientation, because turning the ship visibly turns the dome. The
settle does turn it -- slowly, a few degrees at 4 deg/s, at the end of a
descent the pilot is flying, and it reads as the ship sitting down. It is
recorded as an ADR 0005 amendment (*Documentation*), and
`DeepSpace.Ship.FlightAuthority`'s accounting of writers is extended.
Sign-off item 12.

**Walking gravity stays ship -Z.** Landed on a slope, the deck stays level
for the walker and the landscape outside is tilted. That is a design fact,
stated rather than discovered; `PlaceCamera` assumes it (ADR 0005:95-99).

**The fold from the ground.** The fold opens by itself once engaged,
charged and within the cone, which is ruled acceptable because the pilot
aimed. The settle rotates the nose on the game's initiative, so **the fold
is held while Settling**: the settle can never be what brings the nose into
the cone. Landed, the orientation is fixed, so an engaged, charged jump
opens from the ground only if the landed nose already points along the
course; otherwise the pilot lifts off (a fresh Space), aims, and it opens
in flight. `JumpTo` clears the contact. Sign-off item 11.

**Rejected: suspend the pilot's pitch and roll in the last metres.** The
game taking the controls. **Rejected: a single-point contact at the
origin.** Corners in the ground. **Rejected: all four feet within 1 cm.**
Over-constrained on real ground: LANDED would almost never come.
**Rejected: a least-squares plane with a residual tolerance.** It rests the
ship through the ground on one side and above it on the other; a tripod is
what a rigid hull does. **Rejected: compliant legs (a stroke per foot).**
Better-looking contact, but it is landing gear as a mechanism, a non-goal.
**Rejected: LANDED ends when the boosters are refed.** A starved hover
sinks, lands, is refed, lifts, starves: LANDED latches until the pilot asks
to climb.

**Cost to change:** low to medium; the words and the latch are what the
playtest judges.

### 12. The HUD below the floor: altitude above ground, vertical speed, LANDED

All words come from getters on the flight state, never recomputed in the
widget, and the strings are pure static functions
(`ShipHUDWidget::GroundLine`, `VerticalWords`) so a test reads exactly what
is drawn. **Two altitudes, named and distinct:**

- `GetGroundAltitude()` -- **the ship's origin above the ground directly
  below it** (radially), clearance included. This is the number the HUD
  prints: `1.5 M ABOVE GROUND` hovering at rest over flat ground, the
  deck's height above the rock.
- `GetFootprintClearance()` -- the least height of any footprint point
  above the ground (decision 11): 0 when a foot touches. This is what the
  descent cap, the settle band, the hard stop and contact read. The HUD
  never prints it.

with `GetVerticalSpeed()` (radial, signed) and `GetContact()`.

- **The altitude corner** (bottom left), in the near regime over solid
  ground: `840 M ABOVE GROUND · SINKING 3 M/S`, `· HOVERING`,
  `· CLIMBING 12 M/S`, and at rest on the ground `LANDED`. `HOLDING OFF`
  stays for the soft cap against a ridge and for the skim cap.
  Above the regime it reads as today: `10.2 KM ABOVE Kessa IV`.
  `AltitudeWords` already prints whole metres under a kilometre, and
  learns to print tenths under ten metres (`1.5 M`). **No time to the ground** --
  this corner has no destination, and the rule stands.
- **The motion line** gains the vertical lever where it is live, after
  `LeverWords`' style: `NOW · CRUISE 50 M/S · HOVER`, `· CLIMB 5 M/S`,
  `· SINK 3 M/S`, and `· ABOVE THE GROUND'S REACH` where decision 8's
  blend has taken it out. The measured vertical speed in the altitude
  corner beside the lever's asked rate makes a starved sink under a
  `HOVER` lever legible as a fact, not a warning. `DRIVE ABOVE THE FLOOR`
  (decision 10) dims the drive's word and puts cruise's in ink.
- No colour change, nothing that blinks, no percentages.

**The target line's ETA** keeps its promise to name the moment the ship
arrives, which means it must be measured to where the ship will actually
stop. **In cruise over a solid target, at every altitude**, it is the time
to the ground (`FootprintClearance` 0) under the law the ship is really
flying: the braking curve above the regime, the approach law with its knee
(decision 10) inside it, and the skim cap where the path is shallow
(`ShipFlight::SecondsToGround`, the counterpart of `SecondsToFloor`).
**`FloorFor` is used only in Drive and SpoolingDown**, whose floor it is.
`GetTargetView` picks the floor by the lever the ship is flying on, not by
the mode's name (in `DriveBelowFloor` it is cruise's). Today's cruise ETA
counts to the drive floor, which under this spec a cruising ship passes
through at speed: that would name a moment when nothing happens.
`DeepSpace.Playtest.EtaCountsDown` gains a leg that flies cruise from above
the drive floor to the ground and asserts it counts down a second a second.
Sign-off item 13.

**Cost to change:** low: words.

### 13. What can be landed on

`FSkyBody::Ground` is `Solid` for Barren, Ice and Terrestrial, and `None`
for Ocean and GasGiant, derived in `FromSystem` from `EPlanetKind`. Stars
are `None`. Over `None` the floor is today's sphere. **Terrestrial worlds
are all land in slice 1**: the data has no sea on them, and a sea level
needs a datum, a sea-fraction prior and a floor over water (*Open
questions*). The vision's "set down in the ocean for water" is deferred,
not changed.

**Cost to change:** low: a table.

### 14. Black sky, still worlds

No atmosphere, no sky colour, no fog (ruling 7); `verify_level.py`
already forbids them. Worlds do not spin and orbits stay frozen (ruling 9),
so the ground's frame is the universe's, "up" is radial, and landed means
at rest in universe space. When worlds spin, landed becomes co-rotating,
and that is the reference-frame handoff ADR 0005 defers.

## Seams with work in flight

- **`feat/speed-bands`** owns `ShipFlightState.*`, `ShipDriveLever.*`,
  `ShipFlightSurface.*`, `ShipSubsystem.*` and `ShipHUDWidget.*` edits
  landing will touch. **Slice (b) starts after it merges**; every number
  here (2 km/s^2, the log lever, 200 m/s astern, 0.1 c) is its.
- **`fix/surface-artifacts`** (`037e848` on its branch) owns
  `SkyProjection.*`, `ShipSky.*`, `SkyMaterialContract.h`,
  `setup_sky_materials.py` and a `DeepSpace.Build.cs` edit. **Slice (a)'s
  material work starts after it merges** (it adds `BodyAxisX`/`Y` to
  `M_SkyBody`, which slice (a) rewrites). It ships **no render harness**:
  the rendered parity probe, `Eyes.WorldReliefParity`, is this spec's own
  deliverable (track R).
- `fix/map-tracking` and `fix/chart-parity` touch the map and the chart,
  not landing's files.

## The anti-chore audit

| Thing | Could it tell the player they are behind? | Why not |
|---|---|---|
| Gravity | a load to keep up with | the boosters always hold every lever; effort is watts and sound |
| Hovering | a cost for staying put | the hold is paid only under a solid world's drive floor, where the pilot chose to go; parked at any floor, between worlds or landed, it is free, and the 2026-09-26 "whole at rest" ruling holds everywhere a ship can be parked |
| The hold's hiss | a noise imposed on staying put | silent wherever a ship can be parked; under the floor it never exceeds cruise's hiss |
| The starved sink | motion on its own, with time | only under the floor; never while climbing; ends at rest; shown as a rate, never a warning. It can undo a hover when the jump winds on a heavy world: sign-off item 7 |
| Heavy worlds | a place you cannot leave, or a slow grind out | the climb top depends on gravity alone, 50-200 m/s, never on the split, so nothing is rebalanced to leave; the longest climb-out (6 min 40 s) is sign-off item 6 |
| The power split | a throughput knob | the split changes the hold's watts and acceleration, never a climb or cruise top |
| The descent | a wait | 75-125 s from the floor, the vision's "a minute or two"; the approach is the content |
| The skim cap | a speed limit | the ground flows past at one apparent rate at every height, which is how low flight reads; the pilot climbs for speed |
| Terrain streaming | a loading pause | residency never gates motion; a real tile is always drawn under the floor; the proxy stands in only above it |
| Touchdown | a skill check, a crash | the ground always catches, at 0.5 m/s |
| The drive under the floor | a refused key | F is never refused; the ship flies cruise, says so, and the drive takes over above the floor |
| The fold from the ground | the game acting on its own | held while the settle turns the ship; landed, only the pilot's own aim opens it |

## Deliberate fakes, and what they cost later

- **The boosters hold without limit.** No honest thruster holds 27 g at
  Baemsekai's floor, or 56 g at the smallest dwarfs'. Cost: none until fuel
  or thrust limits exist.
- **Effort is felt only under the floor.** Gravity is real everywhere, but
  holding it costs watts and makes noise only where the pilot flew down to.
  Cost: if thrust ever becomes honest, the scope must be re-derived.
- **The starved sink is a designed bias, not physics.** Cost: if thrust
  ever becomes honest, the sink must be re-derived.
- **The relief grows in between 50 km and the drive floor.** The drawn
  mountains rise as the ship descends; the flight always has the full
  relief, and the ship is 10 km above every peak there. Cost: none unless
  the handover rises.
- **Smooth proxy limbs above 50 km.** Cost: one constant (the handover).
- **No detail finer than 130 m on mid-distance ground.** Per-pixel bands
  stop where the orbit's do; finer relief comes only from vertices, near
  the ship. Cost: detail textures, or per-pixel noise in tile-local
  coordinates.
- **Unlit ground, no shadows, no terrain shading of the deck.** The eclipse
  is still disc overlap; a ridge between the ship and a low sun does not
  darken the cockpit. Cost: a lit material or a horizon march toward the
  sun in `SunVisibleFraction`.
- **Walking gravity is ship -Z on a slope.** Cost: the frame handoff, with
  going outside.
- **No sea on terrestrial worlds.** Cost: a datum, a prior, a floor.

## Fixture worlds

Universe seed 20260925 (`0x135283D`, the ini's), priors as committed.
The home system, Baemsekai (sector (-1, -1, 0) slot 0, 0.10 M_sun), holds
all but one, so a playtest needs no jump:

| Use | World | Kind | Mass, radius | g (world + star) |
|---|---|---|---|---|
| first landing playtest; `LandsGently`; `.Settle`, `.ContactOnce` patches; slice (b) and (c) done-when | **Baemsekai IV** (`ds.Sky.Goto 4 30`) | barren | 0.68 M_E, 0.90 R_E | 0.84 + 0.09 g |
| heavy world: `HeavyWorldStillClimbs`, the starved sink, the jump-winding sink | **Baemsekai III** (`ds.Sky.Goto 3 30`) | barren | 3.21 M_E, 1.39 R_E | 1.66 + 0.31 g |
| terrestrial landing (slice c) | **Baemsekai V** (`ds.Sky.Goto 5 30`) | terrestrial | 0.55 M_E, 0.84 R_E | 0.77 + 0.03 g |
| a star's pull at a floor: `JumpCanWindAtFullSpeed` wholeness | **Baemsekai I** (its drive floor) | barren | 0.26 M_E, 0.68 R_E | 0.56 + 3.9 g |
| ice landing (slice c) | **Gasfe VII** (6.69 ly; one jump) | ice | 1.21 M_E, 1.05 R_E | 1.09 g |

Each test names its world by seed, system and orbit index, so a change of
priors that moves them fails loudly rather than landing somewhere else.
`ds.Sky.Goto <body> <km>` places the ship that far above the datum, facing
the world; it learns to place over ground at low altitude in slice (b)
(through `PlaceShip`, so the ground hard stop lifts it if needed).

## Implementation outline

### New files

- `Shaders/Private/WorldRelief.ush` -- the shared noise core (the scalar,
  no-swizzle subset; `WR_`-prefixed).
- (No shader module: the engine maps `/Project` to `Shaders/` itself. The
  planned `Source/DeepSpaceShaders/` was removed at R2.)
- `Source/DeepSpace/Surface/WorldReliefParams.h` (plain data, `EGround`),
  `WorldRelief.{h,cpp}`, `TerrainQuadtree.{h,cpp}`, `TerrainTile.{h,cpp}`
  (pure), `WorldGround.{h,cpp}` (`AWorldGround`).
- `Source/DeepSpace/Ship/ShipVerticalLever.{h,cpp}`, `ShipLanding.{h,cpp}`
  (footprint, rest plane, contact, settle; pure), `ShipGravity.{h,cpp}`
  (`GravityAt`).
- `Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp`, `Tools/eyes.sh`.
- Tests (below).

### Changed files

- `Universe/UniverseUnits.h` (GM), `StarSystem.h` (`FPlanet::ReliefKm`),
  `StarSystemGenerator.*` (`PlanetSeed` public, the relief draw), `GenPriors.*`,
  `Config/DefaultGame.ini`, `GenSeed` (`SurfaceSeed`).
- `Sky/SkySystem.*` (`GravParam`, `Ground`, `Relief`), `SkyProjection.*`
  (true-scale branch, clamp kept), `ShipSky.*` (hide the proxy on the
  ground's say-so; retire `ds.Sky.Relief`/`.Craters` as live knobs;
  `ds.Sky.Goto` at low altitude), `SkyMaterialContract.h`,
  `Tools/sky_material_contract.json`, `Tools/setup_sky_materials.py`
  (Custom node, the probes, `M_SkyGround`, the summed craters),
  `Tools/sky_probe.py`.
- `Ship/ShipFlightState.*` (gravity getters, vertical target, the regime
  blend, `DriveBelowFloor`, contact), `ShipFlightSurface.*`
  (`FFlightSurface::Ground`, `RayToGround`, `GroundApproachSpeed`, the skim
  cap, `SecondsToGround`), `ShipSubsystem.*` (`UpdateSurfaces` wells and
  grounds, `FloorFor` over peaks, `ApplyHelm`, `AllStop`, `ApplyAllocation`,
  `SetVerticalLever`, `GetTargetView`), `ShipPowerState.*`
  (`SplitBoosters`), `ShipHumVoice.*`, `ShipHumComponent.cpp`.
- `Player/DeepSpaceCharacter.*` (vertical keys, spent holds, Jump gated
  seated).
- `UI/ShipHUDWidget.*`, `UI/TargetMarker.*`.
- `Tools/setup_flight_input.py`, `Tools/hauler_layout.py` (`GEAR`, `BELLY`),
  `Tools/test_placement.py`, `Tools/build_hauler.py` (`hauler_ground`),
  `Tools/verify_level.py`, `Tools/procgen_corpus_contract.json`,
  `Tools/procgen_corpus.py`.
- `DeepSpace.Build.cs` (`ProceduralMeshComponent`), `DeepSpace.uproject`
  (the shader module).

Every new field on `FShipFlightCommand`, `FShipFlightLimits`,
`FFlightSurface` and `FSkyBody` is a header change: `./rebuild.sh --force`.
None is a `UPROPERTY`, so no Blueprint breaks; `BP_DeepSpaceCharacter` is
recompiled once, by `setup_flight_input.py`, for the two new actions.

### CVars

| CVar | Default | Lives in |
|---|---|---|
| `ds.Land.GearClearance` | 150 cm | `ShipSubsystem.cpp`, from `ShipLanding::DefaultGearClearanceCm` |
| `ds.Land.SettleBand`, `.SettleDegPerSec` | 8 m, 4 deg/s | `ShipSubsystem.cpp`, from `ShipLanding` |
| `ds.Land.TouchdownSpeed` | 0.5 m/s | `ShipSubsystem.cpp`, from `ShipFlight::DefaultTouchdownSpeed` |
| `ds.Land.ApproachSeconds` | 4 s, clamped to at least 0.5 s | `ShipSubsystem.cpp`, from `ShipFlight::DefaultApproachSeconds` |
| `ds.Land.SkimSeconds`, `.SkimFloor` | 2.5 s, 20 m/s | `ShipSubsystem.cpp`, from `ShipFlight` |
| `ds.Land.Regime` | 50 km (leaves over 55 km) | `ShipSubsystem.cpp` |
| `ds.Land.DriveHandback` | 500 m | `ShipSubsystem.cpp` |
| `ds.Vertical.Top`, `.Sweep`, `.HeavyFloor` | 200 m/s, 0.25/s, 0.25 | `ShipSubsystem.cpp`, from `ShipVerticalLever` |
| `ds.Boosters.HoldWatts`, `.StarvedSink` | 150 W per g (cap 3 g), 2 m/s; both only under a solid world's drive floor | `ShipSubsystem.cpp` |
| `ds.Hum.HoldHiss` | 0.35 at the 3 g cap, never above `ds.Hum.CruiseHiss` | `ShipHumComponent.cpp` |
| `ds.Terrain.SplitFactor`, `.MaxTiles`, `.BuildTasks`, `.UploadsPerFrame`, `.Show` | 2.0, 2,500, 2, 4, 1 | `WorldGround.cpp` |

Commands: `ds.Terrain.Describe`; `ds.Sky.Goto` as above.

## Tests

All pure where possible, headless, siblings with no children (a test path
with children becomes a group and silently stops running), and **each
proven able to fail with `Tools/mutate.sh`** before it is trusted.

- **Surface, pure:** `DeepSpace.Surface.WorldRelief.KnownValues` (the hash
  exactly, the noise to 1e-6, at inputs computed by hand from the HLSL
  formulas); `.Deterministic`; `.SeedMatchesSky` (the quantised offsets
  equal `ShipSky::SurfaceSeed`'s); `.Bounds` (no sample of 100,000
  directions exceeds `MaxHeightCm`/`MinHeightCm`; oceans and giants 0);
  `.Gradient` (agrees with finite differences, craters included);
  `.CratersContinuous` (no step across any bisector); `.BandLimit` (nothing
  finer than 5 m); `.Footprint` (a footprint removes exactly the faded
  bands, within `OmittedBoundCm`); `.SlopeBound` (`MaxSlope` is never
  exceeded).
- **Rendered, outside `./test.sh`:** `Eyes.WorldReliefParity` (decision 1:
  C++ against the GPU at five footprints, the new graph against the legacy
  one, and `M_SkyGround` against `M_SkyBody` at 50 km; 1e-3).
- **Procgen:** `DeepSpace.Universe.Relief` (per kind, within the guarantee;
  1/g ordering; the `"relief"` stream moves no other draw); the corpus
  columns; `DeepSpace.Universe.Seed`/`.Stream` untouched.
- **Terrain, pure:** `DeepSpace.Surface.Quadtree` over a sweep of altitudes
  and positions (no overlaps, the horizon cap covered, the 2:1 neighbour
  constraint, the ship's chain at `MaxLevel` below a set altitude, `MaxTiles`
  coarsening the far levels first and never the ship's chain; it reports
  the tile and triangle counts per altitude that set the budgets);
  `DeepSpace.Surface.Tile` (shared edges of same-level neighbours
  bit-identical; skirts cover the edge error and the omitted bands; mesh
  versus analytic under the ship within `GearClearance / 10`; heights equal
  the sphere at the handover and the full relief at the drive floor; the
  largest vertex jump at a split, in 4K pixels, reported).
- **Terrain, actor** (spawned before `World->BeginPlay()`, builds flushed
  inline): `DeepSpace.Surface.GroundActor` (sections read back; tile
  transforms equal `UniverseToWorld` of their pivots; `CastShadow`,
  distance-field, indirect-lighting and ray-tracing flags off; the proxy
  hidden once the coarse cut is resident and never drawn under the drive
  floor; uploads per frame capped).
- **Sky:** `SkyProjectionTest` at 1.5 m above ground (the star's proxy
  within `FarProxy`).
- **Gravity and power:** `DeepSpace.Ship.Gravity` (Earth 979.85 cm/s^2, Sun
  at 1 AU 0.593; a full-lever path identical with and without wells);
  `DeepSpace.Ship.Power.SplitBoosters` (the table in decision 5);
  `JumpCanWindAtFullSpeed` extended (whole at rest at the home opening
  placement, AT THE FLOOR over Baemsekai III, at Baemsekai I's floor, at a
  giant's floor; exactly 450 W landed); `HumVoice` (the hold term; never
  above `CruiseHiss`; silent at every floor and at touchdown).
- **Flight, pure:** `DeepSpace.Ship.Landing.GroundAlwaysCatches` (decision
  10's grid and definitions; plus attitude inputs at hover heights of 0-8 m
  on every footprint point); `.HardStopLiftsGroundOnly`; `.ApproachLaw`
  (the knee never demands more than 0.8 A; `ApproachSeconds` at 0 is
  clamped); `.RayToGround` (an exhausted march is a hit; the lookahead
  reaches 2.5 x AGL; a ridge at the skim cap's top is never crossed);
  `.SkimCap`; `.RegimeTop`; `.CruiseUnderDriveFloor`; `.DriveUnderFloor`
  and `.DriveTakesOverAbove` (both ways, no limit cycle); `.Settle` (the
  rest plane reached on real relief; pilot yaw honoured; pilot pitch adds);
  `.ContactOnce` (Airborne -> Settling -> Landed once, never flickering, on
  200 real patches); `.TakeOff` (fresh Space only); `.FoldHeldWhileSettling`.
- **Playtest flights** (`DeepSpace.Playtest.*`, through the pawn and
  `IMC_Default`): `KeysLiftTheShip` (Space and C read from the context and
  flown); `LandsGently` (Baemsekai IV from its drive floor, levers
  mashed); `HoverHoldsWhenPilotStands`; `StarvedSinkLandsGently`;
  `StarvedShipTakesOff`; `HeavyWorldStillClimbs` (Baemsekai III, and a
  synthetic 3.3 g world); `ParkedShipNeverDrifts`; `EtaCountsDown`'s
  landing leg (cruise from above the drive floor to the ground).

**Tests that pin today and will move:** `ShipDriveTest`'s `FloorFor`
numbers over solid worlds (decision 10); `InSystemJumpTest`'s floor
numbers; `ShipHUDAltitudeTest`; parts of `SliceLoopTest`, `PlaytestTest`
and `TargetMarkerTest` that fly cruise onto a rocky world (cruise now flies
to the ground); `SkyProjectionTest`'s rendered floor; `SkyBodyMeshTest`'s
premise; `HumVoiceTest`. Each is changed in the commit that changes the
behaviour, with the reason in the message. `FlightCruiseFloor` does not
move: gravity never enters the velocity (decision 4). Fixtures that build
`FFlightSurface` without a ground are unaffected.

## The three slices

### Slice (a): one height function, the orbital look unchanged

After `fix/surface-artifacts` merges. First the spike (decision 1),
with its go/no-go. Then `WorldRelief` (C++ and the shared `.ush`; `Height`
from the detail bands, craters in the material only, as today), the
engine's own `/Project` mapping held by a test, `M_SkyBody` rewritten onto the Custom node, the
probe materials and `Eyes.WorldReliefParity`, the relief prior and
`FPlanet::ReliefKm` (drawn and in the corpus, but not yet read by the
look), `FSkyBody` gaining `Ground`, `GravParam` and `Relief`.

**Done when:** `./build.sh` and `./test.sh` are green with the surface and
procgen tests above, each mutate-proven; `Eyes.WorldReliefParity` passes
(the new graph against the legacy one, and C++ against the GPU, at five
footprints over 256 x 256 samples, by the measured floor as a rule -- the
rulings at R2 and R4; the legacy half was run at R4 and retired with the
probe, and what remains holds Baemsekai IV through `FWorldRelief` and a
giant at stretch 6 through the file in double, to the recorded floor); the
developer flies to Baemsekai III, IV and V and sees no change. Nothing a
player can see is new: that is the point.

### Slice (b): terrain, gravity and the vertical lever -- fly down and hover over real ground

After `feat/speed-bands` merges. Gravity, `HoldWant` under the floor,
`SplitBoosters` and the hum's hold term; the vertical lever, its keys and
its HUD words; the near regime with its blend and hysteresis, the
horizontal composition and the skim cap; `FloorFor` over peaks, the two
floors and `DriveBelowFloor`; `RayToGround`, the approach law, the ground
hard stop; **the footprint** (`GEAR`, `BELLY`, `FootprintClearance`); the
quadtree, tiles, `AWorldGround`, `M_SkyGround`, the handover at 50 km and
the morph; relief switched to `PeakCm` and the craters to summed kernels
(the orbital shading flattens, with before/after frames to the developer).
First day: measure a tile's build cost, PMC's memory and render-thread cost
at the simulated counts, and revise the budgets (or take sign-off item
15's fallback). No LANDED yet: the ship comes to rest with a foot on the
ground and the corner reads `1.5 M ABOVE GROUND · HOVERING` over flat
ground.

**Done when**, over Baemsekai IV from its drive floor:

- C brings the ship down through the handover with no hole, no flicker, no
  brightness step, and no jump at the handover (tile heights equal the
  sphere there, tested); the largest LOD split jump is reported in 4K
  pixels for the developer to judge (sign-off item 16);
- it comes to hover over real ground in `(H - 800 m) / 200 m/s + 28 s`,
  within 10 %, where H is the start's height above the local ground;
- under the drive floor the proxy is never drawn, and below 1 km AGL the
  mesh under the ship stays within 15 cm of the analytic ground, at the
  full sink and at the skim cap's top;
- standing up leaves it hovering; Space climbs, at 100 m/s over Baemsekai
  III; starving the boosters sinks it gently to the ground;
- the tests above for (b) are green and mutate-proven, and
  `Eyes.WorldReliefParity`'s 50 km case passes;
- **the frame at 4K (3840 x 2160)**, with the project's real anti-aliasing
  and upscaler on this machine's RTX 4070 Ti SUPER, stays within 16.6 ms,
  with `stat unit`'s game, render-thread and GPU times reported
  separately, and the tile counts at 50 km and at the ground (the budgets
  revised if not).

### Slice (c): touchdown and LANDED

`EGroundContact`, the rest plane and the tripod, the settle, LANDED, the
latch, take-off, the levers zeroed at touchdown, the fold held while
settling and from the ground, the ETA to the ground, the ADR 0005
amendment.

**Done when:** from any lever combination the ship settles onto the slope
and reads LANDED, at rest, the landscape through the windows; the pilot can
stand up and walk the landed ship; the hiss falls silent at touchdown and
the boosters' watts return to the split; a fresh Space lifts it off, starved
too; the tests above for (c) are green and mutate-proven; the developer
lands on Baemsekai IV (barren), Gasfe VII (ice) and Baemsekai V
(terrestrial).

## Parallel tracks and file ownership

Each track is a worktree under `.worktrees/`, building with `./build.sh`
and testing with `./test.sh` behind `Tools/ue_lock.sh`. Agents are briefed
with the machine's cap: at most 3-4 workers, `nice -n 19`, and terrain
build tasks at 2. **Every changed file has exactly one owner per slice.**

| Track | Slice | Owns | Waits on |
|---|---|---|---|
| **P: procgen** | a | `Surface/WorldReliefParams.h` (**first**, as a header R and F agree), `Universe/*` (GM, `ReliefKm`, `PlanetSeed`, `SurfaceSeed`), `GenPriors.*`, `DefaultGame.ini`, the corpus files, `Sky/SkySystem.*` | -- |
| **R: relief** | a | `Shaders/`, `Surface/WorldRelief.*` and its tests, `Tests/Eyes/WorldReliefParityTest.cpp`, `Tools/eyes.sh`; **in slice (a)** `setup_sky_materials.py`, `SkyMaterialContract.h` and `sky_material_contract.json` (`M_SkyBody`, the probes) | surface-artifacts merged; P's `WorldReliefParams.h` |
| **F: flight** | b, c | `Ship/ShipFlightState.*`, `ShipFlightSurface.*`, `ShipVerticalLever.*`, `ShipLanding.*`, `ShipGravity.*`, their pure tests, `Tools/hauler_layout.py` (`GEAR`, `BELLY`) and `Tools/test_placement.py` | speed-bands merged; P merged |
| **S: subsystem, power, input, HUD** | b, c | `ShipSubsystem.*` (**including `UpdateSurfaces`**), `ShipPowerState.*`, `ShipHum*`, `DeepSpaceCharacter.*`, `setup_flight_input.py`, `UI/ShipHUDWidget.*`, `UI/TargetMarker.*`, the Playtest tests | F's pure interfaces (agreed first as headers) |
| **T: terrain and sky** | b | `Surface/TerrainQuadtree.*`, `TerrainTile.*`, `WorldGround.*`, `DeepSpace.Build.cs`, `SkyProjection.*`, `ShipSky.*`, `Tools/sky_probe.py`, `build_hauler.py`/`verify_level.py` for `hauler_ground`; **in slice (b)** `setup_sky_materials.py` and the contract, handed over from R when (a) merges (`M_SkyGround`, the `PeakCm` switch, the summed craters in the material) and `Surface/WorldRelief.*` for the summed craters in `Height` | R merged |

Merge order: P first (its header before anything); R (slice a, one merge
after `Eyes.WorldReliefParity` passes); then F's pure core; then T and S in
parallel; slice (b) merges when its done-when holds; then F and S finish
(c). `ShipSubsystem.cpp` is S's alone: F's work reaches it only through
pure headers S calls.

## Documentation, after the merges

- CLAUDE.md: a *Landing* section (the two floors, `DriveBelowFloor`, the
  vertical lever and the regime, the skim cap, the footprint and the
  tripod, LANDED), the keys table (Space, C), the tunables table, the
  *Surface* architecture line, *The sky*'s floor paragraph and the
  handover's morph, and *The system map and the target*'s ETA (to the
  ground in cruise).
- **CLAUDE.md, *Screens, the pointer, and power*: the power paragraph is
  amended.** "Nothing in the model changes on its own with time" gains its
  one sanctioned exception, the starved sink, and why it is bounded: only
  under a solid world's drive floor, at most 2 m/s, never while climbing,
  ending at rest on the ground at no cost. And the boosters' hold is named
  as a want that exists only under that floor, so "staying put is never
  taxed" still reads true.
- ADR 0005 amendment: gravity (held, never in the velocity); the settle as
  an orientation writer from the tick; `JumpTo` zeroes velocity (the
  amendment text still says otherwise); the frame handoff deferred to going
  outside.
- ADR 0006 amendment: the shared `.ush` as one generator compiled twice.
- The sky spec: the handover replaces the proxy below 50 km, as it foretold,
  and the relief grows in.

## Risks

- **Custom-node errors pass the headless suite.** Mitigated by
  `Eyes.WorldReliefParity`, which must compile the node to draw, in slice
  (a)'s and (b)'s done-when; a broken material is grey, not subtle.
- **The shared file cannot reach parity.** The spike decides it
  before anything is built on it; the fallbacks are named (decision 1).
- **Tile counts and PMC.** Simulated at 860-2,200 tiles and 2-4.5 M
  triangles; PMC's CPU copies at 250-400 MB and its dynamic draw path are
  gated on the first task of (b), with a custom primitive as the fallback
  (sign-off item 15).
- **Per-frame repositioning of 1,000-2,000 components.** The ship is the
  origin, so every tile's transform changes every frame. Measure (game
  thread and GPU Scene uploads); if costly, re-pivot tiles in batches under
  shared parents, or take the custom primitive.
- **Tile build cost.** The per-sample estimate is a guess, and the summed
  craters cost more. If a tile costs 30 ms, the skim cap's demand (100-200
  tiles a second) outruns two workers: then `ds.Land.SkimSeconds` rises
  until it does not (a feel change, back to the developer), never a gate
  on motion.
- **The flattened orbit.** The developer may prefer today's look; then
  ruling 5 or 6 has to give (sign-off item 2).
- **The morph reads as mountains growing.** Over 150-200 s of descent it is
  slow; the playtest judges it (sign-off item 8).
- **Float/double drift between the material and the mesh at the handover.**
  The shared source reduces it to rounding; `Eyes.WorldReliefParity`
  measures it at 50 km.
- **Test churn with speed-bands.** Both edit the flight files; slice (b)
  starts from speed-bands' merged tree, never beside it.

## Decisions needing sign-off

Each is expensive to reverse, changes an earlier ruling, or is a choice a
reasonable person could make differently.

1. **One height function as a shared `.ush` compiled into both the C++ and
   `M_SkyBody` (through a Custom node, on the engine's own `/Project`
   shader mapping; the planned `PostConfigInit` module was removed at R2)**: a rewrite of the engine's noise in a scalar, no-swizzle,
   `WR_`-prefixed subset, proven equal to today's nodes by a rendered
   parity test, after a spike with a go/no-go. *Alternatives:* the
   file pasted into the Custom node by the setup script (no module); baked
   cubemaps from C++ (the look changes, 100 MB a world); the engine nodes
   kept, with an independent C++ port held to them by the same rendered
   test. *Recommended:* the shared file. *Cost to change later:* high:
   every consumer calls it, and the material is rewritten around it.
2. **The orbital look is unchanged in slice (a), then changes in slice (b)**:
   relief flattens to physical heights (Earth-like, from gravity), and
   craters become summed continuous kernels (overlaps add, none clipped at
   a bisector), so they can be a height. *Alternatives:* keep today's
   slopes and accept mountains of 26-50 km; keep today's look with an
   orbit-only exaggeration (the orbit then lies about the ground); keep
   today's clipped craters in the look only (the ground's craters then
   differ from the orbit's). *Recommended:* change at (b), with
   before/after frames. *Cost to change later:* medium; once the developer
   has landed on worlds, their heights are what they remember.
3. **The drive's floor is 10 km above the world's highest peak**, not above
   the mean radius. *Alternatives:* above the mean radius with peaks capped
   well below 10 km by guarantee (keeps `FloorFor` exact, but the drive can
   park a kilometre over a summit); 10 km above the local ground (a floor
   that moves under the ship at 0.1 c). *Recommended:* above the peak.
   *Cost to change later:* low in code; it sets where every landing
   begins, where the drive's ETA ends, the in-system jump's "outside every
   floor" and `FixWorld`, and moves `FloorFor`'s and `InSystemJumpTest`'s
   numbers.
4. **Under a solid world's drive floor the drive does not take the ship**:
   an explicit `DriveBelowFloor` mode in which the ship flies cruise and
   the vertical lever, Shift/Ctrl move cruise, the drive notch keeps its
   setting and its position is held at the ship's speed, and the drive
   takes over 500 m above the floor with the nose at or above the tangent.
   *Alternatives:* F refused under the floor; the drive auto-climbs to its
   floor; the drive allowed with a terrain-aware cap. *Recommended:* as
   specified. *Cost to change later:* low; it is how the two regimes meet.
5. **The vertical lever is live in cruise within 50 km of a world's cruise
   floor (leaving over 55 km), "up" radial; cruise there flies the nose's
   horizontal projection; across 40-50 km both blend out, so a climb on
   the vertical lever alone comes to rest near 50 km (`ABOVE THE GROUND'S
   REACH`)** and the pilot leaves on cruise or the drive. *Alternatives:*
   live everywhere in-system with up from the nearest world; the climb
   rate converted into the cruise lever at the top (a lever moving on its
   own); cruise along the nose (looking down dives). *Recommended:* as
   specified. *Cost to change later:* low in code, but it is the feel of
   every descent and climb-out.
6. **Gravity is exact in the sums and invisible in the trajectory; its
   effort is felt only under a solid world's drive floor, airborne**: 150 W
   per g (capped at 3 g) on the boosters' want, a hiss at most cruise's, a
   climb top of 200 m/s x max(0.25, min(1, g_E/g)) that the power split
   never changes, and a starved sink of 2 m/s. Climb-outs from the ground
   to a 20 km drive floor: 100 s at 1 g, 3 min 20 s at 2 g, 5 min 30 s on
   the heaviest solid world, 6 min 40 s beside a close red dwarf. This
   **narrows ruling 4's "felt as effort"** to where the pilot chose to fly
   down to, and so keeps the 2026-09-26 "whole at rest" reactor ruling
   intact. *Alternatives:* effort everywhere g is held (taxes rest at every
   floor, over oceans and giants forever, and at home: a change to the
   2026-09-26 ruling); a reactor grown by the 1 g hold; the climb top
   scaled by the booster share (a throughput knob); no slower climbs.
   *Recommended:* as specified. *Cost to change later:* low for the
   numbers; the scope is what keeps parking free.
7. **A power choice can undo a hover.** On a world over about 1.7 g,
   engaging the jump (or raising the lights' weight) starves the hold and
   the ship sinks gently to the ground (3 g: 0.84 m/s; Baemsekai III: about
   0.25 m/s), though ruling 2 says a hover survives the pilot standing up.
   The sink is the power model changing with time, CLAUDE.md's one
   sanctioned exception (*Documentation*). *Alternatives:* the hold paid
   off the top, before the split (nothing in the stock ship could then
   starve it, and the ruled sink would never happen); the sink only at a
   booster weight of 0 (an explicit act). *Recommended:* as ruled -- the
   sink as the trade-off the allocation is. *Cost to change later:* low.
8. **The handover to terrain is at 50 km, and the relief grows in**: tile
   heights morph from the sphere at 50 km to full at the drive floor (one
   fraction for the whole cut), the flight always on the full relief; the
   proxy may stand in only above the drive floor. *Alternatives:* a swap
   at 50 km (the landscape jumps by degrees); the proxy displaced on the
   GPU (the tearing again); the handover at the drive floor (inherits the
   magnification); higher, 150 km (real limbs, twice the tiles).
   *Recommended:* 50 km with the morph. *Cost to change later:* low
   (constants).
9. **The skim cap**: in the regime, horizontal speed at most max(20 m/s,
   AGL / 2.5 s) -- 20 km/s at 50 km, 200 m/s at 500 m, 20 m/s low down --
   so the ground flows past at one apparent rate at every height and
   streaming and the ray can keep up. *Alternatives:* no cap, and accept
   that the drawn ground falls behind and disagrees with the flight at
   speed; a cap from tile residency (motion waits on loading). *Recommended:*
   as specified, the numbers tuned in play. *Cost to change later:* low
   for the numbers; without some cap the ground cannot keep up.
10. **Touchdown: gear clearance 1.5 m, contact at 0.5 m/s, a 4 s approach
    ease (its own CVar) with a knee the boosters can follow, settling over
    the last 8 m at 4 deg/s with the pilot's input added; resting on a
    tripod (the upper facet under the ship), the fourth foot free;
    touchdown sets cruise to STOP and vertical to HOVER; no taxiing;
    take-off by a fresh Space, starved or not.** *Alternatives:* the
    pilot's pitch and roll suspended in the settle; all four feet on a
    least-squares plane; compliant legs; levers left as they were at
    touchdown. *Recommended:* as specified. *Cost to change later:* low;
    these are what the slice (c) playtest judges.
11. **The fold from the ground: held while Settling; landed, it opens only
    if the landed nose is already on the course; otherwise lift off, aim,
    and it opens in flight.** *Alternatives:* yaw allowed while landed (the
    ship pivots on its feet to aim, re-settling as it turns); no fold while
    landed at all. *Recommended:* as specified. *Cost to change later:*
    low.
12. **The settle is a new orientation writer**, the first thing but the
    pilot to turn the ship, recorded as an ADR 0005 amendment.
    *Alternatives:* no settle -- the ship lands at whatever attitude the
    pilot holds, resting on the tripod that attitude touches first.
    *Recommended:* the settle (ruling 3 asks for it). *Cost to change
    later:* low in code; ADR 0005's writer list is the contract.
13. **The target line's ETA in cruise over a solid world counts to the
    ground at every altitude** (today's counts to the drive floor, which a
    cruising ship now passes through). *Alternatives:* no ETA in cruise
    under the regime; the ETA to the drive floor above it and to the ground
    under it (a jump in the number at the floor). *Recommended:* to the
    ground. *Cost to change later:* low: words.
14. **The ground is unlit, shaded by `M_SkyBody`'s law, per pixel as the
    orbit is: no brightness step at the handover, no ship shadow, no
    terrain shading of the deck.** *Alternatives:* lit under the one Sun,
    with a ruled taper of the 1.5 disc gain and a blob shadow.
    *Recommended:* unlit for slice 1. *Cost to change later:* medium: a
    material, not the geometry.
15. **The mesh: `ProceduralMeshComponent`, 33 x 33 pooled tiles with
    skirts, CDLOD from the ship's origin at split factor 2 with node boxes
    from the parent's built heights and an explicit 2:1 constraint, 2
    build tasks, 4 uploads a frame, 2,500 tiles coarsening the far levels
    first -- gated on the first task of (b) (simulated 860-2,200 tiles, 2-4.5 M
    triangles, 250-400 MB of PMC CPU copies).** *Alternatives:* a custom
    primitive with a static-path scene proxy and no CPU copy (more Unreal
    code, much lighter); `DynamicMeshComponent`; GPU displacement; a
    third-party plugin. *Recommended:* PMC with the gate, the custom
    primitive as the named fallback. *Cost to change later:* medium; the
    pure core survives a change of component.
16. **No geomorphing in slice 1**: LOD splits pop by what the finer bands
    add, estimated at a few 4K pixels, measured by `DeepSpace.Surface.Tile`
    and shown to the developer. *Alternatives:* CDLOD geomorphing now (a
    per-vertex delta to the parent level, eased in the material; more
    material and tile work in slice b). *Recommended:* measure first.
    *Cost to change later:* medium: a material term and a second position
    per vertex.
17. **X sets the vertical lever to HOVER, and so does the fold's all
    stop.** *Alternatives:* X leaves the vertical lever alone; X sets
    HOVER but the fold leaves it (it is zeroed by the arrival anyway).
    *Recommended:* HOVER both. *Cost to change later:* one line.
18. **Space and C shared with jump and crouch**, flying seated and walking
    standing. *Alternatives:* other keys (R/V, PageUp/PageDown).
    *Recommended:* Space and C. *Cost to change later:* a script re-run, but
    it is muscle memory after the first playtest.
19. **Relief: a 1/g ceiling (9 km at 1 g for rock and ice, x0.7 for
    terrestrial), a Beta draw by kind, capped at 10 km by guarantee, from a
    new `"relief"` stream; faces keep their `"sky.surface"` seed.**
    *Alternatives:* flat heights per kind; re-seed faces from the procgen
    `"surface"` hook (every face changes once). *Recommended:* as specified.
    *Cost to change later:* low for the priors; a re-seed later moves every
    place visited.
20. **Terrestrial worlds are all land in slice 1.** *Alternatives:* a sea
    level with a sea-fraction prior and a floor over water. *Recommended:*
    all land now. *Cost to change later:* medium: a datum in `WorldRelief`
    and a second kind of floor.
21. **`ds.Sky.Relief` and `ds.Sky.Craters` are retired as live knobs in
    slice (b)**: amplitude becomes priors data, reloaded with the world.
    *Alternatives:* keep them as orbit-only look knobs (the orbit then
    disagrees with the ground whenever they move); keep them moving the
    ground too (a knob that moves rock under a landed ship). *Recommended:*
    retire. *Cost to change later:* low.

## Open questions

- **Seas on terrestrial worlds**, and the vision's "set down in the ocean
  for water": when, and whether an ocean world gets a surface to set down
  on.
- **Detail finer than 130 m at mid distance.** The ground near the ship has
  it (vertices), the orbit never had it; the middle distance lacks it.
  Detail textures, or per-pixel noise in tile-local coordinates, when the
  playtest asks.
- **The port desk screen** (system-map decision 9) is free if landing wants
  a screen at the helm: a ground map, a slope readout? Nothing in slice 1
  needs it.
- **Terrain shadowing of the deck**, and the ship's shadow on the ground:
  with lighting, or with atmosphere?
- **Effort at a star's floor.** The boosters hold 27-56 g there for
  nothing, silently (decision 5's scope). Should a star's pull be felt at
  all -- a hum, not watts -- or is its silence right?
