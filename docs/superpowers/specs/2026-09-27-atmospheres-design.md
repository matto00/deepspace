# DeepSpace — Atmospheres: Air From Orbit to the Ground

**Date:** 2026-09-27
**Status:** Approved by the developer, 2026-09-27: every item of
*Decisions needing sign-off* as recommended. Not implemented.
**Answers:** the developer's rulings on atmospheres, recorded below
**Follows:** landing (approved 2026-09-27, not implemented; its ruling 7 said
"Atmosphere is its own later sub-project", and its non-goals listed
atmosphere, sky colour, clouds and weather), the sky (implemented; its
`FSkyBody::Rim` is the stub this replaces), flight feel and speed-bands
(implemented; the speeds entry glow is drawn against)
**Depends on (approved, unbuilt):** landing slice (a) -- the
`Shaders/` directory, the `DeepSpaceShaders` module, `M_SkyBody` on a Custom
node, the `Eyes.*` rendered harness and `Tools/eyes.sh` -- before slice 1's
material work; landing slice (b) -- `AWorldGround`, `M_SkyGround`, the 50 km
handover -- before slices 2 and 3; landing slice (c) -- LANDED -- for the
landed half of slice 2's done-when
**Builds on:** ADR 0002 (logic in C++), ADR 0005 (the ship is the origin),
ADR 0006 (one generator, C++; as landing amends it, one `.ush` compiled
twice), ADR 0007 (hierarchical seeds), ADR 0008 (shape the prior)
**Governed by:** `docs/vision.md`: *scale is only felt in contrast*,
*planets are worlds, at Earth's scale*, *approach takes time, and the time
is the content*, *no loading screens*, *most worlds are empty*, ocean,
wilderness and city *legible from orbit*, *the anti-chore principle*;
CLAUDE.md: *Randomness*, *The universe*, *The sky*, *The drive and the
jump*, *The hum and the lamps*

## The developer's rulings, 2026-09-27

These are binding. Everything below is built on them; where a decision goes
past them, it says so and is on the sign-off list.

1. **One spec** covering orbit, the sky from the ground, the 50 km handover
   and entry. **Slice 1 is air seen from orbit**: disc tint and contrast
   loss rising with airmass toward the limb, a limb extending past the
   silhouette, a soft reddened terminator, a forward-scattered crescent and
   a backlit ring. It is built **after landing slice (a) merges**, so
   `M_SkyBody` is rewritten once, through the Custom-node/`.ush` route slice
   (a) introduces. **The pure C++ optics model and its tests may start
   now.** The ground-sky slices come after landing is built.
2. **Sky colour is derived and honest**: the star's blackbody
   (`SkyColour::Blackbody`, the one-blackbody rule) x the composition's
   scattering and absorption x the column. **No palette, no floor.** Most
   skies -- red dwarfs' -- are peach; blue only from about 4,000 K, under
   K, G, F and A stars (as atmosphere plan ruling 1 restates it).
3. **Our own analytic model everywhere**: a pure C++ optics reference
   (transmittance, in-scatter, Chapman airmass) and an unlit Custom HLSL
   term in `M_SkyBody`, later `M_SkyGround` and the dome; small LUTs built
   in C++ at world start, or 8-16 samples. **No `verify_level.py` change.**
   50 km parity held by one law and a rendered parity test like
   `Eyes.WorldReliefParity`.
4. **Procgen: two new draws on new labelled streams**: `SurfacePressureBar`
   (log-normal by kind, bounded by a Jeans-escape guarantee in code) and the
   mean molecular weight / gas mix (categorical: N2/O2, CO2, H2/He). Kinds
   unchanged. Barren and Ice stay airless. The `Seed`/`Stream` known-value
   tests untouched. Priors in the ini with a domain check. A corpus column.
5. **Deck light**: the one Sun, tinted and dimmed by transmittance at the
   local sun elevation. **No sky fill indoors. No SkyLight.**
6. **Exposure stays manual.** Daytime sky radiance is placed on the sky's
   scaled units relative to `ds.Sky.Radiance`, with a checkable target: a
   clear noon under a G star hides all but the brightest stars at galley
   exposure. Windows bright but not clipped; the veil still works; red-dwarf
   skies dimmer.
7. **Entry is look and sound only**: `EntryGlow(rho(alt), v)` drives an
   orange-pink sheath and streaks on `M_SkyGlass`, a rise in the hiss, and a
   warm pulse in the Sun's colour. **Flight unchanged. No damage, no heat
   meter, no timer.**
8. **Haze is capped at nadir by a guarantee in code** (not an ini prior), so
   every airy world's surface stays legible from orbit; haze rises only with
   airmass toward the limb. Thick Venus- and Titan-class worlds are out of
   scope.
9. **No clouds in this sub-project.** Clouds are their own later
   sub-project, with world spin and weather.

**Atmosphere plan ruling 1 (2026-09-27):** keep the physics. In the game's colour space (linear sRGB, D65) a red dwarf's sky is peach, not "pale grey-cyan": the `.StarColour` red-dwarf bounds and ruling 2's words are restated to what the physics gives (saturation about 0.72 at 2,566 K, about 0.96 at 2,000 K; least saturated near 3,500-4,000 K; blue from about 4,000 K). No per-star white balance. A blue sky is still an event.

**Atmosphere plan ruling 2 (2026-09-27):** `MaxNadirTau450` is lowered from 0.5 to 0.32, so `.NadirLegible` holds half the airless contrast at nadir as written.

**Atmosphere plan ruling 3 (2026-09-27):** the spec's named fallback: the shipped law carries four to six spectral bins, not three channels, so it meets the reference within 5% or 1e-3 everywhere, thin airs and grazing paths included. Tasks 4-8 are re-planned first.

**Atmosphere plan ruling 4 (2026-09-27):** "the noon zenith" is straight up from the ground under a sun 45 degrees high (`AtmosphereLaw::NoonSun`), everywhere it is used.

**Atmosphere plan ruling 5 (2026-09-27):** `.HomothetyInvariance` holds the eyes to 1e-12 relative and the law's outputs to 1e-9; not bit for bit.

**Atmosphere plan ruling 6 (2026-09-28):** eight spectral bins, partition {0, 2, 3, 5, 6, 8, 10, 11, 16} (planning note 13), which meets 5% or 1e-3 on the whole grid; no six-bin partition does (the best misses by 4.4x the allowance). This goes past ruling 3's "four to six" by measurement.

**Atmosphere plan ruling 7 (2026-09-28):** as the plan proposes: the law is held at 5% or 1e-3 to the reference with its second scattering made isotropic; the gap from that to the full second order is pinned at 25% or 1e-3; the `Shown` clamp (negative channels to zero on both sides before comparing) is allowed (planning notes 16, 17).

**Ruling 6, the partition corrected by measurement (2026-09-28):** still eight bins, now {0, 3, 5, 6, 8, 9, 10, 11, 16}. The first partition missed `BinsCarryTheSpectrum` at 2,400 K, procgen's coolest star (1.012 of the allowance: CO2 at its ceiling, 88 degrees, green). A search of all 6,435 eight-bin partitions found two that pass at every star, and this one is worst at 0.767 across 2,400-15,000 K. If the law's other agreement tests (single scattering, `LawMatchesReference`) fail with it, the question goes back to the developer.

## Context

### What exists, and what does not

- **A stub.** `FSkyBody::Rim` ("Atmosphere rim; black means none",
  `Sky/SkySystem.h`) is set per kind in `LookOf` (`SkySystem.cpp`):
  Terrestrial and Ocean wear the same Earth blue (0.18, 0.32, 0.70),
  GasGiant a faint brown (0.20, 0.18, 0.14), Barren and Ice black. In
  `M_SkyBody` it is a Fresnel glow, `Rim x Brightness x Fresnel(5) x
  saturate(N.L) x (1 - PointBlend)`, painted only *inside* the silhouette,
  blind to the star's colour and to altitude. A Terrestrial world under a
  2,566 K dwarf wears the same Earth-blue limb as one under a G star.
  `LocalSystemTest.cpp:144` asserts that "an ocean world has a sky at its
  limb".
- **Procgen knows nothing about air.** `FPlanet` has `Kind`, `MassEarth`,
  `RadiusEarth`, `EquilibriumK`, the orbit and `DayHours` (giants only). No
  pressure, no composition. `Kind` is derived, never drawn: GasGiant at 15
  M_E or more; Barren below 0.3 M_E *or* above 320 K; Ice below 180 K;
  otherwise a roll against `OceanFraction` picks Ocean or Terrestrial. So a
  hot Venus is Barren and a Titan is Ice, both airless by definition, and
  every Terrestrial or Ocean world lies between 0.3 and 14.9 M_E and 180 and
  320 K. Surface gravity is implied: g = M^0.44 g_E, 0.59-3.3 g on those
  kinds.
- **The honest weights.** Three suns in four are red dwarfs; home,
  Baemsekai, is a 2,566 K dwarf. Most skies the player will ever stand under
  are red-dwarf skies.
- **Everything outside is unlit**, and each material's emissive is the final
  pixel. Planets do their own Lambert from a per-body `LightDirection`, with
  a 1.5 disc gain, `Brightness` compressed by `ds.Sky.FluxGamma`, and a face
  clamp (0.9) as the half-float guard. The night side is black.
- **Distant bodies are homotheties about the ship** (`SkyProjection`): the
  nearest near side at 50 km, magnified by k = 50 km / altitude; the far
  edge 125,000 km; stacked in depth by `d^2 - R^2` with `StackGap` 1.02;
  the dome at 250,000 km. **Direction, angular size and every ratio of
  lengths survive; distance does not.** Anything that reads scene depth
  above 50 km reads a lie.
- **One Sun.** `AShipSky`'s Movable DirectionalLight: colour the star's
  blackbody, intensity `SunLux x Compress(irradiance) x SunVisibleFraction`
  (the analytic eclipse), `bAtmosphereSunLight = false` with the comment
  "No atmosphere to scatter it". It lights only the ship.
  `verify_level.py` fails the level on any other DirectionalLight, and on
  any SkyLight, SkyAtmosphere, VolumetricCloud or ExponentialHeightFog.
- **Exposure is fixed** (`ds.Sky.ExposureMode 1`, `ds.Sky.Exposure` 0.7
  EV100, still an unread estimate), and `M_SkyGlass`'s veil, `VeilColour x
  Veil x InteriorLight`, is tuned to sit at a flux-4 star at the galley EV
  with the lights fed: about 12% of the starfield shows, all of it with the
  lights off.
- **Landing, on paper.** Slice (a) moves `M_SkyBody` onto one Custom node
  that `#include`s `Shaders/Private/WorldRelief.ush`, a file in a scalar,
  no-swizzle subset that is also valid C++ and is compiled into
  `./build.sh`; it adds the `Eyes.*` rendered tests (outside `./test.sh`,
  with a real GPU). Slice (b) draws the body with `AWorldGround` below 50
  km over a solid world, where k = 1 and the proxy *is* the true sphere,
  shades it with `M_SkyGround` under `M_SkyBody`'s own law, and holds the
  two equal at 50 km with `Eyes.WorldReliefParity`. Oceans and giants keep
  their floor sphere: 10.2 km over an Earth-size ocean, about 112 km over a
  Jupiter.

### What the player will be able to see, and how big it is

For Earth-like air (scale height H about 7.5-8.5 km), the air that matters
lies below about 10 H, 85 km. At the opening shot (an Earth at 40,000 km,
18 degrees across) that shell seen edge-on subtends about 0.05 degrees, 2-3
4K pixels. At the in-system arrival standoff (the world 2 degrees across)
the limb is a tenth of a pixel. **From arrival distance, air shows as
colour on the disc, a softer terminator, and a bright crescent or ring when
the star is behind the world** -- not as a visible shell. The shell becomes
a band only from a few thousand kilometres, and the sky as a sky only
inside it.

The descent is where air is felt most. Landing's anti-chore table counts
150-200 s across the 50 km to drive-floor morph band and 75-125 s from the
floor to the ground. For Earth-like air, 85 km to the ground is the whole
passage from black to day sky: the stratopause at 50 km, the floor in the
lower stratosphere. **The approach is the content, and air is what makes
it change.**

### A few Unreal terms this spec uses

- **Unlit material, emissive.** A material whose output colour is the
  pixel, with no engine lighting. Everything outside the ship is one.
- **Custom node.** A material node whose body is HLSL (shader) text; with
  landing's `DeepSpaceShaders` module it can `#include` a project file.
- **Blend modes.** *Opaque* writes the pixel; *additive* adds to what is
  behind (it can brighten, never dim); *translucent* mixes with one grey
  opacity (it cannot dim red and blue by different amounts).
- **Material Parameter Collection (MPC).** A global bag of parameters every
  material can read and C++ writes once a frame; `MPC_Sky` already carries
  `InteriorLight` and `Veil`.
- **Transient texture.** A `UTexture2D` made at runtime from C++ data,
  never saved as an asset.
- **Half-float scene colour.** The renderer keeps the picture in 16-bit
  floats: values above about 65,000 overflow. The sky's guards (the face
  clamp, the 8x star ceiling) exist for that.

## Goals

- Every world with air looks like it has air, from the arrival standoff to
  the ground, and **what it looks like is a consequence of its star, its
  gas, its pressure and its gravity**, never of a table.
- One optics law, compiled once into C++ and the shaders, held to a
  brute-force spectral reference by pure tests, so the orbit, the sky from
  inside, the ground's haze and the deck's sun agree by construction, and
  the 50 km handover shows no step.
- A red-dwarf majority of pale, dim skies, against which a blue G-star sky
  is rare enough to be an event: scale felt in contrast, applied to colour.
- A fast arrival that burns, a slow one that does not, and nothing the
  player has to watch.

## Non-goals

- Clouds, weather, world spin, day and night by rotation (ruling 9).
- Thick worlds: Venus and Titan classes, featureless discs, sunless
  surfaces (ruling 8). The derived kinds do not change.
- Any engine atmosphere component: SkyAtmosphere, VolumetricCloud,
  ExponentialHeightFog, SkyLight (ruling 3, ruling 5).
- Sky light inside the ship beyond the one Sun (ruling 5).
- Drag, heating damage, a heat gauge, an entry corridor, any change to
  flight (ruling 7).
- Airglow, aurorae, night-side city light through air, rainbows, glories.
- Greenhouse warming: surface temperature stays `EquilibriumK`
  (*Deliberate fakes*).
- Moons (procgen makes none), rings.

## Decisions

### 1. One optics law: a shared `Atmosphere.ush` compiled twice, held to a brute-force reference

Two levels, each pure and headless-testable.

**The reference**, `Source/DeepSpace/Atmosphere/AtmosphereReference.{h,cpp}`:
double precision, spectral at 16 wavelengths from 400 to 700 nm, a fine ray
march (hundreds of steps), exact optical depth to the sun by numerical
integration at every sample, and second-order scattering by brute force.
It is slow and never runs in a frame. It is what "right" means, and it is
the only place physics constants live on the sky's side.

**The shipped law**, `Shaders/Private/Atmosphere.ush`: the approximation
that draws, written once in the same subset as landing's `WorldRelief.ush`
-- valid HLSL and valid C++ at once: no swizzles, no vector or matrix
types, no `mul()`, no implicit vector arithmetic, no `out` parameters;
every vector spelled a component at a time, every multi-valued result a
plain struct -- with `AT_`-prefixed shims matching landing's `WR_` ones
(`AT_floor`, `AT_saturate`, `AT_min`, `AT_max`, `AT_sqrt`, `AT_exp`,
`AT_log`, `AT_cos`, `AT_sin`; the last four have no `WR_` equivalent yet,
so a shared shim header would carry them too). `AT_REAL` is `float` in HLSL; in C++ the file is **compiled
twice**, as `WorldRelief.ush` is: once with `AT_REAL` `double`
(`AtmosphereF64`) and once with `float` (`AtmosphereF32`, the GPU's
mirror), both in `Source/DeepSpace/Atmosphere/Atmosphere.cpp`, so a change
that breaks C++ fails `./build.sh` and a float-only failure fails a pure
test. Its entry points:

```cpp
// All lengths in radii of the body whose air it is (R = 1), all directions
// unit, in whichever axes the caller uses consistently. Vectors are passed
// a component at a time; results are structs, never out-parameters.
struct AT_Rgb     { AT_REAL R; AT_REAL G; AT_REAL B; };
struct AT_Scatter { AT_REAL R; AT_REAL G; AT_REAL B;      // in-scatter
                    AT_REAL TR; AT_REAL TG; AT_REAL TB; }; // transmittance along the view
AT_Rgb     AT_Transmittance(AT_Air A, AT_REAL FX, AT_REAL FY, AT_REAL FZ,
                            AT_REAL DX, AT_REAL DY, AT_REAL DZ, AT_REAL Length);
AT_Scatter AT_InScatter    (AT_Air A, AT_REAL EX, AT_REAL EY, AT_REAL EZ,
                            AT_REAL DX, AT_REAL DY, AT_REAL DZ, AT_REAL Length,
                            AT_REAL SX, AT_REAL SY, AT_REAL SZ);
AT_Rgb     AT_SunThrough   (AT_Air A, AT_REAL PX, AT_REAL PY, AT_REAL PZ,
                            AT_REAL SX, AT_REAL SY, AT_REAL SZ);  // transmittance to the star
AT_REAL    AT_LogChapman   (AT_REAL X, AT_REAL CosZenith);       // ln(airmass), asymptotic form
AT_REAL    AT_SheathProfile(AT_REAL U, AT_REAL V, AT_REAL Facing, AT_REAL Glow); // decision 13
```

`AT_Air` is a plain struct of scalars: per spectral bin, the gas's and the
aerosol's scattering and extinction, colourless, and the star's light in
the bin as `FoldR`, `FoldG` and `FoldB`; then the shape, `GasH`,
`AerosolH`, `AerosolG` and `Top` -- 60 scalars, fifteen float4 (amended
by atmosphere plan rulings 3 and 6). The multiple-scattering table (below)
is read through a hook the file itself defines once per platform, under
`#if defined(AT_CPP)` and its `#else`, together with `AT_TABLE_PARAM`,
`AT_TABLE_ARG`, `AT_PRECISE` and `AT_SPLITTER`: C++ defines only `AT_CPP`
and `AT_REAL` before including it, and a Custom node defines nothing and
must not supply its own. The hook is
`AT_MultiScatter(AT_Air A, AT_REAL Altitude01, AT_REAL CosSunZenith
AT_TABLE_PARAM)`, returning an `AT_Bins`, a value per bin: in HLSL four
`Load`s of each half of the texture the Custom node is handed, blended in
float, in C++ a bilinear read of `FAtmosphere`'s own table. The table
travels as the trailing macro argument `AT_TABLE_PARAM` (`AT_TABLE_ARG` at
a call), which every law function that reaches the hook takes, because a
Custom node's texture is a parameter of the function it generates, not a
global (atmosphere optics plan, planning note 1). The entry points'
arguments are otherwise as above, and they still return linear sRGB,
folded from the bins at the end.

**Float-safe by construction.** Two failures are predictable in `float`
and invisible in `double`, and the law is written against both:

- **The Chapman function below the horizon** has a factor `exp(X(1 - sin
  z))` with `X = R / H`, about 850 for Earth air; in float it overflows
  (argument above 88.7) about 26 degrees past the terminator, and one inf
  pixel is spread across the screen by TSR and bloom. So the law works in
  the log domain: `AT_LogChapman` returns ln(airmass), and every
  transmittance is `exp` of one combined exponent (`-(tau_vertical x
  exp(LogChapman))` is formed as `-exp(ln tau_vertical + LogChapman)`), so
  no intermediate exceeds the final result.
- **Limb rays.** A ray's closest approach to the centre (its impact
  parameter) is taken from `|E x D|`, never from `sqrt(|E|^2 - (E.D)^2)`,
  which loses the tangent height to cancellation in float at a distant eye
  (several per cent of transmittance at the limb from 7.8 R).

- **Density** is exponential in height for each of two constituents, the
  gas (Rayleigh) and the aerosol (Mie), each with its own scale height, and
  an ozone-like absorber for the N2/O2 mix (decision 5).
- **Optical depth to the sun** at every sample is analytic: the vertical
  column times the **Chapman function**, the exact airmass of a curved
  exponential atmosphere, in the asymptotic form sqrt(pi X / 2)
  erfcx(sqrt(X / 2) cos z) with its first correction in 1 / X (Schueler's
  (2012) closed form missed it by 2.5% at 60 degrees: atmosphere optics
  plan, planning note 5), which also handles a sun below the horizon by the planet's occultation.
  That is what makes the terminator soft and the Earth's shadow rise in the
  sky without an inner loop.
- **Along the view**, **12 nodes**, spaced by the density's own
  distribution (dense where the air is); between two, each constituent's
  exact Chapman column times the logarithmic mean of what it gathers per
  unit column -- exact for the density and the view's transmittance -- and
  one node more where single scattering climbs past e^4 (atmosphere plan,
  planning note 14); single scattering with the Rayleigh phase and a
  Henyey-Greenstein phase for the aerosol.
- **Multiple scattering** is Hillaire's (2020) isotropic approximation, read
  from a **32 x 32 table per airy world** (sun zenith by altitude) that
  C++ builds from the same `.ush` when the system loads and uploads as a
  transient texture (decision 11).
- **Eight spectral bins**, not three colour channels (atmosphere plan
  rulings 3 and 6): each bin a run of the reference's wavelengths, narrow
  in the blue, its coefficients the star-weighted mean of its wavelengths'
  and themselves colourless, folded into linear sRGB only at the end by the
  star's own light in each bin -- so the star enters through the bins'
  weights and the folds, which C++ computes, and the shader still never
  sees a temperature (decision 3). Three channels, and any four to six
  bins, missed the reference on long paths by up to several times the
  tolerance.

**How they are held equal.** The `.ush` runs in C++ exactly as it runs on
the GPU, so its *approximations* are testable headlessly:
`DeepSpace.Atmosphere.LawMatchesReference` compares the shipped law against
the reference over a grid of eye heights (inside and outside the air), view
directions (nadir, limb, horizon, zenith) and sun angles (noon, terminator,
backlit), for each mix and pressure extreme, and requires each channel
within 5% relative or 1e-3 absolute of the reference with its second
scattering sent every way alike, as the law's multiple scattering is --
**for both the F64 and the F32 builds** -- and within 25% or 1e-3 of the
reference's full second order, the approximation's own cost (atmosphere
plan ruling 7). `DeepSpace.Atmosphere.FloatMatchesDouble` holds F32 to
F64 over the same grid plus the extremes (a sun 30 degrees below the
horizon, the limb from 7.8 R and from 1e3 R) at 1e-3 relative or 1e-5
absolute, and requires every F32 output finite. What cannot be tested
headlessly -- that the GPU computes what the C++ F32 mirror computes -- is
`Eyes.AtmosphereProbe` (decision 12).

**Rejected: Unreal's SkyAtmosphere.** One per world (the scene holds a
single pointer), so every other airy world needs a material model anyway.
It assumes true scale: its constants are absolute kilometres (a 96 km
aerial-perspective volume, a 0.1 km minimum height that fails beyond about
50,000 km over an Earth under the homothety). Our dome and bodies are
opaque and write depth, so its sky path never fires inside the air. Its
hard planet shadow double-counts `SunVisibleFraction`. And it needs
`verify_level.py`'s ban lifted and the Sun flagged as the atmosphere sun,
which tints the deck by the engine's rules instead of ours. **Rejected: a
hybrid, the engine below 50 km.** Two models to hold equal at exactly the
seam that must not show, and the same ban lifted. **Rejected: a
per-channel analytic formula with no march** (O'Neil-style fits). Cheap,
but it cannot carry the backlit ring or a sun below the horizon, and it
cannot be held to a reference except by fitting it.

**Cost to change:** high once three materials and the deck read it. The
reference survives any change of shipped law.

### 2. The air is a fact of the world: pressure and mix are drawn, everything else is derived

`FPlanet` gains two drawn facts, each on its own labelled stream from the
planet's seed, so drawing them moves no other number of any world (as
`DayHours` and landing's `ReliefKm` do):

- `AirMix` (`EAirMix {None, NitrogenOxygen, CarbonDioxide, HydrogenHelium}`),
  from `Derive(PlanetSeed, Label("air.mix"))`;
- `SurfacePressureBar`, from `Derive(PlanetSeed, Label("air.pressure"))`.

**Which worlds have air** follows the kinds, unchanged:

| Kind | Mix | Pressure |
|---|---|---|
| Barren, Ice | `None` | 0 (airless, as ruled) |
| Terrestrial, Ocean | drawn: categorical, shaped by retention (below) | drawn: log-normal, bounded (below and decision 4) |
| GasGiant | `HydrogenHelium`, not drawn | derived, not drawn: the drawn disc is the level where the air above it reaches `MaxNadirTau450` (decision 4), `P = MaxNadirTau450 / tau450_per_bar(g)` |

**A giant's "surface" is where its air stops being legible**, not 1 bar.
Giants are fixed at 11 R_E from 15 to 3,000 M_E, so their gravity runs from
0.12 g to 25 g, and H2/He carries about 0.56 of tau450 per bar at 1 g: a
1-bar datum would put a 15 M_E giant under tau450 4.5 at nadir, fourteen times
the guarantee, a featureless haze from orbit. Defining the disc at the
guarantee's own depth keeps every airy world, giants included, under one
rule and one test: a 0.12 g giant's disc is its 0.07 bar level, a Jupiter's
(318 M_E, 2.6 g) about 1.5 bar, a 3,000 M_E giant's about 14 bar (at
atmosphere plan ruling 2's 0.32; 0.11, 2.4 and 22 bar at the first 0.5). The
radius stays 11 R_E: the level differs from 1 bar by at most a few scale
heights, about 1% of the radius on the lightest giants (H near 330 km at
0.12 g) and far less on the rest, which the drawing cannot show. Belts are then drawn under half an optical depth
of air on every giant, as the rocky surfaces are.

**The mix is categorical, and the prior is shaped, not rejected** (ADR
0008). Each mix has an ini weight. Before drawing, each weight is multiplied
by the world's **retention** of that mix (below), so a world that cannot
hold hydrogen never draws it, and no draw is thrown away.

**Retention is the Jeans-escape guarantee, in code** (`GenGuarantees`,
never the ini). A gas is held over the age of a system when its molecules'
root-mean-square speed at the top of the air is a small fraction of the
escape velocity. With `v_esc = sqrt(2 g R)` and `v_rms = sqrt(3 k T_exo /
(mu m_u))`, the ratio `lambda = v_esc / v_rms`:

- `RetainedAbove = 6`: fully held;
- `LostBelow = 4`: gone;
- between them, retention is a smoothstep;
- `T_exo = ExobaseFactor x EquilibriumK`, `ExobaseFactor = 4`: the top of
  an air is far hotter than its surface (Earth's thermosphere runs near
  1,000 K over a 255 K equilibrium), and it is the top that loses gas.

What that gives, measured by the pure test: Earth holds N2/O2 (lambda
about 12) and loses H2/He (about 3.4); the smallest, hottest Terrestrial
world (0.3 M_E, 320 K) still holds N2/O2 (about 6.9) and CO2; a cold 10
M_E super-Earth at 200 K holds H2/He (about 8.7), as sub-Neptunes do. So on
the kinds that have air, **N2/O2 and CO2 are always available and the
guarantee decides hydrogen.**

**Pressure is log-normal by kind**: pressure is a product of many factors
(outgassing, loss, sequestration), which is what a log-normal describes.
Its tail is bounded twice, both in code: by retention (a world that
barely holds its gas holds less of it: the ceiling is multiplied by the
drawn mix's retention) and by the nadir-haze guarantee (decision 4). The
bound is a **smooth ceiling**, `P = 1 / (P_draw^-4 + P_max^-4)^(1/4)`, not
a clamp, so the corpus shows no spike of worlds piled at the cap: the
distribution bends under the ceiling rather than stacking against it.

**Derived, never drawn** (pure, `Universe/AirFacts.{h,cpp}`, beside the
generator; physical facts belong to procgen, looks to the sky):

- mean molecular weight by mix: 28.97 (N2/O2), 44.0 (CO2), 2.3 (H2/He);
- `ScaleHeightKm = k T / (mu m_u g)` (in km; the sky converts to cm), with `T = EquilibriumK` (no
  greenhouse; *Deliberate fakes*): about 7.5 km for Earth air on a 1 g, 255
  K world; about 15 km for H2/He on a 2.6 g, 110 K giant;
- the column relative to Earth's, `(P / 1 bar) x (g_E / g) x (28.97 / mu)`
  molecules;
- the Rayleigh optical depth at a reference wavelength, the column times
  the mix's cross-section relative to air (N2/O2 1.0, CO2 2.4, H2/He 0.2);
- the aerosol and absorber of the mix (decision 5).

**The priors, in `[/Script/DeepSpace.ProcGenPriorsConfig]`**, in the
`DS_GEN_PRIORS` list with domain checks in `GenPriorDomain::Refusals`:

| Prior | Default | Domain |
|---|---|---|
| `AirPressureMedianTerrestrialBar` | 0.8 | > 0 |
| `AirPressureMedianOceanBar` | 1.0 | > 0 |
| `AirPressureSigma` | 0.9 (natural-log) | (0, 2.5] |
| `AirMixWeightNitrogenOxygen` | 0.55 | >= 0 |
| `AirMixWeightCarbonDioxide` | 0.40 | >= 0 |
| `AirMixWeightHydrogenHelium` | 0.05 | >= 0; the three sum > 0 |

**The mix weights set the colour of much of the universe** and are on the
sign-off list (item 19). At 0.55 / 0.40 / 0.05, roughly two airy worlds in
five draw CO2, a pale, dusty, Mars-like sky that eats blue; N2/O2 is a bare
majority. The corpus's `sky_zenith_rgb` spread is what the weights are
judged by.

**The corpus** gains `air_mix`, `surface_pressure_bar`, `scale_height_km`
and `nadir_tau_450` (procgen's facts) in `Tools/procgen_corpus_contract.json`,
`procgen_corpus.py` and the corpus test together. `sky_zenith_rgb` and
`sky_zenith_saturation`, the noon zenith from the ground computed through the
sky's side (decision 3), are **a file of their own**:
`Atmosphere.Full.CorpusSkies` (`CorpusSkiesTest.cpp`), run by name outside
the default suite, writes `Saved/procgen_corpus_skies.tsv`, a row per
temperate world of the 10,000 systems, under the contract's separate
`sky_columns`. A noon zenith costs about 50 ms and there are about 7,800
temperate worlds, six or seven minutes that cannot go in
`DeepSpace.Universe.Corpus`, which every worktree's suite runs behind the one
lock (the atmosphere plan's Task 13). `procgen_corpus.py` reads the skies
beside the corpus when the file exists and shows their spread -- the zenith
colour by mix, its hue and its saturation -- across the 10,000 nearest
systems, to answer the question this project asks of everything generated:
places, or rolls.

**Rejected: derive from kind alone, no draws.** Every habitable-kind world
would have the same column; variety only from star and gravity. **Rejected:
new air classes** (moving the 320 K and 180 K thresholds). It changes the
derived kind of existing worlds, and ruling 8 puts thick worlds out of
scope. **Rejected: draw and re-draw until retained.** Rejection sampling
is what ADR 0008 forbids.

**Cost to change:** low for the priors (`ds.Universe.ReloadPriors`); medium
for the law (tests and the corpus); after a playtest has visited airy
worlds, a changed law changes skies the developer remembers.

### 3. Colour: the star's own spectrum through the air, integrated once per system

**The one blackbody stays in `SkyColour`** (plan conflict 6). It gains the
integral it has so far approximated:

```cpp
namespace SkyColour
{
    // Linear sRGB (Rec. 709 primaries, D65 white, the convention Blackbody
    // already uses) of a TemperatureK blackbody seen through Filter(lambda),
    // integrated against the CIE 1931 colour-matching functions at 16
    // wavelengths from 400 to 700 nm. Not normalised: a brightness.
    DEEPSPACE_API FLinearColor ThroughFilter(double TemperatureK,
                                             TFunctionRef<double(double Nm)> Filter);
}
```

`DeepSpace.Sky.OneBlackbody` holds `Blackbody(T)` to the normalised
`ThroughFilter(T, 1)` within 2% per channel from 2,000 to 15,000 K. If
they disagree by more, **`Blackbody` is re-based onto the integral** --
every star's colour moves slightly, once -- rather than keeping two
blackbodies (sign-off item 3).

**Per-world effective coefficients.** Rendering carries eight spectral bins
(atmosphere plan ruling 6), because scattering is spectral, and a naive three-wavelength model goes wrong at
the extremes: a 2,500 K star has almost nothing at 440 nm to scatter.
So when a system loads, for each airy world, `FAtmosphere::Build`
integrates the star's spectrum through the world's own Rayleigh law
(lambda^-4 x the column), aerosol law (lambda^-alpha, alpha by mix) and
absorber band, and averages **per-bin scattering and extinction** by a
scalar weight per wavelength, then folds each bin into colour by the
star's light in it. The average is not exact per channel even in the
optically thin limit, since the fold turns with the wavelength inside the
wider bins, so the thin airs stay in the agreement grid. `LawMatchesReference` (decision 1) is what proves the bins
good enough across the angles that matter; the star enters through the
bins' weights and folds, which C++ computes, so the shader never sees a
temperature.

**No palette, no floor** (ruling 2). What that produces, estimated from the
integral (the table illustrates; `.StarColour` pins the bounds stated in
*Tests*, in linear sRGB with HSV saturation `S = 1 - min/max`):

| Star | Sky at noon under Earth air (straight up, the sun 45 degrees high) |
|---|---|
| 2,566 K (home) | peach: saturation about 0.72, hue about 26 degrees; a sun oranger still (atmosphere plan ruling 1) |
| 3,000 K | a paler peach, saturation about 0.49; the greyest sky is near 3,500 K, and blue begins near 4,000 K |
| 5,772 K (Sun) | Earth's blue |
| 7,000 K+ (F, A) | deeper, towards violet-blue |

A CO2 sky is paler and brighter per bar, an H2/He sky thinner. The N2/O2
mix's ozone absorbs orange, which keeps the zenith coloured at twilight, as
on Earth. **The vision gains this as art direction** (*Documentation*):
blue is what a G star gives, and most of the universe is not lit by one.

**Rejected: a saturation floor** so red-dwarf skies still read "as a sky".
Less honest, and a tunable with no right answer. **Rejected: a palette by
kind and star class.** It is a second, hand-made star colour beside
`SkyColour`'s, against plan conflict 6, and it reads as rolled.

**Cost to change:** low in code; it sets the colour of most of the
universe.

### 4. Haze is capped at nadir by bounding the fact, in code

Ruling 8 says every airy world's surface stays legible from orbit. **The
cap bounds what a world may have, not what the renderer shows.** A
guarantee, `GenGuarantees::MaxNadirTau450 = 0.32`: the total extinction
optical depth (Rayleigh plus aerosol, not absorption) straight down at 450
nm, the bluest channel's centre and the one that hazes first, is at most
0.32. (Amended by atmosphere plan ruling 2: at 0.5 every mix at its ceiling
kept 0.34 of its contrast, not the half this decision requires.) From the mix, gravity and derivations of decision 2 that gives a
per-world pressure ceiling, `P_max = MaxNadirTau450 / tau450_per_bar`,
which the smooth ceiling of decision 2 applies. At 1 g, from Earth air's
Rayleigh 0.216 per bar at 450 nm (0.097 x (550/450)^4) and decision 5's
aerosols: N2/O2 0.216 + 0.061 = 0.277 per bar, so about 1.15 bar; CO2
0.216 x 2.4 x (28.97/44) + 0.08 x (550/450)^0.3 = 0.341 + 0.085 = 0.426
per bar, so about 0.75 bar; H2/He 0.216 x 0.2 x (28.97/2.3) + 0.012 =
0.556 per bar, so about 0.58 bar. The pure test computes these from
`AirFacts`, never from this prose. A heavier world holds more, since the
same pressure is less column under stronger gravity. Giants have no
ceiling to bend under: their disc is *defined* at this depth (decision 2).

What it guarantees is stated as a legibility number, and tested:
`DeepSpace.Atmosphere.NadirLegible` renders (in C++, through the shipped
law) a surface of albedo 0.1 beside one of 0.3 under an overhead G star,
seen from 400 km at nadir, for every mix at its ceiling, and requires at
least half the airless contrast in every channel. Toward the limb the
airmass rises and the surface fades into air, as it should (ruling 8).

The guarantee is **not an ini line**: no prior can put a world under a
haze that hides its surface, as no prior can raise a peak above 10 km.

**Rejected: an honest column with the cap in the renderer.** The world
would have 3 bar and draw 1.5 from orbit; from the ground, the sky would
disagree with the orbit. **Rejected: no cap.** A high-pressure roll could
arrive grey and flat, and if that world is the populated one, the vision's
legibility clause breaks.

**Cost to change:** medium: the number is one constant, but it bounds
every world's air, so moving it changes pressures across the corpus.

### 5. Aerosol and absorber come with the mix; they are not drawn

Ruling 4 allows two draws. Everything else about an air is **derived from
its mix and pressure**, constants in `AirFacts`:

| Mix | Aerosol (optical depth at 550 nm per bar, scale height, Angstrom alpha, asymmetry g, single-scatter albedo) | Absorber |
|---|---|---|
| N2/O2 | 0.05, 1.2 km, 1.0, 0.76, 0.95 | ozone-like: a Chappuis band centred near 600 nm, a column fixed per bar |
| CO2 | 0.08, 2 km, 0.3, 0.7, 0.85 blue / 0.95 red (fine iron-oxide dust: it eats blue, as on Mars) | none |
| H2/He | 0.01, 1 km, 1.0, 0.7, 0.99 | none |

The mix names a whole air, and the look follows. It is a deliberate
simplification: two N2/O2 worlds at equal pressure and gravity under the
same star differ only in scale height. **Every aerosol number above is a
first estimate** that the reference, the corpus and the playtest will
move; the table is the place they are moved.

**Rejected: a third draw for aerosol.** Out of ruling 4, and the first
variety worth drawing is not yet known; the corpus will say.

**Cost to change:** low: a table.

### 6. The orbit: the disc term in `M_SkyBody`, and a shell for the limb

Air seen from outside is two drawings, each a Custom-node call into
`Atmosphere.ush`, in body radii.

**On the disc**, `M_SkyBody` (landing (a)'s Custom node gains a second
call) computes, for each pixel of an airy body:

`pixel = surface x AT_SunThrough(point) x T_view + AT_InScatter(eye to point)`

- `surface` is today's lit face (its Lambert, its 1.5 disc gain,
  `Brightness`, the clamp), now multiplied by the **transmittance to the
  star at that point**: near the terminator the sunlight reaching the
  ground has crossed a long path, so the ground there is lit reddened, and
  the terminator softens and warms.
- `T_view` dims the surface along the path to the eye, and the in-scatter
  adds the air's own light. At nadir both are small (decision 4); toward
  the limb the airmass rises, and the surface's contrast falls into the
  air's colour.
- The **night side** is no longer entirely black: sunlight scattered over
  the terminator (twilight) reaches a band past it, derived, as wide as the
  air is deep.

**The eye in body radii needs no parameter.** The homothety preserves
every ratio of lengths, so `(CameraPosition - ProxyCentre) / ProxyRadius`
in the material is the true eye position over the true radius, at any k.
`RenderableScale`'s rounding does not disturb it: `SkyProjection` moves the
centre out to match the rounded scale, so the drawn proxy is an exact
homothety and the ratio is exact in double. The error that is real is the
GPU's: the material forms `CameraPosition - ObjectPosition` in float from
large-world coordinates. `HomothetyInvariance` holds the pure half (bit for
bit at k = 1 and 1e-3); `Eyes.AtmosphereProbe`'s proxy case (decision 12)
holds the GPU half through the shipping material's own world-position
path.

**Past the silhouette**, a second proxy: the **air shell**, `SM_SkyBody`
scaled to the body's radius x (1 + `AirTop`), with `AirTop` 10 scale
heights (where density is e^-10 of the surface's), drawn with a new
**additive** unlit material, `M_SkyAir`. For each pixel it calls
`AT_InScatter` along the ray through the shell, **and outputs zero where the
ray meets the solid body**, which the disc term already drew. What it draws
is the limb band, the **crescent** at high phase (forward scattering, the
aerosol's g), and, when the star is behind the world, the **full ring** --
the backlit world, the moment the eclipse already makes (`SunVisibleFraction`
at 0, the deck dark, the world ringed in reddened light).

The shell is drawn **with its body's own magnification k and centre** --
the same homothety, the same `RenderableScale` rounding -- with the body's
isolation flags (no shadow, no Lumen, no captures, no ray tracing).

**Stacking.** Stacking on the outer radius everywhere would break the
homothety near the shell: `SkyProjection` puts the nearest stacked near
side at `NearProxy` (50 km), so k = 50 km / (d - R_stacked), which goes to
infinity as the eye nears the shell's top (1 m above an Earth's gives k =
50,000 and a proxy far past `UE_LARGE_HALF_WORLD_MAX`) and is negative
inside it -- and every airy world's 50 km handover is inside its own
shell, where landing needs k = 50 km / altitude = 1. So:

- **The first body in depth order stacks on its solid radius**, exactly as
  today, so k = 50 km / altitude is unchanged and landing's k = 1 at 50 km
  survives. Its shell, drawn at the same k, has its near side at 50 km x
  (a - a_top) / a (a the altitude, a_top = `AirTop` x R), nearer than the
  body's own.
- **An airy body later in the order stacks on its outer radius**, R(1 +
  `AirTop`), so its shell stays behind the far side of the body before it.
  The eye is then far from it (a body whose shell holds the eye, or nearly
  does, has the smallest power and is first), so d - R_outer is large and
  k finite. The scale is invisible and reassigned every frame (sky decision
  3), so a change of order moves nothing on screen.
- **The shell is drawn only while its near side is at least
  `ShellNearMin` (5 km) from the eye**, which for the first body is
  altitude a >= a_top x 10/9: about 83 km over Earth air, 8 km above the
  shell's top. Below that the air belongs to the eye (decision 7), which
  takes over the ray the shell drew; at the crossing both evaluate the same
  ray from the same eye, so the change is continuous by construction. No
  shell is ever drawn from inside, nor within metres of the camera.

`FSkyBody` gains the outer radius; `SkyProjectionTest` gains cases for an
eye far above the shell, just above `ShellNearMin`'s altitude, exactly at
the shell's top, and inside it (k finite and continuous; body k = 1 at 50
km; the shell hidden at and below the handover altitude), and for an airy
body second in order (its shell behind the first body's far side).

**The shell needs an asset slot.** C++ never loads a material itself (ADR
0002): `AShipSky` gains an `AirMaterial` `UPROPERTY` (M_SkyAir), assigned by
`build_hauler.py`'s `place_sky` as the other sky materials are. It is a
header and reflection change, so `./rebuild.sh --force` and a level rebuild
(CLAUDE.md: after any change to a generated actor's components, rebuild the
level). The shell components themselves are runtime `NewObject`s, as the
counter-frame's course marker is, so they add nothing to the placed actor's
saved hierarchy.

**Additive cannot dim.** A star behind the limb band is not dimmed by the
limb (*Deliberate fakes*): the band is a few pixels, and the in-scatter over
it already overwhelms a faint star.

**Airless worlds pay nothing.** A body with `AirMix None` has a zero air
term (`Eyes.AtmosphereProbe` asserts the disc of an airless world is
unchanged) and no shell proxy.

**Giants** get an air too: H2/He Rayleigh over the disc (decision 2), a limb haze
and a crescent derived like any other, replacing the fixed brown rim. A
giant round a red dwarf will look unlike Jupiter, for free.

**`FSkyBody::Rim` is deleted**, with `LookOf`'s rim colours, its contract
entries and the Fresnel graph. `LocalSystemTest.cpp:144` becomes "an airy
world has air, and an airless one none", against `AirMix`.

**Rejected: grow the body proxy to the shell radius and trace the solid
inside it.** One mesh, but the body is opaque and the annulus must add over
whatever is behind it. **Rejected: a screen-space ring.** It would have to
reinvent the ray through the shell and the depth ordering the proxy
already has. **Rejected: keep the Fresnel rim and tint it by the star.**
It is still painted inside the silhouette, still a glow, and it cannot make
a ring.

**Cost to change:** medium: a material, a proxy per airy world, and the
stacking rule.

### 7. Inside the air: one air at a time, read by every sky material

When the ship is inside an airy world's shell -- or within the band above
it where the shell is no longer drawn (decision 6: altitude under a_top x
10/9) -- **that air is between the eye and everything**. Exactly one air
can hold the eye (shells of different bodies never overlap: they are
stacked apart by at least the Hill spacing).
`Atmosphere::AirHere(LocalSystem, ShipPosition)` answers which, asked every
frame, stored nowhere. From just above the shell the same law is right: a
ray from an eye outside the air enters it, and `AT_InScatter` handles
both.

`AShipSky` writes that air to `MPC_Sky` each frame -- the per-channel
coefficients, the shape, the eye in that body's radii, the star's direction
and the body's centre, all in **world** axes (the counter-frame's rotation
applied in C++, since the universe turns around the ship) -- and writes
zeros when the eye is in no air.

**There is no sky surface today, so one is added: the air backdrop.** The
sky draws nothing between the stars: `M_SkyStarfield` is a per-instance
material on 3,000 two-pixel instances (the counter-frame's `DistantStars`
ISM, and `AShipSky`'s `NeighbourStars`), and what lies between them is the
cleared background. A day sky needs a surface. `AShipSky` gains
**`AirDome`**, a runtime `UStaticMeshComponent` (a `NewObject`, like the
counter-frame's course marker): `SM_SkyBody` at 1.02 x `DomeRadius`,
beyond every body and behind every star, with a new opaque, unlit,
two-sided material **`M_SkyAirDome`** that draws `AT_InScatter` out of the
air along each pixel's direction. It is visible only while `AirHere`
answers; outside air it is hidden and costs nothing. Its material reaches
C++ through a new slot, `AirDomeMaterial`, assigned by `build_hauler.py`
(ADR 0002), as `AirMaterial` is (decision 6).

The sky's materials are opaque, so a star covers the backdrop's pixel
rather than adding to it. So every sky material applies the same call
itself, and each pixel carries its own in-scatter:

- **`M_SkyAirDome`**: `AT_InScatter` to infinity, the day sky.
- **`M_SkyStarfield`** (the distant stars and the neighbours): `star x
  T_view + AT_InScatter` along the star's own direction, so a star is its
  surroundings plus its own dimmed light, and is outshone by day with no
  dark speck where it stands. It reads `MPC_Sky` and the shared table
  (decision 11) with no instance parameters: 3,000 stars stay one draw call.
- **`M_SkyStar`**: the local star's disc through the air, reddened and
  dimmed at a low sun, clamped by the existing half-float ceiling -- **only
  where the new scalar `AirApplies` is 1**. Its default is 0, and only
  `AShipSky` sets it, on the local star's instance. `M_SkyStar` also draws
  the counter-frame's **dust motes** and navigation's **teal course
  marker** (`SkyMaterialContract.h`: "the local star, the motes,
  navigation's course marker"), and those are cues, not sky: they are never
  dimmed, reddened or outshone by a day sky. The course marker is how the
  pilot aims a jump (the heading, one of the jump's three levers), so a
  landed ship under a noon sky can aim a jump without climbing out of the
  air or waiting for the night side. `ShipCounterFrame.*` needs no code
  change for this -- its instances never set the scalar -- and
  `ShipCounterFrameTest` holds it (the marker's and the motes' materials
  leave `AirApplies` at 0).
- **`M_SkyBody` for every other body**: dimmed and hazed by the air the eye
  is in, added to its own air term.
- **`M_SkyGround`** (decision 8): the ground's aerial perspective.

**Sunset is a place, not a time**, while worlds do not spin. The sun's
elevation at a spot is fixed by where the ship is: over the substellar
point it is noon, over the terminator a permanent golden sun, just past it
a twilight with the world's shadow rising in the sky. `ds.Sky.Goto <body>
<km> dusk` already places a ship under a low sun.

**Rejected: the shell drawn from inside as the sky.** It is additive and
cannot dim the stars or redden the star, and it would be a second sky to
hold equal to the ground's haze at 50 km.

**Cost to change:** medium: five materials read one MPC block and one
shared table.

### 8. The ground's haze, and the 50 km parity

Below 50 km over a solid world, `AWorldGround` draws the body at true scale
with `M_SkyGround`, which landing gives `M_SkyBody`'s exact law. It gains
the same air term: `surface x AT_SunThrough(point) x T_view +
AT_InScatter(eye to point)`, with the point from the tile's world position
relative to the body's centre (from `MPC_Sky`), in radii. Below 50 km scene
distances are honest (k = 1), but the term does not read depth anyway: it
has the point.

**At 50 km** the proxy's disc term (decision 6) and the ground's evaluate
the same ray from the same eye in the same units: identical by
construction, and the proxy is hidden exactly there. The rendered test that
holds it is decision 12's `Eyes.AtmosphereParity`.

**Legibility from the ground up**: a distant mountain range fades into the
sky's colour at the horizon at the rate the air's column says; nearby
ground is clear. Nothing is tuned for it.

**Cost to change:** low once decision 6 exists: the same call in a second
material.

### 9. The day sky on the game's scale, against a fixed exposure

The sky's radiance is the scattered fraction of the same sunlight that lights
the ground, so **the air term takes the surface's own brightness factors**:
`Brightness` (compressed irradiance, `ds.Sky.FluxGamma`) and the 1.5 disc
gain, times the in-scatter per unit irradiance, which the law computes with
the same pi convention as the Lambert surface. The ratio of sky to ground
on screen is therefore honest -- a clear noon zenith a fraction of a white
surface's radiance, as on Earth -- and it holds at the 50 km seam because
both sides are one law. One scalar, `ds.Air.Radiance` (default 1, honest),
multiplies every air term, for the playtest to move if the target below is
missed.

**The target, stated so it can be checked** (ruling 6), at the galley EV
(`ds.Sky.Exposure`, mode 1), under clear N2/O2 at 1 bar and 1 g, a 5,772 K
star at noon, at Earth's irradiance:

- **the day sky hides all but the brightest stars**: the zenith's pixel,
  through `M_SkyGlass`, exceeds the pixel of every starfield star below
  flux 28 -- under the starfield's Pareto (alpha 1.5), about 20 of its
  3,000 stars stay visible -- while the local star, and a near world, stay;
- **the windows are bright but not clipped**: the zenith's exposed value
  (scene value x 2^`ManualExposureBias`) is at most half of
  **`ShipSky::ExposedWhite` = 1.0**, a new named constant beside
  `ManualExposureBias` in `ShipSky.h`: the white point that function's own
  derivation takes for manual exposure with no physical camera ("a white
  point of 1"). The default filmic tonemapper, which `AShipSky` leaves at
  its engine defaults, maps an exposed 0.5 well below display white and
  under its shoulder, so the sky keeps its hue. The constant is the test's
  number; that the curve does what this sentence says is checked by eye
  in slice 2's frames, and if a later change to the post-process settings
  moves the curve, the constant is re-derived there;
- **the veil still works**: at night and in orbit the outside is dark and
  the veil's flux-4 target is untouched; by day the veil sits far under the
  sky and reads as a faint reflection, as a window's does.

`DeepSpace.Atmosphere.DaySkyHidesStars` and `.WindowsNotClipped` check the
arithmetic through `ShipSky::ManualExposureBias`, the veil's formula and the
starfield's flux distribution. **Red-dwarf skies are dimmer** with no rule
for it: a redder sun has less of the short wavelengths that scatter, so at
the same irradiance its sky scatters less, and more stars show through it.

**The exposure's calibration.** `ds.Sky.Exposure`'s 0.7 is still an
estimate, to be read in the galley after the room moods (CLAUDE.md). A day
sky does not change *how* it is read -- the reading is of the galley -- but
the targets above are checked against whatever value it lands on, and
re-run when it moves.

**Rejected: auto exposure widened under air.** It re-brightens and
re-darkens the interior with where you land, and loses the unchanging
outside the sky's decision 6 is built on. **Rejected: physical sky
luminance and let windows blow out.** Loses the sky's colour where it
matters and strains the half-float guard.

**Cost to change:** trivial for the scalar; the targets are the decision.

### 10. The deck: the one Sun, tinted by the air, and nothing else

Ruling 5, in `AShipSky`'s sun update, beside the eclipse:

```
Colour    = Blackbody(T) x Chroma(T_sun)
Intensity = SunLux x Compress(irradiance) x SunVisibleFraction x Luminance(T_sun)
```

where `T_sun = AT_SunThrough(ship, star direction)`, from the same `.ush`
compiled into C++, zero-cost outside air. Near the terminator the patch of
sunlight on the galley table goes gold and dims; at noon it is barely
changed. `SunVisibleFraction` stays the eclipse -- the analytic disc
overlap, soft and fractional -- and **the law's own planet occultation is
not applied to the deck**, or a sun behind the world would be counted
twice.

**No sky fill.** A landed ship under a blue sky is lit inside only by the
sun through its windows and its own lamps (*Deliberate fakes*). **No
SkyLight, no new light.** `verify_level.py`'s ban on them is unchanged.

**Rejected: a faked sky fill through an MPC scalar** that interior
materials read. It reopens the sky's decision 5 ("nothing else in the sky
lights the deck"), and ruling 5 closed it. **Rejected: a real-time
SkyLight.** Reverses the rule, the proxy isolation and the verifier, and
costs a capture per frame at 4K.

**Cost to change:** low in code.

### 11. Budget: samples in the shader, one small table per world

- **12 view samples** per air term, with the sun's optical depth
  analytic (Chapman), so no inner loop. That is the whole per-pixel cost of
  the disc, the shell, the backdrop, the stars and the ground. The
  backdrop, the bodies, the stars and the ground are opaque and
  depth-tested, so inside the air each screen pixel runs one march
  whichever material covers it (with the early depth pass; measured): the
  ceiling at 4K is 8.3 million 12-sample marches, plus, from outside, the
  additive shells' pixels.
- **One 32 x 32 multiple-scattering table per airy world**, a value per
  bin in each cell, as one 64 x 32 RGBA16F texture (16 KB; bins 0-3 in the
  left half, 4-7 in the right: atmosphere plan ruling 6), built in C++ from the `.ush` when the system loads (so its values are
  the law's, tested headlessly), uploaded as a transient texture, and set
  as `AirMultiScatter` on that world's own body and shell instances, which
  `AShipSky` already owns. A system of twelve airy worlds is under 200 KB.
  It is rebuilt with the system (a jump, a priors reload), never per frame.
- **The eye's air's table cannot go through `MPC_Sky`**: a Material
  Parameter Collection holds scalars and vectors only (UE 5.8's
  `UMaterialParameterCollection` has `ScalarParameters` and
  `VectorParameters`, no textures). Setting a texture on every consumer's
  instance would fan out to the starfield ISM (which has no instance
  material today), the neighbours, the backdrop, every body and every
  `M_SkyGround` tile. So the route is **one shared texture asset**,
  `T_SkyAirHere` (64 x 32, RGBA16F, a world's table's layout,
  uncompressed, no mips, never streamed), authored by `setup_sky_materials.py` and set as the *default*
  of the `HereMultiScatter` texture parameter in every material that reads
  the eye's air. No instance sets it; C++ overwrites its contents with
  `UpdateTextureRegions` when `AirHere` changes body (a jump, a descent
  into a new air, a priors reload), never per frame. `AShipSky` reaches the
  asset through a slot, `AirHereTable`, assigned by `build_hauler.py`.
  If updating an authored texture at runtime proves unreliable in a
  packaged build, the fallback is the fan-out, owned by `AShipSky` for its
  own instances, the counter-frame for the starfield's (a new instance
  material), and `AWorldGround` for its tiles.
- **No per-frame LUT.** A sky-view table (Hillaire's) is the fallback if
  the backdrop's 12-sample march at 4K misses the frame budget (*Risks*).

**Cost to change:** low: a sample count, a table size.

### 12. What the renderer is held to

Headless tests run under `-nullrhi`, with no GPU; a Custom-node HLSL error
ships the default grey material with every test green. So, as landing
does, the air has rendered tests in `Source/DeepSpace/Tests/Eyes/`, named
outside `DeepSpace.` so `./test.sh` never runs them, run through the lock
with `Tools/eyes.sh`:

- **`Eyes.AtmosphereProbe`** (slice 1). A debug material,
  `M_SkyAirProbe`, authored by `setup_sky_materials.py`, maps UV to a grid
  of (eye, direction, sun) in body radii and outputs the raw air term,
  untonemapped, into an `RTF_RGBA32f` render target; C++ computes the same
  grid through the **F32 mirror** (`AtmosphereF32`), as landing compares
  against `FaceF32`, so the tolerance measures the GPU against the same
  float law and not float against double. **Max relative difference 1e-3,
  or absolute 1e-5 where the term is near zero**, over 256 x 256 samples,
  at eye heights equivalent to k = 1 and k = 1e-3, inside and outside the
  air, for each mix, including the float extremes of decision 1 (every
  sample finite). Like landing's bound it is never loosened: a miss is a
  law problem, escalated. The probe also renders **a real body proxy and
  its shell** through the shipping `M_SkyBody` and `M_SkyAir` at a low-orbit
  k and at the opening framing's k -- the eye reached through
  `CameraPosition` and `ObjectPosition`, the path the shipping material
  takes -- and compares a row of pixels against C++ at the same tolerance;
  and it renders an airless body's disc and asserts it unchanged from its
  pre-air render.
- **`Eyes.AtmosphereCrossing`** (slice 2). The shell's term just above
  `ShellNearMin`'s altitude against the backdrop's and the body's own
  (decision 7) just below it, over the same directions: where the shell
  hands the ray to the eye's air. The same tolerance.
- **`Eyes.AtmosphereParity`** (slice 3). `M_SkyBody`'s disc term and
  `M_SkyGround`'s over the same patch from 50 km, the same tolerance.
  Beside `Eyes.WorldReliefParity`, which holds the surface.

### 13. Entry: a sheath, a roar, a warm light; the flight is unchanged

Ruling 7. A pure function, in `Atmosphere/EntryGlow.{h,cpp}`:

```cpp
// 0 in vacuum and wherever the shock layer would not visibly glow (every
// speed under about 3 km/s in Earth's densest air); rising as sqrt(rho) x
// v^8, the steep speed dependence of shock-layer radiation (what glows),
// not v^3 (stagnation heating: what heats, and nothing here heats);
// 0.5 at the reference, softly saturating toward 1.
double EntryGlow(double DensityKgM3, double SpeedCmPerS);
```

- **The law.** `x = sqrt(rho / rho_ref) x (v / v_ref)^8`, `rho_ref` the
  density of Earth air at 60 km (3e-4 kg/m^3) and `v_ref` 7.5 km/s, so x =
  1 is a re-entering capsule at 60 km. With the knee `k = 0.05`, `s =
  max(0, x - k) / (1 - k)` and **`Glow = s / (1 + s)`**: zero at or below a
  twentieth of the reference, **exactly 0.5 at x = 1**, never reaching 1.
  The density at the ship is the eye's air (decision 7) at its altitude,
  from `AirFacts`' surface density and scale height. Worked through Earth
  air (1.225 kg/m^3 at the surface, H 7.5 km):

  | Where | Speed | x | Glow |
  |---|---|---|---|
  | sea level | 2 km/s | 0.0016 | 0 |
  | sea level | 3 km/s | 0.042 | 0 |
  | 5 km (landing's skim cap: AGL / 2.5 s) | 2 km/s | 0.001 | 0 |
  | 10 km (skim cap) | 4 km/s | 0.21 | 0.15 |
  | 15 km (skim cap) | 6 km/s | 3.9 | 0.80 |
  | 20 km (skim cap) | 8 km/s | 28 | 0.97 |
  | 20 km, half the skim cap | 4 km/s | 0.11 | 0.06 |
  | 50 km (the handover), under the drive's cap | 7.5-10 km/s | 2.3-23 | 0.70-0.96 |
  | 60 km, under the drive's cap (d / 4 s) | 12.5 km/s | 70 | 0.99 |

  (v^3 was the first draft's exponent. Under it 2 km/s at sea level glowed
  at half, and every low skim burned; v^8 is both nearer what glows and
  dark where the ship is slow.)
- **What drives it, said plainly.** Cruise tops at 20 km/s and the drive's
  soft cap holds the ship off at d / `HoldSeconds` -- about 12.5 km/s at 60
  km and 7.5-10 km/s at the 50 km handover. So **the default drive approach
  to an airy world burns**: from the upper air through the handover and
  into the first kilometres of landing's morph band, where the mountains
  rise. Below about 10 km and 3-4 km/s it is dark, which is where a pilot
  skims for somewhere to land at the speeds that let the ground be read.
  **A skim at landing's skim cap above about 12 km burns** (the cap allows
  AGL / 2.5 s: 8 km/s at 20 km), and **a drive-speed pass anywhere inside
  the shell is at full sheath** for as long as it lasts (at 20 km/s a
  grazing chord through Earth's 75 km shell is about 100 s; at 1 c, a few
  milliseconds). That is honest -- a body at those speeds in that air is a
  fireball -- and it is the pilot's speed that chooses it, but it is not
  rare: it is what most arrivals at an airy world look like. What keeps it
  from being a nuisance is not rarity but a bound on what it hides
  (below), and the developer's judgement after back-to-back landings
  (slice 4's done-when, sign-off item 14).
- **The sheath never hides the world.** `M_SkyGlass` gains an orange-pink
  emissive over the glass, brightest on panes facing the velocity (the
  pane's normal against the velocity direction in world axes, from
  `MPC_Sky`), with streaks of noise drawn along the velocity across the
  pane, scrolling at a rate from the speed. Its coverage is
  `AT_SheathProfile(U, V, Facing, Glow)` in `Atmosphere.ush`, so the pure
  tests hold the shipped profile: the full sheath lies on the **outer
  quarter of each pane** (by width and by height) and in the streaks; over
  the **middle half** of every pane, at Glow 1 and facing the velocity
  head-on, the sheath's exposed value is at most **0.1 x
  `ShipSky::ExposedWhite`** (decision 9) -- a wash, under which a lit
  world's albedo-0.1 and 0.3 ground (decision 4's pair, under the galley
  exposure) keep at least half their contrast. `DeepSpace.Atmosphere.
  SheathBound` holds it. Colour is one constant for every mix in this
  sub-project (*Open questions*). `MPC_Sky` gains `EntryGlow` and
  `EntryDirection`, written by `AShipSky`, which asks every frame.
- **The hum**: `FShipHumVoice` gains an entry term, a broadband roar that
  rises with the glow under `ds.Hum.EntryHiss`; `UShipHumComponent` asks
  `EntryGlow` each tick, as it asks everything else. **Its ceiling is named:
  at Glow 1 and any `ds.Hum.EntryHiss` the CVar allows, the roar's RMS is
  at most the reactor drone's RMS at Feed 0** -- the idle drone, the
  quietest the reactor ever is -- so the roar is never louder than an idle
  ship's own hum, whatever the jump is doing. The CVar is clamped to the
  value that reaches that ceiling.
- **A warm pulse through the Sun**: the Sun's colour leans toward the
  sheath's by `0.3 x Glow`, with a flicker whose frequency is **bounded in
  hertz, not keyed to position**. A position-keyed noise has frequency
  speed / wavelength, and no single wavelength is slow at both 7 km/s and
  1 c: slow at 1 c means nothing at all at entry speeds, and slow at entry
  speeds strobes at 1 c. So the flicker is a band-limited noise of world
  time, `EntryGlow::Flicker(TimeSeconds)`, pure, with **no energy above 2 Hz**
  (well under the 3 Hz where flashing lights become a photosensitivity
  concern), varying the Sun's intensity by at most 10% x Glow. It exists
  only while the glow does -- at rest, or slow, there is none -- so it is a
  consequence of the pilot's speed, never something changing on its own.
  `DeepSpace.Atmosphere.EntryFlicker` holds the bound at 20 km/s and at 1
  c (the same answer by construction, which is the point). No new light.
- **Nothing else.** No drag, no heat, no gauge, no warning, no HUD word:
  `FShipFlightState` does not know air exists. The glow is a consequence,
  never a gate.

**Rejected: the air's top as a floor sphere** that eases the drive to rest
above the air. It would put minutes on every landing on an airy world on
the game's schedule, and landing's audit already spends the vision's "a
minute or two". **Rejected: no entry effect.** The fastest arrivals would
look exactly like the slowest.

**Cost to change:** low: a function, a material term, a hum term.

## The anti-chore audit

| Thing | Could it tell the player they are behind? | Why not |
|---|---|---|
| Entry glow | a heat gauge, a corridor to hit; a nuisance repeated on every landing | no gauge, no damage, no warning, no drag. It is **not** rare: the default drive approach to every airy world burns, from the upper air through the handover, and so does a skim at the skim cap above about 12 km. What keeps it from becoming a toll is a bound: the middle half of every pane stays a readable wash at full glow (`SheathBound`), so the approach, the morph band and the search for a landing site are never hidden; slower is dark. Whether it still reads as an event after several landings in a row is the developer's call in slice 4 |
| The entry roar | an alarm | a sound of speed, as the hiss is of effort; silent when slow; never louder than the idle reactor drone (Feed 0) |
| The warm pulse | a strobe | flicker band-limited to 2 Hz at every speed, at most 10% of the Sun's intensity; none when slow |
| The day sky hiding stars | a view taken away | the stars are where they were; fly up out of the air, or to the night side, and they are back; a choice of place |
| The day sky hiding the course marker | the jump held hostage to the weather | the course marker and the motes never take the air term (`AirApplies` 0): a jump can be aimed from under any noon sky |
| Sunset | a clock | worlds do not spin: the light at a spot never changes while the ship stays |
| The tinted deck sun | a change on its own | it changes only as the ship moves |
| Pale red-dwarf skies | a lesser reward | they are most of the universe; the blue sky's rarity is the contrast, not a grind for it |
| Haze toward the limb | a surface hidden | nadir is legible by guarantee; fly over what you want to see |
| Procgen air | a stat to optimise | nothing reads it but the look; no world is better for landing because of its air |

## Deliberate fakes, and what they cost later

- **No greenhouse.** Scale height uses `EquilibriumK`, so every air is
  about 12% shallower than a greenhouse-warmed one would be. Cost: a
  greenhouse term on `FPlanet` (a third derived fact), then re-derive.
- **A mix is a whole air.** Aerosol and absorber follow the mix, not the
  world. Cost: a third draw, when the corpus shows the sameness.
- **The N2/O2 mix carries ozone**, which needs oxygen, which needs life.
  Cost: splitting the mix (*Open questions*).
- **The limb band cannot dim a star behind it** (additive shell). Cost: a
  translucent shell with per-channel dual-source blending, or drawing the
  annulus in the air backdrop.
- **No sky fill inside.** A landed ship's shadows are black under a blue
  sky. Cost: a ruling that reopens the sky's decision 5.
- **The deck ignores terrain.** A ridge between a landed ship and a low sun
  does not shadow the cockpit (landing's own fake). Cost: a horizon march
  in `SunVisibleFraction`.
- **Slice 1's interim**: until slice 2, a ship below the shell-hide
  altitude (a_top x 10/9, about 83 km over Earth air) but above 50 km sees
  the disc through the air correctly, **no shell** (hidden, as decision 6
  says, from slice 1 on) and a **black sky overhead**. Slice 1 is judged
  from above that altitude. Cost: none once slice 2 merges.
- **The 50 km step between slices 2 and 3**: until slice 3 merges, the
  proxy's disc above 50 km carries the air term and `M_SkyGround` below it
  does not, so the handover shows a step in haze. Slice 2 is not judged on
  it; slice 3 removes it. Cost: none once slice 3 merges.
- **One entry colour for every air.** Cost: a colour per mix.

## Fixture worlds

Universe seed 20260925, priors as committed. Each test names its world by
seed, system and orbit index, so a change that moves one fails loudly
rather than testing somewhere else.

**The fixtures are chosen by the mix they need, after the draws exist.**
Which mix and pressure a world draws is not known until track G's draws
are committed, and a look written against a named world can fail because
of the draw rather than the code: with these weights a 0.55 M_E, 252 K
world that cannot hold hydrogen draws CO2 about 42% of the time, and CO2's
dust eats blue. So every done-when below is written **by role** ("an N2/O2
world under the home star"), and **track G's first procgen commit fills
this table** -- each world's name, index, mix, pressure, gravity and nadir
tau450 -- by the rule in each row, before slices 2 and 4 are planned in
detail. Where the preferred world draws the wrong mix, the rule names the
next.

| Role | Rule | Candidate | Why |
|---|---|---|---|
| **R**: a red-dwarf N2/O2 sky | the home system's terrestrial or ocean world with N2/O2, else the nearest N2/O2 world under an M dwarf | Baemsekai V (terrestrial, 252 K, 2,566 K star), if it draws N2/O2; it drew CO2 (it is C), so Gelaes III | most skies look like this one |
| **G**: a G-star N2/O2 sky | the nearest N2/O2 world under a 5,000-6,000 K star | Sova IV (terrestrial, 5,306 K, one jump from home), if it draws N2/O2; Sova V, as drawn | the rare blue |
| **C**: a CO2 sky | the nearest CO2 world to home | Baemsekai V, as drawn | the dusty sky most of the rest will wear |
| **N**: airless, unchanged | the home system's first barren world | Baemsekai I (barren; I-IV all are, and I is first) | the air term must be exactly zero |
| **J**: a giant's air | the nearest gas giant to home, named with its mass and disc pressure | Krothmertas VII, as drawn | H2/He haze, the fixed rim's replacement |
| **E**: entry | R, from its drive floor's approach, at 15 km/s | -- | a drive approach into air |

**The fixtures as drawn** (universe seed 20260925, priors as committed at this
table's commit; found by `DeepSpace.Atmosphere.FixtureWorlds`, which holds
each role to its row -- world, sector, slot, orbit index, mix, and pressure
to four figures -- and fails when a change moves one; its log reprints the
table, so a meant move is copied from there into this table and the test's
together):

| Role | World | Sector, slot, orbit index | Mix | Pressure (bar) | Gravity (g) | tau450 | Mass (M_E) | Star (K) | Distance (ly) |
|---|---|---|---|---|---|---|---|---|---|
| R | Gelaes III | (-2, -1, 0), slot 0, orbit index 2 | nitrogen-oxygen | 1.068 | 0.984 | 0.301 | 0.96 | 3673 | 6.97 |
| G | Sova V | (0, -2, 1), slot 0, orbit index 4 | nitrogen-oxygen | 0.5134 | 0.813 | 0.175 | 0.62 | 5306 | 5.31 |
| C | Baemsekai V | (-1, -1, 0), slot 0, orbit index 4 | carbon-dioxide | 0.5285 | 0.766 | 0.295 | 0.55 | 2566 | 0.00 |
| N | Baemsekai I | (-1, -1, 0), slot 0, orbit index 0 | none | 0 | 0.548 | 0.000 | 0.26 | 2566 | 0.00 |
| J | Krothmertas VII | (-2, -5, 1), slot 0, orbit index 6 | hydrogen-helium | 0.4015 | 0.699 | 0.320 | 84.63 | 4765 | 19.83 |
| E | as R | | | | | | | | |

To look (for R at orbit index n): `ds.Sky.Goto n 40000` (the opening
framing), `ds.Sky.Goto n 1000 dusk` (the terminator), and a new side,
`ds.Sky.Goto n 40000 backlit` (the world between the ship and its star:
the ring). Not `ds.Sky.Goto n 30`: 30 km is inside the air, which is slice
2's.

## Implementation outline

### New files

- `Shaders/Private/Atmosphere.ush` -- the shipped law (`AT_`-prefixed,
  landing's scalar subset and shims).
- `Source/DeepSpace/Atmosphere/Atmosphere.{h,cpp}` (compiles the `.ush`
  twice, F64 and F32;
  `FAtmosphere` built per airy world: coefficients, shape, the
  multiple-scattering table; `AirHere`), `AtmosphereReference.{h,cpp}`,
  `EntryGlow.{h,cpp}`.
- `Source/DeepSpace/Universe/AirFacts.{h,cpp}` -- `EAirMix`, gas constants,
  retention, scale height, column, the nadir ceiling.
- `Source/DeepSpace/Tests/AtmosphereTest.cpp`, `AirFactsTest.cpp`, `AirProcGenTest.cpp`,
  `AirFixtureWorlds.h`,
  `Tests/Eyes/AtmosphereProbeTest.cpp`, `Tests/Eyes/AtmosphereCrossingTest.cpp`,
  `Tests/Eyes/AtmosphereParityTest.cpp`.
- `Tools/sky_air_ground.py` -- the function that authors `M_SkyGround`'s
  air call, imported by whichever script landing (b) authors `M_SkyGround`
  with (track A's, so it never shares a file with D).
- Assets authored by `setup_sky_materials.py`: `M_SkyAir`, `M_SkyAirDome`,
  `M_SkyAirProbe`, `T_SkyAirHere`.

### Changed files

- `Universe/StarSystem.h` (`FPlanet::AirMix`, `SurfacePressureBar`),
  `StarSystemGenerator.*` (the two draws), `GenPriors.*` (priors,
  guarantees, domain), `Config/DefaultGame.ini`, the corpus contract,
  `procgen_corpus.py`, `ProcGenCorpusTest.cpp` and `CorpusSkiesTest.cpp`
  (the noon skies, decision 2).
- `Sky/SkyColour.*` (`ThroughFilter`), `SkySystem.*` (`FSkyBody::Air`,
  the outer radius; `Rim` deleted), `SkyProjection.*` (stacking on the
  outer radius for an airy body not first in order), `ShipSky.*` (the new
  slots, shell proxies, the `AirDome` backdrop, the air parameters and
  tables, `T_SkyAirHere`'s update, `MPC_Sky`'s air block, `AirApplies` on
  the local star, `ExposedWhite`, `FindMaterialProblems`, the Sun's tint
  and warm pulse, `Goto ... backlit`, `ds.Air.*`). `ShipCounterFrame.*` is
  unchanged (decision 7).
- `SkyMaterialContract.h`, `Tools/sky_material_contract.json`,
  `Tools/setup_sky_materials.py` (`M_SkyBody`'s air call, `M_SkyAir`,
  `M_SkyAirProbe`; later `M_SkyAirDome`, `T_SkyAirHere`,
  `M_SkyStarfield`'s and `M_SkyStar`'s air, `M_SkyGlass`'s sheath,
  `MPC_Sky`; `M_SkyGround`'s through `Tools/sky_air_ground.py`).
- `Ship/ShipHumVoice.*`, `ShipHumComponent.cpp` (the entry roar).
- `Tests/LocalSystemTest.cpp` (the rim assertion), `Tests/SkyTestFixtures.h`
  (sets `Home.Rim`; the build breaks without it), `SkyProjectionTest.cpp`,
  `SkyMaterialContractTest.cpp`, `ShipSkyTest.cpp`, `ShipCounterFrameTest.cpp`
  (the marker and the motes untouched by the air), `HumVoiceTest.cpp`.
- `Tools/build_hauler.py` (`place_sky`: the new slots).

**New asset slots on `AShipSky`**, each an `EditAnywhere` `UPROPERTY`
assigned by `build_hauler.py` (ADR 0002: C++ never loads a material
itself), as `BodyMaterial`, `StarMaterial` and `PointStarMaterial` are:
`AirMaterial` (M_SkyAir, slice 1), `AirDomeMaterial` (M_SkyAirDome, slice
2), `AirHereTable` (T_SkyAirHere, slice 2). `M_SkyAirProbe` needs none: the
`Eyes.` test loads it by path, as landing's relief probe is. These, and the
`FSkyBody` and `FPlanet` fields, are header and reflection changes:
`./rebuild.sh --force`, then **a level rebuild** (`build_hauler.py`, then
`verify_level.py`), since a placed `hauler_sky` keeps the slots it was
saved with, and `check_blueprints.py`. The shell and backdrop components
are runtime `NewObject`s and change no saved hierarchy.

**Checking the new slots, and ruling 3.** Ruling 3 says "No
`verify_level.py` change". Its purpose, from its context, is that no
engine atmosphere component is admitted, and that ban stands untouched.
But the verifier is also where every sky slot is checked (`check_sky`), and
the new slots would be the only ones it does not check. This spec does not
reinterpret the ruling: **until the developer rules otherwise, the slots
are checked in C++** -- `AShipSky::FindMaterialProblems`, like the
counter-frame's, warns at `BeginPlay` for a missing or wrong slot, and
`ShipSkyTest` holds that it names each one -- and `verify_level.py` is
unchanged. Whether to add the three slot checks to `check_sky` as well is
sign-off item 20.

### Material contract additions

Per body (`M_SkyBody`, `M_SkyAir`): `AirRayleigh` (per-channel scattering
per radius), `AirMie` (per-channel extinction per radius; w the asymmetry),
`AirAbsorb` (per-channel absorber per radius), `AirShape` (x Rayleigh H/R,
y Mie H/R, z `AirTop`, w the Mie single-scatter albedo), `AirMultiScatter`
(texture, per instance). In `MPC_Sky` (scalars and vectors only), the air
the eye is in: `HereRayleigh`, `HereMie`, `HereAbsorb`, `HereShape`,
`HereEye` (xyz the eye in radii, world axes; w 1 if in air), `HereSun` (the
star's direction, world axes), `HereCentre` (the body's centre from the
camera, in radii); and `EntryGlow`, `EntryDirection`, `AirRadiance`. The
eye's air's table is **not** in `MPC_Sky` (it cannot hold a texture): it is
`HereMultiScatter`, a texture parameter whose default is `T_SkyAirHere`, in
`M_SkyAirDome`, `M_SkyStarfield`, `M_SkyStar`, `M_SkyBody` and `M_SkyGround`
(decision 11). `M_SkyStar` gains the scalar `AirApplies` (default 0).
Every name on all three sides, `DeepSpace.Sky.MaterialContract` holding
them equal; a misspelt name fails silently. `Rim` leaves all three.

### CVars and commands

| CVar | Default | Lives in |
|---|---|---|
| `ds.Air.Radiance` | 1 (honest sky-to-ground ratio) | `ShipSky.cpp` |
| `ds.Air.Show` | 1; 0 draws every world airless, for comparison | `ShipSky.cpp` |
| `ds.Entry.Glow` | 1 (scales the sheath and the pulse) | `ShipSky.cpp` |
| `ds.Hum.EntryHiss` | 0.5; clamped so the roar at Glow 1 never exceeds the drone's RMS at Feed 0 | `ShipHumComponent.cpp` |

Constants with tests on them, not CVars: the 12 samples, the 32 x 32 table,
`AirTop` 10 H, `ShellNearMin` 5 km, the entry law's references, exponent 8
and knee 0.05, the sheath bound (outer quarter; 0.1 x `ExposedWhite` over
the middle half), the flicker's 2 Hz band and 10% depth,
`ShipSky::ExposedWhite` 1.0, the guarantees (`ExobaseFactor`,
`RetainedAbove`, `LostBelow`, `MaxNadirTau450`).

`ds.Air.Describe [body]` -- the air of the world the ship is in or the
named one: mix, pressure, scale height, the nadir optical depth per
channel, the noon zenith colour from the ground, retention of each mix.

## Tests

All pure where possible, headless, siblings with no children (a test path
with children becomes a group and silently stops running), and **each
proven able to fail with `Tools/mutate.sh`** before it is trusted. **Every
test is tagged with the slice whose done-when requires it**; a slice's
done-when names exactly its tagged tests, and a test tagged for a later
slice is not required earlier. [O] marks the ones track O may write before
sign-off (ruling 1).

- **Optics, pure (`DeepSpace.Atmosphere.*`)**:
  - **[1, O]** `.ReferenceKnownValues`: Earth air under 5,772 K, Rayleigh
    optical depth 0.097 at 550 nm within 3%; the zenith's blue channel
    above green above red; the horizon whiter than the zenith; a sun at the
    horizon transmits red over blue.
  - **[1, O]** `.Chapman`: `exp(AT_LogChapman)` against numerical
    integration, within 0.5% to 89 degrees and through the horizon to 30
    degrees below it; finite in F32 throughout.
  - **[1, O]** `.LawMatchesReference` (decision 1's grid and tolerance,
    the reference's second scattering isotropic as the law's is: atmosphere
    plan ruling 7), for F64 and F32. The default suite runs its hardest air
    along its longest paths; the whole grid is
    Atmosphere.Full.LawMatchesReference, and the full second order's gap,
    within 25% or 1e-3, Atmosphere.Full.MultipleScatteringGap, both run by
    name before a merge.
  - **[1, O]** `.FloatMatchesDouble` (decision 1): F32 against F64 at 1e-3
    relative or 1e-5 absolute, every output finite, including a sun 30
    degrees below the horizon and the limb from 7.8 R and 1e3 R.
  - **[1, O]** `.HomothetyInvariance`: the same term in double, at k = 1
    and k = 1e-3, the eyes within 1e-12 relative and the law's outputs
    within 1e-9 (atmosphere plan ruling 5: the proxy's eye and the true eye
    are one ratio formed by two roundings), with the eye formed as `SkyProjection`
    forms it after `RenderableScale`'s rounding (the GPU's float path is
    `Eyes.AtmosphereProbe`'s).
  - **[1, O]** `.StarColour`, in linear sRGB, HSV saturation `S = 1 -
    min/max` and hue in degrees, of the noon zenith from the ground
    (straight up, the sun 45 degrees high: atmosphere plan ruling 4) under
    Earth air at 1 bar and 1 g, from 2,000 to 15,000 K: S falls to the
    greyest sky, between 3,000 and 4,500 K, and rises from it; from 4,000 K
    up the hue is blue (200-240 degrees); **under 2,566 K a peach sky, S
    between 0.62 and 0.82 and hue between 15 and 40 degrees**; **under
    2,000 K, S at least 0.85**; **under 5,772 K, hue between 200 and 235
    degrees and S between 0.40 and 0.85** (Earth's clear zenith). No
    palette, no floor, no white balance per star. (Amended by atmosphere
    plan ruling 1: in the game's linear sRGB with a D65 white, a red
    dwarf's sky is the star's own orange pulled toward blue by lambda^-4,
    not the pale grey-cyan first estimated, which read the sky relative to
    the star's light.)
  - **[1, O]** `.LimbBeyondSilhouette`, `.TerminatorReddens`,
    `.CrescentAtHighPhase`, `.BacklitRing` (ring radiance with the star
    behind exceeds the lit limb's at 90 degrees phase), `.AirlessIsZero`.
  - **[1, O]** `.MultiScatterTable`: the C++ table against the
    reference's second order.
  - **[1, O]** `DeepSpace.Sky.OneBlackbody` (decision 3): the comparison
    only; the re-base waits on sign-off item 3.
  - **[1]** `.NadirLegible` (decision 4), through `AirFacts`' ceilings for
    every mix, giants' disc depth included.
  - **[1]** `Atmosphere.Full.GroundSkySwatch`, a writer like the corpus test, run
    by name and outside the default suite, since it is a picture for the
    developer's eyes rather than a gate: the noon and
    dusk ground sky of fixtures R and G (and C), as a fisheye through the
    shipped law in C++, exposed through `ManualExposureBias` at the galley
    EV, to `Saved/air_swatch_*.png`; it fails only if it writes nothing.
  - **[2]** `.DaySkyHidesStars`, `.WindowsNotClipped` (against
    `ExposedWhite`), `.RedDwarfSkyDimmer` (decision 9).
  - **[2]** `.SunThroughAir`: the deck's tint at noon, terminator and below
    the horizon; unchanged outside air; no occultation double count with
    `SunVisibleFraction`.
  - **[2]** `.AirHere`: one air or none, over a sweep of positions,
    including the band above the shell where the shell is hidden; shells
    never overlap in the corpus's systems.
  - **[4]** `.EntryGlow`: zero in vacuum; zero at 2 and 3 km/s at Earth's
    sea-level density (1.225 kg/m^3); zero along landing's skim cap below
    9 km; **exactly 0.5 at the reference** (60 km's 3e-4 kg/m^3, 7.5 km/s);
    the table in decision 13 within 1e-2; monotonic in speed and density;
    always below 1.
  - **[4]** `.SheathBound`: `AT_SheathProfile` at Glow 1, head-on, over the
    middle half of a pane, at most 0.1 x `ExposedWhite` exposed; the full
    sheath only in the outer quarter and the streaks.
  - **[4]** `.EntryFlicker`: `EntryGlow::Flicker`'s spectrum has no energy
    above 2 Hz and its depth is at most 10% x Glow, at 20 km/s and at 1 c;
    zero at Glow 0.
- **Gases (`DeepSpace.Universe.Gases`, `AirFactsTest.cpp`)** **[1]**: the
  pure `AirFacts`: retention (Earth keeps N2/O2 and loses H2/He; the
  0.3 M_E, 320 K case; the 10 M_E, 200 K case); the scale heights; the
  ceilings at 1 g and their linearity in gravity and retention; a giant's
  disc (a 15 M_E giant about 0.07 bar, a Jupiter about 1.5, at ruling 2's
  0.32); the smooth
  ceiling's shape; decision 5's table, every constant of every mix,
  pinned where it is defined.
- **Procgen (`DeepSpace.Universe.Air`)** **[1]**: Barren and Ice airless;
  giants H2/He with their disc at `MaxNadirTau450`; no world holds a
  mix whose retention is zero; every temperate world's pressure is
  `SmoothCeiling(drawn, PressureCeilingBar(mix, g, retention))`, recomputed
  from its inputs; every airy world, giants included, at or
  under `MaxNadirTau450`; **the smooth ceiling leaves no spike**: over
  the corpus's airy worlds, with `P_max` each world's own ceiling (a
  per-world ratio, since the ceiling varies with gravity, mix and
  retention, and a histogram of raw pressure would smear it), the fraction with `P / P_max` above 0.99999 is
  under a tenth of the fraction whose pre-ceiling draw exceeded `P_max`
  (recomputed through the generator's pure pressure function) -- a clamp
  would put every one of those exactly at the ceiling, a ratio of 1; the
  two new labels move no other quantity of any world (kinds, masses,
  orbits, `ReliefKm`, `DayHours` compared with the draws off); domain
  refusals name the line.
  `DeepSpace.Universe.Seed` and `.Stream` untouched.
- **Sky**:
  - **[1]** `SkyProjectionTest`: the first body stacked on its solid
    radius, k = 50 km / altitude, 1 at 50 km; an eye far above the shell,
    just above the shell-hide altitude, at the shell's top and inside it,
    k finite and continuous throughout; an airy body second in order
    stacked on its outer radius, its shell behind the first body's far
    side; no body inside another's shell's interval.
  - **[1]** `ShipSkyTest`: a shell per airy world, none for the airless;
    the shell's flags; **the shell hidden below the shell-hide altitude**;
    `FindMaterialProblems` names a missing `AirMaterial`.
  - **[1]** `LocalSystemTest` (air, not rim); `SkyMaterialContract`.
  - **[2]** `ShipSkyTest`: the backdrop shown only while `AirHere`
    answers; `AirApplies` 1 on the local star only;
    `FindMaterialProblems` names a missing `AirDomeMaterial` and
    `AirHereTable`. `ShipCounterFrameTest`: the course marker's and the
    motes' materials leave `AirApplies` at 0.
- **Hum** **[4]**: `HumVoiceTest`'s entry term: silent at zero glow; at
  Glow 1 and the CVar's clamp, the roar's RMS at most the drone's RMS at
  Feed 0.
- **Rendered, outside `./test.sh`**: **[1]** `Eyes.AtmosphereProbe`;
  **[2]** `Eyes.AtmosphereCrossing`; **[3]** `Eyes.AtmosphereParity`.

**Tests that pin today and will move:** `LocalSystemTest`'s rim and
`SkyTestFixtures.h`'s `Home.Rim`; `SkyProjectionTest`'s stacking numbers
where an airy world stands second or later in the stack;
`SkyMaterialContractTest`'s lists; `HumVoiceTest`'s totals. Each is
changed in the commit that changes the behaviour, with the reason in the
message.

## The slices

### Slice 1: air seen from orbit

The pure optics (track O) starts now, within ruling 1: the law, the
reference and their tests, and `ThroughFilter` with the `OneBlackbody`
comparison -- **not** a re-base of `Blackbody`, which waits on sign-off
item 3. The rest waits for landing slice (a) to merge: the procgen draws
and corpus columns (and the fixture table, filled by G's first commit),
`FSkyBody::Air`, the per-world build and tables, the disc term in
`M_SkyBody`, `M_SkyAir`, its `AirMaterial` slot and the shells, the
stacking rule for an airy body not first in order, **the shells hidden
below the shell-hide altitude**, the rim deleted, `M_SkyAirProbe` and
`Eyes.AtmosphereProbe`, `ds.Air.Describe`, `Goto ... backlit`, the level
rebuild.

**Done when:**

- `./build.sh` and `./test.sh` are green with every test tagged **[1]**
  in *Tests*, each mutate-proven;
- `Eyes.AtmosphereProbe` passes, including the real proxy and shell and
  the airless disc unchanged;
- the frame at 4K on this machine stays within 16.6 ms with fixture R
  filling the view from 500 km and from the opening framing, `stat unit`'s
  game, render-thread and GPU times reported;
- the corpus reads: `procgen_corpus.py` shows the mixes, pressures and
  noon zenith colours across the 10,000 nearest systems (the colours from
  `Atmosphere.Full.CorpusSkies`'s file), and the developer has seen the
  spread;
- **the ground skies, before the ground slices**: `Atmosphere.Full.GroundSkySwatch`'s
  noon and dusk fisheyes under fixtures R, G and C are in `Saved/` and
  have gone to the developer, so an honest red-dwarf sky seen *from the
  ground* is judged now, while ruling 2's risk is cheap to act on, and not
  after slice 2 is built;
- before/after frames of R and G from the opening framing, at dusk and
  backlit, and of J, go to the developer; the look is the next playtest's
  question.

### Slice 2: inside the air -- the sky, the star and the deck's sun

After landing slice (b) merges. `AirHere`, `MPC_Sky`'s air block, the
shared `T_SkyAirHere` table and its slot; the `AirDome` backdrop,
`M_SkyAirDome` and its slot; the starfield, the local star (through
`AirApplies`) and other bodies through the air; the deck's Sun tinted
(decision 10); `ds.Air.Radiance` and the day-sky targets;
`Eyes.AtmosphereCrossing`; the level rebuild.

**Done when:** every test tagged **[2]** is green and mutate-proven, and
`Eyes.AtmosphereCrossing` passes; descending on fixture R from 200 km to
its drive floor, the sky goes from black through its pale sky with **no
step where the shell hands over to the eye's air**, the stars fade by
contrast and return climbing out, and the course marker stays teal and
undimmed under a noon sky; at `ds.Sky.Goto <R> 1000 dusk` the deck's sun
patch is gold; the same descent on fixture G is blue; the frame at 4K
within 16.6 ms at the ground, looking at the horizon; with landing slice
(c) merged, the landed ship under both skies, walked, to the developer.
**The 50 km handover is not judged here**: until slice 3 merges, the ground
below it has no air term (*Deliberate fakes*).

### Slice 3: the ground through the air, and the 50 km parity

After landing slice (b) merges **and after D's interface commit** (the
`MPC_Sky` air block's names in the contract files, which `M_SkyGround`
reads); beside slice 2 (different files). The air term in `M_SkyGround`,
authored by `Tools/sky_air_ground.py`; `Eyes.AtmosphereParity`.

**Done when:** every test tagged **[3]** passes -- `Eyes.AtmosphereParity`
(disc against ground at 50 km); with slice 2 merged, a descent through the
handover over fixture R shows **no brightness or colour step at 50 km**;
distant ground fades to the horizon's colour.

### Slice 4: entry

After slice 2 merges (it uses the eye's air). `EntryGlow`, the sheath and
streaks on `M_SkyGlass` through `AT_SheathProfile`, `MPC_Sky`'s entry
pair, the hum's roar, the Sun's warm pulse.

**Done when:** every test tagged **[4]** is green and mutate-proven; a
drive approach to fixture E's floor burns the forward panes' edges, keeps
the middle of every pane readable, and fades as the cap slows the ship;
the same descent on cruise at 200 m/s shows nothing; a skim under 10 km at
under 4 km/s shows nothing; a tangential drive pass through the upper air
flares and fades; airless worlds never glow; **and the developer flies
several landings back to back on airy worlds (at least three, R and G
among them) and judges whether the burn still reads as an event or has
become a toll.** If a toll, the answer is on the sign-off list's item 14
(a narrower sheath, a higher knee, or a glow that the default approach does
not reach), never a gauge or a warning.

## Parallel tracks and file ownership

Each track is a worktree under `.worktrees/`, building with `./build.sh`
and testing with `./test.sh` behind `Tools/ue_lock.sh`. Agents are briefed
with the machine's cap: at most 3-4 workers, `nice -n 19`. **Every changed
file has exactly one owner at a time**, across slices that run together.

| Track | Slice | Owns | Waits on |
|---|---|---|---|
| **O: optics** | 1 (starts now) | `Shaders/Private/Atmosphere.ush` (a new file beside landing's), `Atmosphere/*` (not `EntryGlow.*`) and its pure tests, `Sky/SkyColour.*` (`ThroughFilter` only; the re-base waits on item 3) and `OneBlackbody` | -- (its `AT_` shims stay inline in `Atmosphere.ush`: landing (a) made no shared shim header, and `WorldRelief.ush` keeps its `WR_` shims inline; whether to extract one shared subset header is **orbital slice 1's first decision**, and `WorldRelief.ush` is landing track T's through slice (b)) |
| **G: procgen** | 1 | `Universe/AirFacts.*`, `StarSystem.h`, `StarSystemGenerator.*`, `GenPriors.*`, `DefaultGame.ini`, the corpus files, `AirProcGenTest.cpp`, the fixture table in this spec | landing (a) merged (landing's track P owns these files until then) |
| **M: materials and sky** | 1 | `Sky/SkySystem.*`, `SkyProjection.*`, `ShipSky.*`, `SkyMaterialContract.h`, `sky_material_contract.json`, `setup_sky_materials.py`, `build_hauler.py` (`place_sky`), `LocalSystemTest.cpp`, `Tests/SkyTestFixtures.h`, `SkyProjectionTest.cpp`, `ShipSkyTest.cpp`, `SkyMaterialContractTest.cpp`, `Tests/Eyes/AtmosphereProbeTest.cpp` | landing (a) merged; track O merged; the shim decision, orbital slice 1's first; G's `AirFacts.h` (agreed first as a header) |
| **D: inside the air** | 2 | `ShipSky.*`, **the contract files and `setup_sky_materials.py` (sole owner while D and A overlap)**, `build_hauler.py` (`place_sky`), `ShipSkyTest.cpp`, `ShipCounterFrameTest.cpp`, `Tests/Eyes/AtmosphereCrossingTest.cpp` | slice 1 merged; landing (b) merged |
| **A: aerial perspective** | 3 | `Tools/sky_air_ground.py` (new), `Tests/Eyes/AtmosphereParityTest.cpp` | slice 1 merged; landing (b) merged; **D's interface commit**: the `MPC_Sky` air block's names in the contract files, and the one-line import of `sky_air_ground.py` into the script that authors `M_SkyGround`, both made by D |
| **E: entry** | 4 | `Atmosphere/EntryGlow.*`, `Ship/ShipHumVoice.*`, `ShipHumComponent.cpp`, `HumVoiceTest.cpp`; `ShipSky.*`, the contract files and `setup_sky_materials.py` for `M_SkyGlass`, `AT_SheathProfile` in `Atmosphere.ush` (O's file, handed to E for slice 4) and the entry pair | slice 2 merged |

Merge order: O's pure core and tests whenever green (they touch no shared
file); after landing (a), G, then M (the shim decision first), then slice 1
merges when its done-when holds; after landing (b), D's interface commit
first, then D and A in parallel, D merging first; then E.

## Documentation, after the merges

- **CLAUDE.md**: a new section, *The air* -- the law and the reference, the
  shared `.ush`, what is drawn and what derived, the nadir guarantee, one
  air at a time through `MPC_Sky`, the shells, the deck's tint, the entry
  glow, `ds.Air.Describe` -- and the architecture line
  (`Source/DeepSpace/Atmosphere/`). *The sky*: the material list gains
  `M_SkyAir`; "the materials are unlit, and the sun lights only the ship"
  stays and gains "tinted by the air it crosses"; the Rim is gone from
  every mention. *The universe*: the two draws and the guarantees. The
  tunables table: the four CVars. *The hum and the lamps*: the roar.
- **`docs/vision.md`**: a paragraph under *Worlds and why you go to them*,
  or a new *Art direction* heading: skies are derived, never picked; blue
  is what a G star gives, and most of the universe is not lit by one; a
  blue sky should be an event.
- **The sky spec**: its `Rim` superseded; decision 5's sun gains the air's
  tint.
- **The landing spec**: ruling 7 and decision 14 point here.
- **A new ADR 0009, "Our own atmosphere, not the engine's"**: why
  SkyAtmosphere, VolumetricCloud and height fog stay banned (one per world,
  true scale, depth, the second sun), and that clouds, when they come, start
  from this law rather than from the engine's.

## Risks

- **Custom-node errors pass the headless suite.** A grey `M_SkyBody` with
  every test green. Mitigated by `Eyes.AtmosphereProbe` in slice 1's
  done-when: a broken material cannot draw the probe.
- **Landing (a)'s route fails its spike.** Its fallbacks (pasted text in
  the Custom node; engine nodes kept) apply here too: the `.ush` pasted by
  the setup script, held by the same rendered probe.
- **Cost at 4K.** A disc that fills the screen from low orbit, or the
  backdrop and the bodies inside the air (one opaque march per pixel,
  whichever covers it), is 8.3 million pixels of 12-sample march. Measured
  in slices 1 and 2's done-when; if missed, the sky-view table (decision
  11), or fewer samples where the view's airmass is small.
- **A new sky surface.** The backdrop (`AirDome`) is the first thing the
  sky draws that is not a body or a point. It sits at 1.02 x `DomeRadius`,
  behind every star; if the far plane or the counter-frame's dome radius
  moves, it must move with them, and `ShipSkyTest` holds it behind the
  stars.
- **Two new texture routes.** Updating an authored texture
  (`T_SkyAirHere`) at runtime, and a texture read from inside included
  shader code (the `AT_MultiScatter` hook), are both new here; landing
  (a)'s spike covers neither. The probe exercises both in slice 1 (the
  per-world table through the hook) and slice 2 (the shared table); the
  fallbacks are decision 11's fan-out and single scattering plus a
  constant multiple-scattering term fitted per world.
- **Half-float.** A backlit ring beside a star, the star's disc through
  thin air, forward scattering at small angles: every air output passes the
  existing guards (the face clamp, the star ceiling) and a new clamp on the
  in-scatter; `.BacklitRing` and `.WindowsNotClipped` check the extremes.
- **Honest red-dwarf skies may read as no sky.** Ruled (ruling 2); the risk
  is that the developer, seeing them, wants a floor. The orbit frames do
  not show a sky from the ground, so slice 1's done-when includes
  `Atmosphere.Full.GroundSkySwatch`: noon and dusk fisheyes of the ground sky under a red
  dwarf, a G star and a CO2 world, through the shipped law, before slice 2
  is started. The ruling stands; the swatch is how the developer sees what
  it produces while acting on it is still cheap.
- **Entry burns on most arrivals.** The default drive approach to an airy
  world glows from the upper air through the handover (decision 13). It is
  bounded (`SheathBound`: the middle of every pane readable) and judged
  after back-to-back landings in slice 4; the alternatives are sign-off
  item 14's.
- **Eight bins may not carry the extremes.** Three channels, and every
  four- to six-bin partition, missed the reference (atmosphere plan rulings
  3 and 6), so the law carries eight spectral bins, averaged per bin and
  folded to RGB at the end. A long limb path under a 2,500 K star is still
  the worst case; `LawMatchesReference` measures it, and there is no
  cheaper fallback left to take -- a miss there means more bins.
- **The exobase factor is a single number standing for a whole
  thermosphere.** It decides which worlds may hold hydrogen. Its effect is
  visible in the corpus before anyone flies there.
- **Two sky slices after landing (b).** The air's biggest moment waits on
  the terrain. Slice 1's interim black sky inside the air is the cost.
- **Shared files with landing.** `setup_sky_materials.py`, the contract,
  `SkySystem`, `ShipSky` and the procgen files are landing's in slices (a)
  and (b). Nothing here starts on them until the landing slice that owns
  them has merged.

## Decisions needing sign-off

Each is expensive to reverse, goes past a ruling, or is a choice a
reasonable person could make differently.

1. **Two levels: a brute-force spectral reference in C++, and the shipped
   law in a shared `Atmosphere.ush` compiled into C++ and every sky
   material, held to the reference by pure tests and to the GPU by a
   rendered probe; the `.ush` in landing's scalar subset (component
   arguments, struct returns), compiled in C++ as both F64 and an F32
   mirror, written in the log domain so float cannot overflow.**
   *Alternatives:* the shipped law alone, tested only against
   hand-computed values; a C++ port beside hand-written HLSL.
   *Recommended:* two levels. *Cost to change later:* high: every air
   material calls it.
2. **The air is drawn as ruled, and shaped, not rejected: the mix's weights
   multiplied by retention before the draw; pressure log-normal (medians
   0.8 bar terrestrial, 1.0 ocean, sigma 0.9) under a smooth ceiling;
   giants fixed at H2/He, their drawn disc defined at the depth where the
   air above reaches `MaxNadirTau450` (0.07 bar on a 15 M_E giant, about
   1.5 on a Jupiter, at ruling 2's 0.32), not drawn.** *Alternatives:* clamp instead of the
   smooth ceiling (a spike at the cap in the corpus); giants drawn too (a
   giant's "surface pressure" means nothing); giants at a 1-bar datum and
   exempt from the guarantee (a 15 M_E giant then sits under tau450 4.5, a
   featureless haze from orbit; every giant under about 210 M_E exceeds the
   cap, 134 M_E at the first 0.5).
   *Recommended:* as specified. *Cost to change later:* low for priors;
   medium once worlds have been visited.
3. **`SkyColour::ThroughFilter` integrates the star's spectrum, and if the
   existing `Blackbody` disagrees with it by more than 2%, `Blackbody` is
   re-based onto it**, moving every star's colour slightly once.
   *Alternatives:* keep `Blackbody` as is and let the sky's integral
   disagree with the star it lights (two blackbodies, against plan conflict
   6). *Recommended:* re-base if needed. *Cost to change later:* low now;
   after playtests, star colours are remembered.
4. **Jeans retention: `ExobaseFactor` 4, held above lambda 6, lost below 4,
   a smoothstep between; it shapes the mix and scales the pressure
   ceiling.** *Alternatives:* retention on `EquilibriumK` itself (Earth
   would keep hydrogen); a hard threshold (a cliff in the corpus).
   *Recommended:* as specified. *Cost to change later:* low in code; it
   decides which worlds may have hydrogen skies.
5. **The nadir cap bounds the fact: `MaxNadirTau450` 0.32 (ruling 2; 0.5 as first put up), applied as a
   per-world pressure ceiling, with a legibility test (half the airless
   contrast at nadir under a G star).** *Alternatives:* cap the rendering
   and keep the fact (orbit and ground disagree); a lower cap (thinner,
   clearer worlds; less air variety); a higher one. *Recommended:* bound
   the fact; ruled at 0.32, the value that holds the legibility test as
   written. *Cost to change later:* medium: it moves pressures
   across the corpus.
6. **Aerosol and absorber follow the mix (decision 5's table); N2/O2
   carries ozone.** *Alternatives:* a third draw now; no aerosol or ozone
   at all (Rayleigh only: bluer twilight zeniths lost, Mars-like dust
   skies lost). *Recommended:* the table. *Cost to change later:* low.
7. **Surface temperature stays `EquilibriumK`** for the scale height and
   retention: no greenhouse. *Alternatives:* a derived greenhouse term by
   mix and pressure (a third derived fact, and the kinds are derived from
   `EquilibriumK`, so it must not feed back into them). *Recommended:* none
   in this sub-project. *Cost to change later:* low; every scale height
   moves.
8. **The limb is a second, additive proxy per airy world (`M_SkyAir`),
   drawn at its body's k; the first body in depth order stacks on its
   solid radius as today (k = 50 km / altitude, landing's k = 1 at 50 km
   kept), an airy body later in the order on its outer radius; the shell
   is drawn only while its near side is at least 5 km off (altitude a_top
   x 10/9), and the eye's air takes over below; the limb band cannot dim a
   star behind it.** *Alternatives:* stack on the outer radius always
   (k infinite at the shell's top, negative inside it: breaks the
   handover); the body proxy grown to the shell, drawn translucent (it
   must also be opaque: two passes); a screen-space ring. *Recommended:*
   the shell as stated. *Cost to change later:* medium: the stacking rule.
9. **Inside the air, one air at a time through `MPC_Sky` and one shared
   table asset (`T_SkyAirHere`, since an MPC cannot hold a texture),
   applied by a new opaque backdrop (`AirDome`, `M_SkyAirDome`) behind the
   stars, the starfield, the local star, other bodies and the ground; the
   course marker and the dust motes exempt (`AirApplies` 0), so a jump can
   be aimed under any day sky.** *Alternatives:* the shell drawn from
   inside as the sky (cannot dim or redden); a screen-space sky pass
   instead of a backdrop mesh (a new render hook, outside the sky's
   isolation rules); the table fanned out to every instance (more owners,
   an instance material for the starfield); the course marker taking the
   air, with the HUD's caret alone for aiming by day. *Recommended:* as
   specified. *Cost to change later:* medium: five materials read it.
10. **Slice 1's interim: until slice 2, below the shell-hide altitude
    (about 83 km over Earth air) but above 50 km, no shell is drawn and the
    sky overhead stays black**; slice 1 is judged from above that
    altitude. *Alternatives:* bring slice 2's backdrop forward before
    landing (b) (the ruling puts ground-sky slices after landing; the
    backdrop is the ground sky's); draw the shell from inside as a stopgap (thrown away in slice
    2). *Recommended:* the interim. *Cost to change later:* none after
    slice 2.
11. **12 samples, Chapman for the sun, and one 32 x 32 multiple-scattering
    table per airy world built in C++ at system load; no per-frame table.**
    *Alternatives:* single scattering only (darker, bluer skies, and the
    twilight wrong); per-frame sky-view tables (dearer, more code).
    *Recommended:* as specified, with the sky-view table as the named
    fallback. *Cost to change later:* low.
12. **The sky takes the ground's own brightness factors (`Brightness`, the
    1.5 disc gain), so sky to ground is honest; `ds.Air.Radiance` 1; the
    targets: the day sky hides every star under flux 28 (about 20 of
    3,000), the zenith's exposed value at most half of `ExposedWhite`
    (1.0, manual exposure's white point), the veil's flux-4 target
    untouched at night.** *Alternatives:* a sky scale independent of the
    ground (the 50 km seam would need its own matching); a different flux threshold. *Recommended:* as specified.
    *Cost to change later:* trivial for the scalar.
13. **The deck's Sun: colour times the transmittance's chroma, intensity
    times its luminance, the eclipse kept as it is and the law's own
    occultation not applied to the deck.** *Alternatives:* the engine's
    atmosphere-sun path (needs SkyAtmosphere); tint only, no dimming.
    *Recommended:* as specified. *Cost to change later:* low.
14. **Entry: `sqrt(rho) x v^8` (the shape of shock-layer radiation),
    x = 1 at Earth's 60 km and 7.5 km/s, Glow 0.5 there, a knee at a
    twentieth, softly saturating; dark below about 3 km/s at sea level and
    along the skim cap under 9 km. Said plainly: the default drive approach
    burns, from the upper air through the 50 km handover, and a skim at the
    skim cap above about 12 km burns. Bounded, not rare: the sheath fills
    each pane's outer quarter and streaks, and the middle half stays a wash
    of at most 0.1 x `ExposedWhite`. One orange-pink for every air; a warm
    pulse of 0.3 in the Sun's colour, its flicker band-limited to 2 Hz and
    10% of intensity, a noise of time that exists only while the glow does
    (a position-keyed flicker cannot be slow at both 7 km/s and 1 c); the
    roar never louder than the idle drone.** *Alternatives:* `v^3`, the
    first draft (heating's shape: 2 km/s at sea level glows at half and
    every low skim burns); glow by speed alone (a thin air burns like a
    thick one); a higher knee, or a speed threshold above the drive's
    handover speed, so the default approach stays dark and only a chosen
    dive burns (less honest: a 10 km/s ship in 50 km of air is a
    fireball); a narrower sheath; colour by mix now. *Recommended:* as
    specified, judged after back-to-back landings in slice 4. *Cost to
    change later:* low.
15. **`FSkyBody::Rim` and its Fresnel term are deleted**, and giants get a
    derived H2/He air: every giant's limb changes in slice 1.
    *Alternatives:* keep the rim for giants until a giant slice.
    *Recommended:* delete. *Cost to change later:* low.
16. **`ds.Sky.Goto ... backlit`** joins `night` and `dusk`. *Alternatives:*
    none needed; a console convenience. *Recommended:* add. *Cost to
    change later:* nil.
17. **The vision gains its art direction for skies** (derived; blue is
    rare). *Alternatives:* leave it implicit in this spec. *Recommended:*
    write it into the vision, where it will be measured. *Cost to change
    later:* a paragraph.
18. **ADR 0009 records the engine's atmosphere as refused**, with the
    reasons, so clouds start from here. *Alternatives:* leave the reasons
    in this spec. *Recommended:* the ADR. *Cost to change later:* nil.
19. **The mix weights: N2/O2 0.55, CO2 0.40, H2/He 0.05**, before retention.
    They set the colour of a large share of the universe: about two airy
    worlds in five wear CO2's pale, dusty sky that eats blue.
    *Alternatives:* an N2/O2 majority (for example 0.75 / 0.20 / 0.05: more
    Rayleigh skies, so the star's colour carries the variety and a G-star
    blue is rarer only by the star); CO2 rarer still (0.85 / 0.10 / 0.05:
    dust skies an event of their own); CO2 the majority (a dusty universe,
    Rayleigh skies the contrast). *Recommended:* 0.55 / 0.40 / 0.05, judged
    on the corpus's `sky_zenith_rgb` spread and the slice 1 swatch.
    *Cost to change later:* low in the ini; after playtests, skies are
    remembered.
20. **Ruling 3's "No `verify_level.py` change" and the three new sky slots
    (`AirMaterial`, `AirDomeMaterial`, `AirHereTable`).** The ruling is
    kept: the engine-atmosphere ban is untouched and, as written, the
    verifier does not change; the slots are checked in C++
    (`FindMaterialProblems` at `BeginPlay`, held by `ShipSkyTest`).
    *Alternatives:* the developer reads the ruling as the atmosphere ban
    only, and `check_sky` gains the three slot checks it has for every
    other sky slot (a level built with a slot empty then fails the
    verifier, not only a warning in play). *Recommended:* allow the three
    slot checks in `check_sky`; the ban is what the ruling protects.
    *Cost to change later:* nil.

## Open questions

- **Oxygen without life.** "N2/O2" as a mix implies oxygen, which on Earth
  is biology. Should the mix split into N2 (no ozone) and N2/O2 (ozone,
  only on inhabited or living worlds), tying the sky's twilight colour to
  life?
- **Colour of cold giants.** Methane absorbs red, which turns Uranus and
  Neptune blue-green. A methane absorber for H2/He below some temperature
  is one line of `AirFacts`; is it wanted before giants are a destination?
- **Thick worlds.** Venus and Titan classes need the kind thresholds
  moved, a cloud deck and an answer to legibility (featureless by day,
  city lights by night, the reveal under the deck on descent). Their own
  sub-project, or part of clouds?
- **Clouds, spin and weather** (ruling 9): their own sub-project. Nothing
  on record forbids clouds from drifting; the anti-chore line is weather
  that closes a landing on a timer.
- **Entry colour by composition**: CO2 glows differently from N2/O2.
- **Sky fill inside**: if a landed ship's black shadows under a blue sky
  read wrong, the sky's decision 5 would have to be reopened, deliberately.
- **The stars through the limb band**: if a star behind a limb ever reads
  wrong, the shell becomes translucent with a second pass.
- **Seas on terrestrial worlds** (landing's open question): a sea surface
  would add glint and a different albedo under the same air.
- **Terrain shadowing of the deck** (landing's open question): with air, a
  ridge between a landed ship and a low sun would shade a gold deck; a
  horizon march in `SunVisibleFraction` is still the route.
