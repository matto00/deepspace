# Atmosphere Optics Model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the part of the atmospheres spec that may start before landing slice (a) merges: the pure C++ optics (a brute-force spectral reference and the shipped law in `Shaders/Private/Atmosphere.ush`, compiled into C++ twice), `SkyColour::ThroughFilter`, the ground-sky swatches the developer rules on, and procgen's air (the two draws, their guarantees, the fixture worlds and the corpus). Each piece is held by headless tests. It stops at the boundary where `M_SkyBody` needs landing (a)'s Custom-node route.

**Architecture:**
- **Track O, optics (starts now).** `SkyColour` gains the spectral integral that its one blackbody has so far approximated. `AtmosphereReference` is the slow truth: 16 wavelengths, exact columns by numerical integration, and second-order scattering by brute force. `Atmosphere.ush` is the law the GPU will run. It is written in landing's scalar subset (the `AT_` prefix for landing's `WR_`), in the log domain so that float cannot overflow, and it is compiled into `Atmosphere.cpp` as `AtmosphereF64` and as `AtmosphereF32`, the GPU's mirror. `FAtmosphere::Build` averages the air into eight spectral bins, with the star's light in each bin as its fold back to colour (atmosphere plan rulings 3 and 6), and builds the 32 x 32 multiple-scattering table, a value per bin. `Atmosphere.Full.GroundSkySwatch` writes noon and dusk fisheyes of the ground sky through the shipped law, which is what the red-dwarf ruling is made from; it is a picture for the developer's eyes, not a gate, so it is run by name and not by the default suite. Pure tests hold the law to the reference and float to double.
- **Track G, procgen's air.** `AirFacts` (pure, new files, starts now) holds the derived facts and the Jeans and nadir-haze guarantees. The two draws land on `FPlanet` (landing's track P, which owned every generator file, has merged), with the spec's fixture worlds found by its rules and the corpus's four air facts. After track O has also merged, `PlanetAir` adapts a planet's facts to the optics, `.NadirLegible` holds the guarantee, and a writer outside the default suite gives every temperate world of the corpus its noon sky.
- Nothing in this plan touches `M_SkyBody`, `setup_sky_materials.py`, the material contract, `SkySystem.*`, `SkyProjection.*`, `ShipSky.*`, `DeepSpace.Build.cs` or any `.uasset`.

**Tech Stack:** Unreal Engine 5.8.2 C++ (module `DeepSpace`); a shared HLSL/C++ `.ush` in landing's subset; automation tests (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`) run through `./test.sh`; mutation proofs through `Tools/mutate.sh`; `FImageUtils` (module `Engine`, which already carries `ImageCore` publicly) for the swatch PNGs; Python for the corpus reader (`Tools/procgen_corpus.py`, `Tools/test_procgen_corpus.py`).

**Spec:** /home/matt/Development/deepspace/docs/superpowers/specs/2026-09-27-atmospheres-design.md (approved 2026-09-27, every sign-off item as recommended). Decision numbers below are the spec's. The landing plan it runs beside is /home/matt/Development/deepspace/docs/superpowers/plans/2026-09-27-landing-slice-1.md; the wear plan, which shares one file with this one, is /home/matt/Development/deepspace/docs/superpowers/plans/2026-09-27-wear-slice-1-upgrade-seam.md.

## Global Constraints

- **Ruling 1:** "The pure C++ optics model and its tests may start now." Nothing here rewrites `M_SkyBody`. The Custom-node route is landing slice (a)'s, and this plan stops before it.
- **Ruling 2:** sky colour is derived and honest, from the star's blackbody x the composition's scattering and absorption x the column. **No palette, no floor.**
- **Ruling 3:** our own analytic model everywhere. No engine atmosphere component. **No `verify_level.py` change** in this plan.
- **Ruling 4:** two new draws on new labelled streams, `Label("air.mix")` and `Label("air.pressure")`, from the planet's seed. Kinds unchanged. Barren and Ice stay airless. `DeepSpace.Universe.Seed` and `.Stream` stay untouched. Priors go in the ini with a domain check.
- **Ruling 8:** haze is capped at nadir by a guarantee in code, `GenGuarantees::MaxNadirTau450`, never an ini prior. Decision 4 set it at 0.5, and **atmosphere plan ruling 2 lowers it to 0.32** (Task 12 moves the constant; Task 4 moves the optics fixtures' ceilings to it).
- **This plan's rulings are gates** (*Rulings needed before execution*). A gated task does not start until its ruling is recorded in the spec, so no step commits a known-red test and every "whole suite" step expects green.
- **The reference:** double precision, spectral at 16 wavelengths from 400 to 700 nm, a fine ray march, exact optical depth to the sun by numerical integration, and second order by brute force. It never runs in a frame.
- **The law:** `Shaders/Private/Atmosphere.ush`, in landing's subset:
  - no swizzles, no vector or matrix types, no `mul()`, no implicit vector arithmetic, no `out` parameters;
  - every vector is spelled out a component at a time, and every multi-valued result is a plain struct;
  - every literal is `AT_REAL(...)`, and every symbol is prefixed `AT_`;
  - shims `AT_floor`, `AT_saturate`, `AT_min`, `AT_max` and `AT_sqrt`, mirroring landing's `WR_floor`, `WR_saturate`, `WR_min`, `WR_max` and `WR_sqrt` in `WorldRelief.ush`, with `AT_exp`, `AT_log`, `AT_cos` and `AT_sin` added (landing has no `exp`, `log`, `cos` or `sin` shim; it has `WR_frac` and `WR_step`, which the law does not use);
  - compiled in C++ as `AtmosphereF64` (double) and `AtmosphereF32` (float).
- **Float-safe by construction:** the Chapman function works in the log domain. The impact parameter comes from `|E x D|`, never from `sqrt(|E|^2 - (E.D)^2)`.
- **The law's spectrum:** **eight spectral bins** (atmosphere plan rulings 3 and 6), `AtmosphereBins::First = {0, 2, 3, 5, 6, 8, 10, 11, 16}` over the 16 wavelengths. Each bin's coefficients are the star-weighted mean of its wavelengths', and each bin is folded into linear sRGB by the star's light in it. The entry points still return linear sRGB.
- **Samples:** **12** view nodes, with a span's light integrated exactly for an exponential (logarithmic mean over the Chapman columns), and one more node halfway where single scattering changes by more than e^4 (planning note 14). The sun's optical depth is analytic (Chapman). Multiple scattering is Hillaire's (2020) isotropic approximation, from a **32 x 32** table per airy world: sun zenith, crowded toward the horizon (planning note 15), by altitude, a value per bin. It is built in C++ and stored through half floats: on the GPU a 64 x 32 RGBA16F texture, bins 0-3 left and 4-7 right.
- **The air's top:** `AirTop` is **10** scale heights of the gas.
- **Tolerances:**
  - `.LawMatchesReference`: each channel within **5% relative or 1e-3 absolute**, for both the F64 and the F32 build, everywhere (ruling 3), against the reference with its second scattering isotropic as the law's is (ruling 7).
  - `.MultipleScatteringGap`: the same against the reference's full second order, within **25% or 1e-3** (ruling 7).
  - `.SingleScatteringMatchesReference`: the law without its table against the reference's first order, within 5% or 1e-3. `.BinsCarryTheSpectrum`: the bins' transmittance against the spectrum's, the same. Both run under the home star (2,566 K), procgen's coolest star (2,400 K, the M band's floor in `StarSystemGenerator.cpp`'s `ClassBands`) and the Sun.
  - **As a display shows it.** The agreement tests compare each channel after setting a negative one to zero on both sides (`Shown`): the reference's spectral light can leave sRGB's gamut, where the law's non-negative bins cannot follow. No ruling approved this, so each test counts and prints how many values it changed (planning note 17), and the count goes to the developer with ruling 7.
  - `.MultiScatterTable`: each bin of the table within **10% relative or 1e-3 absolute** of the reference's source averaged into it, at twelve texel centres and at a sun on the horizon. The spec gives no number for the table on its own; this is the plan's, and a table that met it and failed `.LawMatchesReference` would show as the verdict table's TABLE.
  - `.FloatMatchesDouble`: within **1e-3 relative or 1e-5 absolute**, and every F32 output finite, over decision 1's whole grid (every mix at 0.05 bar and at its ceiling), the giant, and the float extremes.
  - `.Chapman`: within **0.5%** to 89 degrees and through the horizon to 30 degrees below it, and finite in F32 throughout.
  - `.OneBlackbody`: within **2%** per channel from 2,000 to 15,000 K.
  - `.HomothetyInvariance`: ruling 5's: the eyes within **1e-12** relative, the law's outputs within **1e-9**.
- **`.StarColour` bounds** (the noon zenith from the ground, straight up under a sun 45 degrees high by ruling 4, Earth air, 1 bar, 1 g), in linear sRGB with HSV `S = 1 - min/max`, as **ruling 1** restates them:
  - S falls from 2,000 K to the greyest sky, which lies between 3,000 and 4,500 K, and rises from it to 15,000 K;
  - under 2,566 K a peach sky, **S within 0.62-0.82 and hue within 15-40 degrees**; under 2,000 K, **S >= 0.85**;
  - from 4,000 K up, hue within 200-240 degrees; under 5,772 K, **hue within 200-235 degrees and S within 0.40-0.85**;
  - no palette, no floor, no white balance per star.
- **Mixes:** mean molecular weight 28.97 (N2/O2), 44.0 (CO2) and 2.3 (H2/He). Rayleigh cross-section relative to air 1.0, 2.4 and 0.2. Earth air's Rayleigh depth is 0.097 per bar at 550 nm.
- **Aerosol and absorber** (decision 5), as optical depth at 550 nm per bar, scale height, Angstrom alpha, asymmetry g, and single-scatter albedo:

  | Mix | Aerosol | Absorber |
  |---|---|---|
  | N2/O2 | 0.05, 1.2 km, 1.0, 0.76, 0.95 | ozone-like, Chappuis band near 600 nm |
  | CO2 | 0.08, 2 km, 0.3, 0.7, 0.85 blue / 0.95 red | none |
  | H2/He | 0.01, 1 km, 1.0, 0.7, 0.99 | none |

- **Retention** (in code, `GenGuarantees` in `GenPriors.h` from Task 10 on): `ExobaseFactor = 4`, `RetainedAbove = 6`, `LostBelow = 4`, and a smoothstep between.
- **Priors:**

  | Prior | Default | Domain |
  |---|---|---|
  | `AirPressureMedianTerrestrialBar` | 0.8 | > 0 |
  | `AirPressureMedianOceanBar` | 1.0 | > 0 |
  | `AirPressureSigma` | 0.9 | (0, 2.5] |
  | `AirMixWeightNitrogenOxygen` | 0.55 | >= 0 |
  | `AirMixWeightCarbonDioxide` | 0.40 | >= 0 |
  | `AirMixWeightHydrogenHelium` | 0.05 | >= 0, and the three sum > 0 |

- **The pressure ceiling is smooth:** `P = 1 / (P_draw^-4 + P_max^-4)^(1/4)`. A giant's disc is where the air above reaches `MaxNadirTau450`.
- **The default suite stays fast.** `./test.sh` with no argument runs every `DeepSpace.*` test, in every worktree, one at a time behind `Tools/ue_lock.sh`: landing's and wear's trees pay for every test this plan adds to it (CLAUDE.md measures the whole suite at about 15 s). A test whose full grid takes more than a few seconds keeps a small grid under `DeepSpace.` and names its full grid `Atmosphere.Full.<Name>`, which the `DeepSpace` filter does not match. The full grids are run explicitly (`./test.sh Atmosphere.Full.<Name>`) in their task and before each merge, as `Eyes.*` is run through `Tools/eyes.sh`. The seconds each new test takes are measured in its task (*Conventions*) and written into its commit message.
  - **The total, not only each test.** Tasks 4-8 add to the default suite about 14 s over start-up, estimated from the harness at `-O2`: `.MultiScatterTable` 4.1 s, `.LawMatchesReference` 3.6 s, `.SingleScatteringMatchesReference` about 2.9 s with the 2,400 K star, `.StarColour`'s 27 noon-only builds about 2.4 s, `.FloatMatchesDouble` about 1.4 s with the thin airs, the rest under a second together. That roughly doubles CLAUDE.md's 15 s, and landing's trees pay it behind the one lock. **The budget is 15 s over start-up for this plan's additions.** Task 8, Step 5 times the whole suite in `air-optics` and on `main` and writes `Measured: the default suite grew by N s.` into Task 8's commit. Over 15 s, stop and report with the per-test times, so that the developer chooses which to move under `Atmosphere.Full.`.
  - **Measured, and brought under (2026-09-28, the orchestrator's decision).** Measured from the log's `Test Started` and `Test Completed` stamps, `DeepSpace.Atmosphere.*` came to 23.5 s, `.GroundSkySwatch` alone 6.03 s, over the 5 s cap. Two changes, and no coverage cut: the swatch, a picture generator rather than a gate, is `Atmosphere.Full.GroundSkySwatch`, run by name; and the tests that build the same fixture air under the same star -- Earth's air under the Sun with a full table (`.MultiScatterTable`, and `AtmosphereLawTest.cpp`'s shared `Earth()`), its noon-only table (`.MultiScatterTable`, `.StarColour`), and the reference airs `.SingleScatteringMatchesReference`, `.LawMatchesReference` and `.MultiScatterTable` share -- ask `Tests/AtmosphereTestCache.h`, which builds each once per run, lazily, read-only, keyed on every input. No tolerance, grid or sample count moved, and each test still builds what it needs when run alone. After both, measured the same way twice: **15.96 s, still 0.96 s over**, no test over the cap (`.LawMatchesReference` 3.07, `.MultiScatterTable` 3.02, `.StarColour` 2.65, `.SingleScatteringMatchesReference` 2.56, `.BacklitRing` 2.52, `.FloatMatchesDouble` 1.29, the rest 0.85 together). What remains is distinct work, each piece now built once -- four full tables (Earth's air and N2/O2 at 0.05 bar under the Sun, CO2 at its ceiling under each star), 43 noon-only tables and the reference's second-order traces -- so no further sharing is left, and which test moves under `Atmosphere.Full.` is the developer's choice. **Settled (2026-09-28):** the budget for this plan's default-suite additions is raised to 16.5 s rather than move a real agreement check out of the default suite; the 5 s per-test cap stands.
- **Project rules:**
  - All logic lives in C++ (ADR 0002). Randomness uses the distribution that describes the thing (log-normal pressure, categorical mix, shaped rather than rejected: ADR 0008).
  - Build and test only through `./build.sh`, `./test.sh` and `Tools/mutate.sh`, behind `Tools/ue_lock.sh`, with the editor closed. Use at most 3-4 workers and `nice -n 19` for any local sweep.
  - Unity is off, so test files use a named local namespace (`<File>Local`), never an anonymous one. Every test path is a sibling with no children.
  - Every test is proven able to fail with `Tools/mutate.sh`. A surviving mutant strengthens the test; it never weakens the mutation.
  - Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
  - Nothing is created, moved or removed outside `/home/matt/Development/deepspace` (the home directory needs the developer's word).

## Review Focus

The spec implies these five input classes, and no test the spec lists exercises any of them. They are the most likely to bite first, and each one's pinning test is in the task that owns the code.

1. **An eye below the datum.** Landing's relief puts valley floors under the sphere the law calls the ground: a world's relief reaches `FWorldReliefParams::PeakCm` (`Source/DeepSpace/Surface/WorldReliefParams.h`) either side of the datum, and landing's `FWorldRelief::MinHeightCm()` (landing plan Task 10 (R3), not yet built) is `-PeakCm`. So a landed ship can have `|E| < 1`. A reasonable person expects the sky from a valley, not a black sky or a NaN. The law lifts such an eye to the surface above it. Pinned by `DeepSpace.Atmosphere.EyeBelowTheDatum` in **Task 5 (O5)**.
2. **No star.** `FSkyFrame::SunDirection` is the zero vector in a system with no star, and every caller will pass it straight through. The expectation: no in-scatter, no sunlight through the air, a transmittance that is still the air's, and an airless world still unchanged. Pinned by `DeepSpace.Atmosphere.NoStar` in **Task 5 (O5)**.
3. **Degenerate rays:**
   - a view straight through the centre (impact parameter exactly 0);
   - a length of zero or less;
   - a length of "to infinity" (`1e30`);
   - an eye a million radii out.

   Each must be finite and mean what it says. Pinned by `DeepSpace.Atmosphere.DegenerateRays` in **Task 5 (O5)**.
4. **Star temperatures outside the blackbody's range** (0 K, 500 K, 40,000 K), and a 1,000 K star whose blue is almost nothing. The bins weigh by the star's light and the fold divides by its own channel, so these must stay finite and non-negative, clamped exactly as `Blackbody` clamps. Pinned by `DeepSpace.Atmosphere.StarTemperatureExtremes` in **Task 5 (O5)**, over the bins' coefficients and folds.
5. **Priors whose mix weights leave a world nothing it can hold.** For example, `AirMixWeightNitrogenOxygen=0` and `AirMixWeightCarbonDioxide=0` pass the domain, since the sum is above 0. Every temperate world that cannot keep hydrogen then has zero total weight, and `FGenStream::Categorical` `check()`s on a zero total, which is a crash. The expectation: that world is airless (`None`, 0 bar), and no draw happens. Pinned inside `DeepSpace.Universe.Air` in **Task 10 (G2)**.

## Rulings needed before execution (gates)

The plan's harness (planning note 12) compiled and ran this plan's pure C++. Three of the spec's stated numbers do not hold for the law the spec specifies, and the plan departs from the spec's words in two more places. **Each row is a gate:** the task it names does not start (or, for Task 7, does not pass its gate step) until the ruling is recorded. Rulings 1-5 were recorded on 2026-09-27. Re-planning Tasks 4-8 for ruling 3 measured two more questions, rows 6 and 7, which gate Tasks 4 and 8.

**How a ruling is recorded.** On the developer's word, the orchestrator adds one paragraph to the spec's *The developer's rulings, 2026-09-27* section, on `main`, beginning exactly `**Atmosphere plan ruling N (<date>):**` followed by the developer's choice, and commits it on `main` (a docs commit; no track owns that section). The gated task's gate step merges `main` into its tree and greps for that line. The task that encodes the ruling then amends the spec's affected sentence to say the same, with the reason, in its own commit: the spec is amended, never the test alone.

| # | The spec says | Measured, or the plan's departure | Gates | The developer's choice (recorded) |
|---|---|---|---|---|
| 1 | `.StarColour`: under 2,566 K a "pale grey-cyan" sky, saturation <= 0.25; <= 0.12 at 2,000 K; saturation rising with temperature (ruling 2) | peach: 0.72 at 2,566 K, 0.96 at 2,000 K; the least saturated sky is near 3,500-4,000 K. The G-star bounds hold (note 11) | Task 7, Step 5. Made **from Task 7's swatches** (Steps 1-4), which show the ground sky under a red dwarf, a G star and a CO2 world through the shipped law, as the spec's `.GroundSkySwatch` intends | Keep the physics: peach, no white balance per star (2026-09-27). Task 7 writes the bounds. |
| 2 | `.NadirLegible`: half the airless contrast at `MaxNadirTau450 = 0.5` (decision 4) | 0.34, and it would pass at tau450 about 0.32 (note 6) | Task 12 (does not start) | `MaxNadirTau450` 0.32 (2026-09-27). Task 12 applies it; Task 4 moves the optics fixtures' ceilings. |
| 3 | `.LawMatchesReference`: three channels within 5% or 1e-3 everywhere (decision 1) | 229 of 3,456 values per build miss. 16 are in the thin airs (up to 21%); the rest are the ceiling airs' long grazing paths, worst in the blue channel's transmittance | Task 8 (does not start) | Spectral bins, four to six, to meet 5% or 1e-3 everywhere (2026-09-27). Tasks 4-8 re-planned; the measured count is row 6's. |
| 4 | "the noon zenith from the ground" (`.StarColour`, decision 3's table, the corpus's sky), read as a sun overhead | straight up under a sun **45 degrees** high (note 9). With the sun overhead the zenith is the sun's own aureole and wears the star's colour: under the Sun, Earth air, saturation 0.15, which fails the spec's own G-star bound (0.40-0.85). At 45 degrees it is 0.60, hue 224. This departure is what makes the G bounds pass | Task 7, Step 5 (with ruling 1) | 45 degrees, `AtmosphereLaw::NoonSun`, everywhere (2026-09-27). |
| 5 | `.HomothetyInvariance`: the air term "bit for bit in double" from the proxy's eye and the true eye | the eyes to 1e-12 relative and the law's outputs to 1e-9 (note 4): the two eyes are one ratio formed by two roundings | Task 8 (with ruling 3) | The tolerances (2026-09-27). Task 8 writes them. |
| 6 | Ruling 3: "four to six spectral bins" | No partition into four, five or six bins meets 5% or 1e-3 on the grid: the best six miss a red dwarf's blue through a long horizontal CO2 path by 4.4 times the allowance. Eight bins, narrow in the blue, meet it: with the code as the plan gives it, worst 0.92 of the allowance in the bins' transmittance alone (`.BinsCarryTheSpectrum`), 0.80 in single scattering and transmittance (`.SingleScatteringMatchesReference`), and 0.80 with the table (`.LawMatchesReference`). Seven meet it by one partition only (note 13). **What eight does not meet:** across stars of 2,000-15,000 K, eight bins miss 3 values per build, all under a 2,000 K star, the worst 1.12 times its allowance. Procgen's coolest star is 2,400 K, which the tests now include but the harness did not measure. The partition was chosen on the grid the tests check, so their margins are in-sample (note 13). **What six would save:** six bins still need two RGBA16F texels per table cell, so the table and its two reads per node are unchanged; the air is 12 float4 instead of 15, and the per-bin arithmetic about a quarter less. The comparisons set negative channels to zero on both sides (note 17) | Task 4 (does not start) | eight bins, as planned; or six, with a tolerance restated for that path (Task 4, Step 1 says what then changes) |
| 7 | `.LawMatchesReference` against the reference's full second order (decision 1) | The law's multiple scattering is Hillaire's isotropic table, as decision 1 chose. Against the reference with its second scattering isotropic too, the law meets 5% or 1e-3 everywhere. Against the full second order, 32 values per build miss, the worst about 19% off: backlit horizons too dark (the haze's forward peak), nadir views from inside ceiling airs too bright (note 16). Every agreement compares each channel as a display shows it, a negative channel set to zero on both sides; each test prints how many values that changed, and the developer rules on that too (note 17) | Task 8 (does not start) | hold the law to the isotropic reference at 5% or 1e-3 and pin the full gap at 25% or 1e-3 (as planned); or another gap bound; or a directional table, a change to decisions 1 and 11 |

Two departures an earlier draft carried are gone. The corpus's noon skies cover all 10,000 systems, as slice 1's done-when says, through a writer outside the default suite (Task 13). And `DeepSpace.Universe.Air` compares every world against the same world generated with the draws off, as the spec's *Tests* say (Task 10).

Everything else in Tasks 1-6, 8, 9 and 12 passed in the harness as written; planning note 12 lists what the harness did not cover. The re-planned Tasks 4-8 were run again, file by file as the plan gives them, in a harness rebuilt from the built code (notes 13-16). Every test passes there, and every mutation named in them was killed there except the two that need the engine (the swatch's file writing, and `SkyProjection`).

## Planning notes (read before executing)

These arose while writing the plan. Each one is argued here once, and each task that it touches refers back to it.

1. **The multiple-scattering table travels as a trailing macro argument (a change to decision 1's fixed interface).**
   - The spec fixes the entry points' form, and it has the hook, `AT_MultiScatter(AT_Air A, AT_REAL Altitude01, AT_REAL CosSunZenith)`, read "the table the Custom node is handed". A Custom node's texture input is a parameter of the function the node generates, though, not a global. Code included at global scope cannot see it.
   - So every law function that reaches the hook takes a trailing `AT_TABLE_PARAM`, which is an empty-looking macro. In HLSL it is `, Texture2D AT_Table, SamplerState AT_TableSampler`. In C++ it is `, const FAtmosphereTable& AT_Table`.
   - Calls pass `AT_TABLE_ARG`. The spec's argument lists are otherwise unchanged. The subset is kept, because the only non-subset text lives in the two platform halves.
   - Orbital slice 1's probe is the first thing that proves the HLSL half. This departure goes to the developer with this plan.
2. **The per-bin coefficients and folds, and a shape, not the spec's contract names.**
   - `AT_Air` carries, per bin (eight, ruling 6), the gas's scatter and extinction and the aerosol's scatter and extinction, colourless; the star's light in each bin as `FoldR`, `FoldG` and `FoldB`; and `GasH`, `AerosolH`, the aerosol's `g` and `Top`, all in radii. That is 60 scalars, fifteen float4.
   - Extinction is per unit light in the bin, and a transmittance is folded as 1 less what is lost, so a white surface seen through no air stays exactly white.
   - The spec's contract (`AirRayleigh`, `AirMie` with `w` as `g`, `AirAbsorb`, and `AirShape` with `w` as one albedo) cannot carry CO2's blue/red single-scatter albedo, nor bins.
   - The contract is orbital slice 1's to write, from this struct. See *What orbital slice 1 needs*.
3. **The planet's shadow is hard, in the law and in the reference.**
   - A sample whose ray to the star passes nearer the centre than the surface gets no sunlight. That is the law's "planet's occultation" (decision 1), made exact rather than smeared by the Chapman function's exponential continuation into the planet.
   - Both sides agree, so `.LawMatchesReference` measures only the approximations.
   - With 12 samples, twilight may band on a slow pan. That is a look for orbital slice 1's probe and the playtest. The softening is one line: a smoothstep over one gas scale height of tangent radius.
4. **`.HomothetyInvariance` is held to 1e-9 relative, not "bit for bit"** (*Rulings needed*, 5).
   - `-ProxyLocation / ProxyRadius` and `(Ship - Centre) / R` are the same number mathematically. They are formed by different roundings, though (`SkyProjection` divides a rounded radius by a sine, and the test subtracts universe positions), so their last bits differ.
   - The test holds the two eyes equal to 1e-12, and the law's outputs from them equal to 1e-9. That still proves what the spec wants proven: the air term does not depend on the proxy's scale.
   - Task 8 amends the spec's `.HomothetyInvariance` item to the ruled tolerances in the commit that writes the test.
5. **The Chapman function is not Schueler's closed form.**
   - Schueler's `c / ((c - 1) cos z + 1)` misses the numerical Chapman function by about 2.5% at 60 degrees for Earth air, against `.Chapman`'s 0.5%.
   - The law instead uses the asymptotic form `sqrt(pi X / 2) erfcx(sqrt(X / 2) cos z)` and its first correction in `1 / X` (derived for this plan; its end values are exact to that order, `1 / X` at the zenith and `3 / (8X)` at the horizon, the known `X e^X K1(X)` expansion). `erfcx` is Numerical Recipes' `erfcc` with its `e^(-z^2)` cancelled, so it cannot overflow, and its relative error is below 1.2e-7.
   - Every world that holds its air has `X >= 96`, because `X = 6 lambda^2` for the Jeans lambda and retention begins at `lambda = 4`. That keeps the estimated error under 0.05%.
6. **A measured failure: `.NadirLegible` as stated** (*Rulings needed*, 2).
   - Legibility from 400 km under an overhead sun crosses the air twice, sunlight down and the view up. At `MaxNadirTau450 = 0.5`, every mix at its ceiling keeps **0.34** of its airless contrast in the blue channel, measured by the harness (note 12), not the spec's 0.5. The lightest giant keeps 0.40.
   - Each world would pass at about **tau450 0.32-0.33**, and the lightest giant at 0.38. The test prints these.
   - The constant or the legibility definition is the developer's to change (decision 4's *Cost to change: medium*), never the test's. The ruling is a gate on Task 12, which writes the test and the constant as ruled and amends the spec's decision 4 and `.NadirLegible` item to match.
7. **Aerosol and ozone depth scale with the column mass, `P / g`.** The spec gives both "per bar" at 1 g, and says a heavier world "holds more, since the same pressure is less column". Its giant disc pressures (0.11 bar at 15 M_E, 2.4 bar for a Jupiter) are only reproduced when the aerosol scales as `1 / g` too. Named here so the developer can overrule it.
8. **The spec's "shared shim header" does not exist.** Landing slice (a) kept its `WR_` shims inline in `WorldRelief.ush` and made no shared header (still so on `main` after landing (a) merged, `8cd12ab`). The spec's "O's first commit after landing (a) merges replaces the copies with landing's shared shim header" therefore has nothing to point at, and its *Parallel tracks* table makes track M wait on that commit. The shims stay inline in `Atmosphere.ush`, and whether to extract one shared file is orbital slice 1's first decision (see the last section). `WorldRelief.ush` belongs to landing's track T through slice (b), so extracting one is sequenced there, not here. **Task 8, Step 9 amends the spec's *Parallel tracks* rows for O and M and its merge-order paragraph**, so that M waits on the shim decision in orbital slice 1 rather than on a commit this plan will never make.
9. **"The noon zenith" is straight up under a sun 45 degrees high** (`AtmosphereLaw::NoonSun`, Task 6), not under a sun at the zenith (*Rulings needed*, 4). With the sun overhead, the zenith is the sun's own forward-scattered aureole, and it wears the star's colour. Measured under the Sun with Earth air, its saturation is 0.15 and it fails the spec's "Earth's blue" bound. At 45 degrees it is 0.60, hue 224. `.StarColour`, `.ReferenceKnownValues`, the swatch's noon and the corpus's sky all use this one definition. Task 7 amends the spec's `.StarColour` item and decision 3's table heading to name it, in the commit that writes `.StarColour`.
10. **`.BacklitRing` compares the ring and the lit limb at one grazing height, 3 scale heights.** There the ring is 1.36 times the lit limb (8.3e-2 against 6.1e-2, measured). At each one's own brightest point, though, the lit limb wins: it peaks near the surface at 0.157, where the ring's light, crossing the whole grazing path, is its own extinction, and the ring peaks at 0.103. The spec's sentence does not say which reading it means. The test pins the same-height one and prints both, so the developer sees the peaks. As built (`f60611b`), `.BacklitRing` also pins the haze's forward peak, not only the gas's: at 3 H in Earth's air there is no haze left, so the gas's (1 + cos^2) alone makes the ring win there, and the test adds a thin air (0.05 bar) low down, 0.25 H, where the grazing path is still optically thin and the haze dense, and requires the ring more than four times the lit limb -- more than the gas's twofold could give. The re-planned law keeps it.
11. **A measured failure: `.StarColour`'s red-dwarf bounds** (*Rulings needed*, 1). In the game's colour space (linear sRGB, D65 white, which is `SkyColour::Blackbody`'s), a red dwarf's sky is not "pale grey-cyan". It is the star's own orange, shifted toward blue by Rayleigh's lambda^-4, and it lands peach. Measured noon zeniths, Earth air:

    | Star | Saturation | Hue |
    |---|---|---|
    | 2,000 K | 0.96 | 22 |
    | 2,566 K | 0.72 | 26 |
    | 3,000 K | 0.49 | 29 |
    | 3,500 K | 0.17 | 25 |
    | 4,000 K | 0.17 | 227 |
    | 5,772 K | 0.60 | 224 |
    | 10,000 K | 0.81 | 229 |

   - Saturation therefore falls to a grey near 3,500-4,000 K and rises again into blue. It is not monotonic.
   - The G-star bounds hold (hue within 200-235, saturation within 0.40-0.85).
   - The spec's own estimate, "(1, 1.26, 1.31) relative" at 3,000 K, reads as the sky relative to the star's light, as an eye adapted to the star would see it. But the game does not white-balance per star, and relative to the star every sky is about the same blue (saturation 0.6-0.8 at every temperature).
   - So the developer rules before `.StarColour` is written, and rules from Task 7's swatches rather than from this table. Ruling 2's "pale grey-cyan", the *Worlds* art direction and the bounds move together, and "a blue sky should be an event" still holds, since blue begins near 4,000 K.
12. **This plan's pure C++ was compiled and run before it was written down.**
    - **How.** Every code block of Tasks 1-6, 8, 9 and 12, and Task 7's `.StarColour`, was assembled from this file by applying its steps' own edits at their anchors. That also checks that every anchor exists exactly once. It was compiled with clang 22 (`-Wall -Wshadow -Werror=shadow`) against a minimal stand-in for the UE types it uses, and each test was run.
    - **Passing:** `.Chapman` (worst 0.036%), `.OneBlackbody` (worst 0.0076, so Step 5 of Task 1 should not re-base), `.ReferenceKnownValues`, `.ChannelFit`, `.StarTemperatureExtremes`, every Task 5 test, `.MultiScatterTable`, `.FloatMatchesDouble`, `.PlanetAir` and `.Gases`.
    - **Failing:** `.StarColour` (note 11) and `.NadirLegible` (note 6), as reported above. `.LawMatchesReference` has its own result in Task 8's verdict table.
    - **Fixed in this plan because of those runs:**
      - an eye on the surface looking along it was classed as underground by rounding, a coin toss that blacked out the sky from the ground;
      - float lost a near-eye sample's height in `R - 1` (hence `AT_Altitude`);
      - a Fibonacci sphere left the table 10% from itself;
      - a defaulted nested-struct argument that clang refuses;
      - two `-Wshadow` errors.
    - **Fixed in review, after the harness ran:** `.ChannelFit` bound a reference to a member of a temporary `FAtmosphere` (`Build(...).GetAir()`), which the harness passed by luck; Task 4 now keeps the `FAtmosphere` alive.
    - **Not covered by the harness:** the HLSL half (orbital slice 1's probe), the engine's real headers, UBT's flags (`.ImpactParameter` is the canary for a fused multiply-add), Task 7's swatch writer, Task 8's split into a default and a full grid, Task 10's generator code, `.FixtureWorlds` and the guarantees' move, and Tasks 11 and 13's C++. The Python of the corpus's air columns was run and passes; Task 13's skies reader was not.
13. **The bin count, by measurement (ruling 3's re-plan; ruling 6).**
    - **How.** The plan's harness (note 12's, rebuilt from the built code of Tasks 1-6) ran the law with bins against the reference over decision 1's grid, at ruling 2's ceilings. It searched:
      - every contiguous partition of the 16 wavelengths into four, five, six, seven and eight bins: 455, 1,365, 3,003, 5,005 and 6,435 of them;
      - five ways of weighting a wavelength within its bin, and two of fitting a bin's extinction.
      Single scattering and transmittance were checked against the reference's first order across the whole search. The table was added for the best candidates.
    - **Why three channels failed.** Transmittance is exp of the depth, and one depth per channel cannot stand for a channel whose wavelengths' depths differ by a factor of three over a horizon path. The worst miss was about 54 times its allowance: a blue transmittance of 0.074 against 0.0199, from inside a ceiling air looking at the horizon.
    - **Four to eight bins, the best of each** (single scattering and transmittance against the first order, both builds):

      | Bins | Misses (of 6,912) | Worst, times the allowance | Where |
      |---|---|---|---|
      | 4 | 28 | 4.1 | CO2 at its ceiling, the home star, a horizon from inside, blue transmittance |
      | 5 | 6 | 4.5 | the same ray |
      | 6 | 6 | 4.4 | the same ray |
      | 7 | 0 | 0.86 | -- (one partition only) |
      | 8 | 0 | 0.75 in the search; 0.80 with the code as the plan gives it | -- (several partitions) |

      That ray is the hard one. A red dwarf's blue channel is small, and its light is the difference of large blue terms and negative green-yellow ones, so within-bin error there is amplified. Six bins cannot place a boundary that serves both the blue and the green.
    - **The choice: eight.**
      - Across stars of 2,000-15,000 K (first order), eight bins miss 3 values per build, all at 2,000 K, the worst 1.12 times its allowance. The best six miss 31-48. Procgen makes no star under **2,400 K** (the M band's floor in `StarSystemGenerator.cpp`'s `ClassBands`), so 2,000 K is outside the game, but 2,400 K is not, and the sweep did not report it. `.BinsCarryTheSpectrum` and `.SingleScatteringMatchesReference` therefore run 2,400 K beside the home star and the Sun (`AtmosphereTestFixtures::CoolestStarK`, Task 4). If either misses there, stop and report: that is ruling 6's question again, not a tolerance to loosen.
      - **The margins are in-sample.** The partition was picked from 6,435 candidates against exactly the six airs and two stars those tests check, so their margins measure the fit, not how it generalises. The star sweep above, and now 2,400 K, are the only checks on anything else.
      - Eight is two whole RGBA16F texels for the table, and fifteen float4 for the air (32 coefficients, 24 folds, 4 shape numbers).
      - The weight is the star's light times the length of the wavelength's sRGB vector. Luminance weighting, the obvious choice, fails 22-fold: it all but ignores the blue.
      - A bin's extinction is its wavelengths' weighted mean depth (exact thin). The nadir-exact fit was measured too, and misses more on long paths.
    - **The margins, for every re-planned test.** Each is the harness's, at `-O2`, with the code exactly as the plan gives it, before 2,400 K and the thin airs of `.FloatMatchesDouble` were added (those are unmeasured). These are the figures the plan quotes; the search table's 0.75 was a search variant's:
      - `.BinsCarryTheSpectrum`: 0.92 of its allowance, the tightest, so eight bins have 8% to spare.
      - `.SingleScatteringMatchesReference`: 0.80.
      - `.LawMatchesReference`: 0.80 on the full grid. The default share is now CO2 at its ceiling (Task 8), which the harness did not run as a share; its rays are inside the full grid's 0.80. (The earlier N2/O2 share measured 0.66.)
      - `.MultipleScatteringGap`: 0.75 of 25%.
      - `.FloatMatchesDouble`: 0 misses of 3,960 over the ceiling airs and the giant. With the 0.05 bar airs added (Task 8), it checks 6,930, and the thin airs are unmeasured.
      - `.MultiScatterTable`: every bin within 10%.
      - `.StarColour`: inside ruling 1's bounds, with the margins Task 7 lists.
14. **The march, and why bins alone could not meet ruling 3.**
    - **The midpoint rule's floor.** With sixteen bins, one per wavelength and so spectrally exact, the built law's 12-sample midpoint march still missed 33 values per build in single scattering alone (thin airs up to 21%), and 81 per build in all, at the first ceilings.
    - **Where it missed:**
      - the haze's 1 km scale height under H2/He's 55 km, whose first sample sat a whole haze scale height up;
      - horizons from the ground through ceiling airs, where the view's transmittance falls by e over a few kilometres;
      - limbs, where the density is Gaussian along the ray.
    - **The replacement:**
      - The 12 samples become 12 nodes, `u^2`-spaced from the closest point as before, with both ends of each piece as nodes.
      - Between two nodes, each constituent contributes its exact column times the logarithmic mean `(B - A) / ln(B / A)` of what it gathers per unit column. The column is the difference of the Chapman columns the nodes already hold, for free. The mean is exact for anything exponential in the column: the density, and the view's transmittance through one constituent.
      - A span whose single scattering changes by more than `e^4` takes one more node halfway. Sunlight climbing out of a grazing column, or out of the ground's shadow, rises faster than exponentially, and the mean misses its approach.
    - **The result.** An explicit shadow-edge split was tried as well and dropped: the halfway node alone meets the grid, and a mutation removing the split survived. Across the grid no ray takes more than 14 node evaluations, and a whole dusk sky from the ground takes 12 everywhere. `AT_VIEW_SAMPLES` stays 12, the spec's constant. Its meaning changes from samples to nodes, and the refinement is new; the developer confirms both with the plan (*What the developer is asked*).
15. **The table's columns** (Task 6). Equal steps in the sun's cosine put the two columns either side of the horizon 3.7 degrees from it, where the light the air hands on changes fastest. A ground horizon under a setting sun read about half as bright again as the reference. Columns at equal steps in `T`, cosine `T |T|`, put them 0.06 degrees from it, and that error is gone. The HLSL hook takes a square root per read.
16. **What remains: the isotropy of multiple scattering (ruling 7), and the reference's own azimuth.**
    - **The law's approximation.** The law's table is Hillaire's isotropic approximation (decision 1). The reference computes the second order exactly, with the Rayleigh and Henyey-Greenstein phases, and only the tail beyond it isotropically.
    - **Measured, with sixteen bins, the new march and the new columns:**
      - Against the reference with its second scattering made isotropic (`FOptions::bIsotropicSecondOrder`), the law matches everywhere, with and without the table. So the table and the march are what they claim.
      - Against the full reference, 32 values per build miss 5%, the worst about 19% off.
      - The misses: backlit horizons and limbs from the ground are too dark. The haze's forward peak is aimed at the sky's light, and an isotropic table cannot aim it. Nadir views from inside a ceiling air are too bright, since the haze near the ground scatters forward the dim light from below, not the sky.
      - The gap is physics the spec's method leaves out, not a defect. Closing it needs a directional table: per bin, the second-order light's first and second angular moments. That is five numbers more per bin, so five more 64 x 32 RGBA16F textures per world and ten more texture reads per node. That is a change to decisions 1 and 11.
    - **The reference's azimuth.** The reference itself was found 5.3% short of its own converged value on a backlit horizon: 12 azimuth segments missed the forward peak between them. With 24 it is within 0.5% of 48, at twice the second order's time. Task 8 raises the default.
    - **The Dekker mutant.** The first plan's Task 8 expected `.FloatMatchesDouble` to kill the Dekker-product mutant. It cannot, on the grid's axis-aligned eyes, where every cross product is exact. The float extremes' limbs are now turned out of the axes, and it is killed.
17. **`Shown`: the agreements compare what a display shows, and count what that hides.**
    - The reference's light is spectral, folded to linear sRGB at the end, and a deep orange's blue can come out negative there: outside sRGB's gamut. The law's bins are non-negative, and so is what it folds. `Shown` sets a negative channel to zero on both sides before comparing, in `.SingleScatteringMatchesReference`, `.LawMatchesReference`, `.MultipleScatteringGap`, and (as `Max(..., 0)`) `.BinsCarryTheSpectrum`.
    - That is a loosening no ruling approved: a disagreement wholly outside the gamut is invisible to the test. So each of those tests counts the values `Shown` changed on either side and prints the count in its info line (`N changed by the gamut clamp`). It is expected to be 0 or small, and only under the red dwarfs. It was not counted in the harness.
    - The count goes to the developer with ruling 7. If the developer rules the clamp out, the comparisons drop `Shown`, and any value it was hiding is a miss.

## Execution order and file ownership

**Trees.** Every command names its tree, because an agent's working directory resets between calls.

| Track | Tree, branch | Tasks | Owns |
|---|---|---|---|
| orchestrator | main checkout | the merges; recording the rulings | git, and the spec's *The developer's rulings* section (one `**Atmosphere plan ruling N**` paragraph per ruling) |
| **O: optics** | `.worktrees/air-optics`, `feat/air-optics` | 1-8 (O1-O8) | `Shaders/Private/Atmosphere.ush` (new), `Source/DeepSpace/Atmosphere/Atmosphere.{h,cpp}` (new), `Source/DeepSpace/Atmosphere/AtmosphereReference.{h,cpp}` (new), `Source/DeepSpace/Atmosphere/AtmosphereBins.{h,cpp}` (new), `Source/DeepSpace/Sky/SkyColour.{h,cpp}`, `Source/DeepSpace/Tests/SkyColourSpectrumTest.cpp`, `AtmosphereChapmanTest.cpp`, `AtmosphereReferenceTest.cpp`, `AtmosphereBinsTest.cpp`, `AtmosphereBuildTest.cpp`, `AtmosphereLawTest.cpp`, `AtmosphereMarchTest.cpp`, `AtmosphereTableTest.cpp`, `AtmosphereSwatchTest.cpp`, `AtmosphereAgreementTest.cpp`, `Tests/AtmosphereTestFixtures.h` (all new). In the spec: the *Tests* section's `.StarColour`, `.LawMatchesReference` and `.HomothetyInvariance` items, decision 1's bullets, its interface paragraph and its paragraph on agreement, decision 3's table and its paragraph on coefficients, decision 11's table sizes, and ruling 2's sentence on red dwarfs' skies, which ruling 1 directs (Tasks 7 and 8), and the *Parallel tracks* rows for O and M and the merge-order paragraph under it (Task 8) |
| **G: procgen's air** | `.worktrees/air-procgen`, `feat/air-procgen` | 9 (G1), then 10 (G2), 11 (G3); after track O merges, 12 (G4), 13 (G5) | Task 9: `Universe/AirFacts.{h,cpp}` (new) and `Tests/AirFactsTest.cpp` (new). Task 10: `Universe/StarSystem.h`, `StarSystemGenerator.{h,cpp}`, `GenPriors.{h,cpp}`, `ProcGenPriorsConfig.h`, `Config/DefaultGame.ini`, `Universe/AirFacts.h` (the guarantees move out), `Tests/AirFixtureWorlds.h` and `Tests/AirProcGenTest.cpp` (new), and the spec's *Fixture worlds* table. Task 11: `Tools/procgen_corpus_contract.json`, `Tools/procgen_corpus.py`, `Tools/procgen_corpus_sample.tsv`, `Tools/test_procgen_corpus.py` and `Tests/ProcGenCorpusTest.cpp`. Task 12: `Atmosphere/PlanetAir.{h,cpp}` (new), `Tests/AtmosphereNadirTest.cpp` (new), `Tests/AtmosphereSwatchTest.cpp` (track O's, handed over once O has merged), `Universe/GenPriors.h` again if ruling 2 moves `MaxNadirTau450`, and the spec's decision 4 and `.NadirLegible` item. Task 13: `Tests/CorpusSkiesTest.cpp` (new), `Tools/procgen_corpus_skies_sample.tsv` (new), `Tools/procgen_corpus_contract.json`, `Tools/procgen_corpus.py` and `Tools/test_procgen_corpus.py` |

Within this plan, the spec is edited by O, G and the orchestrator, always in different hunks: O's are in *Tests* (the three items), decisions 1, 3 and 11, ruling 2's one sentence, and *Parallel tracks*; G's in *Fixture worlds*, decision 4 and *Tests* (the `.NadirLegible` item, a different item from O's); the orchestrator's in *The developer's rulings*. Each gated task merges `main` before it edits, so it edits on top of the rulings.

**Files this plan and the landing and wear plans both touch, and who goes first** (as of `main` at `df37646`, 2026-09-28). **Landing slice (a) has merged:** track P as `d1cfbc8`, track R as `8cd12ab`, and both trees are gone. R's `cb0a651` removed the `DeepSpaceShaders` module: `/Project` is now the engine's own mapping, which `FEngineLoop::PreInit` makes to `<project>/Shaders` whenever that directory exists, and R's `DeepSpace.Surface.ShaderMapping` holds that. `Atmosphere.ush` relies on exactly that mapping and on no module; it adds none. R's `rebuild.sh` and `launch.sh` now watch every `Shaders/*.ush`, which covers `Atmosphere.ush` too. **Track G's Tasks 9-11 have merged** (`3c205c5`), and **wear slice 1 has merged** (`43af0dd`; `.worktrees/wear-1-s` is gone). **Landing slice (b) runs in three trees**, `.worktrees/landing-b-f`, `landing-b-s` and `landing-b-t` (`feat/landing-b-{f,s,t}`, at `d6933a9`, `3013b3a` and `bed40c8`). Between them they touch `DeepSpace.uproject`, `DeepSpace.Build.cs`, `Shaders/Private/WorldRelief.ush`, `Sky/ShipSky.*`, `Sky/SkyMaterialContract.h`, `Tools/sky_material_contract.json`, `Tools/setup_sky_materials.py`, sky material assets, `CLAUDE.md`, and ship, surface and flight files. Tasks 4-8 edit none of them, and no test name collides. What slice (b) does share with Tasks 4-8 is the one lock: every test Tasks 4-8 add to the default suite runs in all three trees (Global Constraints, *The default suite stays fast*).

| File | The other plan's owner | This plan | Order |
|---|---|---|---|
| `Source/DeepSpace/Universe/StarSystem.h` | landing P (Tasks 2-3: `ReliefKm`, `SurfaceGravityEarth`) | Task 10 (G2): `AirMix`, `SurfacePressureBar` | **P merged to `main` first** (`d1cfbc8`). Task 10's Step 1 checks that its tree has P. Landing's slices (b) and (c) edit none of Tasks 10, 11 or 13's files. |
| `Universe/StarSystemGenerator.{h,cpp}` | landing P (`PlanetSeed` public, `DrawRelief`, `GenerateRelief`) | Task 10 (`FAirDraw`, `EAirDraws`, `DrawAir`, `GenerateAir`, `GenerateWithPlanetCount`'s draws-off parameter) | P first (done) |
| `Universe/GenPriors.{h,cpp}` | landing P (relief priors, `MaxReliefKm`) | Task 10 (air priors, domain, the air's guarantees), Task 12 only if ruling 2 moves `MaxNadirTau450` | P first (done) |
| `Universe/ProcGenPriorsConfig.h` | landing P | Task 10 | P first (done) |
| `Config/DefaultGame.ini` | landing P (relief lines, merged); wear S5 (a `[/Script/DeepSpace.ShipSubsystem]` section at the end of the file, merged `43af0dd`) | Task 10 (air lines inside `[/Script/DeepSpace.ProcGenPriorsConfig]`, before `; The galaxy: ...`), merged `3c205c5` | Done: all three merged, non-adjacent hunks. Task 13 merges `main` into `air-procgen` before it merges (Steps 10-11). |
| `Tools/procgen_corpus_contract.json`, `procgen_corpus.py`, `procgen_corpus_sample.tsv`, `test_procgen_corpus.py`, `Tests/ProcGenCorpusTest.cpp` | landing P (Task 4: `surface_gravity_g`, `relief_km`) | Task 11 (G3): four air columns; Task 13 (G5): the skies file | P first (done), then Task 11 after Task 10, and Task 13 after Task 12 |
| `Shaders/Private/` (the directory) | landing R created `WorldRelief.ush` there (merged `8cd12ab`); slice (b)'s three trees all edit it | Task 2 (O2) created `Atmosphere.ush` beside it | No shared file. `feat/air-optics` may merge before or after slice (b). |
| `Universe/UniverseUnits.h` | landing P (adds GM constants) | read only (Task 9 includes it) | No edit here |
| `Sky/SkyProjection.{h,cpp}`, `Sky/SkySystem.h` | landing P owned `SkySystem.*` in (a); T owns `SkyProjection.*` in (b) (`.worktrees/landing-b-t`) | read only: Task 8's `.HomothetyInvariance` calls `SkyProjection::Project` and builds an `FSkySystem`; Task 8, Step 8's seventh mutation edits `SkyProjection.cpp` for one run, restored by `Tools/mutate.sh` | No edit here. `SkyProjection.*` is unchanged in `landing-b-t` at `bed40c8`, and landing's T tasks plan no edit to it. **If T merges first, re-check** before Task 8: `git diff df37646 main -- Source/DeepSpace/Sky/SkyProjection.*` must leave `Project`'s signature and the mutation's anchor line intact. A later change to `Project`'s signature by T must update `AtmosphereAgreementTest.cpp`, which T then owns for that commit. |
| `Sky/ShipSky.{h,cpp}` | landing R (merged) and T (`landing-b-t` edits both), and the atmospheres spec's M | read only: Task 7 calls `ShipSky::ManualExposureBias` | No edit here. `landing-b-t` leaves `ManualExposureBias` unchanged at `bed40c8`. |
| `Source/DeepSpace/DeepSpace.Build.cs`, `DeepSpace.uproject` | landing slice (b), all three trees | **not edited**: the swatch's `FImageUtils` is in `Engine`, which already depends publicly on `ImageCore` | No overlap |
| `Tools/mutate.sh` | landing R (adds `MUTATE_RUNNER`) | used, never edited | No edit here |
| `rebuild.sh`, `launch.sh` | landing R (taught them `Shaders/` and `.ush`; merged) | not edited | Done: a change to `Atmosphere.ush` alone now makes `./launch.sh` and `./rebuild.sh` rebuild. |
| `CLAUDE.md` | landing P and R, wear Z (merged); landing slice (b)'s `landing-b-s` and `landing-b-t` | **not edited here.** The spec's documentation ("CLAUDE.md: a new section, *The air*") is written after slice 1 merges | No overlap |

**Order and merge points:**

```
Track O (now, no landing dependency):
  1 (O1) SkyColour's spectrum, ThroughFilter, OneBlackbody
  2 (O2) Atmosphere.ush: shims and the Chapman function, compiled twice
  3 (O3) the reference
  (1-3 built; 4-6 and 7's swatch built for three channels, re-planned below for ruling 3)
  4 (O4) [GATE: ruling 6 recorded on main]  the fixtures at ruling 2's ceilings; the eight bins
  5 (O5) the law over the bins: the air, the march, the fold, the table per bin
  6 (O6) the table's columns crowd the horizon
  7 (O7) the swatches again through the bins, to the developer; the swatch's mutation corrected;
         [rulings 1 and 4 recorded]  .StarColour as ruling 1 states it, and the spec's words for it
  8 (O8) [GATE: ruling 7 recorded on main]  the law against the reference (a default share,
         Atmosphere.Full.LawMatchesReference, Atmosphere.Full.MultipleScatteringGap), float against
         double, the homothety; the spec's shim rows
  -> MERGE feat/air-optics into main (touches no landing file): when Tasks 1-8 are committed,
     ./test.sh is green, both Atmosphere.Full.* grids are green, and
     Atmosphere.Full.GroundSkySwatch writes its six swatches

Track G:
  9 (G1) AirFacts, pure (now: new files only)
  -> MERGE feat/air-procgen into main when green (new files only)
  10 (G2) the draws on FPlanet, the draws-off comparison, the fixture worlds and the spec's table,
          the guarantees into GenPriors.h
          (merge main first; wear-1-s not mid-edit on Config/DefaultGame.ini)
  11 (G3) the corpus's four air facts
  -> MERGE feat/air-procgen into main (the fixture table and the draws reach main before
     slices 2 and 4 are planned; the second of air-procgen and wear-1-s to merge merges main first)
  [wait: track O merged into main]
  12 (G4) [GATE: ruling 2 recorded on main]  PlanetAir, NadirLegible; the swatch re-pointed at R, G and C
  13 (G5) the corpus's skies, all 10,000 systems (Atmosphere.Full.CorpusSkies, outside the default suite)
  -> MERGE feat/air-procgen into main
```

Tasks 4-8 are sequential: each consumes the previous one's interface, except that Task 8 consumes only Tasks 3-6, so it may run before Task 7 (both in `air-optics`, one commit at a time). Task 9 runs in parallel with all of them. Tasks 10-13 are sequential.

## Conventions for every task

- **Line numbers** are "about", taken from `main` at `e5d257c`. For Tasks 10, 11 and 13 they are taken from `main` after track P merged (`d1cfbc8`), so search for the quoted anchor text, never the number.
- **Build after every C++ change:** `./build.sh`. A new or changed header (`.h` or `.ush`) needs the full build. If an editor was open on the tree, run `./rebuild.sh --force` first.
- **Test** with `./test.sh <path>`. It prints `passed: N` and exits non-zero on any failure, when no test ran, or when the log is dirty.
- **Whole suite green.** Every "whole suite" step expects `./test.sh` green. The rulings are gates precisely so that no task commits a known-red test; a step that finds the suite red stops and reports, and does not commit.
- **Seconds per test.** Before each commit that adds a `DeepSpace.*` test, time it: `time ./test.sh <path>` against `time ./test.sh DeepSpace.Sky.Colour` (an existing test that costs nothing: start-up alone), in the same tree, and put a line `Measured: <path> costs N s over start-up.` above the commit message's `Co-Authored-By` line, `N` being the difference in whole seconds. Task 6 shows the form. **Every `Measured:` line in this plan's commit messages is a template:** replace `N` with the measured number before committing, and never commit a literal `N`. Check with `git log -1 --format=%B | grep -c ' N s'`, which must print `0` (if it does not, `git commit --amend` with the numbers). A new test more than 5 s over start-up stops the task before its commit: its grid is split as Task 8 splits `.LawMatchesReference` (a default grid under `DeepSpace.`, the full grid as `Atmosphere.Full.<Name>`), and the split is reported with the numbers. The plan's harness could not measure these in the engine, so the plan states none.
- **Mutate** only committed files, because `Tools/mutate.sh` refuses uncommitted ones. Every mutation comes after its task's commit. Run `./build.sh` after each mutation run, because its last build held the mutant.
- **Test names** are siblings. `DeepSpace.Atmosphere`, `DeepSpace.Universe` and `Atmosphere.Full` are groups, never tests. No test name here is a prefix of another (`./test.sh` filters by substring), and no `Atmosphere.Full.*` name contains `DeepSpace`.
- **Units.** The optics work in radii of the body (the surface at `R = 1`) and in nadir optical depths. `FAirSpec` is in centimetres. `AirFacts` is in bar, km, K, Earth masses and radii, and Earth g.

---
# Track O: the optics

## Task 1 (O1): SkyColour's spectrum, `ThroughFilter`, and `DeepSpace.Sky.OneBlackbody`

Decision 3: the one blackbody stays in `SkyColour` (plan conflict 6), which gains the integral it has so far approximated. What this task adds:
- the 16-sample spectrum, the CIE 1931 matching functions and the sRGB matrix, which the reference and the channel fit both use;
- `ThroughFilter`;
- the comparison that holds `Blackbody` to the integral.

Sign-off item 3 is approved as recommended: if the two disagree by more than 2%, `Blackbody` is re-based onto the integral. Step 5 is that branch.

**Files:**
- Create the tree: `.worktrees/air-optics` on `feat/air-optics`
- Modify: `Source/DeepSpace/Sky/SkyColour.h` (whole file below)
- Modify: `Source/DeepSpace/Sky/SkyColour.cpp` (whole file below)
- Test: create `Source/DeepSpace/Tests/SkyColourSpectrumTest.cpp` (`DeepSpace.Sky.OneBlackbody`)

**Interfaces:**
- Consumes: `FLinearColor::MakeFromColorTemperature` (engine, through `SkyColour::Blackbody`).
- Produces:

```cpp
namespace SkyColour
{
    DEEPSPACE_API FLinearColor Blackbody(double TemperatureK);                       // unchanged signature
    DEEPSPACE_API FLinearColor ThroughFilter(double TemperatureK, TFunctionRef<double(double Nm)> Filter);
    namespace Spectral
    {
        inline constexpr int32 Count = 16;
        inline constexpr double FirstNm = 400.0, StepNm = 20.0;
        inline constexpr double MinTemperatureK = 1000.0, MaxTemperatureK = 15000.0;
        constexpr double Nm(int32 Index);
        DEEPSPACE_API double Planck(double Nm, double TemperatureK);        // W sr^-1 m^-2 nm^-1
        DEEPSPACE_API FVector3d ChannelWeights(int32 Index);                 // linear sRGB per unit spectral radiance
        DEEPSPACE_API double Luminance(int32 Index);                         // CIE Y per unit spectral radiance
        DEEPSPACE_API FVector3d ToLinearSrgb(TConstArrayView<double> Spectrum);
    }
}
```

- [ ] **Step 1: The tree.**

```bash
cd /home/matt/Development/deepspace && git worktree add .worktrees/air-optics -b feat/air-optics main
cd /home/matt/Development/deepspace/.worktrees/air-optics && git log --oneline -1
```

Expected: the tree exists at `main`'s head.

- [ ] **Step 2: Write the failing test.** Create `Source/DeepSpace/Tests/SkyColourSpectrumTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Sky/SkyColour.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyOneBlackbodyTest,
    "DeepSpace.Sky.OneBlackbody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SkyColourSpectrumTestLocal
{
    /** A light as Blackbody gives one: no channel below zero, the brightest 1. */
    FLinearColor Chromaticity(FLinearColor Colour)
    {
        Colour.R = FMath::Max(Colour.R, 0.0f);
        Colour.G = FMath::Max(Colour.G, 0.0f);
        Colour.B = FMath::Max(Colour.B, 0.0f);
        const float Peak = FMath::Max3(Colour.R, Colour.G, Colour.B);
        if (Peak > 0.0f)
        {
            Colour.R /= Peak;
            Colour.G /= Peak;
            Colour.B /= Peak;
        }
        Colour.A = 1.0f;
        return Colour;
    }
}

bool FSkyOneBlackbodyTest::RunTest(const FString& Parameters)
{
    using namespace SkyColourSpectrumTestLocal;
    const auto Clear = [](double) { return 1.0; };

    // -- The integral on its own -------------------------------------------------
    const FLinearColor Black = SkyColour::ThroughFilter(5772.0, [](double) { return 0.0; });
    TestTrue(TEXT("through an opaque filter a star is black"), Black.R == 0.0f && Black.G == 0.0f && Black.B == 0.0f);

    const FLinearColor Sun = SkyColour::ThroughFilter(5772.0, Clear);
    TestTrue(FString::Printf(TEXT("a Sun through nothing is a light, every channel positive (%.4g, %.4g, %.4g)"), Sun.R, Sun.G, Sun.B),
        Sun.R > 0.0f && Sun.G > 0.0f && Sun.B > 0.0f);

    const FLinearColor Red = SkyColour::ThroughFilter(5772.0, [](double Nm) { return Nm >= 600.0 ? 1.0 : 0.0; });
    TestTrue(TEXT("a filter that passes only 600 nm and up leaves red, and next to no blue"),
        Red.R > 0.0f && Red.B < 0.05f * Red.R);

    const FLinearColor Hot = SkyColour::ThroughFilter(15000.0, Clear);
    const FLinearColor Cool = SkyColour::ThroughFilter(2000.0, Clear);
    TestTrue(TEXT("a hot star is bluer than red, a cool one redder than blue"), Hot.B > Hot.R && Cool.R > Cool.B);

    TestTrue(TEXT("a brightness, not a chromaticity: a 6,000 K surface outshines a 3,000 K one"),
        SkyColour::ThroughFilter(6000.0, Clear).G > 10.0f * SkyColour::ThroughFilter(3000.0, Clear).G);

    const FLinearColor Under = SkyColour::ThroughFilter(500.0, Clear);
    const FLinearColor Floor = SkyColour::ThroughFilter(1000.0, Clear);
    TestTrue(TEXT("clamped as Blackbody clamps: 500 K integrates as 1,000 K"),
        Under.R == Floor.R && Under.G == Floor.G && Under.B == Floor.B);

    // -- Blackbody against it (decision 3, sign-off item 3) ------------------------
    float Worst = 0.0f;
    int32 WorstK = 0;
    for (int32 Kelvin = 2000; Kelvin <= 15000; Kelvin += 250)
    {
        const FLinearColor Fit = SkyColour::Blackbody(Kelvin);
        const FLinearColor Integral = Chromaticity(SkyColour::ThroughFilter(Kelvin, Clear));
        const float Diff = FMath::Max3(FMath::Abs(Fit.R - Integral.R), FMath::Abs(Fit.G - Integral.G), FMath::Abs(Fit.B - Integral.B));
        if (Diff > Worst)
        {
            Worst = Diff;
            WorstK = Kelvin;
        }
    }
    AddInfo(FString::Printf(TEXT("Blackbody against ThroughFilter, both with the brightest channel 1: worst %.4f, at %d K"), Worst, WorstK));
    TestTrue(TEXT("Blackbody is the spectral integral to within 2% of its brightest channel, 2,000-15,000 K"), Worst <= 0.02f);
    return true;
}

#endif
```

  "2% per channel" is read as an absolute 0.02 on colours whose brightest channel is 1. That is the scale `Blackbody` returns. A relative reading would be undefined for the near-zero blue of a 2,000 K star.

- [ ] **Step 3: Run it; expect a compile failure.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: FAIL, `no member named 'ThroughFilter' in namespace 'SkyColour'`.

- [ ] **Step 4: The spectrum and the integral.** Replace `Source/DeepSpace/Sky/SkyColour.h` with:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Templates/Function.h"

/**
 * The one blackbody in the project (plan conflict 6). A star's colour is a
 * presentation concern, so it lives on the sky's side of the seam and
 * procgen's subsystem has no StarColour of its own.
 *
 * The sky's air integrates the same blackbody through its own scattering
 * (atmospheres decision 3), so the spectrum it integrates over lives here
 * too: one Planck function, one set of matching functions, one sRGB matrix.
 */
namespace SkyColour
{
    /**
     * The colour of a blackbody at this temperature, in linear sRGB, scaled
     * so its brightest channel is 1: a chromaticity, never a brightness. How
     * bright a star is comes from its luminosity and distance, and mixing the
     * two here would make a hot star brighter twice.
     *
     * Hot stars are blue-white and cool ones orange-red, against a D65 white,
     * so a Sun-like star reads faintly warm -- as the Sun does in a photograph
     * white-balanced for daylight. Clamped to the 1,000-15,000 K the
     * approximation covers; nothing drawn here is cooler, and above 15,000 K
     * the colour has stopped changing to the eye.
     */
    DEEPSPACE_API FLinearColor Blackbody(double TemperatureK);

    /**
     * Linear sRGB (Rec. 709 primaries, D65 white, the convention Blackbody
     * already uses) of a TemperatureK blackbody seen through Filter(lambda),
     * integrated against the CIE 1931 colour-matching functions at the 16
     * wavelengths of Spectral (atmospheres decision 3). Not normalised: a
     * brightness, in W sr^-1 m^-2 weighted by the matching functions. The
     * temperature is clamped as Blackbody clamps it. A channel can come back
     * negative for a light outside the sRGB gamut; clamping it is the
     * caller's choice, not this function's.
     */
    DEEPSPACE_API FLinearColor ThroughFilter(double TemperatureK, TFunctionRef<double(double Nm)> Filter);

    /**
     * The spectrum the sky's colour is integrated over: 16 samples, 400 to
     * 700 nm, 20 nm apart. Coarse, and deliberately so: the air's optical
     * depths vary smoothly across it, the reference integrates on the same
     * samples, and DeepSpace.Sky.OneBlackbody holds the result to the
     * engine's Planckian-locus fit.
     */
    namespace Spectral
    {
        inline constexpr int32 Count = 16;
        inline constexpr double FirstNm = 400.0;
        inline constexpr double StepNm = 20.0;
        inline constexpr double MinTemperatureK = 1000.0;
        inline constexpr double MaxTemperatureK = 15000.0;

        /** The wavelength of sample Index, nm: 400, 420, ..., 700. */
        constexpr double Nm(int32 Index) { return FirstNm + StepNm * Index; }

        /** Planck's spectral radiance, W sr^-1 m^-2 nm^-1, at a temperature
         *  clamped to [MinTemperatureK, MaxTemperatureK]. */
        DEEPSPACE_API double Planck(double Nm, double TemperatureK);

        /** The linear sRGB that one unit of spectral radiance at sample
         *  Index adds over its 20 nm: the XYZ-to-sRGB matrix applied to the
         *  matching functions there, times the step. Channels can be
         *  negative: the matching functions reach outside the sRGB gamut. */
        DEEPSPACE_API FVector3d ChannelWeights(int32 Index);

        /** What that unit adds to luminance, CIE Y: y-bar times the step. */
        DEEPSPACE_API double Luminance(int32 Index);

        /** The sum over the samples of Spectrum[i] ChannelWeights(i).
         *  Spectrum holds Count values. */
        DEEPSPACE_API FVector3d ToLinearSrgb(TConstArrayView<double> Spectrum);
    }
}
```

  Replace `Source/DeepSpace/Sky/SkyColour.cpp` with:

```cpp
#include "Sky/SkyColour.h"

#include <cmath>

namespace SkyColourLocal
{
    using SkyColour::Spectral::Count;

    // CIE 1931 2-degree standard observer at 400, 420, ..., 700 nm.
    constexpr double CieX[Count] = {0.01431, 0.13438, 0.34828, 0.29080, 0.09564, 0.00490, 0.06327, 0.29040,
                                    0.59450, 0.91630, 1.06220, 0.85445, 0.44790, 0.16490, 0.04677, 0.01136};
    constexpr double CieY[Count] = {0.000396, 0.004000, 0.023000, 0.060000, 0.139020, 0.323000, 0.710000, 0.954000,
                                    0.995000, 0.870000, 0.631000, 0.381000, 0.175000, 0.061000, 0.017000, 0.004102};
    constexpr double CieZ[Count] = {0.06785, 0.64560, 1.74706, 1.66920, 0.81295, 0.27200, 0.07825, 0.02030,
                                    0.00390, 0.00165, 0.00080, 0.00019, 0.00002, 0.0, 0.0, 0.0};

    /** XYZ to linear sRGB, BT.709 primaries and D65: the matrix
     *  FLinearColor::MakeFromColorTemperature uses, so the fit and the
     *  integral land in one space. */
    FVector3d XyzToLinearSrgb(double X, double Y, double Z)
    {
        return FVector3d(
             3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z,
            -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z,
             0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z);
    }
}

FLinearColor SkyColour::Blackbody(double TemperatureK)
{
    // The engine's Planckian-locus fit (Krystek), which returns linear sRGB
    // at unit luminance. Wrapped rather than rewritten: one fit, maintained
    // by somebody else, and this function is the only place the project
    // asks for it. DeepSpace.Sky.OneBlackbody holds it to ThroughFilter.
    const float Clamped = static_cast<float>(FMath::Clamp(TemperatureK, Spectral::MinTemperatureK, Spectral::MaxTemperatureK));
    FLinearColor Colour = FLinearColor::MakeFromColorTemperature(Clamped);

    // Out of gamut below about 1,900 K the fit dips a channel negative;
    // no display can show less than none of a primary.
    Colour.R = FMath::Max(Colour.R, 0.0f);
    Colour.G = FMath::Max(Colour.G, 0.0f);
    Colour.B = FMath::Max(Colour.B, 0.0f);

    const float Peak = FMath::Max3(Colour.R, Colour.G, Colour.B);
    if (Peak > 0.0f)
    {
        Colour.R /= Peak;
        Colour.G /= Peak;
        Colour.B /= Peak;
    }
    Colour.A = 1.0f;
    return Colour;
}

double SkyColour::Spectral::Planck(double Nm, double TemperatureK)
{
    // 2 h c^2 / lambda^5 / (e^(h c / lambda k T) - 1), per metre of
    // wavelength, then per nanometre. expm1 keeps the short-wave tail of a
    // cool star, where the exponent is large, and the long-wave one of a hot
    // star, where it is small.
    constexpr double TwoHC2 = 2.0 * 6.62607015e-34 * 299792458.0 * 299792458.0;
    constexpr double SecondRadiation = 1.438776877e-2;   // h c / k, m K
    const double T = FMath::Clamp(TemperatureK, MinTemperatureK, MaxTemperatureK);
    const double Metres = Nm * 1.0e-9;
    return TwoHC2 / std::pow(Metres, 5.0) / std::expm1(SecondRadiation / (Metres * T)) * 1.0e-9;
}

FVector3d SkyColour::Spectral::ChannelWeights(int32 Index)
{
    using namespace SkyColourLocal;
    check(Index >= 0 && Index < Count);
    return XyzToLinearSrgb(CieX[Index], CieY[Index], CieZ[Index]) * StepNm;
}

double SkyColour::Spectral::Luminance(int32 Index)
{
    using namespace SkyColourLocal;
    check(Index >= 0 && Index < Count);
    return CieY[Index] * StepNm;
}

FVector3d SkyColour::Spectral::ToLinearSrgb(TConstArrayView<double> Spectrum)
{
    check(Spectrum.Num() == Count);
    FVector3d Sum = FVector3d::ZeroVector;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        Sum += ChannelWeights(Index) * Spectrum[Index];
    }
    return Sum;
}

FLinearColor SkyColour::ThroughFilter(double TemperatureK, TFunctionRef<double(double Nm)> Filter)
{
    double Spectrum[Spectral::Count];
    for (int32 Index = 0; Index < Spectral::Count; ++Index)
    {
        const double Nm = Spectral::Nm(Index);
        Spectrum[Index] = Spectral::Planck(Nm, TemperatureK) * Filter(Nm);
    }
    const FVector3d Rgb = Spectral::ToLinearSrgb(MakeArrayView(Spectrum, Spectral::Count));
    return FLinearColor(static_cast<float>(Rgb.X), static_cast<float>(Rgb.Y), static_cast<float>(Rgb.Z), 1.0f);
}
```

- [ ] **Step 5: Build and run; read the comparison.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && ./test.sh DeepSpace.Sky.OneBlackbody; grep -h "Blackbody against ThroughFilter" Saved/Logs/DeepSpace.log | tail -1
```

Expected: `passed: 1`, with the info line printing a worst difference at or under 0.02. The plan's harness measured 0.0076, at 6,750 K, so the re-base below should not be needed. **If the only failure is the 2% comparison**, sign-off item 3 applies (approved: re-base). Replace the body of `SkyColour::Blackbody` in `SkyColour.cpp`, from `const float Clamped` through `Colour.A = 1.0f;`, with the lines below, and change the comment above it to say `Blackbody` is the integral, normalised:

```cpp
    // Re-based onto the spectral integral (atmospheres sign-off item 3): the
    // engine's Planckian fit missed it by more than 2% of the brightest
    // channel, and two blackbodies would let the sky's air and the star it
    // lights disagree. Every star's colour moved slightly, once.
    FLinearColor Colour = ThroughFilter(TemperatureK, [](double) { return 1.0; });
    Colour.R = FMath::Max(Colour.R, 0.0f);
    Colour.G = FMath::Max(Colour.G, 0.0f);
    Colour.B = FMath::Max(Colour.B, 0.0f);
    const float Peak = FMath::Max3(Colour.R, Colour.G, Colour.B);
    if (Peak > 0.0f)
    {
        Colour.R /= Peak;
        Colour.G /= Peak;
        Colour.B /= Peak;
    }
    Colour.A = 1.0f;
```

  Then run `./build.sh && ./test.sh DeepSpace.Sky`. Expected: every `DeepSpace.Sky.*` test passes, `DeepSpace.Sky.Colour` included. Note in the commit which branch was taken, and the worst difference printed.

- [ ] **Step 6: The whole suite.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./test.sh
```

Expected: `passed: N`, no `FAILED`, no `LOG NOT CLEAN`.

- [ ] **Step 7: Commit.**

```bash
git -C /home/matt/Development/deepspace/.worktrees/air-optics add Source/DeepSpace/Sky/SkyColour.h Source/DeepSpace/Sky/SkyColour.cpp Source/DeepSpace/Tests/SkyColourSpectrumTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/air-optics commit -F - <<'MSG'
feat(sky): the one blackbody's spectrum -- ThroughFilter, and Blackbody held to it

SkyColour::ThroughFilter integrates a blackbody through a filter against
the CIE 1931 matching functions at 16 wavelengths, 400-700 nm, into the
linear sRGB Blackbody already uses (atmospheres decision 3). The spectrum,
the matching functions and the sRGB matrix live here, the one place, for
the air's reference and its channel fit to share.

DeepSpace.Sky.OneBlackbody holds Blackbody to the normalised integral
within 2% of the brightest channel from 2,000 to 15,000 K.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

  If Step 5 re-based `Blackbody`, add to the message body: `Blackbody re-based onto the integral (sign-off item 3): the fit missed by <worst> at <K>.`

- [ ] **Step 8: Prove the test can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Sky/SkyColour.cpp 'Spectrum[Index] = Spectral::Planck(Nm, TemperatureK) * Filter(Nm);' 'Spectrum[Index] = Spectral::Planck(Nm, TemperatureK);' DeepSpace.Sky.OneBlackbody
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: `KILLED` (the opaque filter no longer blackens the star). If `Blackbody` was **not** re-based, also run this. It must print `KILLED` too, because the comparison itself sees a wrong matching function:

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Sky/SkyColour.cpp '0.06785, 0.64560, 1.74706' '0.06785, 0.64560, 0.74706' DeepSpace.Sky.OneBlackbody
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

---

## Task 2 (O2): `Atmosphere.ush`'s shims and the Chapman function, compiled twice

This is the shared file's first text: the two platform halves' shims, the constants, and the Chapman function in the log domain. The file is compiled into `Atmosphere.cpp` as double and as float. Planning note 5 explains why the form is the asymptotic one with its first correction, and not Schueler's.

**Files:**
- Create: `Shaders/Private/Atmosphere.ush`
- Create: `Source/DeepSpace/Atmosphere/Atmosphere.h`, `Source/DeepSpace/Atmosphere/Atmosphere.cpp`
- Test: create `Source/DeepSpace/Tests/AtmosphereChapmanTest.cpp` (`DeepSpace.Atmosphere.Chapman`)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces:
  - The `.ush`: `AT_REAL`, the shims `AT_floor`, `AT_saturate`, `AT_min`, `AT_max`, `AT_sqrt`, `AT_exp`, `AT_log`, `AT_cos` and `AT_sin`, and the constants `AT_PI`, `AT_TINY` and `AT_MAX_EXPONENT`.
  - The Chapman functions:
    ```hlsl
    AT_REAL AT_LogErfcx(AT_REAL Y);
    AT_REAL AT_LogChapmanUp(AT_REAL X, AT_REAL CosZenith);
    AT_REAL AT_LogChapman(AT_REAL X, AT_REAL CosZenith);
    ```
  - In C++, the namespaces `AtmosphereF64` and `AtmosphereF32`, each holding the whole `.ush`, and the wrappers `double AtmosphereLaw::LogChapmanF64(double X, double CosZenith)` and `float AtmosphereLaw::LogChapmanF32(float X, float CosZenith)`.

- [ ] **Step 1: Write the failing test.** Create `Source/DeepSpace/Tests/AtmosphereChapmanTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAtmosphereChapmanTest,
    "DeepSpace.Atmosphere.Chapman",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereChapmanTestLocal
{
    /** The integral of F(t) for t = T0 + Sign L u^2, u in [0, 1]: Simpson in
     *  u, which crowds the samples at T0, where every integrand here peaks. */
    template <typename TF>
    double Mapped(TF F, double T0, double Sign, double L, int32 Intervals)
    {
        const double Step = 1.0 / Intervals;
        double Sum = 0.0;
        for (int32 I = 0; I <= Intervals; ++I)
        {
            const double U = I * Step;
            const double Weight = (I == 0 || I == Intervals) ? 1.0 : ((I % 2) == 1 ? 4.0 : 2.0);
            Sum += Weight * F(T0 + Sign * L * U * U) * 2.0 * L * U;
        }
        return Sum * Step / 3.0;
    }

    /**
     * ln of the Chapman function by quadrature, in scale heights: the column
     * along the ray over the vertical column from the same point, the
     * integral of e^(X - r(t)). Below the horizon the density is taken
     * relative to the ray's lowest point, so no term exceeds 1 and nothing
     * overflows however deep the tangent; the planet is ignored, as the
     * function ignores it.
     */
    double LogChapmanByQuadrature(double X, double Cos)
    {
        const double Sin = std::sqrt(FMath::Max(1.0 - Cos * Cos, 0.0));
        const double Lowest = Cos < 0.0 ? X * Sin : X;
        const auto Density = [X, Cos, Lowest](double T) { return std::exp(Lowest - std::sqrt(X * X + 2.0 * X * T * Cos + T * T)); };
        const double Far = Lowest + 80.0;
        double Sum = 0.0;
        if (Cos < 0.0)
        {
            const double Tangent = -X * Cos;
            Sum += Mapped(Density, Tangent, -1.0, Tangent, 40000);
            Sum += Mapped(Density, Tangent, 1.0, std::sqrt(Far * Far - Lowest * Lowest), 40000);
        }
        else
        {
            Sum += Mapped(Density, 0.0, 1.0, std::sqrt(Far * Far - X * X * Sin * Sin) - X * Cos, 40000);
        }
        return (X - Lowest) + std::log(Sum);
    }
}

bool FAtmosphereChapmanTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereChapmanTestLocal;

    // X = R / H. Every world that holds its air has X >= 96 (X = 6 lambda^2,
    // and retention starts at lambda 4); an Earth is about 850 and a heavy
    // giant several thousand.
    const double Xs[] = {96.0, 200.0, 850.0, 4000.0, 10000.0};
    double Worst = 0.0;
    FString WorstAt;
    bool bFinite = true;
    double WorstFloat = 0.0;
    for (const double X : Xs)
    {
        // 0 to 89 degrees, the horizon, and 30 degrees below it.
        for (int32 Degrees = 0; Degrees <= 120; ++Degrees)
        {
            const double Cos = std::cos(FMath::DegreesToRadians(static_cast<double>(Degrees)));
            const double Law = AtmosphereLaw::LogChapmanF64(X, Cos);
            const double Quadrature = LogChapmanByQuadrature(X, Cos);
            const double Error = FMath::Abs(std::exp(Law - Quadrature) - 1.0);
            if (Error > Worst)
            {
                Worst = Error;
                WorstAt = FString::Printf(TEXT("X %.0f, %d degrees"), X, Degrees);
            }
            const float Float = AtmosphereLaw::LogChapmanF32(static_cast<float>(X), static_cast<float>(Cos));
            bFinite &= std::isfinite(Float);
            WorstFloat = FMath::Max(WorstFloat, FMath::Abs(std::exp(static_cast<double>(Float) - Law) - 1.0));
        }
    }
    AddInfo(FString::Printf(TEXT("Chapman against quadrature: worst %.4f%% at %s; float against double worst %.2e"),
        100.0 * Worst, *WorstAt, WorstFloat));
    TestTrue(TEXT("the law's Chapman function is the numerical one within 0.5%, to 89 degrees and 30 below the horizon"), Worst <= 0.005);
    TestTrue(TEXT("finite in float throughout"), bFinite);
    TestTrue(TEXT("float follows double to 1e-3"), WorstFloat <= 1.0e-3);

    // Straight up is the vertical column itself; the airmass is 1.
    TestTrue(TEXT("Ch(850, zenith) is 1 to 1e-3"), FMath::Abs(std::exp(AtmosphereLaw::LogChapmanF64(850.0, 1.0)) - 1.0) < 1.0e-3);

    // A sun deep below the horizon from a giant's air is e^thousands of the
    // vertical column: a logarithm in the thousands, never an infinity.
    const float Deep = AtmosphereLaw::LogChapmanF32(10000.0f, -1.0f);
    TestTrue(FString::Printf(TEXT("a sun straight below, X 10,000, float: a finite log (%.1f)"), Deep), std::isfinite(Deep) && Deep > 9000.0f);
    return true;
}

#endif
```

- [ ] **Step 2: Run it; expect a compile failure.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: FAIL, `'Atmosphere/Atmosphere.h' file not found`.

- [ ] **Step 3: The shared file.** Create `Shaders/Private/Atmosphere.ush`:

```hlsl
// Atmosphere.ush -- a world's air as light crosses it, written once and
// compiled twice (atmospheres spec decision 1; ADR 0006 as landing amends
// it: one text, which a change reaches in both compilers or fails in one).
//
// The GPU will include this file into M_SkyBody's Custom node -- and later
// M_SkyAir, M_SkyAirDome and M_SkyGround -- through the engine's own
// /Project mapping (landing ruling R2: FEngineLoop::PreInit maps /Project to
// <project>/Shaders whenever that directory exists; the project maps nothing
// itself); until orbital slice 1, only the C++ reads it. The
// C++ includes it into Source/DeepSpace/Atmosphere/Atmosphere.cpp twice: in
// double (AtmosphereF64), for what the game computes, and in float
// (AtmosphereF32), the GPU's mirror. A syntax one compiler refuses fails
// that compiler: ./build.sh for the C++, the rendered Eyes.AtmosphereProbe
// (orbital slice 1) for the GPU. A Custom node's HLSL error is invisible to
// the headless suite -- the material ships grey with every test green.
//
// The subset is landing's, with AT_ for WR_: valid HLSL and valid C++ at
// once. No swizzles, no vector or matrix types, no mul(), no implicit vector
// arithmetic, no out parameters: every vector is spelled out a component at
// a time, every multi-valued result is a plain struct, and every literal is
// AT_REAL(...), so the float build computes in float. Only the platform
// halves step outside it.
//
// Units: lengths in radii of the body whose air it is, the surface at R = 1;
// directions unit, in whichever axes the caller uses consistently.
// Radiances in the pi convention of M_SkyBody's Lambert face: a white
// surface lit head-on by the star is 1.

#if defined(AT_CPP)
// -- C++: the shims ---------------------------------------------------------
// Atmosphere.cpp includes <cmath>, then this file inside a namespace with
// AT_REAL defined: once as double, once as float. std:: overloads keep each
// in its own precision.
inline AT_REAL AT_floor(AT_REAL X) { return std::floor(X); }
inline AT_REAL AT_saturate(AT_REAL X) { return X < AT_REAL(0.0) ? AT_REAL(0.0) : (X > AT_REAL(1.0) ? AT_REAL(1.0) : X); }
inline AT_REAL AT_min(AT_REAL A, AT_REAL B) { return A < B ? A : B; }
inline AT_REAL AT_max(AT_REAL A, AT_REAL B) { return A > B ? A : B; }
inline AT_REAL AT_sqrt(AT_REAL X) { return std::sqrt(X); }
inline AT_REAL AT_exp(AT_REAL X) { return std::exp(X); }
inline AT_REAL AT_log(AT_REAL X) { return std::log(X); }
inline AT_REAL AT_cos(AT_REAL X) { return std::cos(X); }
inline AT_REAL AT_sin(AT_REAL X) { return std::sin(X); }
#else
// -- HLSL: the shims --------------------------------------------------------
#pragma once
#define AT_REAL float
#define AT_floor(X) floor(X)
#define AT_saturate(X) saturate(X)
#define AT_min(A, B) min(A, B)
#define AT_max(A, B) max(A, B)
#define AT_sqrt(X) sqrt(X)
#define AT_exp(X) exp(X)
#define AT_log(X) log(X)
#define AT_cos(X) cos(X)
#define AT_sin(X) sin(X)
#endif

// -- Constants ---------------------------------------------------------------
static const AT_REAL AT_PI = AT_REAL(3.14159265358979);

// The smallest number a logarithm or a divisor here is allowed to see: far
// under anything physical, and a normal number in float.
static const AT_REAL AT_TINY = AT_REAL(1.0e-30);

// No optical depth past e^80 is worth carrying: exp(-e^80) is zero in
// either precision, and the cap keeps an infinity out of every sum.
static const AT_REAL AT_MAX_EXPONENT = AT_REAL(80.0);

// -- The Chapman function ----------------------------------------------------
// The airmass of a curved exponential atmosphere: the column along a ray over
// the column straight up from the same point. X is the point's distance from
// the centre in scale heights (R / H); CosZenith is the ray's angle to the
// local vertical. Everything is a logarithm, so float cannot overflow where
// a sun is below the horizon and the column is e^800 of the vertical one:
// every transmittance is exp of one combined exponent (decision 1).

// ln of e^(Y^2) erfc(Y), Y >= 0: Numerical Recipes' erfcc with its e^(-Y^2)
// cancelled, so it neither underflows nor overflows at any Y. Relative error
// under 1.2e-7.
AT_REAL AT_LogErfcx(AT_REAL Y)
{
    AT_REAL T = AT_REAL(1.0) / (AT_REAL(1.0) + AT_REAL(0.5) * Y);
    AT_REAL Poly = AT_REAL(-1.26551223) + T * (AT_REAL(1.00002368) + T * (AT_REAL(0.37409196) + T * (AT_REAL(0.09678418)
        + T * (AT_REAL(-0.18628806) + T * (AT_REAL(0.27886807) + T * (AT_REAL(-1.13520398) + T * (AT_REAL(1.48851587)
        + T * (AT_REAL(-0.82215223) + T * AT_REAL(0.17087277)))))))));
    return AT_log(T) + Poly;
}

// ln Ch(X, z) for a ray at or above the horizon: the leading asymptotic form
// sqrt(pi X / 2) erfcx(sqrt(X / 2) cos z), times its first correction in
// 1 / X, 1 + (1 - 0.625 / (1 + Y / 2)^2) / X. The correction is exact to
// that order at both ends -- 1 / X at the zenith (the column straight up is
// exactly one scale height of surface density) and 3 / (8 X) at the horizon
// (Ch = X e^X K1(X)) -- and within 0.035 / X between. Every world that
// holds its air has X >= 96 (X = 6 lambda^2 for the Jeans lambda, and
// retention starts at lambda 4), where the whole is within 0.05%.
AT_REAL AT_LogChapmanUp(AT_REAL X, AT_REAL CosZenith)
{
    AT_REAL Y = AT_sqrt(AT_REAL(0.5) * X) * AT_max(CosZenith, AT_REAL(0.0));
    AT_REAL Q = AT_REAL(1.0) + AT_REAL(0.5) * Y;
    AT_REAL Correction = AT_REAL(1.0) + (AT_REAL(1.0) - AT_REAL(0.625) / (Q * Q)) / X;
    return AT_REAL(0.5) * AT_log(AT_REAL(0.5) * AT_PI * X) + AT_LogErfcx(Y) + AT_log(Correction);
}

// ln Ch for any ray. Below the horizon the ray passes its lowest point, the
// tangent, at X0 = X sin z, and the column is the whole line through the
// tangent less the part behind the point:
//   Ch = 2 Ch(X0, 90 deg) e^(X - X0) - Ch(X, 180 deg - z),
// exact for an exponential atmosphere. X - X0 is formed as
// X cos^2 z / (1 + sin z), which float keeps where 1 - sin z would cancel;
// the grazing column is sqrt(pi X0 / 2) (1 + 3 / (8 X0)).
AT_REAL AT_LogChapman(AT_REAL X, AT_REAL CosZenith)
{
    if (CosZenith >= AT_REAL(0.0))
    {
        return AT_LogChapmanUp(X, CosZenith);
    }
    AT_REAL Sin = AT_sqrt(AT_max(AT_REAL(1.0) - CosZenith * CosZenith, AT_REAL(0.0)));
    AT_REAL Rise = X * CosZenith * CosZenith / (AT_REAL(1.0) + Sin);
    AT_REAL X0 = AT_max(X - Rise, AT_TINY);
    AT_REAL Grazing = AT_REAL(2.0) * AT_sqrt(AT_REAL(0.5) * AT_PI * X0) * (AT_REAL(1.0) + AT_REAL(0.375) / X0);
    AT_REAL Behind = AT_exp(AT_LogChapmanUp(X, -CosZenith) - Rise);
    return Rise + AT_log(AT_max(Grazing - Behind, AT_TINY));
}
```

- [ ] **Step 4: The C++ side.** Create `Source/DeepSpace/Atmosphere/Atmosphere.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * The air's optics as the game draws them (atmospheres decision 1):
 * Shaders/Private/Atmosphere.ush, the one law, compiled here twice -- in
 * double, what the game computes, and in float, the GPU's mirror, which the
 * rendered probe (orbital slice 1) holds the GPU to and the pure tests hold
 * to the double. AtmosphereReference is what "right" means; this is what
 * draws.
 */
namespace AtmosphereLaw
{
    /** ln of the Chapman function, the .ush's AT_LogChapman: X = R / H,
     *  CosZenith the ray against the local vertical. */
    DEEPSPACE_API double LogChapmanF64(double X, double CosZenith);
    DEEPSPACE_API float LogChapmanF32(float X, float CosZenith);
}
```

  Create `Source/DeepSpace/Atmosphere/Atmosphere.cpp`:

```cpp
#include "Atmosphere/Atmosphere.h"

#include <cmath>

// The shared file, twice. AT_CPP selects its C++ halves and AT_REAL its
// precision; each copy lives in its own namespace, so the two sets of AT_
// symbols never meet. The relative path is the file the GPU will include as
// /Project/Private/Atmosphere.ush: a change to it that C++ cannot compile
// fails ./build.sh.
#define AT_CPP 1
namespace AtmosphereF64
{
#define AT_REAL double
#include "../../../Shaders/Private/Atmosphere.ush"
#undef AT_REAL
}
namespace AtmosphereF32
{
#define AT_REAL float
#include "../../../Shaders/Private/Atmosphere.ush"
#undef AT_REAL
}
#undef AT_CPP

double AtmosphereLaw::LogChapmanF64(double X, double CosZenith)
{
    return AtmosphereF64::AT_LogChapman(X, CosZenith);
}

float AtmosphereLaw::LogChapmanF32(float X, float CosZenith)
{
    return AtmosphereF32::AT_LogChapman(X, CosZenith);
}
```

- [ ] **Step 5: Build and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && ./test.sh DeepSpace.Atmosphere.Chapman; grep -h "Chapman against quadrature" Saved/Logs/DeepSpace.log | tail -1
```

Expected: `passed: 1`, and an info line with a worst error well under 0.5% (planning note 5 estimates 0.05%). If the comparison fails:
- Do not loosen 0.5%, and do not swap the form for another fit.
- Report the printed worst `X` and angle through the orchestrator. The spec's tolerance is the developer's.

- [ ] **Step 6: The whole suite, then commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-optics add Shaders/Private/Atmosphere.ush Source/DeepSpace/Atmosphere/Atmosphere.h Source/DeepSpace/Atmosphere/Atmosphere.cpp Source/DeepSpace/Tests/AtmosphereChapmanTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/air-optics commit -F - <<'MSG'
feat(atmosphere): Atmosphere.ush's first law, the Chapman function, compiled twice

Shaders/Private/Atmosphere.ush in landing's scalar subset with AT_ shims,
compiled into Atmosphere.cpp as AtmosphereF64 and AtmosphereF32, the GPU's
mirror (atmospheres decision 1). The Chapman function is worked in the log
domain, so a sun far below the horizon is a large logarithm and never a
float infinity: the asymptotic erfcx form with its first 1/X correction
(Schueler's closed form misses by 2.5% at 60 degrees), erfcx from NR's
erfcc with the e^-z^2 cancelled, and the tangent identity below the horizon.

DeepSpace.Atmosphere.Chapman: against quadrature within 0.5% to 89 degrees
and 30 below the horizon for X 96-10,000, finite in float throughout.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 7: Prove the test can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush 'AT_REAL(1.00002368)' 'AT_REAL(1.10002368)' DeepSpace.Atmosphere.Chapman
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '(AT_REAL(1.0) + AT_REAL(0.375) / X0)' '(AT_REAL(1.0) + AT_REAL(3.75) / X0)' DeepSpace.Atmosphere.Chapman
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: `KILLED` twice. The first mutant bends `erfcx` everywhere above the horizon. The second bends only the grazing column below it. Mutating a `.ush` deletes every object and rebuilds the module (it is not a `.cpp`), so expect a full build each time.

---
## Task 3 (O3): The reference -- what "right" means

This is the slow truth (decision 1): double precision, 16 wavelengths, exact columns by numerical integration, and second-order scattering by brute force. It is also the only place on the sky's side where physics constants live: Rayleigh's cross-section of air, the ozone band, and the spectral laws of the gas and the aerosol. It never runs in a frame. What it adds beyond second order is the geometric tail at each sample, using its own brute-force transfer fraction `f`. That is the one part of it that is not brute force, and it is what the law's multiple-scattering table approximates. Laws are compared like with like.

The optics tests share their airs through `Tests/AtmosphereTestFixtures.h`. The airs are written out by hand from the spec's decisions 4 and 5 at 1 g. That is test data, not a generator (plan conflict 4), and it is allowed to lag `AirFacts`.

**Files:**
- Create: `Source/DeepSpace/Atmosphere/AtmosphereReference.h`, `Source/DeepSpace/Atmosphere/AtmosphereReference.cpp`
- Create: `Source/DeepSpace/Tests/AtmosphereTestFixtures.h`
- Test: create `Source/DeepSpace/Tests/AtmosphereReferenceTest.cpp` (`DeepSpace.Atmosphere.ReferenceKnownValues`)

**Interfaces:**
- Consumes: `SkyColour::Spectral::{Count, Nm, Planck, ChannelWeights, Luminance, ToLinearSrgb}` (Task 1).
- Produces:

```cpp
struct FAirSpec { double RadiusCm, GasScaleHeightCm, GasTau550, OzoneTau600, AerosolScaleHeightCm, AerosolTau550,
                  AerosolAngstrom, AerosolAsymmetry, AerosolAlbedo450, AerosolAlbedo650; bool HasAir() const; };
namespace AtmosphereReference
{
    inline constexpr double AirTopScaleHeights = 10.0, OzonePeakNm = 600.0, OzoneWidthNm = 70.0;
    struct FSpectrum { double Value[SkyColour::Spectral::Count]; };
    struct FSpectralAir { FSpectrum GasScatter, GasAbsorb, AerosolExtinct, AerosolScatter; double GasH, AerosolH, AerosolG, Top; bool bAir; };
    FSpectralAir Spectral(const FAirSpec& Spec);
    FSpectrum StarSpectrum(double TemperatureK);                     // Planck, luminance 1
    FVector3d ToLinearSrgb(const FSpectrum& Spectrum);
    FVector3d Colour(const FSpectrum& Star, const FSpectrum& Value);  // ToLinearSrgb(Star x Value)
    FVector3d ChannelAverage(const FSpectrum& Star, const FSpectrum& Value);  // Colour over the star's own, floored
    double RayleighCrossSectionAirM2(double Nm);
    double ColumnMoleculesPerM2(double PressureBar, double GravityMS2, double MeanMolecularWeight);
    double ColumnExact(double R, double CosZenith, double H, int32 Intervals = 2000);  // -1 when the ray meets the ground
}
class FReferenceAir
{
public:
    struct FRay { FVector3d Eye; FVector3d Direction; double Length; FVector3d Sun; };
    struct FOptions { int32 ViewSteps; bool bSecondOrder; int32 SecondOrderViewSteps; int32 SphereRings; int32 SphereSegments; int32 SecondarySteps; };
    struct FResult { FVector3d InScatter; FVector3d Transmittance; };
    FReferenceAir(const FAirSpec& Spec, double StarTemperatureK);
    bool HasAir() const;
    const AtmosphereReference::FSpectralAir& GetAir() const;
    FResult Trace(const FRay& Ray) const;
    FResult Trace(const FRay& Ray, const FOptions& Options) const;
    FVector3d SunThrough(const FVector3d& Point, const FVector3d& Sun) const;
    FVector3d MultiScatterWhite(double Altitude01, double CosSunZenith, int32 Rings = 48, int32 Segments = 24, int32 Steps = 96) const;
};
namespace AtmosphereTestFixtures   // Tests/ only
{
    inline constexpr double EarthRadiusCm = 6.3781e8, HomeStarK = 2566.0, SunK = 5772.0, LowBar = 0.05,
        NitrogenOxygenCeilingBar = 1.8013, CarbonDioxideCeilingBar = 1.1710, HydrogenHeliumCeilingBar = 0.8968;
    FAirSpec Airless(); FAirSpec NitrogenOxygen(double Bar); FAirSpec CarbonDioxide(double Bar);
    FAirSpec HydrogenHelium(double Bar); FAirSpec EarthAir(); FAirSpec Giant();
    struct FNamedAir { const TCHAR* Name; FAirSpec Air; };
    TArray<FNamedAir> Extremes();   // each mix at LowBar and at its ceiling
}
```

- [ ] **Step 1: The fixtures.** Create `Source/DeepSpace/Tests/AtmosphereTestFixtures.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Atmosphere/AtmosphereReference.h"

/**
 * Airs written out by hand for the optics tests (plan conflict 4): test
 * data, not a generator, and allowed to lag procgen's AirFacts, which
 * derives the same numbers from a world's facts. Every air is on an
 * Earth-radius world at 1 g. The mixes are the atmospheres spec's decisions
 * 4 and 5 at 1 g: the gas 0.097 per bar at 550 nm times its cross-section
 * relative to air and its column (28.97 / mu); aerosol and ozone per bar.
 * The pressures are the two extremes the law is held at: 0.05 bar, and each
 * mix's ceiling under MaxNadirTau450, where tau at 450 nm is 0.5 straight
 * down. Nothing outside Tests/ may include this.
 */
namespace AtmosphereTestFixtures
{
    inline constexpr double EarthRadiusCm = 6.3781e8;
    inline constexpr double HomeStarK = 2566.0;   // Baemsekai
    inline constexpr double SunK = 5772.0;
    inline constexpr double LowBar = 0.05;
    inline constexpr double NitrogenOxygenCeilingBar = 1.8013;
    inline constexpr double CarbonDioxideCeilingBar = 1.1710;
    inline constexpr double HydrogenHeliumCeilingBar = 0.8968;

    inline FAirSpec Airless()
    {
        FAirSpec Air;
        Air.RadiusCm = EarthRadiusCm;
        return Air;
    }

    /** Earth's mix at 255 K and 1 g: the gas's scale height 7.463 km. */
    inline FAirSpec NitrogenOxygen(double Bar)
    {
        FAirSpec Air;
        Air.RadiusCm = EarthRadiusCm;
        Air.GasScaleHeightCm = 7.463e5;
        Air.GasTau550 = 0.097 * Bar;
        Air.OzoneTau600 = 0.0415 * Bar;
        Air.AerosolScaleHeightCm = 1.2e5;
        Air.AerosolTau550 = 0.05 * Bar;
        Air.AerosolAngstrom = 1.0;
        Air.AerosolAsymmetry = 0.76;
        Air.AerosolAlbedo450 = 0.95;
        Air.AerosolAlbedo650 = 0.95;
        return Air;
    }

    /** Carbon dioxide at 255 K and 1 g: 4.914 km; iron-oxide dust that eats blue. */
    inline FAirSpec CarbonDioxide(double Bar)
    {
        FAirSpec Air;
        Air.RadiusCm = EarthRadiusCm;
        Air.GasScaleHeightCm = 4.914e5;
        Air.GasTau550 = 0.15328 * Bar;
        Air.AerosolScaleHeightCm = 2.0e5;
        Air.AerosolTau550 = 0.08 * Bar;
        Air.AerosolAngstrom = 0.3;
        Air.AerosolAsymmetry = 0.7;
        Air.AerosolAlbedo450 = 0.85;
        Air.AerosolAlbedo650 = 0.95;
        return Air;
    }

    /** Hydrogen and helium on a cold world at 150 K and 1 g: 55.3 km, X = 115. */
    inline FAirSpec HydrogenHelium(double Bar)
    {
        FAirSpec Air;
        Air.RadiusCm = EarthRadiusCm;
        Air.GasScaleHeightCm = 5.53e6;
        Air.GasTau550 = 0.24436 * Bar;
        Air.AerosolScaleHeightCm = 1.0e5;
        Air.AerosolTau550 = 0.01 * Bar;
        Air.AerosolAngstrom = 1.0;
        Air.AerosolAsymmetry = 0.7;
        Air.AerosolAlbedo450 = 0.99;
        Air.AerosolAlbedo650 = 0.99;
        return Air;
    }

    inline FAirSpec EarthAir()
    {
        return NitrogenOxygen(1.0);
    }

    /** A Jupiter at its disc: 11 Earth radii, 2.63 g, 110 K, the gas's
     *  scale height 15.4 km, 2.36 bar of hydrogen and helium above the level
     *  where tau at 450 nm reaches 0.5. */
    inline FAirSpec Giant()
    {
        FAirSpec Air;
        Air.RadiusCm = 11.0 * EarthRadiusCm;
        Air.GasScaleHeightCm = 1.542e6;
        Air.GasTau550 = 0.2192;
        Air.AerosolScaleHeightCm = 1.0e5;
        Air.AerosolTau550 = 0.00896;
        Air.AerosolAngstrom = 1.0;
        Air.AerosolAsymmetry = 0.7;
        Air.AerosolAlbedo450 = 0.99;
        Air.AerosolAlbedo650 = 0.99;
        return Air;
    }

    struct FNamedAir
    {
        const TCHAR* Name = TEXT("");
        FAirSpec Air;
    };

    /** Every mix at both pressure extremes (decision 1's "each mix and
     *  pressure extreme"). */
    inline TArray<FNamedAir> Extremes()
    {
        return {
            {TEXT("N2/O2 at 0.05 bar"), NitrogenOxygen(LowBar)},
            {TEXT("N2/O2 at its ceiling"), NitrogenOxygen(NitrogenOxygenCeilingBar)},
            {TEXT("CO2 at 0.05 bar"), CarbonDioxide(LowBar)},
            {TEXT("CO2 at its ceiling"), CarbonDioxide(CarbonDioxideCeilingBar)},
            {TEXT("H2/He at 0.05 bar"), HydrogenHelium(LowBar)},
            {TEXT("H2/He at its ceiling"), HydrogenHelium(HydrogenHeliumCeilingBar)}};
    }
}
```

- [ ] **Step 2: Write the failing test.** Create `Source/DeepSpace/Tests/AtmosphereReferenceTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAtmosphereReferenceKnownValuesTest,
    "DeepSpace.Atmosphere.ReferenceKnownValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereReferenceTestLocal
{
    double Saturation(const FVector3d& Colour)
    {
        const double Max = FMath::Max3(Colour.X, Colour.Y, Colour.Z);
        const double Min = FMath::Max(FMath::Min3(Colour.X, Colour.Y, Colour.Z), 0.0);
        return Max > 0.0 ? 1.0 - Min / Max : 0.0;
    }
}

bool FAtmosphereReferenceKnownValuesTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereReferenceTestLocal;
    using namespace AtmosphereTestFixtures;

    // -- Earth's air from its physics (decision 1's first known value) -----------
    const double Column = AtmosphereReference::ColumnMoleculesPerM2(1.01325, 9.80665, 28.9647);
    const double Tau = AtmosphereReference::RayleighCrossSectionAirM2(550.0) * Column;
    AddInfo(FString::Printf(TEXT("Earth's air: %.4e molecules per m^2, Rayleigh tau at 550 nm %.4f"), Column, Tau));
    TestTrue(TEXT("Earth's Rayleigh optical depth at 550 nm is 0.097 within 3%"), FMath::Abs(Tau / 0.097 - 1.0) < 0.03);

    // -- The column itself --------------------------------------------------------
    const double H = 7.463e5 / EarthRadiusCm;
    TestTrue(TEXT("straight up from the surface the column is one scale height"),
        FMath::Abs(AtmosphereReference::ColumnExact(1.0, 1.0, H) / H - 1.0) < 1.0e-6);
    TestTrue(TEXT("a ray pointing into the ground has no column: -1"),
        AtmosphereReference::ColumnExact(1.0, -0.1, H) < 0.0);

    // -- Earth's sky under the Sun --------------------------------------------------
    const FReferenceAir Earth(EarthAir(), SunK);
    TestTrue(TEXT("Earth's air has air"), Earth.HasAir());

    // The noon zenith: straight up under a sun 45 degrees high, not
    // overhead -- a sun at the zenith would put its own aureole there.
    FReferenceAir::FRay Zenith;
    Zenith.Eye = FVector3d(0.0, 0.0, 1.0 + 1.0e-6);
    Zenith.Direction = FVector3d(0.0, 0.0, 1.0);
    Zenith.Sun = FVector3d(std::sqrt(0.5), 0.0, std::sqrt(0.5));
    const FVector3d Sky = Earth.Trace(Zenith).InScatter;
    AddInfo(FString::Printf(TEXT("noon zenith under the Sun: (%.4f, %.4f, %.4f)"), Sky.X, Sky.Y, Sky.Z));
    TestTrue(TEXT("the noon zenith is blue over green over red"), Sky.Z > Sky.Y && Sky.Y > Sky.X && Sky.X > 0.0);

    FReferenceAir::FRay Horizon = Zenith;
    Horizon.Direction = FVector3d(0.0, 1.0, 0.0);
    const FVector3d Low = Earth.Trace(Horizon).InScatter;
    AddInfo(FString::Printf(TEXT("the horizon at right angles to the sun: (%.4f, %.4f, %.4f), saturation %.3f against the zenith's %.3f"),
        Low.X, Low.Y, Low.Z, Saturation(Low), Saturation(Sky)));
    TestTrue(TEXT("the horizon is whiter than the zenith"), Saturation(Low) < Saturation(Sky));

    const double Setting = FMath::DegreesToRadians(0.5);
    const FVector3d Sunset = Earth.SunThrough(FVector3d(0.0, 0.0, 1.0), FVector3d(std::cos(Setting), 0.0, std::sin(Setting)));
    const FVector3d Noon = Earth.SunThrough(FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, 1.0));
    AddInfo(FString::Printf(TEXT("the sun's light through the air at noon (%.3f, %.3f, %.3f), at half a degree (%.4f, %.4f, %.4f)"),
        Noon.X, Noon.Y, Noon.Z, Sunset.X, Sunset.Y, Sunset.Z));
    TestTrue(TEXT("a sun at the horizon transmits red over blue"), Sunset.X > Sunset.Z);
    TestTrue(TEXT("and less of its blue than at noon by half and more"), Sunset.Z < 0.5 * Noon.Z);

    const FVector3d Night = Earth.SunThrough(FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, -1.0));
    TestTrue(TEXT("the ground's own shadow: a sun below the ground reaches nothing"), Night.IsZero());

    // -- No air, nothing ---------------------------------------------------------------
    const FReferenceAir None(Airless(), SunK);
    const FReferenceAir::FResult Nothing = None.Trace(Zenith);
    TestFalse(TEXT("an airless world has no air"), None.HasAir());
    TestTrue(TEXT("and adds nothing and dims nothing"),
        Nothing.InScatter.IsZero() && Nothing.Transmittance == FVector3d::OneVector);
    return true;
}

#endif
```

- [ ] **Step 3: Run it; expect a compile failure.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: FAIL, `'Atmosphere/AtmosphereReference.h' file not found`.

- [ ] **Step 4: The reference's header.** Create `Source/DeepSpace/Atmosphere/AtmosphereReference.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Sky/SkyColour.h"

/**
 * One world's air as physics describes it: what the optics are built from.
 * Plain data, in centimetres and nadir optical depths. Procgen's facts
 * reach it through one adapter (PlanetAir::SpecOf, once the draws exist);
 * the tests write their airs out by hand. Airless when HasAir() is false.
 */
struct DEEPSPACE_API FAirSpec
{
    /** The body's radius, cm: every length the optics use is in these. */
    double RadiusCm = 0.0;

    /** The gas (Rayleigh) falls by e every this many cm. 0 is airless. */
    double GasScaleHeightCm = 0.0;

    /** Straight down through the whole gas, at 550 nm; (550 / lambda)^4
     *  elsewhere. */
    double GasTau550 = 0.0;

    /** The ozone-like absorber straight down at its Chappuis peak, 600 nm,
     *  shaped as a Gaussian 70 nm wide (sigma), carried on the gas's
     *  profile. 0 is none. */
    double OzoneTau600 = 0.0;

    /** The aerosol (Mie): its scale height, and its extinction straight
     *  down at 550 nm, going as (lambda / 550)^-Angstrom. */
    double AerosolScaleHeightCm = 0.0;
    double AerosolTau550 = 0.0;
    double AerosolAngstrom = 1.0;

    /** The aerosol's Henyey-Greenstein asymmetry g, forward-scattering
     *  above 0. */
    double AerosolAsymmetry = 0.0;

    /** Its single-scatter albedo at 450 and 650 nm, linear between and held
     *  beyond: CO2's iron-oxide dust absorbs more blue than red. */
    double AerosolAlbedo450 = 1.0;
    double AerosolAlbedo650 = 1.0;

    bool HasAir() const
    {
        return RadiusCm > 0.0 && GasScaleHeightCm > 0.0 && (GasTau550 > 0.0 || OzoneTau600 > 0.0 || AerosolTau550 > 0.0);
    }
};

/**
 * What "right" means for the air (atmospheres decision 1): double
 * precision, 16 wavelengths, exact columns by numerical integration, second
 * order by brute force. Slow, and never run in a frame. The only place on
 * the sky's side physics constants live -- the air's Rayleigh
 * cross-section, the ozone band, the gas's and the aerosol's spectral laws
 * -- so the law's channel fit (FAtmosphere::Build) takes its spectra from
 * here.
 *
 * Lengths in radii of the body (the surface at R = 1); radiance in the pi
 * convention of M_SkyBody's Lambert face, for a star of unit luminance.
 */
namespace AtmosphereReference
{
    /** The air's drawn top: ten of the gas's scale heights, where its
     *  density is e^-10 of the surface's. */
    inline constexpr double AirTopScaleHeights = 10.0;

    inline constexpr double OzonePeakNm = 600.0;
    inline constexpr double OzoneWidthNm = 70.0;

    /** A value at each of SkyColour::Spectral's 16 wavelengths. */
    struct FSpectrum
    {
        double Value[SkyColour::Spectral::Count] = {};
    };

    /** An air per wavelength: nadir optical depths, heights in radii. */
    struct FSpectralAir
    {
        FSpectrum GasScatter;
        FSpectrum GasAbsorb;
        FSpectrum AerosolExtinct;
        FSpectrum AerosolScatter;
        double GasH = 0.0;
        double AerosolH = 0.0;
        double AerosolG = 0.0;
        double Top = 0.0;
        bool bAir = false;
    };

    /** The spec's spectral laws applied to an air. An air with no aerosol
     *  is still given a profile for it -- the gas's -- so nothing downstream
     *  divides by a zero height. */
    DEEPSPACE_API FSpectralAir Spectral(const FAirSpec& Spec);

    /** A blackbody at the 16 wavelengths, scaled to unit luminance (CIE Y),
     *  its temperature clamped as SkyColour::Blackbody clamps it. */
    DEEPSPACE_API FSpectrum StarSpectrum(double TemperatureK);

    /** SkyColour::Spectral::ToLinearSrgb of a spectrum. */
    DEEPSPACE_API FVector3d ToLinearSrgb(const FSpectrum& Spectrum);

    /** The linear sRGB of the star's light times Value, wavelength by
     *  wavelength. */
    DEEPSPACE_API FVector3d Colour(const FSpectrum& Star, const FSpectrum& Value);

    /** Colour(Star, Value) over the star's own colour, channel by channel:
     *  what a three-channel renderer must multiply the star's light by to
     *  get Value's effect on it. The star's channel is floored at 1e-4 of
     *  its brightest, so a 1,000 K star's near-absent blue cannot divide
     *  anything into infinity. */
    DEEPSPACE_API FVector3d ChannelAverage(const FSpectrum& Star, const FSpectrum& Value);

    /** Rayleigh scattering cross-section of Earth's air per molecule, m^2:
     *  24 pi^3 / (lambda^4 N_s^2) ((n^2 - 1) / (n^2 + 2))^2 F_K, with Peck
     *  and Reeder's refractivity, Bates's King factors, and Loschmidt's
     *  number at 288.15 K. */
    DEEPSPACE_API double RayleighCrossSectionAirM2(double Nm);

    /** Molecules per m^2 above a surface: P / (mu m_u g). */
    DEEPSPACE_API double ColumnMoleculesPerM2(double PressureBar, double GravityMS2, double MeanMolecularWeight);

    /**
     * The column along a ray from a point R radii from the centre toward
     * CosZenith, per unit density at the surface, for a constituent falling
     * by e every H radii, in radii; by Simpson's rule on Intervals steps in a
     * variable crowded where the density peaks, out to 80 scale heights above
     * the ray's lowest point. -1 when the ray meets the ground first.
     */
    DEEPSPACE_API double ColumnExact(double R, double CosZenith, double H, int32 Intervals = 2000);
}

/** One air under one star, ready to be traced. Building it tabulates the
 *  exact columns once, for the second order's many sun paths. */
class DEEPSPACE_API FReferenceAir
{
public:
    struct FRay
    {
        FVector3d Eye = FVector3d(0.0, 0.0, 1.0);
        FVector3d Direction = FVector3d(0.0, 0.0, 1.0);
        double Length = 1.0e30;
        /** Unit, toward the star; the zero vector for no star. */
        FVector3d Sun = FVector3d(0.0, 0.0, 1.0);
    };

    /** The second order's sphere is SphereRings rings, equal steps in T
     *  with cos(zenith) = T |T| -- crowded at the horizon, where the long
     *  paths and most of the light are -- by SphereSegments of azimuth. */
    struct FOptions
    {
        int32 ViewSteps = 256;
        bool bSecondOrder = true;
        int32 SecondOrderViewSteps = 24;
        int32 SphereRings = 24;
        int32 SphereSegments = 12;
        int32 SecondarySteps = 32;
    };

    struct FResult
    {
        /** Linear sRGB, pi convention, a star of unit luminance. */
        FVector3d InScatter = FVector3d::ZeroVector;
        /** Per channel, relative to the star's own light. */
        FVector3d Transmittance = FVector3d::OneVector;
    };

    FReferenceAir(const FAirSpec& Spec, double StarTemperatureK);

    bool HasAir() const { return Air.bAir; }
    const AtmosphereReference::FSpectralAir& GetAir() const { return Air; }

    /** The light the air adds along the ray, first and second order and the
     *  geometric tail beyond, and what it lets through. Two overloads rather
     *  than a defaulted FOptions: a nested struct's member initializers are
     *  not usable in a default argument inside its own class. */
    FResult Trace(const FRay& Ray) const;
    FResult Trace(const FRay& Ray, const FOptions& Options) const;

    /** The star's light reaching Point, per channel, relative to its own:
     *  exact columns, and 0 in the ground's shadow. An airless world passes
     *  all of it; no star passes none. */
    FVector3d SunThrough(const FVector3d& Point, const FVector3d& Sun) const;

    /**
     * The reference's multiple-scattering source at Altitude01 of the air's
     * depth under a sun at CosSunZenith, for a sun of unit light at every
     * wavelength: pi times the mean first-order radiance over the sphere,
     * over 1 - f, weighted into channels by the star's spectrum
     * (ChannelAverage). What the law's table texel approximates.
     */
    FVector3d MultiScatterWhite(double Altitude01, double CosSunZenith, int32 Rings = 48, int32 Segments = 24, int32 Steps = 96) const;

private:
    static constexpr int32 TableAltitudes = 64;
    static constexpr int32 TableCosines = 128;

    struct FSecond
    {
        AtmosphereReference::FSpectrum IntoViewGas;
        AtmosphereReference::FSpectrum IntoViewAerosol;
        AtmosphereReference::FSpectrum Mean;
        AtmosphereReference::FSpectrum Transfer;
    };

    AtmosphereReference::FSpectralAir Air;
    AtmosphereReference::FSpectrum Star;
    TArray<double> LogGasColumn;
    TArray<double> LogAerosolColumn;

    double LogColumnLookup(const TArray<double>& Table, double R, double Cos, double H) const;
    AtmosphereReference::FSpectrum SunExact(double R, double Cos) const;
    AtmosphereReference::FSpectrum SunLookup(double R, double Cos) const;
    FSecond SecondOrderAt(const FVector3d& Point, const FVector3d& View, const FVector3d& Sun, int32 Rings, int32 Segments, int32 Steps) const;
};
```

- [ ] **Step 5: The reference.** Create `Source/DeepSpace/Atmosphere/AtmosphereReference.cpp`:

```cpp
#include "Atmosphere/AtmosphereReference.h"

#include <cmath>
#include <limits>

namespace AtmosphereReferenceLocal
{
    using AtmosphereReference::FSpectrum;
    using SkyColour::Spectral::Count;

    constexpr double LoschmidtPerM3 = 2.546899e25;
    constexpr double AtomicMassKg = 1.66053906660e-27;
    constexpr double FourPi = 4.0 * UE_DOUBLE_PI;

    /** Standard air's refractivity, n - 1, at 288.15 K and 1013.25 hPa:
     *  Peck and Reeder (1972). */
    double RefractivityAir(double Nm)
    {
        const double InvUm2 = 1.0 / FMath::Square(Nm * 1.0e-3);
        return (8060.51 + 2480990.0 / (132.274 - InvUm2) + 17455.7 / (39.32957 - InvUm2)) * 1.0e-8;
    }

    /** Air's King factor, by volume: N2 and O2 by Bates (1984), argon 1,
     *  carbon dioxide 1.15. */
    double KingFactorAir(double Nm)
    {
        const double InvUm2 = 1.0 / FMath::Square(Nm * 1.0e-3);
        const double N2 = 1.034 + 3.17e-4 * InvUm2;
        const double O2 = 1.096 + 1.385e-3 * InvUm2 + 1.448e-4 * InvUm2 * InvUm2;
        return (78.084 * N2 + 20.946 * O2 + 0.934 * 1.0 + 0.036 * 1.15) / (78.084 + 20.946 + 0.934 + 0.036);
    }

    double RayleighPhase(double Cos)
    {
        return 3.0 / (16.0 * UE_DOUBLE_PI) * (1.0 + Cos * Cos);
    }

    double HenyeyGreenstein(double Cos, double G)
    {
        const double Den = 1.0 + G * G - 2.0 * G * Cos;
        return (1.0 - G * G) / (4.0 * UE_DOUBLE_PI * Den * std::sqrt(Den));
    }

    /** The integral of F(t) for t = T0 + Sign L u^2, Simpson in u. */
    template <typename TF>
    double Mapped(TF F, double T0, double Sign, double L, int32 Intervals)
    {
        const double Step = 1.0 / Intervals;
        double Sum = 0.0;
        for (int32 I = 0; I <= Intervals; ++I)
        {
            const double U = I * Step;
            const double Weight = (I == 0 || I == Intervals) ? 1.0 : ((I % 2) == 1 ? 4.0 : 2.0);
            Sum += Weight * F(T0 + Sign * L * U * U) * 2.0 * L * U;
        }
        return Sum * Step / 3.0;
    }

    /** The ray's part in the air, measured along it from its closest
     *  approach to the centre: exactly the law's geometry, in double. */
    struct FPath
    {
        double B = 0.0;
        FVector3d C = FVector3d::ZeroVector;
        FVector3d D = FVector3d::ZeroVector;
        double S0 = 1.0;
        double S1 = 0.0;
        bool IsEmpty() const { return !(S1 > S0); }
    };

    FPath PathThroughAir(FVector3d E, const FVector3d& D, double Length, double Top)
    {
        FPath Path;
        Path.D = D;
        const double E2 = E.SizeSquared();
        if (E2 < 1.0)
        {
            if (E2 <= 0.0)
            {
                return Path;
            }
            E /= std::sqrt(E2);
        }
        const FVector3d W = FVector3d::CrossProduct(E, D);
        Path.C = FVector3d::CrossProduct(D, W);
        Path.B = W.Size();
        const double SE = FVector3d::DotProduct(E, D);
        const double TopR = 1.0 + Top;
        if (Path.B >= TopR || !(Length > 0.0))
        {
            return Path;
        }
        const double Half = std::sqrt((TopR - Path.B) * (TopR + Path.B));
        double S0 = FMath::Max(SE, -Half);
        double S1 = FMath::Min(SE + Length, Half);
        if (Path.B < 1.0)
        {
            // As the law decides it: by the direction, not by the eye's
            // distance against the chord, which rounding decides for an eye
            // on the surface looking along it.
            const double Ground = std::sqrt((1.0 - Path.B) * (1.0 + Path.B));
            if (SE < -Ground)
            {
                S1 = FMath::Min(S1, -Ground);
            }
            else if (SE < 0.0)
            {
                S1 = S0;
            }
        }
        Path.S0 = S0;
        Path.S1 = S1;
        return Path;
    }

    struct FStep
    {
        double S = 0.0;
        double DS = 0.0;
    };

    /** Samples along the path in travel order: split at the closest point,
     *  spaced as u^2 from it, N in all -- the law's placement, finer. */
    TArray<FStep> Steps(const FPath& Path, int32 N)
    {
        TArray<FStep> Out;
        if (Path.IsEmpty() || N < 2)
        {
            return Out;
        }
        const auto Piece = [&Out](double Low, double Far, int32 Samples, bool bTowardLow)
        {
            const double Span = Far - Low;
            const double Abs = std::fabs(Span);
            for (int32 K = 0; K < Samples; ++K)
            {
                const int32 I = bTowardLow ? Samples - 1 - K : K;
                const double U = (I + 0.5) / Samples;
                Out.Add({Low + Span * U * U, Abs * 2.0 * U / Samples});
            }
        };
        if (Path.S0 >= 0.0)
        {
            Piece(Path.S0, Path.S1, N, false);
        }
        else if (Path.S1 <= 0.0)
        {
            Piece(Path.S1, Path.S0, N, true);
        }
        else
        {
            const int32 Behind = FMath::Clamp(FMath::RoundToInt32(N * (-Path.S0) / (Path.S1 - Path.S0)), 1, N - 1);
            Piece(0.0, Path.S0, Behind, true);
            Piece(0.0, Path.S1, N - Behind, false);
        }
        return Out;
    }

    /** One direction of the product rule: ring I of Rings (cos(zenith) =
     *  T |T|, T at the ring's centre in [-1, 1]), segment J of Segments, about
     *  the local vertical Up; and its share of the whole sphere. */
    struct FDirection
    {
        FVector3d W = FVector3d::ZeroVector;
        double Share = 0.0;
    };

    FDirection SphereDirection(const FVector3d& Up, int32 I, int32 Rings, int32 J, int32 Segments)
    {
        const FVector3d Across = FMath::Abs(Up.Z) < 0.9 ? FVector3d(0.0, 0.0, 1.0) : FVector3d(1.0, 0.0, 0.0);
        const FVector3d East = FVector3d::CrossProduct(Across, Up).GetSafeNormal();
        const FVector3d North = FVector3d::CrossProduct(Up, East);
        const double T = -1.0 + (2.0 * I + 1.0) / Rings;
        const double Mu = T * std::fabs(T);
        const double Ring = std::sqrt(FMath::Max(1.0 - Mu * Mu, 0.0));
        const double Phi = 2.0 * UE_DOUBLE_PI * (J + 0.5) / Segments;
        FDirection Out;
        Out.W = East * (Ring * std::cos(Phi)) + North * (Ring * std::sin(Phi)) + Up * Mu;
        Out.Share = 2.0 * std::fabs(T) / (static_cast<double>(Rings) * Segments);
        return Out;
    }

    bool InShadow(double R, double Cos)
    {
        return Cos < 0.0 && R * R * FMath::Max(1.0 - Cos * Cos, 0.0) < 1.0;
    }
}

AtmosphereReference::FSpectralAir AtmosphereReference::Spectral(const FAirSpec& Spec)
{
    FSpectralAir Air;
    if (!Spec.HasAir())
    {
        return Air;
    }
    Air.bAir = true;
    Air.GasH = Spec.GasScaleHeightCm / Spec.RadiusCm;
    const bool bAerosol = Spec.AerosolTau550 > 0.0 && Spec.AerosolScaleHeightCm > 0.0;
    Air.AerosolH = bAerosol ? Spec.AerosolScaleHeightCm / Spec.RadiusCm : Air.GasH;
    Air.AerosolG = FMath::Clamp(Spec.AerosolAsymmetry, 0.0, 0.95);
    Air.Top = AirTopScaleHeights * Air.GasH;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        const double Nm = SkyColour::Spectral::Nm(I);
        Air.GasScatter.Value[I] = Spec.GasTau550 * std::pow(550.0 / Nm, 4.0);
        Air.GasAbsorb.Value[I] = Spec.OzoneTau600 * std::exp(-0.5 * FMath::Square((Nm - OzonePeakNm) / OzoneWidthNm));
        Air.AerosolExtinct.Value[I] = bAerosol ? Spec.AerosolTau550 * std::pow(Nm / 550.0, -Spec.AerosolAngstrom) : 0.0;
        const double Along = FMath::Clamp((Nm - 450.0) / 200.0, 0.0, 1.0);
        Air.AerosolScatter.Value[I] = Air.AerosolExtinct.Value[I] * FMath::Lerp(Spec.AerosolAlbedo450, Spec.AerosolAlbedo650, Along);
    }
    return Air;
}

AtmosphereReference::FSpectrum AtmosphereReference::StarSpectrum(double TemperatureK)
{
    FSpectrum Star;
    double Luminance = 0.0;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        Star.Value[I] = SkyColour::Spectral::Planck(SkyColour::Spectral::Nm(I), TemperatureK);
        Luminance += Star.Value[I] * SkyColour::Spectral::Luminance(I);
    }
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        Star.Value[I] /= Luminance;
    }
    return Star;
}

FVector3d AtmosphereReference::ToLinearSrgb(const FSpectrum& Spectrum)
{
    return SkyColour::Spectral::ToLinearSrgb(MakeArrayView(Spectrum.Value, SkyColour::Spectral::Count));
}

FVector3d AtmosphereReference::Colour(const FSpectrum& Star, const FSpectrum& Value)
{
    FSpectrum Product;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        Product.Value[I] = Star.Value[I] * Value.Value[I];
    }
    return ToLinearSrgb(Product);
}

FVector3d AtmosphereReference::ChannelAverage(const FSpectrum& Star, const FSpectrum& Value)
{
    const FVector3d Own = ToLinearSrgb(Star);
    const double Floor = 1.0e-4 * FMath::Max3(Own.X, Own.Y, Own.Z);
    const FVector3d Seen = Colour(Star, Value);
    return FVector3d(Seen.X / FMath::Max(Own.X, Floor), Seen.Y / FMath::Max(Own.Y, Floor), Seen.Z / FMath::Max(Own.Z, Floor));
}

double AtmosphereReference::RayleighCrossSectionAirM2(double Nm)
{
    using namespace AtmosphereReferenceLocal;
    const double Metres = Nm * 1.0e-9;
    const double N = 1.0 + RefractivityAir(Nm);
    const double Ratio = (N * N - 1.0) / (N * N + 2.0);
    return 24.0 * FMath::Cube(UE_DOUBLE_PI) * Ratio * Ratio / (std::pow(Metres, 4.0) * LoschmidtPerM3 * LoschmidtPerM3) * KingFactorAir(Nm);
}

double AtmosphereReference::ColumnMoleculesPerM2(double PressureBar, double GravityMS2, double MeanMolecularWeight)
{
    using namespace AtmosphereReferenceLocal;
    return PressureBar * 1.0e5 / (MeanMolecularWeight * AtomicMassKg * GravityMS2);
}

double AtmosphereReference::ColumnExact(double R, double CosZenith, double H, int32 Intervals)
{
    using namespace AtmosphereReferenceLocal;
    const double Cos = FMath::Clamp(CosZenith, -1.0, 1.0);
    const double Sin2 = FMath::Max(1.0 - Cos * Cos, 0.0);
    if (InShadow(R, Cos))
    {
        return -1.0;
    }
    // The density relative to the ray's lowest point, so no term exceeds 1;
    // that point's own density is put back at the end.
    const double Lowest = Cos < 0.0 ? R * std::sqrt(Sin2) : R;
    const auto Density = [R, Cos, H, Lowest](double T) { return std::exp(-(std::sqrt(R * R + 2.0 * R * T * Cos + T * T) - Lowest) / H); };
    const double Far = Lowest + 80.0 * H;
    const int32 N = FMath::Max(2, Intervals + (Intervals & 1));
    double Sum = 0.0;
    if (Cos < 0.0)
    {
        const double Tangent = -R * Cos;
        Sum += Mapped(Density, Tangent, -1.0, Tangent, N);
        Sum += Mapped(Density, Tangent, 1.0, std::sqrt(Far * Far - Lowest * Lowest), N);
    }
    else
    {
        Sum += Mapped(Density, 0.0, 1.0, std::sqrt(Far * Far - R * R * Sin2) - R * Cos, N);
    }
    return std::exp(-(Lowest - 1.0) / H) * Sum;
}

FReferenceAir::FReferenceAir(const FAirSpec& Spec, double StarTemperatureK)
    : Air(AtmosphereReference::Spectral(Spec))
    , Star(AtmosphereReference::StarSpectrum(StarTemperatureK))
{
    if (!Air.bAir)
    {
        return;
    }
    // ln of the exact column over altitude and angle, for the second
    // order's sun paths; +infinity where the ray meets the ground, and never
    // below -700, where a column is nothing and a logarithm must stay a
    // number to be interpolated.
    const int32 Cells = TableAltitudes * TableCosines;
    LogGasColumn.SetNumUninitialized(Cells);
    LogAerosolColumn.SetNumUninitialized(Cells);
    for (int32 J = 0; J < TableAltitudes; ++J)
    {
        const double R = 1.0 + Air.Top * J / (TableAltitudes - 1);
        for (int32 I = 0; I < TableCosines; ++I)
        {
            const double Cos = -1.0 + 2.0 * I / (TableCosines - 1);
            const double Gas = AtmosphereReference::ColumnExact(R, Cos, Air.GasH, 400);
            const double Aerosol = AtmosphereReference::ColumnExact(R, Cos, Air.AerosolH, 400);
            LogGasColumn[J * TableCosines + I] = Gas < 0.0 ? std::numeric_limits<double>::infinity() : FMath::Max(std::log(FMath::Max(Gas, 1.0e-300)), -700.0);
            LogAerosolColumn[J * TableCosines + I] = Aerosol < 0.0 ? std::numeric_limits<double>::infinity() : FMath::Max(std::log(FMath::Max(Aerosol, 1.0e-300)), -700.0);
        }
    }
}

double FReferenceAir::LogColumnLookup(const TArray<double>& Table, double R, double Cos, double H) const
{
    const double FY = FMath::Clamp((R - 1.0) / Air.Top, 0.0, 1.0) * (TableAltitudes - 1);
    const double FX = FMath::Clamp((Cos + 1.0) * 0.5, 0.0, 1.0) * (TableCosines - 1);
    const int32 Y0 = FMath::Min(FMath::FloorToInt32(FY), TableAltitudes - 2);
    const int32 X0 = FMath::Min(FMath::FloorToInt32(FX), TableCosines - 2);
    const double TY = FY - Y0;
    const double TX = FX - X0;
    const double C00 = Table[Y0 * TableCosines + X0];
    const double C01 = Table[Y0 * TableCosines + X0 + 1];
    const double C10 = Table[(Y0 + 1) * TableCosines + X0];
    const double C11 = Table[(Y0 + 1) * TableCosines + X0 + 1];
    if (!std::isfinite(C00) || !std::isfinite(C01) || !std::isfinite(C10) || !std::isfinite(C11))
    {
        // Beside the ground's shadow the table has no neighbours to blend:
        // integrate this one exactly.
        const double Exact = AtmosphereReference::ColumnExact(R, Cos, H, 400);
        return Exact < 0.0 ? std::numeric_limits<double>::infinity() : FMath::Max(std::log(FMath::Max(Exact, 1.0e-300)), -700.0);
    }
    return FMath::Lerp(FMath::Lerp(C00, C01, TX), FMath::Lerp(C10, C11, TX), TY);
}

AtmosphereReference::FSpectrum FReferenceAir::SunExact(double R, double Cos) const
{
    AtmosphereReference::FSpectrum Out;
    if (AtmosphereReferenceLocal::InShadow(R, Cos))
    {
        return Out;
    }
    const double Gas = AtmosphereReference::ColumnExact(R, Cos, Air.GasH, 1000);
    const double Aerosol = AtmosphereReference::ColumnExact(R, Cos, Air.AerosolH, 1000);
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        const double Tau = (Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I]) / Air.GasH * Gas + Air.AerosolExtinct.Value[I] / Air.AerosolH * Aerosol;
        Out.Value[I] = std::exp(-Tau);
    }
    return Out;
}

AtmosphereReference::FSpectrum FReferenceAir::SunLookup(double R, double Cos) const
{
    AtmosphereReference::FSpectrum Out;
    if (AtmosphereReferenceLocal::InShadow(R, Cos))
    {
        return Out;
    }
    const double Gas = std::exp(LogColumnLookup(LogGasColumn, R, Cos, Air.GasH));
    const double Aerosol = std::exp(LogColumnLookup(LogAerosolColumn, R, Cos, Air.AerosolH));
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        const double Tau = (Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I]) / Air.GasH * Gas + Air.AerosolExtinct.Value[I] / Air.AerosolH * Aerosol;
        Out.Value[I] = std::exp(-Tau);
    }
    return Out;
}

FReferenceAir::FSecond FReferenceAir::SecondOrderAt(const FVector3d& Point, const FVector3d& View, const FVector3d& Sun, int32 Rings, int32 Segments, int32 StepsPerRay) const
{
    using namespace AtmosphereReferenceLocal;
    FSecond Out;
    const FVector3d Up = Point.GetSafeNormal();
    for (int32 D = 0; D < Rings * Segments; ++D)
    {
        const FDirection Direction = SphereDirection(Up, D / Segments, Rings, D % Segments, Segments);
        const FVector3d& W = Direction.W;
        const double Solid = FourPi * Direction.Share;
        const FPath Ray = PathThroughAir(Point, W, 1.0e30, Air.Top);
        // Light from the sun, scattered once at Q toward Point: it travels
        // along -W, so its angle to the sunlight's -Sun is Sun . W.
        const double CosSun = FVector3d::DotProduct(Sun, W);
        const double PhaseGas = RayleighPhase(CosSun);
        const double PhaseAerosol = HenyeyGreenstein(CosSun, Air.AerosolG);
        FSpectrum Arriving;
        FSpectrum Transfer;
        FSpectrum Depth;
        for (const FStep& Step : Steps(Ray, StepsPerRay))
        {
            const FVector3d Q = Ray.C + Step.S * Ray.D;
            const double RQ = std::sqrt(Ray.B * Ray.B + Step.S * Step.S);
            const double GasDensity = std::exp(-(RQ - 1.0) / Air.GasH);
            const double AerosolDensity = std::exp(-(RQ - 1.0) / Air.AerosolH);
            const FSpectrum SunAtQ = SunLookup(RQ, FVector3d::DotProduct(Q, Sun) / RQ);
            for (int32 I = 0; I < Count; ++I)
            {
                const double GasScatter = Air.GasScatter.Value[I] / Air.GasH * GasDensity;
                const double AerosolScatter = Air.AerosolScatter.Value[I] / Air.AerosolH * AerosolDensity;
                const double DTau = ((Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I]) / Air.GasH * GasDensity
                    + Air.AerosolExtinct.Value[I] / Air.AerosolH * AerosolDensity) * Step.DS;
                const double Through = std::exp(-(Depth.Value[I] + 0.5 * DTau));
                Arriving.Value[I] += (GasScatter * PhaseGas + AerosolScatter * PhaseAerosol) * SunAtQ.Value[I] * Through * Step.DS;
                Transfer.Value[I] += (GasScatter + AerosolScatter) * Through * Step.DS;
                Depth.Value[I] += DTau;
            }
        }
        // Scattered again at Point into the view: light arriving from W
        // travels along -W, and leaves along -View toward the eye.
        const double CosView = FVector3d::DotProduct(W, View);
        const double IntoGas = RayleighPhase(CosView) * Solid;
        const double IntoAerosol = HenyeyGreenstein(CosView, Air.AerosolG) * Solid;
        for (int32 I = 0; I < Count; ++I)
        {
            Out.IntoViewGas.Value[I] += IntoGas * Arriving.Value[I];
            Out.IntoViewAerosol.Value[I] += IntoAerosol * Arriving.Value[I];
            Out.Mean.Value[I] += Direction.Share * Arriving.Value[I];
            Out.Transfer.Value[I] += Direction.Share * Transfer.Value[I];
        }
    }
    return Out;
}

FReferenceAir::FResult FReferenceAir::Trace(const FRay& In) const
{
    return Trace(In, FOptions());
}

FReferenceAir::FResult FReferenceAir::Trace(const FRay& In, const FOptions& Options) const
{
    using namespace AtmosphereReferenceLocal;
    FResult Result;
    if (!Air.bAir)
    {
        return Result;
    }
    const FVector3d D = In.Direction.GetSafeNormal();
    const bool bSun = In.Sun.SizeSquared() > 0.25;
    const FVector3d S = bSun ? In.Sun.GetSafeNormal() : FVector3d::ZeroVector;
    const FPath Path = PathThroughAir(In.Eye, D, In.Length, Air.Top);
    if (Path.IsEmpty())
    {
        return Result;
    }

    const double CosView = FVector3d::DotProduct(S, D);
    const double PhaseGas = RayleighPhase(CosView);
    const double PhaseAerosol = HenyeyGreenstein(CosView, Air.AerosolG);
    FSpectrum Light;
    FSpectrum Depth;

    // First order: fine samples in travel order, the view's transmittance
    // accumulated, the sun's by exact integration at every sample.
    for (const FStep& Step : Steps(Path, Options.ViewSteps))
    {
        const FVector3d P = Path.C + Step.S * Path.D;
        const double R = std::sqrt(Path.B * Path.B + Step.S * Step.S);
        const double GasDensity = std::exp(-(R - 1.0) / Air.GasH);
        const double AerosolDensity = std::exp(-(R - 1.0) / Air.AerosolH);
        const FSpectrum SunAtP = bSun ? SunExact(R, FVector3d::DotProduct(P, S) / R) : FSpectrum();
        for (int32 I = 0; I < Count; ++I)
        {
            const double DTau = ((Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I]) / Air.GasH * GasDensity
                + Air.AerosolExtinct.Value[I] / Air.AerosolH * AerosolDensity) * Step.DS;
            const double Through = std::exp(-(Depth.Value[I] + 0.5 * DTau));
            Light.Value[I] += (Air.GasScatter.Value[I] / Air.GasH * GasDensity * PhaseGas
                + Air.AerosolScatter.Value[I] / Air.AerosolH * AerosolDensity * PhaseAerosol) * SunAtP.Value[I] * Through * Step.DS;
            Depth.Value[I] += DTau;
        }
    }

    // Second order by brute force, and the geometric tail beyond it with
    // the reference's own transfer fraction f, on a coarser path.
    if (bSun && Options.bSecondOrder)
    {
        FSpectrum Coarse;
        for (const FStep& Step : Steps(Path, Options.SecondOrderViewSteps))
        {
            const FVector3d P = Path.C + Step.S * Path.D;
            const double R = std::sqrt(Path.B * Path.B + Step.S * Step.S);
            const double GasDensity = std::exp(-(R - 1.0) / Air.GasH);
            const double AerosolDensity = std::exp(-(R - 1.0) / Air.AerosolH);
            const FSecond Second = SecondOrderAt(P, D, S, Options.SphereRings, Options.SphereSegments, Options.SecondarySteps);
            for (int32 I = 0; I < Count; ++I)
            {
                const double GasScatter = Air.GasScatter.Value[I] / Air.GasH * GasDensity;
                const double AerosolScatter = Air.AerosolScatter.Value[I] / Air.AerosolH * AerosolDensity;
                const double DTau = ((Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I]) / Air.GasH * GasDensity
                    + Air.AerosolExtinct.Value[I] / Air.AerosolH * AerosolDensity) * Step.DS;
                const double Through = std::exp(-(Coarse.Value[I] + 0.5 * DTau));
                const double F = FMath::Min(Second.Transfer.Value[I], 0.999);
                const double Tail = (GasScatter + AerosolScatter) * Second.Mean.Value[I] * F / (1.0 - F);
                Light.Value[I] += (GasScatter * Second.IntoViewGas.Value[I] + AerosolScatter * Second.IntoViewAerosol.Value[I] + Tail) * Through * Step.DS;
                Coarse.Value[I] += DTau;
            }
        }
    }

    FSpectrum Through;
    for (int32 I = 0; I < Count; ++I)
    {
        Through.Value[I] = std::exp(-Depth.Value[I]);
    }
    Result.InScatter = UE_DOUBLE_PI * AtmosphereReference::Colour(Star, Light);
    Result.Transmittance = AtmosphereReference::ChannelAverage(Star, Through);
    return Result;
}

FVector3d FReferenceAir::SunThrough(const FVector3d& Point, const FVector3d& Sun) const
{
    if (!Air.bAir)
    {
        return FVector3d::OneVector;
    }
    if (Sun.SizeSquared() < 0.25)
    {
        return FVector3d::ZeroVector;
    }
    const double Length = Point.Size();
    const double R = FMath::Max(Length, 1.0);
    const double Cos = FVector3d::DotProduct(Point, Sun.GetSafeNormal()) / FMath::Max(Length, 1.0e-300);
    return AtmosphereReference::ChannelAverage(Star, SunExact(R, Cos));
}

FVector3d FReferenceAir::MultiScatterWhite(double Altitude01, double CosSunZenith, int32 Rings, int32 Segments, int32 StepsPerRay) const
{
    using namespace AtmosphereReferenceLocal;
    if (!Air.bAir)
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d Point(0.0, 0.0, 1.0 + FMath::Clamp(Altitude01, 0.0, 1.0) * Air.Top);
    const FVector3d Sun(std::sqrt(FMath::Max(1.0 - CosSunZenith * CosSunZenith, 0.0)), 0.0, CosSunZenith);
    // The View only weights IntoView*, which this does not read.
    const FSecond Second = SecondOrderAt(Point, FVector3d(0.0, 0.0, 1.0), Sun, Rings, Segments, StepsPerRay);
    FSpectrum Psi;
    for (int32 I = 0; I < Count; ++I)
    {
        const double F = FMath::Min(Second.Transfer.Value[I], 0.999);
        Psi.Value[I] = UE_DOUBLE_PI * Second.Mean.Value[I] / (1.0 - F);
    }
    return AtmosphereReference::ChannelAverage(Star, Psi);
}
```

- [ ] **Step 6: Build and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && ./test.sh DeepSpace.Atmosphere.ReferenceKnownValues; grep -hE "Earth's air:|noon zenith|the horizon at noon|through the air at noon" Saved/Logs/DeepSpace.log | tail -4
```

Expected: `passed: 1`. The four info lines give Earth's tau (about 0.0969), the zenith (blue over green over red), the horizon's saturation under the zenith's, and the sunset's red over blue.

- [ ] **Step 7: The whole suite, then commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-optics add Source/DeepSpace/Atmosphere/AtmosphereReference.h Source/DeepSpace/Atmosphere/AtmosphereReference.cpp Source/DeepSpace/Tests/AtmosphereTestFixtures.h Source/DeepSpace/Tests/AtmosphereReferenceTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/air-optics commit -F - <<'MSG'
feat(atmosphere): the reference -- 16 wavelengths, exact columns, second order by brute force

FReferenceAir is what "right" means for the air (atmospheres decision 1):
double precision, the star's blackbody at 16 wavelengths through the
gas's (550/lambda)^4, the aerosol's Angstrom law and the ozone band, exact
columns by numerical integration at every first-order sample, second
order by brute force over a 24 x 12 sphere crowded at the horizon, and the
geometric tail beyond it
with its own transfer fraction. The ground's shadow is hard. The only place
on the sky's side physics constants live: air's Rayleigh cross-section
(Peck and Reeder, Bates) reproduces Earth's 0.097 at 550 nm.

DeepSpace.Atmosphere.ReferenceKnownValues: Earth's tau within 3%; the
zenith blue over green over red; the horizon whiter; a setting sun red over
blue. Tests/AtmosphereTestFixtures.h: the mixes at 0.05 bar and their
ceilings, a Jupiter, and no air, written out by hand.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 8: Prove the test can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Atmosphere/AtmosphereReference.cpp 'Air.GasScatter.Value[I] = Spec.GasTau550 * std::pow(550.0 / Nm, 4.0);' 'Air.GasScatter.Value[I] = Spec.GasTau550;' DeepSpace.Atmosphere.ReferenceKnownValues
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Atmosphere/AtmosphereReference.cpp 'return 24.0 * FMath::Cube(UE_DOUBLE_PI)' 'return 12.0 * FMath::Cube(UE_DOUBLE_PI)' DeepSpace.Atmosphere.ReferenceKnownValues
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: `KILLED` twice. Grey scattering loses the blue zenith. Half the cross-section misses Earth's tau.

---
## Tasks 4-8, re-planned for ruling 3 (read first)

Tasks 4, 5 and 6 were built for three channels (`262b3f5`, `d56a0e7`, `f60611b`, `42e49f6`), and Task 7's Steps 1-4 (`5027df5`), on `feat/air-optics`. Atmosphere plan ruling 3 chose the spec's named fallback: spectral bins in the law. So the three built tasks are replaced here, not added to. Each re-planned task starts from the built code and replaces what it names, whole files where the change is wide. Tasks 1-3 stand, except for three small, named edits to the reference (Tasks 5 and 8).

What the re-plan measured (planning note 13 has the numbers), in the plan's harness, against the reference over decision 1's grid at ruling 2's ceilings:
- **No partition into four, five or six bins meets 5% or 1e-3 everywhere.** The best six still miss a red dwarf's blue through a long horizontal CO2 path by 4.4 times the allowance. Eight bins, narrow in the blue, meet it: the tightest check, the bins' transmittance alone, reads 0.92 of its allowance. Eight is past ruling 3's four to six, so **Task 4 is gated on ruling 6**.
- **Bins were not the only cause.** The thin airs' misses were the march's: a midpoint rule over 12 samples cannot see the haze's 1 km scale height under H2/He's 55 km, nor a horizon through a ceiling air. Task 5 replaces it with a march that integrates each span exactly for an exponential, on the Chapman columns the nodes already hold, with one extra node where the sunlight climbs out of a grazing column. It keeps the 12 samples as its nodes (planning note 14).
- **The multiple-scattering table misread the sun at the horizon.** Its columns were equal steps in the sun's cosine, and the light the air hands on changes fastest right there. Task 6 crowds its columns toward the horizon (planning note 15).
- **What remains is the spec's own approximation.** Hillaire's multiple scattering is isotropic, and the reference's second order is not. The law meets the reference with its second scattering sent every way alike, within 5% or 1e-3 everywhere (worst 0.80 of the allowance). Against the full second order it stays within 25% or 1e-3 (worst 0.75 of that). **Task 8 is gated on ruling 7**, which holds the law to the first and pins the second (planning note 16). While measuring, the reference was found up to 5% short of its own converged value on backlit horizons (12 azimuth segments). Task 8 raises them to 24.

The other rulings fold in as follows:
- **Ruling 1** gives `.StarColour` its bounds (Task 7, Steps 5-9).
- **Ruling 2** lowers the fixtures' ceilings (Task 4, Step 2) and fixes Task 12's guarantee at 0.32.
- **Ruling 4** is `AtmosphereLaw::NoonSun`, already built.
- **Ruling 5** gives `.HomothetyInvariance` its tolerances (Task 8).

The two corrections the builder reported are in:
- `.BacklitRing`'s strengthening (`f60611b`) stands, and planning note 10 says what it pins.
- The swatch's mutation (Task 7, Step 3). UE 5.8.2 writes a PNG for an unknown extension, so `.nonesuch` proved nothing.

---
## Task 4 (O4, re-planned): The bins -- the law's spectrum, and the fixtures at ruling 2's ceilings

The law will carry eight spectral bins rather than three channels (rulings 3 and 6). This task builds the bins themselves, pure and on their own, before anything uses them:
- **The partition:** `AtmosphereBins::First`, eight runs of the reference's 16 wavelengths: 400-420, 440, 460-480, 500, 520-540, 560-580, 600 and 620-700 nm.
- **The average (`Average`):** each bin's value is its wavelengths' mean, weighted by `Weight`. `Weight` is the star's light times the length of the wavelength's linear sRGB vector. Luminance weighting would all but ignore the blue: measured, it misses by 22 times.
- **The fold:**
  - `Fold`: the star's light in a bin, in linear sRGB.
  - `FoldLight` and `FoldThrough`: bins back to colour. `FoldThrough` folds a transmittance as 1 less what is lost, as the `.ush` will, so that nothing lost is exactly 1.

The bins' one approximation is that one optical depth stands for several within a bin. `.BinsCarryTheSpectrum` holds it alone, along every path from the ground. It first lowers the fixtures' ceilings to ruling 2's 0.32, which every later agreement is measured at.

**Files:**
- Modify: `Source/DeepSpace/Tests/AtmosphereTestFixtures.h` (Step 2: the three ceilings, the giant, two comments)
- Create: `Source/DeepSpace/Atmosphere/AtmosphereBins.h`, `Source/DeepSpace/Atmosphere/AtmosphereBins.cpp`
- Test: create `Source/DeepSpace/Tests/AtmosphereBinsTest.cpp` (`DeepSpace.Atmosphere.BinFolds`, `DeepSpace.Atmosphere.BinsCarryTheSpectrum`)

**Interfaces:**
- Consumes: `AtmosphereReference::{FSpectrum, FSpectralAir, Spectral, StarSpectrum, ToLinearSrgb, Colour, ChannelAverage, ColumnExact}` (Task 3); `SkyColour::Spectral::{Count, ChannelWeights}` (Task 1).
- Produces:

```cpp
namespace AtmosphereBins
{
    inline constexpr int32 Count = 8;
    inline constexpr int32 First[Count + 1] = {0, 2, 3, 5, 6, 8, 10, 11, 16};
    struct FBins { double Value[Count] = {}; };
    double Weight(const AtmosphereReference::FSpectrum& Star, int32 Index);
    FBins Average(const AtmosphereReference::FSpectrum& Star, const AtmosphereReference::FSpectrum& Value);
    FVector3d Fold(const AtmosphereReference::FSpectrum& Star, int32 Bin);
    FVector3d FoldLight(const AtmosphereReference::FSpectrum& Star, const FBins& Light);
    FVector3d FoldThrough(const AtmosphereReference::FSpectrum& Star, const FBins& Through);
}
namespace AtmosphereTestFixtures   // Tests/ only
{
    inline constexpr double NitrogenOxygenCeilingBar = 1.1528, CarbonDioxideCeilingBar = 0.7494, HydrogenHeliumCeilingBar = 0.5740;
    inline constexpr double CoolestStarK = 2400.0;   // procgen's coolest star: the M band's floor
}
```

- [ ] **Step 1: The gate: ruling 6.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && git merge -q main && grep -c '^\*\*Atmosphere plan ruling 6 ' docs/superpowers/specs/2026-09-27-atmospheres-design.md
```

Expected: `1`. If `0`, stop: the ruling is not recorded, and nothing in Tasks 4-8 runs until it is. Read it.
- **Ruling 6 accepts eight bins:** go on.
- **Ruling 6 holds to six:** stop and report. Task 4's partition becomes `{0, 3, 5, 6, 9, 11, 16}`, measured the best six, and `Count` becomes 6. Task 5's HLSL hook then reads a 64 x 32 texture with its right half's `b` and `a` unused, and `.BinsCarryTheSpectrum` and every agreement need a tolerance the ruling states. That re-plan comes first.

- [ ] **Step 2: The fixtures at ruling 2's ceilings.** Every ceiling and the giant's disc scale with the guarantee, since the gas's and the aerosol's depths both go with `P`, so each is its 0.5 value times 0.64. In `Source/DeepSpace/Tests/AtmosphereTestFixtures.h`:
  - Replace `    inline constexpr double NitrogenOxygenCeilingBar = 1.8013;` with `    inline constexpr double NitrogenOxygenCeilingBar = 1.1528;`.
  - Replace `    inline constexpr double CarbonDioxideCeilingBar = 1.1710;` with `    inline constexpr double CarbonDioxideCeilingBar = 0.7494;`.
  - Replace `    inline constexpr double HydrogenHeliumCeilingBar = 0.8968;` with `    inline constexpr double HydrogenHeliumCeilingBar = 0.5740;`.
  - After `    inline constexpr double SunK = 5772.0;`, add `    inline constexpr double CoolestStarK = 2400.0;   // procgen's coolest star: ClassBands' M floor (StarSystemGenerator.cpp)`. The home star and the Sun are what the partition was chosen against; this is the coolest star the game makes (planning note 13).
  - In `Giant()`, replace `        Air.GasTau550 = 0.2192;` with `        Air.GasTau550 = 0.1403;`, and `        Air.AerosolTau550 = 0.00896;` with `        Air.AerosolTau550 = 0.005734;`.
  - In the file's opening comment, replace

```cpp
 * mix's ceiling under MaxNadirTau450, where tau at 450 nm is 0.5 straight
 * down. Nothing outside Tests/ may include this.
```

    with

```cpp
 * mix's ceiling under MaxNadirTau450, where tau at 450 nm is 0.32 straight
 * down (atmosphere plan ruling 2; at 0.5 they were 1.8013, 1.1710 and
 * 0.8968 bar, and every ceiling scales with the guarantee). Nothing
 * outside Tests/ may include this.
```

  - In `Giant()`'s comment, replace `2.36 bar of hydrogen and helium above the level` and the line after it, `     *  where tau at 450 nm reaches 0.5. */`, with `1.51 bar of hydrogen and helium above the level` and `     *  where tau at 450 nm reaches 0.32 (ruling 2; 2.36 bar at 0.5). */`.

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-optics add Source/DeepSpace/Tests/AtmosphereTestFixtures.h
git -C /home/matt/Development/deepspace/.worktrees/air-optics commit -F - <<'MSG'
test(atmosphere): the fixtures' ceilings at ruling 2's guarantee

Atmosphere plan ruling 2 lowers MaxNadirTau450 from 0.5 to 0.32, so each
mix's ceiling air and the giant's disc -- the extremes decision 1's grid
holds the law at -- scale by 0.64: N2/O2 1.1528 bar, CO2 0.7494, H2/He
0.5740, a Jupiter's disc 1.51 bar. The guarantee itself moves in Task 12.
CoolestStarK names procgen's coolest star, 2,400 K, for the bins' tests.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

Expected: the whole suite green, as before (the harness ran every built optics test on the new ceilings), then the commit.

- [ ] **Step 3: Write the failing tests.** Create `Source/DeepSpace/Tests/AtmosphereBinsTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Atmosphere/AtmosphereBins.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereBinFoldsTest, "DeepSpace.Atmosphere.BinFolds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereBinsCarryTheSpectrumTest, "DeepSpace.Atmosphere.BinsCarryTheSpectrum",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereBinsTestLocal
{
    using AtmosphereReference::FSpectrum;
    using SkyColour::Spectral::Count;

    /** How far Got is from Want, over max(Relative |Want|, Absolute). */
    double Over(double Got, double Want, double Relative, double Absolute)
    {
        return std::isfinite(Got) ? FMath::Abs(Got - Want) / FMath::Max(Relative * FMath::Abs(Want), Absolute) : 1.0e30;
    }
}

bool FAtmosphereBinFoldsTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereBinsTestLocal;
    using AtmosphereBins::First;

    // The partition: every wavelength in exactly one bin, in order.
    bool bContiguous = First[0] == 0 && First[AtmosphereBins::Count] == Count;
    for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
    {
        bContiguous &= First[Bin + 1] > First[Bin];
    }
    TestTrue(TEXT("the bins cover the 16 wavelengths, each once, none empty"), bContiguous);

    for (const double Kelvin : {0.0, 1000.0, 2000.0, 2566.0, 5772.0, 15000.0, 40000.0})
    {
        const FSpectrum Star = AtmosphereReference::StarSpectrum(Kelvin);
        const FVector3d Own = AtmosphereReference::ToLinearSrgb(Star);

        // The folds are the star's light, split: they sum to its colour.
        FVector3d Sum = FVector3d::ZeroVector;
        bool bWeights = true;
        for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
        {
            Sum += AtmosphereBins::Fold(Star, Bin);
        }
        for (int32 I = 0; I < Count; ++I)
        {
            const double W = AtmosphereBins::Weight(Star, I);
            bWeights &= std::isfinite(W) && W > 0.0;
        }
        TestTrue(FString::Printf(TEXT("%.0f K: the folds sum to the star's colour"), Kelvin), (Sum - Own).GetAbsMax() <= 1.0e-12 * Own.GetAbsMax());
        TestTrue(FString::Printf(TEXT("%.0f K: every wavelength weighs something, and something finite"), Kelvin), bWeights);

        // Nothing lost is all of it kept, exactly.
        AtmosphereBins::FBins Ones;
        for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
        {
            Ones.Value[Bin] = 1.0;
        }
        const FVector3d Kept = AtmosphereBins::FoldThrough(Star, Ones);
        TestTrue(FString::Printf(TEXT("%.0f K: a clear path keeps all of the star's light"), Kelvin), (Kept - FVector3d::OneVector).GetAbsMax() < 1.0e-12);

        // A spectrum constant within each bin is carried exactly: averaged
        // to itself, and folded to what the reference integrates.
        FSpectrum Stepped;
        for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
        {
            for (int32 I = First[Bin]; I < First[Bin + 1]; ++I)
            {
                Stepped.Value[I] = 0.1 * (Bin + 1);
            }
        }
        const AtmosphereBins::FBins Averaged = AtmosphereBins::Average(Star, Stepped);
        bool bAveraged = true;
        for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
        {
            bAveraged &= FMath::Abs(Averaged.Value[Bin] - 0.1 * (Bin + 1)) < 1.0e-12;
        }
        TestTrue(FString::Printf(TEXT("%.0f K: a stepped spectrum averages to its steps"), Kelvin), bAveraged);
        const FVector3d Light = AtmosphereBins::FoldLight(Star, Averaged);
        const FVector3d Want = AtmosphereReference::Colour(Star, Stepped);
        TestTrue(FString::Printf(TEXT("%.0f K: and folds to the reference's colour of it"), Kelvin), (Light - Want).GetAbsMax() <= 1.0e-12 * Want.GetAbsMax());
        // The reference's own fold (ChannelAverage), wherever the star has a
        // channel's light to keep: under 2,000 K its blue is below the
        // floor, where the fold keeps all of nothing and the reference a
        // fraction of it.
        if (Kelvin >= 2000.0)
        {
            const FVector3d Through = AtmosphereBins::FoldThrough(Star, Averaged);
            const FVector3d ThroughWant = AtmosphereReference::ChannelAverage(Star, Stepped);
            TestTrue(FString::Printf(TEXT("%.0f K: and to its transmittance"), Kelvin), (Through - ThroughWant).GetAbsMax() < 1.0e-12);
        }
    }
    return true;
}

bool FAtmosphereBinsCarryTheSpectrumTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereBinsTestLocal;
    using namespace AtmosphereTestFixtures;

    // The bins' one approximation, alone: within a bin one optical depth
    // stands for several. Decision 1's airs under the home star, procgen's
    // coolest star and the Sun, along every path from the ground --
    // straight up to the horizon, each constituent through its own exact
    // column -- the binned transmittance against the spectrum's, each
    // channel within decision 1's 5% or 1e-3, as a display shows it: a
    // channel below zero counts as zero on both sides, and how often that
    // changed a value is counted and printed (planning note 17).
    double Worst = 0.0;
    FString Where;
    int32 Checked = 0;
    int32 Clamped = 0;
    for (const FNamedAir& Named : Extremes())
    {
        const AtmosphereReference::FSpectralAir Air = AtmosphereReference::Spectral(Named.Air);
        FSpectrum GasTau;
        for (int32 I = 0; I < Count; ++I)
        {
            GasTau.Value[I] = Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I];
        }
        for (const double Kelvin : {HomeStarK, CoolestStarK, SunK})
        {
            const FSpectrum Star = AtmosphereReference::StarSpectrum(Kelvin);
            const AtmosphereBins::FBins Gas = AtmosphereBins::Average(Star, GasTau);
            const AtmosphereBins::FBins Aerosol = AtmosphereBins::Average(Star, Air.AerosolExtinct);
            for (const double FromZenith : {0.0, 60.0, 80.0, 85.0, 88.0, 90.0})
            {
                // Airmasses: each constituent's column along the path over
                // its column straight up.
                const double Cos = FMath::Cos(FMath::DegreesToRadians(FromZenith));
                const double GasAirmass = AtmosphereReference::ColumnExact(1.0, Cos, Air.GasH) / Air.GasH;
                const double AerosolAirmass = AtmosphereReference::ColumnExact(1.0, Cos, Air.AerosolH) / Air.AerosolH;
                FSpectrum Through;
                for (int32 I = 0; I < Count; ++I)
                {
                    Through.Value[I] = std::exp(-(GasAirmass * GasTau.Value[I] + AerosolAirmass * Air.AerosolExtinct.Value[I]));
                }
                AtmosphereBins::FBins BinThrough;
                for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
                {
                    BinThrough.Value[Bin] = std::exp(-(GasAirmass * Gas.Value[Bin] + AerosolAirmass * Aerosol.Value[Bin]));
                }
                const FVector3d Want = AtmosphereReference::ChannelAverage(Star, Through);
                const FVector3d Got = AtmosphereBins::FoldThrough(Star, BinThrough);
                for (int32 C = 0; C < 3; ++C)
                {
                    ++Checked;
                    Clamped += (Got[C] < 0.0 || Want[C] < 0.0) ? 1 : 0;
                    const double Miss = Over(FMath::Max(Got[C], 0.0), FMath::Max(Want[C], 0.0), 0.05, 1.0e-3);
                    if (Miss > Worst)
                    {
                        Worst = Miss;
                        Where = FString::Printf(TEXT("%s, %.0f K, %.0f degrees from the zenith, channel %d: %.5f against %.5f"),
                            Named.Name, Kelvin, FromZenith, C, Got[C], Want[C]);
                    }
                }
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d binned transmittances, %d changed by the gamut clamp; the worst at %.2f of its allowance: %s"), Checked, Clamped, Worst, *Where));
    TestEqual(TEXT("six airs, three stars, six paths, three channels"), Checked, 6 * 3 * 6 * 3);
    TestTrue(TEXT("every channel within 5% or 1e-3 of the spectrum's"), Worst <= 1.0);
    return true;
}

#endif
```

- [ ] **Step 4: Run it; expect a compile failure.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: FAIL, `'Atmosphere/AtmosphereBins.h' file not found`.

- [ ] **Step 5: The bins.** Create `Source/DeepSpace/Atmosphere/AtmosphereBins.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Atmosphere/AtmosphereReference.h"

/**
 * The law's spectrum (atmosphere plan ruling 3): the reference's 16
 * wavelengths gathered into Count contiguous bins, the air one number per
 * bin, and each bin's light folded into linear sRGB only at the end, by the
 * star's own light in that bin. Pure: spectra in, bins out.
 * FAtmosphere::Build fills the law's air from these, and the .ush's AT_BINS
 * is Count.
 *
 * Why these bins: the air's optical depth runs as lambda^-4, so the bins
 * are narrow in the blue, where it changes fastest, and wide in the red.
 * They were chosen by measurement against the reference over decision 1's
 * grid (atmosphere optics plan, planning note 13): no partition into four,
 * five or six bins met decision 1's 5% or 1e-3 everywhere -- the best six
 * still missed a red dwarf's blue through a long horizontal CO2 path by 4.4
 * times its allowance -- and these eight meet it, the tightest check
 * (.BinsCarryTheSpectrum) at 0.92 of its allowance (atmosphere plan
 * ruling 6).
 */
namespace AtmosphereBins
{
    inline constexpr int32 Count = 8;

    /** The index (SkyColour::Spectral::Nm) of each bin's first wavelength,
     *  and after the last the count of wavelengths: 400-420, 440, 460-480,
     *  500, 520-540, 560-580, 600 and 620-700 nm. */
    inline constexpr int32 First[Count + 1] = {0, 2, 3, 5, 6, 8, 10, 11, 16};

    /** A value per bin. */
    struct FBins
    {
        double Value[Count] = {};
    };

    /** What a wavelength counts for inside its bin: the star's light there
     *  times the length of the wavelength's linear sRGB vector
     *  (SkyColour::Spectral::ChannelWeights) -- how far it can move any
     *  channel, not only luminance, which would all but ignore the blue.
     *  Positive for every star in SkyColour's range. */
    DEEPSPACE_API double Weight(const AtmosphereReference::FSpectrum& Star, int32 Index);

    /** Value averaged over each bin by Weight: exact in the optically thin
     *  limit for anything linear in the spectrum -- scattering, an optical
     *  depth. */
    DEEPSPACE_API FBins Average(const AtmosphereReference::FSpectrum& Star, const AtmosphereReference::FSpectrum& Value);

    /** The star's light in bin Bin, in linear sRGB at the spectrum's own
     *  scale: the sum over the bin's wavelengths of the star times
     *  ChannelWeights. The Count folds sum to ToLinearSrgb(Star). A channel
     *  can be negative: the matching functions reach outside sRGB's gamut. */
    DEEPSPACE_API FVector3d Fold(const AtmosphereReference::FSpectrum& Star, int32 Bin);

    /** Light per bin, for unit light in each, folded into linear sRGB: the
     *  sum of Fold(Star, b) times Light[b]. */
    DEEPSPACE_API FVector3d FoldLight(const AtmosphereReference::FSpectrum& Star, const FBins& Light);

    /** A transmittance per bin, folded as the reference folds one
     *  (AtmosphereReference::ChannelAverage): the star's light it keeps over
     *  the star's light, channel by channel, the star's channel floored at
     *  1e-4 of its brightest. */
    DEEPSPACE_API FVector3d FoldThrough(const AtmosphereReference::FSpectrum& Star, const FBins& Through);
}
```

  Create `Source/DeepSpace/Atmosphere/AtmosphereBins.cpp`:

```cpp
#include "Atmosphere/AtmosphereBins.h"

#include <cmath>

double AtmosphereBins::Weight(const AtmosphereReference::FSpectrum& Star, int32 Index)
{
    return Star.Value[Index] * SkyColour::Spectral::ChannelWeights(Index).Size();
}

AtmosphereBins::FBins AtmosphereBins::Average(const AtmosphereReference::FSpectrum& Star, const AtmosphereReference::FSpectrum& Value)
{
    FBins Out;
    for (int32 Bin = 0; Bin < Count; ++Bin)
    {
        double Sum = 0.0;
        double Total = 0.0;
        for (int32 I = First[Bin]; I < First[Bin + 1]; ++I)
        {
            Sum += Weight(Star, I) * Value.Value[I];
            Total += Weight(Star, I);
        }
        Out.Value[Bin] = Total > 0.0 ? Sum / Total : 0.0;
    }
    return Out;
}

FVector3d AtmosphereBins::Fold(const AtmosphereReference::FSpectrum& Star, int32 Bin)
{
    FVector3d Out = FVector3d::ZeroVector;
    for (int32 I = First[Bin]; I < First[Bin + 1]; ++I)
    {
        Out += SkyColour::Spectral::ChannelWeights(I) * Star.Value[I];
    }
    return Out;
}

FVector3d AtmosphereBins::FoldLight(const AtmosphereReference::FSpectrum& Star, const FBins& Light)
{
    FVector3d Out = FVector3d::ZeroVector;
    for (int32 Bin = 0; Bin < Count; ++Bin)
    {
        Out += Fold(Star, Bin) * Light.Value[Bin];
    }
    return Out;
}

FVector3d AtmosphereBins::FoldThrough(const AtmosphereReference::FSpectrum& Star, const FBins& Through)
{
    // As the .ush's AT_FoldThrough: 1 less what is lost, so nothing lost is
    // exactly 1, even in a channel the star all but lacks.
    const FVector3d Own = AtmosphereReference::ToLinearSrgb(Star);
    const double Floor = FMath::Max(1.0e-4 * FMath::Max3(Own.X, Own.Y, Own.Z), 1.0e-30);
    FBins Lost;
    for (int32 Bin = 0; Bin < Count; ++Bin)
    {
        Lost.Value[Bin] = 1.0 - Through.Value[Bin];
    }
    const FVector3d Gone = FoldLight(Star, Lost);
    return FVector3d(1.0 - Gone.X / FMath::Max(Own.X, Floor), 1.0 - Gone.Y / FMath::Max(Own.Y, Floor), 1.0 - Gone.Z / FMath::Max(Own.Z, Floor));
}
```

- [ ] **Step 6: Build and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && ./test.sh DeepSpace.Atmosphere.BinFolds && ./test.sh DeepSpace.Atmosphere.BinsCarryTheSpectrum; grep -h "binned transmittances" Saved/Logs/DeepSpace.log | tail -1
```

Expected: `passed: 1` twice. Over the home star and the Sun, the harness measured the info line's worst at 0.92 of its allowance, for CO2 at its ceiling under the home star, 88 degrees from the zenith, in the green channel. The 2,400 K star was not in the harness, so its figure is new; the line names the worst case whichever star it is under. If `.BinsCarryTheSpectrum` fails at 2,400 K alone, stop and report the info line: that is ruling 6's question again (planning note 13), not a tolerance to loosen. Report the gamut-clamp count as well (planning note 17).

- [ ] **Step 7: The whole suite, then commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-optics add Source/DeepSpace/Atmosphere/AtmosphereBins.h Source/DeepSpace/Atmosphere/AtmosphereBins.cpp Source/DeepSpace/Tests/AtmosphereBinsTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/air-optics commit -F - <<'MSG'
feat(atmosphere): the law's spectrum -- eight bins, averaged and folded back to colour

AtmosphereBins (atmosphere plan rulings 3 and 6): the reference's 16
wavelengths as eight contiguous bins, narrow in the blue where lambda^-4
changes fastest -- 400-420, 440, 460-480, 500, 520-540, 560-580, 600,
620-700 nm. A bin's value is its wavelengths' mean weighted by the star's
light times the length of each wavelength's sRGB vector (luminance
weighting all but ignores the blue); a bin's light goes back to linear
sRGB through the star's own light in it, and a transmittance as 1 less
what is lost. Measured against the reference over decision 1's grid, no
four, five or six bins met 5% or 1e-3 everywhere; these eight do.

DeepSpace.Atmosphere.BinFolds holds the partition and the folds exact;
DeepSpace.Atmosphere.BinsCarryTheSpectrum holds the one approximation --
one depth for several -- within 5% or 1e-3 along every path from the
ground, under the home star, procgen's coolest (2,400 K) and the Sun,
every mix at both extremes.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 8: Prove the tests can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Atmosphere/AtmosphereBins.cpp '    return Star.Value[Index] * SkyColour::Spectral::ChannelWeights(Index).Size();' '    return Star.Value[Index] * SkyColour::Spectral::Luminance(Index);' DeepSpace.Atmosphere.BinsCarryTheSpectrum
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Atmosphere/AtmosphereBins.cpp '        Out += SkyColour::Spectral::ChannelWeights(I) * Star.Value[I];' '        Out += SkyColour::Spectral::ChannelWeights(I);' DeepSpace.Atmosphere.BinFolds
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: `KILLED` twice.
- The first weighs the bins by luminance. The harness measured H2/He's blue at 80 degrees 22 times over its allowance.
- The second folds the eye's response without the star, so the folds no longer sum to the star's colour.

---
## Task 5 (O5, re-planned): The law over the bins -- the air, the march, the fold

The built law carries three channels in `AT_Air`. This task re-cuts every function over `AT_Air` at once, since the struct's layout is what they share. It has no intermediate green state, so it is one task.

**The air.** `AT_Air` holds, per bin and colourless:
- the gas's and the aerosol's scattering and extinction per radius;
- `FoldR`, `FoldG` and `FoldB`: the star's light in each bin, in linear sRGB.

The sums of the folds are the star's colour. The shader still never sees a temperature (decision 3), and there is no white air: the table is built from the air itself, and the star's colour enters once, in the fold.

**The fold.** `AT_FoldLight` and `AT_FoldThrough` turn bins into colour at the end, so the entry points still return linear sRGB, and their signatures (decision 1) are unchanged.

**The march** (planning note 14). The 12 samples become 12 nodes. The piece either side of the closest point keeps its `u^2` spacing, now with both ends as nodes. Between two nodes, each constituent's light is its exact column there, times the logarithmic mean of what it gathers per unit column:
- The column is the difference of the Chapman columns the nodes already hold, so it costs nothing.
- The logarithmic mean is `(B - A) / ln(B / A)`, exact for anything that falls exponentially with the column: the density itself, and the view's transmittance through one constituent. A midpoint rule is not, where a scale height or an optical depth is short against the spacing. That is what missed in the thin airs and on the grazing paths.
- A span whose single scattering changes by more than `e^4` takes one more node halfway: sunlight climbing out of a grazing column, or out of the ground's shadow, rises faster than exponentially. Over the whole grid no ray takes more than 14 nodes, and a dusk sky takes 12 everywhere.

**The table** holds a value per bin: 32 x 32 texels of eight values. On the GPU that is a 64 x 32 RGBA16F texture, bins 0-3 in its left half and 4-7 in its right. Task 6 changes only how its columns are spaced.

Three tests change:
- `.ChannelFit` becomes `.BinnedAir`.
- `.StarTemperatureExtremes` holds the bins' coefficients and folds.
- `.MultiScatterTable` holds each bin against the reference's source, per wavelength, averaged into it. So the reference gains `MultiScatterSpectrum`.

`.SingleScatteringMatchesReference` is new. It holds the march and the bins together, with the table out: the law with no table against the reference's first order, over decision 1's whole grid, both builds, at decision 1's 5% or 1e-3. That grid moves into the fixtures, where Task 8's agreement will read it too.

`AtmosphereLawTest.cpp`'s nine tests are untouched and must pass as they stand, `.BacklitRing` strengthened as built.

**Files:**
- Modify: `Shaders/Private/Atmosphere.ush` (from `// -- The air ---` to the end: replaced)
- Modify: `Source/DeepSpace/Atmosphere/Atmosphere.h`, `Source/DeepSpace/Atmosphere/Atmosphere.cpp` (whole files below)
- Modify: `Source/DeepSpace/Atmosphere/AtmosphereReference.h`, `Source/DeepSpace/Atmosphere/AtmosphereReference.cpp` (`MultiScatterSpectrum`)
- Modify: `Source/DeepSpace/Tests/AtmosphereTestFixtures.h` (append `FRayCase` and `Grid`)
- Test: replace `Source/DeepSpace/Tests/AtmosphereBuildTest.cpp` (`DeepSpace.Atmosphere.BinnedAir`, `DeepSpace.Atmosphere.StarTemperatureExtremes`) and `Source/DeepSpace/Tests/AtmosphereTableTest.cpp` (`DeepSpace.Atmosphere.MultiScatterTable`); create `Source/DeepSpace/Tests/AtmosphereMarchTest.cpp` (`DeepSpace.Atmosphere.SingleScatteringMatchesReference`)

**Interfaces:**
- Consumes: `AtmosphereBins::{Count, First, FBins, Weight, Average, Fold}` (Task 4); `AT_LogChapman` (Task 2); `FReferenceAir` (Task 3).
- Produces, in the `.ush` (every other `AT_` function keeps its signature, over the new `AT_Air`):

```hlsl
static const int AT_BINS = 8;
struct AT_Air { AT_REAL GasScatter[AT_BINS]; AT_REAL GasExtinct[AT_BINS]; AT_REAL AerosolScatter[AT_BINS]; AT_REAL AerosolExtinct[AT_BINS];
                AT_REAL FoldR[AT_BINS]; AT_REAL FoldG[AT_BINS]; AT_REAL FoldB[AT_BINS]; AT_REAL GasH, AerosolH, AerosolG, Top; };
struct AT_Bins { AT_REAL V[AT_BINS]; };
struct AT_March { AT_REAL L[AT_BINS]; AT_REAL T[AT_BINS]; AT_REAL F[AT_BINS]; };
AT_Bins AT_MultiScatter(AT_Air A, AT_REAL Altitude01, AT_REAL CosSunZenith AT_TABLE_PARAM);   // the hook, per platform
AT_Rgb  AT_FoldLight(AT_Air A, AT_Bins L);
AT_Rgb  AT_FoldThrough(AT_Air A, AT_Bins T);
AT_REAL AT_LogMean(AT_REAL A, AT_REAL B);
AT_Bins AT_MultiScatterCell(AT_Air A, AT_REAL Altitude01, AT_REAL CosSunZenith AT_TABLE_PARAM);
```

- And in C++:

```cpp
struct FAtmosphereAir { AtmosphereBins::FBins GasScatter, GasExtinct, AerosolScatter, AerosolExtinct; FVector3d Fold[AtmosphereBins::Count];
                        double GasH, AerosolH, AerosolG, Top; bool IsAirless() const; };
struct FAtmosphereTable { static constexpr int32 Size = 32; TArray<float> Texels; bool IsEmpty() const;
                          float Texel(int32 Row, int32 Column, int32 Bin) const;
                          void Sample(double Altitude01, double CosSunZenith, double (&Out)[AtmosphereBins::Count]) const; };
// FAtmosphere: GetWhiteAir() is gone; Build, HasAir, GetAir, GetTable, GetStarColour as built.
AtmosphereReference::FSpectrum FReferenceAir::MultiScatterSpectrum(double Altitude01, double CosSunZenith,
                                                                   int32 Rings = 48, int32 Segments = 24, int32 Steps = 96) const;
namespace AtmosphereTestFixtures { struct FRayCase { FString Name; FVector3d Eye, Direction; double Length; FVector3d Sun; };
                                   TArray<FRayCase> Grid(double GasH, double Top); }   // Tests/ only
```

- [ ] **Step 1: The grid, into the fixtures.** In `Source/DeepSpace/Tests/AtmosphereTestFixtures.h`, replace the file's last lines

```cpp
            {TEXT("H2/He at its ceiling"), HydrogenHelium(HydrogenHeliumCeilingBar)}};
    }
}
```

  with

```cpp
            {TEXT("H2/He at its ceiling"), HydrogenHelium(HydrogenHeliumCeilingBar)}};
    }

    /** One ray of decision 1's grid, in one air's radii. */
    struct FRayCase
    {
        FString Name;
        FVector3d Eye = FVector3d::ZeroVector;
        FVector3d Direction = FVector3d::ZeroVector;
        double Length = 1.0e30;
        FVector3d Sun = FVector3d::ZeroVector;
    };

    /**
     * Decision 1's grid for an air of gas scale height GasH and top Top, the
     * eye on +Z: four eyes (the ground, inside at 2 H, just above the top,
     * and 7.8 radii out), four views (nadir, zenith, horizon, limb --
     * outside, the ray grazing 2 H up; inside, 5 degrees above the horizon)
     * and three suns (noon, the terminator, and backlit -- behind the world
     * from outside, low ahead from inside). 48 rays, each named
     * "<eye> eye, <view>, <sun> sun".
     */
    inline TArray<FRayCase> Grid(double GasH, double Top)
    {
        TArray<FRayCase> Cases;
        const double Heights[] = {1.0e-3 * GasH, 2.0 * GasH, Top + 2.0 * GasH, 6.8};
        const TCHAR* const EyeNames[] = {TEXT("ground"), TEXT("inside"), TEXT("above"), TEXT("far")};
        for (int32 E = 0; E < 4; ++E)
        {
            const double R = 1.0 + Heights[E];
            const bool bInside = Heights[E] < Top;
            FVector3d Limb;
            if (bInside)
            {
                const double Five = FMath::DegreesToRadians(5.0);
                Limb = FVector3d(FMath::Cos(Five), 0.0, FMath::Sin(Five));
            }
            else
            {
                const double SinAngle = (1.0 + 2.0 * GasH) / R;
                Limb = FVector3d(SinAngle, 0.0, -FMath::Sqrt(1.0 - SinAngle * SinAngle));
            }
            const FVector3d Views[] = {FVector3d(0.0, 0.0, -1.0), FVector3d(0.0, 0.0, 1.0), FVector3d(1.0, 0.0, 0.0), Limb};
            const TCHAR* const ViewNames[] = {TEXT("nadir"), TEXT("zenith"), TEXT("horizon"), TEXT("limb")};
            const FVector3d Suns[] = {FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 1.0, 0.0),
                bInside ? FVector3d(1.0, 0.0, 0.1).GetSafeNormal() : FVector3d(0.0, 0.0, -1.0)};
            const TCHAR* const SunNames[] = {TEXT("noon"), TEXT("terminator"), TEXT("backlit")};
            for (int32 V = 0; V < 4; ++V)
            {
                for (int32 S = 0; S < 3; ++S)
                {
                    FRayCase Case;
                    Case.Name = FString::Printf(TEXT("%s eye, %s, %s sun"), EyeNames[E], ViewNames[V], SunNames[S]);
                    Case.Eye = FVector3d(0.0, 0.0, R);
                    Case.Direction = Views[V];
                    Case.Sun = Suns[S];
                    Cases.Add(Case);
                }
            }
        }
        return Cases;
    }
}
```

- [ ] **Step 2: Write the failing tests.** Replace `Source/DeepSpace/Tests/AtmosphereBuildTest.cpp` with:

```cpp
#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAtmosphereBinnedAirTest,
    "DeepSpace.Atmosphere.BinnedAir",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAtmosphereStarTemperatureExtremesTest,
    "DeepSpace.Atmosphere.StarTemperatureExtremes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereBuildTestLocal
{
    double Luminance(const FVector3d& Colour)
    {
        return 0.2126 * Colour.X + 0.7152 * Colour.Y + 0.0722 * Colour.Z;
    }

    bool FiniteAndNonNegative(const AtmosphereBins::FBins& B)
    {
        bool bGood = true;
        for (int32 K = 0; K < AtmosphereBins::Count; ++K)
        {
            bGood &= std::isfinite(B.Value[K]) && B.Value[K] >= 0.0;
        }
        return bGood;
    }

    bool Same(const AtmosphereBins::FBins& A, const AtmosphereBins::FBins& B)
    {
        bool bSame = true;
        for (int32 K = 0; K < AtmosphereBins::Count; ++K)
        {
            bSame &= A.Value[K] == B.Value[K];
        }
        return bSame;
    }

    bool Same(const FAtmosphereAir& A, const FAtmosphereAir& B)
    {
        bool bSame = Same(A.GasScatter, B.GasScatter) && Same(A.GasExtinct, B.GasExtinct) && Same(A.AerosolScatter, B.AerosolScatter)
            && Same(A.AerosolExtinct, B.AerosolExtinct) && A.GasH == B.GasH && A.AerosolH == B.AerosolH && A.AerosolG == B.AerosolG && A.Top == B.Top;
        for (int32 K = 0; K < AtmosphereBins::Count; ++K)
        {
            bSame &= A.Fold[K] == B.Fold[K];
        }
        return bSame;
    }
}

bool FAtmosphereBinnedAirTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereBuildTestLocal;
    using namespace AtmosphereTestFixtures;

    // No tables: the coefficients are the question here, and a table is a
    // second a world.
    const FAtmosphere Earth = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::None);
    const FAtmosphereAir& A = Earth.GetAir();
    TestTrue(TEXT("Earth's air has air"), Earth.HasAir());
    TestTrue(TEXT("its top is ten of its gas's scale heights"), FMath::IsNearlyEqual(A.Top, 10.0 * A.GasH, 1.0e-15));

    // The bins are colourless and the gas's scattering falls from the blue
    // bin to the red, as lambda^-4 does.
    bool bFalls = true;
    for (int32 K = 1; K < AtmosphereBins::Count; ++K)
    {
        bFalls &= A.GasScatter.Value[K] < A.GasScatter.Value[K - 1];
    }
    TestTrue(TEXT("the gas scatters less in every redder bin"), bFalls);

    // Each bin is its wavelengths' own air, averaged as AtmosphereBins
    // averages: exact in the thin limit, per radius.
    const AtmosphereReference::FSpectralAir Spectral = AtmosphereReference::Spectral(EarthAir());
    const AtmosphereReference::FSpectrum Star = AtmosphereReference::StarSpectrum(SunK);
    AtmosphereReference::FSpectrum GasTau;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        GasTau.Value[I] = Spectral.GasScatter.Value[I] + Spectral.GasAbsorb.Value[I];
    }
    const AtmosphereBins::FBins Scatter = AtmosphereBins::Average(Star, Spectral.GasScatter);
    const AtmosphereBins::FBins Extinct = AtmosphereBins::Average(Star, GasTau);
    const AtmosphereBins::FBins Aerosol = AtmosphereBins::Average(Star, Spectral.AerosolExtinct);
    for (int32 K = 0; K < AtmosphereBins::Count; ++K)
    {
        TestTrue(FString::Printf(TEXT("bin %d: the gas's scattering and extinction, and the aerosol's, are the bin's average over its scale height"), K),
            FMath::Abs(A.GasScatter.Value[K] * A.GasH - Scatter.Value[K]) <= 1.0e-12 * Scatter.Value[K]
            && FMath::Abs(A.GasExtinct.Value[K] * A.GasH - Extinct.Value[K]) <= 1.0e-12 * Extinct.Value[K]
            && FMath::Abs(A.AerosolExtinct.Value[K] * A.AerosolH - Aerosol.Value[K]) <= 1.0e-12 * Aerosol.Value[K]);
        TestTrue(FString::Printf(TEXT("bin %d: its fold is the star's light there"), K), A.Fold[K] == AtmosphereBins::Fold(Star, K));
    }

    // The ozone absorbs: the gas takes out more than it scatters where the
    // Chappuis band is, and exactly what it scatters where it is not.
    TestTrue(TEXT("in the 600 nm bin the gas's extinction exceeds its scattering"),
        A.GasExtinct.Value[6] > 1.01 * A.GasScatter.Value[6]);

    // Linear in the column.
    const FAtmosphere Double = FAtmosphere::Build(NitrogenOxygen(2.0), SunK, EAtmosphereTable::None);
    bool bLinear = true;
    for (int32 K = 0; K < AtmosphereBins::Count; ++K)
    {
        bLinear &= FMath::Abs(Double.GetAir().GasScatter.Value[K] - 2.0 * A.GasScatter.Value[K]) <= 1.0e-9 * A.GasScatter.Value[K];
    }
    TestTrue(TEXT("twice the pressure, twice the scatter"), bLinear);

    // The star is in the fold and nowhere else: its colour is the folds'
    // sum, at unit luminance.
    FVector3d Folds = FVector3d::ZeroVector;
    for (int32 K = 0; K < AtmosphereBins::Count; ++K)
    {
        Folds += A.Fold[K];
    }
    TestTrue(FString::Printf(TEXT("the star's colour has luminance 1 (%.6f)"), Luminance(Earth.GetStarColour())),
        FMath::Abs(Luminance(Earth.GetStarColour()) - 1.0) < 1.0e-3);
    TestTrue(TEXT("and is the sum of the folds"), (Folds - Earth.GetStarColour()).GetAbsMax() <= 1.0e-12);

    // A red dwarf's air folds redder light, though its bins scatter alike.
    const FAtmosphere HomeAir = FAtmosphere::Build(EarthAir(), HomeStarK, EAtmosphereTable::None);
    const FAtmosphereAir& Home = HomeAir.GetAir();
    TestTrue(TEXT("under the home star the red bin's fold outweighs the blue bin's more than under the Sun"),
        Home.Fold[AtmosphereBins::Count - 1].X / Home.Fold[0].Z > A.Fold[AtmosphereBins::Count - 1].X / A.Fold[0].Z);

    // No air.
    const FAtmosphere None = FAtmosphere::Build(Airless(), SunK);
    TestFalse(TEXT("an airless world has no air"), None.HasAir());
    TestTrue(TEXT("and no coefficients"), None.GetAir().GasScatter.Value[0] == 0.0 && None.GetAir().AerosolExtinct.Value[0] == 0.0 && None.GetAir().Top == 0.0);
    TestTrue(TEXT("and no table"), None.GetTable().IsEmpty());
    return true;
}

bool FAtmosphereStarTemperatureExtremesTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereBuildTestLocal;
    using namespace AtmosphereTestFixtures;

    // Review focus 4: temperatures past the blackbody's range, and a 1,000 K
    // star with almost no blue in its bins' weights.
    const double Kelvins[] = {0.0, 500.0, 1000.0, 2000.0, 2566.0, 15000.0, 40000.0};
    for (const double Kelvin : Kelvins)
    {
        for (const FAirSpec& Spec : {EarthAir(), CarbonDioxide(CarbonDioxideCeilingBar), Giant()})
        {
            const FAtmosphere Air = FAtmosphere::Build(Spec, Kelvin, EAtmosphereTable::None);
            const FAtmosphereAir& A = Air.GetAir();
            bool bFolds = true;
            for (int32 K = 0; K < AtmosphereBins::Count; ++K)
            {
                bFolds &= std::isfinite(A.Fold[K].X) && std::isfinite(A.Fold[K].Y) && std::isfinite(A.Fold[K].Z);
            }
            TestTrue(FString::Printf(TEXT("%.0f K: every coefficient finite and not negative, every fold finite"), Kelvin),
                FiniteAndNonNegative(A.GasScatter) && FiniteAndNonNegative(A.GasExtinct) && FiniteAndNonNegative(A.AerosolScatter)
                && FiniteAndNonNegative(A.AerosolExtinct) && bFolds);
            TestTrue(FString::Printf(TEXT("%.0f K: the star's colour has luminance 1"), Kelvin),
                FMath::Abs(Luminance(Air.GetStarColour()) - 1.0) < 1.0e-3);
        }
    }
    TestTrue(TEXT("clamped as Blackbody clamps: 0 K and 500 K build as 1,000 K"),
        Same(FAtmosphere::Build(EarthAir(), 0.0, EAtmosphereTable::None).GetAir(), FAtmosphere::Build(EarthAir(), 1000.0, EAtmosphereTable::None).GetAir())
        && Same(FAtmosphere::Build(EarthAir(), 500.0, EAtmosphereTable::None).GetAir(), FAtmosphere::Build(EarthAir(), 1000.0, EAtmosphereTable::None).GetAir()));
    TestTrue(TEXT("and 40,000 K as 15,000 K"),
        Same(FAtmosphere::Build(EarthAir(), 40000.0, EAtmosphereTable::None).GetAir(), FAtmosphere::Build(EarthAir(), 15000.0, EAtmosphereTable::None).GetAir()));
    return true;
}

#endif
```

  Replace `Source/DeepSpace/Tests/AtmosphereTableTest.cpp` with:

```cpp
#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereMultiScatterTableTest, "DeepSpace.Atmosphere.MultiScatterTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereTableTestLocal
{
    double Luminance(const FVector3d& Colour)
    {
        return 0.2126 * Colour.X + 0.7152 * Colour.Y + 0.0722 * Colour.Z;
    }

    /** Straight up from the ground under the noon sun, 45 degrees high. */
    FVector3d NoonZenith(const FAtmosphere& Air)
    {
        const FVector3d Up(0.0, 0.0, 1.0);
        return AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), Up, Up, AtmosphereLaw::NoEnd, AtmosphereLaw::NoonSun()).InScatter;
    }
}

bool FAtmosphereMultiScatterTableTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereTableTestLocal;
    using namespace AtmosphereTestFixtures;
    constexpr int32 Size = FAtmosphereTable::Size;
    constexpr int32 Bins = AtmosphereBins::Count;

    const FAtmosphere Full = FAtmosphere::Build(EarthAir(), SunK);
    const FAtmosphere CarbonDioxideAir = FAtmosphere::Build(CarbonDioxide(CarbonDioxideCeilingBar), SunK);
    const FAirSpec Specs[] = {EarthAir(), CarbonDioxide(CarbonDioxideCeilingBar)};
    const FAtmosphere* const Laws[] = {&Full, &CarbonDioxideAir};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const FAirSpec& Spec = Specs[Index];
        const FAtmosphere& Law = *Laws[Index];
        const FReferenceAir Reference(Spec, SunK);
        const AtmosphereReference::FSpectrum Star = AtmosphereReference::StarSpectrum(SunK);
        const FAtmosphereTable& Table = Law.GetTable();
        if (!TestFalse(TEXT("an airy world has a table"), Table.IsEmpty()))
        {
            return false;
        }
        bool bFinite = true;
        for (const float Texel : Table.Texels)
        {
            bFinite &= std::isfinite(Texel) && Texel >= 0.0f;
        }
        TestTrue(TEXT("every texel finite and not negative"), bFinite);

        // Texel centres: altitude J / 31 of the air's depth, sun cosine
        // -1 + 2 I / 31 -- overhead, 29 degrees up, 5.6 up, 5.6 down -- each
        // bin against the reference's source averaged into it.
        for (const int32 J : {0, 3, 9})
        {
            for (const int32 I : {31, 23, 17, 14})
            {
                const double Altitude01 = static_cast<double>(J) / (Size - 1);
                const double Cos = -1.0 + 2.0 * I / (Size - 1);
                const AtmosphereBins::FBins Want = AtmosphereBins::Average(Star, Reference.MultiScatterSpectrum(Altitude01, Cos));
                for (int32 K = 0; K < Bins; ++K)
                {
                    const double Got = Table.Texel(J, I, K);
                    TestTrue(FString::Printf(TEXT("altitude %.3f, sun cosine %.4f, bin %d: the table's %.5f against the reference's %.5f"),
                        Altitude01, Cos, K, Got, Want.Value[K]),
                        FMath::Abs(Got - Want.Value[K]) <= FMath::Max(0.10 * FMath::Abs(Want.Value[K]), 1.0e-3));
                }
            }
        }

        // Read between texel centres, bilinearly.
        double Read[Bins];
        Table.Sample(0.0, -1.0 + 2.0 * 30.5 / (Size - 1), Read);
        TestTrue(TEXT("halfway between two texels the table reads their mean"),
            FMath::Abs(Read[0] - 0.5 * (Table.Texel(0, 30, 0) + Table.Texel(0, 31, 0))) < 1.0e-6);
    }

    // Coverage. The noon sun's cosine, sin 45 degrees = 0.7071, lies between
    // columns 26 (0.677) and 27 (0.742).
    const FAtmosphere Noon = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::NoonOnly);
    const FAtmosphere NoTable = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::None);
    bool bNoonColumns = true;
    bool bRestZero = true;
    for (int32 J = 0; J < Size; ++J)
    {
        for (int32 I = 0; I < Size; ++I)
        {
            for (int32 K = 0; K < Bins; ++K)
            {
                const float Only = Noon.GetTable().Texel(J, I, K);
                if (I == 26 || I == 27)
                {
                    bNoonColumns &= Only == Full.GetTable().Texel(J, I, K);
                }
                else
                {
                    bRestZero &= Only == 0.0f;
                }
            }
        }
    }
    TestTrue(TEXT("the noon table's two columns are the full table's, exactly"), bNoonColumns);
    TestTrue(TEXT("and it has nothing else"), bRestZero);
    TestTrue(TEXT("no table when none is asked for"), NoTable.GetTable().IsEmpty());
    TestTrue(TEXT("the noon zenith reads the same from either table"), NoonZenith(Full) == NoonZenith(Noon));

    // Multiple scattering adds light.
    TestTrue(FString::Printf(TEXT("the table brightens the noon zenith (%.4f with, %.4f without)"),
        Luminance(NoonZenith(Full)), Luminance(NoonZenith(NoTable))),
        Luminance(NoonZenith(Full)) > 1.01 * Luminance(NoonZenith(NoTable)));
    return true;
}

#endif
```

  Create `Source/DeepSpace/Tests/AtmosphereMarchTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereSingleScatteringTest, "DeepSpace.Atmosphere.SingleScatteringMatchesReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereMarchTestLocal
{
    /** As a display shows it: no channel below none (the reference's light
     *  can leave sRGB's gamut, where no screen could show the difference).
     *  No ruling approved this, so the test counts what it changes
     *  (planning note 17). */
    double Shown(double Channel)
    {
        return FMath::Max(Channel, 0.0);
    }
}

bool FAtmosphereSingleScatteringTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereMarchTestLocal;
    using namespace AtmosphereTestFixtures;

    // The law's march and its bins, with multiple scattering out of both:
    // the law with no table against the reference's first order, over
    // decision 1's whole grid, both builds, at decision 1's 5% or 1e-3.
    // The cases that fail a midpoint rule are here -- the haze's 1 km under
    // H2/He's 55 km, the horizon through a ceiling air, a limb whose light
    // climbs out of the ground's shadow -- and so are the long paths where
    // one depth per bin must stand for several.
    FReferenceAir::FOptions FirstOrder;
    FirstOrder.bSecondOrder = false;
    int32 Checked = 0;
    int32 Clamped = 0;
    double Worst = 0.0;
    TArray<FString> Misses;
    for (const FNamedAir& Named : Extremes())
    {
        // The home star and the Sun, which the bins were chosen against, and
        // procgen's coolest star, which they were not (planning note 13).
        for (const double Kelvin : {HomeStarK, CoolestStarK, SunK})
        {
            const FAtmosphere Law = FAtmosphere::Build(Named.Air, Kelvin, EAtmosphereTable::None);
            const FReferenceAir Reference(Named.Air, Kelvin);
            for (const FRayCase& Case : Grid(Law.GetAir().GasH, Law.GetAir().Top))
            {
                FReferenceAir::FRay Ray;
                Ray.Eye = Case.Eye;
                Ray.Direction = Case.Direction;
                Ray.Length = Case.Length;
                Ray.Sun = Case.Sun;
                const FReferenceAir::FResult Want = Reference.Trace(Ray, FirstOrder);
                const FAtmosphereScatter F64 = AtmosphereLaw::InScatterF64(Law.GetAir(), Law.GetTable(), Case.Eye, Case.Direction, Case.Length, Case.Sun);
                const FAtmosphereScatter F32 = AtmosphereLaw::InScatterF32(Law.GetAir(), Law.GetTable(),
                    FVector3f(Case.Eye), FVector3f(Case.Direction), static_cast<float>(Case.Length), FVector3f(Case.Sun));
                for (const FAtmosphereScatter* Got : {&F64, &F32})
                {
                    for (int32 C = 0; C < 3; ++C)
                    {
                        const double Raw[2][2] = {{Got->InScatter[C], Want.InScatter[C]}, {Got->Transmittance[C], Want.Transmittance[C]}};
                        const double Pairs[2][2] = {{Shown(Got->InScatter[C]), Shown(Want.InScatter[C])},
                                                    {Shown(Got->Transmittance[C]), Shown(Want.Transmittance[C])}};
                        for (int32 Q = 0; Q < 2; ++Q)
                        {
                            ++Checked;
                            Clamped += (Raw[Q][0] < 0.0 || Raw[Q][1] < 0.0) ? 1 : 0;
                            const double Allowance = FMath::Max(0.05 * Pairs[Q][1], 1.0e-3);
                            const double Over = std::isfinite(Pairs[Q][0]) ? FMath::Abs(Pairs[Q][0] - Pairs[Q][1]) / Allowance : 1.0e30;
                            Worst = FMath::Max(Worst, Over);
                            if (!(Over <= 1.0))
                            {
                                Misses.Add(FString::Printf(TEXT("%s, %.0f K, %s, %s %s channel %d: %.5f against %.5f"), Named.Name, Kelvin, *Case.Name,
                                    Got == &F64 ? TEXT("F64") : TEXT("F32"), Q == 0 ? TEXT("in-scatter") : TEXT("transmittance"), C, Pairs[Q][0], Pairs[Q][1]));
                            }
                        }
                    }
                }
            }
        }
    }
    AddInfo(FString::Printf(TEXT("single scattering against the reference's first order: %d values, %d outside 5%% or 1e-3, the worst at %.2f of its allowance; %d changed by the gamut clamp"),
        Checked, Misses.Num(), Worst, Clamped));
    for (int32 I = 0; I < FMath::Min(Misses.Num(), 40); ++I)
    {
        AddInfo(Misses[I]);
    }
    TestEqual(TEXT("the whole grid was checked: 18 airs and stars, 48 rays, two builds, three channels, two quantities"), Checked, 18 * 48 * 2 * 3 * 2);
    TestEqual(TEXT("every channel of both builds within 5% or 1e-3 of the reference's first order"), Misses.Num(), 0);
    return true;
}

#endif
```

- [ ] **Step 3: Run it; expect a compile failure.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: FAIL, `no member named 'Fold' in 'FAtmosphereAir'` (or `no member named 'MultiScatterSpectrum'`).

- [ ] **Step 4: The reference's source per wavelength.** In `Source/DeepSpace/Atmosphere/AtmosphereReference.h`, after the line

```cpp
    FVector3d MultiScatterWhite(double Altitude01, double CosSunZenith, int32 Rings = 48, int32 Segments = 24, int32 Steps = 96) const;
```

  add

```cpp

    /** The same source per wavelength, before any weighting into channels:
     *  what the law's table is held to, bin by bin. */
    AtmosphereReference::FSpectrum MultiScatterSpectrum(double Altitude01, double CosSunZenith, int32 Rings = 48, int32 Segments = 24, int32 Steps = 96) const;
```

  In `Source/DeepSpace/Atmosphere/AtmosphereReference.cpp`, replace the whole of `FReferenceAir::MultiScatterWhite` (from `FVector3d FReferenceAir::MultiScatterWhite(` to its closing brace, the end of the file) with:

```cpp
AtmosphereReference::FSpectrum FReferenceAir::MultiScatterSpectrum(double Altitude01, double CosSunZenith, int32 Rings, int32 Segments, int32 StepsPerRay) const
{
    using namespace AtmosphereReferenceLocal;
    FSpectrum Psi;
    if (!Air.bAir)
    {
        return Psi;
    }
    const FVector3d Point(0.0, 0.0, 1.0 + FMath::Clamp(Altitude01, 0.0, 1.0) * Air.Top);
    const FVector3d Sun(std::sqrt(FMath::Max(1.0 - CosSunZenith * CosSunZenith, 0.0)), 0.0, CosSunZenith);
    // The View only weights IntoView*, which this does not read.
    const FSecond Second = SecondOrderAt(Point, FVector3d(0.0, 0.0, 1.0), Sun, Rings, Segments, StepsPerRay);
    for (int32 I = 0; I < Count; ++I)
    {
        const double F = FMath::Min(Second.Transfer.Value[I], 0.999);
        Psi.Value[I] = UE_DOUBLE_PI * Second.Mean.Value[I] / (1.0 - F);
    }
    return Psi;
}

FVector3d FReferenceAir::MultiScatterWhite(double Altitude01, double CosSunZenith, int32 Rings, int32 Segments, int32 StepsPerRay) const
{
    if (!Air.bAir)
    {
        return FVector3d::ZeroVector;
    }
    return AtmosphereReference::ChannelAverage(Star, MultiScatterSpectrum(Altitude01, CosSunZenith, Rings, Segments, StepsPerRay));
}
```

- [ ] **Step 5: The law.** In `Shaders/Private/Atmosphere.ush`, replace everything from the line `// -- The air -----------------------------------------------------------------` to the end of the file with:

```hlsl
// -- The air -----------------------------------------------------------------
// The law carries light in AT_BINS spectral bins, not three colour channels
// (atmosphere plan ruling 3): each bin is a run of the reference's 16
// wavelengths (AtmosphereBins::First in C++), and within a bin the air is
// one number. Only at the end is each bin's light folded into linear sRGB,
// by the star's own light in that bin (Fold), so the shader never sees a
// temperature and three channels never have to stand for a spectrum.
static const int AT_BINS = 8;

// One world's air as the law takes it. Per bin, per radius at the surface,
// for unit light in the bin: the gas's and the aerosol's scattering and
// extinction, colourless. Fold: the star's light in each bin in linear
// sRGB at unit luminance, so the three sums are the star's colour. The gas
// falls by e every GasH radii, the aerosol every AerosolH; AerosolG is the
// aerosol's Henyey-Greenstein asymmetry; the air is drawn to Top radii above
// the surface, and Top 0 is no air.
struct AT_Air
{
    AT_REAL GasScatter[AT_BINS];
    AT_REAL GasExtinct[AT_BINS];
    AT_REAL AerosolScatter[AT_BINS];
    AT_REAL AerosolExtinct[AT_BINS];
    AT_REAL FoldR[AT_BINS];
    AT_REAL FoldG[AT_BINS];
    AT_REAL FoldB[AT_BINS];
    AT_REAL GasH;
    AT_REAL AerosolH;
    AT_REAL AerosolG;
    AT_REAL Top;
};

// A value per bin: light, or the fraction of it that survives.
struct AT_Bins
{
    AT_REAL V[AT_BINS];
};

struct AT_Rgb
{
    AT_REAL R;
    AT_REAL G;
    AT_REAL B;
};

// What a view gathers (in-scatter) and what it lets through (T), folded
// into linear sRGB.
struct AT_Scatter
{
    AT_REAL R;
    AT_REAL G;
    AT_REAL B;
    AT_REAL TR;
    AT_REAL TG;
    AT_REAL TB;
};

struct AT_Columns
{
    AT_REAL Gas;
    AT_REAL Aerosol;
};

// A ray's part in the air, measured along it from its closest approach to
// the centre (S = 0 there): the impact parameter B, the closest point C, the
// direction, and [S0, S1], empty when S1 <= S0.
struct AT_Ray
{
    AT_REAL B;
    AT_REAL CX;
    AT_REAL CY;
    AT_REAL CZ;
    AT_REAL DX;
    AT_REAL DY;
    AT_REAL DZ;
    AT_REAL S0;
    AT_REAL S1;
};

// A march's sums, per bin: in-scatter for unit light in the bin (L), the
// transmittance (T), and the fraction of light the air scatters along the
// way (F), which the multiple-scattering table needs.
struct AT_March
{
    AT_REAL L[AT_BINS];
    AT_REAL T[AT_BINS];
    AT_REAL F[AT_BINS];
};

AT_Bins AT_FilledBins(AT_REAL Value)
{
    AT_Bins Out;
    for (int K = 0; K < AT_BINS; ++K)
    {
        Out.V[K] = Value;
    }
    return Out;
}

#if defined(AT_CPP)
// -- C++: the table, and exact products ---------------------------------------
// The multiple-scattering table travels as a trailing argument (planning note
// 1 of the atmosphere optics plan): in C++ a reference to FAtmosphereTable,
// read bilinearly between texel centres; its values have already been
// through a half float, as the GPU's RGBA16F texture will hold them.
#define AT_TABLE_PARAM , const FAtmosphereTable& AT_Table
#define AT_TABLE_ARG , AT_Table
#define AT_PRECISE
static const AT_REAL AT_SPLITTER = sizeof(AT_REAL) == 8 ? AT_REAL(134217729.0) : AT_REAL(4097.0);
inline AT_Bins AT_MultiScatter(AT_Air A, AT_REAL Altitude01, AT_REAL CosSunZenith AT_TABLE_PARAM)
{
    (void)A;
    double Values[AT_BINS];
    AT_Table.Sample(double(Altitude01), double(CosSunZenith), Values);
    AT_Bins Out;
    for (int K = 0; K < AT_BINS; ++K)
    {
        Out.V[K] = AT_REAL(Values[K]);
    }
    return Out;
}
#else
// -- HLSL: the table, and exact products ----------------------------------------
// The Custom node hands its texture and sampler down as the trailing
// arguments: a 64 x 32 RGBA16F texture, bins 0-3 in its left half and 4-7
// in its right, each half read as the C++ reads the table -- 32 texels
// across, 0 and 1 at the first and last centre.
// Load, never SampleLevel: the GPU's bilinear weights are 8-bit fixed
// point, and FAtmosphereTable::Sample blends in full precision, so each
// half's four texels are loaded and blended in float, as the C++ blends
// them. The sampler is unused; it stays in the signature because a Custom
// node's texture input brings one.
#define AT_TABLE_PARAM , Texture2D AT_Table, SamplerState AT_TableSampler
#define AT_TABLE_ARG , AT_Table, AT_TableSampler
#define AT_PRECISE precise
static const float AT_SPLITTER = 4097.0;
AT_Bins AT_MultiScatter(AT_Air A, float Altitude01, float CosSunZenith AT_TABLE_PARAM)
{
    float FX = saturate((CosSunZenith + 1.0) * 0.5) * 31.0;
    float FY = saturate(Altitude01) * 31.0;
    int X0 = min(int(floor(FX)), 30);
    int Y0 = min(int(floor(FY)), 30);
    float TX = FX - float(X0);
    float TY = FY - float(Y0);
    float4 Low = lerp(lerp(AT_Table.Load(int3(X0, Y0, 0)), AT_Table.Load(int3(X0 + 1, Y0, 0)), TX),
                      lerp(AT_Table.Load(int3(X0, Y0 + 1, 0)), AT_Table.Load(int3(X0 + 1, Y0 + 1, 0)), TX), TY);
    float4 High = lerp(lerp(AT_Table.Load(int3(X0 + 32, Y0, 0)), AT_Table.Load(int3(X0 + 33, Y0, 0)), TX),
                       lerp(AT_Table.Load(int3(X0 + 32, Y0 + 1, 0)), AT_Table.Load(int3(X0 + 33, Y0 + 1, 0)), TX), TY);
    AT_Bins Out;
    Out.V[0] = Low.r;
    Out.V[1] = Low.g;
    Out.V[2] = Low.b;
    Out.V[3] = Low.a;
    Out.V[4] = High.r;
    Out.V[5] = High.g;
    Out.V[6] = High.b;
    Out.V[7] = High.a;
    return Out;
}
#endif

static const int AT_VIEW_SAMPLES = 12;

// "To infinity": a view with no end stops at the air's top or the ground.
static const AT_REAL AT_NO_END = AT_REAL(1.0e30);

// -- Columns -----------------------------------------------------------------
// ln of the column from a point R radii from the centre, Altitude = R - 1
// above the surface, toward CosZenith, per unit density at the surface, for
// a constituent that falls by e every H radii:
// ln H - Altitude / H + ln Ch(R / H, CosZenith). The altitude comes in
// separately because R - 1 is where float loses a point's height: see
// AT_Altitude.
AT_REAL AT_LogColumn(AT_REAL R, AT_REAL Altitude, AT_REAL CosZenith, AT_REAL H)
{
    return AT_log(H) - Altitude / H + AT_LogChapman(R / H, CosZenith);
}

// The altitude of the point S along a ray of impact parameter B,
// sqrt(B^2 + S^2) - 1, formed as (B - 1) + S^2 / (sqrt(B^2 + S^2) + B).
// Near the eye of a horizontal view from the ground, S^2 is below float's
// step at B^2 and sqrt(B^2 + S^2) - 1 cannot see it -- and the view's first
// optical depth is the difference of two grazing columns 36 scale heights
// deep, which that lost height shifts by per cent.
AT_REAL AT_Altitude(AT_REAL B, AT_REAL S)
{
    return (B - AT_REAL(1.0)) + S * S / (AT_sqrt(B * B + S * S) + B);
}

// -- Exact products ----------------------------------------------------------
// A * B - P exactly, where P is A * B rounded: Dekker's product, split by
// Veltkamp without a fused multiply-add. Every step is its own statement, and
// AT_PRECISE (HLSL's precise) in the shader, so no compiler fuses one into
// the next: a fused split is a wrong split.
AT_REAL AT_ProductError(AT_REAL A, AT_REAL B, AT_REAL P)
{
    AT_PRECISE AT_REAL TA = AT_SPLITTER * A;
    AT_PRECISE AT_REAL TA2 = TA - A;
    AT_PRECISE AT_REAL AH = TA - TA2;
    AT_PRECISE AT_REAL AL = A - AH;
    AT_PRECISE AT_REAL TB = AT_SPLITTER * B;
    AT_PRECISE AT_REAL TB2 = TB - B;
    AT_PRECISE AT_REAL BH = TB - TB2;
    AT_PRECISE AT_REAL BL = B - BH;
    AT_PRECISE AT_REAL E1 = AH * BH;
    AT_PRECISE AT_REAL E2 = E1 - P;
    AT_PRECISE AT_REAL E3 = AH * BL;
    AT_PRECISE AT_REAL E4 = E2 + E3;
    AT_PRECISE AT_REAL E5 = AL * BH;
    AT_PRECISE AT_REAL E6 = E4 + E5;
    AT_PRECISE AT_REAL E7 = AL * BL;
    return E6 + E7;
}

// A * B - C * D to the precision of the result, not of the products: the
// rounded difference of the rounded products -- exact by Sterbenz when they
// are close, which is when it matters -- plus the difference of their exact
// errors.
AT_REAL AT_DiffOfProducts(AT_REAL A, AT_REAL B, AT_REAL C, AT_REAL D)
{
    AT_PRECISE AT_REAL P = A * B;
    AT_PRECISE AT_REAL Q = C * D;
    AT_PRECISE AT_REAL EP = AT_ProductError(A, B, P);
    AT_PRECISE AT_REAL EQ = AT_ProductError(C, D, Q);
    AT_PRECISE AT_REAL PQ = P - Q;
    AT_PRECISE AT_REAL E = EP - EQ;
    return PQ + E;
}

// -- The ray -----------------------------------------------------------------
// The part of a ray inside the air. The closest point C and the impact
// parameter B come from W = E x D, never from |E|^2 - (E.D)^2: from a
// distant eye that difference cancels to nothing in float and takes the
// tangent height with it, where W -- each component a difference of
// products kept to its own precision -- loses nothing. C = D x W.
// An eye below the surface (a valley under the datum) is taken to the
// surface above it: the sky from a valley floor is the sky from the datum.
// Empty when the ray misses the air, is not going anywhere, or starts on the
// ground looking into it.
AT_Ray AT_RayThroughAir(AT_Air A, AT_REAL EX, AT_REAL EY, AT_REAL EZ, AT_REAL DX, AT_REAL DY, AT_REAL DZ, AT_REAL Length)
{
    AT_Ray Ray;
    Ray.DX = DX;
    Ray.DY = DY;
    Ray.DZ = DZ;
    Ray.S0 = AT_REAL(1.0);
    Ray.S1 = AT_REAL(0.0);
    AT_REAL E2 = EX * EX + EY * EY + EZ * EZ;
    if (E2 < AT_TINY)
    {
        // An eye at the centre has no surface above it to be lifted to.
        return Ray;
    }
    if (E2 < AT_REAL(1.0))
    {
        AT_REAL Lift = AT_REAL(1.0) / AT_sqrt(E2);
        EX = EX * Lift;
        EY = EY * Lift;
        EZ = EZ * Lift;
    }
    AT_REAL WX = AT_DiffOfProducts(EY, DZ, EZ, DY);
    AT_REAL WY = AT_DiffOfProducts(EZ, DX, EX, DZ);
    AT_REAL WZ = AT_DiffOfProducts(EX, DY, EY, DX);
    Ray.CX = DY * WZ - DZ * WY;
    Ray.CY = DZ * WX - DX * WZ;
    Ray.CZ = DX * WY - DY * WX;
    Ray.B = AT_sqrt(WX * WX + WY * WY + WZ * WZ);
    AT_REAL SE = EX * DX + EY * DY + EZ * DZ;
    AT_REAL Top = AT_REAL(1.0) + A.Top;
    if (Ray.B >= Top || !(Length > AT_REAL(0.0)))
    {
        return Ray;
    }
    AT_REAL Half = AT_sqrt((Top - Ray.B) * (Top + Ray.B));
    AT_REAL S0 = AT_max(SE, -Half);
    AT_REAL S1 = AT_min(SE + Length, Half);
    if (Ray.B < AT_REAL(1.0))
    {
        // The ray's line passes through the ground. Heading toward its
        // closest point (SE < 0), it ends at the near side -- at once, for an
        // eye on the surface looking down. Heading away (SE >= 0), the eye is
        // on the far side, never inside, since it was lifted to the surface:
        // it is not the eye's |E| against the ground's chord that decides,
        // which rounding makes a coin toss for an eye on the surface looking
        // along it, but the direction.
        AT_REAL Ground = AT_sqrt((AT_REAL(1.0) - Ray.B) * (AT_REAL(1.0) + Ray.B));
        if (SE < -Ground)
        {
            S1 = AT_min(S1, -Ground);
        }
        else if (SE < AT_REAL(0.0))
        {
            S1 = S0;
        }
    }
    Ray.S0 = S0;
    Ray.S1 = S1;
    return Ray;
}

// The two constituents' columns from the point at S on the ray to the top,
// looking away from the closest point -- always upward, so always the
// Chapman function's easy branch.
AT_Columns AT_ColumnsAway(AT_Air A, AT_REAL B, AT_REAL S)
{
    AT_REAL R = AT_sqrt(B * B + S * S);
    AT_REAL Altitude = AT_Altitude(B, S);
    AT_REAL Cos = AT_max(S, -S) / AT_max(R, AT_TINY);
    AT_Columns Out;
    Out.Gas = AT_exp(AT_LogColumn(R, Altitude, Cos, A.GasH));
    Out.Aerosol = AT_exp(AT_LogColumn(R, Altitude, Cos, A.AerosolH));
    return Out;
}

// The columns between SA <= SB along the ray, by differences of upward
// columns, splitting at the closest point when the span crosses it.
AT_Columns AT_ColumnsBetween(AT_Air A, AT_REAL B, AT_REAL SA, AT_REAL SB)
{
    AT_Columns CA = AT_ColumnsAway(A, B, SA);
    AT_Columns CB = AT_ColumnsAway(A, B, SB);
    AT_Columns Out;
    if (SA >= AT_REAL(0.0))
    {
        Out.Gas = CA.Gas - CB.Gas;
        Out.Aerosol = CA.Aerosol - CB.Aerosol;
    }
    else if (SB <= AT_REAL(0.0))
    {
        Out.Gas = CB.Gas - CA.Gas;
        Out.Aerosol = CB.Aerosol - CA.Aerosol;
    }
    else
    {
        AT_Columns C0 = AT_ColumnsAway(A, B, AT_REAL(0.0));
        Out.Gas = AT_REAL(2.0) * C0.Gas - CA.Gas - CB.Gas;
        Out.Aerosol = AT_REAL(2.0) * C0.Aerosol - CA.Aerosol - CB.Aerosol;
    }
    Out.Gas = AT_max(Out.Gas, AT_REAL(0.0));
    Out.Aerosol = AT_max(Out.Aerosol, AT_REAL(0.0));
    return Out;
}

// What a pair of columns lets through, per bin.
AT_Bins AT_Through(AT_Air A, AT_Columns C)
{
    AT_Bins Out;
    for (int K = 0; K < AT_BINS; ++K)
    {
        Out.V[K] = AT_exp(-(A.GasExtinct[K] * C.Gas + A.AerosolExtinct[K] * C.Aerosol));
    }
    return Out;
}

// -- The sun -----------------------------------------------------------------
// The star's light reaching a point R radii out (Altitude above the surface)
// whose sun is at CosZenith, per bin: 0 in the ground's own shadow (a ray to
// the star that passes nearer the centre than the surface), else exp of one
// combined exponent, each depth capped at e^80 so a sun just past the
// tangent never makes an infinity.
AT_Bins AT_SunAt(AT_Air A, AT_REAL R, AT_REAL Altitude, AT_REAL CosZenith)
{
    AT_Bins Out = AT_FilledBins(AT_REAL(0.0));
    AT_REAL Sin2 = AT_max(AT_REAL(1.0) - CosZenith * CosZenith, AT_REAL(0.0));
    if (CosZenith < AT_REAL(0.0) && R * R * Sin2 < AT_REAL(1.0))
    {
        return Out;
    }
    AT_REAL LogGas = AT_LogColumn(R, Altitude, CosZenith, A.GasH);
    AT_REAL LogAerosol = AT_LogColumn(R, Altitude, CosZenith, A.AerosolH);
    for (int K = 0; K < AT_BINS; ++K)
    {
        Out.V[K] = AT_exp(-(AT_exp(AT_min(AT_log(AT_max(A.GasExtinct[K], AT_TINY)) + LogGas, AT_MAX_EXPONENT))
                          + AT_exp(AT_min(AT_log(AT_max(A.AerosolExtinct[K], AT_TINY)) + LogAerosol, AT_MAX_EXPONENT))));
    }
    return Out;
}

// -- Folding the bins into colour ---------------------------------------------
// Light: each bin's light times the star's light in that bin, summed.
AT_Rgb AT_FoldLight(AT_Air A, AT_Bins L)
{
    AT_Rgb Out;
    Out.R = AT_REAL(0.0);
    Out.G = AT_REAL(0.0);
    Out.B = AT_REAL(0.0);
    for (int K = 0; K < AT_BINS; ++K)
    {
        Out.R = Out.R + A.FoldR[K] * L.V[K];
        Out.G = Out.G + A.FoldG[K] * L.V[K];
        Out.B = Out.B + A.FoldB[K] * L.V[K];
    }
    return Out;
}

// A transmittance: what each channel of the star's own light keeps, as 1
// less what it loses, so that nothing lost is exactly 1. A channel of the
// star's light floored at 1e-4 of its brightest (a 1,000 K star's
// near-absent blue), as the reference's ChannelAverage floors it.
AT_Rgb AT_FoldThrough(AT_Air A, AT_Bins T)
{
    AT_REAL OwnR = AT_REAL(0.0);
    AT_REAL OwnG = AT_REAL(0.0);
    AT_REAL OwnB = AT_REAL(0.0);
    AT_REAL LostR = AT_REAL(0.0);
    AT_REAL LostG = AT_REAL(0.0);
    AT_REAL LostB = AT_REAL(0.0);
    for (int K = 0; K < AT_BINS; ++K)
    {
        AT_REAL Lost = AT_REAL(1.0) - T.V[K];
        OwnR = OwnR + A.FoldR[K];
        OwnG = OwnG + A.FoldG[K];
        OwnB = OwnB + A.FoldB[K];
        LostR = LostR + A.FoldR[K] * Lost;
        LostG = LostG + A.FoldG[K] * Lost;
        LostB = LostB + A.FoldB[K] * Lost;
    }
    // AT_TINY, not 0, for an airless world's empty fold: it loses nothing.
    AT_REAL Floor = AT_max(AT_REAL(1.0e-4) * AT_max(OwnR, AT_max(OwnG, OwnB)), AT_TINY);
    AT_Rgb Out;
    Out.R = AT_REAL(1.0) - LostR / AT_max(OwnR, Floor);
    Out.G = AT_REAL(1.0) - LostG / AT_max(OwnG, Floor);
    Out.B = AT_REAL(1.0) - LostB / AT_max(OwnB, Floor);
    return Out;
}

// -- Phase -------------------------------------------------------------------
AT_REAL AT_RayleighPhase(AT_REAL Cos)
{
    return AT_REAL(3.0) / (AT_REAL(16.0) * AT_PI) * (AT_REAL(1.0) + Cos * Cos);
}

AT_REAL AT_HenyeyGreenstein(AT_REAL Cos, AT_REAL G)
{
    AT_REAL Den = AT_REAL(1.0) + G * G - AT_REAL(2.0) * G * Cos;
    return (AT_REAL(1.0) - G * G) / (AT_REAL(4.0) * AT_PI * Den * AT_sqrt(Den));
}

// -- The march ---------------------------------------------------------------
AT_March AT_EmptyMarch()
{
    AT_March M;
    for (int K = 0; K < AT_BINS; ++K)
    {
        M.L[K] = AT_REAL(0.0);
        M.T[K] = AT_REAL(1.0);
        M.F[K] = AT_REAL(0.0);
    }
    return M;
}

// (B - A) / ln(B / A), the logarithmic mean: what a function whose
// logarithm is linear between A and B averages to, and so, times the
// length, its exact integral -- for an exponential, however long the
// length. The mean of A and B where either is 0 (the ground's shadow);
// near A = B the series, where the quotient would cancel.
AT_REAL AT_LogMean(AT_REAL A, AT_REAL B)
{
    if (!(A > AT_REAL(0.0)) || !(B > AT_REAL(0.0)))
    {
        return AT_REAL(0.5) * (A + B);
    }
    AT_REAL X = B / A - AT_REAL(1.0);
    if (AT_max(X, -X) < AT_REAL(0.01))
    {
        return A * (AT_REAL(1.0) + X * (AT_REAL(0.5) - X / AT_REAL(12.0)));
    }
    return (B - A) / AT_log(B / A);
}

// What the view gathers at one point, per bin, for unit light in the bin,
// per unit column of each constituent: its scattering coefficient times
// the sun's light through the air (Single) and times the table's (Multi),
// each times the view's transmittance back to the ray's start, S0 (Seen);
// and the columns from S0 to the point.
struct AT_Node
{
    AT_REAL GasSingle[AT_BINS];
    AT_REAL AerosolSingle[AT_BINS];
    AT_REAL GasMulti[AT_BINS];
    AT_REAL AerosolMulti[AT_BINS];
    AT_REAL Seen[AT_BINS];
    AT_REAL GasColumn;
    AT_REAL AerosolColumn;
};

AT_Node AT_NodeAt(AT_Air A, AT_Ray Ray, AT_REAL S, AT_REAL SX, AT_REAL SY, AT_REAL SZ,
                  AT_REAL GasPhase, AT_REAL AerosolPhase, AT_REAL SunOn AT_TABLE_PARAM)
{
    AT_REAL PX = Ray.CX + S * Ray.DX;
    AT_REAL PY = Ray.CY + S * Ray.DY;
    AT_REAL PZ = Ray.CZ + S * Ray.DZ;
    AT_REAL R = AT_sqrt(Ray.B * Ray.B + S * S);
    AT_REAL Altitude = AT_Altitude(Ray.B, S);
    AT_REAL CosSun = (PX * SX + PY * SY + PZ * SZ) / AT_max(R, AT_TINY);
    AT_Columns Behind = AT_ColumnsBetween(A, Ray.B, Ray.S0, S);
    AT_Bins Seen = AT_Through(A, Behind);
    AT_Bins Sun = AT_SunAt(A, R, Altitude, CosSun);
    AT_Bins Multi = AT_MultiScatter(A, AT_saturate(Altitude / A.Top), CosSun AT_TABLE_ARG);
    AT_Node Out;
    for (int K = 0; K < AT_BINS; ++K)
    {
        AT_REAL Gas = A.GasScatter[K] * Seen.V[K];
        AT_REAL Aerosol = A.AerosolScatter[K] * Seen.V[K];
        Out.GasSingle[K] = Gas * GasPhase * Sun.V[K];
        Out.AerosolSingle[K] = Aerosol * AerosolPhase * Sun.V[K];
        Out.GasMulti[K] = Gas * SunOn * Multi.V[K];
        Out.AerosolMulti[K] = Aerosol * SunOn * Multi.V[K];
        Out.Seen[K] = Seen.V[K];
    }
    Out.GasColumn = Behind.Gas;
    Out.AerosolColumn = Behind.Aerosol;
    return Out;
}

// The single scattering between two nodes, per bin: each constituent's
// column there -- exact, from the Chapman columns the nodes hold -- times
// the logarithmic mean of what it gathers per unit column.
AT_Bins AT_SingleBetween(AT_Node P, AT_Node Q)
{
    AT_REAL Gas = AT_max(Q.GasColumn - P.GasColumn, P.GasColumn - Q.GasColumn);
    AT_REAL Aerosol = AT_max(Q.AerosolColumn - P.AerosolColumn, P.AerosolColumn - Q.AerosolColumn);
    AT_Bins Out;
    for (int K = 0; K < AT_BINS; ++K)
    {
        Out.V[K] = Gas * AT_LogMean(P.GasSingle[K], Q.GasSingle[K]) + Aerosol * AT_LogMean(P.AerosolSingle[K], Q.AerosolSingle[K]);
    }
    return Out;
}

// The single scattering over a span, P at SP to Q at SQ. The logarithmic
// mean is exact for light that falls exponentially, but sunlight climbing
// out of a grazing column rises faster than that -- its optical depth
// itself falls exponentially -- and the ground's shadow ends it in a step.
// Where the ends differ by more than e^4, either way, the mean all but loses
// the brighter end's approach, and the span takes a node of its own halfway.
AT_Bins AT_SingleSpan(AT_Air A, AT_Ray Ray, AT_Node P, AT_Node Q, AT_REAL SP, AT_REAL SQ,
                      AT_REAL SX, AT_REAL SY, AT_REAL SZ, AT_REAL GasPhase, AT_REAL AerosolPhase, AT_REAL SunOn AT_TABLE_PARAM)
{
    AT_REAL LightP = AT_REAL(0.0);
    AT_REAL LightQ = AT_REAL(0.0);
    for (int K = 0; K < AT_BINS; ++K)
    {
        LightP = LightP + P.GasSingle[K] + P.AerosolSingle[K];
        LightQ = LightQ + Q.GasSingle[K] + Q.AerosolSingle[K];
    }
    AT_REAL Steep = AT_REAL(54.6);   // e^4
    if (!(LightP > Steep * LightQ) && !(LightQ > Steep * LightP))
    {
        return AT_SingleBetween(P, Q);
    }
    AT_Node M = AT_NodeAt(A, Ray, AT_REAL(0.5) * (SP + SQ), SX, SY, SZ, GasPhase, AerosolPhase, SunOn AT_TABLE_ARG);
    AT_Bins First = AT_SingleBetween(P, M);
    AT_Bins Second = AT_SingleBetween(M, Q);
    AT_Bins Out;
    for (int K = 0; K < AT_BINS; ++K)
    {
        Out.V[K] = First.V[K] + Second.V[K];
    }
    return Out;
}

// The light between two nodes: the multiple scattering and the fraction
// scattered by the logarithmic mean over the span, the single scattering by
// AT_SingleSpan.
AT_March AT_Span(AT_Air A, AT_Ray Ray, AT_Node P, AT_Node Q, AT_REAL SP, AT_REAL SQ, AT_March Sum,
                 AT_REAL SX, AT_REAL SY, AT_REAL SZ, AT_REAL GasPhase, AT_REAL AerosolPhase, AT_REAL SunOn AT_TABLE_PARAM)
{
    AT_REAL Gas = AT_max(Q.GasColumn - P.GasColumn, P.GasColumn - Q.GasColumn);
    AT_REAL Aerosol = AT_max(Q.AerosolColumn - P.AerosolColumn, P.AerosolColumn - Q.AerosolColumn);
    AT_Bins Single = AT_SingleSpan(A, Ray, P, Q, SP, SQ, SX, SY, SZ, GasPhase, AerosolPhase, SunOn AT_TABLE_ARG);
    AT_March Out = Sum;
    for (int K = 0; K < AT_BINS; ++K)
    {
        Out.L[K] = Out.L[K] + Single.V[K] + Gas * AT_LogMean(P.GasMulti[K], Q.GasMulti[K]) + Aerosol * AT_LogMean(P.AerosolMulti[K], Q.AerosolMulti[K]);
        Out.F[K] = Out.F[K] + (Gas * A.GasScatter[K] + Aerosol * A.AerosolScatter[K]) * AT_LogMean(P.Seen[K], Q.Seen[K]);
    }
    return Out;
}

// N >= 2 nodes from Low (the end nearest the closest point, where the air
// is densest) to Far, spaced as u^2 so they crowd where the density is, and
// the light of each span between them. A midpoint rule is not exact where a
// constituent's scale height or its optical depth is short against the
// spacing (the haze's 1 km under a gas of 55 km; a horizon through thick
// air), or where the density is curved along the ray (a limb); this is.
AT_March AT_MarchPiece(AT_Air A, AT_Ray Ray, AT_REAL Low, AT_REAL Far, int N,
                       AT_REAL SX, AT_REAL SY, AT_REAL SZ, AT_REAL PhaseGas, AT_REAL PhaseAerosol, AT_REAL SunOn AT_TABLE_PARAM)
{
    AT_March Out = AT_EmptyMarch();
    AT_REAL Span = Far - Low;
    AT_REAL GasPhase = AT_PI * PhaseGas * SunOn;
    AT_REAL AerosolPhase = AT_PI * PhaseAerosol * SunOn;
    AT_REAL Last = Low;
    AT_Node Previous = AT_NodeAt(A, Ray, Low, SX, SY, SZ, GasPhase, AerosolPhase, SunOn AT_TABLE_ARG);
    for (int I = 1; I < N; ++I)
    {
        AT_REAL U = AT_REAL(I) / AT_REAL(N - 1);
        AT_REAL S = Low + Span * U * U;
        AT_Node Next = AT_NodeAt(A, Ray, S, SX, SY, SZ, GasPhase, AerosolPhase, SunOn AT_TABLE_ARG);
        Out = AT_Span(A, Ray, Previous, Next, Last, S, Out, SX, SY, SZ, GasPhase, AerosolPhase, SunOn AT_TABLE_ARG);
        Previous = Next;
        Last = S;
    }
    return Out;
}

// The whole view: its path through the air split at the closest point, the
// 12 samples shared between the two sides by length, and the transmittance of
// the whole path. No star (a zero Sun) means no light, never a sun on the
// horizon.
AT_March AT_MarchAir(AT_Air A, AT_REAL EX, AT_REAL EY, AT_REAL EZ, AT_REAL DX, AT_REAL DY, AT_REAL DZ,
                     AT_REAL Length, AT_REAL SX, AT_REAL SY, AT_REAL SZ AT_TABLE_PARAM)
{
    AT_March Out = AT_EmptyMarch();
    if (A.Top <= AT_REAL(0.0))
    {
        return Out;
    }
    AT_Ray Ray = AT_RayThroughAir(A, EX, EY, EZ, DX, DY, DZ, Length);
    if (Ray.S1 <= Ray.S0)
    {
        return Out;
    }
    AT_REAL SunOn = (SX * SX + SY * SY + SZ * SZ) > AT_REAL(0.25) ? AT_REAL(1.0) : AT_REAL(0.0);
    AT_REAL CosView = SX * DX + SY * DY + SZ * DZ;
    AT_REAL PhaseGas = AT_RayleighPhase(CosView);
    AT_REAL PhaseAerosol = AT_HenyeyGreenstein(CosView, A.AerosolG);
    AT_March First = AT_EmptyMarch();
    AT_March Second = AT_EmptyMarch();
    if (Ray.S0 >= AT_REAL(0.0))
    {
        First = AT_MarchPiece(A, Ray, Ray.S0, Ray.S1, AT_VIEW_SAMPLES, SX, SY, SZ, PhaseGas, PhaseAerosol, SunOn AT_TABLE_ARG);
    }
    else if (Ray.S1 <= AT_REAL(0.0))
    {
        First = AT_MarchPiece(A, Ray, Ray.S1, Ray.S0, AT_VIEW_SAMPLES, SX, SY, SZ, PhaseGas, PhaseAerosol, SunOn AT_TABLE_ARG);
    }
    else
    {
        AT_REAL Share = AT_floor(AT_REAL(AT_VIEW_SAMPLES) * (-Ray.S0) / (Ray.S1 - Ray.S0) + AT_REAL(0.5));
        int Before = int(AT_min(AT_max(Share, AT_REAL(2.0)), AT_REAL(AT_VIEW_SAMPLES - 2)));
        First = AT_MarchPiece(A, Ray, AT_REAL(0.0), Ray.S0, Before, SX, SY, SZ, PhaseGas, PhaseAerosol, SunOn AT_TABLE_ARG);
        Second = AT_MarchPiece(A, Ray, AT_REAL(0.0), Ray.S1, AT_VIEW_SAMPLES - Before, SX, SY, SZ, PhaseGas, PhaseAerosol, SunOn AT_TABLE_ARG);
    }
    AT_Bins Whole = AT_Through(A, AT_ColumnsBetween(A, Ray.B, Ray.S0, Ray.S1));
    for (int K = 0; K < AT_BINS; ++K)
    {
        Out.L[K] = First.L[K] + Second.L[K];
        Out.F[K] = First.F[K] + Second.F[K];
        Out.T[K] = Whole.V[K];
    }
    return Out;
}

// -- The entry points (decision 1) ------------------------------------------
// The light the air adds between an eye E and Length along D, under a star
// toward S, and what it lets through, in linear sRGB.
AT_Scatter AT_InScatter(AT_Air A, AT_REAL EX, AT_REAL EY, AT_REAL EZ, AT_REAL DX, AT_REAL DY, AT_REAL DZ,
                        AT_REAL Length, AT_REAL SX, AT_REAL SY, AT_REAL SZ AT_TABLE_PARAM)
{
    AT_March M = AT_MarchAir(A, EX, EY, EZ, DX, DY, DZ, Length, SX, SY, SZ AT_TABLE_ARG);
    AT_Bins L;
    AT_Bins T;
    for (int K = 0; K < AT_BINS; ++K)
    {
        L.V[K] = M.L[K];
        T.V[K] = M.T[K];
    }
    AT_Rgb Light = AT_FoldLight(A, L);
    AT_Rgb Through = AT_FoldThrough(A, T);
    AT_Scatter Out;
    Out.R = Light.R;
    Out.G = Light.G;
    Out.B = Light.B;
    Out.TR = Through.R;
    Out.TG = Through.G;
    Out.TB = Through.B;
    return Out;
}

// What the air lets through between F and Length along D.
AT_Rgb AT_Transmittance(AT_Air A, AT_REAL FX, AT_REAL FY, AT_REAL FZ, AT_REAL DX, AT_REAL DY, AT_REAL DZ, AT_REAL Length)
{
    AT_Rgb Out;
    Out.R = AT_REAL(1.0);
    Out.G = AT_REAL(1.0);
    Out.B = AT_REAL(1.0);
    if (A.Top <= AT_REAL(0.0))
    {
        return Out;
    }
    AT_Ray Ray = AT_RayThroughAir(A, FX, FY, FZ, DX, DY, DZ, Length);
    if (Ray.S1 <= Ray.S0)
    {
        return Out;
    }
    return AT_FoldThrough(A, AT_Through(A, AT_ColumnsBetween(A, Ray.B, Ray.S0, Ray.S1)));
}

// The star's light reaching P, per channel, relative to its own. An airless
// world passes all of it whatever the star (the disc's Lambert, not an air,
// darkens a night side); with an air, no star passes none.
AT_Rgb AT_SunThrough(AT_Air A, AT_REAL PX, AT_REAL PY, AT_REAL PZ, AT_REAL SX, AT_REAL SY, AT_REAL SZ)
{
    AT_Rgb Out;
    Out.R = AT_REAL(1.0);
    Out.G = AT_REAL(1.0);
    Out.B = AT_REAL(1.0);
    if (A.Top <= AT_REAL(0.0))
    {
        return Out;
    }
    if (SX * SX + SY * SY + SZ * SZ < AT_REAL(0.25))
    {
        Out.R = AT_REAL(0.0);
        Out.G = AT_REAL(0.0);
        Out.B = AT_REAL(0.0);
        return Out;
    }
    AT_REAL Length = AT_sqrt(PX * PX + PY * PY + PZ * PZ);
    AT_REAL Cos = (PX * SX + PY * SY + PZ * SZ) / AT_max(Length, AT_TINY);
    return AT_FoldThrough(A, AT_SunAt(A, AT_max(Length, AT_REAL(1.0)), AT_max(Length - AT_REAL(1.0), AT_REAL(0.0)), Cos));
}

// -- The multiple-scattering table (decision 11) -------------------------------
// The sphere of directions as rings of equal step in T, cos(zenith) = T |T|,
// crowded toward the horizon -- where a ray's path through the air is longest
// and most of the light at low altitude comes from -- by segments of equal
// azimuth over half the circle: the texel's sun lies in the X-Z plane, so the
// light field is mirror-symmetric in azimuth and the other half is the same.
// A Fibonacci sphere of 64, 128 or 256 points gives this integral 10% apart
// from itself (the thin band of long horizontal paths falls between its
// points differently each time); 32 rings are within 6% of a 192-ring
// integral at the ground, where it converges slowest.
static const int AT_SPHERE_RINGS = 32;
static const int AT_SPHERE_SEGMENTS = 8;

// One texel (Hillaire 2020): at Altitude01 of the air's depth under a sun at
// CosSunZenith, per bin, the light the air sends every way after one
// scattering, averaged over the sphere, and grown by the geometric series of
// the fraction F each scattering hands on: Psi = L1 / (1 - F). The bins are
// colourless, so the star's colour enters once, where the bins are folded.
// Built in C++ when a system loads, never in a frame, and read from an empty
// table while it is built.
AT_Bins AT_MultiScatterCell(AT_Air A, AT_REAL Altitude01, AT_REAL CosSunZenith AT_TABLE_PARAM)
{
    AT_REAL R = AT_REAL(1.0) + AT_saturate(Altitude01) * A.Top;
    AT_REAL SX = AT_sqrt(AT_max(AT_REAL(1.0) - CosSunZenith * CosSunZenith, AT_REAL(0.0)));
    AT_Bins L = AT_FilledBins(AT_REAL(0.0));
    AT_Bins F = AT_FilledBins(AT_REAL(0.0));
    for (int I = 0; I < AT_SPHERE_RINGS; ++I)
    {
        AT_REAL T = AT_REAL(-1.0) + (AT_REAL(2.0) * AT_REAL(I) + AT_REAL(1.0)) / AT_REAL(AT_SPHERE_RINGS);
        AT_REAL AbsT = AT_max(T, -T);
        AT_REAL Mu = T * AbsT;
        // The share of the sphere one direction stands for, its mirror image
        // included: 2 x (2 |T| dT)(dPhi) / 4 pi, dT = 2 / RINGS, dPhi =
        // pi / SEGMENTS.
        AT_REAL Weight = AT_REAL(2.0) * AbsT / AT_REAL(AT_SPHERE_RINGS * AT_SPHERE_SEGMENTS);
        AT_REAL Ring = AT_sqrt(AT_max(AT_REAL(1.0) - Mu * Mu, AT_REAL(0.0)));
        for (int J = 0; J < AT_SPHERE_SEGMENTS; ++J)
        {
            AT_REAL Phi = AT_PI * (AT_REAL(J) + AT_REAL(0.5)) / AT_REAL(AT_SPHERE_SEGMENTS);
            AT_March M = AT_MarchAir(A, AT_REAL(0.0), AT_REAL(0.0), R, Ring * AT_cos(Phi), Ring * AT_sin(Phi), Mu,
                                     AT_NO_END, SX, AT_REAL(0.0), CosSunZenith AT_TABLE_ARG);
            for (int K = 0; K < AT_BINS; ++K)
            {
                L.V[K] = L.V[K] + Weight * M.L[K];
                F.V[K] = F.V[K] + Weight * M.F[K];
            }
        }
    }
    AT_Bins Out;
    for (int K = 0; K < AT_BINS; ++K)
    {
        Out.V[K] = L.V[K] / AT_max(AT_REAL(1.0) - F.V[K], AT_REAL(0.001));
    }
    return Out;
}
```

- [ ] **Step 6: The C++ side.** Replace `Source/DeepSpace/Atmosphere/Atmosphere.h` with:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Atmosphere/AtmosphereBins.h"
#include "Atmosphere/AtmosphereReference.h"

/**
 * The air's optics as the game draws them (atmospheres decision 1):
 * Shaders/Private/Atmosphere.ush, the one law, compiled here twice -- in
 * double, what the game computes, and in float, the GPU's mirror, which the
 * rendered probe (orbital slice 1) holds the GPU to and the pure tests hold
 * to the double. AtmosphereReference is what "right" means; this is what
 * draws.
 */
namespace AtmosphereLaw
{
    /** ln of the Chapman function, the .ush's AT_LogChapman: X = R / H,
     *  CosZenith the ray against the local vertical. */
    DEEPSPACE_API double LogChapmanF64(double X, double CosZenith);
    DEEPSPACE_API float LogChapmanF32(float X, float CosZenith);
}

/**
 * One world's air as the law takes it: the .ush's AT_Air, in double, over
 * AtmosphereBins' Count spectral bins (atmosphere plan ruling 3). Per bin,
 * per radius of the body at the surface, for unit light in the bin: the
 * gas's and the aerosol's scattering and extinction, colourless. Fold[b] is
 * the star's light in bin b in linear sRGB at unit luminance -- the star is
 * in the fold, so the shader never sees a temperature (decision 3) -- and
 * the Count folds sum to the star's colour. The gas falls by e every GasH
 * radii and the aerosol every AerosolH; the air is drawn to Top radii above
 * the surface. Airless when Top is 0.
 */
struct FAtmosphereAir
{
    AtmosphereBins::FBins GasScatter;
    AtmosphereBins::FBins GasExtinct;
    AtmosphereBins::FBins AerosolScatter;
    AtmosphereBins::FBins AerosolExtinct;
    FVector3d Fold[AtmosphereBins::Count] = {};
    double GasH = 0.0;
    double AerosolH = 0.0;
    double AerosolG = 0.0;
    double Top = 0.0;

    bool IsAirless() const { return !(Top > 0.0); }
};

/**
 * One airy world's multiple-scattering table (decision 11): Size x Size
 * texels, row = altitude over the air's depth (0 at the surface), column =
 * the sun's zenith cosine from -1 to 1, each a value per bin
 * -- two RGBA16F texels on the GPU, a 64 x 32 texture with bins 0-3 in its
 * left half and 4-7 in its right, which is why the values stored here have
 * already been through a half float. Texels[(Row * Size + Column) * Count +
 * Bin]. Empty until FAtmosphere::Build fills it; an empty table reads as no
 * multiple scattering.
 */
struct DEEPSPACE_API FAtmosphereTable
{
    static constexpr int32 Size = 32;

    TArray<float> Texels;

    bool IsEmpty() const { return Texels.Num() != Size * Size * AtmosphereBins::Count; }

    float Texel(int32 Row, int32 Column, int32 Bin) const { return Texels[(Row * Size + Column) * AtmosphereBins::Count + Bin]; }

    /** Bilinear between texel centres, clamped at the edges: what the .ush's
     *  hook reads. Zeros when empty. */
    void Sample(double Altitude01, double CosSunZenith, double (&Out)[AtmosphereBins::Count]) const;
};

/** How much of the multiple-scattering table Build fills. NoonOnly fills
 *  the two columns either side of the noon sun's cosine -- every sample of
 *  a zenith view under that sun reads only those, so it is all the noon
 *  zenith needs and exactly what the full table holds there -- for the
 *  corpus's sky of every world; None fills nothing (single scattering). */
enum class EAtmosphereTable : uint8 { None, NoonOnly, Full };

namespace AtmosphereLaw
{
    /** The sky's reference view, what the corpus's sky_zenith_rgb and
     *  DeepSpace.Atmosphere.StarColour mean by "the noon zenith": straight
     *  up from the ground under a sun 45 degrees high. Not a sun at the
     *  zenith: the zenith would then be the star's own forward-scattered
     *  aureole, and wear the star's colour rather than the sky's. */
    inline constexpr double NoonSunElevationDeg = 45.0;

    /** That sun, the zenith being +Z: (cos 45, 0, sin 45). */
    inline FVector3d NoonSun()
    {
        const double Radians = FMath::DegreesToRadians(NoonSunElevationDeg);
        return FVector3d(FMath::Cos(Radians), 0.0, FMath::Sin(Radians));
    }
}

/** One world's air under one star, fitted for the law (decision 3). */
class DEEPSPACE_API FAtmosphere
{
public:
    /**
     * The per-bin coefficients from the star's spectrum through the world's
     * own spectral laws, each its wavelengths' average by AtmosphereBins::
     * Weight -- exact in the optically thin limit, the gas's extinction
     * carrying the ozone -- and the star's light in each bin as the fold. The star's temperature is
     * clamped as SkyColour::Blackbody clamps it. The multiple-scattering
     * table is built through the .ush's own AT_MultiScatterCell, and stored
     * through half floats.
     */
    static FAtmosphere Build(const FAirSpec& Spec, double StarTemperatureK, EAtmosphereTable Coverage = EAtmosphereTable::Full);

    bool HasAir() const { return !Air.IsAirless(); }
    const FAtmosphereAir& GetAir() const { return Air; }
    const FAtmosphereTable& GetTable() const { return Table; }

    /** The star's light in linear sRGB at unit luminance: the sum of the
     *  air's folds. */
    const FVector3d& GetStarColour() const { return StarColour; }

private:
    FAtmosphereAir Air;
    FAtmosphereTable Table;
    FVector3d StarColour = FVector3d::ZeroVector;
};

/** What one view gathers and lets through, per channel: linear sRGB in the
 *  pi convention for a star of unit luminance, and the fraction of the
 *  star's own light that survives the path. */
struct FAtmosphereScatter
{
    FVector3d InScatter = FVector3d::ZeroVector;
    FVector3d Transmittance = FVector3d::OneVector;
};

/**
 * The .ush's entry points, in double and in the GPU's float. Positions in
 * radii of the body, its centre at the origin; directions unit; Sun toward
 * the star, or the zero vector for no star. Length is how far the view goes
 * (NoEnd: to the air's top or the ground).
 */
namespace AtmosphereLaw
{
    inline constexpr int32 ViewSamples = 12;
    inline constexpr double NoEnd = 1.0e30;

    DEEPSPACE_API FAtmosphereScatter InScatterF64(const FAtmosphereAir& Air, const FAtmosphereTable& Table,
        const FVector3d& Eye, const FVector3d& Direction, double Length, const FVector3d& Sun);
    DEEPSPACE_API FAtmosphereScatter InScatterF32(const FAtmosphereAir& Air, const FAtmosphereTable& Table,
        const FVector3f& Eye, const FVector3f& Direction, float Length, const FVector3f& Sun);

    DEEPSPACE_API FVector3d TransmittanceF64(const FAtmosphereAir& Air, const FVector3d& From, const FVector3d& Direction, double Length);
    DEEPSPACE_API FVector3d TransmittanceF32(const FAtmosphereAir& Air, const FVector3f& From, const FVector3f& Direction, float Length);

    DEEPSPACE_API FVector3d SunThroughF64(const FAtmosphereAir& Air, const FVector3d& Point, const FVector3d& Sun);
    DEEPSPACE_API FVector3d SunThroughF32(const FAtmosphereAir& Air, const FVector3f& Point, const FVector3f& Sun);

    /** |Eye x Direction| as the law takes it: each component a difference
     *  of products kept to its own precision. */
    DEEPSPACE_API double ImpactParameterF64(const FVector3d& Eye, const FVector3d& Direction);
    DEEPSPACE_API float ImpactParameterF32(const FVector3f& Eye, const FVector3f& Direction);
}
```

  Replace `Source/DeepSpace/Atmosphere/Atmosphere.cpp` with:

```cpp
#include "Atmosphere/Atmosphere.h"

#include <cmath>
#include "Math/Float16.h"

// The shared file, twice. AT_CPP selects its C++ halves and AT_REAL its
// precision; each copy lives in its own namespace, so the two sets of AT_
// symbols never meet. The relative path is the file the GPU will include as
// /Project/Private/Atmosphere.ush: a change to it that C++ cannot compile
// fails ./build.sh.
#define AT_CPP 1
namespace AtmosphereF64
{
#define AT_REAL double
#include "../../../Shaders/Private/Atmosphere.ush"
#undef AT_REAL
}
namespace AtmosphereF32
{
#define AT_REAL float
#include "../../../Shaders/Private/Atmosphere.ush"
#undef AT_REAL
}
#undef AT_CPP

static_assert(AtmosphereF64::AT_BINS == AtmosphereBins::Count && AtmosphereF32::AT_BINS == AtmosphereBins::Count,
    "Atmosphere.ush's AT_BINS is AtmosphereBins::Count");

namespace AtmosphereLocal
{
    /** FAtmosphereAir as one precision's AT_Air. */
    template <typename TAir, typename TReal>
    TAir ToAir(const FAtmosphereAir& In)
    {
        TAir A;
        for (int32 K = 0; K < AtmosphereBins::Count; ++K)
        {
            A.GasScatter[K] = TReal(In.GasScatter.Value[K]);
            A.GasExtinct[K] = TReal(In.GasExtinct.Value[K]);
            A.AerosolScatter[K] = TReal(In.AerosolScatter.Value[K]);
            A.AerosolExtinct[K] = TReal(In.AerosolExtinct.Value[K]);
            A.FoldR[K] = TReal(In.Fold[K].X);
            A.FoldG[K] = TReal(In.Fold[K].Y);
            A.FoldB[K] = TReal(In.Fold[K].Z);
        }
        A.GasH = TReal(In.GasH);
        A.AerosolH = TReal(In.AerosolH);
        A.AerosolG = TReal(In.AerosolG);
        A.Top = TReal(In.Top);
        return A;
    }

    template <typename TScatter>
    FAtmosphereScatter ToScatter(const TScatter& S)
    {
        FAtmosphereScatter Out;
        Out.InScatter = FVector3d(S.R, S.G, S.B);
        Out.Transmittance = FVector3d(S.TR, S.TG, S.TB);
        return Out;
    }

    template <typename TRgb>
    FVector3d ToVector(const TRgb& C)
    {
        return FVector3d(C.R, C.G, C.B);
    }
}

double AtmosphereLaw::LogChapmanF64(double X, double CosZenith)
{
    return AtmosphereF64::AT_LogChapman(X, CosZenith);
}

float AtmosphereLaw::LogChapmanF32(float X, float CosZenith)
{
    return AtmosphereF32::AT_LogChapman(X, CosZenith);
}


void FAtmosphereTable::Sample(double Altitude01, double CosSunZenith, double (&Out)[AtmosphereBins::Count]) const
{
    if (IsEmpty())
    {
        for (int32 K = 0; K < AtmosphereBins::Count; ++K)
        {
            Out[K] = 0.0;
        }
        return;
    }
    const double FX = FMath::Clamp((CosSunZenith + 1.0) * 0.5, 0.0, 1.0) * (Size - 1);
    const double FY = FMath::Clamp(Altitude01, 0.0, 1.0) * (Size - 1);
    const int32 X0 = FMath::Min(FMath::FloorToInt32(FX), Size - 2);
    const int32 Y0 = FMath::Min(FMath::FloorToInt32(FY), Size - 2);
    const double TX = FX - X0;
    const double TY = FY - Y0;
    for (int32 K = 0; K < AtmosphereBins::Count; ++K)
    {
        Out[K] = FMath::Lerp(FMath::Lerp(Texel(Y0, X0, K), Texel(Y0, X0 + 1, K), TX),
                             FMath::Lerp(Texel(Y0 + 1, X0, K), Texel(Y0 + 1, X0 + 1, K), TX), TY);
    }
}

FAtmosphere FAtmosphere::Build(const FAirSpec& Spec, double StarTemperatureK, EAtmosphereTable Coverage)
{
    using namespace AtmosphereReference;
    FAtmosphere Out;
    const FSpectrum Star = StarSpectrum(StarTemperatureK);
    Out.StarColour = ToLinearSrgb(Star);
    const FSpectralAir Spectra = Spectral(Spec);
    if (!Spectra.bAir)
    {
        return Out;
    }

    // Each bin's air is its wavelengths' own, averaged by AtmosphereBins::
    // Weight: exact in the thin limit. Deep, one number stands for a spread
    // of depths; the bins are narrow enough that it does (planning note 13).
    FSpectrum GasExtinct;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        GasExtinct.Value[I] = Spectra.GasScatter.Value[I] + Spectra.GasAbsorb.Value[I];
    }
    const AtmosphereBins::FBins GasScatter = AtmosphereBins::Average(Star, Spectra.GasScatter);
    const AtmosphereBins::FBins GasTau = AtmosphereBins::Average(Star, GasExtinct);
    const AtmosphereBins::FBins AerosolScatter = AtmosphereBins::Average(Star, Spectra.AerosolScatter);
    const AtmosphereBins::FBins AerosolTau = AtmosphereBins::Average(Star, Spectra.AerosolExtinct);

    FAtmosphereAir& A = Out.Air;
    A.GasH = Spectra.GasH;
    A.AerosolH = Spectra.AerosolH;
    A.AerosolG = Spectra.AerosolG;
    A.Top = Spectra.Top;
    for (int32 K = 0; K < AtmosphereBins::Count; ++K)
    {
        A.GasScatter.Value[K] = GasScatter.Value[K] / Spectra.GasH;
        A.GasExtinct.Value[K] = GasTau.Value[K] / Spectra.GasH;
        A.AerosolScatter.Value[K] = AerosolScatter.Value[K] / Spectra.AerosolH;
        A.AerosolExtinct.Value[K] = AerosolTau.Value[K] / Spectra.AerosolH;
        A.Fold[K] = AtmosphereBins::Fold(Star, K);
    }

    if (Coverage != EAtmosphereTable::None)
    {
        // Each texel from the .ush's own AT_MultiScatterCell in double, so
        // the table is the law's (decision 11), read while it is built from
        // an empty table -- single scattering only -- and stored through
        // half floats, as the GPU's RGBA16F texture will hold it.
        constexpr int32 Size = FAtmosphereTable::Size;
        const AtmosphereF64::AT_Air Air64 = AtmosphereLocal::ToAir<AtmosphereF64::AT_Air, double>(Out.Air);
        const FAtmosphereTable Empty;
        Out.Table.Texels.SetNumZeroed(Size * Size * AtmosphereBins::Count);
        // NoonOnly: the two columns either side of the noon sun's, which
        // every sample of a zenith view under that sun reads.
        const int32 NoonColumn = FMath::Min(FMath::FloorToInt32((AtmosphereLaw::NoonSun().Z + 1.0) * 0.5 * (Size - 1)), Size - 2);
        const int32 FirstColumn = Coverage == EAtmosphereTable::Full ? 0 : NoonColumn;
        const int32 LastColumn = Coverage == EAtmosphereTable::Full ? Size - 1 : NoonColumn + 1;
        for (int32 Row = 0; Row < Size; ++Row)
        {
            for (int32 Column = FirstColumn; Column <= LastColumn; ++Column)
            {
                const double Altitude01 = static_cast<double>(Row) / (Size - 1);
                const double Cos = -1.0 + 2.0 * Column / (Size - 1);
                const AtmosphereF64::AT_Bins Cell = AtmosphereF64::AT_MultiScatterCell(Air64, Altitude01, Cos, Empty);
                for (int32 K = 0; K < AtmosphereBins::Count; ++K)
                {
                    Out.Table.Texels[(Row * Size + Column) * AtmosphereBins::Count + K] = FFloat16(static_cast<float>(Cell.V[K])).GetFloat();
                }
            }
        }
    }
    return Out;
}

FAtmosphereScatter AtmosphereLaw::InScatterF64(const FAtmosphereAir& Air, const FAtmosphereTable& Table,
    const FVector3d& Eye, const FVector3d& Direction, double Length, const FVector3d& Sun)
{
    const AtmosphereF64::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF64::AT_Air, double>(Air);
    return AtmosphereLocal::ToScatter(AtmosphereF64::AT_InScatter(A, Eye.X, Eye.Y, Eye.Z, Direction.X, Direction.Y, Direction.Z,
        Length, Sun.X, Sun.Y, Sun.Z, Table));
}

FAtmosphereScatter AtmosphereLaw::InScatterF32(const FAtmosphereAir& Air, const FAtmosphereTable& Table,
    const FVector3f& Eye, const FVector3f& Direction, float Length, const FVector3f& Sun)
{
    const AtmosphereF32::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF32::AT_Air, float>(Air);
    return AtmosphereLocal::ToScatter(AtmosphereF32::AT_InScatter(A, Eye.X, Eye.Y, Eye.Z, Direction.X, Direction.Y, Direction.Z,
        Length, Sun.X, Sun.Y, Sun.Z, Table));
}

FVector3d AtmosphereLaw::TransmittanceF64(const FAtmosphereAir& Air, const FVector3d& From, const FVector3d& Direction, double Length)
{
    const AtmosphereF64::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF64::AT_Air, double>(Air);
    return AtmosphereLocal::ToVector(AtmosphereF64::AT_Transmittance(A, From.X, From.Y, From.Z, Direction.X, Direction.Y, Direction.Z, Length));
}

FVector3d AtmosphereLaw::TransmittanceF32(const FAtmosphereAir& Air, const FVector3f& From, const FVector3f& Direction, float Length)
{
    const AtmosphereF32::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF32::AT_Air, float>(Air);
    return AtmosphereLocal::ToVector(AtmosphereF32::AT_Transmittance(A, From.X, From.Y, From.Z, Direction.X, Direction.Y, Direction.Z, Length));
}

FVector3d AtmosphereLaw::SunThroughF64(const FAtmosphereAir& Air, const FVector3d& Point, const FVector3d& Sun)
{
    const AtmosphereF64::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF64::AT_Air, double>(Air);
    return AtmosphereLocal::ToVector(AtmosphereF64::AT_SunThrough(A, Point.X, Point.Y, Point.Z, Sun.X, Sun.Y, Sun.Z));
}

FVector3d AtmosphereLaw::SunThroughF32(const FAtmosphereAir& Air, const FVector3f& Point, const FVector3f& Sun)
{
    const AtmosphereF32::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF32::AT_Air, float>(Air);
    return AtmosphereLocal::ToVector(AtmosphereF32::AT_SunThrough(A, Point.X, Point.Y, Point.Z, Sun.X, Sun.Y, Sun.Z));
}

double AtmosphereLaw::ImpactParameterF64(const FVector3d& Eye, const FVector3d& Direction)
{
    using namespace AtmosphereF64;
    const double WX = AT_DiffOfProducts(Eye.Y, Direction.Z, Eye.Z, Direction.Y);
    const double WY = AT_DiffOfProducts(Eye.Z, Direction.X, Eye.X, Direction.Z);
    const double WZ = AT_DiffOfProducts(Eye.X, Direction.Y, Eye.Y, Direction.X);
    return std::sqrt(WX * WX + WY * WY + WZ * WZ);
}

float AtmosphereLaw::ImpactParameterF32(const FVector3f& Eye, const FVector3f& Direction)
{
    using namespace AtmosphereF32;
    const float WX = AT_DiffOfProducts(Eye.Y, Direction.Z, Eye.Z, Direction.Y);
    const float WY = AT_DiffOfProducts(Eye.Z, Direction.X, Eye.X, Direction.Z);
    const float WZ = AT_DiffOfProducts(Eye.X, Direction.Y, Eye.Y, Direction.X);
    return std::sqrt(WX * WX + WY * WY + WZ * WZ);
}
```

- [ ] **Step 7: Build and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && for T in BinnedAir StarTemperatureExtremes MultiScatterTable SingleScatteringMatchesReference AirlessIsZero LimbBeyondSilhouette TerminatorReddens CrescentAtHighPhase BacklitRing ImpactParameter EyeBelowTheDatum NoStar DegenerateRays; do ./test.sh DeepSpace.Atmosphere.$T || break; done; grep -h "single scattering against" Saved/Logs/DeepSpace.log | tail -1
cd /home/matt/Development/deepspace/.worktrees/air-optics && for T in DeepSpace.Atmosphere.BinnedAir DeepSpace.Atmosphere.MultiScatterTable DeepSpace.Atmosphere.SingleScatteringMatchesReference DeepSpace.Sky.Colour; do echo "$T"; time ./test.sh "$T"; done
```

Expected: `passed: 1` thirteen times. The info line now reads `10368 values`. In the harness, over the home star and the Sun only, it read `6912 values, 0 outside 5% or 1e-3, the worst at 0.80 of its allowance`, and the three new tests took 0.0, 4.1 and 1.9 s at `-O2`; the 2,400 K star adds about half again to `.SingleScatteringMatchesReference`, about 2.9 s. If it misses only at 2,400 K, stop and report the printed misses (planning note 13). Report the gamut-clamp count too (planning note 17).
- The `static_assert` in `Atmosphere.cpp` holds the `.ush`'s `AT_BINS` to `AtmosphereBins::Count`. If it fires, the two were edited apart.
- **If a shape test fails** (`.CrescentAtHighPhase`, `.BacklitRing`), print its info lines and report. They are the spec's claims about the look, and the re-cut law must keep them.

- [ ] **Step 8: The whole suite, then commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-optics add Shaders/Private/Atmosphere.ush Source/DeepSpace/Atmosphere/Atmosphere.h Source/DeepSpace/Atmosphere/Atmosphere.cpp Source/DeepSpace/Atmosphere/AtmosphereReference.h Source/DeepSpace/Atmosphere/AtmosphereReference.cpp Source/DeepSpace/Tests/AtmosphereTestFixtures.h Source/DeepSpace/Tests/AtmosphereBuildTest.cpp Source/DeepSpace/Tests/AtmosphereTableTest.cpp Source/DeepSpace/Tests/AtmosphereMarchTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/air-optics commit -F - <<'MSG'
feat(atmosphere): the law over eight bins -- the air, a march exact for exponentials, the fold

Atmosphere.ush re-cut for atmosphere plan rulings 3 and 6: AT_Air holds
per bin the gas's and the aerosol's scattering and extinction, colourless,
and the star's light in each bin as the fold; the entry points fold the
bins into linear sRGB at the end, so their signatures are decision 1's
still. The march's 12 samples are 12 nodes: between two, each
constituent's exact column (the Chapman columns the nodes hold) times the
logarithmic mean of what it gathers per unit column -- exact for the
density and for the view's transmittance, where a midpoint rule missed
the haze's 1 km under H2/He's 55 km and horizons through ceiling airs --
with one more node halfway where single scattering climbs past e^4, as
sunlight out of a grazing column does. The table holds a value per bin
(on the GPU, two RGBA16F texels); there is no white air.

DeepSpace.Atmosphere.SingleScatteringMatchesReference holds the march and
the bins to the reference's first order over decision 1's whole grid,
both builds, within 5% or 1e-3. .BinnedAir and .StarTemperatureExtremes
hold the coefficients and folds; .MultiScatterTable each bin against
FReferenceAir::MultiScatterSpectrum averaged into it. The grid moves into
the fixtures.

Measured: DeepSpace.Atmosphere.BinnedAir costs N s over start-up.
Measured: DeepSpace.Atmosphere.MultiScatterTable costs N s over start-up.
Measured: DeepSpace.Atmosphere.SingleScatteringMatchesReference costs N s over start-up.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

  Each `N` is its time less `DeepSpace.Sky.Colour`'s, in whole seconds (*Conventions*).

- [ ] **Step 9: Prove the tests can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '    if (CosZenith < AT_REAL(0.0) && R * R * Sin2 < AT_REAL(1.0))' '    if (CosZenith < AT_REAL(-2.0) && R * R * Sin2 < AT_REAL(1.0))' DeepSpace.Atmosphere.TerminatorReddens
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '    return PQ + E;' '    return PQ;' DeepSpace.Atmosphere.ImpactParameter
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '    if (E2 < AT_REAL(1.0))' '    if (E2 < AT_REAL(0.0))' DeepSpace.Atmosphere.EyeBelowTheDatum
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '> AT_REAL(0.25) ? AT_REAL(1.0) : AT_REAL(0.0);' '> AT_REAL(-1.0) ? AT_REAL(1.0) : AT_REAL(0.0);' DeepSpace.Atmosphere.NoStar
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '    AT_REAL PhaseAerosol = AT_HenyeyGreenstein(CosView, A.AerosolG);' '    AT_REAL PhaseAerosol = AT_HenyeyGreenstein(-CosView, A.AerosolG);' DeepSpace.Atmosphere.BacklitRing
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '            S1 = AT_min(S1, -Ground);' '            S1 = AT_min(S1, Ground);' DeepSpace.Atmosphere.DegenerateRays
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '    AT_REAL Floor = AT_max(AT_REAL(1.0e-4) * AT_max(OwnR, AT_max(OwnG, OwnB)), AT_TINY);' '    AT_REAL Floor = AT_REAL(1.0e-4) * AT_max(OwnR, AT_max(OwnG, OwnB));' DeepSpace.Atmosphere.AirlessIsZero
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '    return (B - A) / AT_log(B / A);' '    return AT_REAL(0.5) * (A + B);' DeepSpace.Atmosphere.SingleScatteringMatchesReference
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '    AT_REAL Steep = AT_REAL(54.6);   // e^4' '    AT_REAL Steep = AT_REAL(1.0e30);   // e^4' DeepSpace.Atmosphere.SingleScatteringMatchesReference
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '        Out.B = Out.B + A.FoldB[K] * L.V[K];' '        Out.B = Out.B + A.FoldG[K] * L.V[K];' DeepSpace.Atmosphere.SingleScatteringMatchesReference
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Atmosphere/Atmosphere.cpp '        A.GasExtinct.Value[K] = GasTau.Value[K] / Spectra.GasH;' '        A.GasExtinct.Value[K] = GasScatter.Value[K] / Spectra.GasH;' DeepSpace.Atmosphere.BinnedAir
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Sky/SkyColour.cpp '    const double T = FMath::Clamp(TemperatureK, MinTemperatureK, MaxTemperatureK);' '    const double T = TemperatureK;' DeepSpace.Atmosphere.StarTemperatureExtremes
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '        Out.V[K] = L.V[K] / AT_max(AT_REAL(1.0) - F.V[K], AT_REAL(0.001));' '        Out.V[K] = AT_REAL(4.0) * L.V[K] / AT_max(AT_REAL(1.0) - F.V[K], AT_REAL(0.001));' DeepSpace.Atmosphere.MultiScatterTable
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Atmosphere/AtmosphereReference.cpp '        Psi.Value[I] = UE_DOUBLE_PI * Second.Mean.Value[I] / (1.0 - F);' '        Psi.Value[I] = UE_DOUBLE_PI * Second.Mean.Value[I];' DeepSpace.Atmosphere.MultiScatterTable
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: `KILLED` fourteen times:
1. The shadow is gone past the terminator.
2. The naive difference loses the float impact parameter.
3. The valley eye is no longer lifted, and sees nothing.
4. A zero sun becomes a sun on the horizon.
5. Forward scattering turns backward, and the ring dims.
6. The view straight down runs through the planet.
7. An airless world's empty fold divides nothing by nothing.
8. The logarithmic mean becomes the midpoint's arithmetic one, and the thin airs and grazing paths miss again.
9. No span is ever split, and the far eye's backlit limb loses the light climbing out of the grazing column.
10. The blue channel folds the green's light.
11. The ozone drops out of the gas's extinction.
12. The star's temperature goes unclamped: a 0 K star's spectrum is not a number.
13. The table's texels are four times their light.
14. The reference's source is read without its geometric tail.

Every mutant was run in the plan's harness and killed. Each `.ush` mutation rebuilds the whole module.

---
## Task 6 (O6, re-planned): The table's columns crowd the horizon

The multiple-scattering table was built in `42e49f6` (Hillaire's isotropic approximation: 32 rings crowded toward the horizon, 8 half-circle segments, `Psi = L1 / (1 - F)`, 32 x 32 texels through half floats, `NoonOnly` and `None` coverage, and `AtmosphereLaw::NoonSun`). Task 5 carried all of it over to the bins. This task changes one thing, measured by the re-plan (planning note 15).

**The problem.** The columns were equal steps in the sun's cosine: 0.065 apart, so the two either side of the horizon hold suns 3.7 degrees below and above it. The light the air hands on changes fastest exactly there, as the sun goes from lighting the air to leaving it in the ground's shadow. A bilinear read between those columns carried the lit side's light into the terminator: a ground horizon under a sun on the horizon, through a ceiling air, came out about half as bright again as the reference.

**The fix.** The columns become equal steps in `T` with the sun's cosine `T |T|`, as the table's own sphere crowds its rings, so the two columns either side of the horizon hold suns 0.06 degrees from it. `FAtmosphereTable::ColumnOf` and `CosOfColumn` are the one mapping, and the build, `Sample` and the HLSL hook all use it. The noon sun's columns move from 26-27 to 28-29.

**Files:**
- Modify: `Source/DeepSpace/Atmosphere/Atmosphere.h` (`FAtmosphereTable`), `Source/DeepSpace/Atmosphere/Atmosphere.cpp` (`ColumnOf`, `CosOfColumn`, `Sample`, `Build`), `Shaders/Private/Atmosphere.ush` (the HLSL hook)
- Test: replace `Source/DeepSpace/Tests/AtmosphereTableTest.cpp` (`DeepSpace.Atmosphere.MultiScatterTable`)

**Interfaces:**
- Consumes: `FAtmosphereTable`, `FAtmosphere::Build`, `EAtmosphereTable` (Task 5); `FReferenceAir::MultiScatterSpectrum` (Task 5); `AtmosphereBins::Average` (Task 4).
- Produces:

```cpp
static double FAtmosphereTable::ColumnOf(double CosSunZenith);   // fractional column: (T + 1) / 2 * (Size - 1), CosSunZenith = T |T|
static double FAtmosphereTable::CosOfColumn(int32 Column);       // its inverse at a column's centre
```

- [ ] **Step 1: Write the failing test.** Replace `Source/DeepSpace/Tests/AtmosphereTableTest.cpp` with:

```cpp
#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereMultiScatterTableTest, "DeepSpace.Atmosphere.MultiScatterTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereTableTestLocal
{
    double Luminance(const FVector3d& Colour)
    {
        return 0.2126 * Colour.X + 0.7152 * Colour.Y + 0.0722 * Colour.Z;
    }

    /** Straight up from the ground under the noon sun, 45 degrees high. */
    FVector3d NoonZenith(const FAtmosphere& Air)
    {
        const FVector3d Up(0.0, 0.0, 1.0);
        return AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), Up, Up, AtmosphereLaw::NoEnd, AtmosphereLaw::NoonSun()).InScatter;
    }
}

bool FAtmosphereMultiScatterTableTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereTableTestLocal;
    using namespace AtmosphereTestFixtures;
    constexpr int32 Size = FAtmosphereTable::Size;
    constexpr int32 Bins = AtmosphereBins::Count;

    // The columns: equal steps in T, the sun's cosine T |T|, crowded at the
    // horizon; ColumnOf undoes CosOfColumn.
    bool bColumns = true;
    for (int32 I = 0; I < Size; ++I)
    {
        bColumns &= FMath::Abs(FAtmosphereTable::ColumnOf(FAtmosphereTable::CosOfColumn(I)) - I) < 1.0e-9;
    }
    TestTrue(TEXT("ColumnOf finds each column's own sun"), bColumns);
    TestTrue(TEXT("the ends are the sun overhead and underfoot"),
        FAtmosphereTable::CosOfColumn(0) == -1.0 && FAtmosphereTable::CosOfColumn(Size - 1) == 1.0);
    TestTrue(TEXT("and the two columns either side of the horizon hold suns within a tenth of a degree of it"),
        FMath::Abs(FAtmosphereTable::CosOfColumn(Size / 2)) < std::sin(FMath::DegreesToRadians(0.1)));

    const FAtmosphere Full = FAtmosphere::Build(EarthAir(), SunK);
    const FAtmosphere CarbonDioxideAir = FAtmosphere::Build(CarbonDioxide(CarbonDioxideCeilingBar), SunK);
    const FAirSpec Specs[] = {EarthAir(), CarbonDioxide(CarbonDioxideCeilingBar)};
    const FAtmosphere* const Laws[] = {&Full, &CarbonDioxideAir};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const FAirSpec& Spec = Specs[Index];
        const FAtmosphere& Law = *Laws[Index];
        const FReferenceAir Reference(Spec, SunK);
        const AtmosphereReference::FSpectrum Star = AtmosphereReference::StarSpectrum(SunK);
        const FAtmosphereTable& Table = Law.GetTable();
        if (!TestFalse(TEXT("an airy world has a table"), Table.IsEmpty()))
        {
            return false;
        }
        bool bFinite = true;
        for (const float Texel : Table.Texels)
        {
            bFinite &= std::isfinite(Texel) && Texel >= 0.0f;
        }
        TestTrue(TEXT("every texel finite and not negative"), bFinite);

        // Texel centres: altitude J / 31 of the air's depth, the sun at
        // CosOfColumn(I) -- overhead, 17.5 degrees up, 1.5 up, 1.5
        // down -- each bin against the reference's source averaged into it.
        for (const int32 J : {0, 3, 9})
        {
            for (const int32 I : {31, 24, 18, 13})
            {
                const double Altitude01 = static_cast<double>(J) / (Size - 1);
                const double Cos = FAtmosphereTable::CosOfColumn(I);
                const AtmosphereBins::FBins Want = AtmosphereBins::Average(Star, Reference.MultiScatterSpectrum(Altitude01, Cos));
                for (int32 K = 0; K < Bins; ++K)
                {
                    const double Got = Table.Texel(J, I, K);
                    TestTrue(FString::Printf(TEXT("altitude %.3f, sun cosine %.4f, bin %d: the table's %.5f against the reference's %.5f"),
                        Altitude01, Cos, K, Got, Want.Value[K]),
                        FMath::Abs(Got - Want.Value[K]) <= FMath::Max(0.10 * FMath::Abs(Want.Value[K]), 1.0e-3));
                }
            }
        }

        // A sun on the horizon, read between the columns either side of it:
        // there the light the air hands on changes fastest with the sun, and
        // a blend across a coarse step reads the lit side's light into the
        // shadow's.
        const AtmosphereBins::FBins Horizon = AtmosphereBins::Average(Star, Reference.MultiScatterSpectrum(0.0, 0.0));
        double AtHorizon[Bins];
        Table.Sample(0.0, 0.0, AtHorizon);
        for (int32 K = 0; K < Bins; ++K)
        {
            TestTrue(FString::Printf(TEXT("the sun on the horizon, bin %d: the table reads %.5f against the reference's %.5f"), K, AtHorizon[K], Horizon.Value[K]),
                FMath::Abs(AtHorizon[K] - Horizon.Value[K]) <= FMath::Max(0.10 * Horizon.Value[K], 1.0e-3));
        }

        // Read between texel centres, bilinearly.
        const double Between = 0.5 * (FAtmosphereTable::CosOfColumn(30) + FAtmosphereTable::CosOfColumn(31));
        double Read[Bins];
        Table.Sample(0.0, Between, Read);
        const double Column = FAtmosphereTable::ColumnOf(Between) - 30.0;
        TestTrue(TEXT("between two texels the table reads their blend at the sun's own column"),
            FMath::Abs(Read[0] - FMath::Lerp(static_cast<double>(Table.Texel(0, 30, 0)), static_cast<double>(Table.Texel(0, 31, 0)), Column)) < 1.0e-6);
    }

    // Coverage. The noon sun's cosine, sin 45 degrees, lies between columns
    // 28 and 29 (T = 0.8409, column 28.53).
    const FAtmosphere Noon = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::NoonOnly);
    const FAtmosphere NoTable = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::None);
    bool bNoonColumns = true;
    bool bRestZero = true;
    for (int32 J = 0; J < Size; ++J)
    {
        for (int32 I = 0; I < Size; ++I)
        {
            for (int32 K = 0; K < Bins; ++K)
            {
                const float Only = Noon.GetTable().Texel(J, I, K);
                if (I == 28 || I == 29)
                {
                    bNoonColumns &= Only == Full.GetTable().Texel(J, I, K);
                }
                else
                {
                    bRestZero &= Only == 0.0f;
                }
            }
        }
    }
    TestTrue(TEXT("the noon table's two columns are the full table's, exactly"), bNoonColumns);
    TestTrue(TEXT("and it has nothing else"), bRestZero);
    TestTrue(TEXT("no table when none is asked for"), NoTable.GetTable().IsEmpty());
    TestTrue(TEXT("the noon zenith reads the same from either table"), NoonZenith(Full) == NoonZenith(Noon));

    // Multiple scattering adds light.
    TestTrue(FString::Printf(TEXT("the table brightens the noon zenith (%.4f with, %.4f without)"),
        Luminance(NoonZenith(Full)), Luminance(NoonZenith(NoTable))),
        Luminance(NoonZenith(Full)) > 1.01 * Luminance(NoonZenith(NoTable)));
    return true;
}

#endif
```

- [ ] **Step 2: Run it; expect a compile failure.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: FAIL, `no member named 'ColumnOf' in 'FAtmosphereTable'`.

- [ ] **Step 3: The mapping.** In `Source/DeepSpace/Atmosphere/Atmosphere.h`, in `FAtmosphereTable`'s comment replace ` * the sun's zenith cosine from -1 to 1, each a value per bin` with ` * the sun's zenith cosine from -1 to 1 (ColumnOf), each a value per bin`. After the line `    bool IsEmpty() const { return Texels.Num() != Size * Size * AtmosphereBins::Count; }`, add:

```cpp

    /** The column, fractional, that holds a sun at CosSunZenith: the
     *  columns are equal steps in T with CosSunZenith = T |T|, crowded
     *  toward the horizon, where the light that has scattered changes
     *  fastest with the sun. */
    static double ColumnOf(double CosSunZenith);

    /** The sun's zenith cosine at a column's centre. */
    static double CosOfColumn(int32 Column);
```

  In `Source/DeepSpace/Atmosphere/Atmosphere.cpp`:
  - Immediately before `void FAtmosphereTable::Sample(`, add:

```cpp
double FAtmosphereTable::ColumnOf(double CosSunZenith)
{
    const double Cos = FMath::Clamp(CosSunZenith, -1.0, 1.0);
    const double T = Cos < 0.0 ? -std::sqrt(-Cos) : std::sqrt(Cos);
    return (T + 1.0) * 0.5 * (Size - 1);
}

double FAtmosphereTable::CosOfColumn(int32 Column)
{
    const double T = -1.0 + 2.0 * Column / (Size - 1);
    return T * std::fabs(T);
}

```

  - In `Sample`, replace `    const double FX = FMath::Clamp((CosSunZenith + 1.0) * 0.5, 0.0, 1.0) * (Size - 1);` with `    const double FX = ColumnOf(CosSunZenith);`.
  - In `Build`, replace `        const int32 NoonColumn = FMath::Min(FMath::FloorToInt32((AtmosphereLaw::NoonSun().Z + 1.0) * 0.5 * (Size - 1)), Size - 2);` with `        const int32 NoonColumn = FMath::Min(FMath::FloorToInt32(FAtmosphereTable::ColumnOf(AtmosphereLaw::NoonSun().Z)), Size - 2);`.
  - Also in `Build`, replace the two lines

```cpp
                const double Cos = -1.0 + 2.0 * Column / (Size - 1);
                const AtmosphereF64::AT_Bins Cell = AtmosphereF64::AT_MultiScatterCell(Air64, Altitude01, Cos, Empty);
```

    with

```cpp
                const AtmosphereF64::AT_Bins Cell = AtmosphereF64::AT_MultiScatterCell(Air64, Altitude01, FAtmosphereTable::CosOfColumn(Column), Empty);
```

  In `Shaders/Private/Atmosphere.ush`'s HLSL half, replace `// across, 0 and 1 at the first and last centre.` with the two lines

```hlsl
// across, 0 and 1 at the first and last centre, the sun's columns crowded
// toward the horizon as FAtmosphereTable::ColumnOf crowds them.
```

    and replace `    float FX = saturate((CosSunZenith + 1.0) * 0.5) * 31.0;` with

```hlsl
    float Cos = clamp(CosSunZenith, -1.0, 1.0);
    float T = Cos < 0.0 ? -sqrt(-Cos) : sqrt(Cos);
    float FX = saturate((T + 1.0) * 0.5) * 31.0;
```

- [ ] **Step 4: Build and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && for T in MultiScatterTable TerminatorReddens CrescentAtHighPhase BacklitRing; do ./test.sh DeepSpace.Atmosphere.$T || break; done && ./test.sh Atmosphere.Full.GroundSkySwatch
cd /home/matt/Development/deepspace/.worktrees/air-optics && time ./test.sh DeepSpace.Atmosphere.MultiScatterTable && time ./test.sh DeepSpace.Sky.Colour
```

Expected: `passed: 1` five times, and the two times (*Conventions*). `.MultiScatterTable` holds the mapping. The other four build full tables and read them off the noon columns: the shape tests at the terminator and backlit, the swatch at dusk. So they are what sees the columns move. (`.SingleScatteringMatchesReference` builds no table and cannot see them.) In the harness, with the old equal-cosine columns, the horizon check read up to 0.00616 against the reference's 0.00489 (bin 3, 26% over). With these columns every bin is within 10%.

If `.MultiScatterTable` misses by more than 10% elsewhere, the first remedy is still C++-only: raise `AT_SPHERE_RINGS` and `AT_SPHERE_SEGMENTS`, and report why in the commit.

- [ ] **Step 5: The whole suite, then commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-optics add Shaders/Private/Atmosphere.ush Source/DeepSpace/Atmosphere/Atmosphere.h Source/DeepSpace/Atmosphere/Atmosphere.cpp Source/DeepSpace/Tests/AtmosphereTableTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/air-optics commit -F - <<'MSG'
feat(atmosphere): the table's columns crowd the horizon

The multiple-scattering table's sun axis was equal steps in the sun's
cosine, so the columns either side of the horizon held suns 3.7 degrees
below and above it -- where the light the air hands on changes fastest --
and a read between them carried the lit side's light into the terminator:
a ground horizon under a setting sun came out half as bright again as
the reference. Now
the columns are equal steps in T, the cosine T |T|, as the table's own
sphere crowds its rings: 0.06 degrees either side of the horizon.
FAtmosphereTable::ColumnOf and CosOfColumn are the one mapping, used by the
build, Sample and the HLSL hook; noon's columns are 28 and 29.

DeepSpace.Atmosphere.MultiScatterTable holds the mapping, each bin against
the reference, and the table's read at a sun on the horizon.

Measured: DeepSpace.Atmosphere.MultiScatterTable costs N s over start-up.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

  Replace each `N` with the measured seconds before committing, and check with `git log -1 --format=%B | grep -c ' N s'`, which must print `0` (*Conventions*).

- [ ] **Step 6: Prove the test can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Atmosphere/Atmosphere.cpp '    return (T + 1.0) * 0.5 * (Size - 1);' '    return (Cos + 1.0) * 0.5 * (Size - 1);' DeepSpace.Atmosphere.MultiScatterTable
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: `KILLED`. The mutant reads the old equal-cosine columns from a table built on the new ones: `ColumnOf` no longer undoes `CosOfColumn`, and the horizon read misses.

---
## Task 7 (O7, re-planned): The ground sky again through the bins, and the sky's colour as ruled

`.GroundSkySwatch` was built in `5027df5`: Steps 1-4 of the first plan, with the file as built, saving to `FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), ...))`. It writes six 256-pixel fisheyes of the ground sky, noon and dusk:
- Earth air under the home star and under the Sun;
- carbon dioxide at 1 bar under the home star.

Each is drawn through the shipped law in double, exposed at `ds.Sky.Radiance` 3.0 and the galley's EV100 0.7, to `Saved/air_swatch_*.png`. Rulings 1 and 4 were made from those swatches. Its code does not change: it calls only the entry points, whose signatures the bins kept.

Two things are owed:
- **The swatches through the binned law.** The developer ruled from the three-channel law's. The bins move the skies a little, so they go to the developer again, for information. No new ruling is asked.
- **A proof the swatch can fail.** The first plan's mutation renamed the extension to `.nonesuch`, and UE 5.8.2 writes a PNG for an extension it does not know, so that mutant survived and proved nothing (the builder's report). The corrected mutation points the file under `DeepSpace.uproject`, as if it were a directory: it is a file, so the directory cannot be made and nothing is written. The attempt stays inside the repository (never a system path such as `/proc`: CLAUDE.md forbids touching those, and the plan creates nothing outside the repository), and it does not depend on how a kernel refuses a write.

Then `.StarColour`: rulings 1 and 4 are recorded (2026-09-27), so its gate passes. Ruling 1 restates the red-dwarf bounds to what the physics gives, and this task writes them.

**Files:**
- Test: modify `Source/DeepSpace/Tests/AtmosphereSwatchTest.cpp` (`DeepSpace.Atmosphere.StarColour`, beside the built `.GroundSkySwatch`)
- Modify: `docs/superpowers/specs/2026-09-27-atmospheres-design.md` (Step 5):
  - the *Tests* section's `.StarColour` item;
  - decision 3's table;
  - ruling 2's sentence on red dwarfs' skies, which ruling 1 restates.

**Interfaces:**
- Consumes: `FAtmosphere::Build`, `EAtmosphereTable::NoonOnly`, `AtmosphereLaw::{InScatterF64, NoEnd, NoonSun}` (Tasks 5-6); the fixtures (Task 3); the built swatch's `AtmosphereSwatchTestLocal` namespace.
- Produces: `DeepSpace.Atmosphere.StarColour`, and in `AtmosphereSwatchTestLocal`: `FVector3d NoonZenith(const FAtmosphere&)`, `double Saturation(const FVector3d&)`, `double Hue(const FVector3d&)`. Task 12 re-points the swatch's `Skies()` and gives `PlanetAir` its own `NoonZenith` and `Saturation` for the corpus.

- [ ] **Step 1: The swatches through the bins, to the developer.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && ./test.sh Atmosphere.Full.GroundSkySwatch; grep -h "swatch " Saved/Logs/DeepSpace.log | tail -6; ls -l Saved/air_swatch_*.png
```

Expected: `passed: 1`, six `swatch` lines, six fresh PNGs. Report them through the orchestrator to the developer: the six paths in `.worktrees/air-optics/Saved/` and the six `swatch` lines, marked "the same skies through the eight-bin law; for information, no ruling asked". Nothing is committed: the test's code did not change.

- [ ] **Step 2: Prove the swatch can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Tests/AtmosphereSwatchTest.cpp 'FPaths::Combine(FPaths::ProjectSavedDir(), ' 'FPaths::Combine(FPaths::ProjectDir(), TEXT("DeepSpace.uproject"), ' Atmosphere.Full.GroundSkySwatch
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && git status --short && test -f DeepSpace.uproject
```

Expected: `KILLED`: every path is `<project>/DeepSpace.uproject/air_swatch_*.png`, whose directory would be a file, so `every swatch is written` fails with 0 of 6. Then `git status --short` prints nothing and `DeepSpace.uproject` is still a file: the attempt left nothing behind. If it survives, `FImageUtils::SaveImageByExtension` is reporting success for a file it did not write. Stop and report the log's `swatch` lines: then the test must check the files exist on disk (`IFileManager::Get().FileSize(*Path) > 0`) rather than trust the return value, and that is a change to the test, made and proved before going on.

- [ ] **Step 3: The gate: rulings 1 and 4.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && git merge -q main && grep -c '^\*\*Atmosphere plan ruling 1 ' docs/superpowers/specs/2026-09-27-atmospheres-design.md && grep -c '^\*\*Atmosphere plan ruling 4 ' docs/superpowers/specs/2026-09-27-atmospheres-design.md
```

Expected: `1` and `1` (both recorded 2026-09-27). Ruling 4 keeps 45 degrees, so Task 6's columns and `.StarColour`'s G-star bounds stand. Ruling 1 keeps the physics: peach under red dwarfs, about 0.72 at 2,566 K and 0.96 at 2,000 K, the greyest near 3,500-4,000 K, blue from about 4,000 K, and no white balance per star.

- [ ] **Step 4: Write `.StarColour`.** In `AtmosphereSwatchTest.cpp`, after the `IMPLEMENT_SIMPLE_AUTOMATION_TEST` of `FAtmosphereGroundSkySwatchTest`, add:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereStarColourTest, "DeepSpace.Atmosphere.StarColour",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

  In `namespace AtmosphereSwatchTestLocal`, after `SunAt`, add:

```cpp

    /** Straight up from the ground under the noon sun (ruling 4). */
    FVector3d NoonZenith(const FAtmosphere& Air)
    {
        const FVector3d Up(0.0, 0.0, 1.0);
        return AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), Up, Up, AtmosphereLaw::NoEnd, AtmosphereLaw::NoonSun()).InScatter;
    }

    /** HSV saturation, 1 - min / max, of the colour with its negative
     *  channels taken as none. */
    double Saturation(const FVector3d& Colour)
    {
        const FVector3d C(FMath::Max(Colour.X, 0.0), FMath::Max(Colour.Y, 0.0), FMath::Max(Colour.Z, 0.0));
        const double Max = C.GetMax();
        return Max > 0.0 ? 1.0 - C.GetMin() / Max : 0.0;
    }

    /** HSV hue, degrees. */
    double Hue(const FVector3d& Colour)
    {
        const FVector3d C(FMath::Max(Colour.X, 0.0), FMath::Max(Colour.Y, 0.0), FMath::Max(Colour.Z, 0.0));
        const double Max = C.GetMax();
        const double Delta = Max - C.GetMin();
        if (Delta <= 0.0)
        {
            return 0.0;
        }
        double Degrees = 0.0;
        if (Max == C.X)
        {
            Degrees = 60.0 * std::fmod((C.Y - C.Z) / Delta, 6.0);
        }
        else if (Max == C.Y)
        {
            Degrees = 60.0 * ((C.Z - C.X) / Delta + 2.0);
        }
        else
        {
            Degrees = 60.0 * ((C.X - C.Y) / Delta + 4.0);
        }
        return Degrees < 0.0 ? Degrees + 360.0 : Degrees;
    }
```

  Before the closing `#endif`, add:

```cpp
bool FAtmosphereStarColourTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereSwatchTestLocal;
    using namespace AtmosphereTestFixtures;
    const auto Zenith = [](double Kelvin) { return NoonZenith(FAtmosphere::Build(EarthAir(), Kelvin, EAtmosphereTable::NoonOnly)); };

    // Decision 3's table, pinned as atmosphere plan ruling 1 restates it: no
    // palette, no floor, no white balance per star. In linear sRGB with a
    // D65 white a red dwarf's sky is peach, the star's own orange pulled
    // toward blue by lambda^-4; the sky is greyest near 3,500 K and blue
    // from 4,000 K up.
    TArray<double> Saturations;
    TArray<int32> Kelvins;
    bool bBlue = true;
    for (int32 Kelvin = 2000; Kelvin <= 15000; Kelvin += 500)
    {
        const FVector3d Sky = Zenith(Kelvin);
        const double S = Saturation(Sky);
        AddInfo(FString::Printf(TEXT("%5d K: noon zenith (%.4f, %.4f, %.4f), saturation %.3f, hue %.0f"),
            Kelvin, Sky.X, Sky.Y, Sky.Z, S, Hue(Sky)));
        Saturations.Add(S);
        Kelvins.Add(Kelvin);
        if (Kelvin >= 4000)
        {
            bBlue &= Hue(Sky) >= 200.0 && Hue(Sky) <= 240.0;
        }
    }
    int32 Least = 0;
    for (int32 I = 1; I < Saturations.Num(); ++I)
    {
        Least = Saturations[I] < Saturations[Least] ? I : Least;
    }
    bool bFalls = true;
    bool bRises = true;
    for (int32 I = 1; I < Saturations.Num(); ++I)
    {
        if (I <= Least)
        {
            bFalls &= Saturations[I] < Saturations[I - 1];
        }
        else
        {
            bRises &= Saturations[I] > Saturations[I - 1];
        }
    }
    // Atmosphere plan ruling 1 (2026-09-27).
    TestTrue(FString::Printf(TEXT("the greyest sky is between 3,000 and 4,500 K (%d K)"), Kelvins[Least]), Kelvins[Least] >= 3000 && Kelvins[Least] <= 4500);
    TestTrue(TEXT("saturation falls from 2,000 K to the greyest and rises from it to 15,000 K"), bFalls && bRises);
    TestTrue(TEXT("from 4,000 K up the sky is blue: hue within 200-240"), bBlue);

    const FVector3d Home = Zenith(HomeStarK);
    const double Coolest = Saturation(Zenith(2000.0));
    const FVector3d Sun = Zenith(SunK);
    // Atmosphere plan ruling 1 (2026-09-27).
    TestTrue(FString::Printf(TEXT("under the home star, 2,566 K, a peach sky: saturation %.3f in [0.62, 0.82]"), Saturation(Home)),
        Saturation(Home) >= 0.62 && Saturation(Home) <= 0.82);
    TestTrue(FString::Printf(TEXT("and its hue %.1f orange, in [15, 40]"), Hue(Home)), Hue(Home) >= 15.0 && Hue(Home) <= 40.0);
    // Atmosphere plan ruling 1 (2026-09-27).
    TestTrue(FString::Printf(TEXT("under 2,000 K deep orange: saturation %.3f >= 0.85"), Coolest), Coolest >= 0.85);
    TestTrue(FString::Printf(TEXT("under the Sun, Earth's blue: hue %.1f in [200, 235]"), Hue(Sun)), Hue(Sun) >= 200.0 && Hue(Sun) <= 235.0);
    TestTrue(FString::Printf(TEXT("and saturation %.3f in [0.40, 0.85]"), Saturation(Sun)), Saturation(Sun) >= 0.40 && Saturation(Sun) <= 0.85);
    return true;
}
```

  The bounds are ruling 1's, with the binned law's measured values inside them. The harness measured:
  - 2,000 K: saturation 0.920, hue 20;
  - 2,566 K: 0.715, hue 26;
  - 3,500 K, the greyest: 0.163;
  - 4,000 K: 0.171, hue 226;
  - 5,772 K: 0.620, hue 224;
  - 15,000 K: 0.855, hue 230.

  The three-channel law gave 0.96 at 2,000 K. The bins give 0.92, still "about" ruling 1's figure, so the 2,000 K bound is `>= 0.85` and not a band.

- [ ] **Step 5: Run; expect PASS. Amend the spec to match.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && ./test.sh DeepSpace.Atmosphere.StarColour; grep -h "K: noon zenith" Saved/Logs/DeepSpace.log | tail -27
cd /home/matt/Development/deepspace/.worktrees/air-optics && time ./test.sh DeepSpace.Atmosphere.StarColour && time ./test.sh DeepSpace.Sky.Colour
```

  Expected: `passed: 1` and the 27-line table. A failure means the test does not say what ruling 1 says: fix the test to the ruling, never the ruling to the test.

  Then, in `docs/superpowers/specs/2026-09-27-atmospheres-design.md`:
  - In the *Tests* section, replace the `.StarColour` item, from `  - **[1, O]** \`.StarColour\`, in linear sRGB, HSV saturation \`S = 1 -` through `    alone.`, with:

```markdown
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
```

  - In decision 3, replace the table heading `| Star | Sky at noon under Earth air |` with `| Star | Sky at noon under Earth air (straight up, the sun 45 degrees high) |`.
  - In the same table, replace the row `| 2,566 K (home) | pale grey-cyan to off-white, low saturation; a sun far oranger than the sky |` with `| 2,566 K (home) | peach: saturation about 0.72, hue about 26 degrees; a sun oranger still (atmosphere plan ruling 1) |`.
  - Replace the row `| 3,000 K | about (1, 1.26, 1.31) relative, a faint cyan |` with `| 3,000 K | a paler peach, saturation about 0.49; the greyest sky is near 3,500 K, and blue begins near 4,000 K |`.
  - In ruling 2 of *The developer's rulings*, replace the words `skies -- red dwarfs' -- are pale grey-cyan; blue only under G, F and A` (they wrap after `A`) and `   stars.` with `skies -- red dwarfs' -- are peach; blue only from about 4,000 K, under` and `   K, G, F and A stars (as atmosphere plan ruling 1 restates it).`. Ruling 1 itself directs this restatement. It is the only edit to that section outside the orchestrator's, one sentence, far from the plan-ruling paragraphs.

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && grep -c "pale grey-cyan" docs/superpowers/specs/2026-09-27-atmospheres-design.md
cd /home/matt/Development/deepspace/.worktrees/air-optics && grep -c "pale grey-cyan to off-white\|are pale grey-cyan\|a faint cyan" docs/superpowers/specs/2026-09-27-atmospheres-design.md
```

  Expected: `2`, then `0`. The two left are records of what was replaced: the quotation in ruling 1's own paragraph, and the new `.StarColour` item's "not the pale grey-cyan first estimated". The `0` is the old decision 3 rows and ruling 2's old wording, all gone.

- [ ] **Step 6: The whole suite, then commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-optics add Source/DeepSpace/Tests/AtmosphereSwatchTest.cpp docs/superpowers/specs/2026-09-27-atmospheres-design.md
git -C /home/matt/Development/deepspace/.worktrees/air-optics commit -F - <<'MSG'
test(atmosphere): the sky's colour from the star, as ruled

DeepSpace.Atmosphere.StarColour pins decision 3's table as atmosphere plan
rulings 1 and 4 state it: the noon zenith straight up under a sun 45
degrees high, Earth air at 1 bar and 1 g, from 2,000 to 15,000 K through
the eight-bin law. Peach under the home star (saturation 0.72, hue 26),
deep orange at 2,000 K, the greyest sky between 3,000 and 4,500 K, blue
from 4,000 K up, Earth's blue under the Sun (hue 224, saturation 0.62). No
palette, no floor, no white balance per star. The spec's .StarColour
item, decision 3's table and ruling 2's sentence say the same.

Measured: DeepSpace.Atmosphere.StarColour costs N s over start-up.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

  Replace each `N` with the measured seconds before committing, and check with `git log -1 --format=%B | grep -c ' N s'`, which must print `0` (*Conventions*).

- [ ] **Step 7: Prove `.StarColour` can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Atmosphere/Atmosphere.cpp '        A.Fold[K] = AtmosphereBins::Fold(Star, K);' '        A.Fold[K] = AtmosphereBins::Fold(AtmosphereReference::StarSpectrum(6504.0), K);' DeepSpace.Atmosphere.StarColour
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Tests/AtmosphereSwatchTest.cpp 'AtmosphereLaw::NoEnd, AtmosphereLaw::NoonSun()).InScatter;' 'AtmosphereLaw::NoEnd, FVector3d(0.0, 0.0, 1.0)).InScatter;' DeepSpace.Atmosphere.StarColour
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: `KILLED` twice. Both were run in the plan's harness.
- The first folds every star's light as a 6,504 K star's, a white balance per star, which ruling 1 refused. The home star's sky turns blue.
- The second puts the noon sun overhead, so the zenith becomes the Sun's aureole. The mutation's text is `NoonZenith`'s own line. The swatch's `Write` passes `Sun`, not `NoonSun()`, so it is untouched.

---
## Task 8 (O8, re-planned): The law against the reference, float against double, and the homothety

These are decision 1's three agreements, as rulings 3, 5, 6 and 7 leave them:
- **`.LawMatchesReference`:** the shipped law, in both builds, against the reference, over decision 1's grid: every mix at both pressure extremes, under the home star and the Sun.
  - The reference's second scattering is sent every way alike (`FOptions::bIsotropicSecondOrder`), as the law's table sends every order past the first (ruling 7).
  - The tolerance is decision 1's 5% or 1e-3, everywhere, thin airs and grazing paths included (ruling 3).
  - The harness measured 0 misses of 6,912 values, the worst at 0.80 of its allowance.
- **`.MultipleScatteringGap`:** the same grid against the reference's full second order, held to ruling 7's bound: 25% or 1e-3, measured worst 0.75 of it. That bound is what the isotropic approximation costs. Its worst cases are backlit horizons from the ground, where the haze's forward peak is aimed at skylight the table cannot aim, and nadir views from inside the ceiling airs.
- **`.FloatMatchesDouble`:** F32 against F64 over the grid and decision 1's float extremes, at 1e-3 or 1e-5, every output finite.
  - Its limbs from 7.8 and 1,000 radii are now turned out of the axes, as `.ImpactParameter`'s ray is. On an axis every cross product is exact, and the first plan's mutant for the Dekker product survived there (planning note 16).
  - It builds the noon table only: a full table is over a second a world, and float against double is the same arithmetic whatever the table holds.
  - It runs decision 1's whole grid of airs, as the spec's "over the same grid" says: every mix at 0.05 bar and at its ceiling, and the giant. The first re-plan ran the ceilings and the giant only.
- **`.HomothetyInvariance`:** ruling 5's tolerances: the eyes to 1e-12 relative, the law's outputs to 1e-9.

The reference gains the isotropic option and a finer azimuth. With 12 segments it was up to 5% short of its own converged value on backlit horizons; with 24 it is within 0.5% of 48 (planning note 16). That costs the second order twice the time. So:
- `Atmosphere.Full.LawMatchesReference` and `Atmosphere.Full.MultipleScatteringGap` stay outside the default suite. Each takes about 2.5 minutes in the harness, on one process behind the lock.
- The default `.LawMatchesReference` keeps the grid's hard case, the one planning note 13 and Task 4 name: CO2 at its ceiling under the home star, along its longest paths, the horizon from the ground and the horizon from inside (blue transmittance, where one depth per bin must stand for most). That is 6 rays, as many as the earlier N2/O2 share (ground horizon and limb), so it should cost about the same 3.6 s; it is re-timed against the 5 s cap in Step 4.

The grid itself is the fixtures' (Task 5).

This task is gated on ruling 7 (Step 1). It writes the reference's two options and the tests. If one fails, the verdict table says what follows. It never says to loosen a tolerance past a ruling.

**Files:**
- Modify: `Source/DeepSpace/Atmosphere/AtmosphereReference.h`, `Source/DeepSpace/Atmosphere/AtmosphereReference.cpp` (Step 2: `SphereSegments`, `bIsotropicSecondOrder`)
- Test: create `Source/DeepSpace/Tests/AtmosphereAgreementTest.cpp`:
  - in the default suite: `DeepSpace.Atmosphere.LawMatchesReference`, `DeepSpace.Atmosphere.FloatMatchesDouble` and `DeepSpace.Atmosphere.HomothetyInvariance`;
  - outside it: `Atmosphere.Full.LawMatchesReference` and `Atmosphere.Full.MultipleScatteringGap`.
- Modify: `docs/superpowers/specs/2026-09-27-atmospheres-design.md`:
  - Step 6: decision 1 (the bins, the interface paragraph with `AT_Bins` and `AT_TABLE_PARAM`, the Chapman form, the march, the isotropic agreement), decision 3's paragraph on coefficients, decision 11's table sizes (and `T_SkyAirHere`'s), and the *Tests* section's `.LawMatchesReference` and `.HomothetyInvariance` items;
  - Step 9: the *Parallel tracks* rows for O and M, and the merge-order paragraph.

**Interfaces:**
- Consumes: everything in Tasks 3-6, and `AtmosphereTestFixtures::{FRayCase, Grid}` (Task 5). Also, read only:
  - `SkyProjection::Project`, `FSkyViewParams`, `FSkyFrame` and `FSkyBodyView` (`Sky/SkyProjection.h`);
  - `FSkySystem`, `FSkyBody` and `ESkyBodyKind` (`Sky/SkySystem.h`);
  - `FUniversePosition`.
- Produces:

```cpp
struct FReferenceAir::FOptions { ...; int32 SphereSegments = 24; bool bIsotropicSecondOrder = false; };
```

| Verdict | When | What follows |
|---|---|---|
| **PASS** | all green, both full grids too | Step 6 onward, then the merge (Step 10) |
| **BINS** | `.LawMatchesReference` misses only in transmittance, or in-scatter on long paths, alike in both builds | The bins are not carrying the spectrum. Run `.BinsCarryTheSpectrum` and `.SingleScatteringMatchesReference`; the one that fails names the part. Report; do not re-partition unasked, since ruling 6 fixed the bins. |
| **TABLE** | misses only where multiple scattering dominates (terminator suns, nadir from inside), `.SingleScatteringMatchesReference` green | The table against the isotropic reference: check `.MultiScatterTable`'s cells and the column mapping (Task 6). |
| **GAP** | `.MultipleScatteringGap` alone misses | The isotropic approximation costs more than ruling 7 allows. That is the developer's, with the printed worst cases. |
| **FLOAT FLOOR** | `.FloatMatchesDouble` misses, the printed worst cases all limb rays whose impact parameters (printed) differ by more than 1e-6 | Stop and escalate, as landing does. |

- [ ] **Step 1: The gate: rulings 5 and 7.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && git merge -q main && grep -c '^\*\*Atmosphere plan ruling 5 ' docs/superpowers/specs/2026-09-27-atmospheres-design.md && grep -c '^\*\*Atmosphere plan ruling 7 ' docs/superpowers/specs/2026-09-27-atmospheres-design.md
```

Expected: `1` and `1`. Ruling 5 is recorded (the tolerances). If ruling 7 is `0`, stop: this task waits for it. Read it.
- **Ruling 7 accepts the isotropic agreement and the 25% gap:** Step 3's code stands as written.
- **Ruling 7 names another gap bound:** `FAtmosphereFullMultipleScatteringGapTest`'s `0.25` and its `TEXT("25% or 1e-3")` take the ruled number, with `// Atmosphere plan ruling 7 (<its date>).` above them.
- **Ruling 7 asks the law to meet the full second order at 5%:** stop. That is a directional multiple-scattering table (planning note 16), a change to decisions 1 and 11 that is re-planned before anything here runs.

- [ ] **Step 2: The reference's two options.** In `Source/DeepSpace/Atmosphere/AtmosphereReference.h`:
  - In `FOptions`, replace `        int32 SphereSegments = 12;` with `        int32 SphereSegments = 24;`.
  - Also in `FOptions`, after `        int32 SecondarySteps = 32;`, add:

```cpp
        /** The second scattering sent every way alike, as the law's table
         *  sends every order past the first: what the law is held to
         *  (atmosphere plan ruling 7). */
        bool bIsotropicSecondOrder = false;
```

  - Replace `    FSecond SecondOrderAt(const FVector3d& Point, const FVector3d& View, const FVector3d& Sun, int32 Rings, int32 Segments, int32 Steps) const;` with `    FSecond SecondOrderAt(const FVector3d& Point, const FVector3d& View, const FVector3d& Sun, int32 Rings, int32 Segments, int32 Steps, bool bIsotropic) const;`.

  In `Source/DeepSpace/Atmosphere/AtmosphereReference.cpp`:
  - Replace `FReferenceAir::FSecond FReferenceAir::SecondOrderAt(const FVector3d& Point, const FVector3d& View, const FVector3d& Sun, int32 Rings, int32 Segments, int32 StepsPerRay) const` with `FReferenceAir::FSecond FReferenceAir::SecondOrderAt(const FVector3d& Point, const FVector3d& View, const FVector3d& Sun, int32 Rings, int32 Segments, int32 StepsPerRay, bool bIsotropic) const`.
  - In it, replace the two lines

```cpp
        const double IntoGas = RayleighPhase(CosView) * Solid;
        const double IntoAerosol = HenyeyGreenstein(CosView, Air.AerosolG) * Solid;
```

    with

```cpp
        // Isotropic, the second scattering sends light every way alike: the
        // law's own assumption for every order past the first.
        const double IntoGas = bIsotropic ? Direction.Share : RayleighPhase(CosView) * Solid;
        const double IntoAerosol = bIsotropic ? Direction.Share : HenyeyGreenstein(CosView, Air.AerosolG) * Solid;
```

  - In `Trace`, replace `SecondOrderAt(P, D, S, Options.SphereRings, Options.SphereSegments, Options.SecondarySteps);` with `SecondOrderAt(P, D, S, Options.SphereRings, Options.SphereSegments, Options.SecondarySteps, Options.bIsotropicSecondOrder);`.
  - In `MultiScatterSpectrum`, replace `SecondOrderAt(Point, FVector3d(0.0, 0.0, 1.0), Sun, Rings, Segments, StepsPerRay);` with `SecondOrderAt(Point, FVector3d(0.0, 0.0, 1.0), Sun, Rings, Segments, StepsPerRay, false);`.

  In the comment above `FOptions`, replace `     *  paths and most of the light are -- by SphereSegments of azimuth. */` with:

```cpp
     *  paths and most of the light are -- by SphereSegments of azimuth.
     *  Twelve segments left a backlit horizon 5% short of its converged
     *  value; 24 are within 0.5% of 48. */
```

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && ./test.sh DeepSpace.Atmosphere.ReferenceKnownValues && ./test.sh DeepSpace.Atmosphere.MultiScatterTable
```

  Expected: `passed: 1` twice: the known values hold at the finer azimuth, and the table's source is unchanged (its own 48 x 24 sphere).

- [ ] **Step 3: Write the tests.** Create `Source/DeepSpace/Tests/AtmosphereAgreementTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkySystem.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereLawMatchesReferenceTest, "DeepSpace.Atmosphere.LawMatchesReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// Outside DeepSpace., so the default suite does not run it (Global
// Constraints): minutes of second-order reference traces. Run by name.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereFullLawMatchesReferenceTest, "Atmosphere.Full.LawMatchesReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// Outside DeepSpace. too: the law against the reference's full second
// order, whose phase the law's isotropic table leaves out (ruling 7).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereFullMultipleScatteringGapTest, "Atmosphere.Full.MultipleScatteringGap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereFloatMatchesDoubleTest, "DeepSpace.Atmosphere.FloatMatchesDouble",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereHomothetyInvarianceTest, "DeepSpace.Atmosphere.HomothetyInvariance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereAgreementTestLocal
{
    using AtmosphereTestFixtures::FRayCase;

    /** Decision 1's float extremes on top of the grid: a sun 30 degrees
     *  below the horizon inside the air, and limbs from 7.8 and 1,000 radii
     *  in no special axes -- turned as .ImpactParameter turns its ray, since
     *  an eye on an axis makes every cross product exact and would let
     *  float's cancellation through unseen. */
    TArray<FRayCase> FloatExtremes(double GasH)
    {
        TArray<FRayCase> Cases;
        const double Thirty = FMath::DegreesToRadians(30.0);
        const FVector3d Under(std::cos(Thirty), 0.0, -std::sin(Thirty));
        for (const FVector3d& View : {FVector3d(0.0, 0.0, 1.0), FVector3d(1.0, 0.0, 0.0), FVector3d(-1.0, 0.0, 0.0)})
        {
            FRayCase Case;
            Case.Name = TEXT("inside, a sun 30 degrees below the horizon");
            Case.Eye = FVector3d(0.0, 0.0, 1.0 + 2.0 * GasH);
            Case.Direction = View;
            Case.Sun = Under;
            Cases.Add(Case);
        }
        const FQuat Turn(FVector(0.3, 0.5, 0.8).GetSafeNormal(), 0.7);
        for (const double R : {7.8, 1000.0})
        {
            const double SinAngle = (1.0 + 2.0 * GasH) / R;
            for (const FVector3d& Sun : {FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 0.0, -1.0)})
            {
                FRayCase Case;
                Case.Name = FString::Printf(TEXT("the limb from %.1f radii"), R);
                Case.Eye = Turn.RotateVector(FVector(0.0, 0.0, R));
                Case.Direction = Turn.RotateVector(FVector(SinAngle, 0.0, -std::sqrt(1.0 - SinAngle * SinAngle)));
                Case.Sun = Turn.RotateVector(Sun);
                Cases.Add(Case);
            }
        }
        return Cases;
    }

    bool Within(double Got, double Want, double Relative, double Absolute)
    {
        return std::isfinite(Got) && FMath::Abs(Got - Want) <= FMath::Max(Relative * FMath::Abs(Want), Absolute);
    }

    /** As a display shows it: no channel below none. The reference's
     *  spectral light can fall outside the sRGB gamut -- a deep orange whose
     *  blue is negative -- where non-negative light cannot follow and no
     *  screen could show the difference. */
    double Shown(double Channel)
    {
        return FMath::Max(Channel, 0.0);
    }

    struct FAgreement
    {
        int32 Checked = 0;
        int32 Clamped = 0;   // values Shown changed on either side (planning note 17)
        double Worst = 0.0;
        TArray<FString> Misses;
    };

    /**
     * Both builds of the law against the reference, for each air under each
     * star, over the grid's rays whose name starts with one of Rays ("ground
     * eye, horizon", say; every ray when Rays is empty), each channel
     * within Relative or Absolute. Isotropic: the reference's second
     * scattering sent every way alike, as the law's table sends every order
     * past the first (atmosphere plan ruling 7). Worst is the largest miss
     * over its allowance, met or not.
     */
    FAgreement LawAgainstReference(const TArray<AtmosphereTestFixtures::FNamedAir>& Airs, const TArray<double>& Kelvins, const TArray<FString>& Rays,
                                   bool bIsotropic, double Relative, double Absolute)
    {
        FAgreement Out;
        FReferenceAir::FOptions Options;
        Options.bIsotropicSecondOrder = bIsotropic;
        for (const AtmosphereTestFixtures::FNamedAir& Named : Airs)
        {
            for (const double Kelvin : Kelvins)
            {
                const FAtmosphere Law = FAtmosphere::Build(Named.Air, Kelvin);
                const FReferenceAir Reference(Named.Air, Kelvin);
                for (const FRayCase& Case : AtmosphereTestFixtures::Grid(Law.GetAir().GasH, Law.GetAir().Top))
                {
                    if (!Rays.IsEmpty() && !Rays.ContainsByPredicate([&Case](const FString& Ray) { return Case.Name.StartsWith(Ray); }))
                    {
                        continue;
                    }
                    FReferenceAir::FRay Ray;
                    Ray.Eye = Case.Eye;
                    Ray.Direction = Case.Direction;
                    Ray.Length = Case.Length;
                    Ray.Sun = Case.Sun;
                    const FReferenceAir::FResult Want = Reference.Trace(Ray, Options);
                    const FAtmosphereScatter F64 = AtmosphereLaw::InScatterF64(Law.GetAir(), Law.GetTable(), Case.Eye, Case.Direction, Case.Length, Case.Sun);
                    const FAtmosphereScatter F32 = AtmosphereLaw::InScatterF32(Law.GetAir(), Law.GetTable(),
                        FVector3f(Case.Eye), FVector3f(Case.Direction), static_cast<float>(Case.Length), FVector3f(Case.Sun));
                    for (const FAtmosphereScatter* Got : {&F64, &F32})
                    {
                        for (int32 C = 0; C < 3; ++C)
                        {
                            const double Raw[2][2] = {{Got->InScatter[C], Want.InScatter[C]}, {Got->Transmittance[C], Want.Transmittance[C]}};
                            const double Pairs[2][2] = {{Shown(Got->InScatter[C]), Shown(Want.InScatter[C])},
                                                        {Shown(Got->Transmittance[C]), Shown(Want.Transmittance[C])}};
                            for (int32 Q = 0; Q < 2; ++Q)
                            {
                                ++Out.Checked;
                                Out.Clamped += (Raw[Q][0] < 0.0 || Raw[Q][1] < 0.0) ? 1 : 0;
                                const double Allowance = FMath::Max(Relative * Pairs[Q][1], Absolute);
                                const double Over = std::isfinite(Pairs[Q][0]) ? FMath::Abs(Pairs[Q][0] - Pairs[Q][1]) / Allowance : 1.0e30;
                                Out.Worst = FMath::Max(Out.Worst, Over);
                                if (!(Over <= 1.0))
                                {
                                    Out.Misses.Add(FString::Printf(TEXT("%s, %.0f K, %s, %s %s channel %d: %.5f against %.5f"), Named.Name, Kelvin, *Case.Name,
                                        Got == &F64 ? TEXT("F64") : TEXT("F32"), Q == 0 ? TEXT("in-scatter") : TEXT("transmittance"), C, Pairs[Q][0], Pairs[Q][1]));
                                }
                            }
                        }
                    }
                }
            }
        }
        return Out;
    }

    void Report(FAutomationTestBase& Test, const FAgreement& Result, const TCHAR* Tolerance)
    {
        Test.AddInfo(FString::Printf(TEXT("the law against the reference: %d channel values checked, %d outside %s, the worst at %.2f of its allowance; %d changed by the gamut clamp"),
            Result.Checked, Result.Misses.Num(), Tolerance, Result.Worst, Result.Clamped));
        for (int32 I = 0; I < FMath::Min(Result.Misses.Num(), 40); ++I)
        {
            Test.AddInfo(Result.Misses[I]);
        }
        Test.TestTrue(TEXT("the grid checked something: an empty grid is not agreement"), Result.Checked > 0);
        Test.TestEqual(FString::Printf(TEXT("every channel of both builds within %s of the reference"), Tolerance), Result.Misses.Num(), 0);
    }
}

bool FAtmosphereLawMatchesReferenceTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereAgreementTestLocal;
    using namespace AtmosphereTestFixtures;

    // The default suite's share of the grid: its hard case, CO2 at its
    // ceiling under the home star, where a red dwarf's small blue must come
    // through a long horizontal path (planning note 13), along the horizon
    // from the ground and from inside the air.
    // Atmosphere.Full.LawMatchesReference is the whole grid.
    const TArray<FNamedAir> Hardest = {{TEXT("CO2 at its ceiling"), CarbonDioxide(CarbonDioxideCeilingBar)}};
    Report(*this, LawAgainstReference(Hardest, {HomeStarK}, {TEXT("ground eye, horizon"), TEXT("inside eye, horizon")}, true, 0.05, 1.0e-3), TEXT("5% or 1e-3"));
    return true;
}

bool FAtmosphereFullLawMatchesReferenceTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereAgreementTestLocal;
    using namespace AtmosphereTestFixtures;

    // Decision 1's grid: every mix at 0.05 bar and at its ceiling, under the
    // home star and the Sun, from all four eyes.
    Report(*this, LawAgainstReference(Extremes(), {HomeStarK, SunK}, {}, true, 0.05, 1.0e-3), TEXT("5% or 1e-3"));
    return true;
}

bool FAtmosphereFullMultipleScatteringGapTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereAgreementTestLocal;
    using namespace AtmosphereTestFixtures;

    // What the law's isotropic multiple scattering costs against the
    // reference's full second order, over the same grid, held to ruling 7's
    // bound so the gap cannot grow unseen. Its worst cases are backlit
    // horizons -- the haze's forward peak, which an isotropic table cannot
    // aim -- and nadir views from inside the ceiling airs.
    Report(*this, LawAgainstReference(Extremes(), {HomeStarK, SunK}, {}, false, 0.25, 1.0e-3), TEXT("25% or 1e-3"));
    return true;
}

bool FAtmosphereFloatMatchesDoubleTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereAgreementTestLocal;
    using namespace AtmosphereTestFixtures;

    // The noon table only: a full one costs over a second a world, and float
    // against double is the same arithmetic whatever the table holds.
    // Decision 1's grid of airs -- every mix at 0.05 bar and at its ceiling
    // -- and the giant.
    TArray<FAirSpec> Airs;
    for (const FNamedAir& Named : Extremes())
    {
        Airs.Add(Named.Air);
    }
    Airs.Add(Giant());
    int32 Checked = 0;
    bool bFinite = true;
    TArray<FString> Misses;
    for (const FAirSpec& Spec : Airs)
    {
        for (const double Kelvin : {HomeStarK, SunK})
        {
            const FAtmosphere Air = FAtmosphere::Build(Spec, Kelvin, EAtmosphereTable::NoonOnly);
            TArray<FRayCase> Cases = Grid(Air.GetAir().GasH, Air.GetAir().Top);
            Cases.Append(FloatExtremes(Air.GetAir().GasH));
            for (const FRayCase& Case : Cases)
            {
                // The float law on the case rounded to float, the double law on
                // the same rounded inputs: the difference is arithmetic only.
                const FVector3f Eye(Case.Eye);
                const FVector3f Direction(Case.Direction);
                const FVector3f Sun(Case.Sun);
                const float Length = static_cast<float>(Case.Length);
                const FAtmosphereScatter F32 = AtmosphereLaw::InScatterF32(Air.GetAir(), Air.GetTable(), Eye, Direction, Length, Sun);
                const FAtmosphereScatter F64 = AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), FVector3d(Eye), FVector3d(Direction),
                    static_cast<double>(Length), FVector3d(Sun));
                const FVector3d SunF32 = AtmosphereLaw::SunThroughF32(Air.GetAir(), Eye, Sun);
                const FVector3d SunF64 = AtmosphereLaw::SunThroughF64(Air.GetAir(), FVector3d(Eye), FVector3d(Sun));
                for (int32 C = 0; C < 3; ++C)
                {
                    Checked += 3;
                    bFinite &= std::isfinite(F32.InScatter[C]) && std::isfinite(F32.Transmittance[C]) && std::isfinite(SunF32[C]);
                    if (!Within(F32.InScatter[C], F64.InScatter[C], 1.0e-3, 1.0e-5)
                        || !Within(F32.Transmittance[C], F64.Transmittance[C], 1.0e-3, 1.0e-5)
                        || !Within(SunF32[C], SunF64[C], 1.0e-3, 1.0e-5))
                    {
                        const double B64 = AtmosphereLaw::ImpactParameterF64(FVector3d(Eye), FVector3d(Direction));
                        const float B32 = AtmosphereLaw::ImpactParameterF32(Eye, Direction);
                        Misses.Add(FString::Printf(TEXT("%.0f K, %s, channel %d: in-scatter %.6g against %.6g, transmittance %.6g against %.6g, sun %.6g against %.6g; impact parameter %.9f against %.9f"),
                            Kelvin, *Case.Name, C, F32.InScatter[C], F64.InScatter[C], F32.Transmittance[C], F64.Transmittance[C],
                            SunF32[C], SunF64[C], static_cast<double>(B32), B64));
                    }
                }
            }
        }
    }
    AddInfo(FString::Printf(TEXT("float against double: %d values checked, %d outside 1e-3 or 1e-5"), Checked, Misses.Num()));
    for (int32 I = 0; I < FMath::Min(Misses.Num(), 40); ++I)
    {
        AddInfo(Misses[I]);
    }
    TestTrue(TEXT("every float output finite"), bFinite);
    TestEqual(TEXT("float within 1e-3 or 1e-5 of double everywhere"), Misses.Num(), 0);
    return true;
}

bool FAtmosphereHomothetyInvarianceTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereTestFixtures;

    // A Sun and an Earth with Earth's air 1 AU apart; the ship 50 km and
    // 50,000 km up, off to one side, where the proxy is drawn at k = 1 and
    // k = 1e-3.
    FSkySystem System;
    FSkyBody& Star = System.Bodies.AddDefaulted_GetRef();
    Star.Id = TEXT("Star");
    Star.Kind = ESkyBodyKind::Star;
    Star.Position = FUniversePosition();
    Star.Radius = 6.957e10;
    Star.Luminosity = 1.0;
    Star.TemperatureK = SunK;
    FSkyBody& Body = System.Bodies.AddDefaulted_GetRef();
    Body.Id = TEXT("World");
    Body.Kind = ESkyBodyKind::Planet;
    Body.Position = FUniversePosition() + FVector(1.495978707e13, 0.0, 0.0);
    Body.Radius = EarthRadiusCm;

    // No table: what is compared is the eye, and the law's arithmetic on it.
    // A full table costs about 1.4 s and adds the same reads to both sides.
    const FAtmosphere Air = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::None);
    const FVector3d Up = FVector3d(-1.0, 0.3, 0.2).GetSafeNormal();
    const FVector3d ToStar = (Star.Position - Body.Position).GetSafeNormal();
    const FVector3d Views[] = {-Up, (FVector3d(0.0, 0.0, 1.0) - Up).GetSafeNormal(), FVector3d::CrossProduct(Up, FVector3d(0.0, 0.0, 1.0)).GetSafeNormal()};

    for (const double Altitude : {5.0e6, 5.0e9})
    {
        const FUniversePosition Ship = Body.Position + Up * (Body.Radius + Altitude);
        const FSkyFrame Frame = SkyProjection::Project(System, Ship, FSkyViewParams());
        const FSkyBodyView& View = Frame.Bodies[1];
        const double K = View.ProxyRadius / Body.Radius;
        const FVector3d ProxyEye = -View.ProxyLocation / View.ProxyRadius;
        const FVector3d TrueEye = (Ship - Body.Position) / Body.Radius;
        AddInfo(FString::Printf(TEXT("%.0f km up: the proxy is drawn at k = %.4e; eyes differ by %.3e radii"),
            Altitude / 1.0e5, K, (ProxyEye - TrueEye).Size()));
        TestTrue(TEXT("the proxy's scale is the altitude's: about 1 at 50 km, about 1e-3 at 50,000 km"),
            K > 0.5 * 5.0e6 / Altitude && K < 2.0 * 5.0e6 / Altitude);
        // Ruling 5: two roundings of one ratio, so 1e-12 on the eyes and
        // 1e-9 on what the law makes of them, not bit for bit.
        TestTrue(TEXT("the eye in the proxy's radii is the true eye in the world's, to 1e-12"),
            (ProxyEye - TrueEye).Size() <= 1.0e-12 * TrueEye.Size());
        for (const FVector3d& Direction : Views)
        {
            const FAtmosphereScatter FromProxy = AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), ProxyEye, Direction, AtmosphereLaw::NoEnd, ToStar);
            const FAtmosphereScatter FromTrue = AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), TrueEye, Direction, AtmosphereLaw::NoEnd, ToStar);
            for (int32 C = 0; C < 3; ++C)
            {
                TestTrue(FString::Printf(TEXT("%.0f km up, channel %d: the air term does not see the proxy's scale"), Altitude / 1.0e5, C),
                    FMath::Abs(FromProxy.InScatter[C] - FromTrue.InScatter[C]) <= 1.0e-9 * FMath::Max(FMath::Abs(FromTrue.InScatter[C]), 1.0e-12)
                    && FMath::Abs(FromProxy.Transmittance[C] - FromTrue.Transmittance[C]) <= 1.0e-9);
            }
        }
    }
    return true;
}

#endif
```

- [ ] **Step 4: Build and run, the full grids too.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh && ./test.sh DeepSpace.Atmosphere.HomothetyInvariance && ./test.sh DeepSpace.Atmosphere.FloatMatchesDouble && ./test.sh DeepSpace.Atmosphere.LawMatchesReference; grep -hE "the law against the reference|float against double|k = " Saved/Logs/DeepSpace.log | tail -4
cd /home/matt/Development/deepspace/.worktrees/air-optics && time ./test.sh Atmosphere.Full.LawMatchesReference; grep -h "the law against the reference" Saved/Logs/DeepSpace.log | tail -1
cd /home/matt/Development/deepspace/.worktrees/air-optics && time ./test.sh Atmosphere.Full.MultipleScatteringGap; grep -h "the law against the reference" Saved/Logs/DeepSpace.log | tail -1
cd /home/matt/Development/deepspace/.worktrees/air-optics && for T in DeepSpace.Atmosphere.HomothetyInvariance DeepSpace.Atmosphere.FloatMatchesDouble DeepSpace.Atmosphere.LawMatchesReference DeepSpace.Sky.Colour; do echo "$T"; time ./test.sh "$T"; done
```

Expected: all five pass. The harness measured:
- the full grid, 0 of 6,912 outside 5% or 1e-3, the worst at 0.80;
- the gap, 0 outside 25% or 1e-3, the worst at 0.75;
- the earlier default share (N2/O2 at its ceiling, the ground horizon and limb), 72 values, the worst at 0.66, in 3.6 s at `-O2`. The share is now CO2 at its ceiling along the ground and inside horizons, also 72 values; the harness did not run it as a share, but its rays are inside the full grid's 0.80, and it should cost about the same;
- float against double, 0 of 3,960 over the ceilings and the giant, in 0.8 s. With the 0.05 bar airs it checks 6,930, about 1.4 s; the thin airs are unmeasured.

Each info line also prints how many values the gamut clamp changed (planning note 17): report those counts with the times.

The harness could not build `.HomothetyInvariance` (it needs `SkyProjection`). The first plan's harness passed it, and its law calls are the entry points, whose signatures did not change. The printed misses (up to 40) are the report if anything fails; the verdict table says what follows. A default test more than 5 s over start-up stops the task: report it, with the time.

- [ ] **Step 5: The whole suite, and what it now costs.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && time ./test.sh
cd /home/matt/Development/deepspace && ./build.sh && time ./test.sh
```

Expected: green twice (the editor closed on both trees). The first is this tree, the second `main`, which has none of Tasks 4-8's tests; the difference is what Tasks 4-8 added to the default suite. Put it in Step 7's commit as `Measured: the default suite grew by N s.` Over 15 s, stop and report the per-test times (Global Constraints, *The default suite stays fast*).

- [ ] **Step 6: Amend the spec to the rulings.** In `docs/superpowers/specs/2026-09-27-atmospheres-design.md`:
  - In decision 1's list, replace the bullet `- **Three colour channels**, with per-world effective coefficients that` / `  already fold in the star's spectrum (decision 3).` with:

```markdown
- **Eight spectral bins**, not three colour channels (atmosphere plan
  rulings 3 and 6): each bin a run of the reference's wavelengths, narrow
  in the blue, its coefficients the star-weighted mean of its wavelengths'
  and themselves colourless, folded into linear sRGB only at the end by the
  star's own light in each bin -- so the star enters through the bins'
  weights and the folds, which C++ computes, and the shader still never
  sees a temperature (decision 3). Three channels, and any four to six
  bins, missed the reference on long paths by up to several times the
  tolerance.
```

  - Replace the paragraph beginning `` `AT_Air` is a plain struct of scalars (the per-channel coefficients and the`` and ending `(a) merges.` (eight lines) with:

```markdown
`AT_Air` is a plain struct of scalars: per spectral bin, the gas's and the
aerosol's scattering and extinction, colourless, and the star's light in
the bin as `FoldR`, `FoldG` and `FoldB`; then the shape, `GasH`,
`AerosolH`, `AerosolG` and `Top` -- 60 scalars, fifteen float4 (amended
by atmosphere plan rulings 3 and 6). The multiple-scattering table (below)
is read through a hook each side defines *before* including the file,
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
```

  - In the code block of entry points, replace `AT_REAL    AT_LogChapman   (AT_REAL X, AT_REAL CosZenith);       // ln(airmass), Schueler's form` with `AT_REAL    AT_LogChapman   (AT_REAL X, AT_REAL CosZenith);       // ln(airmass), asymptotic form`.
  - In decision 1's bullet on the optical depth to the sun, replace `exponential atmosphere, in Schueler's (2012) closed-form approximation,` with `exponential atmosphere, in the asymptotic form sqrt(pi X / 2) erfcx(sqrt(X / 2) cos z) with its first correction in 1 / X (Schueler's (2012) closed form missed it by 2.5% at 60 degrees: atmosphere optics plan, planning note 5),`, re-wrapped at the bullet's width.
  - In decision 11 (*Performance*), the table grows with the bins, a cell holding eight values, not three:
    - replace `- **One 32 x 32 multiple-scattering table per airy world**, RGBA16F (8 KB),` with `- **One 32 x 32 multiple-scattering table per airy world**, a value per bin in each cell, as one 64 x 32 RGBA16F texture (16 KB; bins 0-3 in the left half, 4-7 in the right: atmosphere plan ruling 6),`, re-wrapped;
    - replace `A system of twelve airy worlds is under 100 KB.` with `A system of twelve airy worlds is under 200 KB.`;
    - replace `` `T_SkyAirHere` (32 x 32, RGBA16F, uncompressed, no mips, never`` with `` `T_SkyAirHere` (64 x 32, RGBA16F, a world's table's layout, uncompressed, no mips, never``, re-wrapped.
  - Replace the bullet beginning `- **Along the view**, **12 samples**, spaced by the density's own` (three lines, ending `Rayleigh phase and a Henyey-Greenstein phase for the aerosol.`) with:

```markdown
- **Along the view**, **12 nodes**, spaced by the density's own
  distribution (dense where the air is); between two, each constituent's
  exact Chapman column times the logarithmic mean of what it gathers per
  unit column -- exact for the density and the view's transmittance -- and
  one node more where single scattering climbs past e^4 (atmosphere plan,
  planning note 14); single scattering with the Rayleigh phase and a
  Henyey-Greenstein phase for the aerosol.
```

  - In the paragraph beginning `**How they are held equal.**`, replace `and requires each channel` / `within 5% relative or 1e-3 absolute of the reference -- **for both the F64` / `and the F32 builds**.` with `and requires each channel within 5% relative or 1e-3 absolute of the reference with its second scattering sent every way alike, as the law's multiple scattering is -- **for both the F64 and the F32 builds** -- and within 25% or 1e-3 of the reference's full second order, the approximation's own cost (atmosphere plan ruling 7).`, re-wrapped at the paragraph's width.
  - In decision 3's paragraph beginning `**Per-world effective coefficients.**`, replace `Rendering carries three channels, but` with `Rendering carries eight spectral bins (atmosphere plan ruling 6), because`, and replace `fits **per-channel effective scattering and extinction` / `coefficients** exact in the optically thin limit and at the world's own` / `nadir column.` with `averages **per-bin scattering and extinction** exact in the optically thin limit, then folds each bin into colour by the star's light in it.`, re-wrapped; and replace `is what proves three` / `channels good enough across the angles that matter;` with `is what proves the bins good enough across the angles that matter;`; and replace `the star colour is` / `already in the coefficients, so the shader never sees a temperature.` with `the star enters through the bins' weights and folds, which C++ computes, so the shader never sees a temperature.`, re-wrapped.
  - In the *Tests* section:
    - In the `.LawMatchesReference` item, replace `(decision 1's grid and tolerance),` with `(decision 1's grid and tolerance, the reference's second scattering isotropic as the law's is: atmosphere plan ruling 7),`. After `for F64 and F32.`, add ` The default suite runs its hardest air along its longest paths; the whole grid is Atmosphere.Full.LawMatchesReference, and the full second order's gap, within 25% or 1e-3, Atmosphere.Full.MultipleScatteringGap, both run by name before a merge.`
    - In the `.HomothetyInvariance` item, replace the words `the same term, bit for bit in double, at k = 1 and k = 1e-3,` (they wrap after `in`) with `the same term in double, at k = 1 and k = 1e-3, the eyes within 1e-12 relative and the law's outputs within 1e-9 (atmosphere plan ruling 5: the proxy's eye and the true eye are one ratio formed by two roundings),`, wrapped at the item's width.

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && grep -c "Three colour channels\|bit for bit in\|proves three" docs/superpowers/specs/2026-09-27-atmospheres-design.md
cd /home/matt/Development/deepspace/.worktrees/air-optics && grep -c "per-channel coefficients and the\|returning an .AT_Rgb.: in HLSL\|Schueler's form\|Schueler's (2012) closed-form\|already in the coefficients\|(8 KB)\|under 100 KB\|T_SkyAirHere. (32 x 32" docs/superpowers/specs/2026-09-27-atmospheres-design.md
```

  Expected: `0` twice: the three channels, the old interface paragraph, Schueler's form, the star "in the coefficients" and decision 11's three-channel sizes are all gone.

- [ ] **Step 7: Commit.**

```bash
git -C /home/matt/Development/deepspace/.worktrees/air-optics add Source/DeepSpace/Atmosphere/AtmosphereReference.h Source/DeepSpace/Atmosphere/AtmosphereReference.cpp Source/DeepSpace/Tests/AtmosphereAgreementTest.cpp docs/superpowers/specs/2026-09-27-atmospheres-design.md
git -C /home/matt/Development/deepspace/.worktrees/air-optics commit -F - <<'MSG'
test(atmosphere): the law against the reference, float against double, the homothety

Decision 1's three agreements as atmosphere plan rulings 3, 5, 6 and 7
leave them. LawMatchesReference: both builds of the eight-bin law against
the reference, its second scattering isotropic as the law's table is,
over four eyes, four views and three suns, every mix at 0.05 bar and at
ruling 2's ceilings, under the home star and the Sun, within 5% or 1e-3
everywhere -- the whole grid as Atmosphere.Full.LawMatchesReference, its
hardest air along its longest paths in the default suite.
Atmosphere.Full.MultipleScatteringGap holds the full second order within
ruling 7's 25% or 1e-3: what the isotropic table costs, worst on backlit
horizons. FloatMatchesDouble: F32 within 1e-3 or 1e-5 of F64, every output
finite, its limbs turned out of the axes where float's cancellation can
show. HomothetyInvariance: ruling 5's 1e-12 on the eyes and 1e-9 on the
term. The reference's sphere takes 24 azimuth segments, not 12, which
left a backlit horizon 5% short of converged. The spec's decision 1 and
Tests items say the same.

Measured: DeepSpace.Atmosphere.HomothetyInvariance costs N s over start-up.
Measured: DeepSpace.Atmosphere.FloatMatchesDouble costs N s over start-up.
Measured: DeepSpace.Atmosphere.LawMatchesReference costs N s over start-up.
Measured: Atmosphere.Full.LawMatchesReference takes N s.
Measured: Atmosphere.Full.MultipleScatteringGap takes N s.
Measured: the default suite grew by N s.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

  Replace each `N` with the measured seconds before committing, and check with `git log -1 --format=%B | grep -c ' N s'`, which must print `0` (*Conventions*).

- [ ] **Step 8: Prove the tests can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '    return AT_REAL(3.0) / (AT_REAL(16.0) * AT_PI) * (AT_REAL(1.0) + Cos * Cos);' '    return AT_REAL(3.0) / (AT_REAL(8.0) * AT_PI) * (AT_REAL(1.0) + Cos * Cos);' DeepSpace.Atmosphere.LawMatchesReference
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Tests/AtmosphereAgreementTest.cpp '        Options.bIsotropicSecondOrder = bIsotropic;' '        Options.bIsotropicSecondOrder = false;' DeepSpace.Atmosphere.LawMatchesReference
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Tests/AtmosphereAgreementTest.cpp 'Report(*this, LawAgainstReference(Extremes(), {HomeStarK, SunK}, {}, true, 0.05, 1.0e-3), TEXT("5% or 1e-3"));' 'Report(*this, LawAgainstReference(Extremes(), {HomeStarK, SunK}, {TEXT("nowhere")}, true, 0.05, 1.0e-3), TEXT("5% or 1e-3"));' Atmosphere.Full.LawMatchesReference
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '    AT_REAL PhaseAerosol = AT_HenyeyGreenstein(CosView, A.AerosolG);' '    AT_REAL PhaseAerosol = AT_HenyeyGreenstein(-CosView, A.AerosolG);' Atmosphere.Full.MultipleScatteringGap
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '    return PQ + E;' '    return PQ;' DeepSpace.Atmosphere.FloatMatchesDouble
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Shaders/Private/Atmosphere.ush '    return (B - AT_REAL(1.0)) + S * S / (AT_sqrt(B * B + S * S) + B);' '    return AT_sqrt(B * B + S * S) - AT_REAL(1.0);' DeepSpace.Atmosphere.FloatMatchesDouble
cd /home/matt/Development/deepspace/.worktrees/air-optics && Tools/mutate.sh Source/DeepSpace/Sky/SkyProjection.cpp 'const double Centre = View.ProxyRadius / Shape.Sin;' 'const double Centre = View.ProxyRadius / Shape.Sin * 1.001;' DeepSpace.Atmosphere.HomothetyInvariance
cd /home/matt/Development/deepspace/.worktrees/air-optics && ./build.sh
```

Expected: `KILLED` seven times:
1. The law's Rayleigh phase doubles, and the reference's does not.
2. The default share is held to the full second order at 5%, and the backlit horizon's forward-scattered skylight misses.
3. The full grid checks no ray at all, which `Report`'s "the grid checked something" catches: an empty grid is not agreement.
4. The haze scatters backward, and the gap on the backlit horizons passes 25%.
5. The Dekker product's error term is gone, and the float impact parameter loses the tangent from the turned 1,000-radii limb.
6. Float loses a near sample's height in `R - 1`.
7. The proxy stops being a homothety.

Mutants 1-6 were run in the plan's harness and killed, mutant 2 on the earlier N2/O2 share. On the CO2 share it is unmeasured. If it survives, the share gains N2/O2 at its ceiling along the ground horizon (three rays, about 1.8 s more), and the test is re-timed; a surviving mutant strengthens the test, never weakens the mutation. The seventh is the first plan's, unchanged. Mutant 4 takes as long as the gap's full grid.

The seventh mutation touches `SkyProjection.cpp`, which is not this plan's file: `Tools/mutate.sh` restores it and rebuilds, and nothing of it is committed.

- [ ] **Step 9: The spec's shim rows** (planning note 8). In `docs/superpowers/specs/2026-09-27-atmospheres-design.md`, *Parallel tracks and file ownership*:
  - In track O's row, replace `-- (its shims are copied from landing's `WR_` ones while (a) is unmerged; **O's first commit after landing (a) merges replaces the copies with landing's shared shim header**, and M waits on that commit)` with `-- (its `AT_` shims stay inline in `Atmosphere.ush`: landing (a) made no shared shim header, and `WorldRelief.ush` keeps its `WR_` shims inline; whether to extract one shared subset header is **orbital slice 1's first decision**, and `WorldRelief.ush` is landing track T's through slice (b))`.
  - In track M's row, replace `landing (a) merged; O's shim-unification commit; G's `AirFacts.h` (agreed first as a header)` with `landing (a) merged; track O merged; the shim decision, orbital slice 1's first; G's `AirFacts.h` (agreed first as a header)`.
  - In the paragraph under the table, replace `after landing (a), O's shim unification, G, then M,` with `after landing (a), G, then M (the shim decision first),`.

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && grep -c "shim-unification\|shared shim header\*\*" docs/superpowers/specs/2026-09-27-atmospheres-design.md
git -C /home/matt/Development/deepspace/.worktrees/air-optics add docs/superpowers/specs/2026-09-27-atmospheres-design.md
git -C /home/matt/Development/deepspace/.worktrees/air-optics commit -F - <<'MSG'
docs(atmospheres): M waits on the shim decision, not a commit no one will make

Landing slice (a) keeps its WR_ shims inline in WorldRelief.ush and makes
no shared shim header, so track O's "first commit after landing (a)
merges" has nothing to replace its copies with, and track M, which waited
on that commit, would wait for ever. O's AT_ shims stay inline; whether to
extract one shared subset header is orbital slice 1's first decision
(atmosphere plan, planning note 8).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

Expected: `0` from the grep (no stale wording left), then the commit.

- [ ] **Step 10: Merge track O.** On the developer's word through the orchestrator, and only when Tasks 1-8 are committed with every ruling they encode (1 and 3-7) recorded, `./test.sh` is green in `air-optics`, and both full grids are green there:

```bash
cd /home/matt/Development/deepspace/.worktrees/air-optics && git merge -q main && ./build.sh && ./test.sh && ./test.sh Atmosphere.Full.LawMatchesReference && ./test.sh Atmosphere.Full.MultipleScatteringGap
cd /home/matt/Development/deepspace && git merge --no-ff feat/air-optics -m "merge: air-optics (atmospheres slice 1, track O: the pure optics)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
cd /home/matt/Development/deepspace && ./build.sh && ./test.sh
```

Expected: both runs green in the tree, a clean merge (track O touches no landing file, and its spec hunks are its own), and the whole suite green on `main`. If either run in the tree is red, do not merge: report. Use superpowers:finishing-a-development-branch for the tree.

---
# Track G: procgen's air

## Task 9 (G1): `AirFacts` -- what an air is, derived, and the guarantees that bound it

Decision 2 says physical facts belong to procgen, and looks to the sky. This task writes the pure derivations beside the generator:
- each mix's constants;
- scale height, column and optical depths;
- Jeans retention;
- the nadir-haze pressure ceiling (decision 4);
- the smooth ceiling, and a giant's disc pressure.

`EAirMix` lives here too. It is all new files, so nothing waits on landing. The guarantees reopen `namespace GenGuarantees` in `AirFacts.h` for this task only, so that it touches no existing file; Task 10, which edits `GenPriors.h` anyway, moves them into `GenPriors.h`'s `GenGuarantees`, beside every other number no ini line may move, where CLAUDE.md sends a reader for them.

**Files:**
- Create the tree: `.worktrees/air-procgen` on `feat/air-procgen`
- Create: `Source/DeepSpace/Universe/AirFacts.h`, `Source/DeepSpace/Universe/AirFacts.cpp`
- Test: create `Source/DeepSpace/Tests/AirFactsTest.cpp` (`DeepSpace.Universe.Gases`)

**Interfaces:**
- Consumes: `UniverseUnits::CmPerEarthRadius` (read only).
- Produces:

```cpp
enum class EAirMix : uint8 { None, NitrogenOxygen, CarbonDioxide, HydrogenHelium };
namespace GenGuarantees { inline constexpr double ExobaseFactor = 4.0, RetainedAbove = 6.0, LostBelow = 4.0, MaxNadirTau450 = 0.5; }
struct FAirMixFacts { double MeanMolecularWeight, RayleighPerAir, AerosolTau550PerBar, AerosolScaleHeightKm, AerosolAngstrom,
                      AerosolAsymmetry, AerosolAlbedo450, AerosolAlbedo650, OzoneTau600PerBar; };
namespace AirFacts
{
    inline constexpr double BoltzmannJPerK, AtomicMassKg, StandardGravityMS2, EarthRayleighTau550PerBar = 0.097, EarthAirMolecularWeight = 28.97;
    inline constexpr int32 NumMixes = 3;
    const FAirMixFacts& Facts(EAirMix Mix);
    const TCHAR* Name(EAirMix Mix);                        // "none", "nitrogen-oxygen", "carbon-dioxide", "hydrogen-helium"
    double ScaleHeightKm(EAirMix Mix, double TemperatureK, double GravityEarth);
    double ColumnRelativeToEarth(EAirMix Mix, double PressureBar, double GravityEarth);
    double RayleighTau550(EAirMix Mix, double PressureBar, double GravityEarth);
    double AerosolTau550(EAirMix Mix, double PressureBar, double GravityEarth);
    double OzoneTau600(EAirMix Mix, double PressureBar, double GravityEarth);
    double NadirTau450(EAirMix Mix, double PressureBar, double GravityEarth);
    double JeansLambda(EAirMix Mix, double MassEarth, double RadiusEarth, double EquilibriumK);
    double Retention(EAirMix Mix, double MassEarth, double RadiusEarth, double EquilibriumK);
    double PressureCeilingBar(EAirMix Mix, double GravityEarth, double RetentionOfMix);
    double SmoothCeiling(double DrawnBar, double CeilingBar);
    double GiantDiscPressureBar(double GravityEarth);
}
```

- [ ] **Step 1: The tree.**

```bash
cd /home/matt/Development/deepspace && git worktree add .worktrees/air-procgen -b feat/air-procgen main
```

- [ ] **Step 2: Write the failing test.** Create `Source/DeepSpace/Tests/AirFactsTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Universe/AirFacts.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAirFactsTest,
    "DeepSpace.Universe.Gases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AirFactsTestLocal
{
    /** The generator's radius law for rock: R = M^0.28. */
    double RockyRadius(double MassEarth)
    {
        return std::pow(MassEarth, 0.28);
    }

    bool Between(double Value, double Low, double High)
    {
        return Value >= Low && Value <= High;
    }
}

bool FAirFactsTest::RunTest(const FString& Parameters)
{
    using namespace AirFactsTestLocal;
    const EAirMix Mixes[] = {EAirMix::NitrogenOxygen, EAirMix::CarbonDioxide, EAirMix::HydrogenHelium};

    // -- Retention: who keeps what (decision 2's measured cases) -----------------
    const double EarthN2 = AirFacts::JeansLambda(EAirMix::NitrogenOxygen, 1.0, 1.0, 255.0);
    const double EarthH2 = AirFacts::JeansLambda(EAirMix::HydrogenHelium, 1.0, 1.0, 255.0);
    AddInfo(FString::Printf(TEXT("an Earth: lambda %.2f for N2/O2, %.2f for H2/He"), EarthN2, EarthH2));
    TestTrue(TEXT("an Earth holds N2/O2 (lambda about 12)"), Between(EarthN2, 11.5, 12.5));
    TestTrue(TEXT("and loses H2/He (lambda about 3.4)"), Between(EarthH2, 3.2, 3.6));
    TestEqual(TEXT("fully"), AirFacts::Retention(EAirMix::NitrogenOxygen, 1.0, 1.0, 255.0), 1.0);
    TestEqual(TEXT("and none of it"), AirFacts::Retention(EAirMix::HydrogenHelium, 1.0, 1.0, 255.0), 0.0);

    const double Small = AirFacts::JeansLambda(EAirMix::NitrogenOxygen, 0.3, RockyRadius(0.3), 320.0);
    TestTrue(FString::Printf(TEXT("the smallest, hottest temperate world (0.3 M_E, 320 K) still holds N2/O2: lambda %.2f"), Small), Between(Small, 6.6, 7.2));
    TestEqual(TEXT("fully"), AirFacts::Retention(EAirMix::NitrogenOxygen, 0.3, RockyRadius(0.3), 320.0), 1.0);
    TestEqual(TEXT("and CO2"), AirFacts::Retention(EAirMix::CarbonDioxide, 0.3, RockyRadius(0.3), 320.0), 1.0);

    const double Cold = AirFacts::JeansLambda(EAirMix::HydrogenHelium, 10.0, RockyRadius(10.0), 200.0);
    TestTrue(FString::Printf(TEXT("a cold 10 M_E super-Earth at 200 K holds H2/He: lambda %.2f"), Cold), Between(Cold, 8.4, 9.0));

    const double Halfway = AirFacts::Retention(EAirMix::HydrogenHelium, 1.0, 1.0, 255.0 * FMath::Square(EarthH2 / 5.0));
    TestTrue(FString::Printf(TEXT("halfway between lost (4) and held (6), retention is the smoothstep's middle: %.4f"), Halfway),
        FMath::Abs(Halfway - 0.5) < 1.0e-9);

    // -- Scale heights -----------------------------------------------------------
    TestTrue(TEXT("Earth air at 255 K and 1 g: about 7.5 km"), Between(AirFacts::ScaleHeightKm(EAirMix::NitrogenOxygen, 255.0, 1.0), 7.3, 7.6));
    TestTrue(TEXT("H2/He on a 2.6 g giant at 110 K: about 15 km"), Between(AirFacts::ScaleHeightKm(EAirMix::HydrogenHelium, 110.0, 318.0 / 121.0), 15.0, 16.0));

    // -- The nadir guarantee as a pressure (decision 4) ------------------------------
    const double N2Ceiling = AirFacts::PressureCeilingBar(EAirMix::NitrogenOxygen, 1.0, 1.0);
    const double CO2Ceiling = AirFacts::PressureCeilingBar(EAirMix::CarbonDioxide, 1.0, 1.0);
    const double H2Ceiling = AirFacts::PressureCeilingBar(EAirMix::HydrogenHelium, 1.0, 1.0);
    AddInfo(FString::Printf(TEXT("ceilings at 1 g: N2/O2 %.4f bar, CO2 %.4f, H2/He %.4f"), N2Ceiling, CO2Ceiling, H2Ceiling));
    TestTrue(TEXT("N2/O2 about 1.8 bar"), Between(N2Ceiling, 1.78, 1.82));
    TestTrue(TEXT("CO2 about 1.17 bar"), Between(CO2Ceiling, 1.15, 1.19));
    TestTrue(TEXT("H2/He about 0.9 bar"), Between(H2Ceiling, 0.88, 0.91));
    for (const EAirMix Mix : Mixes)
    {
        for (const double G : {0.6, 1.0, 3.0})
        {
            const double Ceiling = AirFacts::PressureCeilingBar(Mix, G, 1.0);
            TestTrue(FString::Printf(TEXT("%s at %.1f g: straight down at 450 nm the ceiling is the guarantee's 0.5"), AirFacts::Name(Mix), G),
                FMath::Abs(AirFacts::NadirTau450(Mix, Ceiling, G) - GenGuarantees::MaxNadirTau450) < 1.0e-9);
        }
        TestTrue(FString::Printf(TEXT("%s: a heavier world holds more, the same pressure being less column"), AirFacts::Name(Mix)),
            FMath::Abs(AirFacts::PressureCeilingBar(Mix, 2.0, 1.0) / AirFacts::PressureCeilingBar(Mix, 1.0, 1.0) - 2.0) < 1.0e-12);
        TestTrue(FString::Printf(TEXT("%s: a world that barely holds its gas holds less of it"), AirFacts::Name(Mix)),
            FMath::Abs(AirFacts::PressureCeilingBar(Mix, 1.0, 0.25) / AirFacts::PressureCeilingBar(Mix, 1.0, 1.0) - 0.25) < 1.0e-12);
    }

    // -- A giant's disc (decision 2) --------------------------------------------------
    const double Light = AirFacts::GiantDiscPressureBar(15.0 / 121.0);
    const double Jupiter = AirFacts::GiantDiscPressureBar(318.0 / 121.0);
    const double Heavy = AirFacts::GiantDiscPressureBar(3000.0 / 121.0);
    AddInfo(FString::Printf(TEXT("giants' discs: 15 M_E %.4f bar, 318 M_E %.3f, 3,000 M_E %.2f"), Light, Jupiter, Heavy));
    TestTrue(TEXT("a 15 M_E giant's disc is its 0.11 bar level"), Between(Light, 0.10, 0.12));
    TestTrue(TEXT("a Jupiter's about 2.4 bar"), Between(Jupiter, 2.3, 2.45));
    TestTrue(TEXT("a 3,000 M_E giant's about 22 bar"), Between(Heavy, 21.0, 23.0));

    // -- The smooth ceiling -------------------------------------------------------------
    TestTrue(TEXT("far under the ceiling, the draw stands"), FMath::Abs(AirFacts::SmoothCeiling(0.01, 1.0) / 0.01 - 1.0) < 1.0e-7);
    TestTrue(TEXT("far over it, the ceiling"), FMath::Abs(AirFacts::SmoothCeiling(100.0, 1.0) - 1.0) < 1.0e-7);
    TestTrue(TEXT("at it, 2^-1/4 of it: the curve bends, it does not clip"),
        FMath::Abs(AirFacts::SmoothCeiling(1.0, 1.0) - std::pow(2.0, -0.25)) < 1.0e-12);
    double Previous = 0.0;
    bool bRises = true;
    bool bUnder = true;
    for (double Drawn = 0.05; Drawn < 20.0; Drawn *= 1.1)
    {
        const double P = AirFacts::SmoothCeiling(Drawn, 1.8);
        bRises &= P > Previous;
        bUnder &= P < FMath::Min(Drawn, 1.8);
        Previous = P;
    }
    TestTrue(TEXT("rising with the draw"), bRises);
    TestTrue(TEXT("and always under both the draw and the ceiling"), bUnder);
    TestEqual(TEXT("no ceiling, no air"), AirFacts::SmoothCeiling(1.0, 0.0), 0.0);

    // -- No air ---------------------------------------------------------------------------
    TestEqual(TEXT("no mix has no scale height"), AirFacts::ScaleHeightKm(EAirMix::None, 255.0, 1.0), 0.0);
    TestEqual(TEXT("nor any depth"), AirFacts::NadirTau450(EAirMix::None, 1.0, 1.0), 0.0);
    TestEqual(TEXT("nor any retention"), AirFacts::Retention(EAirMix::None, 1.0, 1.0, 255.0), 0.0);
    TestEqual(TEXT("and is called none"), FString(AirFacts::Name(EAirMix::None)), FString(TEXT("none")));
    return true;
}

#endif
```

  The halfway case: `lambda` goes as `1 / sqrt(T)`, so scaling Earth's temperature by `(lambda_Earth / 5)^2` puts H2/He's lambda at exactly 5. That is the middle of the 4-to-6 smoothstep, where retention is exactly 0.5.

- [ ] **Step 3: Run it; expect a compile failure.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh
```

Expected: FAIL, `'Universe/AirFacts.h' file not found`.

- [ ] **Step 4: The facts.** Create `Source/DeepSpace/Universe/AirFacts.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * What a world's air is, as procgen's facts (atmospheres decision 2): the
 * two drawn facts -- the mix and the surface pressure -- live on FPlanet;
 * everything else is derived here, pure, from them and the world's mass,
 * radius and equilibrium temperature. Looks are the sky's
 * (PlanetAir::SpecOf turns these into optics); nothing here knows a colour.
 */

/** Which air a world holds. None for barren and ice worlds (ruled
 *  airless); one of the three for terrestrial and ocean worlds, drawn; and
 *  hydrogen and helium for every giant. A mix names a whole air: its
 *  aerosol and absorber follow it (decision 5). */
enum class EAirMix : uint8 { None, NitrogenOxygen, CarbonDioxide, HydrogenHelium };

/**
 * The air's guarantees (decisions 2 and 4): numbers no ini line may move,
 * because an invariant rests on each. This reopens GenPriors.h's namespace
 * of the same name.
 */
namespace GenGuarantees
{
    /** The top of an air is far hotter than its surface -- Earth's
     *  thermosphere runs near 1,000 K over a 255 K equilibrium -- and it is
     *  the top that loses gas: the exobase is taken at this many times
     *  EquilibriumK. It decides which worlds may hold hydrogen. */
    inline constexpr double ExobaseFactor = 4.0;

    /** Escape speed over the molecules' root-mean-square speed at the
     *  exobase: at or above RetainedAbove a gas is held over a star's age;
     *  at or below LostBelow it is gone; a smoothstep between. */
    inline constexpr double RetainedAbove = 6.0;
    inline constexpr double LostBelow = 4.0;

    /** No air's total extinction straight down at 450 nm (Rayleigh plus
     *  aerosol, not absorption) exceeds this (ruling 8): every airy world's
     *  surface stays legible from orbit. Bounds the fact, as a per-world
     *  pressure ceiling, never the rendering. */
    inline constexpr double MaxNadirTau450 = 0.5;
}

/** One mix's constants: what it is made of, and the aerosol and absorber
 *  that come with it (decision 5). Every aerosol number is a first estimate
 *  for the reference, the corpus and the playtest to move. */
struct FAirMixFacts
{
    double MeanMolecularWeight = 0.0;

    /** Rayleigh cross-section per molecule relative to Earth's air. */
    double RayleighPerAir = 0.0;

    /** Aerosol extinction straight down at 550 nm, per bar at 1 g; like the
     *  gas, it goes with the column, P / g. */
    double AerosolTau550PerBar = 0.0;
    double AerosolScaleHeightKm = 0.0;
    double AerosolAngstrom = 1.0;
    double AerosolAsymmetry = 0.0;
    double AerosolAlbedo450 = 1.0;
    double AerosolAlbedo650 = 1.0;

    /** The ozone-like absorber at its Chappuis peak, 600 nm, per bar at 1 g.
     *  0 for a mix with none. */
    double OzoneTau600PerBar = 0.0;
};

namespace AirFacts
{
    inline constexpr double BoltzmannJPerK = 1.380649e-23;
    inline constexpr double AtomicMassKg = 1.66053906660e-27;
    inline constexpr double StandardGravityMS2 = 9.80665;

    /** Earth's air: its Rayleigh depth straight down at 550 nm per bar, and
     *  its mean molecular weight -- what every other mix is measured
     *  against. */
    inline constexpr double EarthRayleighTau550PerBar = 0.097;
    inline constexpr double EarthAirMolecularWeight = 28.97;

    /** The mixes a world can draw: EAirMix's after None, in order. */
    inline constexpr int32 NumMixes = 3;

    DEEPSPACE_API const FAirMixFacts& Facts(EAirMix Mix);

    /** The corpus's word for a mix. */
    DEEPSPACE_API const TCHAR* Name(EAirMix Mix);

    /** k T / (mu m_u g), km, with T the equilibrium temperature (no
     *  greenhouse: *Deliberate fakes*). 0 for None or a non-positive input. */
    DEEPSPACE_API double ScaleHeightKm(EAirMix Mix, double TemperatureK, double GravityEarth);

    /** Molecules above the surface relative to Earth's:
     *  (P / 1 bar) (1 / g) (28.97 / mu). */
    DEEPSPACE_API double ColumnRelativeToEarth(EAirMix Mix, double PressureBar, double GravityEarth);

    /** The gas's Rayleigh depth straight down at 550 nm: Earth's per bar,
     *  times the column, times the mix's cross-section. */
    DEEPSPACE_API double RayleighTau550(EAirMix Mix, double PressureBar, double GravityEarth);

    /** The aerosol's extinction straight down at 550 nm: per bar at 1 g,
     *  times P / g. */
    DEEPSPACE_API double AerosolTau550(EAirMix Mix, double PressureBar, double GravityEarth);

    /** The absorber's depth straight down at 600 nm: per bar at 1 g, times
     *  P / g. */
    DEEPSPACE_API double OzoneTau600(EAirMix Mix, double PressureBar, double GravityEarth);

    /** Rayleigh plus aerosol extinction straight down at 450 nm, the bluest
     *  channel's centre and the one that hazes first: what MaxNadirTau450
     *  bounds. */
    DEEPSPACE_API double NadirTau450(EAirMix Mix, double PressureBar, double GravityEarth);

    /** Escape speed over the molecules' root-mean-square speed at the
     *  exobase (ExobaseFactor x EquilibriumK); 0 for None. */
    DEEPSPACE_API double JeansLambda(EAirMix Mix, double MassEarth, double RadiusEarth, double EquilibriumK);

    /** 0 at or below LostBelow, 1 at or above RetainedAbove, a smoothstep
     *  between. */
    DEEPSPACE_API double Retention(EAirMix Mix, double MassEarth, double RadiusEarth, double EquilibriumK);

    /** The most of this mix a world of this gravity may hold, bar: where
     *  NadirTau450 reaches MaxNadirTau450, times the world's retention of
     *  the mix (a world that barely holds its gas holds less of it). */
    DEEPSPACE_API double PressureCeilingBar(EAirMix Mix, double GravityEarth, double RetentionOfMix);

    /** A drawn pressure under a ceiling, bent rather than clipped:
     *  1 / (Drawn^-4 + Ceiling^-4)^(1/4), so no pile of worlds stacks at the
     *  cap. 0 when either is not positive. */
    DEEPSPACE_API double SmoothCeiling(double DrawnBar, double CeilingBar);

    /** A giant's "surface": where the air above it reaches MaxNadirTau450,
     *  bar -- its hydrogen and helium's ceiling at full retention. */
    DEEPSPACE_API double GiantDiscPressureBar(double GravityEarth);
}
```

  Create `Source/DeepSpace/Universe/AirFacts.cpp`:

```cpp
#include "Universe/AirFacts.h"

#include "Universe/UniverseUnits.h"

#include <cmath>

namespace AirFactsLocal
{
    /** Indexed by EAirMix. The aerosol and absorber are decision 5's table:
     *  N2/O2 a thin haze and an ozone-like Chappuis band; CO2 fine
     *  iron-oxide dust that eats blue, as on Mars; H2/He almost clean. */
    const FAirMixFacts Mixes[] = {
        FAirMixFacts{},
        FAirMixFacts{28.97, 1.0, 0.05, 1.2, 1.0, 0.76, 0.95, 0.95, 0.0415},
        FAirMixFacts{44.0, 2.4, 0.08, 2.0, 0.3, 0.70, 0.85, 0.95, 0.0},
        FAirMixFacts{2.3, 0.2, 0.01, 1.0, 1.0, 0.70, 0.99, 0.99, 0.0},
    };
}

const FAirMixFacts& AirFacts::Facts(EAirMix Mix)
{
    return AirFactsLocal::Mixes[static_cast<int32>(Mix)];
}

const TCHAR* AirFacts::Name(EAirMix Mix)
{
    switch (Mix)
    {
    case EAirMix::NitrogenOxygen: return TEXT("nitrogen-oxygen");
    case EAirMix::CarbonDioxide: return TEXT("carbon-dioxide");
    case EAirMix::HydrogenHelium: return TEXT("hydrogen-helium");
    default: return TEXT("none");
    }
}

double AirFacts::ScaleHeightKm(EAirMix Mix, double TemperatureK, double GravityEarth)
{
    const double Mu = Facts(Mix).MeanMolecularWeight;
    if (!(Mu > 0.0 && TemperatureK > 0.0 && GravityEarth > 0.0))
    {
        return 0.0;
    }
    return BoltzmannJPerK * TemperatureK / (Mu * AtomicMassKg * GravityEarth * StandardGravityMS2) / 1000.0;
}

double AirFacts::ColumnRelativeToEarth(EAirMix Mix, double PressureBar, double GravityEarth)
{
    const double Mu = Facts(Mix).MeanMolecularWeight;
    if (!(Mu > 0.0 && GravityEarth > 0.0))
    {
        return 0.0;
    }
    return PressureBar / GravityEarth * (EarthAirMolecularWeight / Mu);
}

double AirFacts::RayleighTau550(EAirMix Mix, double PressureBar, double GravityEarth)
{
    return EarthRayleighTau550PerBar * ColumnRelativeToEarth(Mix, PressureBar, GravityEarth) * Facts(Mix).RayleighPerAir;
}

double AirFacts::AerosolTau550(EAirMix Mix, double PressureBar, double GravityEarth)
{
    return GravityEarth > 0.0 ? Facts(Mix).AerosolTau550PerBar * PressureBar / GravityEarth : 0.0;
}

double AirFacts::OzoneTau600(EAirMix Mix, double PressureBar, double GravityEarth)
{
    return GravityEarth > 0.0 ? Facts(Mix).OzoneTau600PerBar * PressureBar / GravityEarth : 0.0;
}

double AirFacts::NadirTau450(EAirMix Mix, double PressureBar, double GravityEarth)
{
    const double Rayleigh = RayleighTau550(Mix, PressureBar, GravityEarth) * std::pow(550.0 / 450.0, 4.0);
    const double Aerosol = AerosolTau550(Mix, PressureBar, GravityEarth) * std::pow(450.0 / 550.0, -Facts(Mix).AerosolAngstrom);
    return Rayleigh + Aerosol;
}

double AirFacts::JeansLambda(EAirMix Mix, double MassEarth, double RadiusEarth, double EquilibriumK)
{
    const double Mu = Facts(Mix).MeanMolecularWeight;
    if (!(Mu > 0.0 && MassEarth > 0.0 && RadiusEarth > 0.0 && EquilibriumK > 0.0))
    {
        return 0.0;
    }
    const double RadiusM = RadiusEarth * UniverseUnits::CmPerEarthRadius / 100.0;
    const double GravityMS2 = StandardGravityMS2 * MassEarth / (RadiusEarth * RadiusEarth);
    const double Escape = std::sqrt(2.0 * GravityMS2 * RadiusM);
    const double Exobase = GenGuarantees::ExobaseFactor * EquilibriumK;
    const double Rms = std::sqrt(3.0 * BoltzmannJPerK * Exobase / (Mu * AtomicMassKg));
    return Escape / Rms;
}

double AirFacts::Retention(EAirMix Mix, double MassEarth, double RadiusEarth, double EquilibriumK)
{
    if (Mix == EAirMix::None)
    {
        return 0.0;
    }
    const double Lambda = JeansLambda(Mix, MassEarth, RadiusEarth, EquilibriumK);
    const double T = FMath::Clamp((Lambda - GenGuarantees::LostBelow) / (GenGuarantees::RetainedAbove - GenGuarantees::LostBelow), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

double AirFacts::PressureCeilingBar(EAirMix Mix, double GravityEarth, double RetentionOfMix)
{
    const double PerBar = NadirTau450(Mix, 1.0, GravityEarth);
    return PerBar > 0.0 ? FMath::Max(RetentionOfMix, 0.0) * GenGuarantees::MaxNadirTau450 / PerBar : 0.0;
}

double AirFacts::SmoothCeiling(double DrawnBar, double CeilingBar)
{
    if (!(DrawnBar > 0.0 && CeilingBar > 0.0))
    {
        return 0.0;
    }
    // 1 / (D^-4 + C^-4)^(1/4), written as D / (1 + (D / C)^4)^(1/4) so no
    // small draw raises anything to a huge power.
    return DrawnBar / std::pow(1.0 + std::pow(DrawnBar / CeilingBar, 4.0), 0.25);
}

double AirFacts::GiantDiscPressureBar(double GravityEarth)
{
    return PressureCeilingBar(EAirMix::HydrogenHelium, GravityEarth, 1.0);
}
```

- [ ] **Step 5: Build and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh && ./test.sh DeepSpace.Universe.Gases; grep -hE "an Earth: lambda|ceilings at 1 g|giants' discs" Saved/Logs/DeepSpace.log | tail -3
```

Expected: `passed: 1`. The info lines print lambda about 11.9 and 3.36, the ceilings about 1.801, 1.171 and 0.897, and the discs about 0.111, 2.357 and 22.2.

- [ ] **Step 6: The whole suite, then commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-procgen add Source/DeepSpace/Universe/AirFacts.h Source/DeepSpace/Universe/AirFacts.cpp Source/DeepSpace/Tests/AirFactsTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/air-procgen commit -F - <<'MSG'
feat(universe): AirFacts -- what an air is, derived, and the guarantees that bound it

EAirMix and the pure derivations beside the generator (atmospheres
decision 2): each mix's molecular weight, cross-section, aerosol and
absorber (decision 5); scale height on EquilibriumK; the column, and the
Rayleigh and aerosol depths from it; Jeans retention at an exobase four
times the equilibrium temperature, held above lambda 6, lost below 4, a
smoothstep between; the per-world pressure ceiling where straight down at
450 nm reaches GenGuarantees::MaxNadirTau450 (decision 4); the smooth
ceiling; a giant's disc at the guarantee's depth.

DeepSpace.Universe.Gases: an Earth keeps N2/O2 and loses H2/He; the
smallest hottest temperate world keeps N2/O2 and CO2; a cold super-Earth
keeps H2/He; the ceilings 1.8, 1.17 and 0.9 bar at 1 g; giants' discs
0.11 bar to 22.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 7: Prove the test can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Universe/AirFacts.h 'inline constexpr double ExobaseFactor = 4.0;' 'inline constexpr double ExobaseFactor = 1.0;' DeepSpace.Universe.Gases
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Universe/AirFacts.cpp 'return DrawnBar / std::pow(1.0 + std::pow(DrawnBar / CeilingBar, 4.0), 0.25);' 'return FMath::Min(DrawnBar, CeilingBar);' DeepSpace.Universe.Gases
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh
```

Expected: `KILLED` twice.
- The first makes an Earth keep hydrogen.
- The second turns the smooth ceiling into a clamp, which reaches the ceiling at the draw.

---
## Task 10 (G2): The two draws on `FPlanet`

Ruling 4 and decision 2 put two draws on new labelled streams under the planet's seed: `AirMix` from `Label("air.mix")` and `SurfacePressureBar` from `Label("air.pressure")`.
- The mix is categorical. Its ini weights are each multiplied by the world's retention of that mix before the draw: shaped, never rejected (ADR 0008).
- The pressure is log-normal by kind, bent under its ceiling by the smooth ceiling.
- Giants draw nothing: hydrogen and helium, at their disc.
- Barren and ice worlds draw nothing either: airless.

These are landing track P's files, and **P merged into `main` as `d1cfbc8` while this plan was written**. A tree cut from `main` after that commit already has P's work. Step 1 checks it, and checks that the wear plan's S5 is not mid-edit on `Config/DefaultGame.ini`, the one file this plan shares with the wear plan.

Two more things land here, because neither needs the optics:
- **The spec's comparison with the draws off.** `GenerateWithPlanetCount` gains a defaulted `EAirDraws` parameter, test-only in use: with `EAirDraws::Off` the air block is skipped and every world is airless. `DeepSpace.Universe.Air` generates the 500 nearest systems both ways and holds every other quantity equal. The retune check compares two runs of the new code and cannot see an air block that disturbs another quantity by a fixed amount; the draws-off comparison can.
- **The fixture worlds.** `.FixtureWorlds` reads only the draws, gravity and `AirFacts::NadirTau450`, so it is written here, and the spec's *Fixture worlds* table is filled from its log ("track G's first procgen commit fills this table"). The rules live once, in `Tests/AirFixtureWorlds.h`, which Task 12's swatch uses too.

**Files:**
- Modify: `Source/DeepSpace/Universe/StarSystem.h` (the includes; `FPlanet`, after `SurfaceGravityEarth()`, line ~118)
- Modify: `Source/DeepSpace/Universe/StarSystemGenerator.h` (`EAirDraws` and `FAirDraw` before the struct; `GenerateAir` after `GenerateRelief`, line ~48; `GenerateWithPlanetCount`'s declaration, line ~57)
- Modify: `Source/DeepSpace/Universe/StarSystemGenerator.cpp` (`DrawAir` after `DrawRelief`, line ~131; `GenerateWithPlanetCount`'s signature, line ~210; one block after `Planet.ReliefKm = DrawRelief(...)`, line ~284; `GenerateAir` at the end)
- Modify: `Source/DeepSpace/Universe/GenPriors.h` (a section before `// -- the galaxy --`, line ~109; `DS_GEN_PRIORS`, lines ~133-136; the air's four guarantees at the end of `GenGuarantees`, line ~196; `GenPriorDomain`)
- Modify: `Source/DeepSpace/Universe/AirFacts.h` (the include; the `GenGuarantees` block out)
- Modify: `Source/DeepSpace/Universe/GenPriors.cpp` (`Refusals`, before `return Out;`)
- Modify: `Source/DeepSpace/Universe/ProcGenPriorsConfig.h` (before `UPROPERTY(Config) double SystemsPerSector;`, line ~105)
- Modify: `Config/DefaultGame.ini` (before `; The galaxy: Poisson mean...`, line ~87)
- Create: `Source/DeepSpace/Tests/AirFixtureWorlds.h`
- Test: create `Source/DeepSpace/Tests/AirProcGenTest.cpp` (`DeepSpace.Universe.Air`, `DeepSpace.Atmosphere.FixtureWorlds`)
- Modify: `docs/superpowers/specs/2026-09-27-atmospheres-design.md` (*Fixture worlds*: a table after the rules table)

**Interfaces:**
- Consumes: `AirFacts::*`, `EAirMix` and the air's `GenGuarantees` (Task 9). From P: `FStarSystemGenerator::PlanetSeed(uint64, int32)`, `FPlanet::SurfaceGravityEarth()` and `FPlanet::ReliefKm`. `FGenStream::Categorical`, `FGenStream::LogNormal`. `FGalaxyGenerator::{GenerateStub, StartSystem, FindSystemsWithin, GenerateSystem, MaxSearchRadiusLy}`, `UProcGenPriorsConfig::ToPriors`.
- Produces:

```cpp
EAirMix FPlanet::AirMix = EAirMix::None;
double FPlanet::SurfacePressureBar = 0.0;
enum class EAirDraws : uint8 { On, Off };
struct FAirDraw { EAirMix Mix; double DrawnBar; double CeilingBar; double PressureBar; };
static FAirDraw FStarSystemGenerator::GenerateAir(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors);
static FStarSystem FStarSystemGenerator::GenerateWithPlanetCount(const FStarSystemStub& Stub, const FGenPriors& Priors, int32 PlanetCount,
                                                                 EAirDraws AirDraws = EAirDraws::On);
double FGenPriors::{AirPressureMedianTerrestrialBar, AirPressureMedianOceanBar, AirPressureSigma,
                    AirMixWeightNitrogenOxygen, AirMixWeightCarbonDioxide, AirMixWeightHydrogenHelium};
inline constexpr double GenPriorDomain::MaxAirPressureSigma = 2.5;
namespace GenGuarantees { inline constexpr double ExobaseFactor = 4.0, RetainedAbove = 6.0, LostBelow = 4.0, MaxNadirTau450 = 0.5; }   // now in GenPriors.h
namespace AirFixtureWorlds   // Tests/ only
{
    inline constexpr uint64 UniverseSeed = 20260925;
    inline constexpr int32 SearchSystems = 2000;
    struct FFixture { const TCHAR* Role; bool bFound; FStarSystem System; int32 Index; double DistanceLy; const FPlanet& Planet() const; };
    TArray<FFixture> Find(uint64 Root, const FGenPriors& Priors, int32 MaxSystems);   // R, G, C, N, J in that order
}
```

- [ ] **Step 1: The tree has P, and the wear plan is not mid-edit on the ini.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && git merge -q main && git log --oneline | grep -c "merge: landing-a-procgen" && grep -c "double ReliefKm" Source/DeepSpace/Universe/StarSystem.h
{ [ ! -d /home/matt/Development/deepspace/.worktrees/wear-1-s ] || [ -z "$(git -C /home/matt/Development/deepspace/.worktrees/wear-1-s status --short -- Config/DefaultGame.ini)" ]; } && echo "wear-1-s not editing the ini"
```

Expected: `1`, `1`, then `wear-1-s not editing the ini`. If either count is 0, P is not in this tree: stop, Task 10 waits for P (see *Execution order*). If the last line does not print, the wear plan's S5 holds an uncommitted edit to `Config/DefaultGame.ini` in `.worktrees/wear-1-s`: wait until it commits, then run this step again. Do not edit `Config/DefaultGame.ini` until all three pass. The two edits are non-adjacent hunks, so they merge cleanly in either order once neither is mid-edit.

- [ ] **Step 2: Write the failing test.** Create `Source/DeepSpace/Tests/AirProcGenTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Tests/AirFixtureWorlds.h"
#include "Universe/AirFacts.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/GenPriors.h"
#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/StarSystemGenerator.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAirProcGenTest,
    "DeepSpace.Universe.Air",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The atmospheres spec's fixture roles, found by its rules (AirFixtureWorlds):
 *  its log fills the spec's *Fixture worlds* table. Fails if a role has no
 *  world within AirFixtureWorlds::SearchSystems of home. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAtmosphereFixtureWorldsTest,
    "DeepSpace.Atmosphere.FixtureWorlds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AirProcGenTestLocal
{
    constexpr uint64 Root = 20260925;
    constexpr int32 SystemCount = 3000;

    /** The Count systems nearest home under these priors, nearest first. */
    TArray<FStarSystem> Nearest(const FGenPriors& Priors, int32 Count)
    {
        TArray<FStarSystem> Systems;
        const FGalaxyGenerator Galaxy(Root, Priors);
        const TOptional<FStarSystemStub> Home = Galaxy.GenerateStub(Galaxy.StartSystem());
        if (!Home.IsSet())
        {
            return Systems;
        }
        TArray<FStarSystemStub> Near = Galaxy.FindSystemsWithin(Home->Position, FGalaxyGenerator::MaxSearchRadiusLy * UniverseUnits::CmPerLightYear);
        const FUniversePosition Origin = Home->Position;
        Near.Sort([&Origin](const FStarSystemStub& A, const FStarSystemStub& B) { return Origin.DistanceTo(A.Position) < Origin.DistanceTo(B.Position); });
        for (int32 I = 0; I < FMath::Min(Count, Near.Num()); ++I)
        {
            Systems.Add(Galaxy.GenerateSystem(Near[I]));
        }
        return Systems;
    }

    bool IsTemperate(EPlanetKind Kind)
    {
        return Kind == EPlanetKind::Terrestrial || Kind == EPlanetKind::Ocean;
    }

    bool Names(const TArray<FString>& Refusals, const TCHAR* Line)
    {
        return Refusals.ContainsByPredicate([Line](const FString& Refusal) { return Refusal.Contains(Line); });
    }
}

bool FAirProcGenTest::RunTest(const FString& Parameters)
{
    using namespace AirProcGenTestLocal;
    const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();
    TestTrue(TEXT("the ini's priors are accepted"), GenPriorDomain::Refusals(Priors).IsEmpty());
    const TArray<FStarSystem> Systems = Nearest(Priors, SystemCount);
    if (!TestEqual(TEXT("enough systems near home"), Systems.Num(), SystemCount))
    {
        return false;
    }

    // -- Every world, against the rules ---------------------------------------
    FString FirstBreak;
    int32 Temperate = 0;
    int32 Giants = 0;
    int32 OverCeiling = 0;
    int32 AtCeiling = 0;
    int32 ByMix[4] = {0, 0, 0, 0};
    for (const FStarSystem& System : Systems)
    {
        for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
        {
            const FPlanet& Planet = System.Planets[Index];
            const double G = Planet.SurfaceGravityEarth();
            const auto Break = [&FirstBreak, &Planet](const TCHAR* What)
            {
                if (FirstBreak.IsEmpty())
                {
                    FirstBreak = FString::Printf(TEXT("%s (%s, %.3f bar): %s"), *Planet.Designation, AirFacts::Name(Planet.AirMix), Planet.SurfacePressureBar, What);
                }
            };
            if (Planet.Kind == EPlanetKind::Barren || Planet.Kind == EPlanetKind::Ice)
            {
                if (Planet.AirMix != EAirMix::None || Planet.SurfacePressureBar != 0.0)
                {
                    Break(TEXT("barren and ice worlds are airless"));
                }
                continue;
            }
            if (Planet.Kind == EPlanetKind::GasGiant)
            {
                ++Giants;
                if (Planet.AirMix != EAirMix::HydrogenHelium || Planet.SurfacePressureBar != AirFacts::GiantDiscPressureBar(G))
                {
                    Break(TEXT("a giant is hydrogen and helium, its disc at the guarantee's depth"));
                }
                if (FMath::Abs(AirFacts::NadirTau450(Planet.AirMix, Planet.SurfacePressureBar, G) - GenGuarantees::MaxNadirTau450) > 1.0e-9)
                {
                    Break(TEXT("a giant's disc is where the air above reaches MaxNadirTau450"));
                }
                continue;
            }
            ++Temperate;
            ++ByMix[static_cast<int32>(Planet.AirMix)];
            const double Retained = AirFacts::Retention(Planet.AirMix, Planet.MassEarth, Planet.RadiusEarth, Planet.EquilibriumK);
            if (Planet.AirMix == EAirMix::None || !(Planet.SurfacePressureBar > 0.0))
            {
                Break(TEXT("a temperate world under the ini's priors has air"));
            }
            if (!(Retained > 0.0))
            {
                Break(TEXT("no world holds a mix it cannot retain"));
            }
            if (AirFacts::NadirTau450(Planet.AirMix, Planet.SurfacePressureBar, G) > GenGuarantees::MaxNadirTau450 + 1.0e-9)
            {
                Break(TEXT("every airy world at or under MaxNadirTau450"));
            }
            const FAirDraw Draw = FStarSystemGenerator::GenerateAir(FStarSystemGenerator::PlanetSeed(System.Stub.Seed, Index), Planet, Priors);
            if (Draw.Mix != Planet.AirMix || Draw.PressureBar != Planet.SurfacePressureBar)
            {
                Break(TEXT("GenerateAir is what Generate drew"));
            }
            if (Planet.SurfacePressureBar > Draw.CeilingBar)
            {
                Break(TEXT("never over the ceiling"));
            }
            OverCeiling += Draw.DrawnBar > Draw.CeilingBar ? 1 : 0;
            AtCeiling += Planet.SurfacePressureBar / Draw.CeilingBar > 0.99999 ? 1 : 0;
        }
    }
    AddInfo(FString::Printf(TEXT("%d temperate worlds: %d N2/O2, %d CO2, %d H2/He; %d giants; %d drew over their ceiling, %d sit within 1e-5 of it"),
        Temperate, ByMix[1], ByMix[2], ByMix[3], Giants, OverCeiling, AtCeiling));
    TestTrue(TEXT("every world keeps the rules: ") + FirstBreak, FirstBreak.IsEmpty());
    TestTrue(TEXT("the mixes spread: N2/O2 and CO2 both occur"), ByMix[1] > 0 && ByMix[2] > 0);
    TestTrue(TEXT("the ceiling bends some worlds (else the next check proves nothing)"), OverCeiling > 0);
    TestTrue(TEXT("the smooth ceiling leaves no spike: under a tenth as many at the cap as drew past it"), 10 * AtCeiling < OverCeiling);

    // -- The draws come from the two named streams ---------------------------------
    // Drawn again by hand from "air.mix" and "air.pressure" under the first
    // 50 temperate worlds' seeds: a mix or pressure on any other stream would
    // be a draw that moves when that stream's quantity is retuned, and 50
    // worlds leave no chance of a wrong stream agreeing everywhere.
    int32 StreamsChecked = 0;
    int32 StreamMisses = 0;
    for (int32 S = 0; StreamsChecked < 50 && S < Systems.Num(); ++S)
    {
        for (int32 Index = 0; StreamsChecked < 50 && Index < Systems[S].Planets.Num(); ++Index)
        {
            const FPlanet& Planet = Systems[S].Planets[Index];
            if (!IsTemperate(Planet.Kind))
            {
                continue;
            }
            ++StreamsChecked;
            const uint64 Seed = FStarSystemGenerator::PlanetSeed(Systems[S].Stub.Seed, Index);
            const EAirMix Mixes[] = {EAirMix::NitrogenOxygen, EAirMix::CarbonDioxide, EAirMix::HydrogenHelium};
            const double Weights[] = {Priors.AirMixWeightNitrogenOxygen, Priors.AirMixWeightCarbonDioxide, Priors.AirMixWeightHydrogenHelium};
            double Shaped[3];
            for (int32 M = 0; M < 3; ++M)
            {
                Shaped[M] = Weights[M] * AirFacts::Retention(Mixes[M], Planet.MassEarth, Planet.RadiusEarth, Planet.EquilibriumK);
            }
            FGenStream MixStream(GenSeed::Derive(Seed, GenSeed::Label("air.mix")));
            const EAirMix ByHand = Mixes[MixStream.Categorical(MakeArrayView(Shaped, 3))];
            const double Median = Planet.Kind == EPlanetKind::Ocean ? Priors.AirPressureMedianOceanBar : Priors.AirPressureMedianTerrestrialBar;
            FGenStream PressureStream(GenSeed::Derive(Seed, GenSeed::Label("air.pressure")));
            const double DrawnByHand = PressureStream.LogNormal(Median, Priors.AirPressureSigma);
            const FAirDraw Draw = FStarSystemGenerator::GenerateAir(Seed, Planet, Priors);
            StreamMisses += (Planet.AirMix != ByHand || Draw.DrawnBar != DrawnByHand) ? 1 : 0;
        }
    }
    TestEqual(TEXT("50 temperate worlds checked against their named streams"), StreamsChecked, 50);
    TestEqual(TEXT("every mix is its air.mix stream's and every drawn pressure its air.pressure stream's"), StreamMisses, 0);

    // -- The draws off: the air moves nothing else about any world ----------------
    // The spec's comparison. Each of the 500 nearest systems generated again
    // with no air drawn is the same system in everything but its air: an air
    // block that touched any other quantity, by however fixed an amount,
    // shows here, where two runs of the new code (below) cannot see it.
    bool bOffSame = true;
    bool bOffAirless = true;
    int32 OffWorlds = 0;
    for (int32 S = 0; bOffSame && S < 500; ++S)
    {
        const FStarSystem& On = Systems[S];
        const FStarSystem Off = FStarSystemGenerator::GenerateWithPlanetCount(On.Stub, Priors, On.Planets.Num(), EAirDraws::Off);
        bOffSame &= Off.Planets.Num() == On.Planets.Num() && Off.Star.TemperatureK == On.Star.TemperatureK && Off.Star.MassSolar == On.Star.MassSolar;
        for (int32 P = 0; bOffSame && P < On.Planets.Num(); ++P)
        {
            const FPlanet& A = On.Planets[P];
            const FPlanet& B = Off.Planets[P];
            ++OffWorlds;
            bOffSame &= A.Kind == B.Kind && A.MassEarth == B.MassEarth && A.RadiusEarth == B.RadiusEarth
                && A.SemiMajorAxisAU == B.SemiMajorAxisAU && A.EquilibriumK == B.EquilibriumK && A.PhaseRad == B.PhaseRad
                && A.ReliefKm == B.ReliefKm && A.DayHours == B.DayHours && A.Population == B.Population && A.GivenName == B.GivenName;
            bOffAirless &= B.AirMix == EAirMix::None && B.SurfacePressureBar == 0.0;
        }
    }
    TestTrue(FString::Printf(TEXT("with the draws off, each of %d worlds is the same world but for its air: kind, mass, radius, orbit, phase, relief, day and people"), OffWorlds),
        bOffSame && OffWorlds > 0);
    TestTrue(TEXT("and with the draws off, no world has air"), bOffAirless);

    // -- The two labels' priors move nothing else ----------------------------------
    FGenPriors Retuned = Priors;
    Retuned.AirMixWeightNitrogenOxygen = 0.001;
    Retuned.AirMixWeightCarbonDioxide = 1.0;
    Retuned.AirMixWeightHydrogenHelium = 1.0;
    Retuned.AirPressureMedianTerrestrialBar = 0.01;
    Retuned.AirPressureMedianOceanBar = 5.0;
    Retuned.AirPressureSigma = 2.0;
    const TArray<FStarSystem> Again = Nearest(Retuned, 500);
    bool bSame = Again.Num() == 500;
    bool bAirMoved = false;
    for (int32 S = 0; bSame && S < Again.Num(); ++S)
    {
        bSame &= Again[S].Planets.Num() == Systems[S].Planets.Num();
        for (int32 P = 0; bSame && P < Again[S].Planets.Num(); ++P)
        {
            const FPlanet& A = Systems[S].Planets[P];
            const FPlanet& B = Again[S].Planets[P];
            bSame &= A.Kind == B.Kind && A.MassEarth == B.MassEarth && A.RadiusEarth == B.RadiusEarth
                && A.SemiMajorAxisAU == B.SemiMajorAxisAU && A.EquilibriumK == B.EquilibriumK && A.PhaseRad == B.PhaseRad
                && A.ReliefKm == B.ReliefKm && A.DayHours == B.DayHours && A.Population == B.Population && A.GivenName == B.GivenName;
            bAirMoved |= A.AirMix != B.AirMix || A.SurfacePressureBar != B.SurfacePressureBar;
        }
    }
    TestTrue(TEXT("retuning every air prior moves no kind, mass, radius, orbit, phase, relief, day or people"), bSame);
    TestTrue(TEXT("and does move the air"), bAirMoved);

    // -- The domain names the line ------------------------------------------------------
    FGenPriors Bad = Priors;
    Bad.AirPressureSigma = 0.0;
    TestTrue(TEXT("a zero pressure sigma is refused by name"), Names(GenPriorDomain::Refusals(Bad), TEXT("AirPressureSigma")));
    Bad = Priors;
    Bad.AirPressureSigma = 3.0;
    TestTrue(TEXT("and one past 2.5"), Names(GenPriorDomain::Refusals(Bad), TEXT("AirPressureSigma")));
    Bad = Priors;
    Bad.AirPressureMedianOceanBar = -1.0;
    TestTrue(TEXT("a negative ocean median is refused by name"), Names(GenPriorDomain::Refusals(Bad), TEXT("AirPressureMedianOceanBar")));
    Bad = Priors;
    Bad.AirMixWeightCarbonDioxide = -0.1;
    TestTrue(TEXT("a negative mix weight is refused by name"), Names(GenPriorDomain::Refusals(Bad), TEXT("AirMixWeightCarbonDioxide")));
    Bad = Priors;
    Bad.AirMixWeightNitrogenOxygen = 0.0;
    Bad.AirMixWeightCarbonDioxide = 0.0;
    Bad.AirMixWeightHydrogenHelium = 0.0;
    TestTrue(TEXT("no gas at all is refused"), Names(GenPriorDomain::Refusals(Bad), TEXT("AirMixWeightNitrogenOxygen..AirMixWeightHydrogenHelium")));

    // -- Review focus 5: legal weights that leave a world nothing it can hold ------------
    FGenPriors Hydrogen = Priors;
    Hydrogen.AirMixWeightNitrogenOxygen = 0.0;
    Hydrogen.AirMixWeightCarbonDioxide = 0.0;
    Hydrogen.AirMixWeightHydrogenHelium = 1.0;
    TestTrue(TEXT("hydrogen alone is a legal set of weights"), GenPriorDomain::Refusals(Hydrogen).IsEmpty());
    int32 Left = 0;
    int32 Held = 0;
    FString HydrogenBreak;
    for (const FStarSystem& System : Nearest(Hydrogen, 500))
    {
        for (const FPlanet& Planet : System.Planets)
        {
            if (!IsTemperate(Planet.Kind))
            {
                continue;
            }
            const bool bCanHold = AirFacts::Retention(EAirMix::HydrogenHelium, Planet.MassEarth, Planet.RadiusEarth, Planet.EquilibriumK) > 0.0;
            if (bCanHold && Planet.AirMix == EAirMix::HydrogenHelium && Planet.SurfacePressureBar > 0.0)
            {
                ++Held;
            }
            else if (!bCanHold && Planet.AirMix == EAirMix::None && Planet.SurfacePressureBar == 0.0)
            {
                ++Left;
            }
            else if (HydrogenBreak.IsEmpty())
            {
                HydrogenBreak = Planet.Designation;
            }
        }
    }
    AddInfo(FString::Printf(TEXT("hydrogen only: %d temperate worlds hold it, %d cannot and are airless"), Held, Left));
    TestTrue(TEXT("a world that can hold nothing the weights allow is airless, and nothing is drawn: ") + HydrogenBreak, HydrogenBreak.IsEmpty());
    TestTrue(TEXT("some such worlds exist near home"), Left > 0);
    return true;
}

bool FAtmosphereFixtureWorldsTest::RunTest(const FString& Parameters)
{
    const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();
    for (const AirFixtureWorlds::FFixture& Role : AirFixtureWorlds::Find(AirFixtureWorlds::UniverseSeed, Priors, AirFixtureWorlds::SearchSystems))
    {
        if (!TestTrue(FString::Printf(TEXT("fixture %s is found within %d systems of home"), Role.Role, AirFixtureWorlds::SearchSystems), Role.bFound))
        {
            continue;
        }
        const FPlanet& Planet = Role.Planet();
        const double G = Planet.SurfaceGravityEarth();
        AddInfo(FString::Printf(TEXT("fixture %s: %s | sector (%lld, %lld, %lld) slot %d, orbit index %d | %s | %.4g bar | %.3f g | tau450 %.3f | %.2f M_E | star %.0f K | %.2f ly"),
            Role.Role, *Planet.Designation,
            static_cast<long long>(Role.System.Stub.Id.Sector.X), static_cast<long long>(Role.System.Stub.Id.Sector.Y), static_cast<long long>(Role.System.Stub.Id.Sector.Z),
            Role.System.Stub.Id.Slot, Role.Index, AirFacts::Name(Planet.AirMix), Planet.SurfacePressureBar, G,
            AirFacts::NadirTau450(Planet.AirMix, Planet.SurfacePressureBar, G), Planet.MassEarth,
            Role.System.Star.TemperatureK, Role.DistanceLy));
    }
    return true;
}

#endif
```

  Create `Source/DeepSpace/Tests/AirFixtureWorlds.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Universe/AirFacts.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseUnits.h"

/**
 * The atmospheres spec's fixture worlds (*Fixture worlds*), found by its
 * rules among the systems nearest home: the one statement of those rules,
 * which DeepSpace.Atmosphere.FixtureWorlds prints into the spec's table and
 * Atmosphere.Full.GroundSkySwatch draws. Tests/ only. Each role is the
 * first world, nearest home first and in orbit order, that its rule admits.
 */
namespace AirFixtureWorlds
{
    /** The universe the spec's fixtures are named in (*Fixture worlds*). */
    inline constexpr uint64 UniverseSeed = 20260925;

    /** How far the search goes, in systems nearest home. */
    inline constexpr int32 SearchSystems = 2000;

    struct FFixture
    {
        const TCHAR* Role = TEXT("");
        bool bFound = false;
        FStarSystem System;
        int32 Index = -1;
        double DistanceLy = 0.0;

        const FPlanet& Planet() const
        {
            return System.Planets[Index];
        }
    };

    inline bool IsTemperate(EPlanetKind Kind)
    {
        return Kind == EPlanetKind::Terrestrial || Kind == EPlanetKind::Ocean;
    }

    /** R, G, C, N and J, in that order, among the MaxSystems systems nearest
     *  Root's home under Priors. A role with no world has bFound false. */
    inline TArray<FFixture> Find(uint64 Root, const FGenPriors& Priors, int32 MaxSystems)
    {
        TArray<FFixture> Roles;
        for (const TCHAR* Role : {TEXT("R"), TEXT("G"), TEXT("C"), TEXT("N"), TEXT("J")})
        {
            FFixture& Added = Roles.AddDefaulted_GetRef();
            Added.Role = Role;
        }
        const FGalaxyGenerator Galaxy(Root, Priors);
        const TOptional<FStarSystemStub> Home = Galaxy.GenerateStub(Galaxy.StartSystem());
        if (!Home.IsSet())
        {
            return Roles;
        }
        TArray<FStarSystemStub> Near = Galaxy.FindSystemsWithin(Home->Position, FGalaxyGenerator::MaxSearchRadiusLy * UniverseUnits::CmPerLightYear);
        const FUniversePosition Origin = Home->Position;
        Near.Sort([&Origin](const FStarSystemStub& A, const FStarSystemStub& B) { return Origin.DistanceTo(A.Position) < Origin.DistanceTo(B.Position); });
        Near.SetNum(FMath::Min(Near.Num(), MaxSystems));

        const auto Claim = [&Origin](FFixture& Role, const FStarSystem& System, int32 Index)
        {
            if (!Role.bFound)
            {
                Role.bFound = true;
                Role.System = System;
                Role.Index = Index;
                Role.DistanceLy = Origin.DistanceTo(System.Stub.Position) / UniverseUnits::CmPerLightYear;
            }
        };
        for (int32 S = 0; S < Near.Num(); ++S)
        {
            const FStarSystem System = Galaxy.GenerateSystem(Near[S]);
            for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
            {
                const FPlanet& Planet = System.Planets[Index];
                const bool bNitrogen = IsTemperate(Planet.Kind) && Planet.AirMix == EAirMix::NitrogenOxygen;
                // R: home's N2/O2 world, else the nearest N2/O2 world under an M dwarf.
                if (bNitrogen && (S == 0 || System.Star.Class == EStarClass::M))
                {
                    Claim(Roles[0], System, Index);
                }
                // G: the nearest N2/O2 world under a 5,000-6,000 K star.
                if (bNitrogen && System.Star.TemperatureK >= 5000.0 && System.Star.TemperatureK <= 6000.0)
                {
                    Claim(Roles[1], System, Index);
                }
                // C: the nearest CO2 world.
                if (IsTemperate(Planet.Kind) && Planet.AirMix == EAirMix::CarbonDioxide)
                {
                    Claim(Roles[2], System, Index);
                }
                // N: home's first barren world.
                if (S == 0 && Planet.Kind == EPlanetKind::Barren)
                {
                    Claim(Roles[3], System, Index);
                }
                // J: the nearest giant.
                if (Planet.Kind == EPlanetKind::GasGiant)
                {
                    Claim(Roles[4], System, Index);
                }
            }
        }
        return Roles;
    }
}
```

- [ ] **Step 3: Run it; expect a compile failure.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh
```

Expected: FAIL, `no member named 'AirMix' in 'FPlanet'`, `no member named 'GenerateAir'` and `use of undeclared identifier 'EAirDraws'`.

- [ ] **Step 4: `FPlanet`.** In `StarSystem.h`, after `#include "Universe/GenSeed.h"`, add `#include "Universe/AirFacts.h"`. After the lines

```cpp
        return RadiusEarth > 0.0 ? MassEarth / (RadiusEarth * RadiusEarth) : 0.0;
    }
```

  add:

```cpp

    /** Which air this world holds (atmospheres decision 2): none for barren
     *  and ice worlds; drawn for terrestrial and ocean worlds, from weights
     *  shaped by what the world can hold (AirFacts::Retention); hydrogen and
     *  helium for every giant. */
    EAirMix AirMix = EAirMix::None;

    /** Bar at the surface: log-normal by kind, bent under the world's
     *  ceiling (AirFacts::PressureCeilingBar). A giant's is the level where
     *  the air above reaches GenGuarantees::MaxNadirTau450, its disc. 0
     *  without air. */
    double SurfacePressureBar = 0.0;
```

- [ ] **Step 5: The priors, the list, the domain.** In `GenPriors.h`, immediately before `    // -- the galaxy -------------------------------------------------------------`, add:

```cpp
    // -- the air (atmospheres decision 2) ----------------------------------------

    /** A temperate world's surface pressure, bar: log-normal, because a
     *  pressure is a product of many factors -- outgassing, loss,
     *  sequestration. Bounded above in code, not here: by the world's
     *  retention of its gas and the nadir-haze guarantee
     *  (AirFacts::PressureCeilingBar), bent under that ceiling rather than
     *  clipped, so no pile of worlds stacks at the cap. */
    double AirPressureMedianTerrestrialBar = 0.8;
    double AirPressureMedianOceanBar = 1.0;
    double AirPressureSigma = 0.9;

    /** Which gas a temperate world holds, before retention: relative
     *  weights, each multiplied by how well the world holds that gas, so a
     *  world that cannot keep hydrogen never draws it and no draw is thrown
     *  away (ADR 0008). They set the colour of much of the universe
     *  (sign-off item 19): the corpus's sky_zenith_rgb is what they are
     *  judged by. */
    double AirMixWeightNitrogenOxygen = 0.55;
    double AirMixWeightCarbonDioxide = 0.40;
    double AirMixWeightHydrogenHelium = 0.05;

```

  In `DS_GEN_PRIORS` replace

```cpp
    X(ReliefBetaA) X(ReliefBetaB) X(ReliefTerrestrialBetaA) X(ReliefTerrestrialBetaB) \
    X(SystemsPerSector)
```

  with

```cpp
    X(ReliefBetaA) X(ReliefBetaB) X(ReliefTerrestrialBetaA) X(ReliefTerrestrialBetaB) \
    X(AirPressureMedianTerrestrialBar) X(AirPressureMedianOceanBar) X(AirPressureSigma) \
    X(AirMixWeightNitrogenOxygen) X(AirMixWeightCarbonDioxide) X(AirMixWeightHydrogenHelium) \
    X(SystemsPerSector)
```

  In `namespace GenGuarantees`, after `    inline constexpr double MaxReliefRadiusFraction = 0.005;`, add:

```cpp

    /** The top of an air is far hotter than its surface -- Earth's
     *  thermosphere runs near 1,000 K over a 255 K equilibrium -- and it is
     *  the top that loses gas: the exobase is taken at this many times
     *  EquilibriumK (atmospheres decision 2). It decides which worlds may
     *  hold hydrogen. */
    inline constexpr double ExobaseFactor = 4.0;

    /** Escape speed over the molecules' root-mean-square speed at the
     *  exobase: at or above RetainedAbove a gas is held over a star's age;
     *  at or below LostBelow it is gone; a smoothstep between. */
    inline constexpr double RetainedAbove = 6.0;
    inline constexpr double LostBelow = 4.0;

    /** No air's total extinction straight down at 450 nm (Rayleigh plus
     *  aerosol, not absorption) exceeds this (atmospheres ruling 8, decision
     *  4): every airy world's surface stays legible from orbit. Bounds the
     *  fact, as a per-world pressure ceiling, never the rendering. */
    inline constexpr double MaxNadirTau450 = 0.5;
```

  In `AirFacts.h`, after `#include "CoreMinimal.h"`, add `#include "Universe/GenPriors.h"`. Then delete the air's own `GenGuarantees` block: from the `/**` line above ` * The air's guarantees (decisions 2 and 4): numbers no ini line may move,` through the `}` that closes that `namespace GenGuarantees`. Put in its place:

```cpp
/* The air's guarantees -- GenGuarantees::ExobaseFactor, RetainedAbove,
 * LostBelow and MaxNadirTau450 -- are GenPriors.h's, beside every other
 * number no ini line may move. */
```

  `GenPriors.h` includes only `CoreMinimal.h`, so the include makes no cycle, and `AirFacts.cpp` and `AirFactsTest.cpp` read the same names as before.

  In `namespace GenPriorDomain`, after `    inline constexpr double MaxInnermostMedianFactor = 100.0;`, add:

```cpp

    /** Past this a world's pressure spans a factor of twelve either way at
     *  one sigma, and the ceiling is doing all the work the prior should. */
    inline constexpr double MaxAirPressureSigma = 2.5;
```

  In `GenPriors.cpp`, `GenPriorDomain::Refusals`, replace

```cpp
    Within(Out, TEXT("ReliefTerrestrialBetaB"), P.ReliefTerrestrialBetaB, GenPriorDomain::MinBetaShape, false, Unbounded, Share);
    return Out;
```

  with

```cpp
    Within(Out, TEXT("ReliefTerrestrialBetaB"), P.ReliefTerrestrialBetaB, GenPriorDomain::MinBetaShape, false, Unbounded, Share);

    Within(Out, TEXT("AirPressureMedianTerrestrialBar"), P.AirPressureMedianTerrestrialBar, 0.0, true, Unbounded, Median);
    Within(Out, TEXT("AirPressureMedianOceanBar"), P.AirPressureMedianOceanBar, 0.0, true, Unbounded, Median);
    Within(Out, TEXT("AirPressureSigma"), P.AirPressureSigma, 0.0, true, GenPriorDomain::MaxAirPressureSigma,
        TEXT("a log-normal sigma for pressure, above zero and no more than 2.5"));
    const TCHAR* const Gas = TEXT("a mix weight is how often that gas is chosen before retention, relative to the others");
    Within(Out, TEXT("AirMixWeightNitrogenOxygen"), P.AirMixWeightNitrogenOxygen, 0.0, false, Unbounded, Gas);
    Within(Out, TEXT("AirMixWeightCarbonDioxide"), P.AirMixWeightCarbonDioxide, 0.0, false, Unbounded, Gas);
    Within(Out, TEXT("AirMixWeightHydrogenHelium"), P.AirMixWeightHydrogenHelium, 0.0, false, Unbounded, Gas);
    const double Gases = P.AirMixWeightNitrogenOxygen + P.AirMixWeightCarbonDioxide + P.AirMixWeightHydrogenHelium;
    const bool bGasesValid = std::isfinite(P.AirMixWeightNitrogenOxygen) && P.AirMixWeightNitrogenOxygen >= 0.0
        && std::isfinite(P.AirMixWeightCarbonDioxide) && P.AirMixWeightCarbonDioxide >= 0.0
        && std::isfinite(P.AirMixWeightHydrogenHelium) && P.AirMixWeightHydrogenHelium >= 0.0;
    if (bGasesValid && !(std::isfinite(Gases) && Gases > 0.0))
    {
        Out.Add(FString::Printf(TEXT("AirMixWeightNitrogenOxygen..AirMixWeightHydrogenHelium total %g: some gas must be possible, and the sum finite"), Gases));
    }
    return Out;
```

- [ ] **Step 6: The ini mirror.** In `ProcGenPriorsConfig.h`, before `    UPROPERTY(Config) double SystemsPerSector;`, add:

```cpp
    UPROPERTY(Config) double AirPressureMedianTerrestrialBar;
    UPROPERTY(Config) double AirPressureMedianOceanBar;
    UPROPERTY(Config) double AirPressureSigma;
    UPROPERTY(Config) double AirMixWeightNitrogenOxygen;
    UPROPERTY(Config) double AirMixWeightCarbonDioxide;
    UPROPERTY(Config) double AirMixWeightHydrogenHelium;

```

  In `Config/DefaultGame.ini`, before `; The galaxy: Poisson mean of systems per 2^62 cm (~4.9 ly) sector.`, add:

```ini
; The air (atmospheres decision 2): a temperate world's surface pressure is
; log-normal (median by kind, natural-log sigma), bent under a ceiling that
; is code, not a line here (the world's retention of its gas and
; MaxNadirTau450). Its gas is drawn from these weights times how well the
; world holds each: a world that cannot keep hydrogen never draws it.
AirPressureMedianTerrestrialBar=0.8
AirPressureMedianOceanBar=1.0
AirPressureSigma=0.9
AirMixWeightNitrogenOxygen=0.55
AirMixWeightCarbonDioxide=0.40
AirMixWeightHydrogenHelium=0.05

```

- [ ] **Step 7: The draws.** In `StarSystemGenerator.h`, immediately before the `/**` comment that opens `struct DEEPSPACE_API FStarSystemGenerator`, add:

```cpp
/** One world's air as the generator draws it (atmospheres decision 2): the
 *  mix, the log-normal draw before its ceiling, the ceiling, and the
 *  pressure the world has. Public so the ceiling's shape can be tested
 *  against the draws it bent. */
struct FAirDraw
{
    EAirMix Mix = EAirMix::None;
    double DrawnBar = 0.0;
    double CeilingBar = 0.0;
    double PressureBar = 0.0;
};

```

  Between that `FAirDraw`'s closing `};` and the `/**` comment of `FStarSystemGenerator`, add:

```cpp
/** Whether GenerateWithPlanetCount draws the air (atmospheres spec, *Tests*:
 *  the labels "compared with the draws off"). Off only in a test: every
 *  world airless and nothing else different. */
enum class EAirDraws : uint8
{
    On,
    Off,
};

```

  Replace the declaration

```cpp
    static FStarSystem GenerateWithPlanetCount(const FStarSystemStub& Stub, const FGenPriors& Priors, int32 PlanetCount);
```

  with

```cpp
    static FStarSystem GenerateWithPlanetCount(const FStarSystemStub& Stub, const FGenPriors& Priors, int32 PlanetCount,
        EAirDraws AirDraws = EAirDraws::On);
```

  After the declaration `    static double GenerateRelief(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors);` add:

```cpp

    /** A world's air as Generate draws it for the planet whose seed this
     *  is, from Planet's Kind, MassEarth, RadiusEarth and EquilibriumK. */
    static FAirDraw GenerateAir(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors);
```

  In `StarSystemGenerator.cpp`, in the anonymous namespace, replace

```cpp
        return Ceiling * Share;
    }
```

  with

```cpp
        return Ceiling * Share;
    }

    /**
     * A world's air, from its own two streams (atmospheres decision 2).
     * Giants draw nothing: hydrogen and helium at their disc. Barren and ice
     * worlds draw nothing: airless. A temperate world's mix is categorical
     * over the ini's weights, each multiplied by how well the world holds
     * that gas, so what it cannot hold is never drawn and nothing drawn is
     * thrown away (ADR 0008); if the weights leave it nothing it can hold,
     * it is airless and draws nothing. Its pressure is log-normal by kind --
     * a product of many factors -- bent under the world's ceiling by the
     * smooth ceiling, so the corpus shows no pile at the cap.
     */
    FAirDraw DrawAir(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors)
    {
        FAirDraw Air;
        const double Gravity = Planet.SurfaceGravityEarth();
        if (Planet.Kind == EPlanetKind::GasGiant)
        {
            Air.Mix = EAirMix::HydrogenHelium;
            Air.CeilingBar = AirFacts::GiantDiscPressureBar(Gravity);
            Air.DrawnBar = Air.CeilingBar;
            Air.PressureBar = Air.CeilingBar;
            return Air;
        }
        if (Planet.Kind != EPlanetKind::Terrestrial && Planet.Kind != EPlanetKind::Ocean)
        {
            return Air;
        }
        const EAirMix Mixes[AirFacts::NumMixes] = {EAirMix::NitrogenOxygen, EAirMix::CarbonDioxide, EAirMix::HydrogenHelium};
        const double Weights[AirFacts::NumMixes] = {Priors.AirMixWeightNitrogenOxygen, Priors.AirMixWeightCarbonDioxide, Priors.AirMixWeightHydrogenHelium};
        double Retained[AirFacts::NumMixes];
        double Shaped[AirFacts::NumMixes];
        double Total = 0.0;
        for (int32 Index = 0; Index < AirFacts::NumMixes; ++Index)
        {
            Retained[Index] = AirFacts::Retention(Mixes[Index], Planet.MassEarth, Planet.RadiusEarth, Planet.EquilibriumK);
            Shaped[Index] = Weights[Index] * Retained[Index];
            Total += Shaped[Index];
        }
        if (!(Total > 0.0))
        {
            return Air;
        }
        FGenStream MixStream(GenSeed::Derive(PlanetSeed, GenSeed::Label("air.mix")));
        const int32 Chosen = MixStream.Categorical(MakeArrayView(Shaped, AirFacts::NumMixes));
        Air.Mix = Mixes[Chosen];
        const double Median = Planet.Kind == EPlanetKind::Ocean ? Priors.AirPressureMedianOceanBar : Priors.AirPressureMedianTerrestrialBar;
        FGenStream PressureStream(GenSeed::Derive(PlanetSeed, GenSeed::Label("air.pressure")));
        Air.DrawnBar = PressureStream.LogNormal(Median, Priors.AirPressureSigma);
        Air.CeilingBar = AirFacts::PressureCeilingBar(Air.Mix, Gravity, Retained[Chosen]);
        Air.PressureBar = AirFacts::SmoothCeiling(Air.DrawnBar, Air.CeilingBar);
        return Air;
    }
```

  Replace the definition's first line

```cpp
FStarSystem FStarSystemGenerator::GenerateWithPlanetCount(const FStarSystemStub& Stub, const FGenPriors& Priors, int32 PlanetCount)
```

  with

```cpp
FStarSystem FStarSystemGenerator::GenerateWithPlanetCount(const FStarSystemStub& Stub, const FGenPriors& Priors, int32 PlanetCount, EAirDraws AirDraws)
```

  In `GenerateWithPlanetCount`, after `        Planet.ReliefKm = DrawRelief(PlanetSeed, Planet, Priors);` add:

```cpp

        // Its own two streams: a world's air moves nothing else about it,
        // which the draws-off comparison holds.
        if (AirDraws == EAirDraws::On)
        {
            const FAirDraw Air = DrawAir(PlanetSeed, Planet, Priors);
            Planet.AirMix = Air.Mix;
            Planet.SurfacePressureBar = Air.PressureBar;
        }
```

  The two existing callers (`Generate`, and `StarSystemGenerationTest.cpp`'s count test) pass three arguments and keep the draws on.

  At the end of the file add:

```cpp

FAirDraw FStarSystemGenerator::GenerateAir(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors)
{
    return DrawAir(PlanetSeed, Planet, Priors);
}
```

- [ ] **Step 8: Build and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh && ./test.sh DeepSpace.Universe.Air && ./test.sh DeepSpace.Universe && ./test.sh DeepSpace.Atmosphere.FixtureWorlds; grep -h "temperate worlds:" Saved/Logs/DeepSpace.log | tail -1; grep -h "fixture [RGCNJ]:" Saved/Logs/DeepSpace.log | tail -5
cd /home/matt/Development/deepspace/.worktrees/air-procgen && for T in DeepSpace.Universe.Air DeepSpace.Atmosphere.FixtureWorlds DeepSpace.Sky.Colour; do echo "$T"; time ./test.sh "$T"; done
```

Expected: `DeepSpace.Universe.Air` passes. Every `DeepSpace.Universe.*` passes, including:
- `.Gases`, now reading the guarantees from `GenPriors.h`;
- `.Priors`, which walks `DS_GEN_PRIORS`, so the six new lines are mirrored and present;
- `.Seed` and `.Stream`, untouched;
- `.SystemGeneration`, whose count test calls `GenerateWithPlanetCount` with three arguments;
- `.Relief` and `.Corpus`, whose columns are unchanged until Task 11.

`.FixtureWorlds` passes with five `fixture` lines. The info lines give the mix counts, the worlds over their ceiling, and those at it; the times go into the commit (*Conventions*).

- [ ] **Step 9: Fill the spec's fixture table.** The spec's rules table stands. In `docs/superpowers/specs/2026-09-27-atmospheres-design.md`, after the rules table under *Fixture worlds* and before the paragraph beginning `To look (for R at orbit index n)`, add this, then one row per `fixture <Role>:` line of Step 8's log, in the order R, G, C, N, J, fields in the log's order:

```markdown
**The fixtures as drawn** (universe seed 20260925, priors as committed at this
table's commit; found by `DeepSpace.Atmosphere.FixtureWorlds`, whose log
reprints this table -- a change that moves a world shows there first):

| Role | World | Sector, slot, orbit index | Mix | Pressure (bar) | Gravity (g) | tau450 | Mass (M_E) | Star (K) | Distance (ly) |
|---|---|---|---|---|---|---|---|---|---|
```

  **E** is R (the spec: "R, from its drive floor's approach"). Its row says `E | as R`, after J's.

- [ ] **Step 10: The whole suite, then commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-procgen add Source/DeepSpace/Universe/StarSystem.h Source/DeepSpace/Universe/StarSystemGenerator.h Source/DeepSpace/Universe/StarSystemGenerator.cpp Source/DeepSpace/Universe/GenPriors.h Source/DeepSpace/Universe/GenPriors.cpp Source/DeepSpace/Universe/AirFacts.h Source/DeepSpace/Universe/ProcGenPriorsConfig.h Config/DefaultGame.ini Source/DeepSpace/Tests/AirFixtureWorlds.h Source/DeepSpace/Tests/AirProcGenTest.cpp docs/superpowers/specs/2026-09-27-atmospheres-design.md
git -C /home/matt/Development/deepspace/.worktrees/air-procgen commit -F - <<'MSG'
feat(universe): every world draws its air -- a mix shaped by what it can hold, a pressure under its ceiling

Two draws on their own streams under the planet's seed (atmospheres ruling
4, decision 2): FPlanet::AirMix from "air.mix", categorical over the ini's
weights each multiplied by the world's Jeans retention of that gas -- shaped,
never rejected; FPlanet::SurfacePressureBar from "air.pressure", log-normal
by kind, bent under the world's ceiling by the smooth ceiling. Giants are
hydrogen and helium at the level where the air above reaches
MaxNadirTau450; barren and ice worlds are airless. Six priors in the ini,
domain-checked; the guarantees are code, and move from AirFacts.h into
GenPriors.h's GenGuarantees beside the others.

DeepSpace.Universe.Air: the rules over 3,000 systems, no spike at the
ceiling, the air moving nothing else against the same worlds with the draws
off (GenerateWithPlanetCount's EAirDraws, test-only) and its priors moving
nothing else when retuned, the domain naming its lines, and review focus
5 -- weights that leave a world nothing it can hold make it airless, never
a zero-weight draw. DeepSpace.Atmosphere.FixtureWorlds finds the spec's
fixture roles by its rules (Tests/AirFixtureWorlds.h); the spec's table is
filled from its log.

Measured: DeepSpace.Universe.Air costs N s over start-up.
Measured: DeepSpace.Atmosphere.FixtureWorlds costs N s over start-up.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 11: Prove the tests can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Universe/StarSystemGenerator.cpp 'Air.PressureBar = AirFacts::SmoothCeiling(Air.DrawnBar, Air.CeilingBar);' 'Air.PressureBar = FMath::Min(Air.DrawnBar, Air.CeilingBar);' DeepSpace.Universe.Air
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Universe/StarSystemGenerator.cpp 'Shaped[Index] = Weights[Index] * Retained[Index];' 'Shaped[Index] = Weights[Index];' DeepSpace.Universe.Air
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Universe/StarSystemGenerator.cpp 'FGenStream MixStream(GenSeed::Derive(PlanetSeed, GenSeed::Label("air.mix")));' 'FGenStream MixStream(GenSeed::Derive(PlanetSeed, GenSeed::Label("relief")));' DeepSpace.Universe.Air
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Universe/StarSystemGenerator.cpp '        if (!(Total > 0.0))
        {
            return Air;
        }' '        if (!(Total > 0.0))
        {
            Air.Mix = EAirMix::NitrogenOxygen;
            return Air;
        }' DeepSpace.Universe.Air
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Universe/StarSystemGenerator.cpp '            Planet.SurfacePressureBar = Air.PressureBar;' '            Planet.SurfacePressureBar = Air.PressureBar;
            Planet.ReliefKm += 1.0e-9;' DeepSpace.Universe.Air
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Tests/AirFixtureWorlds.h 'if (IsTemperate(Planet.Kind) && Planet.AirMix == EAirMix::CarbonDioxide)' 'if (IsTemperate(Planet.Kind) && Planet.AirMix == EAirMix::None)' DeepSpace.Atmosphere.FixtureWorlds
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh
```

Expected: `KILLED` six times:
1. A clamp piles worlds at the cap.
2. Unshaped weights draw hydrogen that a world cannot hold.
3. A mix drawn from the relief's stream no longer matches the one drawn by hand from `air.mix`.
4. An airless-by-weights world is given a gas.
5. The air block nudges every world's relief by the same hair. Two runs of the new code agree on it, however the priors are retuned, so only the draws-off comparison sees it: the disturbance the retune check alone would miss.
6. Role C looks for a temperate world with no air, and under the ini's priors every temperate world has one, so C is not found.

---
## Task 11 (G3): The corpus reads the air's facts

The corpus gains four columns, procgen's facts: `air_mix`, `surface_pressure_bar`, `scale_height_km` and `nadir_tau_450`. They go on all three sides: the contract, `DeepSpace.Universe.Corpus`'s writer and `procgen_corpus.py`. None needs the optics, so this task follows Task 10 directly. The noon sky of each world, which does need them, is Task 13's, in a file of its own written outside the default suite.

**Files:**
- Modify: `Tools/procgen_corpus_contract.json` (`columns`)
- Modify: `Tools/procgen_corpus_sample.tsv` (whole file)
- Modify: `Tools/procgen_corpus.py` (`TEXT_COLUMNS`, `PLANET_COLUMNS`, a new `AIR_MIXES`, `report`)
- Modify: `Tools/test_procgen_corpus.py` (two tests before `def main():`)
- Modify: `Source/DeepSpace/Tests/ProcGenCorpusTest.cpp`:
  - `WrittenColumns`, line ~60;
  - `PlanetColumns`, lines ~140-153;
  - `ExpectedRow`, lines ~160-193;
  - `NoPlanetColumns`, line ~209.

**Interfaces:**
- Consumes: `FPlanet::{AirMix, SurfacePressureBar, SurfaceGravityEarth()}` (Task 10); `AirFacts::{Name, ScaleHeightKm, NadirTau450}` (Task 9).
- Produces: the corpus columns `air_mix`, `surface_pressure_bar`, `scale_height_km` and `nadir_tau_450`, and the report's *Air, by mix* block.

- [ ] **Step 1: Write the failing Python tests.** Append to `Tools/test_procgen_corpus.py`, before `def main():`:

```python
def test_report_gives_air_by_mix():
    text = C.report(rows())
    assert "Air, by mix (worlds, median surface pressure, median nadir tau at 450 nm)" in text, text
    # Alpha III n2/o2 1 bar 0.277; Gamma I co2 0.8 bar 0.34; Gamma II h2/he 2.4 bar 0.5.
    assert "  nitrogen-oxygen  n=1      median   1.000 bar  tau450 0.277" in text, text
    assert "  carbon-dioxide   n=1      median   0.800 bar  tau450 0.340" in text, text
    assert "  hydrogen-helium  n=1      median   2.400 bar  tau450 0.500" in text, text


def test_air_is_typed():
    alpha3 = [r for r in rows() if r["designation"] == "Alpha III"][0]
    assert alpha3["air_mix"] == "nitrogen-oxygen"
    assert near(alpha3["surface_pressure_bar"], 1.0) and near(alpha3["scale_height_km"], 8.78)
    alpha1 = [r for r in rows() if r["designation"] == "Alpha I"][0]
    assert alpha1["air_mix"] == "none" and near(alpha1["surface_pressure_bar"], 0.0)
    giant = [r for r in rows() if r["designation"] == "Gamma II"][0]
    assert giant["air_mix"] == "hydrogen-helium" and near(giant["nadir_tau_450"], 0.5)
    beta = [r for r in rows() if r["system"] == "Beta"][0]
    assert beta["air_mix"] is None and beta["surface_pressure_bar"] is None
```

- [ ] **Step 2: Run; expect FAIL.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && python3 Tools/test_procgen_corpus.py
```

Expected: FAIL, `KeyError: 'air_mix'` or the report assertion.

- [ ] **Step 3: The contract and the sample.** In `procgen_corpus_contract.json` replace

```json
    "semi_major_axis_au", "mass_earth", "radius_earth", "equilibrium_k", "population", "surface_gravity_g", "relief_km"
```

  with

```json
    "semi_major_axis_au", "mass_earth", "radius_earth", "equilibrium_k", "population", "surface_gravity_g", "relief_km",
    "air_mix", "surface_pressure_bar", "scale_height_km", "nadir_tau_450"
```

  Replace `Tools/procgen_corpus_sample.tsv` with the lines below, which are tab-separated. Beta's row ends in fourteen tabs after `-1`, for fourteen empty planet cells.

```text
system	sector_x	sector_y	sector_z	slot	distance_ly	star_class	star_mass_solar	star_luminosity_solar	star_temperature_k	habitable_inner_au	habitable_outer_au	frost_line_au	planet_count	planet	designation	given_name	kind	semi_major_axis_au	mass_earth	radius_earth	equilibrium_k	population	surface_gravity_g	relief_km	air_mix	surface_pressure_bar	scale_height_km	nadir_tau_450
Alpha	0	0	0	0	0	M	0.2	0.01	3100	0.095	0.137	0.27	3	0	Alpha I		barren	0.01	0.5	0.8	880	0	0.78125	7.5	none	0	0	0
Alpha	0	0	0	0	0	M	0.2	0.01	3100	0.095	0.137	0.27	3	1	Alpha II		barren	0.02	1	1	620	0	1	6	none	0	0	0
Alpha	0	0	0	0	0	M	0.2	0.01	3100	0.095	0.137	0.27	3	2	Alpha III	Hollin	terrestrial	0.04	1	1	300	1000	1	3.8	nitrogen-oxygen	1	8.78	0.277
Beta	1	0	0	0	5.5	K	0.7	0.2	4400	0.42	0.61	1.2	0	-1														
Gamma	0	1	0	1	7.25	G	1	1	5800	0.95	1.37	2.7	2	0	Gamma I		ocean	1	1	1	280	0	1	0	carbon-dioxide	0.8	5.4	0.34
Gamma	0	1	0	1	7.25	G	1	1	5800	0.95	1.37	2.7	2	1	Gamma II		gas giant	4	100	11	140	0	0.826446	0	hydrogen-helium	2.4	25	0.5
Delta	-1	0	0	0	9	M	0.3	0.02	3300	0.13	0.19	0.38	1	0	Delta I		barren	0.05	2	1.2	310	0	1.38889	4.2	none	0	0	0
```

- [ ] **Step 4: The reader.** In `procgen_corpus.py` replace

```python
TEXT_COLUMNS = {"system", "star_class", "designation", "given_name", "kind"}
PLANET_COLUMNS = ("designation", "given_name", "kind", "semi_major_axis_au", "mass_earth",
                  "radius_earth", "equilibrium_k", "population", "surface_gravity_g", "relief_km")
```

  with

```python
TEXT_COLUMNS = {"system", "star_class", "designation", "given_name", "kind", "air_mix"}
PLANET_COLUMNS = ("designation", "given_name", "kind", "semi_major_axis_au", "mass_earth",
                  "radius_earth", "equilibrium_k", "population", "surface_gravity_g", "relief_km",
                  "air_mix", "surface_pressure_bar", "scale_height_km", "nadir_tau_450")
AIR_MIXES = ("nitrogen-oxygen", "carbon-dioxide", "hydrogen-helium")
```

  In `report()`, immediately before `    out += places_or_rolls(found)`, add:

```python
    airy = [p for p in planets if p["air_mix"] not in (None, "none")]
    out.append("Air, by mix (worlds, median surface pressure, median nadir tau at 450 nm)")
    for mix in AIR_MIXES:
        worlds = [p for p in airy if p["air_mix"] == mix]
        if worlds:
            pressures = sorted(p["surface_pressure_bar"] for p in worlds)
            taus = sorted(p["nadir_tau_450"] for p in worlds)
            out.append("  %-16s n=%-6d median %7.3f bar  tau450 %5.3f" % (
                mix, len(worlds), pressures[len(pressures) // 2], taus[len(taus) // 2]))
        else:
            out.append("  %-16s none" % mix)
    out.append("")

```

- [ ] **Step 5: Run the Python; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && python3 Tools/test_procgen_corpus.py
```

Expected: every test `ok`, `0 failed`.

- [ ] **Step 6: The C++ writer.** In `ProcGenCorpusTest.cpp`:
  - Immediately before `#include "Universe/GalaxyGenerator.h"`, add `#include "Universe/AirFacts.h"` (the includes stay sorted).
  - `WrittenColumns`: replace `TEXT("population"), TEXT("surface_gravity_g"), TEXT("relief_km")};` with

```cpp
TEXT("population"), TEXT("surface_gravity_g"), TEXT("relief_km"),
        TEXT("air_mix"), TEXT("surface_pressure_bar"), TEXT("scale_height_km"), TEXT("nadir_tau_450")};
```

  - `PlanetColumns`: replace

```cpp
            Num(Planet.ReliefKm, NonFinite)}, TEXT("\t"));
    }
```

    with

```cpp
            Num(Planet.ReliefKm, NonFinite),
            AirFacts::Name(Planet.AirMix),
            Num(Planet.SurfacePressureBar, NonFinite),
            Num(AirFacts::ScaleHeightKm(Planet.AirMix, Planet.EquilibriumK, Planet.SurfaceGravityEarth()), NonFinite),
            Num(AirFacts::NadirTau450(Planet.AirMix, Planet.SurfacePressureBar, Planet.SurfaceGravityEarth()), NonFinite)}, TEXT("\t"));
    }
```

  - `ExpectedRow`: replace `            {TEXT("relief_km"), bAny ? G(Planet.ReliefKm) : FString()}};` with

```cpp
            {TEXT("relief_km"), bAny ? G(Planet.ReliefKm) : FString()},
            {TEXT("air_mix"), bAny ? FString(AirFacts::Name(Planet.AirMix)) : FString()},
            {TEXT("surface_pressure_bar"), bAny ? G(Planet.SurfacePressureBar) : FString()},
            {TEXT("scale_height_km"), bAny ? G(AirFacts::ScaleHeightKm(Planet.AirMix, Planet.EquilibriumK, Planet.SurfaceGravityEarth())) : FString()},
            {TEXT("nadir_tau_450"), bAny ? G(AirFacts::NadirTau450(Planet.AirMix, Planet.SurfacePressureBar, Planet.SurfaceGravityEarth())) : FString()}};
```

  - `NoPlanetColumns`: replace `return TEXT("-1\t\t\t\t\t\t\t\t\t\t");` with `return TEXT("-1\t\t\t\t\t\t\t\t\t\t\t\t\t\t");` (fourteen tabs, one per empty planet cell).

- [ ] **Step 7: Run the corpus and read it.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh && ./test.sh DeepSpace.Universe.Corpus && python3 Tools/procgen_corpus.py | sed -n '/^Air, by mix/,/^$/p'
cd /home/matt/Development/deepspace/.worktrees/air-procgen && time ./test.sh DeepSpace.Universe.Corpus && time ./test.sh DeepSpace.Sky.Colour
```

Expected: `passed: 1`, then the *Air, by mix* block for the 10,000 systems: counts, median pressures and median nadir depths by mix. The four columns are arithmetic on each world, so the corpus test costs about what it did; the times go into the commit. The block goes to the developer with Task 10's fixture table.

- [ ] **Step 8: The whole suite, then commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-procgen add Tools/procgen_corpus_contract.json Tools/procgen_corpus.py Tools/procgen_corpus_sample.tsv Tools/test_procgen_corpus.py Source/DeepSpace/Tests/ProcGenCorpusTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/air-procgen commit -F - <<'MSG'
feat(corpus): the air's facts -- mix, pressure, scale height, nadir haze

Four columns on all three sides (atmospheres decision 2): air_mix,
surface_pressure_bar, scale_height_km and nadir_tau_450, procgen's facts
for every world. The report adds the air by mix: counts, median pressure,
median depth straight down at 450 nm. The noon sky of each world waits for
the optics (the corpus's skies, a separate file).

Measured: DeepSpace.Universe.Corpus costs N s over start-up.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 9: Prove the corpus test can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Tests/ProcGenCorpusTest.cpp '            Num(Planet.SurfacePressureBar, NonFinite),' '            Num(Planet.ReliefKm, NonFinite),' DeepSpace.Universe.Corpus
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh
```

Expected: `KILLED`. The row read back by name no longer holds the pressure.

- [ ] **Step 10: Merge track G so far.** On the developer's word through the orchestrator. This puts the draws, the guarantees and the spec's fixture table on `main` before slices 2 and 4 are planned, and before track O merges. First, the wear plan: if `feat/wear-1-s` has already merged to `main`, this merge is the second, so `main` comes into the tree first (it does in any case, below).

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && git merge -q main && ./build.sh && ./test.sh
cd /home/matt/Development/deepspace && git merge --no-ff feat/air-procgen -m "merge: air-procgen (atmospheres slice 1, track G: procgen's air, the draws and the corpus's facts)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
cd /home/matt/Development/deepspace && ./build.sh && ./test.sh
```

Expected: green in the tree after `main` came in, a clean merge (landing's P merged first; wear's S5 section, if merged, is a hunk of its own at the end of `Config/DefaultGame.ini`), and green on `main`. Keep the tree: Tasks 12 and 13 continue in it.

---

## Task 12 (G4): `PlanetAir`, the nadir guarantee as legibility, and the drawn fixtures' skies

This task joins procgen's air to the optics. `PlanetAir::SpecOf` is the one adapter from a planet's facts to an `FAirSpec`: the sky's side of the seam, as `FSkySystem::FromSystem` is for everything else.

`.NadirLegible` (decision 4) states the guarantee as a legibility number. A surface of albedo 0.1 beside one of 0.3, under an overhead G star, is seen from 400 km at nadir through the shipped law, for every mix at its ceiling and every giant at its disc. It must keep at least half the airless contrast in every channel. Planning note 6 measured a miss in the blue channel (0.34 at `MaxNadirTau450 = 0.5`), so this task is gated on ruling 2 and writes the test and the guarantee as ruled.

With `PlanetAir` in place, `.GroundSkySwatch` (Task 7) is re-pointed from the fixtures' stand-ins to the drawn worlds R, G and C, found by `AirFixtureWorlds` (Task 10), as the spec names it. `AtmosphereSwatchTest.cpp` is track O's file; O has merged, so it is this task's for that edit.

This task waits for **track O merged into `main`** (Task 8, Step 10) and for **ruling 2** (Step 1). Ruling 2 is recorded (2026-09-27): `MaxNadirTau450` is lowered to **0.32**, so Step 5 takes its first branch with `T = 0.32`, and Step 8 its first. Track O's optics fixtures already sit at the 0.32 ceilings (Task 4, Step 2), and `AtmosphereLaw` now reads eight bins: `.NadirLegible` compares colours from the entry points, which still return linear sRGB, so its code is unchanged. The re-plan's harness ran it through the eight-bin law: every mix at its 0.32 ceiling and every giant at its disc keeps 0.503-0.556 of its airless contrast, so it passes, with little margin.

**Files:**
- Create: `Source/DeepSpace/Atmosphere/PlanetAir.h`, `Source/DeepSpace/Atmosphere/PlanetAir.cpp`
- Test: create `Source/DeepSpace/Tests/AtmosphereNadirTest.cpp` (`DeepSpace.Atmosphere.PlanetAir`, `.NadirLegible`)
- Modify: `Source/DeepSpace/Tests/AtmosphereSwatchTest.cpp` (the includes; `Skies()`; one assertion in `.GroundSkySwatch`)
- Modify, only if ruling 2 lowers the guarantee: `Source/DeepSpace/Universe/GenPriors.h` (`MaxNadirTau450`), `Source/DeepSpace/Tests/AirFactsTest.cpp` (the ceilings' and discs' bounds), and the spec's *The fixtures as drawn* rows
- Modify: `docs/superpowers/specs/2026-09-27-atmospheres-design.md` (decision 4; the *Tests* section's `.NadirLegible` item)

**Interfaces:**
- Consumes: `FAirSpec`, `FAtmosphere::Build`, `EAtmosphereTable` and `AtmosphereLaw::{InScatterF64, SunThroughF64, NoEnd}` (track O); `AirFacts::*` (Task 9); `FPlanet::{AirMix, SurfacePressureBar, SurfaceGravityEarth()}` (Task 10); `AirFixtureWorlds::{Find, FFixture, UniverseSeed, SearchSystems}` (Task 10); `AtmosphereSwatchTestLocal::FSky` (Task 7); `UProcGenPriorsConfig`.
- Produces:

```cpp
namespace PlanetAir
{
    DEEPSPACE_API FAirSpec SpecOf(const FPlanet& Planet);
    DEEPSPACE_API FVector3d NoonZenith(const FPlanet& Planet, double StarTemperatureK);   // linear sRGB, pi convention; zero airless
    DEEPSPACE_API double Saturation(const FVector3d& Colour);                             // 1 - min / max, negatives as none; 0 for black
}
```

- [ ] **Step 1: The tree has track O, and ruling 2 is recorded.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && git merge -q main && test -f Shaders/Private/Atmosphere.ush && test -f Source/DeepSpace/Atmosphere/Atmosphere.h && test -f Source/DeepSpace/Tests/AtmosphereSwatchTest.cpp && echo "track O present"
cd /home/matt/Development/deepspace/.worktrees/air-procgen && grep -c '^\*\*Atmosphere plan ruling 2 ' docs/superpowers/specs/2026-09-27-atmospheres-design.md
```

Expected: `track O present`, then `1`. Otherwise stop: this task waits for Task 8's merge, or for ruling 2. Read the ruling: it either lowers `MaxNadirTau450` (to a number it names) or restates legibility (a definition it names). Step 5 applies it.

- [ ] **Step 2: Write the failing tests.** Create `Source/DeepSpace/Tests/AtmosphereNadirTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Atmosphere/PlanetAir.h"
#include "Universe/AirFacts.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseUnits.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmospherePlanetAirTest, "DeepSpace.Atmosphere.PlanetAir",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereNadirLegibleTest, "DeepSpace.Atmosphere.NadirLegible",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereNadirTestLocal
{
    constexpr double SunK = 5772.0;

    FPlanet Made(EPlanetKind Kind, double MassEarth, double RadiusEarth, double EquilibriumK, EAirMix Mix, double PressureBar)
    {
        FPlanet Planet;
        Planet.Designation = TEXT("Made");
        Planet.Kind = Kind;
        Planet.MassEarth = MassEarth;
        Planet.RadiusEarth = RadiusEarth;
        Planet.EquilibriumK = EquilibriumK;
        Planet.AirMix = Mix;
        Planet.SurfacePressureBar = PressureBar;
        return Planet;
    }

    /** The least, over the channels, of the contrast between an albedo-0.3
     *  and an albedo-0.1 surface under an overhead star, seen from 400 km
     *  straight down, as a fraction of the airless contrast: the sunlight
     *  that reaches the ground times the view's transmittance. The air's
     *  own light adds equally to both and cancels. */
    double NadirContrast(const FPlanet& Planet)
    {
        const FAirSpec Spec = PlanetAir::SpecOf(Planet);
        const FAtmosphere Air = FAtmosphere::Build(Spec, SunK);
        const double Altitude = 4.0e7 / Spec.RadiusCm;
        const FVector3d Up(0.0, 0.0, 1.0);
        const FAtmosphereScatter View = AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), Up * (1.0 + Altitude), -Up, Altitude, Up);
        const FVector3d Sun = AtmosphereLaw::SunThroughF64(Air.GetAir(), Up, Up);
        const double Bright = 0.3;
        const double Dark = 0.1;
        double Least = 1.0;
        for (int32 C = 0; C < 3; ++C)
        {
            const double Light = Bright * Sun[C] * View.Transmittance[C] + View.InScatter[C];
            const double Shade = Dark * Sun[C] * View.Transmittance[C] + View.InScatter[C];
            Least = FMath::Min(Least, (Light - Shade) / (Bright - Dark));
        }
        return Least;
    }
}

bool FAtmospherePlanetAirTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereNadirTestLocal;

    const FPlanet Earth = Made(EPlanetKind::Terrestrial, 1.0, 1.0, 255.0, EAirMix::NitrogenOxygen, 1.0);
    const FAirSpec Spec = PlanetAir::SpecOf(Earth);
    TestTrue(TEXT("an Earth has air"), Spec.HasAir());
    TestEqual(TEXT("its radius is the world's"), Spec.RadiusCm, UniverseUnits::CmPerEarthRadius);
    TestTrue(TEXT("its gas's scale height is AirFacts'"),
        FMath::IsNearlyEqual(Spec.GasScaleHeightCm, AirFacts::ScaleHeightKm(EAirMix::NitrogenOxygen, 255.0, 1.0) * UniverseUnits::CmPerKm));
    TestTrue(TEXT("its Rayleigh depth is Earth's 0.097"), FMath::IsNearlyEqual(Spec.GasTau550, 0.097, 1.0e-12));
    TestTrue(TEXT("its ozone 0.0415 and its haze 0.05"), FMath::IsNearlyEqual(Spec.OzoneTau600, 0.0415, 1.0e-12) && FMath::IsNearlyEqual(Spec.AerosolTau550, 0.05, 1.0e-12));
    TestTrue(TEXT("its haze's shape is the mix's"), FMath::IsNearlyEqual(Spec.AerosolScaleHeightCm, 1.2e5, 1.0e-6) && Spec.AerosolAsymmetry == 0.76 && Spec.AerosolAlbedo450 == 0.95);

    const FPlanet Rock = Made(EPlanetKind::Barren, 1.0, 1.0, 400.0, EAirMix::None, 0.0);
    TestFalse(TEXT("a barren world has no air"), PlanetAir::SpecOf(Rock).HasAir());
    TestTrue(TEXT("and no sky"), PlanetAir::NoonZenith(Rock, SunK).IsZero());

    const double JupiterG = 318.0 / 121.0;
    const FPlanet Jupiter = Made(EPlanetKind::GasGiant, 318.0, 11.0, 110.0, EAirMix::HydrogenHelium, AirFacts::GiantDiscPressureBar(JupiterG));
    const FAirSpec Giant = PlanetAir::SpecOf(Jupiter);
    const double Tau450 = Giant.GasTau550 * std::pow(550.0 / 450.0, 4.0) + Giant.AerosolTau550 * std::pow(450.0 / 550.0, -Giant.AerosolAngstrom);
    TestTrue(FString::Printf(TEXT("a Jupiter's disc is where straight down at 450 nm reaches the guarantee, %.3f (%.6f)"), GenGuarantees::MaxNadirTau450, Tau450),
        FMath::Abs(Tau450 - GenGuarantees::MaxNadirTau450) < 1.0e-9);

    const FVector3d Sky = PlanetAir::NoonZenith(Earth, SunK);
    TestTrue(FString::Printf(TEXT("an Earth's noon zenith under the Sun is blue (%.4f, %.4f, %.4f)"), Sky.X, Sky.Y, Sky.Z), Sky.Z > Sky.X);
    const double S = PlanetAir::Saturation(Sky);
    TestTrue(FString::Printf(TEXT("its saturation %.3f is a number between 0 and 1"), S), S > 0.0 && S < 1.0);
    TestEqual(TEXT("black has no saturation"), PlanetAir::Saturation(FVector3d::ZeroVector), 0.0);
    return true;
}

bool FAtmosphereNadirLegibleTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereNadirTestLocal;
    const EAirMix Mixes[] = {EAirMix::NitrogenOxygen, EAirMix::CarbonDioxide, EAirMix::HydrogenHelium};
    TArray<FPlanet> Worlds;
    for (const EAirMix Mix : Mixes)
    {
        for (const double G : {0.6, 1.0, 2.0})
        {
            // Mass g at radius 1: AirFacts reads the gravity, not the size.
            Worlds.Add(Made(EPlanetKind::Terrestrial, G, 1.0, 255.0, Mix, AirFacts::PressureCeilingBar(Mix, G, 1.0)));
        }
    }
    for (const double Mass : {15.0, 318.0, 3000.0})
    {
        const double G = Mass / 121.0;
        Worlds.Add(Made(EPlanetKind::GasGiant, Mass, 11.0, 110.0, EAirMix::HydrogenHelium, AirFacts::GiantDiscPressureBar(G)));
    }

    for (const FPlanet& Candidate : Worlds)
    {
        const double Contrast = NadirContrast(Candidate);
        const double G = Candidate.SurfaceGravityEarth();
        FString Diagnosis;
        if (Contrast < 0.5)
        {
            // Where this world would pass: the pressure fraction that brings
            // its worst channel to half, by bisection, as a depth at 450 nm.
            double Low = 0.0;
            double High = 1.0;
            for (int32 Step = 0; Step < 30; ++Step)
            {
                const double Mid = 0.5 * (Low + High);
                FPlanet Thinner = Candidate;
                Thinner.SurfacePressureBar = Candidate.SurfacePressureBar * Mid;
                (NadirContrast(Thinner) >= 0.5 ? Low : High) = Mid;
            }
            Diagnosis = FString::Printf(TEXT(" -- it would reach half at %.3f of its pressure, tau450 %.3f"),
                Low, AirFacts::NadirTau450(Candidate.AirMix, Candidate.SurfacePressureBar * Low, G));
        }
        AddInfo(FString::Printf(TEXT("%s at %.2f g, %.3f bar (tau450 %.3f): nadir contrast %.3f of airless%s"),
            AirFacts::Name(Candidate.AirMix), G, Candidate.SurfacePressureBar, AirFacts::NadirTau450(Candidate.AirMix, Candidate.SurfacePressureBar, G), Contrast, *Diagnosis));
        TestTrue(FString::Printf(TEXT("%s at %.2f g keeps half its surface's contrast straight down"), AirFacts::Name(Candidate.AirMix), G), Contrast >= 0.5);
    }
    return true;
}

#endif
```

- [ ] **Step 3: Run it; expect a compile failure.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh
```

Expected: FAIL, `'Atmosphere/PlanetAir.h' file not found`.

- [ ] **Step 4: The adapter.** Create `Source/DeepSpace/Atmosphere/PlanetAir.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Universe/StarSystem.h"

/**
 * A world's air as the optics take it, from procgen's facts: the one
 * adapter across the seam, as FSkySystem::FromSystem is for everything else
 * the sky draws. Pure.
 */
namespace PlanetAir
{
    /** The world's air as an FAirSpec: its radius, and -- if it has air --
     *  its gas's scale height and Rayleigh and ozone depths, and its mix's
     *  aerosol, all from AirFacts. Airless (HasAir() false) for EAirMix::None
     *  or no pressure. */
    DEEPSPACE_API FAirSpec SpecOf(const FPlanet& Planet);

    /** The noon zenith from the ground (AtmosphereLaw::NoonSun: straight up,
     *  the sun 45 degrees high) under a star of this temperature, through
     *  the shipped law: linear sRGB in the pi convention, for the star at
     *  unit luminance. Zero for an airless world. The corpus's
     *  sky_zenith_rgb. */
    DEEPSPACE_API FVector3d NoonZenith(const FPlanet& Planet, double StarTemperatureK);

    /** HSV saturation, 1 - min / max, with negative channels taken as none;
     *  0 for black. The corpus's sky_zenith_saturation. */
    DEEPSPACE_API double Saturation(const FVector3d& Colour);
}
```

  Create `Source/DeepSpace/Atmosphere/PlanetAir.cpp`:

```cpp
#include "Atmosphere/PlanetAir.h"

#include "Atmosphere/Atmosphere.h"
#include "Universe/AirFacts.h"
#include "Universe/UniverseUnits.h"

FAirSpec PlanetAir::SpecOf(const FPlanet& Planet)
{
    FAirSpec Spec;
    Spec.RadiusCm = Planet.RadiusEarth * UniverseUnits::CmPerEarthRadius;
    if (Planet.AirMix == EAirMix::None || !(Planet.SurfacePressureBar > 0.0))
    {
        return Spec;
    }
    const double Gravity = Planet.SurfaceGravityEarth();
    const FAirMixFacts& Facts = AirFacts::Facts(Planet.AirMix);
    Spec.GasScaleHeightCm = AirFacts::ScaleHeightKm(Planet.AirMix, Planet.EquilibriumK, Gravity) * UniverseUnits::CmPerKm;
    Spec.GasTau550 = AirFacts::RayleighTau550(Planet.AirMix, Planet.SurfacePressureBar, Gravity);
    Spec.OzoneTau600 = AirFacts::OzoneTau600(Planet.AirMix, Planet.SurfacePressureBar, Gravity);
    Spec.AerosolScaleHeightCm = Facts.AerosolScaleHeightKm * UniverseUnits::CmPerKm;
    Spec.AerosolTau550 = AirFacts::AerosolTau550(Planet.AirMix, Planet.SurfacePressureBar, Gravity);
    Spec.AerosolAngstrom = Facts.AerosolAngstrom;
    Spec.AerosolAsymmetry = Facts.AerosolAsymmetry;
    Spec.AerosolAlbedo450 = Facts.AerosolAlbedo450;
    Spec.AerosolAlbedo650 = Facts.AerosolAlbedo650;
    return Spec;
}

FVector3d PlanetAir::NoonZenith(const FPlanet& Planet, double StarTemperatureK)
{
    // Only the two table columns the noon zenith reads: exactly the full
    // table's there, at a sixteenth of the cost -- the corpus asks this of
    // thousands of worlds.
    const FAtmosphere Air = FAtmosphere::Build(SpecOf(Planet), StarTemperatureK, EAtmosphereTable::NoonOnly);
    if (!Air.HasAir())
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d Up(0.0, 0.0, 1.0);
    return AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), Up, Up, AtmosphereLaw::NoEnd, AtmosphereLaw::NoonSun()).InScatter;
}

double PlanetAir::Saturation(const FVector3d& Colour)
{
    const FVector3d C(FMath::Max(Colour.X, 0.0), FMath::Max(Colour.Y, 0.0), FMath::Max(Colour.Z, 0.0));
    const double Max = C.GetMax();
    return Max > 0.0 ? 1.0 - C.GetMin() / Max : 0.0;
}
```

- [ ] **Step 5: Apply ruling 2.** One of two, as the ruling says:
  - **The guarantee is lowered** to the ruled depth, `T`:
    - In `Source/DeepSpace/Universe/GenPriors.h`, replace `    inline constexpr double MaxNadirTau450 = 0.5;` with the same line holding the ruled number in place of `0.5`, and in its comment, after `never the rendering.`, add ` T, not decision 4's first 0.5: atmosphere plan ruling 2 (at 0.5 every mix at its ceiling kept 0.34 of its contrast).`
    - Every ceiling and every giant's disc is linear in the guarantee (both the gas's and the aerosol's depth go with `P`), so in `Source/DeepSpace/Tests/AirFactsTest.cpp` multiply both bounds of each of the six `Between` checks of the ceilings (`N2/O2 about 1.8 bar`, `CO2 about 1.17 bar`, `H2/He about 0.9 bar`) and the discs (`0.11 bar`, `about 2.4 bar`, `about 22 bar`) by `T / 0.5`, and their messages' numbers to match. The check `straight down at 450 nm the ceiling is the guarantee's 0.5` reads `GenGuarantees::MaxNadirTau450`; change only its message's `0.5` to the new number.
    - The optics fixtures' `*CeilingBar` constants and `Giant()` (`AtmosphereTestFixtures.h`) stay: they are the extremes the law was proven over in Task 8, and they are thicker than any world now is.
    - Pressures near a ceiling move, so the spec's *The fixtures as drawn* rows are refreshed in Step 7.
  - **Legibility is restated** (for example, Weber contrast, or the green channel's): in `NadirContrast`, replace the loop's body and the `Least` it returns with the ruling's definition, and change the test's message and threshold to the ruling's words. The guarantee stays 0.5.

- [ ] **Step 6: Build and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh && ./test.sh DeepSpace.Atmosphere.PlanetAir && ./test.sh DeepSpace.Atmosphere.NadirLegible && ./test.sh DeepSpace.Universe; grep -h "nadir contrast" Saved/Logs/DeepSpace.log | tail -12
cd /home/matt/Development/deepspace/.worktrees/air-procgen && for T in DeepSpace.Atmosphere.PlanetAir DeepSpace.Atmosphere.NadirLegible DeepSpace.Sky.Colour; do echo "$T"; time ./test.sh "$T"; done
```

Expected: `passed: 1` twice, every `DeepSpace.Universe.*` green (`.Gases` and `.Air` under the ruled guarantee), and twelve `nadir contrast` lines, each at or above the ruled threshold. A failure means the test does not yet say what ruling 2 says: fix it to the ruling, never the ruling to the test.

- [ ] **Step 7: Point the swatch at the drawn fixtures.** In `Source/DeepSpace/Tests/AtmosphereSwatchTest.cpp`:
  - After `#include "Atmosphere/Atmosphere.h"`, add `#include "Atmosphere/PlanetAir.h"`; after `#include "Sky/ShipSky.h"`, add `#include "Tests/AirFixtureWorlds.h"`; after `#include "Tests/AtmosphereTestFixtures.h"`, add `#include "Universe/ProcGenPriorsConfig.h"`.
  - Replace

```cpp
    /** The skies the developer judges: the spec's fixtures R, G and C by
     *  their stand-ins, until the drawn worlds exist (Task 12). */
    TArray<FSky> Skies()
    {
        using namespace AtmosphereTestFixtures;
        return {
            {TEXT("R_n2o2_2566K"), EarthAir(), HomeStarK},
            {TEXT("G_n2o2_5772K"), EarthAir(), SunK},
            {TEXT("C_co2_2566K"), CarbonDioxide(1.0), HomeStarK}};
    }
```

    with

```cpp
    /** The skies the developer judges: the spec's fixtures R, G and C as
     *  drawn (AirFixtureWorlds, the spec's rules), each under its own star. */
    TArray<FSky> Skies()
    {
        TArray<FSky> Out;
        const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();
        for (const AirFixtureWorlds::FFixture& Role : AirFixtureWorlds::Find(AirFixtureWorlds::UniverseSeed, Priors, AirFixtureWorlds::SearchSystems))
        {
            const FString Name(Role.Role);
            if (Role.bFound && (Name == TEXT("R") || Name == TEXT("G") || Name == TEXT("C")))
            {
                Out.Add({Name + TEXT("_") + Role.Planet().Designation.Replace(TEXT(" "), TEXT("_")),
                         PlanetAir::SpecOf(Role.Planet()), Role.System.Star.TemperatureK});
            }
        }
        return Out;
    }
```

  - In `FAtmosphereGroundSkySwatchTest::RunTest`, after `    const TArray<FSky> All = Skies();`, add `    TestEqual(TEXT("three skies to judge: the fixtures R, G and C as drawn"), All.Num(), 3);`.

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh && ./test.sh Atmosphere.Full.GroundSkySwatch && ./test.sh DeepSpace.Atmosphere.FixtureWorlds; grep -hE "swatch |fixture [RGCNJ]:" Saved/Logs/DeepSpace.log | tail -11; ls -l Saved/air_swatch_[RGC]_*.png
```

  Expected: `passed: 1` twice, six `swatch` lines naming the drawn worlds, and their six PNGs. If Step 5 lowered the guarantee, refresh the spec's *The fixtures as drawn* rows from the five `fixture` lines (same fields, same order). The six swatches go to the developer with Task 13's spread: slice 1's done-when asks that the drawn R, G and C skies "have gone to the developer".

- [ ] **Step 8: Amend the spec to ruling 2.** In `docs/superpowers/specs/2026-09-27-atmospheres-design.md`:
  - If the guarantee was lowered: in decision 4, replace `GenGuarantees::MaxNadirTau450 = 0.5` with the ruled number, add after the sentence it opens ` (Amended by atmosphere plan ruling 2: at 0.5 every mix at its ceiling kept 0.34 of its contrast, not the half this decision requires.)`, and rework its per-bar ceilings (`about 1.8 bar` and the others) and decision 2's giant disc pressures (0.11 and 2.4 bar) by `T / 0.5`. Ruling 8's own words ("capped at nadir by a guarantee in code") stand.
  - If legibility was restated: in decision 4's paragraph beginning `What it guarantees is stated as a legibility number`, replace the words `requires at least half the airless contrast in every channel` (they wrap after `at`) with the ruling's definition followed by ` (atmosphere plan ruling 2)`; and in the *Tests* section's `.NadirLegible` item, after `giants' disc depth included.`, add ` Legibility as atmosphere plan ruling 2 defines it.`

- [ ] **Step 9: The whole suite, then commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-procgen add Source/DeepSpace/Atmosphere/PlanetAir.h Source/DeepSpace/Atmosphere/PlanetAir.cpp Source/DeepSpace/Tests/AtmosphereNadirTest.cpp Source/DeepSpace/Tests/AtmosphereSwatchTest.cpp docs/superpowers/specs/2026-09-27-atmospheres-design.md
git -C /home/matt/Development/deepspace/.worktrees/air-procgen add Source/DeepSpace/Universe/GenPriors.h Source/DeepSpace/Tests/AirFactsTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/air-procgen commit -F - <<'MSG'
feat(atmosphere): PlanetAir -- a world's facts to its optics; the nadir guarantee as legibility, as ruled

PlanetAir::SpecOf is the one adapter from FPlanet's air to FAirSpec;
NoonZenith and Saturation are the corpus's sky. DeepSpace.Atmosphere.
NadirLegible holds decision 4's guarantee as a number, as atmosphere plan
ruling 2 states it: an albedo 0.1 and 0.3 surface under an overhead G star,
seen from 400 km through the shipped law, for every mix at its ceiling and
every giant at its disc. The spec's decision 4 and its Tests item say the
same. Atmosphere.Full.GroundSkySwatch now draws the fixtures R, G and
C as drawn (AirFixtureWorlds), each under its own star.

Measured: DeepSpace.Atmosphere.PlanetAir costs N s over start-up.
Measured: DeepSpace.Atmosphere.NadirLegible costs N s over start-up.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

  The second `git add` names files Step 5 changes only when the guarantee is lowered; unchanged, they add nothing. If Step 5 lowered it, add a line to the body before `Measured:`: `MaxNadirTau450 is T (ruling 2); .Gases' ceilings and discs scale by T / 0.5.`

- [ ] **Step 10: Prove the tests can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Atmosphere/PlanetAir.cpp 'Spec.GasTau550 = AirFacts::RayleighTau550(Planet.AirMix, Planet.SurfacePressureBar, Gravity);' 'Spec.GasTau550 = AirFacts::RayleighTau550(Planet.AirMix, Planet.SurfacePressureBar, 1.0);' DeepSpace.Atmosphere.PlanetAir
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Universe/GenPriors.h "$(grep -o 'inline constexpr double MaxNadirTau450 = [0-9.]*;' Source/DeepSpace/Universe/GenPriors.h)" 'inline constexpr double MaxNadirTau450 = 3.0;' DeepSpace.Atmosphere.NadirLegible
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Tests/AtmosphereSwatchTest.cpp '            if (Role.bFound && (Name == TEXT("R") || Name == TEXT("G") || Name == TEXT("C")))' '            if (Role.bFound && (Name == TEXT("R") || Name == TEXT("G")))' Atmosphere.Full.GroundSkySwatch
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh
```

Expected: `KILLED` three times.
- The first ignores the giant's gravity, so the Jupiter's disc misses tau 450's guarantee.
- The second puts every world under six times the haze or more, and none stays legible. (The first argument is the guarantee's line as it now stands, 0.5 or the ruled number.)
- The third drops fixture C, and the swatch no longer draws the three skies the spec names.

---

## Task 13 (G5): The corpus's skies, across all 10,000 systems

Slice 1's done-when asks that `procgen_corpus.py` show "the mixes, pressures and noon zenith colours across the 10,000 nearest systems", and decision 2 says the mix weights "are judged by" the spread of `sky_zenith_rgb`. One noon zenith builds two columns of its world's table, about 50 ms (measured in the plan's harness), and the 10,000 systems hold about 7,800 temperate worlds: six or seven minutes. That cannot go in `DeepSpace.Universe.Corpus`, which every worktree's suite runs behind the one lock (*Global Constraints*). So the skies are a writer of their own:
- `Atmosphere.Full.CorpusSkies`, outside the `DeepSpace` filter, run by name, writes `Saved/procgen_corpus_skies.tsv`: one row per temperate world of the corpus's 10,000 systems, keyed by sector, slot and orbit index, with its noon zenith (`PlanetAir::NoonZenith`, under the world's own star) and that colour's saturation.
- `procgen_corpus.py` reads it beside the corpus when it exists, and the report shows the saturation's spread, and the zenith colour by mix (median RGB, hue range, median saturation) with a hue histogram, since saturation alone cannot tell a peach sky from a blue one (review fix). The skies file has its own columns in the contract, `sky_columns`.
- A giant has no ground to see a sky from, and barren and ice worlds no air, so neither has a row.

**Files:**
- Create: `Source/DeepSpace/Tests/CorpusSkiesTest.cpp` (`Atmosphere.Full.CorpusSkies`)
- Create: `Tools/procgen_corpus_skies_sample.tsv`
- Modify: `Tools/procgen_corpus_contract.json` (a `sky_columns` list after `columns`)
- Modify: `Tools/procgen_corpus.py` (`DEFAULT_SKIES`, `load_skies`, `report`'s `skies` argument, `main`)
- Modify: `Tools/test_procgen_corpus.py` (`SKIES_SAMPLE`; four tests before `def main():`)

**Interfaces:**
- Consumes: `PlanetAir::{NoonZenith, Saturation}` (Task 12); `FPlanet::{AirMix, Kind, Id}` (Task 10); `UUniverseSubsystem::{GetRootSeed, GetPriors, GetStartSystem, GetSystem}` and `FGalaxyGenerator::{FindSystemsWithin, GenerateSystem, MaxSearchRadiusLy}`, as `ProcGenCorpusTest.cpp` uses them.
- Produces: `Saved/procgen_corpus_skies.tsv`, columns `sector_x`, `sector_y`, `sector_z`, `slot`, `planet`, `designation`, `sky_zenith_rgb` (three numbers, comma-separated) and `sky_zenith_saturation`; in Python, `load_skies(path, contract=None) -> dict[(sector_x, sector_y, sector_z, slot, planet)] = {"designation", "sky_zenith_rgb", "sky_zenith_saturation"}` and `report(rows, contract=None, skies=None)`.

- [ ] **Step 1: Write the failing Python tests.** In `Tools/test_procgen_corpus.py`, after the line `SAMPLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "procgen_corpus_sample.tsv")`, add:

```python
SKIES_SAMPLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "procgen_corpus_skies_sample.tsv")
```

  Append, before `def main():`:

```python
def test_the_skies_sample_is_written_to_the_contract():
    with open(SKIES_SAMPLE) as f:
        header = f.readline().rstrip("\n").split("\t")
    assert header == C.load_contract()["sky_columns"], header


def test_skies_are_typed_and_keyed_by_world():
    skies = C.load_skies(SKIES_SAMPLE)
    assert len(skies) == 2
    alpha3 = skies[(0, 0, 0, 0, 2)]
    assert alpha3["designation"] == "Alpha III"
    assert all(near(a, b) for a, b in zip(alpha3["sky_zenith_rgb"], (0.21, 0.34, 0.62)))
    assert near(alpha3["sky_zenith_saturation"], 0.66129)
    assert near(skies[(0, 1, 0, 1, 0)]["sky_zenith_saturation"], 0.175)


def test_report_gives_the_spread_of_skies():
    text = C.report(rows(), skies=C.load_skies(SKIES_SAMPLE))
    assert "  2 of 2 temperate worlds have a noon sky" in text, text
    assert "Noon zenith saturation of temperate worlds' skies (share of those with one)" in text, text


def test_report_without_skies_says_how_to_write_them():
    text = C.report(rows())
    assert "No skies: ./test.sh Atmosphere.Full.CorpusSkies writes Saved/procgen_corpus_skies.tsv" in text, text


def test_a_skies_file_missing_a_column_is_refused():
    with open(SKIES_SAMPLE) as f:
        lines = f.read().splitlines()
    cut = [line.split("\t") for line in lines]
    index = cut[0].index("sky_zenith_saturation")
    trimmed = "\n".join("\t".join(c[:index] + c[index + 1:]) for c in cut) + "\n"
    with tempfile.NamedTemporaryFile("w", suffix=".tsv", delete=False) as f:
        f.write(trimmed)
    try:
        C.load_skies(f.name)
    except ValueError as e:
        assert "sky_zenith_saturation" in str(e)
    else:
        raise AssertionError("a skies file without sky_zenith_saturation was read")
    finally:
        os.unlink(f.name)
```

- [ ] **Step 2: Run; expect FAIL.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && python3 Tools/test_procgen_corpus.py
```

Expected: FAIL, `KeyError: 'sky_columns'`, `No such file` for the skies sample, or `no attribute 'load_skies'`.

- [ ] **Step 3: The contract and the sample.** In `procgen_corpus_contract.json` replace

```json
  "classes": ["M", "K", "G", "F", "A", "B"],
```

  with

```json
  "sky_columns": ["sector_x", "sector_y", "sector_z", "slot", "planet", "designation", "sky_zenith_rgb", "sky_zenith_saturation"],
  "classes": ["M", "K", "G", "F", "A", "B"],
```

  Create `Tools/procgen_corpus_skies_sample.tsv`, tab-separated, the skies of the corpus sample's two temperate worlds:

```text
sector_x	sector_y	sector_z	slot	planet	designation	sky_zenith_rgb	sky_zenith_saturation
0	0	0	0	2	Alpha III	0.21,0.34,0.62	0.66129
0	1	0	1	0	Gamma I	0.4,0.38,0.33	0.175
```

- [ ] **Step 4: The reader.** In `procgen_corpus.py`:
  - After `DEFAULT_TSV = os.path.join(os.path.dirname(TOOLS), "Saved", "procgen_corpus.tsv")`, add:

```python
DEFAULT_SKIES = os.path.join(os.path.dirname(TOOLS), "Saved", "procgen_corpus_skies.tsv")
```

  - Immediately before `def systems(rows):`, add:

```python
def load_skies(path, contract=None):
    """The skies file (Atmosphere.Full.CorpusSkies) as a dict keyed by
    (sector_x, sector_y, sector_z, slot, planet), each a dict of the world's
    designation, its noon zenith as three floats and that colour's
    saturation. Raises ValueError if the file lacks a column the contract's
    sky_columns names."""
    contract = contract or load_contract()
    with open(path) as f:
        lines = f.read().splitlines()
    if not lines:
        raise ValueError("%s is empty" % path)
    header = lines[0].split("\t")
    missing = [c for c in contract["sky_columns"] if c not in header]
    if missing:
        raise ValueError("%s lacks columns %s" % (path, ", ".join(missing)))
    skies = {}
    for number, line in enumerate(lines[1:], start=2):
        if not line:
            continue
        cells = line.split("\t")
        if len(cells) != len(header):
            raise ValueError("%s:%d has %d cells, the header %d" % (path, number, len(cells), len(header)))
        raw = dict(zip(header, cells))
        key = tuple(int(raw[c]) for c in ("sector_x", "sector_y", "sector_z", "slot", "planet"))
        skies[key] = {
            "designation": raw["designation"],
            "sky_zenith_rgb": tuple(float(v) for v in raw["sky_zenith_rgb"].split(",")),
            "sky_zenith_saturation": float(raw["sky_zenith_saturation"]),
        }
    return skies


```

  - Replace `def report(rows, contract=None):` with `def report(rows, contract=None, skies=None):`, and its docstring `"""The whole report as text."""` with `"""The whole report as text. skies, if given, is load_skies' dict."""`.
  - In `report()`, immediately before `    out += places_or_rolls(found)`, after Task 11's air block, add:

```python
    temperate_worlds = [r for r in rows if r["kind"] in TEMPERATE]
    if skies is None:
        out.append("No skies: ./test.sh Atmosphere.Full.CorpusSkies writes Saved/procgen_corpus_skies.tsv")
    else:
        seen = [skies[k]["sky_zenith_saturation"] for k in
                ((r["sector_x"], r["sector_y"], r["sector_z"], r["slot"], r["planet"]) for r in temperate_worlds)
                if k in skies]
        out.append("  %d of %d temperate worlds have a noon sky" % (len(seen), len(temperate_worlds)))
        if seen:
            out += _render_histogram("Noon zenith saturation of temperate worlds' skies (share of those with one)",
                                     histogram(seen, [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]), len(seen))
    out.append("")

```

  - In `main`, replace `    print(report(load_corpus(path)))` with

```python
    skies = load_skies(DEFAULT_SKIES) if len(argv) <= 1 and os.path.exists(DEFAULT_SKIES) else None
    print(report(load_corpus(path), skies=skies))
```

  - In the module docstring, after `    python3 Tools/procgen_corpus.py PATH     # reads another`, add `    ./test.sh Atmosphere.Full.CorpusSkies    # writes Saved/procgen_corpus_skies.tsv, read beside it`.

- [ ] **Step 5: Run the Python; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && python3 Tools/test_procgen_corpus.py
```

Expected: every test `ok`, `0 failed`.

- [ ] **Step 6: The C++ writer.** Create `Source/DeepSpace/Tests/CorpusSkiesTest.cpp`:

```cpp
#include "Atmosphere/PlanetAir.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Not a test of correctness but a run whose output is read, like
 * DeepSpace.Universe.Corpus, and beside it: the noon zenith from the ground
 * of every temperate world in the corpus's 10,000 systems (atmospheres
 * decision 2, the sky the mix weights are judged by), under the universe the
 * game would play, to Saved/procgen_corpus_skies.tsv for
 * Tools/procgen_corpus.py. Named outside DeepSpace. because it takes minutes
 * (each sky builds two columns of its world's table), so the default suite,
 * which every worktree runs behind one lock, never runs it: run it by name.
 *
 * It fails only if its header no longer fits the contract's sky_columns,
 * something is not a number, no sky was written, or the file cannot be
 * written.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCorpusSkiesTest,
    "Atmosphere.Full.CorpusSkies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CorpusSkiesTestLocal
{
    /** DeepSpace.Universe.Corpus's CorpusSize: the same 10,000 systems. */
    constexpr int32 CorpusSize = 10000;

    /** The columns in the order the rows write them; the contract checks them. */
    const TCHAR* const WrittenColumns[] = {
        TEXT("sector_x"), TEXT("sector_y"), TEXT("sector_z"), TEXT("slot"), TEXT("planet"), TEXT("designation"),
        TEXT("sky_zenith_rgb"), TEXT("sky_zenith_saturation")};

    struct FTestWorld
    {
        UWorld* World = nullptr;

        explicit FTestWorld(const TCHAR* Name)
        {
            World = UWorld::CreateWorld(EWorldType::Game, false, Name);
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
        }

        ~FTestWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };

    TArray<FString> SkyColumnsOfContract(FString& OutError)
    {
        TArray<FString> Result;
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/procgen_corpus_contract.json"));
        FString Text;
        TSharedPtr<FJsonObject> Root;
        if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            OutError = TEXT("cannot read or parse ") + Path;
            return Result;
        }
        for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("sky_columns")))
        {
            Result.Add(Value->AsString());
        }
        return Result;
    }

    /** Six significant figures, as the corpus writes them. */
    FString Num(double Value, int32& NonFinite)
    {
        if (!std::isfinite(Value))
        {
            ++NonFinite;
        }
        return FString::Printf(TEXT("%.6g"), Value);
    }
}

bool FCorpusSkiesTest::RunTest(const FString& Parameters)
{
    using namespace CorpusSkiesTestLocal;

    // The contract first, so a header that no longer fits fails at once
    // rather than after minutes of skies.
    FString Error;
    const TArray<FString> Contract = SkyColumnsOfContract(Error);
    TArray<FString> Columns;
    for (const TCHAR* Column : WrittenColumns)
    {
        Columns.Add(Column);
    }
    if (!TestEqual(TEXT("the skies file writes the contract's sky_columns, in its order ") + Error,
            FString::Join(Columns, TEXT(",")), FString::Join(Contract, TEXT(","))))
    {
        return false;
    }

    // The universe the game would play, as DeepSpace.Universe.Corpus takes it.
    FTestWorld Test(TEXT("CorpusSkiesWorld"));
    const UUniverseSubsystem* Universe = Test.World->GetSubsystem<UUniverseSubsystem>();
    if (!TestNotNull(TEXT("the world has a universe"), Universe))
    {
        return false;
    }
    const FGalaxyGenerator Galaxy(Universe->GetRootSeed(), Universe->GetPriors());
    const TOptional<FStarSystem> Home = Universe->GetSystem(Universe->GetStartSystem());
    if (!TestTrue(TEXT("the universe has a home"), Home.IsSet()))
    {
        return false;
    }
    TArray<FStarSystemStub> Near = Galaxy.FindSystemsWithin(Home->Stub.Position, FGalaxyGenerator::MaxSearchRadiusLy * UniverseUnits::CmPerLightYear);
    Near.SetNum(FMath::Min(Near.Num(), CorpusSize));

    int32 NonFinite = 0;
    int32 Skies = 0;
    FString Tsv = FString::Join(Columns, TEXT("\t")) + TEXT("\n");
    FString FirstRow;
    FVector3d FirstZenith = FVector3d::ZeroVector;
    for (const FStarSystemStub& Stub : Near)
    {
        const FStarSystem System = Galaxy.GenerateSystem(Stub);
        for (const FPlanet& Planet : System.Planets)
        {
            if (Planet.Kind != EPlanetKind::Terrestrial && Planet.Kind != EPlanetKind::Ocean)
            {
                continue;
            }
            const FVector3d Zenith = PlanetAir::NoonZenith(Planet, System.Star.TemperatureK);
            const FString Row = FString::Join(TArray<FString>{
                FString::Printf(TEXT("%lld"), static_cast<long long>(Stub.Id.Sector.X)),
                FString::Printf(TEXT("%lld"), static_cast<long long>(Stub.Id.Sector.Y)),
                FString::Printf(TEXT("%lld"), static_cast<long long>(Stub.Id.Sector.Z)),
                FString::FromInt(Stub.Id.Slot),
                FString::FromInt(Planet.Id.Planet),
                Planet.Designation,
                Num(Zenith.X, NonFinite) + TEXT(",") + Num(Zenith.Y, NonFinite) + TEXT(",") + Num(Zenith.Z, NonFinite),
                Num(PlanetAir::Saturation(Zenith), NonFinite)}, TEXT("\t"));
            if (Skies == 0)
            {
                FirstRow = Row;
                FirstZenith = Zenith;
            }
            Tsv += Row + TEXT("\n");
            ++Skies;
        }
    }
    TestTrue(FString::Printf(TEXT("%d temperate worlds have a sky"), Skies), Skies > 0);
    TestEqual(TEXT("nothing in the skies is NaN or infinite"), NonFinite, 0);
    TestTrue(FString::Printf(TEXT("the first sky is a colour, not black (%.4f, %.4f, %.4f)"), FirstZenith.X, FirstZenith.Y, FirstZenith.Z),
        FirstZenith.GetMax() > 0.0);

    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir(), TEXT("procgen_corpus_skies.tsv"));
    TestTrue(TEXT("the skies are written to ") + Path, FFileHelper::SaveStringToFile(Tsv, *Path));

    // Read back as the Python will: the header, and the first row whole.
    TArray<FString> Lines;
    FFileHelper::LoadFileToStringArray(Lines, *Path);
    TestEqual(TEXT("one line per sky, plus the header"), Lines.Num(), Skies + 1);
    TestTrue(TEXT("the first row reads back as written"), Lines.Num() > 1 && Lines[1] == FirstRow);

    AddInfo(FString::Printf(TEXT("%d skies from %d systems -> %s. Then: python3 Tools/procgen_corpus.py"), Skies, Near.Num(), *Path));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

  `Near` is not sorted here, and need not be: `FindSystemsWithin`'s order is the corpus test's too, and each row is keyed by its world, not by its place in the file.

- [ ] **Step 7: Build, write the skies, and read them.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh && ./test.sh DeepSpace.Universe.Corpus && time ./test.sh Atmosphere.Full.CorpusSkies && python3 Tools/procgen_corpus.py | sed -n '/^Air, by mix/,/^$/p;/temperate worlds have a noon sky/,/^$/p'
```

Expected: `passed: 1` twice, then the *Air, by mix* block, the line `N of M temperate worlds have a noon sky` with N equal to M, and the saturation histogram of every temperate sky in the 10,000 systems. The run takes minutes; its `time` goes into the commit. The block and the histogram go to the developer with Task 10's fixture table and Task 12's swatches: slice 1's done-when asks that "the developer has seen the spread".

- [ ] **Step 8: The whole suite, then commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/air-procgen add Tools/procgen_corpus_contract.json Tools/procgen_corpus.py Tools/procgen_corpus_skies_sample.tsv Tools/test_procgen_corpus.py Source/DeepSpace/Tests/CorpusSkiesTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/air-procgen commit -F - <<'MSG'
feat(corpus): the noon sky of every temperate world in the 10,000 systems

Atmosphere.Full.CorpusSkies writes Saved/procgen_corpus_skies.tsv: each
temperate world of the corpus's systems, keyed by sector, slot and orbit
index, with its noon zenith from the ground (straight up, the sun 45
degrees high) through the shipped law under its own star, and that
colour's saturation -- the spread the mix weights are judged by
(atmospheres decision 2, sign-off item 19; slice 1's done-when). Minutes
of table columns, so it is named outside DeepSpace. and the default suite
never runs it. procgen_corpus.py reads it beside the corpus, against the
contract's sky_columns, and the report shows the saturation's spread.

Measured: Atmosphere.Full.CorpusSkies takes N s.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 9: Prove the writer can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && Tools/mutate.sh Source/DeepSpace/Tests/CorpusSkiesTest.cpp 'TEXT("sky_zenith_rgb"), TEXT("sky_zenith_saturation")};' 'TEXT("sky_zenith_saturation"), TEXT("sky_zenith_rgb")};' Atmosphere.Full.CorpusSkies
cd /home/matt/Development/deepspace/.worktrees/air-procgen && ./build.sh
```

Expected: `KILLED`, at once: the header no longer fits the contract, and the test returns before any sky is built.

- [ ] **Step 10: The tree is ready to merge.**

```bash
cd /home/matt/Development/deepspace/.worktrees/air-procgen && git merge -q main && ./build.sh && ./test.sh
```

Expected: green. If `feat/wear-1-s` merged to `main` since Task 11's merge, its section has just come in: this is the "second to merge merges `main` first".

- [ ] **Step 11: Merge track G.** On the developer's word through the orchestrator, and only when ruling 2 is recorded and encoded (Task 12), Tasks 12 and 13 are committed, and Step 10 was green:

```bash
cd /home/matt/Development/deepspace && git merge --no-ff feat/air-procgen -m "merge: air-procgen (atmospheres slice 1, track G: PlanetAir, the nadir guarantee, the corpus's skies)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
cd /home/matt/Development/deepspace && ./build.sh && ./test.sh
```

Expected: a clean merge and a green suite. Landing's track P merged first, track G has touched its files only since, and wear's S5 section is its own hunk. Use superpowers:finishing-a-development-branch for the tree.

---

# What orbital slice 1 (the follow-on plan) will need

This plan stops where `M_SkyBody` needs landing slice (a)'s Custom-node route. The follow-on is spec slice 1's track M, and whatever of O and G this plan leaves. It starts after landing (a) merges, and needs these, in about this order:

1. **The rulings above**, all seven recorded and encoded by this plan's gates. Ruling 3 chose spectral bins and ruling 6 eight of them, so `AT_Air` is 60 scalars (note 2), and no material is written against the old three-channel struct.
2. **The shim decision** (planning note 8), which the spec's *Parallel tracks* now makes track M wait on (Task 8, Step 9). Keep `AT_` shims inline, or extract one shared subset header with `WorldRelief.ush`. The latter touches `WorldRelief.ush`, which landing's track T owns through slice (b), so it is sequenced after slice (b) or done by T.
3. **The HLSL half, proven.** `Atmosphere.ush`'s HLSL half has never been compiled. The first material that includes it (`M_SkyAirProbe`, then `M_SkyBody`) is the first compiler, and `DeepSpace.Sky.MaterialContract`'s translator run is where a syntax error first shows. What to check:
   - `precise` on `AT_ProductError`'s and `AT_DiffOfProducts`' locals (DXC and SPIR-V `NoContraction`);
   - the macro `AT_TABLE_PARAM`'s `Texture2D` and `SamplerState` parameters (planning note 1). The Custom node's body calls, for example, `AT_InScatter(..., AirMultiScatter, AirMultiScatterSampler)`;
   - the `static const int` loop bounds, and `int(...)` casts.
4. **The texture routes** (spec *Risks*):
   - The per-world table is 32 x 32 texels of eight bins, `Texels[(Row * 32 + Column) * 8 + Bin]`, with row = altitude and column = `FAtmosphereTable::ColumnOf` of the sun's cosine (crowded toward the horizon), already rounded through half floats. It is uploaded as one transient 64 x 32 RGBA16F `UTexture2D`, bins 0-3 in the left half and 4-7 in the right, with no mips, no sRGB and clamped addressing, and set as `AirMultiScatter` per body.
   - The GPU's bilinear filter weights are 8-bit fixed point, and the C++ blends exactly. So the HLSL hook, as Tasks 5 and 6 write it, already `Load`s the four texels of each half and blends in float, as the C++ does, never `SampleLevel`. `Eyes.AtmosphereProbe`'s 1e-3 is the first proof of that. The sampler parameter stays unused in the hook's signature.
   - The eye's-air table `T_SkyAirHere` is slice 2's. It grows with the bins: 64 x 32 RGBA16F, a world's table's layout, not the spec's first 32 x 32 (Task 8, Step 6 amends decision 11 to say so).
5. **The material contract, re-cut to the law's struct** (planning note 2). Per body, eight bins each of gas scatter, gas extinction, aerosol scatter and aerosol extinction (two float4 apiece), the fold's R, G and B (two float4 apiece), and `AirShape` = (`GasH`, `AerosolH`, `AerosolG`, `Top`) in radii: fifteen float4, plus the `AirMultiScatter` texture. It goes on all three sides: `SkyMaterialContract.h`, `sky_material_contract.json` and the assets. The spec's `AirRayleigh`, `AirMie`, `AirAbsorb` and `AirShape` names are amended with the reason. The `MPC_Sky` `Here*` block (slice 2) takes the same shape.
6. **`FSkyBody` carries the air.** `FSkySystem::FromSystem` (`SkySystem.*`, free after landing (a)) builds `FAtmosphere::Build(PlanetAir::SpecOf(Planet), Star.TemperatureK)` per airy world, or holds the `FAirSpec` and builds at `AShipSky`'s system change.
   - At about 1.4 s per world for the full eight-bin table (measured in the harness at `-O2`), a system of several airy worlds costs seconds. So the build belongs off the game thread (`UE::Tasks`), with the air term drawn table-less (`EAtmosphereTable::None`) until it lands, or at the jump's fold.
   - `FSkyBody::Rim` is deleted with its `LookOf` colours, its contract entries, the Fresnel graph, `LocalSystemTest.cpp:144`'s rim assertion and `SkyTestFixtures.h`'s `Home.Rim`.
7. **The disc term in `M_SkyBody`.** It is `surface x AT_SunThrough(point) x T_view + AT_InScatter(eye to point)` through the Custom node. The eye is `(CameraPosition - ObjectPosition) / ProxyRadius`, per decision 6. Its multipliers are `Brightness` and the 1.5 disc gain (decision 9), and the star's colour is already in the scatter.
8. **The shell** (`M_SkyAir`, additive, `SM_SkyBody` at `1 + AirTop` radii):
   - the `AirMaterial` slot, and `FindMaterialProblems`;
   - `build_hauler.py`'s `place_sky`, and `check_sky`'s slot checks (sign-off item 20, approved);
   - `SkyProjection`'s stacking of an airy body later in the order on its outer radius;
   - the shell hidden below `ShellNearMin`'s altitude;
   - `SkyProjectionTest` and `ShipSkyTest` cases.
9. **`Eyes.AtmosphereProbe`:** `M_SkyAirProbe` against `AtmosphereLaw::InScatterF32`, the real proxy and shell, and the airless disc unchanged. Also the twilight-banding look from the hard shadow (planning note 3).
10. **The rest of slice 1's done-when:**
    - `ds.Air.Describe`, `ds.Air.Show` and `ds.Sky.Goto ... backlit`;
    - the level rebuild and `check_blueprints.py`;
    - the 4K frame at 16.6 ms (12 nodes, at most 14 with the refinement, eight bins, two texture reads a node, and the disc filling the view from 500 km). **Before measuring it, take `AT_SunAt`'s logarithms out of the node.** As written, each node computes `AT_log(AT_max(A.GasExtinct[K], AT_TINY))` and the aerosol's for every bin: 16 logarithms of numbers that never change for a given air, about 190 a pixel on top of the logarithmic means. Compute them once per march (an `AT_Bins` pair made in `AT_MarchAir` and `AT_SunThrough` and passed down to `AT_NodeAt` and `AT_SunAt`), or carry them in `AT_Air` (eight float4 more in the contract). The values are the same numbers, so every agreement test should read identically; the change is proved by `.FloatMatchesDouble`, `.SingleScatteringMatchesReference` and `Atmosphere.Full.LawMatchesReference` before the frame is timed. It is left out of Tasks 5-8 because the plan's harness verified the law as written, and it changes cost, not a value;
    - before and after frames of R, G and J to the developer;
    - `.GroundSkySwatch`'s drawn R, G and C (Task 12) and the corpus's spread (Tasks 11 and 13) are this plan's; the follow-on only confirms they reached the developer.
11. **The documentation "after the merges":**
    - CLAUDE.md's *The air* section and the architecture line for `Source/DeepSpace/Atmosphere/`;
    - `docs/vision.md`'s art direction for skies, restated if ruling 1 above moves it;
    - ADR 0009, and the sky and landing specs' pointers;
    - the six new priors in CLAUDE.md's *The universe* paragraph.
12. **The deferred choices this plan named:**
    - aerosol and ozone scaling with `P / g` (planning note 7);
    - `.BacklitRing`'s reading (planning note 10);
    - a directional multiple-scattering table, if ruling 7 is ever reopened (planning note 16).
