# DeepSpace — The Procedural Generation Foundation

**Date:** 2026-09-25 (revised the same day after review — see *Revision*)
**Status:** Draft — not implemented
**Implements:** ADR 0006 (generation is C++ and runs at startup), ADR 0007
(chunked coordinates and hierarchical seeds), ADR 0008 (shape the prior)
**Governed by:** `docs/vision.md` — *Most worlds are empty*, *How social the
game is* (the seed is world-level), the anti-chore principle
**Feeds:** the sky, navigation and ship-detail sub-projects, which read what
this generates and never generate for themselves

## Context

The ship flies. The universe it flies through is 160 spheres on a golden-angle
spiral and 300 motes of dust from an `FRandomStream` seeded with a date. There
is nowhere to go, because nothing out there exists.

Three ADRs have already said what the universe must be. ADR 0006: generated in
C++, at startup, with exactly one generator. ADR 0007: positions are chunked,
seeds derive down a hierarchy by integer hashing only, and anything stored is a
cache, never an authority. ADR 0008: draw from the distribution that describes
the thing, make invalid states unrepresentable, and bound the tails on purpose.
None of it has code yet. This spec is that code: a small pure library, the
subsystem that wraps it, and the one change on screen that makes it real — the
stars outside the window become the stars that are actually there.

The target is a **playable proof of concept**. The developer wants to fly
somewhere and poke holes in the idea, so the bar is "the loop is real", not
"the galaxy is right". What this deliberately fakes is listed at the end, with
what each fake costs to undo. What it does *not* fake is the part that is
expensive to retrofit: the seed hierarchy, the hash, the stream discipline,
and the seam between the pure library and Unreal. Those are the decisions a
save file, a second player and every later generator will be standing on. The
astrophysics is the opposite: cheap to add later, invisible now, and so mostly
deferred.

It is also the first system in the game where an easy mistake quietly changes
everything. Add one random draw in the wrong place and every planet in the
universe moves. Most of the decisions below exist to make that impossible
rather than merely unlikely.

## Goals

- A deterministic seed hierarchy: root → galaxy → sector → system → body, by
  integer hashing, pinned by known-value tests — the hash, the stream and one
  end-to-end chain, not just the mixing function.
- A random stream and the few samplers this spec actually uses — Poisson,
  log-normal, Beta, categorical, and what they are built from — with
  deliberate, tested truncation.
- The data shapes for a star system: star, planets on circular orbits, names.
- A generator that turns a system id into a system, and a position into the
  systems near it, with nothing stored.
- All of the above in plain C++ with no `UObject`, no `UWorld`, and nothing
  from Unreal beyond containers, strings, and the vector types
  `FUniversePosition` already uses — headlessly testable exactly as
  `FShipPowerState` is.
- A `UUniverseSubsystem` that owns the root seed and the priors, holds no
  state that could disagree with anything, and is the only thing gameplay asks.
- **Priors tuned from an ini, not a rebuild**, and a corpus run that writes
  both statistics and a dozen readable system descriptions in one launch.
- **One visible change:** the counter-frame's distant stars are the real
  neighbouring systems, seen from where the ship really is.

## Non-goals

These must not be designed in passing:

- **Rendering beyond the dots.** A planet growing in the window, a sun that
  lights the hull, star colour, the dome's distance — that is the sky
  sub-project, which replaces this spec's crude starfield step outright.
- **Getting anywhere.** Picking a destination, hyperjump, in-system travel,
  where the ship sits on arrival — the navigation sub-project. This spec puts
  the ship in its start system once, as a marked fake (decision 13).
- **Orbital mechanics.** Eccentricity, inclination, nodes, periapses and Kepler
  are deferred (decision 6), with the design that brings eccentricity back
  already written so it cannot reintroduce crossing orbits.
- **Surfaces.** A body's surface gets its seed derivation here so the hook
  exists. Nothing below the body is generated.
- **The ship's own layout.** Ship generation (ADR 0006's port of
  `floorplan`/`placement`) is a separate, larger job. It will use this
  library's seeds and samplers; it is not part of it.
- **What inhabitants are.** A world can be *marked* inhabited, with a
  population. What that means is `docs/vision.md` open question 4.
- **Persistence.** Nothing is saved. There is nothing to save yet — see
  decision 9.

## Where it lives

New files in `Source/DeepSpace/Universe/` beside `UniversePosition.h`.

| File | Contents | Unreal types |
|---|---|---|
| `GenSeed.h/.cpp` | `GenSeed::Mix`, `Label`, `Derive`, `HashCoord` | none (`uint64`) |
| `GenStream.h/.cpp` | `FGenStream`: the stream and every sampler | none |
| `GenPriors.h` | `FGenPriors`: every tunable number, with code defaults | none |
| `UniverseUnits.h` | `UniverseUnits::CmPerAU`, `CmPerLightYear`, `EarthMassSolar`, … | none |
| `StarSystem.h` | the data: `FSystemId`, `FBodyId`, `FStar`, `FPlanet`, `FStarSystemStub`, `FStarSystem` | `FString`, `TArray`, `FUniversePosition` |
| `StarSystemGenerator.h/.cpp` | `FStarSystemGenerator`: stub → system | as above |
| `GalaxyGenerator.h/.cpp` | `FGalaxyGenerator`: position → sectors → stubs; the start | as above |
| `SystemNames.h/.cpp` | `SystemNames::MakeSystemName`, `MakeGivenName`, `Designation` | `FString` |
| `SystemDescription.h/.cpp` | `SystemDescription::Describe(const FStarSystem&) -> FString` | `FString` |
| `ProcGenPriorsConfig.h/.cpp` | `UProcGenPriorsConfig : UObject`, `Config = Game` — the ini mirror of `FGenPriors` | `UObject` |
| `UniverseSubsystem.h/.cpp` | `UUniverseSubsystem : UWorldSubsystem`, the console commands | everything |

Changed: `Source/DeepSpace/Ship/ShipCounterFrame.cpp` (the `.cpp` only — no
header, no `UPROPERTY`, no Blueprint impact), `Config/DefaultGame.ini`.

Tests in `Source/DeepSpace/Tests/`, in the style of `ShipPowerStateTest.cpp`:
`GenSeedTest.cpp`, `GenStreamTest.cpp`, `StarSystemGenerationTest.cpp`,
`GalaxyGeneratorTest.cpp`, `ProcGenCorpusTest.cpp`, `UniverseSubsystemTest.cpp`.

One pure-Python reader, `Tools/procgen_corpus.py`, which only reads what the
C++ wrote (decision 10).

## The seed

```cpp
namespace GenSeed
{
    /** SplitMix64's output for state Value: the finaliser applied to
     *  Value + 0x9E3779B97F4A7C15. The one mixing function in the project.
     *  (The bare finaliser maps 0 to 0, which is why the gamma is added.) */
    constexpr uint64 Mix(uint64 Value);

    /** FNV-1a 64 over an ASCII literal, at compile time. Names a purpose. */
    constexpr uint64 Label(const char* Text);

    /** A child seed: which parent, for what purpose, which one of them. */
    constexpr uint64 Derive(uint64 Parent, uint64 Purpose, uint64 Index = 0)
    {
        return Mix(Parent ^ Mix(Purpose ^ Mix(Index)));
    }

    /** Signed coordinates folded one axis at a time, exactly:
     *    H = Mix(uint64(X)); H = Mix(H ^ uint64(Y)); H = Mix(H ^ uint64(Z));
     *  int64 -> uint64 is two's complement by definition in C++20, so
     *  negatives are safe. */
    constexpr uint64 HashCoord(const FInt64Vector& Coord);
}
```

The hierarchy, every link of it by `Derive`. **Each stream feeds one
quantity** (decision 2): it is constructed, that quantity is drawn, and it is
thrown away.

```
root (world config)
 └─ galaxy          Derive(Root,   Label("galaxy"))
     └─ sector      Derive(Galaxy, Label("sector"), HashCoord(SectorCoord))
         ├─ count   Derive(Sector, Label("count"))            how many systems
         └─ system  Derive(Sector, Label("system"), Slot)
             ├─ place    Derive(System, Label("place"))      position in sector
             ├─ class    Derive(Derive(System, Label("star")), Label("class"))
             ├─ band     Derive(Derive(System, Label("star")), Label("band"))
             ├─ name     Derive(System, Label("name"))
             ├─ planets  Derive(System, Label("planets"))    the count only
             ├─ spacing i  Derive(System, Label("spacing"), i)   orbit i's radius
             └─ planet i   Derive(System, Label("planet"), i)
                 ├─ mass, kind, phase, inhabited, population, givenname
                 │          Derive(Planet, Label("<that word>"))
                 ├─ orbit   Derive(Planet, Label("orbit"))   RESERVED: shape,
                 │          orientation — see "When eccentricity returns"
                 └─ surface Derive(Planet, Label("surface"))  hook only
```

## Known values

Computed with a reference SplitMix64 and FNV-1a while writing this spec, and
pinned by `DeepSpace.Universe.Seed` and `DeepSpace.Universe.Stream`. Every row
is either the hash, the stream, or the chain from a root to a system — the
three things whose change re-rolls every universe. The stream's first three
outputs from seed 0 are SplitMix64's published reference sequence, so that row
is checked against the outside world, not only against ourselves.

| Expression | Value |
|---|---|
| `Mix(0)` | `0xE220A8397B1DCDAF` |
| `Mix(1)` | `0x910A2DEC89025CC1` |
| `Label("star")` | `0xAEFD58191D95E091` |
| `Derive(1, Label("system"), 0)` | `0x07389B5FDEDF9306` |
| `HashCoord((1,0,0))` | `0xB18A02F46D8D86C3` |
| `HashCoord((0,1,0))` | `0x44E5B98100C67FB0` |
| `HashCoord((-1,0,0))` | `0xFC042709560421DA` |
| `HashCoord((0,0,0))` | `0x238275BC38FCBE91` |
| `FGenStream(0)`: first three `NextU64()` | `0xE220A8397B1DCDAF`, `0x6E789E6AA1B965F4`, `0x06C45D188009454F` |
| `FGenStream(0)`: first `Unit()` | `0.88331080821364261` (`(0xE220A8397B1DCDAF >> 11) * 2^-53`, exact) |
| root `20260925` → galaxy | `0x499B04106B52A25F` |
| … → sector `(0,0,0)` | `0xABDDDC1BF5D4C7FB` |
| … sector `(0,0,0)` count, `Poisson(0.5, 8)` | `0` |
| … sector `(1,0,0)` count, `Poisson(0.5, 8)` | `2` |
| … sector `(0,0,0)`, slot 0 system seed | `0x4F6049C72F9DF12D` |

The two counts pin `Poisson` and the `count` stream as well as the hash, and
they use a mean written into the test, not the ini — a prior tune must never
turn a known-value test red. Prior changes stay unpinned on purpose (Risks);
hash and stream changes may not.

If one of those changes, every universe has changed. The test is there to make
that a decision somebody takes, not an accident somebody commits.

## The stream and the samplers

```cpp
/**
 * A deterministic random stream: SplitMix64, one uint64 of state.
 *   NextU64: State += 0x9E3779B97F4A7C15; return finaliser(State);
 * so NextU64() from state S is exactly GenSeed::Mix(S).
 *
 * Every sampler lives here, written out by hand, because the standard
 * library's distributions are implementation-defined -- libstdc++ and MSVC
 * give different normal draws from the same engine -- and a universe that
 * depends on which compiler built it is not deterministic.
 *
 * Construct one per *quantity* (GenSeed::Derive with a Label), draw it, and
 * discard the stream. See decision 2.
 */
struct DEEPSPACE_API FGenStream
{
    explicit FGenStream(uint64 Seed);

    uint64 NextU64();
    double Unit();          // [0, 1): (NextU64() >> 11) * 2^-53
    double UnitOpen();      // (0, 1): ((NextU64() >> 11) + 0.5) * 2^-53, safe for log()
    int64  UniformInt(int64 Min, int64 MaxInclusive);   // unbiased (Lemire)
    bool   Chance(double P);                            // Unit() < P

    // -- the shapes this spec uses ------------------------------------------
    /** Knuth, exactly: L = exp(-Mean); k = 0; p = Unit();
     *  while (p > L && k < Max) { ++k; p *= Unit(); } return k;
     *  Asserts Mean <= 30. */
    int32  Poisson(double Mean, int32 Max);
    double Normal(double Mean, double StdDev);          // Box-Muller: UnitOpen, then Unit; no cached pair
    double LogNormal(double Median, double Sigma);
    double LogNormalBounded(double Median, double Sigma, double Min, double Max);
    double Gamma(double Shape);                         // Marsaglia-Tsang
    double Beta(double A, double B);                    // via two Gammas
    int32  Categorical(TConstArrayView<double> Weights);   // one Unit(), cumulative

private:
    uint64 State;
};
```

Things about that list that are decisions, not details:

- **Bounded means truncated, not clamped.** Clamping piles every out-of-range
  draw onto the bound, so a clamped log-normal of planet mass produces a
  suspicious crowd of planets at exactly the maximum. Log-normal has no
  closed-form inverse, so it resamples up to 16 times and clamps only after
  that; with the bounds used here the clamp fires about once in a billion
  draws. That is ADR 0008's "rejection is a floor, not a method" at the scale
  of one number — and because each stream feeds one quantity, however many
  draws it takes touches nothing else.
- **`Poisson` takes a `Max` and refuses a large mean.** Knuth's method is exact
  and costs O(mean) draws; every count in this spec has a mean under ten. A
  mean over 30 is a design error and asserts, rather than silently getting
  slow.
- **`Normal` throws away Box–Muller's second value.** Caching it would make a
  stream's state two values, and a copied stream that has or has not consumed
  the cache is exactly the kind of bug that surfaces as "this planet is
  different on the second visit".
- **No sampler exists until something uses it.** The first draft carried
  exponential and Weibull (and their exact truncations) for inclinations and
  events; nothing in the revised spec draws either, so neither is written.
  They are twenty lines each when the first real use arrives.

**Uniform is here, and it is right twice.** Where a star sits inside its
sector, and where a planet is along its orbit, genuinely have no preferred
value — a homogeneous Poisson process places its points uniformly, and an
orbit at an unknown epoch has no preferred phase. Those are the only uniform
draws in the generator, and both carry a comment saying why.

## The data

Natural units in the plan; centimetres only at the `FUniversePosition` boundary
(decision 7). Every field is a value; nothing points at anything.

```cpp
/** Which system: the sector it falls in and its slot there. The identity. */
struct FSystemId { FInt64Vector Sector; int32 Slot = 0; };

/** Which body. Moon is -1 until moons exist; the shape has room for them. */
struct FBodyId   { FSystemId System; int32 Planet = 0; int32 Moon = -1; };

enum class EStarClass : uint8 { M, K, G, F, A, B };
enum class EPlanetKind : uint8 { Barren, Terrestrial, Ocean, Ice, GasGiant };

struct FStar
{
    EStarClass Class;
    double MassSolar, RadiusSolar, LuminositySolar, TemperatureK;
    double HabitableInnerAU, HabitableOuterAU, FrostLineAU;   // derived
};

struct FPlanet
{
    FBodyId Id;
    FString Designation;          // "Kessa IV" -- always
    FString GivenName;            // empty unless inhabited
    EPlanetKind Kind;
    double MassEarth, RadiusEarth, EquilibriumK;
    double SemiMajorAxisAU;       // circular and coplanar in the POC -- see Fakes
    double PhaseRad;              // where along the circle; frozen
    double Population = 0.0;      // 0 = nobody
};

/** What a star chart needs, and nothing more. Cheap: a dozen draws. */
struct FStarSystemStub
{
    FSystemId Id;
    uint64 Seed;
    FUniversePosition Position;   // the star's position
    FString Name;
    EStarClass Class;
    double LuminositySolar, TemperatureK;
};

struct FStarSystem
{
    FStarSystemStub Stub;
    FStar Star;
    TArray<FPlanet> Planets;      // in orbit order, innermost first

    /** Star position + a (cos phase, sin phase, 0), through UniverseUnits.
     *  No time argument: nothing moves in the POC. */
    FUniversePosition PlanetPosition(int32 Index) const;
};
```

The first draft's `FOrbit` — eccentricity, inclination, node, periapsis,
epoch, period and a Kepler `OffsetAt` the POC only ever called at *t* = 0 — is
gone. Nothing displayed it. Its return is designed below so that it adds
fields and moves nothing.

## How a system is made

`FStarSystemGenerator::Generate(const FStarSystemStub&, const FGenPriors&) ->
FStarSystem`. Everything below is either sampled from its own stream or
*derived* from what was sampled. The rule (decision 5) is that only free
parameters are drawn; anything physics would determine, physics determines.
Every number marked *(prior)* lives in `FGenPriors` and so in the ini
(decision 14); unmarked numbers are guarantees, and live in code.

**The star** (streams `star/class` and `star/band`, shared with the stub —
decision 4):

- Class: `Categorical` over the solar neighbourhood's main sequence — M 0.76,
  K 0.12, G 0.076, F 0.030, A 0.006, B 0.0013 *(prior)*. Three suns in four
  are dim red dwarfs. That is what the neighbourhood looks like and what *most
  worlds are empty* sounds like. (Decision for the developer: see sign-off.)
- Where in its class: `Beta(1.2, 2.0)` *(prior)*, a bounded proportion skewed
  low because the initial mass function is steep; mass interpolates log-wise
  across the class's band, and temperature follows the same proportion.
- Derived: luminosity from the piecewise mass–luminosity relation (0.23 M^2.3
  below 0.43 M☉, M^4 to 2 M☉, 1.4 M^3.5 above); radius ≈ M^0.8; habitable zone
  0.95–1.37 √L AU; frost line 2.7 √L AU.

**How many planets** (stream `planets`, that and nothing else):
`Poisson(4.0, Max = 12)` *(prior mean)*. A count of things in an interval is
Poisson's whole job; the cap bites once in five thousand systems. Because
every orbit and every planet has its own indexed stream, **a fifth planet
appends; it does not move the first four**, and a test holds that.

**Where each orbit is** (stream `spacing`, index *i*, then `planet` *i* for
the mass it depends on). Orbits are built outward, and **neighbours are spaced
in mutual Hill radii**, the length scale on which two planets disturb each
other:

- Innermost: `LogNormalBounded(0.2 √L, 0.35, max(0.5·median, 5 R★), 2·median)`
  AU *(prior median factor and σ)*. The lower bound is truncation, so no
  planet is drawn inside five stellar radii.
- Each next orbit, *i* ≥ 1:
  1. Draw a spacing `K ~ LogNormalBounded(20, 0.4, 10, 60)` *(prior median and
     σ; the bounds are code)*. Observed compact multi-planet systems sit around
     twenty mutual Hill radii apart; below about ten, systems of small planets
     go unstable within the age of a star, so ten is the floor and is not
     tunable.
  2. Decide the new planet's **region** from the closest it could possibly be:
     `a_lo = a_{i-1} (1 + 5 h₀) / (1 − 5 h₀)`, where `h₀ = (m_{i-1} / 3M★)^⅓`
     — spacing 10 with the new planet massless. If `a_lo` is beyond the frost
     line, the new planet is a beyond-the-frost-line planet.
  3. Draw its mass for that region (below).
  4. With both masses known, `h = ((m_{i-1} + m_i) / 3M★)^⅓` and the mutual
     Hill radius is `h (a_{i-1} + a_i) / 2`. Solving "the gap is K of those"
     for the new orbit gives `a_i = a_{i-1} (1 + K h/2) / (1 − K h/2)`.
  5. That solution needs `K h < 2`. It is kept well inside that by
     **rescaling, not clamping**: K's range [10, 60] is mapped linearly onto
     [10, min(60, 1.5 / h)], so the heaviest pairs get the tightest spacing
     range without any pile-up at a bound. The mass cap below guarantees
     `1.5 / h ≥ 11.9`, so the floor of ten always survives.

  Since the final spacing is never under ten and `h ≥ h₀`, `a_i ≥ a_lo`: a
  planet judged to be beyond the frost line always is. One judged inside may
  land just beyond it — a rocky world past the frost line, which is allowed.

  **Crossing and crowded orbits are unrepresentable.** With circular orbits,
  every neighbouring pair is at least ten mutual Hill radii apart by
  construction, which is ADR 0008 rule 1 honestly this time (decision 15).

**Each planet** (stream `planet`, index *i*, then one sub-stream per
quantity):

- Mass (`mass`): every mass is truncated at **3 × 10⁻³ of the star's mass**
  (about three Jupiters per solar mass — above that it is a brown-dwarf
  companion, which is a binary, which is a fake). Inside the frost line,
  `LogNormalBounded(1.0, 0.9, 0.05, 14.9)` Earth masses *(prior)*. Beyond it,
  a giant with `Chance(min(0.5, 0.3 × M★/M☉))` *(prior)* — giants are rarer
  around small stars — at `LogNormalBounded(100, 1.0, 15, 3000)`, otherwise
  `LogNormalBounded(2.0, 0.9, 0.05, 14.9)`. Mass is a magnitude built from many
  multiplied factors, which is log-normal's definition.
- Equilibrium temperature, derived: 278.6 K × L^¼ / √a.
- **Kind, derived, not drawn.** ≥ 15 M⊕ is a gas giant; under 0.3 M⊕ or hotter
  than 320 K is barren; colder than 180 K is ice; in between is temperate, and
  only *there* does a draw (`kind`) pick Ocean (0.4, *prior*) or Terrestrial.
  An ocean world at 800 K cannot be expressed. (Decision 5.)
- Radius, derived and crude: M^0.28 R⊕ below 15 M⊕; a flat 11 R⊕ above, since
  giants are all roughly Jupiter-sized. The published three-regime fit is
  deferred with the rest of the astrophysics.
- Phase (`phase`): uniform on [0, 2π). No preferred value.
- Inhabited (`inhabited`): temperate worlds only, `Chance(InhabitedChance)`
  *(prior)*. Population (`population`), if so: `LogNormalBounded(5e4, 2.0, 200,
  5e8)` *(prior)* — the settlement-size example ADR 0008 uses, truncated
  because a log-normal will eventually produce a settlement of forty billion.
  This stays in a minimal POC because it is three lines and it is the only
  thing that makes a system an *event*.

A scratch prototype of exactly these priors and stream labels (20,000 systems,
run while revising this spec, not committed — decision 10) gave per system:
2.85 barren, 0.48 terrestrial, 0.32 ocean, 0.35 ice and **0.011 gas giants**;
49% of systems have at least one temperate world; 2% have no planets; with
`InhabitedChance = 0.08` about **6% of systems have somebody in them**.
Neighbouring orbits sit a median 1.58× apart (10th percentile 1.27, range
1.06–7.0). 95% of outermost orbits are under 1 AU, median 0.08 AU, because
most stars are red dwarfs with compact systems. The first draft's 0.16 giants
per system came from Jupiters around 0.1 M☉ stars at spacings that would have
thrown them out of the system; the honest number is rare enough to be the
first tuning question (Risks). The corpus is where these are re-measured for
real.

**Names** (stream `name`, then per-planet `givenname`):

- A system gets a pronounceable name from `SystemNames::MakeSystemName`: two or
  three syllables from fixed onset/nucleus/coda tables (about 20 × 8 × 10),
  weighted towards two, with a short list of forbidden adjacent pairs so it
  stays sayable.
- A planet gets a **designation**, always: the system's name and its orbit as a
  Roman numeral — `Kessa IV`.
- Only an inhabited world gets a **given name**, from the same tables under a
  different stream. Decision 8.

### When eccentricity returns

Not built now; written down so that whoever builds it cannot bring crossing
orbits back. Eccentricity is drawn **after** all the spacing, from each
planet's reserved `orbit` stream (sub-label `shape`), so it moves no radius.
Each gap's slack is what remains after a hard margin of 2√3 mutual Hill radii
(the two-planet stability boundary): `slack = (a_{i+1} − a_i) − 2√3 R_H`, and
each side may spend half of it. So `e_i` is `Beta(0.867, 3.03)` — Kipping's
fit — truncated to `min(0.6, slack_below / 2a_i, slack_above / 2a_i)`, which
makes `periapsis(i+1) − apoapsis(i) ≥ 2√3 R_H` true by construction. The
invariant test changes from "gap ≥ 10 R_H" to exactly that inequality, and a
second test holds that changing the eccentricity prior leaves every
semi-major axis bit-identical. Inclination, node and periapsis argument go
under `orbit/orientation` the same way.

## How systems are found

`FGalaxyGenerator` holds only the galaxy seed and an `FGenPriors`, and is
`const` throughout.

```cpp
struct DEEPSPACE_API FGalaxyGenerator
{
    /** Sector edge, in chunks. 2^18 chunks = 2^62 cm, about 4.9 light years. */
    static constexpr int32 SectorShift = 18;

    FGalaxyGenerator(uint64 RootSeed, const FGenPriors& Priors);

    static FInt64Vector SectorOf(const FUniversePosition& Position);   // floor, not truncate
    TArray<FStarSystemStub> GenerateSector(const FInt64Vector& Sector) const;
    TOptional<FStarSystemStub> GenerateStub(const FSystemId& Id) const;
    TArray<FStarSystemStub> FindSystemsWithin(const FUniversePosition& Centre, double RadiusCm) const;
    /** The nearest system within RadiusCm of Where, if any. "Which system am
     *  I in" is this question, asked with the ship's position. */
    TOptional<FStarSystemStub> FindSystemAt(const FUniversePosition& Where, double RadiusCm) const;
    FStarSystem GenerateSystem(const FStarSystemStub& Stub) const;

    /** The nearest system to the universe origin with at least one planet. */
    FSystemId StartSystem() const;
    /** POC fake (decision 13): the start star + 1.5 x its outermost orbit
     *  along universe +X, in the plane of the planets. Navigation replaces it. */
    FUniversePosition StartPosition() const;
};
```

A sector's system count is `Poisson(SystemsPerSector, 8)` *(prior mean 0.5,
the solar neighbourhood: nearest neighbours ~3.4 ly apart, ~18 systems within
10 ly)* from the sector's `count` stream. Each system's position comes from its
**own** `place` stream — a uniform chunk within the sector plus a uniform
offset within the chunk, built in integers and the existing
`FUniversePosition` so nothing ever passes through a huge double. (The first
draft drew the count and then every slot's position from one sector stream, so
a change to the density moved every star in the sector. That is the trap of
decision 2 again, and it is gone.)

`FindSystemsWithin` enumerates the cube of sectors the sphere touches and
filters by `FUniversePosition::DistanceTo`. For a 10 ly radius that is at most
7³ = 343 sectors and ~170 stubs: microseconds. `StartSystem` searches a growing
cube of sectors around the origin and stops once the best found is nearer than
any unsearched sector could be; only the `planets` count is drawn per
candidate, not the whole system.

`SectorOf` is the classic trap: sector index is `Chunk >> SectorShift`, which
floors for negatives in C++20; a `/` would truncate towards zero and put chunk
−1 in sector 0. The test crosses the origin on every axis.

## Nothing is stored

| What | When | Where it is kept |
|---|---|---|
| Root seed, priors, galaxy generator | Subsystem `Initialize` | members |
| Everything else | when asked | nowhere — generated and returned by value |

The first draft had a start system, a sector cache, a system cache, a stored
`CurrentSystemSeed`, and a refresh "when the ship crosses a sector boundary".
All of it is gone (decisions 12 and 13). Generation costs microseconds, a
query returns a value the caller owns, and "which system am I in" is a
question about the ship's position, so there is nothing that can go stale,
dangle, or disagree with the ship. A cache, if profiling ever asks for one,
goes behind the same value-returning API and changes no caller.

## The seam

**The pure library** is `GenSeed`, `FGenStream`, `FGenPriors`, the data types,
`FStarSystemGenerator`, `FGalaxyGenerator`, `SystemNames` and
`SystemDescription`. It is a pure function of *(root seed, priors, id)*. It
does not log, read config, know the time, or know the ship exists.
`SystemDescription::Describe` returns text; whoever calls it decides where the
text goes. Everything about it can be tested with no world.

**`UUniverseSubsystem`** is everything that touches Unreal:

```cpp
UCLASS(Config = Game)
class DEEPSPACE_API UUniverseSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    static UUniverseSubsystem* Get(const UObject* WorldContext);

    /** Reads UniverseSeed from config, then -UniverseSeed= from the command
     *  line with FParse::Value (UPROPERTY(Config) does not read the command
     *  line by itself), and the priors from GetDefault<UProcGenPriorsConfig>(). */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** POC fake (decision 13): puts the ship at GalaxyGenerator::StartPosition()
     *  once, through UShipSubsystem::PlaceShip. Game worlds only. */
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    uint64 GetRootSeed() const;
    FGenPriors GetPriors() const;

    // Every query works from Initialize on -- no begin-play needed -- and
    // returns a value. Nothing here hands out a reference.
    TOptional<FStarSystem> GetSystemAt(const FUniversePosition& Where) const;   // within InSystemRadius
    TOptional<FStarSystem> GetSystem(const FSystemId& Id) const;                // empty if no such slot
    TArray<FStarSystemStub> GetSystemsNear(const FUniversePosition& Where, double RadiusCm) const;
    FSystemId GetStartSystem() const;

    /** Crude blackbody. The library keeps Kelvin; colour is presentation and
     *  belongs on this side of the seam. */
    static FLinearColor StarColour(double TemperatureK);

    /** "In a system" means within this of its star. 0.25 ly: a hundred times
     *  the widest planetary orbit, a tenth of the nearest-neighbour distance. */
    static constexpr double InSystemRadiusLy = 0.25;

private:
    /** World-level, per docs/vision.md: shareable, and a bug report is a seed. */
    UPROPERTY(Config)
    int64 UniverseSeed = 20260925;

    UPROPERTY(Config)
    bool bPlaceShipAtStart = true;

    TOptional<FGalaxyGenerator> Galaxy;
};
```

`UProcGenPriorsConfig` is a plain `UCLASS(Config = Game)` `UObject` with one
`UPROPERTY(Config) double` per *(prior)* above, each initialised from
`FGenPriors{}` so the code defaults have one source, and a `ToPriors()` that
copies them into the pure struct. The ini section is
`[/Script/DeepSpace.ProcGenPriorsConfig]` and need list only what is being
tuned. Guarantees — the spacing floor of ten, the mass-ratio cap, the kind
thresholds — are deliberately not in it: an ini edit must not be able to
break an invariant.

Console commands, registered with `FAutoConsoleCommandWithWorldAndArgs` in the
`.cpp` (so they cost no header change), for poking holes in play or in the
plain editor — none needs begin-play:

- `ds.Universe.Describe` — the system at the ship's position if it is in one;
  otherwise the start system. Star, every planet's designation, kind, orbit,
  temperature, and who lives there.
- `ds.Universe.Describe <seed>` — the start system of another universe, built
  from a temporary `FGalaxyGenerator`; `ds.Universe.Describe <seed> <x> <y> <z>
  <slot>` — any system of it.
- `ds.Universe.Near <ly>` — the stubs within that range of the ship, nearest
  first.
- `ds.Universe.ReloadPriors` — re-reads the ini section and rebuilds the
  generator, so a prior can be tuned inside one running session. Safe because
  nothing ever held a reference to the old one.

Consumers *ask* this subsystem and never keep a copy, the discipline ADR 0003
set for ship state. `UShipSubsystem` does not learn anything about the
universe; the one dependency runs the other way, is a single `PlaceShip` call
at begin-play, and is marked for deletion when navigation lands.

## The one thing on screen

`AShipCounterFrame::RebuildStarfield` stops drawing the golden-angle spiral.
Instead it asks `UUniverseSubsystem::GetSystemsNear(ship position, 30 ly)` and
places the `DistantStarCount` nearest (about 480 exist in that sphere at this
density) on the existing `DistantStarRadius` shell at their **true direction**
from the ship, scale `DistantStarScale × clamp((L / d²)^¼ normalised, 0.3, 3)`
so that bright near stars read bigger. Because the counter-frame's local axes
are universe axes — the near field already maps them with `WorldToUniverse` —
the stars need no rotation of their own. Directions are computed once at
`BeginPlay`, which is honest: 200 m/s does not change a direction to a star
light years away in a human lifetime.

If there is no universe subsystem, it falls back to the spiral, so
`ShipCounterFrameTest` (16 stars, all on the shell, rotation-only motion) is
unchanged. `StarSeed` keeps seeding the near-field dust. It is a `.cpp`-only
change: no new property, nothing a Blueprint inherits.

This is a stepping stone, not the sky. The sky sub-project moves the dome,
adds colour through per-instance data and a neighbour layer that updates on
arrival, and deletes this code; what it inherits is that the stars are
already the real ones.

## Testing

All but one of these run with no world.

**`DeepSpace.Universe.Seed`** — the hash rows of the known-value table,
exactly; `Label` evaluated at compile time (`static_assert`); the chain from
root `20260925` to sector `(0,0,0)` and slot 0.

**`DeepSpace.Universe.Stream`** — the stream rows of the table, exactly (first
three `NextU64`, first `Unit`, the two sector counts); for each sampler,
100,000 draws land within tolerance of the analytic mean and variance (Beta
against a/(a+b), Poisson mean ≈ variance, log-normal median); every bounded
sampler respects its bounds over 100,000 draws; the truncated log-normal's mean
matches the *truncated* distribution's analytic mean (via `erf`) and fewer than
1 in 10⁴ draws sit exactly on a bound — the test that fails if someone swaps
truncation for a clamp.

**`DeepSpace.Universe.SystemGeneration`** — over 5,000 system ids, run under
both `FGenPriors{}` and the ini's priors, so a tune that breaks an invariant
fails:

- generating twice is field-for-field identical; the system's star agrees with
  its stub;
- **every neighbouring pair is at least ten mutual Hill radii apart**, measured
  from the output as `(a_{i+1} − a_i) / (h (a_i + a_{i+1}) / 2)` — the property
  itself, not the parameter that was meant to produce it — and no planet
  exceeds the mass-ratio cap;
- no NaN; every kind consistent with its temperature and mass; a given name
  only where there is a population;
- **independence:** changing the ocean fraction, `InhabitedChance`, the
  population prior or the planet-count mean leaves every surviving planet's
  semi-major axis and mass bit-identical; a system generated with the count
  forced one higher has the same first *n* planets. A system's *name* is
  identical whether or not its planets were generated.

Changing the *mass* prior does move outer orbits, and is meant to: spacing is
in Hill radii, so an orbit genuinely depends on its neighbours' masses. That is
physics, not stream coupling, and the test does not pretend otherwise.

**`DeepSpace.Universe.Galaxy`** — `SectorOf` across the origin on every axis;
every stub lies inside its own sector; `FindSystemsWithin` across a sector
boundary finds a system planted on the far side; generating sectors in
different orders gives identical stubs; changing `SystemsPerSector` leaves the
position of every slot that still exists unchanged; `StartSystem` is
deterministic, has a planet, and nothing with a planet is nearer the origin;
`FindSystemAt(StartPosition(), 0.25 ly)` is the start system.

**`DeepSpace.Universe.Corpus`** — generates 10,000 systems under the ini's
priors and seed and writes `Saved/procgen_corpus.tsv`, one row per planet,
**and `Saved/procgen_describe.txt`**: `Describe` text for the start systems of
seeds 1–12 and twelve other systems of the configured seed. Always passes
unless something is NaN. Its job is to be read: `python3
Tools/procgen_corpus.py` prints histograms (star classes, planets per system,
kinds, orbit radii, masses, neighbour ratios, how many systems anyone lives
in); the describe file is read as text. The Python functions are
`load_corpus(path)`, `histogram(values, bins, log=False)` and `report(rows)`;
changing what the report shows needs no rebuild.

**`DeepSpace.Universe.Subsystem`** — needs a world, like `ShipPilotTest.cpp`:
the subsystem exists and answers `GetStartSystem` before begin-play; after
`World->BeginPlay()` the ship's position is `StartPosition()` and
`GetSystemAt(ship position)` is the start system; two worlds with the same
seed agree; `-UniverseSeed=` parsing is exercised through the same
`ResolveSeed(const TCHAR* CommandLine)` helper `Initialize` uses; a counter-
frame spawned before begin-play has its first distant star in the direction of
the nearest stub.

**What needs eyes.** Two things, both cheap now. Read `procgen_describe.txt`
from one corpus run and ask whether the systems read as *places* or as
*rolls*. Then play in editor, look out of the window, and ask whether the sky
reads as a neighbourhood. Statistics can show a prior is plausible; only
reading and looking shows whether it is interesting enough to fly to.

## Decisions

### 1. SplitMix64 for hashing and for streams, FNV-1a for labels

**Rejected:** `FRandomStream`, which the counter-frame uses today — 32 bits of
state, and an engine type whose implementation Epic may change under us; the
universe's determinism must not be downstream of an engine upgrade.
`std::mt19937` with `<random>` distributions — the engine is portable but the
distributions are not, so the same seed gives different planets on different
standard libraries. PCG or xoshiro — statistically stronger, but two algorithms
to pin instead of one, and every stream here is short and per-quantity, which
is exactly where SplitMix64 is adequate.

**Chosen because** one twenty-line algorithm, with a published reference
sequence, does both jobs, and the known-value tests pin it completely — the
finaliser, the stream step, the bit extraction, and the coordinate fold.

### 2. One stream per quantity, derived by label and index

Every quantity draws from its own stream — `Derive(System, Label("spacing"),
i)`, `Derive(Planet, Label("mass"))` — which is constructed, used for that one
quantity, and discarded. Draws that together make one quantity (a position's
six components, a name's syllables) share its stream.

**Rejected:** a single stream per system. It is simpler and it is a trap: add
one draw for moons and every name, orbit and planet generated after that point
changes, everywhere, silently.

**Rejected (the first draft):** one stream per *field group* — an `orbits`
stream consumed in order for the count, every spacing ratio, every
eccentricity, inclination, node, periapsis and phase. It argued that
variable-draw samplers were safe because "the damage stays inside their own
purpose", but the purpose was every orbital element of every planet: a change
to the eccentricity prior, or to how many times a bounded log-normal
resampled, moved every outer planet in every system. The same mistake sat in
the sector stream, which drew the count and then every slot's position. A
group is too big; a quantity is the unit at which a variable number of draws
cannot leak.

### 3. Systems are a Poisson process on a sector grid; identity is (sector, slot)

**Rejected:** hashing every chunk for "is there a star here" — a sector holds
2^54 chunks, and a 10 ly query would enumerate more chunks than there are
milliseconds in a century. A generated-once star catalogue — a stored map is a
second definition of the universe, which ADR 0007 forbids.

**Chosen because** it is the right model as well as the cheap one: stars in a
neighbourhood *are*, to first order, a homogeneous Poisson point process, so
Poisson counts per cell and uniform positions within it are the distributions
that describe the thing. The system's seed derives from its position in the
only sense a continuous position can seed anything — through the discrete cell
it falls in.

### 4. Two levels of detail, and the stub is a strict prefix of the system

A stub (position, name, class, luminosity) is what a star chart and a sky need.
The full system adds the star's derived zones and the planets. The full system
is generated *from* the stub's seed through the same `star` and `name` streams,
so they cannot disagree, and a test holds them to it.

**Rejected:** generating full systems for every stub. Today that is honestly
cheap — microseconds each — so this is not about cost now. It is that full
systems will grow (moons, belts, stations, surfaces), and a chart that
generated all of that to draw a dot would become a problem that is hard to
unpick once everything reads the fat type.

### 5. Draw the free parameters; derive everything physics derives

Class, position-in-band, orbit spacing, mass and phase are drawn. Luminosity,
radius, temperature, the habitable zone, the frost line, each orbit's radius
from its spacing, and above all the planet's **kind** — are computed.

**Rejected:** drawing kind from weights. It is the obvious way and it produces
ice worlds skimming their star and oceans past the frost line, which a
plausibility filter would then have to catch. That is ADR 0008's failure mode
exactly; deriving kind makes the absurd case unrepresentable.

### 6. Real priors where they cost nothing; the astrophysics deferred

Star-class weights, planet counts, spacing in Hill radii and a stellar density
of 0.5 systems per 4.9-ly sector are real, because they are one number each
and they shape what the developer sees. Eccentricity, inclination, nodes,
periapses, Kepler motion and the published mass–radius fit are deferred,
because nothing in the POC displays them.

**Rejected (the first draft):** the full orbital set now, with Kipping's
eccentricity Beta and Chen & Kipping's three radius regimes. It was a lot of
machinery for data no system displayed, and it was where the crossing-orbits
bug lived. **Rejected:** flattened priors for variety — more yellow suns, more
giants, more temperate worlds. It would make the first hour prettier and it
would trade away the thing `docs/vision.md` says the game is: *most worlds are
empty*, and an interesting world means something because it is rare. If the
POC reads as drab, the lever is the sign-off decision below, pulled
deliberately — and it is now an ini line.

### 7. Natural units in the plan; centimetres only in `FUniversePosition`

AU, solar and Earth masses and radii, Kelvin, seconds. **Rejected:** everything
in centimetres, matching Unreal. A planet's semi-major axis as
`1.495978707e13` is unreadable in a test, in the corpus report, and in a log;
the priors are written in natural units in every source they come from. The
conversion happens once, in `PlanetPosition` and the stub's position, through
`UniverseUnits`.

### 8. Planets have designations; only inhabited worlds have names

**Rejected:** a pronounceable name for every planet. A name is a claim that
somebody cared about a place. A universe where every rock is called something
lovely is one where every world feels like somewhere, and *most worlds are
empty* is a promise that they do not. `Kessa IV` is indifferent in exactly the
way the universe is supposed to be; a world with a given name is a sign, visible
on a chart before you arrive, that people are there. It costs nothing and it is
content.

Names can collide across the galaxy. That is left alone: two Kessas several
hundred light years apart is what real naming looks like.

### 9. A separate `UUniverseSubsystem`, world-scoped, seed from config

**Rejected:** folding it into `UShipSubsystem`, which is ship state and knows
nothing of the world outside the hull — that separation is ADR 0003's whole
point. A `UGameInstanceSubsystem`, which would outlive a map change; there are
no map changes and never will be (no loading screens), and world scope gives
every automation test a fresh universe for free.

The seed is `UPROPERTY(Config)` so it lives in `Config/DefaultGame.ini`, and
`Initialize` parses `-UniverseSeed=` itself with `FParse::Value` — config
properties do not read the command line on their own. World-level, shareable,
and a bug report is a number. Nothing is persisted, because nothing the player
can change about the universe exists yet; the first thing that does (a
discovered system, a chart note) is stored as a *delta keyed by id*, never as
generated data.

A plain `UWorldSubsystem`, not a tickable one: nothing here happens per frame
(decision 12).

### 10. No Python generator; Python only reads the C++ corpus

**Rejected:** a Python mirror of the generator for fast prior tuning, which is
what the ship-layout probe did. ADR 0006 is explicit that there must be exactly
one generator, and a Python twin used for tuning would be tuned while the C++
drifted. The scratch prototypes used to pick and check the numbers in this
spec were deliberately not committed. Fast tuning comes from decision 14
instead.

### 11. Sampling uses `<cmath>`, and is deterministic on one platform only

ADR 0007 bans floating point from *seed derivation*, and it is banned there.
Sampling necessarily uses `log`, `exp`, `pow`, `cos`, whose last bit can differ
between math libraries.

**Rejected:** fixed-point or table-driven samplers, which would make generation
bit-identical everywhere at the cost of a week and a much harder library to
read. The game builds on Linux with one toolchain; the risk is real only when a
second platform or a second player on a different OS arrives, and the cheapest
mitigation then is structural — make the discrete decisions (counts, classes,
kinds) robust to the last bit — not a rewrite now. Recorded so that it is a
known debt rather than a discovered one.

### 12. Queries return values; nothing is cached

**Rejected (the first draft):** `const FStarSystem& GetSystem(Id)` and
`GetCurrentSystem()` returning references into a `TMap` that `GetSystem`
inserts into. Inserting can reallocate the map, so
`const auto& Here = U->GetCurrentSystem(); const auto& There = U->GetSystem(T);`
leaves `Here` dangling — no crash, just wrong planets somewhere unrelated,
which is this project's most expensive kind of bug. And "consumers never keep
a copy" pushed callers towards holding exactly those references.
**Rejected:** a cache of `TSharedRef<const FStarSystem>`. Safe, but shared
ownership lets any consumer hold a system forever, which is keeping a copy by
another name, and the cache it protects is not needed.

**Chosen because** a system is a few hundred bytes generated in microseconds,
and a value cannot dangle. With nothing cached there is also nothing to
refresh — the first draft's "again when the ship crosses a sector boundary"
had no mechanism (a plain `UWorldSubsystem` does not tick, and watching the
ship would couple the universe to it) and no case: at 200 m/s a 4.9 ly sector
takes about seven million years to cross. Only a jump crosses one, and a jump
is navigation asking a fresh question.

### 13. "Which system am I in" is asked of the ship's position, never stored

**Rejected (the first draft):** a stored `CurrentSystemSeed`, set at begin-play
to the start system and changed by navigation's `SetCurrentSystem`. The ship's
own `FUniversePosition` is the other authority, nothing kept the two in
agreement, and nothing placed the ship: the start system sits typically ~3 ly
from the origin while the ship starts at the origin, so `Describe` would have
reported a "current system" light years away. Two authorities is exactly what
ADR 0003 and ADR 0007 forbid.

**Chosen:** `GetSystemAt(Where)` is the nearest system within 0.25 ly of a
position, and the caller passes the ship's. To make that true at the start,
`OnWorldBeginPlay` calls `UShipSubsystem::PlaceShip(StartPosition(),
Identity)` once, in game worlds, when `bPlaceShipAtStart`. **This is a POC
fake.** Placing the ship is navigation's job, and when navigation lands its
own start placement, this call is deleted so that placement has one owner.
Subsystems' `OnWorldBeginPlay` runs before any actor's `BeginPlay`, so the
counter-frame already sees the placed ship.

**Rejected:** doing the placement in `ADeepSpaceGameMode::StartPlay`, which
keeps both subsystems ignorant of each other. It is the tidier owner, but it is
a header change to a class with a Blueprint subclass, and headless tests do
not get a game mode, so the one claim that matters would be untested.

### 14. Priors live in the ini; guarantees live in code

Every *(prior)* number is a field of the pure `FGenPriors`, mirrored by
`UProcGenPriorsConfig` in `DefaultGame.ini`. A tune is an ini edit and one
corpus run — no compile — or, inside a running session,
`ds.Universe.ReloadPriors` and `ds.Universe.Describe`.

**Rejected (the first draft):** constants in the generator, where every prior
change was `./build.sh` with the editor closed, then a full automation run,
and the one eyeball check was "a dozen launches with `-UniverseSeed=`", each
of which also needed play-in-editor because begin-play was the only way to get
a current system. For a POC whose purpose is poking holes, that was the
slowest possible loop. **Rejected:** a `USTRUCT` of priors used directly by
the library, which would put generated reflection code inside the pure layer.

### 15. Orbits are spaced in mutual Hill radii, with a floor of ten

**Rejected (the first draft):** each next orbit at the previous × a log-normal
ratio floored at 1.3, claimed as "Hill stability by construction". It was not.
With eccentricity drawn independently, the inner apoapsis passes the outer
periapsis at ratio 1.3 whenever *e* > 0.3, which Kipping's Beta draws often;
and with giants up to 3000 M⊕ beyond the frost lines of 0.1 M☉ stars, a mutual
Hill radius is about 0.3 *a*, so stability wanted ratios near 2.7 or more. The
test that was meant to guard it, "every ratio ≥ 1.3", passed on crossing
orbits. That broke ADR 0008 rule 1 while claiming to follow it.
**Rejected:** keeping eccentricity and truncating it to the spacing, with the
1.3 ratio floor. It fixes crossing and not crowding: a pair of giants round a
red dwarf at 1.3 is still unstable.

**Chosen because** the Hill radius is the scale on which neighbours actually
disturb each other, so spacing in it is the distribution that describes the
thing; the floor makes crowding unrepresentable for every mass; circular
orbits make crossing unrepresentable for now; and the design that brings
eccentricity back keeps it that way. The test measures the property from the
output.

## Implementation outline

Everything that compiles or runs a test needs the editor process and is
**serialised** against all other editor work; nothing here is parallel work.
Steps 1–5 are writing, with no editor. They are built **once**, in step 6 —
each header change is a full rebuild with no Live Coding, so batching them
saves cycles. The editor must be closed for `./build.sh`; tests run through
`UnrealEditor-Cmd` with `-nullrhi`.

1. **Seed, stream, priors.** `GenSeed.h/.cpp`, `GenStream.h/.cpp`,
   `GenPriors.h`, `UniverseUnits.h`; `GenSeedTest.cpp`, `GenStreamTest.cpp`
   with the known-value table. *Editor: no.*
2. **Star systems.** `StarSystem.h`, `StarSystemGenerator.h/.cpp`,
   `SystemNames.h/.cpp`, `SystemDescription.h/.cpp`;
   `StarSystemGenerationTest.cpp`. *Editor: no.*
3. **The galaxy.** `GalaxyGenerator.h/.cpp` including `StartSystem` and
   `StartPosition`; `GalaxyGeneratorTest.cpp`. *Editor: no.*
4. **The subsystem.** `ProcGenPriorsConfig.h/.cpp`,
   `UniverseSubsystem.h/.cpp` with `ResolveSeed`, the start placement and the
   console commands; the `[/Script/DeepSpace.UniverseSubsystem]` and
   `[/Script/DeepSpace.ProcGenPriorsConfig]` sections in
   `Config/DefaultGame.ini`; `UniverseSubsystemTest.cpp`. Before writing the
   placement, grep the tests that call `World->BeginPlay()` (today only
   `ScreenPointerTest.cpp`) for assumptions about the ship's absolute
   position. *Editor: no.*
5. **The real stars.** `ShipCounterFrame.cpp` only, with the spiral kept as the
   no-universe fallback. *Editor: no.*
6. **Build and test.** `./build.sh`, then the `DeepSpace` automation run; fix
   and repeat. *Editor: yes.*
7. **The corpus.** `ProcGenCorpusTest.cpp` (built in step 6),
   `Tools/procgen_corpus.py` written and checked against a hand-made TSV with
   no editor; then one automation run of `DeepSpace.Universe.Corpus` produces
   the TSV and the describe file. *Editor: yes for the run.*
8. **Look.** Open the editor, play, look out of the window, run
   `ds.Universe.Describe` and `ds.Universe.Near 15`. *Editor: yes.*
9. **Tune, by reading.** Edit the ini, rerun the corpus (one launch, no
   compile) or `ds.Universe.ReloadPriors` in a session. *Editor: yes, one
   launch per corpus rerun.*

No level rebuild, no new Blueprint, no `.uasset`. `check_blueprints.py` does
not need to run: no component, `UPROPERTY` or event any Blueprint inherits is
added, removed or renamed — the counter-frame change is in its `.cpp`.

## Fakes, and what they will cost

- **Orbits are circular, coplanar and frozen.** Every planet lies in the
  universe XY plane at a fixed phase. Eccentricity returns by the design
  above, adding fields and moving no radius; inclination the same way; motion
  needs a universe clock and, near a planet, ADR 0005's deferred
  reference-frame handoff.
- **The ship is placed at the start by the universe subsystem.** One
  `PlaceShip` call that navigation deletes. If it is not deleted, the ship has
  two placers and the second one silently wins.
- **The distant stars are white dots on a 120 m shell.** No colour, no
  distance behind planets, directions fixed at begin-play. The sky
  sub-project replaces all of it; the cost of the fake is only that it gets
  deleted.
- **The galaxy is an infinite, homogeneous field.** No disc, no arms, no core,
  no edge. Undoing it makes `SystemsPerSector` a function of sector position,
  which changes the count in every sector — but, since positions have their
  own streams now, moves no surviving star. Free now, because nothing is
  saved; do it before saves, or version the generator.
- **Single main-sequence stars only.** No binaries, white dwarfs or giants.
  `FStarSystem::Star` is one value; binaries turn it into an array and every
  reader of `Star` changes. The mass-ratio cap is where binaries are being
  refused today.
- **Radius is one crude power law and a constant.** Visible only once the sky
  draws planets at size; the published fit is a three-line swap.
- **No moons, rings, belts or stations.** `FBodyId::Moon` exists so the id
  shape does not change when they arrive; decision 2 means adding them moves
  nothing else.
- **"Inhabited" is a flag and a number.** Nobody lives there in any sense the
  player can meet. That is open question 4 and should stay open.
- **No caches.** Harmless at POC scale; if a chart ever asks for thousands of
  full systems per frame, a cache goes behind the value API.

## Risks

- **Real scale meets a 200 m/s ship.** The generator places planets at real
  distances — 0.01 to a few AU apart. At cruise speed one AU is two decades.
  That is navigation's problem to solve with in-system travel, and **the fix
  must not be to shrink the orbits**: planets at Earth scale is non-negotiable
  in `docs/vision.md`. If navigation asks for smaller systems, that is a vision
  change and goes to the developer.
- **Honest priors may read as broken in a POC.** Three stars in four are red
  dwarfs; a typical system is a few barren rocks within a tenth of an AU, and
  gas giants are now about one system in ninety — fewer than the few per cent
  observed, because compact red-dwarf systems rarely reach their frost line.
  Giant chance is the first ini knob to try. The sign-off decisions exist so
  that dullness is a choice.
- **Every prior change re-rolls the universe.** Harmless until something
  persists; then it is a migration. Hash and stream changes fail the
  known-value tests loudly, which is intended; prior changes are caught by
  nothing, which is also intended, and should stop being intended before saves
  exist.
- **This spec and navigation's catalogue overlap.** Navigation's
  `FStarCatalog` (2^19 cells, mean 1.2, `DSC` numbers, a forced class-G home)
  is, by its own ownership clause, not built if procgen provides a galaxy.
  Its forced-G home is a real design choice this spec does not make; see
  sign-off 5.
- **Start placement moves the ship light years from the origin in every game
  world that begins play.** Anything that assumed the ship begins at
  `FUniversePosition` zero will see a different number. The ship is the
  origin of its own world (ADR 0005), so geometry is unaffected; absolute
  position reads are not. Step 4's grep is the mitigation;
  `bPlaceShipAtStart = false` is the escape hatch.
- **Floating-point determinism is single-platform** (decision 11). Watch for
  `-ffast-math` ever being enabled in the module's build rules: it would make
  generation depend on optimisation choices even on Linux.
- **`int64` config properties** are supported by the config system but rarely
  used; if `UPROPERTY(Config) int64` misbehaves, fall back to an `FString`
  parsed with `FCString::Strtoui64`.

## Decisions needing sign-off

1. **Star-class weights: the real neighbourhood (three in four red dwarfs), or
   flattened towards yellow suns for the POC?** Recommended: real. Flattening
   is now an ini edit and cheap to revert, but every playtest judgement made on
   a flattened universe is about a different game.
2. **How common are inhabited worlds?** `InhabitedChance = 0.08` gives about
   one system in sixteen. Recommended for the POC; rarer is more true to *rare
   enough to be an event*, but a POC in which the developer never meets one
   cannot test whether meeting one works.
3. **Designations for planets, given names only for inhabited ones**
   (decision 8). Recommended as specified.
4. **Accept single-platform floating-point determinism** (decision 11) as a
   recorded debt. Recommended: yes, until a second platform exists.
5. **Where the game begins: the nearest system with a planet, or a forced
   Sun-like home** (navigation's proposal)? Recommended: nearest-with-a-planet
   for the POC — it is what the generator honestly makes, and it will usually
   be a red dwarf, which is the register. A forced home is one branch in
   `StartSystem` if the first session feels wrong.
6. **Circular orbits for the POC** (decisions 6 and 15). Recommended: yes;
   eccentricity is designed and deferred, and nothing on screen can show it.

## Revision

Revised the same day after review. Each point and where it landed:

1. *The 1.3 ratio floor was not Hill stability, and its test passed on
   crossing orbits.* Agreed. Spacing is now in mutual Hill radii with a floor
   of ten, masses are capped relative to the star, orbits are circular, and
   the test measures the separation from the output. Eccentricity's return is
   designed so crossing stays unrepresentable. Decision 15.
2. *One `orbits` stream reintroduced the trap decision 2 removes.* Agreed, and
   the sector stream had the same fault. One stream per quantity, per index;
   independence tests added. Decision 2.
3. *References into a `TMap` that reallocates; undefined before begin-play.*
   Agreed. Everything returns by value, nothing is cached, and every query
   works from `Initialize`. Decision 12.
4. *`CurrentSystem` contradicted the ship's position, and nothing placed the
   ship.* Agreed. The current system is asked of a position; the ship is
   placed once at begin-play, marked as a fake navigation deletes. Decision 13.
5. *The sector-crossing refresh had no mechanism and no case.* Agreed, and
   deleted with the caches. Decision 12.
6. *The known values did not pin the hash, the stream or a chain.* Agreed. The
   table now pins `HashCoord`, the stream's first outputs (which match
   SplitMix64's published sequence), `Unit`, and a chain from root to slot.
7. *Tuning and eyeballing cost editor launches.* Agreed. Priors are in the
   ini, `-UniverseSeed=` is parsed explicitly, `Describe` needs no begin-play,
   and one corpus run writes a dozen descriptions. Decision 14.
8. *No visible change, and astrophysics nothing reads.* Agreed. Orbital
   elements, Kepler, two samplers and the published fits are cut and listed
   under Fakes; the counter-frame's distant stars become the real neighbours.
   Decision 6 and *The one thing on screen*.
