# Landing Slice 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Fly down and set down. From a solid world's 10 km drive floor the pilot brings the ship down on a third lever, the vertical lever (Space up, C down). The ship passes a seamless 50 km handover onto real, streamed terrain made from one height function, under real gravity felt as effort, and the ground catches it gently whatever the levers do. It settles onto the slope on three feet and reads LANDED. The pilot can walk the ship, and a fresh Space lifts it off. Each of the three slices is playable on its own: (a) the orbital look unchanged, (b) hover over real ground, (c) touchdown.

**Architecture:**
- **Slice (a).** `Shaders/Private/WorldRelief.ush` is written once, in a subset that is both HLSL and C++. It compiles into the pure C++ `FWorldRelief` and, through one Custom node, into `M_SkyBody`; the `DeepSpaceShaders` module maps `/Project` at `PostConfigInit`. The rendered `Eyes.WorldReliefParity` proves the two equal. Procgen draws each world's relief and hands `FSkyBody` its ground facts.
- **Slice (b), the flight.** Gravity is summed from every body and held, never integrated. The ground is an `IGroundField` on `FFlightSurface`, and the flight reads it analytically: a ray march, an eight-point footprint, the approach law and the skim cap. The vertical lever is ship state in `FShipFlightCommand`.
- **Slice (b), the terrain.** A pure cube-sphere quadtree and a pure tile builder sit under `AWorldGround`, which streams pooled mesh tiles onto the counter-frame. `M_SkyGround` shades them with `M_SkyBody`'s own graph, and the sky hands the body to the ground below 50 km, morphing the relief in.
- **Slice (c).** `EGroundContact` lives in `FShipFlightState`: the rest plane and its tripod, the settle, the LANDED latch, take-off, and the fold held while settling.

All logic is C++. Blueprints and materials only carry assets and parameters.

**Tech Stack:** Unreal Engine 5.8.2 C++ (module `DeepSpace`, new module `DeepSpaceShaders`); HLSL in a material Custom node; `ProceduralMeshComponent` (a custom primitive is the gated fallback); `UE::Tasks`; Python editor scripting (`Tools/setup_sky_materials.py`, `setup_flight_input.py`, `build_hauler.py`, `verify_level.py`); automation tests (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`) run through `./test.sh`, rendered checks through `Tools/eyes.sh`, and mutation proofs through `Tools/mutate.sh`.

**Spec:** /home/matt/Development/deepspace/docs/superpowers/specs/2026-09-27-landing-design.md (approved 2026-09-27, every sign-off item as recommended). Decision numbers below are the spec's.

## Global Constraints

- Land on solid surfaces only: rock, ice, and the land of terrestrial worlds (`EGround::Solid`). Oceans and gas giants keep the floor (`EGround::None`). No going outside.
- The drive keeps its 10 km floor (`ds.Flight.Floor`). Over a solid world it is 10 km above the highest peak (`FloorFor`). Below it, cruise and the vertical lever fly, and their floor is the ground plus gear clearance.
- Gravity is real everywhere, from every body, inverse-square at real masses: `GMSunCm3PerS2 = 1.3271244e26`, `GMEarthCm3PerS2 = 3.986004e20`. It is held by the boosters and never added to the velocity.
- Relief is `min(Strength_kind / g, MaxReliefKm) x Beta(A, B)`: `ReliefStrengthRockKm = 9`, `ReliefStrengthIceKm = 9`, `ReliefTerrestrialFactor = 0.7`, Beta(5, 2) for barren and ice, Beta(3, 2) for terrestrial. `GenGuarantees::MaxReliefKm = 10`, and never above 0.5 % of the radius. Oceans and giants have 0.
- One height function: `Height(D) = PeakCm x S(D) / S_max`, so `MaxHeightCm()` is exactly `PeakCm`. Nothing is finer than 5 m (`BandLimitCm = 500`). The per-pixel bands stop at 130 m (49,152 cycles per radius).
- The shared file is written in the scalar, no-swizzle subset, and every symbol is prefixed `WR_`. `WR_REAL` is `float` in HLSL and `double` (or `float`, for the mirror) in C++.
- Parity: max absolute difference 1e-3 over 256 x 256 samples at five footprints (`Eyes.WorldReliefParity`). It is never loosened: a FLOAT FLOOR verdict escalates.
- The vertical lever runs on a log scale from 0.1 m/s to `ds.Vertical.Top` 200 m/s, with HOVER as a detent at zero, `ds.Vertical.Sweep` 0.25 a second and `ds.Vertical.HeavyFloor` 0.25. It persists across F and when the pilot stands up. X and every fold set it to HOVER.
- The near regime is `ds.Land.Regime` 50 km above the nearest world's cruise floor, left above 55 km. Both levers blend across its top 20 %. `ds.Land.DriveHandback` is 500 m.
- The skim cap is max(`ds.Land.SkimFloor` 20 m/s, AGL / `ds.Land.SkimSeconds` 2.5 s).
- Approach law: `ds.Land.ApproachSeconds` 4 s (clamped to at least 0.5 s), contact at `ds.Land.TouchdownSpeed` 0.5 m/s, braking at 0.8 of the boosters.
- The footprint is four gear feet at `Z = -ds.Land.GearClearance` (150 cm), at (-700, -300), (-700, 400), (1600, -100) and (1600, 200), plus four belly corners at `Z = -10`. It is layout data (`GEAR`, `BELLY`).
- The ground always catches: no footprint point may end a substep more than 1 cm under the ground, and every lever-reached contact is at or under touchdown speed.
- The settle: `ds.Land.SettleBand` 8 m and `ds.Land.SettleDegPerSec` 4 deg/s, in roll and pitch only, added to the pilot's input. Landed means no foot more than 1 cm under, the tripod within 2 cm, and at rest within 1 cm/s.
- The hold costs 150 W per g (`ds.Boosters.HoldWatts`), capped at 3 g, ramped over the first km. It is paid only under a solid world's drive floor while airborne. `ds.Boosters.StarvedSink` is 2 m/s, never applied to a climb.
- `ds.Hum.HoldHiss` is 0.35 at the 3 g cap and never above `ds.Hum.CruiseHiss`. It is silent at every floor and at touchdown.
- The handover is at 50 km, where the proxy is the true sphere. The relief morphs from 0 at 50 km to 1 at the drive floor. The flight always uses the full relief.
- Terrain tiles are 33 x 33 vertices with skirts, 0.6 m spacing at the finest level, and no collision. Defaults: `ds.Terrain.SplitFactor` 2.0, `.MaxTiles` 2,500, `.BuildTasks` 2, `.UploadsPerFrame` 4, `.Show` 1. Tiles set no shadow casting, distance field, indirect lighting or ray tracing.
- `ds.Sky.Relief` and `ds.Sky.Craters` are retired in slice (b). Amplitude is data, from the priors.
- Black sky and worlds that do not spin.
- The descent from the drive floor takes `(H - 800 m) / 200 m/s + 28 s`, within 10 %. Below 1 km AGL the mesh under the ship stays within 15 cm of the analytic ground.
- The frame at 4K (3840 x 2160), with the project's anti-aliasing and upscaler on the RTX 4070 Ti SUPER, stays within 16.6 ms, with game, render-thread and GPU times reported separately.
- Anti-chore: no confirm, no charge display, no timer, and no failure state. The fold opens by itself only on the pilot's own aim.
- All logic lives in C++. The material contract has three sides: `SkyMaterialContract.h`, `Tools/sky_material_contract.json` and the assets.
- Build and test only through `./build.sh`, `./test.sh`, `Tools/eyes.sh` and `Tools/mutate.sh`, behind `Tools/ue_lock.sh`. Use at most 3-4 workers and `nice -n 19`.
- Every test is proven able to fail with `Tools/mutate.sh`. Every test path is a sibling with no children.

## Review Focus

These are the five classes of input the spec implies that no planner's tests exercised. They are the most likely to bite first. Each has a pinning test in its owning task.

1. **The vertical lever over a world with no ground** (an ocean or a giant). The regime is any world's (decision 8: "the floor sphere over an ocean or a giant"), but the descent cap reads only a ground. A full sink therefore met the sphere's hard stop at 200 m/s, a velocity step about six times what the boosters can do in a frame.
   - Pinned by `DeepSpace.Ship.Landing.SinkOntoFloorSphere`, with its fix: the braking curve to the floor sphere in the regime. **Task 20 (F6)**, Steps 8-10.
2. **The ship over a cube edge or a cube corner.** The quadtree's sweep used one nadir in the middle of a face. The seams, where two or three faces meet, are where neighbour finding, 2:1 balance and coverage break.
   - Pinned by `DeepSpace.Surface.QuadtreeAtCubeSeams`, whose seam oracle compares the leaves' own `GridDirection` edges across faces and calls neither `KeyAt` nor `EdgeProbe`, and proven by a seam-only mutant that the mid-face test survives. **Task 33 (T3)**, Steps 7-8.
3. **A landed ship far from the universe's origin.** Every touchdown fixture sat at the origin, yet the galaxy is chunked `FUniversePosition`s. The landed ship's exact stillness (distance moved `== 0.0`) and its re-seat must hold there too.
   - Pinned by its own test, `DeepSpace.Ship.Landing.LandedFarAway`, killed by a re-seat taken through a float absolute position. **Task 42 (C3)**.
4. **The gear's length changed at the console while landed** (`ds.Land.GearClearance`, a live CVar). The ship must re-seat on its new legs, up or down, and stay landed.
   - Pinned by `DeepSpace.Ship.Landing.LandedGear`. **Task 42 (C3)**.
5. **`ds.Land.SettleBand 0` at the console.** Contact settles only from inside the band, so a band of 0 means nothing ever settles and nothing ever lands. The band is held to `ShipLanding::MinSettleBandCm` (1 m).
   - The constant is added in **Task 40 (C1)**.
   - The clamp, its assertion, and a sink over Baemsekai IV that still lands with the band at 0, all in `DeepSpace.Ship.Landing.SettleTunables`, are in **Task 43 (C4)**.

## Execution order and parallel tracks

**Dependencies that must be on `main` first.** `fix/surface-artifacts` (`037e848`) and `feat/speed-bands` are both merged; `main` is at `202703c`, and this was checked while writing the plan. Task 1 (A0) re-checks them. Slice (a)'s material work (track R) rewrites `M_SkyBody`, which `fix/surface-artifacts` changed. Slice (b) edits the flight, subsystem and HUD files that `feat/speed-bands` changed, and every number in it is speed-bands'. Neither slice may start beside those branches; both start from them.

**Worktrees and file ownership.** Every changed file has exactly one owner per slice (spec, *Parallel tracks*):

| Slice | Track, tree, branch | Tasks | Owns |
|---|---|---|---|
| a | orchestrator (main checkout) | 1 (A0) | git only |
| a | **P: procgen**, `.worktrees/landing-a-procgen`, `feat/landing-a-procgen` | 2-5 (P1-P4) | `Surface/WorldReliefParams.h` (first: the seam header), `Universe/*`, `GenPriors.*`, `Config/DefaultGame.ini`, the corpus files, `Sky/SkySystem.*` |
| a | **R: relief**, `.worktrees/landing-a-relief`, `feat/landing-a-relief` | 6-12 (R1, R1-F1, R1-F2, R2-R5) | `Shaders/`, `Source/DeepSpaceShaders/`, `DeepSpace.uproject`, `Surface/WorldRelief.*`, `Tests/Eyes/WorldReliefParityTest.cpp`, `Tools/eyes.sh`, `Tools/mutate.sh`'s runner hook, `rebuild.sh`/`launch.sh`; in slice (a) also `setup_sky_materials.py`, `SkyMaterialContract.h`, `sky_material_contract.json` |
| b | orchestrator | 13 (B0), 39 (Z) | git; the 4K frame test; the *Landing* section of CLAUDE.md |
| b | **F: flight**, `.worktrees/landing-b-f`, `feat/landing-b-f` | 15-22 (F1-F8) | `Ship/ShipFlightState.*`, `ShipFlightSurface.*`, `ShipVerticalLever.*`, `ShipLanding.*`, `ShipGravity.*`, `Surface/GroundField.*`, their pure tests, `Tests/GroundFixtures.h`, `Tools/hauler_layout.py` (`GEAR`, `BELLY`), `Tools/test_placement.py` |
| b | **S: subsystem, power, input, HUD**, `.worktrees/landing-b-s`, `feat/landing-b-s` | 23-30 (S1-S8) | `ShipSubsystem.*` (including `UpdateSurfaces`), `ShipPowerState.*`, `ShipHum*`, `DeepSpaceCharacter.*`, `setup_flight_input.py`, `UI/ShipHUDWidget.*`, `UI/TargetMarker.*`, the Playtest tests |
| b | **T: terrain and sky**, `.worktrees/landing-b-t`, `feat/landing-b-t` | 14 (B1), 31-38 (T1-T8) | `Surface/TerrainQuadtree.*`, `TerrainTile.*`, `WorldGround.*`, `DeepSpace.Build.cs`, `DeepSpace.uproject` (B1's plugin entry; R's in slice (a)), `SkyProjection.*`, `ShipSky.*`, `Tools/sky_probe.py`, `build_hauler.py`/`verify_level.py` for `hauler_ground`; in slice (b), handed over from R, `setup_sky_materials.py`, the contract and `Surface/WorldRelief.*` |
| c | **one tree**, `.worktrees/landing-c`, `feat/landing-c` | 40-47 (C1-C8) | F's files for C1-C3, S's for C4-C6, docs for C7 |

**Order and merge points:**

```
Task 1 (A0) preconditions, both slice-(a) trees
  P:  2 (P1) -> 3 (P2) -> 4 (P3) -> 5 (P4) -> MERGE P into main            (P1's header is the seam: first)
  R:  6 (R1 spike, GO / NO-GO; needs nothing from P)
        GO      -> 9 (R2) -> [P merged] 10 (R3) -> 11 (R4) -> 12 (R5) -> MERGE R into main
        NO-GO, the module -> 7 (R1-F1), then 9-12 against the pasted text
        NO-GO, the subset -> stop; 8 (R1-F2) is a re-plan point: the report goes to the developer
        FLOAT FLOOR       -> stop; the report goes to the developer
  Slice (a) done when the developer sees no change on Baemsekai III, IV and V.

Task 13 (B0) the feat/landing-b branch and the three trees          (slice (a) merged)
  T:  14 (B1, the PMC gate, first) -> 31 (T1) -> 33 (T3) -> 34 (T4) -> 32 (T2) -> 36 (T6)
        -> [35 (T5) only on a CUSTOM PRIMITIVE verdict] -> 37 (T7) -> 38 (T8)
        (T4 and T6 wait on F2 merged into feat/landing-b; T6 also on S1; T8 also on S4)
  F:  15 (F1) -> 16 (F2) -> 17 (F3) -> 18 (F4) -> 19 (F5) -> 20 (F6) -> 21 (F7) -> 22 (F8)
  S:  23 (S1) ... 30 (S8), after F1-F7 are merged into feat/landing-b
        (S1 needs F2 and F6's limits; S4 needs F4 and F7)
  Merge each track into feat/landing-b as its tasks land (F first); the others
  `git merge -q feat/landing-b` before a task that needs another track's work.
  39 (Z) on the merged tree: the 4K frame, the developer's flight, the docs.
  feat/landing-b merges to main on the developer's word.

Task 40 (C1) ... 47 (C8) in .worktrees/landing-c, cut from main after slice (b) merges,
in order: the contact is pure (C1), then held by the flight state (C2, C3), then read
by the subsystem, the HUD and the playtests (C4-C6), then documented (C7) and flown (C8).
```

**If R1 takes a fallback, re-plan slice (b)'s shader tasks first.** Tasks 31, 32 and 37 (T1, T2, T7) are written for R1's GO path, where every Custom node includes `/Project/Private/WorldRelief.ush`.
- Under R1-F1 (the module refused), `custom_node` and `shared_terms` paste the file as R1-F1 Step 2 describes, and every call gets the `L.` prefix.
- Under R1-F2 (the subset refused), `M_SkyBody` keeps the engine nodes. T2's and T7's Custom-node edits become graph edits held by the rendered test.

The orchestrator settles which path applies before Task 13 (B0).

Slice (c) is one tree because C4-C6 compile against the `ShipFlightState.h` that C2-C3 change. Splitting it gains nothing but merge churn.

## Interfaces at the seams (reconciled)

These are the exact names every task uses. Where the three planners disagreed, the spec's name wins; where the spec was silent, the producing slice's name wins. Each consumer below has been rewritten to it.

**Slice (a) produces:**

```cpp
// Source/DeepSpace/Surface/WorldReliefParams.h (Task 2, P1) -- plain data
enum class EGround : uint8 { None, Solid };
struct FWorldReliefParams { FVector3d SeedOffset; double RadiusCm; double PeakCm; double Cratering; EGround Ground; };

// Universe (P1, P2)
inline constexpr double UniverseUnits::GMSunCm3PerS2 = 1.3271244e26, UniverseUnits::GMEarthCm3PerS2 = 3.986004e20;
constexpr uint64 GenSeed::SurfaceSeed(uint64 SystemSeed, uint64 Index);
static uint64 FStarSystemGenerator::PlanetSeed(uint64 SystemSeed, int32 Index);
static double FStarSystemGenerator::GenerateRelief(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors);
double FPlanet::ReliefKm; double FPlanet::SurfaceGravityEarth() const;
double FGenPriors::{ReliefStrengthRockKm, ReliefStrengthIceKm, ReliefTerrestrialFactor, ReliefBetaA, ReliefBetaB, ReliefTerrestrialBetaA, ReliefTerrestrialBetaB};
inline constexpr double GenGuarantees::MaxReliefKm = 10.0, GenGuarantees::MaxReliefRadiusFraction = 0.005;

// Source/DeepSpace/Sky/SkySystem.h (P4)
double FSkyBody::GravParam; EGround FSkyBody::Ground; FWorldReliefParams FSkyBody::Relief;
FVector3d SkyLook::SurfaceOffset(uint64 SurfaceSeed);

// Shaders/Private/WorldRelief.ush (R1, R2), reached as /Project/Private/WorldRelief.ush
// shims WR_floor, WR_frac, WR_saturate, WR_min, WR_max, WR_sqrt, WR_step; types WR_Vec3, WR_Hash3 {X, Y, Z},
// WR_Noise4 {GX, GY, GZ, Value}, WR_Site, WR_Crater, WR_Terms; tables WR_DETAIL_*, WR_CRATER_BANDS/FREQUENCY/INDEX;
// constants WR_CRATER_RADIUS, _INV_RADIUS, _DEPTH, _RIM, _WALL, _RIM_FALL, _KEEP, _FLOOR_DARK, _RIM_BRIGHT
WR_Hash3  WR_Rand3DPCG16(int PX, int PY, int PZ);
WR_Vec3   WR_VoronoiJitter(WR_REAL CX, WR_REAL CY, WR_REAL CZ);
WR_Noise4 WR_DetailBand(WR_REAL PX, WR_REAL PY, WR_REAL PZ, WR_REAL Frequency, WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL Width, WR_REAL Weight);
WR_Terms  WR_SurfaceTerms(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL Footprint, WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL Stretch);   // + VertexBandLimit in T2

// Source/DeepSpace/Surface/WorldRelief.h (R1, R3)
struct FFaceTerms { double Continent; double Detail; FVector3d DetailSlope; double CraterAlbedo; FVector3d CraterSlope; };
class FWorldRelief {
    static constexpr double BandLimitCm = 500.0;
    explicit FWorldRelief(const FWorldReliefParams&); const FWorldReliefParams& GetParams() const;
    double Height(const FVector3d& D, double FootprintCm = 0.0) const;
    double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm = 0.0) const;
    FFaceTerms Face(const FVector3d& D, double FootprintCm) const;
    double OmittedBoundCm(double FootprintCm) const; double MaxHeightCm() const; double MinHeightCm() const;
    double MaxSlope() const; double FinestWavelengthCm() const; TConstArrayView<double> GetDetailFrequencies() const;
    double DetailSum(const FVector3d& D, double FootprintRadius, FVector3d* Grad = nullptr) const;   // for T1
    double DetailBound() const; double DetailSlopeBound() const; double DetailOmittedBound(double FootprintRadius) const;
};
namespace WorldReliefNoise { SimplexValueBound; SimplexGradientBound; Hash16; Simplex; GradientNoise; Voronoi; CellHash;
    FFaceTerms FaceF64(const FVector3d& D, double FootprintD, const FVector3d& Offset, double Stretch);   // + VertexBandLimit = 1.0 in T2
    FFaceTerms FaceF32(const FVector3f& D, float FootprintD, const FVector3f& Offset, float Stretch);     // + VertexBandLimit = 1.0f in T2
    CraterMargin; FBands; Bands(); }

// SkyMaterialContract.h (R1): WorldReliefInclude, WorldReliefEntry ("WR_SurfaceTerms"), WorldReliefInputs(), WorldReliefOutputs(),
// ReliefProbePath, ProbeFootprint, ProbeSelect, ProbeBias, ProbeScalars(), ProbeVectors()
```

- Python (`setup_sky_materials.py`, R1): `Terms(coarse, fine, crater_face, crater_slope)`, `shared_terms(g, direction, footprint, seed, stretch)`, `surface(g, knobs, seed, direction, footprint, terms)`, `body_direction(g, axes)`, `probe_direction(g)`, `relief_probe(asset, terms)`.
- Tools: `Tools/eyes.sh <Eyes.Name>` and `MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh FILE OLD NEW Eyes.<Name>`.
- Corpus columns: `surface_gravity_g`, `relief_km`.

**Slice (b) produces (and changes of slice (a)'s names):**

| Name | Header, task |
|---|---|
| `WR_CraterTerm {H, GX, GY, GZ, Albedo}`, `WR_REAL WR_VertexCarries(WR_REAL VertexBandLimit, WR_REAL Frequency)`, `WR_CraterTerm WR_CraterKernelBand(WR_REAL PX, WR_REAL PY, WR_REAL PZ)`, `WR_CraterTerm WR_CraterSum(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL FootprintRadius, WR_REAL VertexBandLimit)`, `WR_CRATER_REACH`, `WR_CRATER_BOUND_COUNT`; `double FWorldRelief::SMax() const`, `double SlopeScale() const`; `Height` includes `Cratering x` the summed craters | `.ush`, `Surface/WorldRelief.h`, Task 31 (T1) |
| `WR_SurfaceTerms(..., WR_REAL Stretch, WR_REAL VertexBandLimit)`; `FaceF64(..., double Stretch, double VertexBandLimit = 1.0)`, `FaceF32(..., float VertexBandLimit = 1.0f)`; `shared_terms(g, direction, footprint, seed, stretch, vertex_band_limit=None)`; `surface()`'s knobs `(mottle, detail, banding, relief_scale, cratering)`; `SkyMaterial::ReliefScale`; `WorldReliefInputs()` + `VertexBandLimit`; `ShipSky::GiantReliefScale` (0.066), `double ShipSky::ReliefScaleOf(const FSkyBody&)` | Task 32 (T2) |
| `WR_GroundNormalOut WR_GroundNormal(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL VX, WR_REAL VY, WR_REAL VZ, WR_REAL SX, WR_REAL SY, WR_REAL SZ)`; `WorldReliefShading::FSurface {FFaceTerms Terms; FVector3d Slope; FVector3d Normal;}`, `Orbit(const FWorldReliefParams&, const FVector3d& D, double FootprintRadius)`, `Ground(const FWorldReliefParams&, const FVector3d& D, const FVector3d& VertexNormal, double FootprintRadius, double VertexBandLimit)`; `SkyMaterial::{GroundPath, GroundProbePath, Morph, BandLimit, TilePivot, VertexBandLimit, BandLimitPrimitiveIndex (0), TilePivotPrimitiveIndex (1), GroundScalars(), GroundVectors(), GroundProbeScalars(), GroundProbeVectors()}`; `M_SkyGround`, `M_SkyGroundProbe`; `hauler_ground`; `placement.GROUND_TAG` | Task 37 (T7) |
| `static void ShipSky::CopyBodyLook(UMaterialInstanceDynamic&, UMaterialInstanceDynamic&)`; tests `DeepSpace.Surface.GroundKeepsUp`, `Eyes.HandoverParity` | Task 38 (T8) |
| `void UShipSubsystem::AddWellForTest(const FGravityWell&)` (tests only) | Task 30 (S8) |
| `class IGroundField`, `FReliefGround`, `using FGroundFieldRef = TSharedPtr<const IGroundField, ESPMode::ThreadSafe>`, `FGroundFieldRef ShipGround::FromRelief(const FWorldReliefParams&)`, `FVector3d ShipGround::NormalAt(const IGroundField&, const FVector3d& D, double FootprintCm = 0)` | `Surface/GroundField.h`, Task 16 (F2) |
| `FGravityWell {FUniversePosition Centre; double Mu; double RadiusCm;}`, `FVector ShipFlight::GravityAt(TConstArrayView<FGravityWell>, const FUniversePosition&)`, `ShipFlight::StandardGravityCmS2 = 980.665` | `Ship/ShipGravity.h`, Task 15 (F1) |
| `FFlightSurface::bWorld`, `FGroundFieldRef FFlightSurface::Ground`, `bool FFlightSurface::HasGround() const`; `TOptional<double> ShipFlight::GroundAt(const FFlightSurface&, const FUniversePosition&)`; `TOptional<double> ShipFlight::RayToGround(const FFlightSurface&, const FUniversePosition& From, const FVector& Direction, double ClearanceCm, double MaxDistanceCm, int32* OutSteps = nullptr)`; `double ShipFlight::GroundApproachSpeed(double D, double BrakingAccel, double ApproachSeconds, double TouchdownSpeed, double Step)`; `double ShipFlight::SkimCap(double AglCm, double SkimSeconds, double SkimFloor)`; `ShipFlight::FGroundLaw`; `double ShipFlight::SecondsToGround(double PathCm, double Speed, double PathSine, const FGroundLaw&)`; `DefaultTouchdownSpeed` (50), `DefaultApproachSeconds` (4), `MinApproachSeconds` (0.5), `DefaultSkimSeconds` (2.5), `DefaultSkimFloor` (2000), `GroundMarchSteps` (64), `DefaultRegimeCm` (5e6), `RegimeExitFactor` (1.1), `RegimeBlendFraction` (0.2), `DefaultDriveHandbackCm` (5e4); `GroundFixtures::*` (tests) | `Ship/ShipFlightSurface.h`, Task 16 (F2) |
| `ShipLanding::DefaultGearClearanceCm` (150), `BellyZCm` (-10), `GearFeetXY[4][2]`, `BellyCorners[4][3]`, `FootprintPoints(double)`, `ReachCm(double)`, `FFootprintClearance {Least, Point, GroundNormal}`, `FootprintClearance(const FFlightSurface&, const FUniversePosition&, const FQuat&, double GearClearanceCm)`; `GEAR`, `BELLY` | `Ship/ShipLanding.h`, Task 17 (F3) |
| `ShipVerticalLever::{Rate, LeverOf, Sweep, Catch, ClimbTop}` and constants | `Ship/ShipVerticalLever.h`, Task 18 (F4) |
| `FShipFlightCommand::Vertical`; `FShipFlightLimits::{GearClearanceCm, TouchdownSpeed, ApproachSeconds, SkimSeconds, SkimFloor, RegimeCm, DriveHandbackCm, VerticalTop, VerticalHeavyFloor, SinkBias}`; `EFlightMode::DriveBelowFloor`; `FGroundLog`; `FShipFlightState::{SetWells, GetWells, GetLocalGravity, GetThrustAcceleration, TOptional<double> GetGroundAltitude() const, TOptional<double> GetFootprintClearance() const, GetDepthUnderDriveFloor, GetGroundLog, ResetGroundLog, IsInNearRegime, GetRegimeWeight, IsVerticalLive, GetVerticalLeverRate, GetVerticalSpeed}`; private `NearestGround()`, `GroundHardStop()` | `Ship/ShipFlightState.h`, Tasks 15-22 (F1-F8) |
| `float ShipPower::HoldWant(double GravityCmS2, double DepthUnderFloorCm, float WattsPerG, bool bAirborne)`, `FBoosterSplit`, `SplitBoosters`; `float UShipSubsystem::GetHoldWant() const`, `float GetHoldWatts() const` (watts delivered), `static float GetHoldWattsPerG()` (S3), `bool SetVerticalLever(APawn*, double)`, `static double GearClearance()`, `static double FloorFor(const FSkyBody&)` | Tasks 23-26 (S1-S4) |
| `IA_VerticalUp` (Space), `IA_VerticalDown` (C); `ADeepSpaceCharacter::TapVertical(int32)`, `HoldVertical(int32)` | Task 27 (S5) |
| `static FString UShipHUDWidget::GroundLine(double GroundAltitudeCm, double VerticalSpeedCmPerSecond, EFlightHold Hold)`, `VerticalWords`, `VerticalLeverWords` | Task 28 (S6) |
| the target line's ETA to the ground in cruise (`GetTargetView`, `DeepSpace.UI.TargetMarker.GroundEta`) | Task 29 (S7) |
| `FTileKey`, `TerrainQuadtree::*`, `FTileBuild`, `TerrainTile::*`, `AWorldGround`, `ds.Terrain.*` | Tasks 33-36 (T3-T6) |

**Slice (c) produces:** `enum class EGroundContact : uint8 { Airborne, Settling, Landed }`, `ShipLanding::{DefaultSettleBandCm, DefaultSettleDegPerSec, MinSettleBandCm, LandedPenetrationCm, TripodToleranceCm, AtRestCmPerSecond, FRestPlane, RestPlane, SettleRate, SettleSkimCap, FContactInputs, NextContact}` (C1); `FShipFlightLimits::{SettleBandCm, SettleRadPerSecond}`, `FShipFlightState::{GetContact, GetRestPlane, GetTouchdownCount, GetGroundSurface}`, `Tests/LandingTestFixtures.h` (C2); `ds.Land.SettleBand`, `ds.Land.SettleDegPerSec` and `Tests/LandingPlaytest.h` (C4); `GroundLine(..., EGroundContact Contact = EGroundContact::Airborne)` (C5).

**Reconciled (what changed from the three drafts):**

- **The shared file's entry point.**
  - Slice (b) was written against a `WR_SkyBodySurface` / `WR_SkyGroundSurface` pair that composed the whole face in HLSL. Slice (a) builds `WR_SurfaceTerms`, raw terms only, and composes the face in the material graph (`surface()` in Python), so it stays that way.
  - T2 extends `WR_SurfaceTerms` with `VertexBandLimit`.
  - T7 composes `M_SkyGround` with the same `surface()` graph and adds only `WR_GroundNormal`.
  - `WorldReliefShading` now returns the terms, the slope and the normal, not a composed face.
- **Types and names in the shared file.**
  - `WR_UInt3` is `WR_Hash3`.
  - The bare shims (`saturate`, `floor`, `max`, `sqrt`) are `WR_saturate`, `WR_floor`, `WR_max` and `WR_sqrt`. Casts are `WR_REAL(...)`.
  - The crater sites come from `WR_VoronoiJitter`.
  - `WR_CRATER_DEPTH` and `WR_CRATER_RIM` are added to R1's constants.
  - The C++ reaches the file's symbols through `namespace WR64 = WorldReliefF64`.
- **`FWorldRelief`'s detail-sum API** (`DetailSum`, `DetailBound`, `DetailSlopeBound`, `DetailOmittedBound`) was assumed by T1. It is now R3's public API; before, it was a private `Sum`.
- **Earth's surface gravity** is 979.8398 cm/s^2 in both P1 and F1. The spec's 979.85 is rounded, and F1's 0.01 tolerance would have failed.
- **Slice (c)'s guessed signatures, each replaced by slice (b)'s:**

  | Slice (c) guessed | Slice (b)'s |
  |---|---|
  | `FFlightGround`, `TOptional<FFlightGround> FFlightSurface::Ground`, `FFlightGround::ClearanceCm` | `FGroundFieldRef Ground`, `HasGround()`, `FShipFlightLimits::GearClearanceCm` |
  | `double GroundAt` | `TOptional<double>` |
  | `double GetFootprintClearance()`, `GetGroundAltitude()` | `TOptional<double>` |
  | `ShipLanding::GearFeet[4]` (FVector) | `GearFeetXY[4][2]` |
  | `GetHoldWattsDelivered()` | `GetHoldWatts()` |
  | private `IsUnderSolidFloor()` | `HoldWant`'s `bAirborne` argument |
  | the `HorizontalSpeed` local | F6's `Horizontal` |
  | `GroundLine`'s `VerticalSpeedCmPerS` | `VerticalSpeedCmPerSecond` |

  `GetGroundSurface()` did not exist in slice (b), so C2 adds it over F5's `NearestGround()`. `LandingTestFixtures.h` did not exist either, so C2 creates it on slice (b)'s `IGroundField` and `GroundFixtures`.
- **The ETA to the ground** was planned twice. Slice (b)'s S7 (with F2's `SecondsToGround` and `DeepSpace.Playtest.EtaCountsDownToTheGround` in S8) is kept.
  - Slice (c)'s Task 5 is deleted: a second `SecondsToGround`, a clashing `DeepSpace.Ship.Landing.SecondsToGround` test, `FTargetGroundEta`, and a second `GetTargetView` rewrite.
  - Its CLAUDE.md paragraph is deleted too.
- **ADR 0005.**
  - F1 records gravity held.
  - C7's amendment records only the settle and the landed re-seat, and `JumpTo` clearing the contact.
- **File names.**
  - Slice (c)'s playtest file is `PlaytestTouchdownTest.cpp`, beside slice (b)'s `PlaytestLandingTest.cpp`.

## Conventions for every task

- **Numbering.** Tasks are numbered 1-47 and keep their track IDs (A0, P1-P4, R1-R5, B0, B1, F1-F8, S1-S8, T1-T8, Z, C1-C8). Cross-references inside tasks use the ID, which is in every task's heading.
- **Trees.** Every command names its tree (`cd /home/matt/Development/deepspace/.worktrees/<tree> && ...` or `git -C <tree>`), because an agent's working directory is reset between calls.
  - Slice (a)'s trees are `landing-a-procgen` and `landing-a-relief`.
  - Slice (b)'s trees are `landing-b-f`, `landing-b-s` and `landing-b-t`, off `feat/landing-b`.
  - Slice (c)'s tree is `landing-c`: every slice (c) command runs from `/home/matt/Development/deepspace/.worktrees/landing-c`.
- **The lock and the cap.**
  - Build only with `./build.sh`, test only with `./test.sh <path>`, render only with `Tools/eyes.sh <Eyes.Name>`, and mutate only with `Tools/mutate.sh`. All of them wait on `Tools/ue_lock.sh`.
  - Never call `Build.sh`, UBT or `UnrealEditor-Cmd` directly, except through `ue_locked` for the authoring commandlets named in steps.
  - The editor must be closed.
  - Never more than 3-4 workers, `nice -n 19` for any local sweep, and terrain build tasks at 2.
- **Header changes** (`.h`, `.ush`): every new field on `FShipFlightCommand`, `FShipFlightLimits`, `FFlightSurface` or `FSkyBody` needs a full `./build.sh`. If an editor was open on the tree, run `./rebuild.sh --force` first.
- **Unity is off.** Test files use a named local namespace (`<File>Local`), never an anonymous one.
- **Tests are siblings.** No test path may be the parent of another. Group paths such as `DeepSpace.Surface.WorldRelief` and `DeepSpace.Ship.Landing` are never themselves tests.
  - `DeepSpace.Ship.HumVoice`, `DeepSpace.Sky.Projection` and `DeepSpace.UI.HUDAltitude` are existing leaves.
  - So this plan adds `DeepSpace.Ship.HumHold`, `DeepSpace.Sky.ProjectionAtGround` and `DeepSpace.UI.HUDGround` beside them.
- **Mutation.**
  - `Tools/mutate.sh FILE 'exact old text' 'new text' FILTER` must print `KILLED`.
  - It refuses uncommitted files, so every mutation comes after its task's commit.
  - A surviving mutant means the test is strengthened and re-committed, never the mutation weakened.
  - Run `./build.sh` after every mutation run, because its last build held the mutant.
  - Rendered tests mutate with `MUTATE_RUNNER=Tools/eyes.sh`.
- **Generated actors and Blueprints.**
  - A change to a generated actor's components means rebuilding the level (`Tools/build_hauler.py`) and then `Tools/verify_level.py`. Only Task 37 (T7) does this (it adds `hauler_ground`); Task 38 (T8) changes `ShipSky`'s code, not `hauler_sky`'s components.
  - Removing a `UPROPERTY`, component or `BlueprintImplementableEvent` means `Tools/check_blueprints.py`. Task 39 (Z) runs it, and none of slice (a) or (c) removes one.
  - New keys go only through `Tools/setup_flight_input.py` (Task 27 (S5)).
- **Units.** Centimetres, seconds, cm/s and cm/s^2 throughout the C++. CVars use the unit the spec's table names and are converted where they are read.
- **Line numbers** are "about", from `202703c`. Search for the quoted code, not the number.
- **Commits** end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Planning notes the executor must know

**RULINGS AFTER PLANNING (2026-09-27; binding over anything below, including Global Constraints' "1e-3, never loosened"):** parity per footprint and per term at the measured float floor (spec rulings, *Ruled at R2*); the DeepSpaceShaders module removed, with a test that the engine's own `/Project` mapping finds `WorldRelief.ush`; and **slice (b) gains a task, owned by track T before T2**: the shared file's integer/fraction split of each band's lattice offset, with the parity tolerances tightened to what it then measures, the orbital look unchanged (a before/after render diff).


These were raised while planning. None changes a ruling.

1. **Tolerance at float's floor.** The noise coordinates reach offsets of about 1,250 (detail) and about 9,000 (craters), where a float's step is 1e-4 to 1e-3 of a cell. The 1e-3 parity tolerance may therefore sit at float's floor.
   - R1's verdict table has a **FLOAT FLOOR** row that stops and escalates rather than loosening the tolerance.
2. **The crater step mask.** Today's Voronoi craters step at the rim and at the bisectors, so R1's metric leaves out samples within 2e-3 cells of a step. At most 1 % may be left out, and the count is reported.
   - T2's summed kernels remove the steps. The mask stays as a harmless guard.
3. **Loose bounds -- a departure from ruling 6 that the developer rules on before slice (b) opens.** The proven noise bounds are loose (3.18 against 0.9997 seen; 24.2 against 5.93), and the spec normalises the height by the proven bound (`S_max`, "its analytic bound"). So:
   - drawn mountains reach about a third of `PeakCm`: decision 3's 1 g barren world "around 6.4 km" draws about 2.1 km, and a light world at the 10 km cap about 3.3 km, against ruling 6's "mountains to about 8-10 km";
   - the drive floor, 10 km above `PeakCm` (`FloorFor`), sits about 10 km + 2/3 `PeakCm` -- 13 to 17 km -- above the highest summit actually drawn, and a descent from the floor, `(H - 800 m) / 200 m/s + 28 s`, takes about 75-140 s, not the spec's 75-125 s (H is 10 km + `PeakCm` less the local ground, which lies within about a third of `PeakCm` of the datum);
   - `MaxSlope` is about four times the truth, so F2's ray march takes more steps than it needs (safe, and not a ruling).
   - Tightening the height's normalisation to a measured bound changes the spec's definition of `S_max` and of `MaxHeightCm` (and with them `FloorFor`, the ray march's shell and the quadtree's height ranges), so it is not this plan's to take. **Task 13 (B0) Step 0** puts it to the developer with R3's measured share; slice (b) opens only on the ruling.
   - **RULED (developer, 2026-09-27): scale by the measured maximum, with a hard cap.** `S_max` is the measured maximum of S over at least 200,000 samples (recorded as a constant with the sample count and seed in its comment), and `Height` passes through a smooth saturating cap so it can never exceed `PeakCm` -- `MaxHeightCm` stays exactly `PeakCm` by construction, now reached in practice. Task 10 (R3) implements this instead of the proven `SimplexValueBound` normalisation (the proven bound stays, as the cap's guarantee argument), with a test that the height never exceeds `PeakCm` over a dense sweep and that the drawn peaks reach at least 0.9 `PeakCm`. Task 13 (B0) Step 0 is answered.
4. **The shader-path test headless.** `AddShaderSourceDirectoryMapping` does nothing when shader compiling is off. If `DeepSpace.Surface.ShaderMapping` cannot pass under `-nullrhi`, R1 Step 6 moves it into the rendered test.
5. **Staleness checks.** R1 teaches `rebuild.sh` and `launch.sh` about the second module and `Shaders/`.
6. **Where slice (b) departs from the spec's wording** (each is argued in its task):
   - The tile height rides UV2.y in km, because PMC's UVs are half floats.
   - The morph is a World Position Offset scaled by one `Morph` parameter.
   - Tile pivots sit on the ground.
   - The "cruise nose-down flies to the ground" test runs with the regime off (decision 8 forbids diving), plus the C leg.
   - Pressing F under the floor skips the spool-down.
   - Crater height no longer varies by basin, only crater albedo does.
   - `GroundAlwaysCatches` is split into four siblings over the whole grid.

---

# Slice (a): one height function, the orbital look unchanged

Tracks P and R. Nothing a player can see is new: that is the point.

## Task 1 (A0): Preconditions

**Files:** none.

- [ ] **Step 1: Check `fix/surface-artifacts` is merged.**

```bash
git -C /home/matt/Development/deepspace merge-base --is-ancestor 037e848 main && echo MERGED || echo NOT-MERGED
```

Expected: `MERGED`. If `NOT-MERGED`: track P may proceed; track R must not start
(it rewrites `M_SkyBody`, which that branch changes). Report and wait.

- [ ] **Step 2: Create both worktrees from `main`.**

```bash
git -C /home/matt/Development/deepspace worktree add .worktrees/landing-a-procgen -b feat/landing-a-procgen main
git -C /home/matt/Development/deepspace worktree add .worktrees/landing-a-relief -b feat/landing-a-relief main
```

- [ ] **Step 3: Build each once** (a cold build of a fresh worktree).

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh
```

Expected: `Result: Succeeded` in each.

---

# Slice (a), track P: procgen

## Task 2 (P1): The ground's facts -- `WorldReliefParams.h`, GM, the face's seed named, `PlanetSeed` public

**Files:**
- Create: `Source/DeepSpace/Surface/WorldReliefParams.h`
- Modify: `Source/DeepSpace/Universe/UniverseUnits.h` (append inside the namespace, after `SolarTemperatureK`, line ~45)
- Modify: `Source/DeepSpace/Universe/GenSeed.h` (after `Derive`, line ~79)
- Modify: `Source/DeepSpace/Universe/StarSystemGenerator.h` (public section, after `GenerateGiantDay`, line ~38)
- Modify: `Source/DeepSpace/Universe/StarSystemGenerator.cpp:201` and append a definition
- Modify: `Source/DeepSpace/Sky/SkySystem.cpp:19-22` (`SurfacePurpose`) and `:84`
- Test: create `Source/DeepSpace/Tests/GroundFactsTest.cpp` (`DeepSpace.Universe.GroundFacts`)

**Interfaces:**
- Consumes: `GenSeed::Derive`, `GenSeed::Label`, `FStarSystemGenerator::GenerateGiantDay(uint64)`, `FSkySystem::FromSystem`.
- Produces: `EGround`, `FWorldReliefParams` (header above); `UniverseUnits::GMSunCm3PerS2`, `GMEarthCm3PerS2`; `constexpr uint64 GenSeed::SurfaceSeed(uint64 SystemSeed, uint64 Index)`; `static uint64 FStarSystemGenerator::PlanetSeed(uint64 SystemSeed, int32 Index)`.

- [ ] **Step 1: Write the failing test.** Create `Source/DeepSpace/Tests/GroundFactsTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Sky/SkySystem.h"
#include "Surface/WorldReliefParams.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/GenSeed.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/StarSystemGenerator.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGroundFactsTest,
    "DeepSpace.Universe.GroundFacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GroundFactsTestLocal
{
    constexpr uint64 Root = 20260925;

    // The face's seed is the ground's now (landing decision 2), and it keeps
    // the derivation every world's face was made with: a change here moves
    // every place anybody has been.
    static_assert(GenSeed::SurfaceSeed(1, 2) == GenSeed::Derive(1, GenSeed::Label("sky.surface"), 2),
        "GenSeed::SurfaceSeed is the sky's derivation, unchanged");
}

bool FGroundFactsTest::RunTest(const FString& Parameters)
{
    using namespace GroundFactsTestLocal;

    // -- The mass parameters (landing decision 4): IAU 2015 nominal values ------
    const double Ratio = UniverseUnits::GMEarthCm3PerS2 / UniverseUnits::GMSunCm3PerS2;
    TestTrue(FString::Printf(TEXT("the Earth's GM over the Sun's (%.7e) is EarthMassSolar to 5e-5"), Ratio),
        FMath::Abs(Ratio / UniverseUnits::EarthMassSolar - 1.0) < 5.0e-5);
    const double EarthSurface = UniverseUnits::GMEarthCm3PerS2 / FMath::Square(UniverseUnits::CmPerEarthRadius);
    TestTrue(FString::Printf(TEXT("an Earth pulls 979.840 cm/s^2 at its equatorial radius (%.4f)"), EarthSurface),
        FMath::Abs(EarthSurface - 979.8398) < 1.0e-3);
    const double SunAtOneAU = UniverseUnits::GMSunCm3PerS2 / FMath::Square(UniverseUnits::CmPerAU);
    TestTrue(FString::Printf(TEXT("the Sun pulls 0.593008 cm/s^2 at 1 AU (%.6f)"), SunAtOneAU),
        FMath::Abs(SunAtOneAU - 0.593008) < 1.0e-5);

    // -- A default is no ground at all -------------------------------------------
    const FWorldReliefParams None;
    TestTrue(TEXT("default relief params are no ground: None, no peak, no radius"),
        None.Ground == EGround::None && None.PeakCm == 0.0 && None.RadiusCm == 0.0 && None.SeedOffset.IsZero());

    // -- Home, as the ini's priors make it ---------------------------------------
    const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();
    const FGalaxyGenerator Galaxy(Root, Priors);
    const TArray<FStarSystemStub> HomeSector = Galaxy.GenerateSector(FInt64Vector(-1, -1, 0));
    if (!TestTrue(TEXT("home's sector holds a system"), HomeSector.Num() > 0))
    {
        return false;
    }
    TestEqual(TEXT("slot 0 of sector (-1, -1, 0) is Baemsekai, the landing fixtures' home"),
        HomeSector[0].Name, FString(TEXT("Baemsekai")));
    const FStarSystem Home = FStarSystemGenerator::Generate(HomeSector[0], Priors);
    const FSkySystem Sky = FSkySystem::FromSystem(Home, {});
    for (int32 Index = 0; Index < Home.Planets.Num(); ++Index)
    {
        TestEqual(FString::Printf(TEXT("%s's face is GenSeed::SurfaceSeed's"), *Home.Planets[Index].Designation),
            Sky.Bodies[Index + 1].SurfaceSeed, GenSeed::SurfaceSeed(Home.Stub.Seed, static_cast<uint64>(Index)));
    }

    // -- PlanetSeed is the seed Generate draws each world from -------------------
    // A giant's day comes from its own stream under the planet's seed, so the
    // day drawn again from PlanetSeed is the day Generate drew only if the two
    // are one seed.
    bool bFoundGiant = false;
    for (int64 X = -10; X <= 10 && !bFoundGiant; ++X)
    {
        for (int64 Y = -10; Y <= 10 && !bFoundGiant; ++Y)
        {
            for (int64 Z = -3; Z <= 3 && !bFoundGiant; ++Z)
            {
                for (const FStarSystemStub& Stub : Galaxy.GenerateSector(FInt64Vector(X, Y, Z)))
                {
                    const FStarSystem System = FStarSystemGenerator::Generate(Stub, Priors);
                    for (int32 Index = 0; Index < System.Planets.Num() && !bFoundGiant; ++Index)
                    {
                        if (System.Planets[Index].Kind != EPlanetKind::GasGiant)
                        {
                            continue;
                        }
                        bFoundGiant = true;
                        TestEqual(FString::Printf(TEXT("%s's day, drawn again from PlanetSeed, is the day Generate drew"),
                                *System.Planets[Index].Designation),
                            FStarSystemGenerator::GenerateGiantDay(FStarSystemGenerator::PlanetSeed(Stub.Seed, Index)),
                            System.Planets[Index].DayHours);
                    }
                    if (bFoundGiant)
                    {
                        break;
                    }
                }
            }
        }
    }
    TestTrue(TEXT("a giant within ten sectors of home, to check PlanetSeed against"), bFoundGiant);
    return true;
}

#endif
```

- [ ] **Step 2: Build; expect it not to compile.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh
```

Expected: FAIL: `'Surface/WorldReliefParams.h' file not found` (then
`no member named 'GMEarthCm3PerS2'`, `'SurfaceSeed'`, `'PlanetSeed'`).

- [ ] **Step 3: Create `Source/DeepSpace/Surface/WorldReliefParams.h`.**

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * What a world's ground is made from, as data (landing decision 1): the
 * facts FWorldRelief is built from. Plain data and nothing else, so FSkyBody
 * can carry it before FWorldRelief exists, and so the sky, the flight and the
 * terrain are handed one set of numbers by one adapter
 * (FSkySystem::FromSystem). Pure: no UObject, no functions.
 */

/** Whether a body has ground a ship can set down on (landing decision 13):
 *  Solid for barren, ice and terrestrial worlds; None for oceans, giants and
 *  stars, over which the floor stays a sphere. */
enum class EGround : uint8
{
    None,
    Solid
};

struct FWorldReliefParams
{
    /** Where on the noise this world's face and ground are taken from: the
     *  xyz M_SkyBody's SurfaceSeed is handed (ShipSky::SurfaceSeed). Each is
     *  a multiple of 1/256 in [0, 256), so the float the GPU gets and this
     *  double are the same number exactly. */
    FVector3d SeedOffset = FVector3d::ZeroVector;

    /** The mean radius, cm: the datum every height is measured from. */
    double RadiusCm = 0.0;

    /** The highest this world's relief is drawn, cm (landing decision 3,
     *  FPlanet::ReliefKm). 0 for oceans, giants and stars. */
    double PeakCm = 0.0;

    /** How much of its craters the ground has kept, 0..1: the look's
     *  Cratering (SkySystem.cpp's LookOf). */
    double Cratering = 0.0;

    EGround Ground = EGround::None;
};
```

- [ ] **Step 4: The mass parameters.** In `Source/DeepSpace/Universe/UniverseUnits.h`, after
  `inline constexpr double SolarTemperatureK = 5772.0;` add:

```cpp

    /** IAU 2015 nominal solar mass parameter GM, cm^3/s^2 (1.3271244e20
     *  m^3/s^2). A definition, like the radii above: the product is known far
     *  better than G or the mass alone (landing decision 4). */
    inline constexpr double GMSunCm3PerS2 = 1.3271244e26;

    /** IAU 2015 nominal terrestrial mass parameter GM, cm^3/s^2 (3.986004e14
     *  m^3/s^2). Its ratio to the Sun's agrees with EarthMassSolar to 2.5e-5.
     *  Over CmPerEarthRadius squared it is 979.840 cm/s^2. */
    inline constexpr double GMEarthCm3PerS2 = 3.986004e20;
```

- [ ] **Step 5: The face's seed, named.** In `Source/DeepSpace/Universe/GenSeed.h`, after the
  `Derive` function add:

```cpp

    /** A world's surface seed: its face in M_SkyBody and, since landing, its
     *  ground (landing decision 2). By orbit index, which never renumbers, so
     *  a world keeps its face however many planets are added outside it. The
     *  derivation is the sky's since the face was first drawn; changing it
     *  moves every place anybody has been. */
    constexpr uint64 SurfaceSeed(uint64 SystemSeed, uint64 Index)
    {
        return Derive(SystemSeed, Label("sky.surface"), Index);
    }
```

  In `Source/DeepSpace/Sky/SkySystem.cpp`, delete the two lines

```cpp
    /** The face's purpose label: a child of the system's seed that no
     *  generator stream reads, so choosing a look changes no world. */
    constexpr uint64 SurfacePurpose = GenSeed::Label("sky.surface");
```

  and replace

```cpp
        Body.SurfaceSeed = GenSeed::Derive(System.Stub.Seed, SurfacePurpose, static_cast<uint64>(Index));
```

  with

```cpp
        Body.SurfaceSeed = GenSeed::SurfaceSeed(System.Stub.Seed, static_cast<uint64>(Index));
```

- [ ] **Step 6: `PlanetSeed` public.** In `StarSystemGenerator.h`, after the
  `GenerateGiantDay` declaration add:

```cpp

    /** The seed every quantity of orbit Index draws under, by label: mass,
     *  kind, phase, day, relief. Public so a quantity can be drawn again, and
     *  tested, without generating the system (landing decision 2). */
    static uint64 PlanetSeed(uint64 SystemSeed, int32 Index);
```

  In `StarSystemGenerator.cpp` replace line 201

```cpp
        const uint64 PlanetSeed = GenSeed::Derive(Seed, GenSeed::Label("planet"), Index);
```

  with

```cpp
        const uint64 PlanetSeed = FStarSystemGenerator::PlanetSeed(Seed, Index);
```

  and append at the end of the file:

```cpp

uint64 FStarSystemGenerator::PlanetSeed(uint64 SystemSeed, int32 Index)
{
    return GenSeed::Derive(SystemSeed, GenSeed::Label("planet"), static_cast<uint64>(Index));
}
```

  (The local `PlanetSeed` hides the function inside the loop from its declaration on;
  the qualified call in its own initializer names the function. `-Wshadow` does not
  warn on a variable hiding a function.)

- [ ] **Step 7: Build and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh && ./test.sh DeepSpace.Universe.GroundFacts
```

Expected: `passed: 1`. Then the untouched pins and the sky adapter:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./test.sh DeepSpace.Universe && ./test.sh DeepSpace.Sky.LocalSystem
```

Expected: every `DeepSpace.Universe.*` passes (`.Seed` and `.Stream` untouched), and `DeepSpace.Sky.LocalSystem` passes.

- [ ] **Step 8: Commit.**

```bash
git -C /home/matt/Development/deepspace/.worktrees/landing-a-procgen add Source/DeepSpace/Surface/WorldReliefParams.h Source/DeepSpace/Universe/UniverseUnits.h Source/DeepSpace/Universe/GenSeed.h Source/DeepSpace/Universe/StarSystemGenerator.h Source/DeepSpace/Universe/StarSystemGenerator.cpp Source/DeepSpace/Sky/SkySystem.cpp Source/DeepSpace/Tests/GroundFactsTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/landing-a-procgen commit -F - <<'MSG'
feat(universe): the ground's facts -- EGround and FWorldReliefParams, GM, the face's seed named

Surface/WorldReliefParams.h is the plain-data seam the relief, flight and
terrain tracks agree on first (landing spec, Parallel tracks). The IAU 2015
mass parameters join UniverseUnits; GenSeed::SurfaceSeed names the face's
derivation, unchanged, so the ground can never re-derive it by hand; and
FStarSystemGenerator::PlanetSeed is public for the relief draw.

DeepSpace.Universe.GroundFacts pins all four.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 9: Prove the test can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && Tools/mutate.sh Source/DeepSpace/Universe/StarSystemGenerator.cpp 'const uint64 PlanetSeed = FStarSystemGenerator::PlanetSeed(Seed, Index);' 'const uint64 PlanetSeed = FStarSystemGenerator::PlanetSeed(Seed, Index + 1);' DeepSpace.Universe.GroundFacts
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && Tools/mutate.sh Source/DeepSpace/Universe/UniverseUnits.h 'GMEarthCm3PerS2 = 3.986004e20;' 'GMEarthCm3PerS2 = 3.996004e20;' DeepSpace.Universe.GroundFacts
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh
```

Expected: `KILLED` twice, then `Result: Succeeded`.

---

## Task 3 (P2): The relief prior and `FPlanet::ReliefKm`

**Files:**
- Modify: `Source/DeepSpace/Universe/StarSystem.h` (`FPlanet`, after `DayHours`, line ~105)
- Modify: `Source/DeepSpace/Universe/GenPriors.h` (a section before `// -- the galaxy --`; `DS_GEN_PRIORS`; `GenGuarantees`)
- Modify: `Source/DeepSpace/Universe/GenPriors.cpp` (`Refusals`, before `return Out;`)
- Modify: `Source/DeepSpace/Universe/ProcGenPriorsConfig.h` (`UPROPERTY(Config)` block, before `SystemsPerSector`)
- Modify: `Source/DeepSpace/Universe/StarSystemGenerator.h` / `.cpp` (`DrawRelief`, `GenerateRelief`, one line in `GenerateWithPlanetCount`)
- Modify: `Config/DefaultGame.ini` (`[/Script/DeepSpace.ProcGenPriorsConfig]`, before the galaxy's line)
- Modify: `CLAUDE.md` (*The universe*, the paragraph beginning **The priors are data.**)
- Test: create `Source/DeepSpace/Tests/UniverseReliefTest.cpp` (`DeepSpace.Universe.Relief`); `DeepSpace.Universe.Priors` covers the new ini lines unchanged (it walks `DS_GEN_PRIORS`).

**Interfaces:**
- Consumes: `FStarSystemGenerator::PlanetSeed` (P1), `FGenStream::Beta`, `GenPriorDomain::MinBetaShape`.
- Produces: `FPlanet::ReliefKm`, `FPlanet::SurfaceGravityEarth()`, the seven priors, `GenGuarantees::MaxReliefKm`, `GenGuarantees::MaxReliefRadiusFraction`, `static double FStarSystemGenerator::GenerateRelief(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors)`.

- [ ] **Step 1: Write the failing test.** Create `Source/DeepSpace/Tests/UniverseReliefTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/GenPriors.h"
#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/StarSystemGenerator.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUniverseReliefTest,
    "DeepSpace.Universe.Relief",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace UniverseReliefTestLocal
{
    constexpr uint64 Root = 20260925;
    constexpr int32 SystemCount = 3000;

    bool IsSolid(EPlanetKind Kind)
    {
        return Kind == EPlanetKind::Barren || Kind == EPlanetKind::Ice || Kind == EPlanetKind::Terrestrial;
    }

    /** Decision 3's ceiling, written out again rather than called, so the
     *  test and the generator are two readings of the spec. */
    double Ceiling(const FPlanet& Planet, const FGenPriors& P)
    {
        const double Strength = Planet.Kind == EPlanetKind::Ice ? P.ReliefStrengthIceKm
            : Planet.Kind == EPlanetKind::Terrestrial ? P.ReliefStrengthRockKm * P.ReliefTerrestrialFactor
            : P.ReliefStrengthRockKm;
        const double RadiusKm = Planet.RadiusEarth * UniverseUnits::CmPerEarthRadius / UniverseUnits::CmPerKm;
        return FMath::Min3(Strength / Planet.SurfaceGravityEarth(), GenGuarantees::MaxReliefKm,
            GenGuarantees::MaxReliefRadiusFraction * RadiusKm);
    }

    TArray<FStarSystem> Systems(const FGenPriors& Priors, int32 Count)
    {
        const FGalaxyGenerator Galaxy(Root, Priors);
        TArray<FStarSystem> Out;
        for (int64 X = -16; X <= 16 && Out.Num() < Count; ++X)
        {
            for (int64 Y = -16; Y <= 16 && Out.Num() < Count; ++Y)
            {
                for (int64 Z = -4; Z <= 4 && Out.Num() < Count; ++Z)
                {
                    for (const FStarSystemStub& Stub : Galaxy.GenerateSector(FInt64Vector(X, Y, Z)))
                    {
                        if (Out.Num() < Count)
                        {
                            Out.Add(FStarSystemGenerator::Generate(Stub, Priors));
                        }
                    }
                }
            }
        }
        return Out;
    }

    FPlanet Made(EPlanetKind Kind, double MassEarth, double RadiusEarth)
    {
        FPlanet Planet;
        Planet.Kind = Kind;
        Planet.MassEarth = MassEarth;
        Planet.RadiusEarth = RadiusEarth;
        return Planet;
    }

    /** Everything about a world but its relief. */
    bool SameButRelief(const FPlanet& A, const FPlanet& B)
    {
        return A.Designation == B.Designation && A.GivenName == B.GivenName && A.Kind == B.Kind
            && A.MassEarth == B.MassEarth && A.RadiusEarth == B.RadiusEarth && A.EquilibriumK == B.EquilibriumK
            && A.SemiMajorAxisAU == B.SemiMajorAxisAU && A.PhaseRad == B.PhaseRad
            && A.Population == B.Population && A.DayHours == B.DayHours;
    }
}

bool FUniverseReliefTest::RunTest(const FString& Parameters)
{
    using namespace UniverseReliefTestLocal;
    const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();

    // -- The law, on made worlds: one seed is one Beta draw, so only the ceiling moves
    const uint64 Seed = FStarSystemGenerator::PlanetSeed(12345, 2);
    const double AtOneG = FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Barren, 1.0, 1.0), Priors);
    const double AtTwoG = FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Barren, 2.0, 1.0), Priors);
    TestTrue(FString::Printf(TEXT("a 1 g barren world has relief under its 1 g ceiling (%.4f km)"), AtOneG),
        AtOneG > 0.0 && AtOneG <= Priors.ReliefStrengthRockKm);
    FGenStream ReliefStream(GenSeed::Derive(Seed, GenSeed::Label("relief")));
    TestTrue(TEXT("the share is the first Beta of the planet's own \"relief\" stream (landing decision 2)"),
        FMath::IsNearlyEqual(AtOneG, Priors.ReliefStrengthRockKm * ReliefStream.Beta(Priors.ReliefBetaA, Priors.ReliefBetaB), 1.0e-12 * AtOneG));
    TestTrue(FString::Printf(TEXT("twice the gravity holds up half the mountain (%.4f, then %.4f km)"), AtOneG, AtTwoG),
        FMath::IsNearlyEqual(AtTwoG, 0.5 * AtOneG, 1.0e-12 * AtOneG));
    const double Light = FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Barren, 0.1, 1.0), Priors);
    TestTrue(FString::Printf(TEXT("a light world's ceiling is the guarantee's 10 km, not 90 (%.4f km)"), Light),
        FMath::IsNearlyEqual(Light, AtOneG * GenGuarantees::MaxReliefKm / Priors.ReliefStrengthRockKm, 1.0e-12 * Light));

    FGenPriors Alike = Priors;
    Alike.ReliefTerrestrialBetaA = Priors.ReliefBetaA;
    Alike.ReliefTerrestrialBetaB = Priors.ReliefBetaB;
    const double Rock = FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Barren, 1.0, 1.0), Alike);
    const double Land = FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Terrestrial, 1.0, 1.0), Alike);
    TestTrue(TEXT("under the same Beta, weather takes the terrestrial factor off the ceiling"),
        FMath::IsNearlyEqual(Land, Rock * Priors.ReliefTerrestrialFactor, 1.0e-12 * Rock));
    TestEqual(TEXT("an ocean has no ground"), FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Ocean, 1.0, 1.0), Priors), 0.0);
    TestEqual(TEXT("nor has a giant"), FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::GasGiant, 300.0, 11.0), Priors), 0.0);

    // -- Every world of 3,000 systems: within its ceiling and the guarantee ----
    const TArray<FStarSystem> Found = Systems(Priors, SystemCount);
    int32 Solid = 0;
    int32 Outside = 0;
    int32 Dry = 0;
    int32 Old = 0;
    int32 Weathered = 0;
    double OldShare = 0.0;
    double WeatheredShare = 0.0;
    for (const FStarSystem& System : Found)
    {
        for (const FPlanet& Planet : System.Planets)
        {
            if (!IsSolid(Planet.Kind))
            {
                Dry += Planet.ReliefKm == 0.0 ? 0 : 1;
                continue;
            }
            ++Solid;
            const double Cap = Ceiling(Planet, Priors);
            const bool bInside = Planet.ReliefKm > 0.0 && Planet.ReliefKm <= Cap * (1.0 + 1.0e-12)
                && Planet.ReliefKm <= GenGuarantees::MaxReliefKm;
            Outside += bInside ? 0 : 1;
            if (Planet.Kind == EPlanetKind::Terrestrial)
            {
                WeatheredShare += Planet.ReliefKm / Cap;
                ++Weathered;
            }
            else
            {
                OldShare += Planet.ReliefKm / Cap;
                ++Old;
            }
        }
    }
    TestEqual(TEXT("no ocean or giant has relief"), Dry, 0);
    TestEqual(FString::Printf(TEXT("every one of %d solid worlds lies in (0, its ceiling], never above 10 km"), Solid), Outside, 0);
    if (TestTrue(FString::Printf(TEXT("enough barren and ice worlds to read the draw (%d)"), Old), Old >= 1000))
    {
        const double Mean = Priors.ReliefBetaA / (Priors.ReliefBetaA + Priors.ReliefBetaB);
        TestTrue(FString::Printf(TEXT("old crust reaches %.3f of its ceiling on average, Beta's %.3f"), OldShare / Old, Mean),
            FMath::Abs(OldShare / Old - Mean) < 0.02);
    }
    if (TestTrue(FString::Printf(TEXT("enough terrestrial worlds to read the draw (%d)"), Weathered), Weathered >= 50))
    {
        const double Mean = Priors.ReliefTerrestrialBetaA / (Priors.ReliefTerrestrialBetaA + Priors.ReliefTerrestrialBetaB);
        TestTrue(FString::Printf(TEXT("weathered crust reaches %.3f of its ceiling on average, Beta's %.3f"), WeatheredShare / Weathered, Mean),
            FMath::Abs(WeatheredShare / Weathered - Mean) < 0.1);
    }

    // -- The relief stream moves nothing else (landing decision 2) --------------
    FGenPriors Other = Priors;
    Other.ReliefStrengthRockKm = 3.0;
    Other.ReliefStrengthIceKm = 4.0;
    Other.ReliefTerrestrialFactor = 0.5;
    Other.ReliefBetaA = 1.5;
    Other.ReliefBetaB = 3.0;
    Other.ReliefTerrestrialBetaA = 2.0;
    Other.ReliefTerrestrialBetaB = 5.0;
    const TArray<FStarSystem> Again = Systems(Other, 300);
    int32 Moved = 0;
    int32 Reliefs = 0;
    for (int32 S = 0; S < Again.Num(); ++S)
    {
        if (Again[S].Planets.Num() != Found[S].Planets.Num())
        {
            ++Moved;
            continue;
        }
        for (int32 P = 0; P < Again[S].Planets.Num(); ++P)
        {
            Moved += SameButRelief(Again[S].Planets[P], Found[S].Planets[P]) ? 0 : 1;
            Reliefs += Again[S].Planets[P].ReliefKm != Found[S].Planets[P].ReliefKm ? 1 : 0;
        }
    }
    TestEqual(TEXT("retuning every relief prior moves no other number of any world"), Moved, 0);
    TestTrue(TEXT("and does move the relief"), Reliefs > 0);

    // -- The domain: a prior its sampler cannot take is refused, by name -------
    const auto Refuses = [&Priors](TFunctionRef<void(FGenPriors&)> Edit, const TCHAR* Name)
    {
        FGenPriors Wrong = Priors;
        Edit(Wrong);
        return GenPriorDomain::Refusals(Wrong).ContainsByPredicate([Name](const FString& Line) { return Line.StartsWith(Name); });
    };
    TestTrue(TEXT("a Beta shape under a tenth is refused"), Refuses([](FGenPriors& P) { P.ReliefBetaA = 0.05; }, TEXT("ReliefBetaA=")));
    TestTrue(TEXT("so is the terrestrial one"), Refuses([](FGenPriors& P) { P.ReliefTerrestrialBetaB = 0.0; }, TEXT("ReliefTerrestrialBetaB=")));
    TestTrue(TEXT("a strength of zero is refused"), Refuses([](FGenPriors& P) { P.ReliefStrengthRockKm = 0.0; }, TEXT("ReliefStrengthRockKm=")));
    TestTrue(TEXT("so is ice's"), Refuses([](FGenPriors& P) { P.ReliefStrengthIceKm = -1.0; }, TEXT("ReliefStrengthIceKm=")));
    TestTrue(TEXT("and a weathering factor of zero"), Refuses([](FGenPriors& P) { P.ReliefTerrestrialFactor = 0.0; }, TEXT("ReliefTerrestrialFactor=")));
    TestTrue(TEXT("the ini's priors are taken"), GenPriorDomain::Refusals(Priors).IsEmpty());

    // -- Home's fixtures (the spec's *Fixture worlds*) -------------------------
    const FGalaxyGenerator Galaxy(Root, Priors);
    const FStarSystem Home = FStarSystemGenerator::Generate(Galaxy.GenerateSector(FInt64Vector(-1, -1, 0))[0], Priors);
    TestEqual(TEXT("home is Baemsekai"), Home.Stub.Name, FString(TEXT("Baemsekai")));
    const struct { int32 Index; EPlanetKind Kind; } Fixtures[] = {
        { 2, EPlanetKind::Barren }, { 3, EPlanetKind::Barren }, { 4, EPlanetKind::Terrestrial } };
    for (const auto& Fixture : Fixtures)
    {
        if (!TestTrue(TEXT("home has the fixture's orbit"), Home.Planets.IsValidIndex(Fixture.Index)))
        {
            continue;
        }
        const FPlanet& World = Home.Planets[Fixture.Index];
        TestTrue(FString::Printf(TEXT("%s is the kind the fixtures say"), *World.Designation), World.Kind == Fixture.Kind);
        TestTrue(FString::Printf(TEXT("%s has relief (%.3f km at %.2f g)"), *World.Designation, World.ReliefKm, World.SurfaceGravityEarth()),
            World.ReliefKm > 0.0);
        AddInfo(FString::Printf(TEXT("%s: %.3f km of relief at %.2f g"), *World.Designation, World.ReliefKm, World.SurfaceGravityEarth()));
    }
    return true;
}

#endif
```

- [ ] **Step 2: Build; expect it not to compile.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh
```

Expected: FAIL: `no member named 'ReliefStrengthRockKm' in 'FGenPriors'`, `no member named 'GenerateRelief'`, `'ReliefKm'`.

- [ ] **Step 3: `FPlanet`.** In `StarSystem.h`, after `double DayHours = 0.0;` add:

```cpp

    /** The highest this world's relief rises, km (landing decision 3): a
     *  Beta proportion of what its crust can hold up, 1/g. 0 for oceans and
     *  giants, which have no ground. Slice (a) draws it; nothing draws the
     *  look from it yet. */
    double ReliefKm = 0.0;

    /** Surface gravity in Earth g: mass over radius squared, both in Earth
     *  units. Derived, never drawn; 0 for a radius of 0. */
    double SurfaceGravityEarth() const
    {
        return RadiusEarth > 0.0 ? MassEarth / (RadiusEarth * RadiusEarth) : 0.0;
    }
```

- [ ] **Step 4: The priors, the list, the guarantee.** In `GenPriors.h`, immediately before
  `    // -- the galaxy ---` add:

```cpp
    // -- the ground (landing decision 3) -----------------------------------------

    /** What a crust can hold up goes as 1/g: Earth's 8.8 km at 1 g, Mars's
     *  22 km at 0.38 g. The ceiling of a world's relief is Strength / g, km,
     *  g in Earth g; weather takes a factor off a terrestrial world's. */
    double ReliefStrengthRockKm = 9.0;
    double ReliefStrengthIceKm = 9.0;
    double ReliefTerrestrialFactor = 0.7;

    /** How much of its ceiling a world reaches, a bounded proportion, which
     *  is Beta's whole job. Barren and ice skew high -- old, unrelaxed crust,
     *  Beta(5, 2), mean 0.71 -- and terrestrial lower, Beta(3, 2), mean 0.6. */
    double ReliefBetaA = 5.0;
    double ReliefBetaB = 2.0;
    double ReliefTerrestrialBetaA = 3.0;
    double ReliefTerrestrialBetaB = 2.0;

```

  In the `DS_GEN_PRIORS` list replace

```cpp
    X(PopulationMedian) X(PopulationSigma) X(PopulationMin) X(PopulationMax) \
    X(SystemsPerSector)
```

  with

```cpp
    X(PopulationMedian) X(PopulationSigma) X(PopulationMin) X(PopulationMax) \
    X(ReliefStrengthRockKm) X(ReliefStrengthIceKm) X(ReliefTerrestrialFactor) \
    X(ReliefBetaA) X(ReliefBetaB) X(ReliefTerrestrialBetaA) X(ReliefTerrestrialBetaB) \
    X(SystemsPerSector)
```

  In `namespace GenGuarantees`, after `IceBelowK`, add:

```cpp

    /** No world's relief rises higher than this, km, whatever the ini says
     *  (landing ruling 6), nor above this fraction of its radius. The drive's
     *  floor stands 10 km above the highest peak (landing decision 10), so
     *  this is what that floor's height rests on. */
    inline constexpr double MaxReliefKm = 10.0;
    inline constexpr double MaxReliefRadiusFraction = 0.005;
```

- [ ] **Step 5: The domain.** In `GenPriors.cpp`, `GenPriorDomain::Refusals`, before `return Out;` add:

```cpp

    const TCHAR* const Strength = TEXT("a crust's strength, km at 1 g, so above zero");
    Within(Out, TEXT("ReliefStrengthRockKm"), P.ReliefStrengthRockKm, 0.0, true, Unbounded, Strength);
    Within(Out, TEXT("ReliefStrengthIceKm"), P.ReliefStrengthIceKm, 0.0, true, Unbounded, Strength);
    Within(Out, TEXT("ReliefTerrestrialFactor"), P.ReliefTerrestrialFactor, 0.0, true, Unbounded,
        TEXT("what weather leaves of a crust's strength, so above zero"));
    Within(Out, TEXT("ReliefBetaA"), P.ReliefBetaA, GenPriorDomain::MinBetaShape, false, Unbounded, Shape);
    Within(Out, TEXT("ReliefBetaB"), P.ReliefBetaB, GenPriorDomain::MinBetaShape, false, Unbounded, Shape);
    Within(Out, TEXT("ReliefTerrestrialBetaA"), P.ReliefTerrestrialBetaA, GenPriorDomain::MinBetaShape, false, Unbounded, Shape);
    Within(Out, TEXT("ReliefTerrestrialBetaB"), P.ReliefTerrestrialBetaB, GenPriorDomain::MinBetaShape, false, Unbounded, Shape);
```

  (`Shape` is the existing local `"a Beta shape, which below a tenth draws stars of no mass"`;
  the words stay true of any Beta.)

- [ ] **Step 6: The ini mirror.** In `ProcGenPriorsConfig.h`, before
  `    UPROPERTY(Config) double SystemsPerSector;` add:

```cpp
    UPROPERTY(Config) double ReliefStrengthRockKm;
    UPROPERTY(Config) double ReliefStrengthIceKm;
    UPROPERTY(Config) double ReliefTerrestrialFactor;
    UPROPERTY(Config) double ReliefBetaA;
    UPROPERTY(Config) double ReliefBetaB;
    UPROPERTY(Config) double ReliefTerrestrialBetaA;
    UPROPERTY(Config) double ReliefTerrestrialBetaB;

```

  In `Config/DefaultGame.ini`, in `[/Script/DeepSpace.ProcGenPriorsConfig]`, before the
  comment line that introduces `SystemsPerSector`, add:

```ini
; The ground (landing decision 3): a crust holds up Strength / g km of relief,
; g in Earth g, weather taking a factor off a terrestrial world's; a world
; reaches a Beta(A, B) share of that. The 10 km cap is code (MaxReliefKm).
ReliefStrengthRockKm=9.0
ReliefStrengthIceKm=9.0
ReliefTerrestrialFactor=0.7
ReliefBetaA=5.0
ReliefBetaB=2.0
ReliefTerrestrialBetaA=3.0
ReliefTerrestrialBetaB=2.0

```

- [ ] **Step 7: The draw.** In `StarSystemGenerator.cpp`, in the anonymous namespace after
  `DrawGiantDay`, add:

```cpp

    /**
     * A solid world's highest relief, km, from its own `relief` stream
     * (landing decision 3). What a crust holds up goes as 1/g, so the ceiling
     * is derived -- a strength over the surface gravity in Earth g, weathered
     * lower on a terrestrial world -- and capped by the guarantees. Only how
     * much of it a world reaches is drawn, and a bounded proportion is Beta's
     * whole job. Oceans and giants have no ground: 0, and nothing drawn.
     */
    double DrawRelief(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors)
    {
        if (Planet.Kind == EPlanetKind::Ocean || Planet.Kind == EPlanetKind::GasGiant)
        {
            return 0.0;
        }
        const bool bWeathered = Planet.Kind == EPlanetKind::Terrestrial;
        const double Strength = Planet.Kind == EPlanetKind::Ice ? Priors.ReliefStrengthIceKm
            : bWeathered ? Priors.ReliefStrengthRockKm * Priors.ReliefTerrestrialFactor
            : Priors.ReliefStrengthRockKm;
        const double RadiusKm = Planet.RadiusEarth * UniverseUnits::CmPerEarthRadius / UniverseUnits::CmPerKm;
        const double Ceiling = FMath::Min3(Strength / Planet.SurfaceGravityEarth(), GenGuarantees::MaxReliefKm,
            GenGuarantees::MaxReliefRadiusFraction * RadiusKm);
        FGenStream Stream(GenSeed::Derive(PlanetSeed, GenSeed::Label("relief")));
        const double Share = bWeathered ? Stream.Beta(Priors.ReliefTerrestrialBetaA, Priors.ReliefTerrestrialBetaB)
                                        : Stream.Beta(Priors.ReliefBetaA, Priors.ReliefBetaB);
        return Ceiling * Share;
    }
```

  In `GenerateWithPlanetCount`, after the line
  `        Planet.RadiusEarth = MassEarth < GenGuarantees::GasGiantMinEarthMasses ? std::pow(MassEarth, 0.28) : 11.0;`
  add:

```cpp

        // Its own stream too: a world's relief moves nothing else about it.
        Planet.ReliefKm = DrawRelief(PlanetSeed, Planet, Priors);
```

  At the end of the file add:

```cpp

double FStarSystemGenerator::GenerateRelief(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors)
{
    return DrawRelief(PlanetSeed, Planet, Priors);
}
```

  In `StarSystemGenerator.h`, after `PlanetSeed`'s declaration add:

```cpp

    /** A world's relief, km, as Generate draws it for the planet whose seed
     *  this is, from Planet's Kind, MassEarth and RadiusEarth. Public so the
     *  law can be tested on made worlds (landing decision 3). */
    static double GenerateRelief(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors);
```

- [ ] **Step 8: CLAUDE.md.** In *The universe*, the paragraph beginning **The priors are
  data.**, replace `(the Hill floor, the mass cap, the kind thresholds) are deliberately not in the
  ini, so no ini edit can break an invariant.` with:

```text
(the Hill floor, the mass cap, the kind thresholds) are deliberately not in the
ini, so no ini edit can break an invariant. A world's relief is one of the
priors' draws (landing decision 3): `FPlanet::ReliefKm` is a Beta share
(`ReliefBeta*`) of a 1/g ceiling (`ReliefStrength*Km`, `ReliefTerrestrialFactor`),
and `GenGuarantees::MaxReliefKm` (10 km) is what no line can raise.
```

- [ ] **Step 9: Build and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh && ./test.sh DeepSpace.Universe.Relief && ./test.sh DeepSpace.Universe
```

Expected: `DeepSpace.Universe.Relief` passes; every `DeepSpace.Universe.*` passes,
`.Priors` included (the seven new lines are mirrored and present), `.Seed` and `.Stream`
untouched. The log's `AddInfo` lines give Baemsekai III, IV and V's relief.

- [ ] **Step 10: Full suite.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./test.sh
```

Expected: `passed: N`, no `FAILED`, no `LOG NOT CLEAN`. (`DeepSpace.Universe.Corpus` is
unchanged here; its columns come in P3.)

- [ ] **Step 11: Commit.**

```bash
git -C /home/matt/Development/deepspace/.worktrees/landing-a-procgen add Source/DeepSpace/Universe/StarSystem.h Source/DeepSpace/Universe/GenPriors.h Source/DeepSpace/Universe/GenPriors.cpp Source/DeepSpace/Universe/ProcGenPriorsConfig.h Source/DeepSpace/Universe/StarSystemGenerator.h Source/DeepSpace/Universe/StarSystemGenerator.cpp Config/DefaultGame.ini CLAUDE.md Source/DeepSpace/Tests/UniverseReliefTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/landing-a-procgen commit -F - <<'MSG'
feat(universe): every solid world draws its relief -- a Beta share of a 1/g ceiling, capped at 10 km

FPlanet::ReliefKm, from its own "relief" stream under the planet's seed, so
no other draw of any world moves (DeepSpace.Universe.Relief retunes every
relief prior and checks). The ceiling is a crust strength over g (9 km at
1 g for rock and ice, x0.7 weathered for terrestrial); the share is Beta(5, 2)
for old crust and Beta(3, 2) for weathered. GenGuarantees::MaxReliefKm and a
0.5% of radius cap are code, not ini. Oceans and giants: 0.

Seven new priors in DefaultGame.ini, domain-checked. Nothing draws the look
from the relief yet (slice (b)).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 12: Prove the test can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && Tools/mutate.sh Source/DeepSpace/Universe/StarSystemGenerator.cpp 'FMath::Min3(Strength / Planet.SurfaceGravityEarth(),' 'FMath::Min3(Strength * Planet.SurfaceGravityEarth(),' DeepSpace.Universe.Relief
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && Tools/mutate.sh Source/DeepSpace/Universe/StarSystemGenerator.cpp 'FGenStream Stream(GenSeed::Derive(PlanetSeed, GenSeed::Label("relief")));' 'FGenStream Stream(GenSeed::Derive(PlanetSeed, GenSeed::Label("mass")));' DeepSpace.Universe.Relief
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh
```

Expected: `KILLED` twice (the second by the check that the share is the `relief` stream's
own first Beta), then `Result: Succeeded`.

---

## Task 4 (P3): The corpus reads relief

**Files:**
- Modify: `Tools/procgen_corpus_contract.json` (`columns`, after `"population"`)
- Modify: `Source/DeepSpace/Tests/ProcGenCorpusTest.cpp` (`WrittenColumns` :55-60, `PlanetColumns` :141-152, `ExpectedRow` :159-190, `NoPlanetColumns` :203-206)
- Modify: `Tools/procgen_corpus.py` (`PLANET_COLUMNS` :37-38, `report` before `places_or_rolls`)
- Modify: `Tools/procgen_corpus_sample.tsv` (two cells per row)
- Test: `Tools/test_procgen_corpus.py` (new test), `DeepSpace.Universe.Corpus`

**Interfaces:**
- Consumes: `FPlanet::ReliefKm`, `FPlanet::SurfaceGravityEarth()` (P2).
- Produces: corpus columns `surface_gravity_g`, `relief_km`; the report's *Relief* block.

- [ ] **Step 1: Write the failing Python test.** Append to `Tools/test_procgen_corpus.py`, before `def main():`:

```python
def test_report_gives_relief_by_kind():
    text = C.report(rows())
    assert "Relief, km, of solid worlds (median, highest, count by kind)" in text, text
    # Barren: Alpha I 7.5, Alpha II 6.0, Delta I 4.2 -> median 6.00, highest 7.50.
    assert "  barren       median   6.00  highest   7.50  n=3" in text, text
    assert "  ice          none" in text, text
    assert "  terrestrial  median   3.80  highest   3.80  n=1" in text, text


def test_gravity_and_relief_are_typed():
    alpha1 = [r for r in rows() if r["designation"] == "Alpha I"][0]
    assert near(alpha1["surface_gravity_g"], 0.78125)
    assert near(alpha1["relief_km"], 7.5)
    beta = [r for r in rows() if r["system"] == "Beta"][0]
    assert beta["relief_km"] is None and beta["surface_gravity_g"] is None
```

  (`main()` runs every `test_` function it finds; nothing to register.)

- [ ] **Step 2: Run; expect FAIL.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && python3 Tools/test_procgen_corpus.py
```

Expected: FAIL: `KeyError: 'surface_gravity_g'` or the relief assertion.

- [ ] **Step 3: The contract and the sample.** In `procgen_corpus_contract.json` replace
  `"semi_major_axis_au", "mass_earth", "radius_earth", "equilibrium_k", "population"` with
  `"semi_major_axis_au", "mass_earth", "radius_earth", "equilibrium_k", "population", "surface_gravity_g", "relief_km"`.
  Replace `Tools/procgen_corpus_sample.tsv` with (tabs between cells):

```text
system	sector_x	sector_y	sector_z	slot	distance_ly	star_class	star_mass_solar	star_luminosity_solar	star_temperature_k	habitable_inner_au	habitable_outer_au	frost_line_au	planet_count	planet	designation	given_name	kind	semi_major_axis_au	mass_earth	radius_earth	equilibrium_k	population	surface_gravity_g	relief_km
Alpha	0	0	0	0	0	M	0.2	0.01	3100	0.095	0.137	0.27	3	0	Alpha I		barren	0.01	0.5	0.8	880	0	0.78125	7.5
Alpha	0	0	0	0	0	M	0.2	0.01	3100	0.095	0.137	0.27	3	1	Alpha II		barren	0.02	1	1	620	0	1	6
Alpha	0	0	0	0	0	M	0.2	0.01	3100	0.095	0.137	0.27	3	2	Alpha III	Hollin	terrestrial	0.04	1	1	300	1000	1	3.8
Beta	1	0	0	0	5.5	K	0.7	0.2	4400	0.42	0.61	1.2	0	-1										
Gamma	0	1	0	1	7.25	G	1	1	5800	0.95	1.37	2.7	2	0	Gamma I		ocean	1	1	1	280	0	1	0
Gamma	0	1	0	1	7.25	G	1	1	5800	0.95	1.37	2.7	2	1	Gamma II		gas giant	4	100	11	140	0	0.826446	0
Delta	-1	0	0	0	9	M	0.3	0.02	3300	0.13	0.19	0.38	1	0	Delta I		barren	0.05	2	1.2	310	0	1.38889	4.2
```

  (Beta's row has 10 empty planet cells after `-1`: the line ends in ten tabs.)

- [ ] **Step 4: The reader.** In `procgen_corpus.py` replace

```python
PLANET_COLUMNS = ("designation", "given_name", "kind", "semi_major_axis_au", "mass_earth",
                  "radius_earth", "equilibrium_k", "population")
```

  with

```python
PLANET_COLUMNS = ("designation", "given_name", "kind", "semi_major_axis_au", "mass_earth",
                  "radius_earth", "equilibrium_k", "population", "surface_gravity_g", "relief_km")
SOLID = ("barren", "ice", "terrestrial")
```

  and in `report()`, immediately before `    out += places_or_rolls(found)`, add:

```python
    out.append("Relief, km, of solid worlds (median, highest, count by kind)")
    for kind in SOLID:
        heights = sorted(p["relief_km"] for p in planets if p["kind"] == kind)
        if heights:
            out.append("  %-12s median %6.2f  highest %6.2f  n=%d" % (
                kind, heights[len(heights) // 2], heights[-1], len(heights)))
        else:
            out.append("  %-12s none" % kind)
    gravity = [p["surface_gravity_g"] for p in planets if p["kind"] in SOLID]
    if gravity:
        out += _render_histogram("Surface gravity of solid worlds, g (share of solid worlds)",
                                 histogram(gravity, 8), len(gravity))
    out.append("")

```

- [ ] **Step 5: Run the Python; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && python3 Tools/test_procgen_corpus.py
```

- [ ] **Step 6: The C++ writer.** In `ProcGenCorpusTest.cpp`:
  - `WrittenColumns`: replace `TEXT("equilibrium_k"), TEXT("population")};` with
    `TEXT("equilibrium_k"), TEXT("population"), TEXT("surface_gravity_g"), TEXT("relief_km")};`
  - `PlanetColumns`: replace `            Num(Planet.Population, NonFinite)}, TEXT("\t"));` with

```cpp
            Num(Planet.Population, NonFinite),
            Num(Planet.SurfaceGravityEarth(), NonFinite),
            Num(Planet.ReliefKm, NonFinite)}, TEXT("\t"));
```

  - `ExpectedRow`: replace `            {TEXT("population"), bAny ? G(Planet.Population) : FString()}};` with

```cpp
            {TEXT("population"), bAny ? G(Planet.Population) : FString()},
            {TEXT("surface_gravity_g"), bAny ? G(Planet.SurfaceGravityEarth()) : FString()},
            {TEXT("relief_km"), bAny ? G(Planet.ReliefKm) : FString()}};
```

  - `NoPlanetColumns`: replace `return TEXT("-1\t\t\t\t\t\t\t\t");` with `return TEXT("-1\t\t\t\t\t\t\t\t\t\t");`

- [ ] **Step 7: Run the corpus test and the reader on the real corpus; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh && ./test.sh DeepSpace.Universe.Corpus && python3 Tools/procgen_corpus.py | sed -n '/Relief, km/,/^$/p'
```

Expected: `passed: 1`; the relief block for 10,000 systems prints.

- [ ] **Step 8: Full suite, commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./test.sh
git -C /home/matt/Development/deepspace/.worktrees/landing-a-procgen add Tools/procgen_corpus_contract.json Tools/procgen_corpus.py Tools/procgen_corpus_sample.tsv Tools/test_procgen_corpus.py Source/DeepSpace/Tests/ProcGenCorpusTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/landing-a-procgen commit -F - <<'MSG'
feat(corpus): surface gravity and relief, so the priors can be read across 10,000 worlds

Two columns, surface_gravity_g and relief_km, on all three sides: the
contract, DeepSpace.Universe.Corpus's writer, and procgen_corpus.py, which
now reports relief by kind and a histogram of solid worlds' gravity.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 9: Prove the corpus test can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && Tools/mutate.sh Source/DeepSpace/Tests/ProcGenCorpusTest.cpp '            Num(Planet.ReliefKm, NonFinite)}, TEXT("\t"));' '            Num(Planet.MassEarth, NonFinite)}, TEXT("\t"));' DeepSpace.Universe.Corpus
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh
```

Expected: `KILLED` (the row read back by name no longer holds the relief).

---

## Task 5 (P4): `FSkyBody` carries the ground: `GravParam`, `Ground`, `Relief`

**Files:**
- Modify: `Source/DeepSpace/Sky/SkySystem.h` (`FSkyBody`, after `BeltPairs` :72; `SkyLook`, after `BeltPairs` decl :92; include)
- Modify: `Source/DeepSpace/Sky/SkySystem.cpp` (`FromSystem`; new `SkyLook::SurfaceOffset`; a `GroundOf` helper)
- Test: create `Source/DeepSpace/Tests/SkyGroundTest.cpp` (`DeepSpace.Sky.WhatCanBeLanded`)

**Interfaces:**
- Consumes: `FWorldReliefParams`, `EGround`, `UniverseUnits::GM*`, `GenSeed::SurfaceSeed` (P1); `FPlanet::ReliefKm` (P2); `SkyMaterial::SurfaceOffsetSpan` (existing); `ShipSky::SurfaceSeed` (existing, test only).
- Produces: `double FSkyBody::GravParam`, `EGround FSkyBody::Ground`, `FWorldReliefParams FSkyBody::Relief`, `FVector3d SkyLook::SurfaceOffset(uint64 SurfaceSeed)`.

- [ ] **Step 1: Write the failing test.** Create `Source/DeepSpace/Tests/SkyGroundTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Sky/ShipSky.h"
#include "Sky/SkySystem.h"
#include "Surface/WorldReliefParams.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyGroundTest,
    "DeepSpace.Sky.WhatCanBeLanded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SkyGroundTestLocal
{
    /** A hand-written system with one world of each kind: data, not a
     *  generator (plan conflict 4), so this pins FromSystem alone. */
    FStarSystem OneOfEach()
    {
        FStarSystem System;
        System.Stub.Name = TEXT("Kessa");
        System.Stub.Seed = 0x1234ABCDull;
        System.Stub.Position = FUniversePosition(FInt64Vector(0, 0, 0), FVector(1.0e12, 0.0, 0.0));
        System.Star.MassSolar = 0.5;
        System.Star.RadiusSolar = 0.5;
        System.Star.LuminositySolar = 0.05;
        System.Star.TemperatureK = 3800.0;
        const EPlanetKind Kinds[] = { EPlanetKind::Barren, EPlanetKind::Terrestrial, EPlanetKind::Ocean, EPlanetKind::Ice, EPlanetKind::GasGiant };
        for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Kinds)); ++Index)
        {
            FPlanet& Planet = System.Planets.AddDefaulted_GetRef();
            Planet.Designation = FString::Printf(TEXT("Kessa %d"), Index + 1);
            Planet.Kind = Kinds[Index];
            Planet.MassEarth = 0.5 + Index;
            Planet.RadiusEarth = Kinds[Index] == EPlanetKind::GasGiant ? 11.0 : 0.8 + 0.1 * Index;
            Planet.SemiMajorAxisAU = 0.05 * (Index + 1);
            Planet.ReliefKm = (Kinds[Index] == EPlanetKind::Ocean || Kinds[Index] == EPlanetKind::GasGiant) ? 0.0 : 2.0 + Index;
            Planet.DayHours = Kinds[Index] == EPlanetKind::GasGiant ? 10.0 : 0.0;
        }
        return System;
    }
}

bool FSkyGroundTest::RunTest(const FString& Parameters)
{
    using namespace SkyGroundTestLocal;
    const FStarSystem System = OneOfEach();
    const FSkySystem Sky = FSkySystem::FromSystem(System, {});
    if (!TestEqual(TEXT("a star and five worlds"), Sky.Bodies.Num(), 6))
    {
        return false;
    }

    const FSkyBody& Star = Sky.Bodies[0];
    TestEqual(TEXT("the star's GM is the Sun's times its mass"), Star.GravParam, UniverseUnits::GMSunCm3PerS2 * 0.5);
    TestTrue(TEXT("a star has no ground"), Star.Ground == EGround::None && Star.Relief.Ground == EGround::None && Star.Relief.PeakCm == 0.0);

    for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
    {
        const FPlanet& Planet = System.Planets[Index];
        const FSkyBody& Body = Sky.Bodies[Index + 1];
        const bool bSolid = Planet.Kind == EPlanetKind::Barren || Planet.Kind == EPlanetKind::Ice || Planet.Kind == EPlanetKind::Terrestrial;
        const FString Name = Planet.Designation;
        TestEqual(Name + TEXT(": GM is the Earth's times its mass"), Body.GravParam, UniverseUnits::GMEarthCm3PerS2 * Planet.MassEarth);
        TestTrue(Name + (bSolid ? TEXT(": solid ground") : TEXT(": no ground")), Body.Ground == (bSolid ? EGround::Solid : EGround::None));
        TestTrue(Name + TEXT(": the relief says the same"), Body.Relief.Ground == Body.Ground);
        TestEqual(Name + TEXT(": the datum is the drawn radius"), Body.Relief.RadiusCm, Body.Radius);
        TestEqual(Name + TEXT(": the peak is the drawn relief, in cm, where there is ground"),
            Body.Relief.PeakCm, bSolid ? Planet.ReliefKm * UniverseUnits::CmPerKm : 0.0);
        TestEqual(Name + TEXT(": the ground keeps the look's craters"), Body.Relief.Cratering, Body.Cratering);
        TestEqual(Name + TEXT(": the seed offset is the face's"), Body.Relief.SeedOffset, SkyLook::SurfaceOffset(Body.SurfaceSeed));

        // Exactly the float M_SkyBody is handed: a multiple of 1/256 in
        // [0, 256), the same number in float and in double.
        const FLinearColor Gpu = ShipSky::SurfaceSeed(Body.SurfaceSeed, Body.BeltPairs);
        TestTrue(Name + TEXT(": the offset is exactly what the GPU gets"),
            Body.Relief.SeedOffset == FVector3d(Gpu.R, Gpu.G, Gpu.B));
        for (const double Part : { Body.Relief.SeedOffset.X, Body.Relief.SeedOffset.Y, Body.Relief.SeedOffset.Z })
        {
            TestTrue(Name + TEXT(": each part a multiple of 1/256 in [0, 256)"),
                Part >= 0.0 && Part < 256.0 && FMath::Frac(Part * 256.0) == 0.0);
        }
    }
    return true;
}

#endif
```

- [ ] **Step 2: Build; expect it not to compile.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh
```

Expected: FAIL: `no member named 'GravParam' in 'FSkyBody'`, `no member named 'SurfaceOffset' in namespace 'SkyLook'`.

- [ ] **Step 3: The fields.** In `SkySystem.h` add `#include "Surface/WorldReliefParams.h"` after
  `#include "Universe/UniversePosition.h"`. Replace the `SurfaceSeed` comment

```cpp
    /** Planets and moons: where on the noise this world's face is taken
     *  from, so that no two worlds wear the same one. Derived, never drawn:
     *  a look, not a fact about the world, so it moves no procgen stream. */
```

  with

```cpp
    /** Planets and moons: where on the noise this world's face -- and, since
     *  landing, its ground -- is taken from, so no two worlds wear the same
     *  one (GenSeed::SurfaceSeed; landing decision 2). Derived, never drawn,
     *  so it moves no procgen stream. */
```

  and after `double BeltPairs = 0.0;` add:

```cpp

    /** GM, cm^3/s^2: the pull of this body (landing decision 4). The
     *  Sun's or the Earth's mass parameter times procgen's mass. */
    double GravParam = 0.0;

    /** Whether there is ground to set down on (landing decision 13). */
    EGround Ground = EGround::None;

    /** What the ground is made from, for FWorldRelief: the face's seed
     *  offset, the radius, the drawn peak (0 where Ground is None) and the
     *  look's craters. Filled only here, in FromSystem. */
    FWorldReliefParams Relief;
```

  In `namespace SkyLook`, after the `BeltPairs` declaration add:

```cpp

    /** Where on the noise a world's face and ground are taken from: 16 bits
     *  of its surface seed per axis, as a multiple of 1/256 in [0, 256) --
     *  exactly the xyz ShipSky::SurfaceSeed hands M_SkyBody, so the GPU's
     *  float and this double are one number. */
    DEEPSPACE_API FVector3d SurfaceOffset(uint64 SurfaceSeed);
```

- [ ] **Step 4: The adapter.** In `SkySystem.cpp` add `#include "Sky/SkyMaterialContract.h"`. In the
  anonymous namespace, after `LookOf`, add:

```cpp

    /** What can be landed on (landing decision 13): rock, ice and the land of
     *  terrestrial worlds, which are all land in slice 1. */
    EGround GroundOf(EPlanetKind Kind)
    {
        return Kind == EPlanetKind::Barren || Kind == EPlanetKind::Ice || Kind == EPlanetKind::Terrestrial
            ? EGround::Solid : EGround::None;
    }
```

  After `Star.TemperatureK = System.Star.TemperatureK;` add:

```cpp
    Star.GravParam = UniverseUnits::GMSunCm3PerS2 * System.Star.MassSolar;
```

  After `Body.BeltPairs = Look.Surface == ESkySurface::Banded ? SkyLook::BeltPairs(Planet.DayHours) : 0.0;` add:

```cpp
        Body.GravParam = UniverseUnits::GMEarthCm3PerS2 * Planet.MassEarth;
        Body.Ground = GroundOf(Planet.Kind);
        Body.Relief.SeedOffset = SkyLook::SurfaceOffset(Body.SurfaceSeed);
        Body.Relief.RadiusCm = Body.Radius;
        Body.Relief.PeakCm = Body.Ground == EGround::Solid ? Planet.ReliefKm * UniverseUnits::CmPerKm : 0.0;
        Body.Relief.Cratering = Look.Cratering;
        Body.Relief.Ground = Body.Ground;
```

  At the end of the file add:

```cpp

FVector3d SkyLook::SurfaceOffset(uint64 SurfaceSeed)
{
    const auto Part = [SurfaceSeed](int32 Shift)
    {
        return static_cast<double>((SurfaceSeed >> Shift) & 0xFFFFull) / 65536.0 * SkyMaterial::SurfaceOffsetSpan;
    };
    return FVector3d(Part(0), Part(16), Part(32));
}
```

- [ ] **Step 5: Build and run; expect PASS; full suite.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh && ./test.sh DeepSpace.Sky.WhatCanBeLanded && ./test.sh
```

Expected: `passed: 1`; the full suite green. `FSkyBody` is not a `UPROPERTY` anywhere, so no
Blueprint is affected.

- [ ] **Step 6: Commit.**

```bash
git -C /home/matt/Development/deepspace/.worktrees/landing-a-procgen add Source/DeepSpace/Sky/SkySystem.h Source/DeepSpace/Sky/SkySystem.cpp Source/DeepSpace/Tests/SkyGroundTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/landing-a-procgen commit -F - <<'MSG'
feat(sky): FSkyBody carries the ground -- GM, whether it can be landed on, and its relief params

Filled only in FSkySystem::FromSystem, the one adapter from procgen to sky
and flight: GravParam from the IAU mass parameters, Ground Solid for rock,
ice and terrestrial (all land in slice 1), and FWorldReliefParams whose seed
offset is exactly the float M_SkyBody is handed (SkyLook::SurfaceOffset).
DeepSpace.Sky.WhatCanBeLanded pins every field on a hand-written system.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 7: Prove the test can fail.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && Tools/mutate.sh Source/DeepSpace/Sky/SkySystem.cpp 'Planet.ReliefKm * UniverseUnits::CmPerKm : 0.0;' 'Planet.ReliefKm * 1000.0 : 0.0;' DeepSpace.Sky.WhatCanBeLanded
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && Tools/mutate.sh Source/DeepSpace/Sky/SkySystem.cpp '& 0xFFFFull) / 65536.0 * SkyMaterial::SurfaceOffsetSpan;' '& 0xFFFFull) / 65535.0 * SkyMaterial::SurfaceOffsetSpan;' DeepSpace.Sky.WhatCanBeLanded
cd /home/matt/Development/deepspace/.worktrees/landing-a-procgen && ./build.sh
```

Expected: `KILLED` twice.

- [ ] **Step 8: Merge track P.** Use superpowers:finishing-a-development-branch. The merge
  gate is `./test.sh` green on the branch rebased on `main`. After the merge, track R rebases:

```bash
git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief rebase main
```

---

# Slice (a), track R: relief

## Task 6 (R1): The spike -- the shared file against today's engine nodes (GO / NO-GO)

This is the spec's go/no-go (decision 1), and it is **time-boxed as the spec says**. It
builds the real machinery -- the shader-path module, the shared file (with one detail
band, one crater band and the coarse band), the probe materials, `Tools/eyes.sh` and
`Eyes.WorldReliefParity` -- and ends in a verdict. If the box runs out before a verdict,
that is itself a NO-GO, and the report goes back to the orchestrator.

**Spike bands.** Detail band 8 (3,072 cycles per radius) and crater band 104 (768), at
their real band numbers so their noise offsets are the ones the full file will use;
and the coarse band, which the terms carry anyway.

**Files:**
- Create: `Source/DeepSpaceShaders/DeepSpaceShaders.Build.cs`, `Source/DeepSpaceShaders/Private/DeepSpaceShadersModule.cpp`
- Modify: `DeepSpace.uproject` (`Modules`), `Source/DeepSpace.Target.cs:13`, `Source/DeepSpaceEditor.Target.cs:13`
- Modify: `launch.sh:48-50`, `rebuild.sh:66-68` (stray removal), `rebuild.sh:89-93` (freshness check)
- Create: `Shaders/Private/WorldRelief.ush`
- Create: `Source/DeepSpace/Surface/WorldRelief.h`, `Source/DeepSpace/Surface/WorldRelief.cpp`
- Modify: `Source/DeepSpace/Sky/SkyMaterialContract.h` (probe paths and names, the shared file's contract)
- Modify: `Tools/sky_material_contract.json` (`parameters`, `materials`, `shared_relief`, `constants.probe_patch`, `constants.probe_bands`)
- Modify: `Tools/setup_sky_materials.py` (`surface` refactored into terms functions; `shared_terms`; `relief_probe`; `main`)
- Create: `Tools/eyes.sh`; modify `Tools/mutate.sh:74` (`MUTATE_RUNNER`: the line `if ./test.sh "$FILTER" > Saved/mutant-test.log 2>&1; then`)
- Modify: `CLAUDE.md` (*Commands*; the `Tools/mutate.sh` paragraph)
- Test: create `Source/DeepSpace/Tests/WorldReliefTest.cpp` (`DeepSpace.Surface.WorldRelief.KnownValues`, `DeepSpace.Surface.ShaderMapping`); modify `Source/DeepSpace/Tests/SkyMaterialContractTest.cpp`; create `Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp` (`Eyes.WorldReliefParity`)
- Assets (generated, LFS): `Content/Materials/Sky/M_SkyReliefProbe.uasset`, `M_SkyReliefProbeLegacy.uasset`; `M_SkyBody.uasset` re-authored with identical arithmetic

**Interfaces:**
- Consumes: `SkyMaterial::BodyAxisX/Y` and `body_axes` / `to_body` / `to_world` (surface-artifacts); `SkyTestWorld::FSkyWorld`.
- Produces: the `/Project` mapping; `WorldRelief.ush`'s `WR_` API (spike tables); `FFaceTerms`; `WorldReliefNoise::{Hash16, Simplex, GradientNoise, Voronoi, CellHash, FaceF64, FaceF32, CraterMargin, FBands, Bands}`; `SkyMaterial::{ReliefProbePath, ReliefProbeLegacyPath, ProbeFootprint, ProbeSelect, ProbeBias, ProbeScalars, ProbeVectors, WorldReliefInclude, WorldReliefEntry, WorldReliefInputs, WorldReliefOutputs}`; `Tools/eyes.sh`; `MUTATE_RUNNER`.

### The go/no-go criteria (write them into the commit that closes the spike)

`Eyes.WorldReliefParity` draws the probes over 256 x 256 samples at each of five
footprints (1/12, 1/96, 1/768, 1/3072, 1/12288 of the radius per pixel) and compares
the five raw terms (continent, detail, crater albedo, detail slope xyz, crater slope
xyz), leaving out samples within 2e-3 cells of a crater's step (its rim, `q = 1`, or the
bisector between two sites), at most 1% of them:

| Verdict | When | What follows |
|---|---|---|
| **GO** | The pipe check passes (a probe selecting nothing reads back its bias exactly); **shared file vs engine nodes** max abs difference <= 1e-3 at all five footprints, barren and giant; **C++ (double) vs shared file** <= 1e-3 at all five, barren; left out <= 1% everywhere. | R2 |
| **NO-GO: the module** | `DeepSpace.Surface.ShaderMapping` fails, or `DeepSpace.Sky.MaterialContract` reports a translation error naming `/Project/Private/WorldRelief.ush`, or the pipe check fails with the log naming the include (`Failed to compile Material ... WorldRelief.ush`), and a pasted copy of the file's text (R1-F1, Step 1) compiles where the include did not. | Fallback 1 (R1-F1): the `DeepSpaceShaders` module is dropped for the pasted text. |
| **NO-GO: the subset** | The pipe check passes and C++ (float build) vs shared file is <= 1e-3 -- so the file computes what it says -- but shared file vs engine nodes stays > 1e-3 after every port bug found inside the box is fixed. | Fallback 2 (R1-F2): the engine nodes stay in `M_SkyBody`; the C++ is an independent port held to them by the same rendered test (sign-off item 1's third alternative). |
| **FLOAT FLOOR** (not in the spec; stop and escalate) | Shared file vs engine nodes <= 1e-3 everywhere, but C++ (double) vs shared file > 1e-3 at some footprint, **and** C++ (double) vs engine nodes is as large (the report's diagnostics) -- the gap is the GPU's float arithmetic, not the file. | Stop. The report goes to the developer through the orchestrator: the spec's 1e-3 at that footprint is below float's floor at these noise coordinates (see *Planning notes the executor must know*, 1). The tolerance per footprint is the spec's to change, not this task's. |

- [ ] **Step 1: The shader-path module.** Create `Source/DeepSpaceShaders/DeepSpaceShaders.Build.cs`:

```csharp
using UnrealBuildTool;

// The shader-path module (landing spec decision 1): maps /Project to this
// project's Shaders/ at PostConfigInit, before any shader compiles, so a
// material can include /Project/Private/WorldRelief.ush -- the file the
// DeepSpace module compiles into C++ as well. A module of its own because
// DeepSpace loads long after shaders start compiling.
public class DeepSpaceShaders : ModuleRules
{
	public DeepSpaceShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "RenderCore" });
	}
}
```

  Create `Source/DeepSpaceShaders/Private/DeepSpaceShadersModule.cpp`:

```cpp
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

/**
 * Maps /Project to Shaders/, so M_SkyBody's Custom node can include
 * /Project/Private/WorldRelief.ush: the one noise file the C++ compiles too
 * (landing decision 1; ADR 0006, amended). Loaded at PostConfigInit
 * (DeepSpace.uproject), before any shader compiles. Nothing else lives here.
 */
class FDeepSpaceShadersModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        // Once: the engine asserts on a second mapping of one virtual path,
        // and a module can be started twice in one process.
        if (!AllShaderSourceDirectoryMappings().Contains(TEXT("/Project")))
        {
            AddShaderSourceDirectoryMapping(TEXT("/Project"),
                FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"))));
        }
    }
};

IMPLEMENT_MODULE(FDeepSpaceShadersModule, DeepSpaceShaders)
```

  In `DeepSpace.uproject`, in `"Modules"`, after the `DeepSpace` entry's closing `}` add
  `,` and:

```json
		{
			"Name": "DeepSpaceShaders",
			"Type": "Runtime",
			"LoadingPhase": "PostConfigInit"
		}
```

  In both `Source/DeepSpace.Target.cs` and `Source/DeepSpaceEditor.Target.cs` replace
  `ExtraModuleNames.Add("DeepSpace");` with
  `ExtraModuleNames.AddRange(new string[] { "DeepSpace", "DeepSpaceShaders" });`.

  In `launch.sh`, after `LIB=Binaries/Linux/libUnrealEditor-DeepSpace.so` add
  `SHADERS_LIB=Binaries/Linux/libUnrealEditor-DeepSpaceShaders.so`, and replace

```bash
    elif [[ -n $(find Source DeepSpace.uproject -newer "$LIB" -type f \
                 \( -name '*.cpp' -o -name '*.h' -o -name '*.cs' -o -name '*.uproject' \) \
                 -print -quit) ]]; then
        echo "C++ has changed since the last build"
```

  with

```bash
    elif [[ -n $(find Source/DeepSpace Source/DeepSpace.Target.cs Source/DeepSpaceEditor.Target.cs Shaders DeepSpace.uproject \
                 -newer "$LIB" -type f \
                 \( -name '*.cpp' -o -name '*.h' -o -name '*.cs' -o -name '*.uproject' -o -name '*.ush' \) \
                 -print -quit) ]]; then
        # Shaders/: the DeepSpace module compiles the shared ground file too.
        echo "C++ has changed since the last build"
    elif [[ ! -f $SHADERS_LIB || -n $(find Source/DeepSpaceShaders -newer "$SHADERS_LIB" -type f -print -quit) ]]; then
        echo "the shader-path module has changed since the last build"
```

  In `rebuild.sh`, after the `rm -fv Binaries/Linux/libUnrealEditor-DeepSpace-[0-9]*.so \` block
  (the three lines ending `2>/dev/null || true`) add:

```bash
rm -fv Binaries/Linux/libUnrealEditor-DeepSpaceShaders-[0-9]*.so \
       Binaries/Linux/libUnrealEditor-DeepSpaceShaders-[0-9]*.debug \
       Binaries/Linux/libUnrealEditor-DeepSpaceShaders-[0-9]*.sym 2>/dev/null || true
```

  and replace

```bash
if [[ -n $(find Source -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.cs' \) \
               -newer "$LIB" -print -quit) ]]; then
    echo "!!! $LIB is older than the newest source file." >&2
    exit 1
fi
```

  with

```bash
# The DeepSpace library against its own sources -- which include the shared
# ground file, Shaders/Private/WorldRelief.ush (landing decision 1) -- and the
# shader-path module's library against its own.
if [[ -n $(find Source/DeepSpace Source/DeepSpace.Target.cs Source/DeepSpaceEditor.Target.cs Shaders -type f \
               \( -name '*.cpp' -o -name '*.h' -o -name '*.cs' -o -name '*.ush' \) \
               -newer "$LIB" -print -quit) ]]; then
    echo "!!! $LIB is older than the newest source file." >&2
    exit 1
fi
SHADERS_LIB="Binaries/Linux/$(python3 -c "import json;print(json.load(open('$MANIFEST'))['Modules']['DeepSpaceShaders'])")"
if [[ -n $(find Source/DeepSpaceShaders -type f -newer "$SHADERS_LIB" -print -quit) ]]; then
    echo "!!! $SHADERS_LIB is older than the newest source file." >&2
    exit 1
fi
```

- [ ] **Step 2: The shared file (spike tables).** Create `Shaders/Private/WorldRelief.ush`:

```hlsl
// WorldRelief.ush -- a world's ground, written once and compiled twice
// (landing spec decision 1; ADR 0006, amended).
//
// The GPU includes this file into M_SkyBody's Custom node through the
// /Project mapping DeepSpaceShaders makes at PostConfigInit. The C++ includes
// it into Source/DeepSpace/Surface/WorldRelief.cpp twice: in double, for the
// ground the flight and the terrain read, and in float, as the parity test's
// mirror of the GPU. A syntax one compiler refuses fails that compiler:
// ./build.sh for the C++, Eyes.WorldReliefParity for the GPU. (A Custom
// node's HLSL error is invisible to the headless suite: the material ships
// grey with every test green.)
//
// The subset: valid HLSL and valid C++ at once. No swizzles, no vector or
// matrix types, no mul(), no implicit vector/scalar arithmetic: every vector
// is spelled out a component at a time, and every literal is WR_REAL(...),
// so the float build computes in float. Every symbol is WR_: Common.ush
// already includes the engine's Random.ush into every material, and an
// unprefixed copy would redefine its functions.
//
// What this must equal is the arithmetic of the engine nodes M_SkyBody drew
// with before -- Rand3DPCG16 (RandomPCG.ush), GradientNoise3D_ALU,
// JacobianSimplex_ALU and VoronoiNoise3D_ALU (Random.ush), and
// MaterialExpressionNoise (Common.ush) -- operation for operation, the hash
// bit for bit. It is a rewrite, not a port, and the equality is measured by
// Eyes.WorldReliefParity, never assumed.
//
// Everything is a function of D, the unit direction from the body's centre in
// its own axes (the universe's: worlds do not spin), and every frequency is
// in cycles per body radius.

#if defined(WR_CPP)
// -- C++ ---------------------------------------------------------------------
// WorldRelief.cpp includes <cmath>, then this file inside a namespace with
// WR_REAL defined: once as double, once as float.
#define WR_UINT uint32
#define WR_DETAIL_BANDS 1
#define WR_CRATER_BANDS 1
#define WR_CONTINENT_LEVELS 4
inline WR_REAL WR_floor(WR_REAL X) { return std::floor(X); }
inline WR_REAL WR_frac(WR_REAL X) { return X - std::floor(X); }
inline WR_REAL WR_saturate(WR_REAL X) { return X < WR_REAL(0.0) ? WR_REAL(0.0) : (X > WR_REAL(1.0) ? WR_REAL(1.0) : X); }
inline WR_REAL WR_min(WR_REAL A, WR_REAL B) { return A < B ? A : B; }
inline WR_REAL WR_max(WR_REAL A, WR_REAL B) { return A > B ? A : B; }
inline WR_REAL WR_sqrt(WR_REAL X) { return std::sqrt(X); }
inline WR_REAL WR_step(WR_REAL Edge, WR_REAL X) { return X >= Edge ? WR_REAL(1.0) : WR_REAL(0.0); }
#else
// -- HLSL --------------------------------------------------------------------
#pragma once
#define WR_REAL float
#define WR_UINT uint
#define WR_DETAIL_BANDS 1
#define WR_CRATER_BANDS 1
#define WR_CONTINENT_LEVELS 4
#define WR_floor(X) floor(X)
#define WR_frac(X) frac(X)
#define WR_saturate(X) saturate(X)
#define WR_min(A, B) min(A, B)
#define WR_max(A, B) max(A, B)
#define WR_sqrt(X) sqrt(X)
#define WR_step(Edge, X) step(Edge, X)
#endif

// -- The bands ---------------------------------------------------------------
// Tools/sky_material_contract.json's constants, which DeepSpace.Sky.
// MaterialContract holds equal to these. Band n of the detail bands is
// detail_frequencies[n - 1], band n of the craters (101..106) is
// crater_frequencies[n - 101], and each is taken from its own corner of the
// noise, at SeedOffset + (37, 59, 83) x n. The coarse band is band 0.
// SPIKE (landing slice (a), task R1): one detail band and one crater band.
static const WR_REAL WR_CONTINENT_FREQUENCY = WR_REAL(1.5);
static const WR_REAL WR_LEVEL_SCALE = WR_REAL(2.0);
static const WR_REAL WR_DETAIL_FREQUENCY[WR_DETAIL_BANDS] = { WR_REAL(3072.0) };
static const WR_REAL WR_DETAIL_WEIGHT[WR_DETAIL_BANDS] = { WR_REAL(0.5) };
static const int WR_DETAIL_INDEX[WR_DETAIL_BANDS] = { 8 };
static const WR_REAL WR_CRATER_FREQUENCY[WR_CRATER_BANDS] = { WR_REAL(768.0) };
static const int WR_CRATER_INDEX[WR_CRATER_BANDS] = { 104 };

// A crater: q is the distance from its centre in crater radii; the bowl is
// depth x (q^2 - 1 + rim) and the rim falls as depth x rim x (3 - 2q)^2 to
// q = 1.5. These are the graph's constants exactly as it computed them.
static const WR_REAL WR_CRATER_RADIUS = WR_REAL(0.35);
static const WR_REAL WR_CRATER_INV_RADIUS = WR_REAL(1.0 / 0.35);
static const WR_REAL WR_CRATER_DEPTH = WR_REAL(0.4);                  // crater_depth: the profile's scale
static const WR_REAL WR_CRATER_RIM = WR_REAL(0.25);                   // crater_rim: the rim's share of it
static const WR_REAL WR_CRATER_WALL = WR_REAL(2.0 * 0.4);             // 2 x crater_depth
static const WR_REAL WR_CRATER_RIM_FALL = WR_REAL(-4.0 * 0.4 * 0.25); // -4 x depth x rim
static const WR_REAL WR_CRATER_KEEP = WR_REAL(0.6);
static const WR_REAL WR_CRATER_FLOOR_DARK = WR_REAL(0.12);
static const WR_REAL WR_CRATER_RIM_BRIGHT = WR_REAL(0.12);

// Random.ush's own constants.
static const WR_REAL WR_VORONOI_JITTER = WR_REAL(0.2588);                // quality 1-2: a 2x2x2 search finds every site
static const WR_REAL WR_SIMPLEX_SCALE = WR_REAL(1024.0) / WR_REAL(375.0); // makes the simplex -1..1

// -- Small types -------------------------------------------------------------
struct WR_Vec3 { WR_REAL X; WR_REAL Y; WR_REAL Z; };
struct WR_Hash3 { WR_UINT X; WR_UINT Y; WR_UINT Z; };
// A value and its gradient: JacobianSimplex_ALU's first row.
struct WR_Noise4 { WR_REAL GX; WR_REAL GY; WR_REAL GZ; WR_REAL Value; };
// The nearest Voronoi site, and how far it is.
struct WR_Site { WR_REAL X; WR_REAL Y; WR_REAL Z; WR_REAL Distance; };
// One crater band's albedo and slope.
struct WR_Crater { WR_REAL Albedo; WR_REAL SX; WR_REAL SY; WR_REAL SZ; };
// M_SkyBody's raw face terms, before any knob or clamp.
struct WR_Terms
{
    WR_REAL Continent;
    WR_REAL Detail;
    WR_REAL DetailSlopeX;
    WR_REAL DetailSlopeY;
    WR_REAL DetailSlopeZ;
    WR_REAL CraterAlbedo;
    WR_REAL CraterSlopeX;
    WR_REAL CraterSlopeY;
    WR_REAL CraterSlopeZ;
};

WR_Vec3 WR_MakeVec3(WR_REAL X, WR_REAL Y, WR_REAL Z)
{
    WR_Vec3 V;
    V.X = X;
    V.Y = Y;
    V.Z = Z;
    return V;
}

// -- The hash: Rand3DPCG16 (RandomPCG.ush), bit for bit ----------------------
// An LCG step, then six multiply-add Feistel rounds; the top 16 bits of each.
WR_Hash3 WR_Rand3DPCG16(int PX, int PY, int PZ)
{
    WR_UINT X = WR_UINT(PX) * 1664525u + 1013904223u;
    WR_UINT Y = WR_UINT(PY) * 1664525u + 1013904223u;
    WR_UINT Z = WR_UINT(PZ) * 1664525u + 1013904223u;
    X += Y * Z;
    Y += Z * X;
    Z += X * Y;
    X += Y * Z;
    Y += Z * X;
    Z += X * Y;
    WR_Hash3 H;
    H.X = X >> 16u;
    H.Y = Y >> 16u;
    H.Z = Z >> 16u;
    return H;
}

// MGradientMask and MGradientScale (Random.ush): a corner of the cube, each
// axis exactly +-1.
WR_Vec3 WR_GradientDirection(WR_UINT R)
{
    return WR_MakeVec3(WR_REAL(R & 0x8000u) * WR_REAL(1.0 / 16384.0) - WR_REAL(1.0),
                       WR_REAL(R & 0x4000u) * WR_REAL(1.0 / 8192.0) - WR_REAL(1.0),
                       WR_REAL(R & 0x2000u) * WR_REAL(1.0 / 4096.0) - WR_REAL(1.0));
}

// -- The coarse band: GradientNoise3D_ALU, untiled, as MaterialExpressionNoise
//    loops it --------------------------------------------------------------
WR_REAL WR_MGradientDot(int Seed, WR_REAL OX, WR_REAL OY, WR_REAL OZ)
{
    const WR_Vec3 G = WR_GradientDirection(WR_Rand3DPCG16(Seed, 0, 0).X);
    return G.X * OX + G.Y * OY + G.Z * OZ;
}

WR_REAL WR_PerlinRamp(WR_REAL T)
{
    return T * T * T * (T * (T * WR_REAL(6.0) - WR_REAL(15.0)) + WR_REAL(10.0));
}

WR_REAL WR_Lerp(WR_REAL A, WR_REAL B, WR_REAL T)
{
    return A + T * (B - A);
}

WR_REAL WR_GradientNoise(WR_REAL VX, WR_REAL VY, WR_REAL VZ)
{
    const WR_REAL IX = WR_floor(VX);
    const WR_REAL IY = WR_floor(VY);
    const WR_REAL IZ = WR_floor(VZ);
    const WR_REAL FX = WR_frac(VX);
    const WR_REAL FY = WR_frac(VY);
    const WR_REAL FZ = WR_frac(VZ);
    // NoiseSeeds, untiled: the corners' seeds are the lattice point dotted
    // with the primes (19, 47, 101), then offsets of those primes.
    const WR_REAL S000 = IX * WR_REAL(19.0) + IY * WR_REAL(47.0) + IZ * WR_REAL(101.0);
    const WR_REAL S100 = S000 + WR_REAL(19.0);
    const WR_REAL S010 = S000 + WR_REAL(47.0);
    const WR_REAL S110 = S100 + WR_REAL(47.0);
    const WR_REAL S001 = S000 + WR_REAL(101.0);
    const WR_REAL S101 = S100 + WR_REAL(101.0);
    const WR_REAL S011 = S010 + WR_REAL(101.0);
    const WR_REAL S111 = S110 + WR_REAL(101.0);
    const WR_REAL R000 = WR_MGradientDot(int(S000), FX, FY, FZ);
    const WR_REAL R100 = WR_MGradientDot(int(S100), FX - WR_REAL(1.0), FY, FZ);
    const WR_REAL R010 = WR_MGradientDot(int(S010), FX, FY - WR_REAL(1.0), FZ);
    const WR_REAL R110 = WR_MGradientDot(int(S110), FX - WR_REAL(1.0), FY - WR_REAL(1.0), FZ);
    const WR_REAL R001 = WR_MGradientDot(int(S001), FX, FY, FZ - WR_REAL(1.0));
    const WR_REAL R101 = WR_MGradientDot(int(S101), FX - WR_REAL(1.0), FY, FZ - WR_REAL(1.0));
    const WR_REAL R011 = WR_MGradientDot(int(S011), FX, FY - WR_REAL(1.0), FZ - WR_REAL(1.0));
    const WR_REAL R111 = WR_MGradientDot(int(S111), FX - WR_REAL(1.0), FY - WR_REAL(1.0), FZ - WR_REAL(1.0));
    const WR_REAL WX = WR_PerlinRamp(FX);
    const WR_REAL WY = WR_PerlinRamp(FY);
    const WR_REAL WZ = WR_PerlinRamp(FZ);
    const WR_REAL I = WR_Lerp(WR_Lerp(R000, R100, WX), WR_Lerp(R010, R110, WX), WY);
    const WR_REAL J = WR_Lerp(WR_Lerp(R001, R101, WX), WR_Lerp(R011, R111, WX), WY);
    return WR_Lerp(I, J, WZ);
}

// MaterialExpressionNoise (Common.ush) as M_SkyBody's coarse band calls it:
// GradientALU, WR_CONTINENT_LEVELS octaves at WR_LEVEL_SCALE, each faded by
// saturate(1 - its filter width), mapped back to -1..1.
WR_REAL WR_Continent(WR_REAL PX, WR_REAL PY, WR_REAL PZ, WR_REAL FilterWidth)
{
    WR_REAL Out = WR_REAL(0.0);
    WR_REAL OutScale = WR_REAL(1.0);
    WR_REAL Width = FilterWidth;
    WR_REAL X = PX;
    WR_REAL Y = PY;
    WR_REAL Z = PZ;
    for (int Level = 0; Level < WR_CONTINENT_LEVELS; ++Level)
    {
        OutScale *= WR_saturate(WR_REAL(1.0) - Width);
        Out += WR_GradientNoise(X, Y, Z) * OutScale;
        X *= WR_LEVEL_SCALE;
        Y *= WR_LEVEL_SCALE;
        Z *= WR_LEVEL_SCALE;
        OutScale *= WR_REAL(1.0) / WR_LEVEL_SCALE;
        Width *= WR_LEVEL_SCALE;
    }
    return WR_Lerp(WR_REAL(-1.0), WR_REAL(1.0), Out * WR_REAL(0.5) + WR_REAL(0.5));
}

// -- The detail bands: JacobianSimplex_ALU's first channel -----------------
// One corner of the tetrahedron: its smoothed contribution to the value and
// to the gradient, added to N.
WR_Noise4 WR_SimplexCorner(WR_Noise4 N, WR_REAL VX, WR_REAL VY, WR_REAL VZ, WR_REAL TX, WR_REAL TY, WR_REAL TZ)
{
    const WR_REAL FX = VX - TX;
    const WR_REAL FY = VY - TY;
    const WR_REAL FZ = VZ - TZ;
    const WR_Vec3 G = WR_GradientDirection(WR_Rand3DPCG16(
        int(WR_floor(WR_REAL(6.0) * TX + WR_REAL(0.5))),
        int(WR_floor(WR_REAL(6.0) * TY + WR_REAL(0.5))),
        int(WR_floor(WR_REAL(6.0) * TZ + WR_REAL(0.5)))).X);
    const WR_REAL GF = G.X * FX + G.Y * FY + G.Z * FZ;
    const WR_REAL D2 = FX * FX + FY * FY + FZ * FZ;
    const WR_REAL S = WR_saturate(WR_REAL(2.0) * D2);
    // SimplexSmooth and SimplexDSmooth.
    const WR_REAL Smooth = WR_SIMPLEX_SCALE + S * (WR_REAL(-3.0) * WR_SIMPLEX_SCALE + S * (WR_REAL(3.0) * WR_SIMPLEX_SCALE - S * WR_SIMPLEX_SCALE));
    const WR_REAL DSmooth = WR_REAL(-12.0) * WR_SIMPLEX_SCALE + S * (WR_REAL(24.0) * WR_SIMPLEX_SCALE - S * WR_REAL(12.0) * WR_SIMPLEX_SCALE);
    N.GX += Smooth * G.X + DSmooth * FX * GF;
    N.GY += Smooth * G.Y + DSmooth * FY * GF;
    N.GZ += Smooth * G.Z + DSmooth * FZ * GF;
    N.Value += Smooth * GF;
    return N;
}

WR_Noise4 WR_Simplex(WR_REAL VX, WR_REAL VY, WR_REAL VZ)
{
    // SimplexCorners: the base corner by skewing to tetrahedral space and
    // back, in the engine's own order of operations.
    const WR_REAL TX = WR_floor(VX + VX / WR_REAL(3.0) + VY / WR_REAL(3.0) + VZ / WR_REAL(3.0));
    const WR_REAL TY = WR_floor(VY + VX / WR_REAL(3.0) + VY / WR_REAL(3.0) + VZ / WR_REAL(3.0));
    const WR_REAL TZ = WR_floor(VZ + VX / WR_REAL(3.0) + VY / WR_REAL(3.0) + VZ / WR_REAL(3.0));
    const WR_REAL BX = TX - TX / WR_REAL(6.0) - TY / WR_REAL(6.0) - TZ / WR_REAL(6.0);
    const WR_REAL BY = TY - TX / WR_REAL(6.0) - TY / WR_REAL(6.0) - TZ / WR_REAL(6.0);
    const WR_REAL BZ = TZ - TX / WR_REAL(6.0) - TY / WR_REAL(6.0) - TZ / WR_REAL(6.0);
    const WR_REAL FX = VX - BX;
    const WR_REAL FY = VY - BY;
    const WR_REAL FZ = VZ - BZ;
    // g = step(f.yzx, f.xyz); h = 1 - g.zxy.
    const WR_REAL GX = WR_step(FY, FX);
    const WR_REAL GY = WR_step(FZ, FY);
    const WR_REAL GZ = WR_step(FX, FZ);
    const WR_REAL HX = WR_REAL(1.0) - GZ;
    const WR_REAL HY = WR_REAL(1.0) - GX;
    const WR_REAL HZ = WR_REAL(1.0) - GY;
    const WR_REAL A1X = WR_min(GX, HX) - WR_REAL(1.0 / 6.0);
    const WR_REAL A1Y = WR_min(GY, HY) - WR_REAL(1.0 / 6.0);
    const WR_REAL A1Z = WR_min(GZ, HZ) - WR_REAL(1.0 / 6.0);
    const WR_REAL A2X = WR_max(GX, HX) - WR_REAL(1.0 / 3.0);
    const WR_REAL A2Y = WR_max(GY, HY) - WR_REAL(1.0 / 3.0);
    const WR_REAL A2Z = WR_max(GZ, HZ) - WR_REAL(1.0 / 3.0);
    WR_Noise4 N;
    N.GX = WR_REAL(0.0);
    N.GY = WR_REAL(0.0);
    N.GZ = WR_REAL(0.0);
    N.Value = WR_REAL(0.0);
    N = WR_SimplexCorner(N, VX, VY, VZ, BX, BY, BZ);
    N = WR_SimplexCorner(N, VX, VY, VZ, BX + A1X, BY + A1Y, BZ + A1Z);
    N = WR_SimplexCorner(N, VX, VY, VZ, BX + A2X, BY + A2Y, BZ + A2Z);
    N = WR_SimplexCorner(N, VX, VY, VZ, BX + WR_REAL(0.5), BY + WR_REAL(0.5), BZ + WR_REAL(0.5));
    return N;
}

// One detail band, as M_SkyBody's detail_band: the simplex at
// P x Frequency + Offset, times its footprint fade and its weight. P is the
// stretched direction (a giant's detail is drawn out across its belts);
// Width is the stretched footprint.
WR_Noise4 WR_DetailBand(WR_REAL PX, WR_REAL PY, WR_REAL PZ, WR_REAL Frequency,
                        WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL Width, WR_REAL Weight)
{
    const WR_Noise4 N = WR_Simplex(PX * Frequency + OX, PY * Frequency + OY, PZ * Frequency + OZ);
    const WR_REAL Scale = WR_saturate(WR_REAL(1.0) - Width * Frequency) * Weight;
    WR_Noise4 R;
    R.GX = N.GX * Scale;
    R.GY = N.GY * Scale;
    R.GZ = N.GZ * Scale;
    R.Value = N.Value * Scale;
    return R;
}

// -- The craters: VoronoiNoise3D_ALU at quality 1 --------------------------
// VoronoiCornerSample: a site's jitter from its lattice corner.
WR_Vec3 WR_VoronoiJitter(WR_REAL CX, WR_REAL CY, WR_REAL CZ)
{
    const WR_Hash3 H = WR_Rand3DPCG16(int(CX), int(CY), int(CZ));
    const WR_REAL NX = WR_REAL(H.X) / WR_REAL(65535.0) - WR_REAL(0.5);
    const WR_REAL NY = WR_REAL(H.Y) / WR_REAL(65535.0) - WR_REAL(0.5);
    const WR_REAL NZ = WR_REAL(H.Z) / WR_REAL(65535.0) - WR_REAL(0.5);
    const WR_REAL Inv = WR_REAL(1.0) / WR_sqrt(NX * NX + NY * NY + NZ * NZ);
    return WR_MakeVec3(NX * Inv * WR_VORONOI_JITTER, NY * Inv * WR_VORONOI_JITTER, NZ * Inv * WR_VORONOI_JITTER);
}

// The nearest of the eight sites around V, and how far it is.
WR_Site WR_Voronoi(WR_REAL VX, WR_REAL VY, WR_REAL VZ)
{
    const WR_REAL FX = WR_frac(VX);
    const WR_REAL FY = WR_frac(VY);
    const WR_REAL FZ = WR_frac(VZ);
    const WR_REAL IX = WR_floor(VX);
    const WR_REAL IY = WR_floor(VY);
    const WR_REAL IZ = WR_floor(VZ);
    WR_Site Best;
    Best.X = WR_REAL(0.0);
    Best.Y = WR_REAL(0.0);
    Best.Z = WR_REAL(0.0);
    Best.Distance = WR_REAL(100.0); // squared, until the end
    for (int OffX = 0; OffX <= 1; ++OffX)
    {
        for (int OffY = 0; OffY <= 1; ++OffY)
        {
            for (int OffZ = 0; OffZ <= 1; ++OffZ)
            {
                const WR_Vec3 J = WR_VoronoiJitter(IX + WR_REAL(OffX), IY + WR_REAL(OffY), IZ + WR_REAL(OffZ));
                const WR_REAL PX = WR_REAL(OffX) + J.X;
                const WR_REAL PY = WR_REAL(OffY) + J.Y;
                const WR_REAL PZ = WR_REAL(OffZ) + J.Z;
                const WR_REAL DX = FX - PX;
                const WR_REAL DY = FY - PY;
                const WR_REAL DZ = FZ - PZ;
                const WR_REAL Squared = DX * DX + DY * DY + DZ * DZ;
                if (!(Squared > Best.Distance))
                {
                    Best.X = IX + PX;
                    Best.Y = IY + PY;
                    Best.Z = IZ + PZ;
                    Best.Distance = Squared;
                }
            }
        }
    }
    Best.Distance = WR_sqrt(Best.Distance);
    return Best;
}

// The CellnoiseALU vector noise's first channel: 0..1 from the lattice cell.
WR_REAL WR_CellHash(WR_REAL X, WR_REAL Y, WR_REAL Z)
{
    return WR_REAL(WR_Rand3DPCG16(int(WR_floor(X)), int(WR_floor(Y)), int(WR_floor(Z))).X) / WR_REAL(65535.0);
}

// One crater band, as M_SkyBody's crater_band: the nearest site is the
// crater's centre, kept or not by the hash of its own cell, its albedo a
// darker floor and brighter rim, its slope the profile's along the way out.
WR_Crater WR_CraterBand(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL Frequency,
                        WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL Footprint)
{
    const WR_REAL VX = DX * Frequency + OX;
    const WR_REAL VY = DY * Frequency + OY;
    const WR_REAL VZ = DZ * Frequency + OZ;
    const WR_Site Site = WR_Voronoi(VX, VY, VZ);
    const WR_REAL Held = WR_step(WR_CellHash(Site.X + WR_REAL(0.5), Site.Y + WR_REAL(0.5), Site.Z + WR_REAL(0.5)), WR_CRATER_KEEP);
    const WR_REAL Q = Site.Distance * WR_CRATER_INV_RADIUS;
    const WR_REAL Outside = WR_step(WR_REAL(1.0), Q);
    const WR_REAL Inside = WR_REAL(1.0) - Outside;
    const WR_REAL Falling = WR_saturate(Q * WR_REAL(-2.0) + WR_REAL(3.0));
    const WR_REAL Wall = Q * WR_CRATER_WALL * Inside + Falling * WR_CRATER_RIM_FALL * Outside;
    // Over a floored distance, not normalised: at the exact centre a
    // normalised zero is NaN, and the wall's slope is 0 there anyway.
    const WR_REAL Apart = WR_max(Site.Distance, WR_REAL(1.0e-4));
    const WR_REAL AwayX = (VX - Site.X) / Apart;
    const WR_REAL AwayY = (VY - Site.Y) / Apart;
    const WR_REAL AwayZ = (VZ - Site.Z) / Apart;
    const WR_REAL FloorDark = (WR_REAL(1.0) - Q * Q) * (Inside * -WR_CRATER_FLOOR_DARK);
    const WR_REAL RimBright = Falling * Outside * WR_CRATER_RIM_BRIGHT;
    const WR_REAL Weight = Held * WR_saturate(WR_REAL(1.0) - Footprint * Frequency);
    WR_Crater C;
    C.Albedo = (FloorDark + RimBright) * Weight;
    C.SX = AwayX * Wall * Weight;
    C.SY = AwayY * Wall * Weight;
    C.SZ = AwayZ * Wall * Weight;
    return C;
}

// -- The entry point --------------------------------------------------------
// Every raw term of M_SkyBody's face at D, for footprint Footprint (D units a
// pixel, times filter_pixels), seed offset O, and Stretch (1 for ground,
// belt_stretch for a giant's detail).
WR_Terms WR_SurfaceTerms(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL Footprint,
                         WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL Stretch)
{
    const WR_REAL PX = DX;
    const WR_REAL PY = DY;
    const WR_REAL PZ = DZ * Stretch;
    WR_Terms T;
    T.Continent = WR_Continent(PX * WR_CONTINENT_FREQUENCY + OX, PY * WR_CONTINENT_FREQUENCY + OY,
                               PZ * WR_CONTINENT_FREQUENCY + OZ, Footprint * WR_CONTINENT_FREQUENCY * Stretch);
    const WR_REAL Width = Footprint * Stretch;
    T.Detail = WR_REAL(0.0);
    T.DetailSlopeX = WR_REAL(0.0);
    T.DetailSlopeY = WR_REAL(0.0);
    T.DetailSlopeZ = WR_REAL(0.0);
    for (int Band = 0; Band < WR_DETAIL_BANDS; ++Band)
    {
        const int Index = WR_DETAIL_INDEX[Band];
        const WR_Noise4 N = WR_DetailBand(PX, PY, PZ, WR_DETAIL_FREQUENCY[Band],
            OX + WR_REAL(37 * Index), OY + WR_REAL(59 * Index), OZ + WR_REAL(83 * Index), Width, WR_DETAIL_WEIGHT[Band]);
        T.DetailSlopeX += N.GX;
        T.DetailSlopeY += N.GY;
        T.DetailSlopeZ += N.GZ;
        T.Detail += N.Value;
    }
    T.CraterAlbedo = WR_REAL(0.0);
    T.CraterSlopeX = WR_REAL(0.0);
    T.CraterSlopeY = WR_REAL(0.0);
    T.CraterSlopeZ = WR_REAL(0.0);
    for (int Crater = 0; Crater < WR_CRATER_BANDS; ++Crater)
    {
        const int Index = WR_CRATER_INDEX[Crater];
        const WR_Crater C = WR_CraterBand(DX, DY, DZ, WR_CRATER_FREQUENCY[Crater],
            OX + WR_REAL(37 * Index), OY + WR_REAL(59 * Index), OZ + WR_REAL(83 * Index), Footprint);
        T.CraterAlbedo += C.Albedo;
        T.CraterSlopeX += C.SX;
        T.CraterSlopeY += C.SY;
        T.CraterSlopeZ += C.SZ;
    }
    return T;
}
```

- [ ] **Step 3: Write the failing headless tests.** Create `Source/DeepSpace/Tests/WorldReliefTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "Sky/SkyMaterialContract.h"
#include "Surface/WorldRelief.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWorldReliefKnownValuesTest,
    "DeepSpace.Surface.WorldRelief.KnownValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShaderMappingTest,
    "DeepSpace.Surface.ShaderMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WorldReliefTestLocal
{
    /** Worked from the HLSL formulas in double, outside the engine and outside
     *  this code: Rand3DPCG16's LCG step and six Feistel rounds by hand, the
     *  noise from Random.ush's arithmetic. Never read back from a run. */
    constexpr double Tolerance = 1.0e-6;
    const FVector3d A(0.3, 1.7, -2.2);
    const FVector3d B(12.34, -5.67, 89.01);
    /** Inside crater band 104's bowl, 0.11 cells from any step, so a
     *  rounding cannot move it across one. */
    const FVector3d Known(0.6005198731401901, -0.47976589864946456, 0.6396878648659529);
    const FVector3d Offset(12.5, 200.25, 77.0);

    bool Near(double X, double Y) { return FMath::Abs(X - Y) <= Tolerance; }
    bool Near(const FVector3d& X, const FVector3d& Y) { return (X - Y).GetAbsMax() <= Tolerance; }
}

bool FWorldReliefKnownValuesTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;

    // -- The hash, bit for bit ------------------------------------------------
    struct FHashCase { int32 X; int32 Y; int32 Z; FIntVector Expected; };
    const FHashCase Hashes[] = {
        { 0, 0, 0, FIntVector(7000, 6616, 52874) },
        { 1, 2, 3, FIntVector(26825, 11689, 49510) },
        { -1, 0, 0, FIntVector(36590, 25752, 191) },
        { -7, 11, -13, FIntVector(7994, 17175, 517) },
        { 123456, -654321, 42, FIntVector(31900, 11355, 10796) },
    };
    for (const FHashCase& Case : Hashes)
    {
        const FIntVector Got = WorldReliefNoise::Hash16(Case.X, Case.Y, Case.Z);
        TestTrue(FString::Printf(TEXT("Rand3DPCG16(%d, %d, %d) is %s (got %s)"), Case.X, Case.Y, Case.Z,
            *Case.Expected.ToString(), *Got.ToString()), Got == Case.Expected);
    }

    // -- The simplex and its gradient ---------------------------------------
    FVector3d Gradient;
    TestTrue(TEXT("simplex value at A"), Near(WorldReliefNoise::Simplex(A, Gradient), -0.3226424121942387));
    TestTrue(TEXT("simplex gradient at A"), Near(Gradient, FVector3d(3.5769516436543185, -0.9177807075555531, -0.24077926399999894)));
    TestTrue(TEXT("simplex value at B"), Near(WorldReliefNoise::Simplex(B, Gradient), 0.2897821367384506));
    TestTrue(TEXT("simplex gradient at B"), Near(Gradient, FVector3d(0.884187009374707, 0.9303530368734697, 2.14895895591946)));
    // The gradient is the value's own: central differences agree.
    WorldReliefNoise::Simplex(A, Gradient);
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        FVector3d Step = FVector3d::ZeroVector;
        Step[Axis] = 1.0e-6;
        FVector3d Unused;
        const double Difference = (WorldReliefNoise::Simplex(A + Step, Unused) - WorldReliefNoise::Simplex(A - Step, Unused)) / 2.0e-6;
        TestTrue(FString::Printf(TEXT("the simplex's gradient is its value's, axis %d (%.9f against %.9f)"), Axis, Gradient[Axis], Difference),
            FMath::Abs(Gradient[Axis] - Difference) < 1.0e-5);
    }

    // -- The coarse band's gradient noise, the Voronoi, the cell hash ---------
    TestTrue(TEXT("gradient noise at A"), Near(WorldReliefNoise::GradientNoise(A), 0.0846626387131395));
    TestTrue(TEXT("gradient noise at B"), Near(WorldReliefNoise::GradientNoise(B), 0.21445867956215167));
    const FVector4d AtA = WorldReliefNoise::Voronoi(A);
    TestTrue(TEXT("the site nearest A"), Near(FVector3d(AtA.X, AtA.Y, AtA.Z), FVector3d(-0.19378062431236706, 1.8322746763096283, -1.9640177066591178)));
    TestTrue(TEXT("and how far"), Near(AtA.W, 0.5630306720859443));
    const FVector4d AtB = WorldReliefNoise::Voronoi(B);
    TestTrue(TEXT("the site nearest B"), Near(FVector3d(AtB.X, AtB.Y, AtB.Z), FVector3d(11.920089334391037, -6.078764238258697, 89.23321217869884)));
    TestTrue(TEXT("and how far"), Near(AtB.W, 0.6270859959294708));
    TestTrue(TEXT("the cell hash"), Near(WorldReliefNoise::CellHash(FVector3d(3.7, -2.2, 11.9)), 0.04057373922331579));

    // -- The surface terms, the file's own bands ------------------------------
    const FFaceTerms Terms = WorldReliefNoise::FaceF64(Known, 0.0, Offset, 1.0);
    TestTrue(TEXT("the continent at D"), Near(Terms.Continent, -0.051624444675196335));
    TestTrue(TEXT("the detail at D"), Near(Terms.Detail, 0.17097829632829997));
    TestTrue(TEXT("the detail's slope at D"), Near(Terms.DetailSlope, FVector3d(-0.8677208825485692, -1.1438504312418438, -0.5836500231613995)));
    TestTrue(TEXT("the craters' albedo at D"), Near(Terms.CraterAlbedo, 0.006394117987959831));
    TestTrue(TEXT("the craters' slope at D"), Near(Terms.CraterSlope, FVector3d(0.009846479798514362, 0.0012842534924583904, -0.018859280772458974)));
    const FFaceTerms Faded = WorldReliefNoise::FaceF64(Known, 1.0 / 768.0, Offset, 1.0);
    TestEqual(TEXT("a footprint of 1/768 fades every band at 768 or finer to nothing"), Faded.Detail, 0.0);
    TestEqual(TEXT("craters included"), Faded.CraterAlbedo, 0.0);
    return true;
}

bool FShaderMappingTest::RunTest(const FString& Parameters)
{
    const FString* Mapped = AllShaderSourceDirectoryMappings().Find(TEXT("/Project"));
    if (!TestNotNull(TEXT("DeepSpaceShaders mapped /Project at PostConfigInit"), Mapped))
    {
        return false;
    }
    TestEqual(TEXT("to this project's Shaders/"), FPaths::ConvertRelativePathToFull(*Mapped),
        FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"))));
    const FString Real = GetShaderSourceFilePath(SkyMaterial::WorldReliefInclude);
    TestTrue(FString::Printf(TEXT("and %s is a file (%s)"), SkyMaterial::WorldReliefInclude, *Real), FPaths::FileExists(Real));
    return true;
}

#endif
```

  (In the spike tables detail band 8 is 3,072 cycles, so a footprint of 1/768 fades it to
  `saturate(1 - 4) = 0`; crater band 104 is 768, faded to `saturate(1 - 1) = 0`.)

- [ ] **Step 4: Build; expect it not to compile.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh
```

Expected: FAIL: `'Surface/WorldRelief.h' file not found`.

- [ ] **Step 5: The C++ side.** Create `Source/DeepSpace/Surface/WorldRelief.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * A world's ground in C++, from Shaders/Private/WorldRelief.ush: the one noise
 * file the GPU compiles too (landing decision 1; ADR 0006, amended). Pure: no
 * UObject, no UWorld, no CVar.
 */

/** M_SkyBody's raw face terms at one direction and footprint, before any
 *  knob or clamp: what the shared file's WR_SurfaceTerms returns, and what
 *  M_SkyReliefProbe draws for Eyes.WorldReliefParity. */
struct FFaceTerms
{
    /** The coarse band, -1..1: continents and basins, or a giant's belts' wander. */
    double Continent = 0.0;

    /** The detail bands' values, each times its weight and its footprint fade. */
    double Detail = 0.0;

    /** Their gradient in noise space per unit of the stretched direction:
     *  the relief's slope before ds.Sky.Relief. */
    FVector3d DetailSlope = FVector3d::ZeroVector;

    /** The crater bands' albedo: darker floors, brighter rims. */
    double CraterAlbedo = 0.0;

    /** Their height's gradient, the same units. */
    FVector3d CraterSlope = FVector3d::ZeroVector;
};

/** The shared file's functions, reached from C++ in double unless named
 *  otherwise: for the tests that pin them, the parity test that holds them
 *  to the GPU, and the material contract that holds the JSON to their tables. */
namespace WorldReliefNoise
{
    /** Rand3DPCG16: 16 random bits in each of X, Y, Z. */
    DEEPSPACE_API FIntVector Hash16(int32 X, int32 Y, int32 Z);

    /** Simplex noise at V and its gradient (JacobianSimplex_ALU's first row). */
    DEEPSPACE_API double Simplex(const FVector3d& V, FVector3d& OutGradient);

    /** GradientNoise3D_ALU at V, untiled. */
    DEEPSPACE_API double GradientNoise(const FVector3d& V);

    /** VoronoiNoise3D_ALU at quality 1: the nearest site (xyz) and its distance (w). */
    DEEPSPACE_API FVector4d Voronoi(const FVector3d& V);

    /** The cell noise's first channel, 0..1. */
    DEEPSPACE_API double CellHash(const FVector3d& V);

    /** Every raw term at D, for a footprint in D units, a seed offset and a
     *  stretch: the file's entry point in double, and in float -- the
     *  float build performs the GPU's operations in the GPU's precision. */
    DEEPSPACE_API FFaceTerms FaceF64(const FVector3d& D, double FootprintD, const FVector3d& Offset, double Stretch);
    DEEPSPACE_API FFaceTerms FaceF32(const FVector3f& D, float FootprintD, const FVector3f& Offset, float Stretch);

    /** How near D lies to a crater's step, in cells, across every crater
     *  band the footprint has not faded: the least of the rim's distance
     *  (|F1 - radius|) and the bisector's (F2 - F1). A height or slope that
     *  steps there differs by a whole step for any rounding at all. */
    DEEPSPACE_API double CraterMargin(const FVector3d& D, double FootprintD, const FVector3d& Offset);

    /** The file's tables and constants, for DeepSpace.Sky.MaterialContract. */
    struct FBands
    {
        double ContinentFrequency = 0.0;
        int32 ContinentLevels = 0;
        double LevelScale = 0.0;
        TArray<double> DetailFrequencies;
        TArray<double> DetailWeights;
        TArray<int32> DetailIndices;
        TArray<double> CraterFrequencies;
        TArray<int32> CraterIndices;
        double CraterRadius = 0.0;
        double CraterInvRadius = 0.0;
        double CraterWall = 0.0;
        double CraterRimFall = 0.0;
        double CraterKeep = 0.0;
        double CraterFloorDark = 0.0;
        double CraterRimBright = 0.0;
    };
    DEEPSPACE_API FBands Bands();
}
```

  Create `Source/DeepSpace/Surface/WorldRelief.cpp`:

```cpp
#include "Surface/WorldRelief.h"

#include <cmath>

// The shared file, twice. WR_CPP selects its C++ half and WR_REAL its
// precision; each copy lives in its own namespace, so the two sets of WR_
// symbols never meet. The relative path is the file the GPU includes as
// /Project/Private/WorldRelief.ush: a change to it that C++ cannot compile
// fails ./build.sh.
#define WR_CPP 1
namespace WorldReliefF64
{
#define WR_REAL double
#include "../../../Shaders/Private/WorldRelief.ush"
#undef WR_REAL
}
namespace WorldReliefF32
{
#define WR_REAL float
#include "../../../Shaders/Private/WorldRelief.ush"
#undef WR_REAL
}
#undef WR_CPP

namespace WorldReliefLocal
{
    template <typename TTerms>
    FFaceTerms ToFaceTerms(const TTerms& Terms)
    {
        FFaceTerms Face;
        Face.Continent = Terms.Continent;
        Face.Detail = Terms.Detail;
        Face.DetailSlope = FVector3d(Terms.DetailSlopeX, Terms.DetailSlopeY, Terms.DetailSlopeZ);
        Face.CraterAlbedo = Terms.CraterAlbedo;
        Face.CraterSlope = FVector3d(Terms.CraterSlopeX, Terms.CraterSlopeY, Terms.CraterSlopeZ);
        return Face;
    }
}

FIntVector WorldReliefNoise::Hash16(int32 X, int32 Y, int32 Z)
{
    const WorldReliefF64::WR_Hash3 Hash = WorldReliefF64::WR_Rand3DPCG16(X, Y, Z);
    return FIntVector(static_cast<int32>(Hash.X), static_cast<int32>(Hash.Y), static_cast<int32>(Hash.Z));
}

double WorldReliefNoise::Simplex(const FVector3d& V, FVector3d& OutGradient)
{
    const WorldReliefF64::WR_Noise4 Noise = WorldReliefF64::WR_Simplex(V.X, V.Y, V.Z);
    OutGradient = FVector3d(Noise.GX, Noise.GY, Noise.GZ);
    return Noise.Value;
}

double WorldReliefNoise::GradientNoise(const FVector3d& V)
{
    return WorldReliefF64::WR_GradientNoise(V.X, V.Y, V.Z);
}

FVector4d WorldReliefNoise::Voronoi(const FVector3d& V)
{
    const WorldReliefF64::WR_Site Site = WorldReliefF64::WR_Voronoi(V.X, V.Y, V.Z);
    return FVector4d(Site.X, Site.Y, Site.Z, Site.Distance);
}

double WorldReliefNoise::CellHash(const FVector3d& V)
{
    return WorldReliefF64::WR_CellHash(V.X, V.Y, V.Z);
}

FFaceTerms WorldReliefNoise::FaceF64(const FVector3d& D, double FootprintD, const FVector3d& Offset, double Stretch)
{
    return WorldReliefLocal::ToFaceTerms(WorldReliefF64::WR_SurfaceTerms(D.X, D.Y, D.Z, FootprintD, Offset.X, Offset.Y, Offset.Z, Stretch));
}

FFaceTerms WorldReliefNoise::FaceF32(const FVector3f& D, float FootprintD, const FVector3f& Offset, float Stretch)
{
    return WorldReliefLocal::ToFaceTerms(WorldReliefF32::WR_SurfaceTerms(D.X, D.Y, D.Z, FootprintD, Offset.X, Offset.Y, Offset.Z, Stretch));
}

double WorldReliefNoise::CraterMargin(const FVector3d& D, double FootprintD, const FVector3d& Offset)
{
    double Margin = TNumericLimits<double>::Max();
    for (int32 Band = 0; Band < WR_CRATER_BANDS; ++Band)
    {
        const double Frequency = WorldReliefF64::WR_CRATER_FREQUENCY[Band];
        if (FootprintD * Frequency >= 1.0)
        {
            continue; // faded to nothing: no step to land on either side of
        }
        const int32 Index = WorldReliefF64::WR_CRATER_INDEX[Band];
        const FVector3d V = D * Frequency + Offset + FVector3d(37.0 * Index, 59.0 * Index, 83.0 * Index);
        const FVector3d Cell(FMath::Floor(V.X), FMath::Floor(V.Y), FMath::Floor(V.Z));
        double Nearest = TNumericLimits<double>::Max();
        double Second = TNumericLimits<double>::Max();
        for (int32 Corner = 0; Corner < 8; ++Corner)
        {
            const FVector3d Lattice = Cell + FVector3d(static_cast<double>(Corner & 1), static_cast<double>((Corner >> 1) & 1),
                static_cast<double>((Corner >> 2) & 1));
            const WorldReliefF64::WR_Vec3 Jitter = WorldReliefF64::WR_VoronoiJitter(Lattice.X, Lattice.Y, Lattice.Z);
            const double Distance = FVector3d::Dist(V, Lattice + FVector3d(Jitter.X, Jitter.Y, Jitter.Z));
            if (Distance < Nearest)
            {
                Second = Nearest;
                Nearest = Distance;
            }
            else if (Distance < Second)
            {
                Second = Distance;
            }
        }
        Margin = FMath::Min3(Margin, Second - Nearest, FMath::Abs(Nearest - WorldReliefF64::WR_CRATER_RADIUS));
    }
    return Margin;
}

WorldReliefNoise::FBands WorldReliefNoise::Bands()
{
    using namespace WorldReliefF64;
    FBands Out;
    Out.ContinentFrequency = WR_CONTINENT_FREQUENCY;
    Out.ContinentLevels = WR_CONTINENT_LEVELS;
    Out.LevelScale = WR_LEVEL_SCALE;
    for (int32 Band = 0; Band < WR_DETAIL_BANDS; ++Band)
    {
        Out.DetailFrequencies.Add(WR_DETAIL_FREQUENCY[Band]);
        Out.DetailWeights.Add(WR_DETAIL_WEIGHT[Band]);
        Out.DetailIndices.Add(WR_DETAIL_INDEX[Band]);
    }
    for (int32 Band = 0; Band < WR_CRATER_BANDS; ++Band)
    {
        Out.CraterFrequencies.Add(WR_CRATER_FREQUENCY[Band]);
        Out.CraterIndices.Add(WR_CRATER_INDEX[Band]);
    }
    Out.CraterRadius = WR_CRATER_RADIUS;
    Out.CraterInvRadius = WR_CRATER_INV_RADIUS;
    Out.CraterWall = WR_CRATER_WALL;
    Out.CraterRimFall = WR_CRATER_RIM_FALL;
    Out.CraterKeep = WR_CRATER_KEEP;
    Out.CraterFloorDark = WR_CRATER_FLOOR_DARK;
    Out.CraterRimBright = WR_CRATER_RIM_BRIGHT;
    return Out;
}
```

  In `SkyMaterialContract.h`, after `BodyMeshPath` add:

```cpp

    // The probes Eyes.WorldReliefParity draws (landing decision 1): the raw
    // face terms over a fixed patch at a fixed footprint, untonemapped.
    // M_SkyReliefProbe reaches them through the shared file;
    // M_SkyReliefProbeLegacy through the engine's noise nodes, as M_SkyBody
    // did before landing slice (a), and is deleted when slice (a) merges.
    inline const TCHAR* const ReliefProbePath = TEXT("/Game/Materials/Sky/M_SkyReliefProbe.M_SkyReliefProbe");
    inline const TCHAR* const ReliefProbeLegacyPath = TEXT("/Game/Materials/Sky/M_SkyReliefProbeLegacy.M_SkyReliefProbeLegacy");
    inline const FName ProbeFootprint = TEXT("ProbeFootprint"); // scalar: D units a pixel, times filter_pixels
    inline const FName ProbeSelect = TEXT("ProbeSelect");       // vector: one-hot, which terms the pixel carries
    inline const FName ProbeBias = TEXT("ProbeBias");           // vector: added to the pixel; the pipe check

    // The shared file as a Custom node reaches it: its include, the function
    // it calls, and its pins, in order.
    inline const TCHAR* const WorldReliefInclude = TEXT("/Project/Private/WorldRelief.ush");
    inline const TCHAR* const WorldReliefEntry = TEXT("WR_SurfaceTerms");
    inline TArray<FName> WorldReliefInputs() { return { TEXT("Direction"), TEXT("Footprint"), TEXT("SeedOffset"), TEXT("Stretch") }; }
    inline TArray<FName> WorldReliefOutputs() { return { TEXT("Continent"), TEXT("CraterAlbedo"), TEXT("CraterSlope") }; }
```

  and after `ParameterScalars()` add each probe's parameters (after `Banding` and
  `SurfaceSeed`, which they name):

```cpp
    inline TArray<FName> ProbeScalars() { return { Banding, ProbeFootprint }; }
    inline TArray<FName> ProbeVectors() { return { SurfaceSeed, ProbeSelect, ProbeBias }; }
```

- [ ] **Step 6: Build and run the headless tests; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh && ./test.sh DeepSpace.Surface
```

Expected: `passed: 2`. If `ShaderMapping` fails only because the mapping is missing under
`-nullrhi` (the engine's `AddShaderSourceDirectoryMapping` returns early when
`AllowShaderCompiling()` is false), move its three assertions into `Eyes.WorldReliefParity`
(Step 10) as its first block, delete `FShaderMappingTest`, and record that in the commit
message: the mapping is then proven where shaders compile. If the C++ build fails in the
`.ush`, that is the subset refusing C++: fix the file, never the includer.

- [ ] **Step 7: The contract, three sides.** In `Tools/sky_material_contract.json`:
  - in `"parameters"`, after `"body_axis_y"` add
    `"probe_footprint": { "name": "ProbeFootprint", "type": "scalar" },`,
    `"probe_select": { "name": "ProbeSelect", "type": "vector" },`,
    `"probe_bias": { "name": "ProbeBias", "type": "vector" },`
  - in `"materials"`, after `"M_SkyGlass"` add
    `"M_SkyReliefProbe": { "parameters": ["surface_seed", "banding", "probe_footprint", "probe_select", "probe_bias"] },` and
    `"M_SkyReliefProbeLegacy": { "parameters": ["surface_seed", "banding", "probe_footprint", "probe_select", "probe_bias"] }`
  - a new top-level key after `"materials"`:

```json
  "shared_relief": {
    "comment": "M_SkyBody's face as one Custom node over Shaders/Private/WorldRelief.ush, the file the C++ compiles too (landing decision 1). SkyMaterialContract.h holds the same include, entry and pins; DeepSpace.Sky.MaterialContract holds the built nodes to them, and the file's tables to this file's constants.",
    "include": "/Project/Private/WorldRelief.ush",
    "entry": "WR_SurfaceTerms",
    "inputs": ["Direction", "Footprint", "SeedOffset", "Stretch"],
    "outputs": [["Continent", "CMOT_FLOAT1"], ["CraterAlbedo", "CMOT_FLOAT1"], ["CraterSlope", "CMOT_FLOAT3"]]
  },
```

  - in `"constants"`, after `"veil_colour"` add:

```json
    "probe_comment": "Eyes.WorldReliefParity's probes: D over a fixed patch -- normalize(centre + (u - 0.5) span east + (v - 0.5) span north), the three an orthonormal frame -- and the bands the legacy probe draws, by band number (detail n is detail_frequencies[n - 1], crater n is crater_frequencies[n - 101]); the shared file must carry exactly these.",
    "probe_patch": { "centre": [0.6, -0.48, 0.64], "east": [0.8, 0.36, -0.48], "north": [0.0, 0.8, 0.6], "span": 0.5 },
    "probe_bands": { "detail": [8], "crater": [104] }
```

- [ ] **Step 8: The authoring script.** In `Tools/setup_sky_materials.py`:
  - after `import json` add `import collections`, and after `CONSTANTS = CONTRACT["constants"]` add:

```python
SHARED = CONTRACT["shared_relief"]

# Every band, by band number: what M_SkyBody draws.
EVERY_DETAIL = list(range(1, len(CONSTANTS["detail_frequencies"]) + 1))
EVERY_CRATER = list(range(101, 101 + len(CONSTANTS["crater_frequencies"])))

# The raw terms a face is composed from, whichever noise made them.
Terms = collections.namedtuple("Terms", "coarse fine crater_face crater_slope")

# The Custom node's body: one call into the shared file, its outputs pinned.
# HLSL only -- the file itself is the shared part.
SHARED_CODE = (
    "WR_Terms T = %s(Direction.x, Direction.y, Direction.z, Footprint, SeedOffset.x, SeedOffset.y, SeedOffset.z, Stretch);\n"
    "Continent = T.Continent;\n"
    "CraterAlbedo = T.CraterAlbedo;\n"
    "CraterSlope = float3(T.CraterSlopeX, T.CraterSlopeY, T.CraterSlopeZ);\n"
    "return float4(T.DetailSlopeX, T.DetailSlopeY, T.DetailSlopeZ, T.Detail);\n" % SHARED["entry"])
```

  - replace the whole of `def surface(g, knobs, seed, axes):` (its docstring through
    `return factor, slope, direction`) with:

```python
def band_offset(g, offset, index):
    """Each band from its own corner of the noise, so no band's features sit
    on the one below's and the octaves read as separate scales. The shared
    file adds the same (37, 59, 83) x band number."""
    return g.add(offset, g.colour((37.0 * index, 59.0 * index, 83.0 * index)))


def seed_offset(g, seed):
    """SurfaceSeed's xyz: where on the noise this world is."""
    offset = g.node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=True, a=False)
    g.link(seed, offset)
    return offset


def stretch_of(g, banding):
    """lerp(1, belt_stretch, Banding): how far a giant's detail is drawn out
    across its belts, so it streaks as cloud does."""
    stretch = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=1.0, const_b=float(CONSTANTS["belt_stretch"]))
    g.link(banding, stretch, "Alpha")
    return stretch


def stretch_axes(g, stretch):
    """(1, 1, stretch), which stretches a direction along the pole."""
    return g.binary(unreal.MaterialExpressionAppendVector,
                    g.node(unreal.MaterialExpressionConstant2Vector, r=1.0, g=1.0), stretch)


def body_direction(g, axes):
    """D, the unit direction to the pixel in the body's own axes --
    normalize(LocalPosition), turned by BodyAxisX/Y -- and the pixel's
    footprint on it, max(|ddx D|, |ddy D|) * filter_pixels. The mesh is
    unturned, so its object space has the world's axes."""
    position = g.node(unreal.MaterialExpressionLocalPosition)
    seen = g.node(unreal.MaterialExpressionNormalize)
    g.link(position, seen, "", output_name="XYZ")
    direction = to_body(g, axes, seen)
    ddx = g.unary(unreal.MaterialExpressionLength, g.unary(unreal.MaterialExpressionDDX, direction))
    ddy = g.unary(unreal.MaterialExpressionLength, g.unary(unreal.MaterialExpressionDDY, direction))
    footprint = g.mul(g.binary(unreal.MaterialExpressionMax, ddx, ddy), g.constant(CONSTANTS["filter_pixels"]))
    return direction, footprint


def legacy_terms(g, direction, footprint, seed, stretch, detail_bands, crater_bands):
    """The raw terms from the engine's own noise nodes, as M_SkyBody drew
    them before landing slice (a): the coarse band of GradientALU octaves,
    one VectorNoise GradientALU per detail band (rgb its gradient, a its
    value), one Voronoi per crater band. The bands are named by number, so
    the legacy probe can draw exactly the bands the shared file carries."""
    offset = seed_offset(g, seed)
    stretched = g.mul(direction, stretch_axes(g, stretch))
    coarse = noise_band(g, stretched, band_offset(g, offset, 0), CONSTANTS["continent_frequency"],
                        CONSTANTS["continent_levels"], footprint, stretch)
    width = g.mul(footprint, stretch)
    fine = None
    for index in detail_bands:
        frequency = CONSTANTS["detail_frequencies"][index - 1]
        weight = CONSTANTS["detail_weights"][index - 1]
        noise, faded = detail_band(g, stretched, band_offset(g, offset, index), frequency, width)
        term = g.mul(noise, g.mul(faded, g.constant(weight)))
        fine = term if fine is None else g.add(fine, term)
    crater_face = None
    crater_slope = None
    for index in crater_bands:
        frequency = CONSTANTS["crater_frequencies"][index - 101]
        albedo, slope = crater_band(g, direction, band_offset(g, offset, index), frequency, footprint)
        crater_face = albedo if crater_face is None else g.add(crater_face, albedo)
        crater_slope = slope if crater_slope is None else g.add(crater_slope, slope)
    return Terms(coarse, fine, crater_face, crater_slope)


def every_band(g, direction, footprint, seed, stretch):
    """The engine nodes, every band: M_SkyBody until landing task R4."""
    return legacy_terms(g, direction, footprint, seed, stretch, EVERY_DETAIL, EVERY_CRATER)


def shared_terms(g, direction, footprint, seed, stretch):
    """The same raw terms from Shaders/Private/WorldRelief.ush -- the file the
    C++ compiles too (landing decision 1) -- through one Custom node that
    includes it and calls its entry point. Every band the file carries."""
    custom = g.node(unreal.MaterialExpressionCustom)
    custom.set_editor_property("description", "WorldRelief")
    custom.set_editor_property("code", SHARED_CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("include_file_paths", [SHARED["include"]])
    pins = []
    for input_name in SHARED["inputs"]:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", input_name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    outputs = []
    for output_name, kind in SHARED["outputs"]:
        pin = unreal.CustomOutput()
        pin.set_editor_property("output_name", output_name)
        pin.set_editor_property("output_type", getattr(unreal.CustomMaterialOutputType, kind))
        outputs.append(pin)
    custom.set_editor_property("additional_outputs", outputs)
    for input_name, source in zip(SHARED["inputs"], (direction, footprint, seed_offset(g, seed), stretch)):
        g.link(source, custom, input_name)
    return Terms(mask(g, custom, "r", output_name="Continent"), custom,
                 mask(g, custom, "r", output_name="CraterAlbedo"),
                 mask(g, custom, "rgb", output_name="CraterSlope"))


def surface(g, knobs, seed, direction, footprint, terms):
    """The world's face and relief, composed from its raw terms: the face is
    the factor the shaded disc is multiplied by, 1 + swing, and the relief is
    the gradient of a height field the normal is tilted by.

        stretch   = lerp(1, belt_stretch, Banding)
        coarse, fine, craters = terms(D, footprint, SurfaceSeed, stretch)
        rocky     = clamp(coarse * continent_contrast, -1, 1)
        belts     = sin(pi * pairs * (D.z + belt_warp * coarse)) * belt fade
        craters   = Cratering * lerp(maria_cratering, 1, highland) * crater terms
        face      = lerp(rocky, belts, Banding) * Mottle + fine.a * Detail + craters.albedo
        factor    = 1 + clamp(face, -max_swing, max_swing)
        slope     = Relief * lerp(1, relief_giant, Banding) * fine.rgb * (1, 1, stretch)
                  + craters.slope

    terms is legacy_terms' engine nodes or shared_terms' file; the
    composition is the same either way, so the face is the terms' alone.
    Rock gets basins and highlands with craters, fewer in the basins; a giant
    gets belts wandered by the same coarse noise and a third of rock's relief.
    The face is centred on zero, so the disc keeps its flux on average, and
    the clamp is the half-float guard."""
    mottle, detail, banding, relief, cratering = knobs
    stretch = stretch_of(g, banding)
    t = terms(g, direction, footprint, seed, stretch)

    rocky = g.node(unreal.MaterialExpressionClamp, min_default=-1.0, max_default=1.0)
    g.link(g.mul(t.coarse, g.constant(CONSTANTS["continent_contrast"])), rocky, "")

    # The seed's w, a giant's belt pairs, from the parameter's own alpha pin:
    # its default output is only the colour's three channels.
    pairs = g.node(unreal.MaterialExpressionMultiply, const_b=1.0)
    g.link(seed, pairs, "A", output_name="A")
    latitude = g.node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
    g.link(direction, latitude)
    wandered = g.add(latitude, g.mul(t.coarse, g.constant(CONSTANTS["belt_warp"])))
    # Sine with period 2 is sin(pi x).
    belts = g.node(unreal.MaterialExpressionSine, period=2.0)
    g.link(g.mul(wandered, pairs), belts)
    belt_fade = g.unary(unreal.MaterialExpressionSaturate,
                        g.unary(unreal.MaterialExpressionOneMinus, g.mul(footprint, pairs)))
    banded = g.mul(belts, belt_fade)

    kind = g.node(unreal.MaterialExpressionLinearInterpolate)
    g.link(rocky, kind, "A")
    g.link(banded, kind, "B")
    g.link(banding, kind, "Alpha")
    face = g.mul(kind, mottle)
    face = g.add(face, g.mul(mask(g, t.fine, "a"), detail))

    highland = g.unary(unreal.MaterialExpressionSaturate, g.add(g.mul(rocky, g.constant(0.5)), g.constant(0.5)))
    marked = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=float(CONSTANTS["maria_cratering"]), const_b=1.0)
    g.link(highland, marked, "Alpha")
    crater_gain = g.mul(cratering, marked)
    face = g.add(face, g.mul(t.crater_face, crater_gain))

    swing = float(CONSTANTS["surface_max_swing"])
    bounded = g.node(unreal.MaterialExpressionClamp, min_default=-swing, max_default=swing)
    g.link(face, bounded, "")
    factor = g.add(bounded, g.constant(1.0))

    cloud = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=1.0, const_b=float(CONSTANTS["relief_giant"]))
    g.link(banding, cloud, "Alpha")
    slope = g.add(g.mul(g.mul(mask(g, t.fine, "rgb"), stretch_axes(g, stretch)), g.mul(relief, cloud)),
                  g.mul(t.crater_slope, crater_gain))
    return factor, slope


def probe_direction(g):
    """D over the contract's fixed patch, from the render target's UV:
    normalize(centre + (u - 0.5) span east + (v - 0.5) span north)."""
    patch = CONSTANTS["probe_patch"]
    uv = g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    across = g.mul(g.add(mask(g, uv, "r"), g.constant(-0.5)), g.constant(patch["span"]))
    up = g.mul(g.add(mask(g, uv, "g"), g.constant(-0.5)), g.constant(patch["span"]))
    point = g.add(g.add(g.colour(patch["centre"]), g.mul(g.colour(patch["east"]), across)),
                  g.mul(g.colour(patch["north"]), up))
    return g.unary(unreal.MaterialExpressionNormalize, point)


def relief_probe(asset, terms):
    """A probe for Eyes.WorldReliefParity: the raw terms over the fixed patch
    at the footprint ProbeFootprint says, untonemapped (landing decision 1).

        pixel = ProbeBias.rgb + ProbeSelect.r * (coarse, fine.a, crater albedo)
                              + ProbeSelect.g * fine.rgb
                              + ProbeSelect.b * crater slope
                              + ProbeSelect.a * D

    One-hot selection multiplies by exactly 1 or 0, which float keeps exactly,
    so each pass reads back one set of terms unaltered. Selecting nothing
    draws the bias alone: the test's check of the pipe itself."""
    material = fresh_material(asset)
    g = Graph(material)
    seed = g.vector("surface_seed", (0.0, 0.0, 0.0, 0.0))
    banding = g.scalar("banding", 0.0)
    footprint = g.scalar("probe_footprint", 0.0)
    select = g.vector("probe_select", (1.0, 0.0, 0.0, 0.0))
    bias = g.vector("probe_bias", (0.0, 0.0, 0.0, 0.0))
    direction = probe_direction(g)
    t = terms(g, direction, footprint, seed, stretch_of(g, banding))
    first = g.binary(unreal.MaterialExpressionAppendVector,
                     g.binary(unreal.MaterialExpressionAppendVector, t.coarse, mask(g, t.fine, "a")), t.crater_face)
    passes = (first, mask(g, t.fine, "rgb"), t.crater_slope, direction)

    def selected(channel):
        # The alpha is its own pin: the default output is the colour's three.
        return mask(g, select, "r", output_name="A") if channel == "a" else mask(g, select, channel)

    out = mask(g, bias, "rgb")
    for channel, value in zip("rgba", passes):
        out = g.add(out, g.mul(value, selected(channel)))
    g.emissive(out)
    finish(material, asset)
```

  - in `sky_body()`, replace

```python
    axes = body_axes(g)
    factor, slope, direction = surface(g, knobs, seed, axes)
    normal = relief_normal(g, direction, slope, axes)
```

  with

```python
    axes = body_axes(g)
    direction, footprint = body_direction(g, axes)
    factor, slope = surface(g, knobs, seed, direction, footprint, every_band)
    normal = relief_normal(g, direction, slope, axes)
```

  - in `main()`, after `sky_glass(collections)` add:

```python
    bands = CONSTANTS["probe_bands"]
    relief_probe("M_SkyReliefProbe", shared_terms)
    relief_probe("M_SkyReliefProbeLegacy",
                 lambda g, d, fp, s, st: legacy_terms(g, d, fp, s, st, bands["detail"], bands["crater"]))
```

  The refactor keeps `M_SkyBody`'s arithmetic node for node (the only new nodes are a
  second `(1, 1, stretch)` append); `DeepSpace.Sky.MaterialContract`'s existing graph
  checks are the proof (Step 11).

- [ ] **Step 9: `Tools/eyes.sh` and the mutation runner.** Create `Tools/eyes.sh`:

```bash
#!/usr/bin/env bash
# Run one rendered check -- an Eyes.* test -- and print the verdict.
#
#   Tools/eyes.sh Eyes.WorldReliefParity
#
# Eyes.* tests render, so they are named outside DeepSpace. and ./test.sh
# never runs them: under -nullrhi there is no GPU. This runs one the way it
# must be run -- through the machine-wide lock (Tools/ue_lock.sh),
# -RenderOffScreen, never -nullrhi -- and reads the verdict from the log as
# ./test.sh does. Exits non-zero unless at least one test ran and none failed.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
FILTER="${1:?usage: Tools/eyes.sh Eyes.<Name>}"
LOG="$ROOT/Saved/Logs/DeepSpace.log"
[[ $FILTER == Eyes.* ]] || { echo "Tools/eyes.sh runs Eyes.* checks; the DeepSpace suite is ./test.sh"; exit 2; }

# shellcheck source=Tools/ue_lock.sh
. "$ROOT/Tools/ue_lock.sh"

ue_locked "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$ROOT/DeepSpace.uproject" \
    -ExecCmds="Automation RunTests $FILTER" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nosplash -NoLiveCoding -RenderOffScreen -FORCELOGFLUSH >/dev/null 2>&1

pass=$(grep -c "Test Completed. Result={Success}" "$LOG" 2>/dev/null || true)
fail=$(grep -E "Test Completed. Result=\{(Fail|Error)" "$LOG" 2>/dev/null || true)
echo "passed: ${pass:-0}"
if [[ -n $fail ]]; then
    echo "FAILED:"
    echo "$fail"
    grep -E "Error: |LogAutomationController: Error" "$LOG" | head -40
    exit 1
fi
[[ ${pass:-0} -gt 0 ]] || { echo "no tests ran -- check $LOG"; exit 1; }
```

```bash
chmod +x /home/matt/Development/deepspace/.worktrees/landing-a-relief/Tools/eyes.sh
```

  In `Tools/mutate.sh`, replace `if ./test.sh "$FILTER" > Saved/mutant-test.log 2>&1; then` with

```bash
# MUTATE_RUNNER=Tools/eyes.sh proves a rendered check (Eyes.*) the same way.
if "${MUTATE_RUNNER:-./test.sh}" "$FILTER" > Saved/mutant-test.log 2>&1; then
```

  and in its header comment, after the usage line `#   Tools/mutate.sh FILE 'exact old text' 'new text' TESTFILTER`, add
  `#   MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh FILE OLD NEW Eyes.<Name>   (a rendered check)`.

- [ ] **Step 10: The rendered test.** Create `Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp`:

```cpp
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Sky/SkyMaterialContract.h"
#include "Surface/WorldRelief.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * A GUARD, not a look (landing decision 1). Named outside DeepSpace. so
 * ./test.sh never runs it: it renders, so it runs through the lock with
 * -RenderOffScreen and never -nullrhi,
 *
 *   Tools/eyes.sh Eyes.WorldReliefParity
 *
 * and slice (a)'s and (b)'s done-when run it. It draws M_SkyReliefProbe --
 * the shared file's raw face terms over a fixed patch at a fixed footprint,
 * untonemapped -- into a 32-bit float target, reads it back, and holds it
 *
 *   - to M_SkyReliefProbeLegacy, the same terms from the engine's own noise
 *     nodes: "the orbital look unchanged", as a number;
 *   - to the C++, at the very D the GPU drew;
 *
 * to a maximum absolute difference of 1e-3, over 256 x 256 samples at each
 * of five footprints. A crater band's albedo and slope step at its rim and
 * at the bisector between two sites, where any rounding at all can change
 * the side a sample lands on: samples within 2e-3 cells of a step are left
 * out, and no more than 1% may be.
 *
 * It must compile the Custom node to draw anything, so it also catches what
 * the headless suite cannot see: an HLSL error in WorldRelief.ush ships the
 * grey default material with every test green. Its first check is the pipe:
 * a probe selecting nothing reads back its bias, exactly, at every pixel.
 *
 * Writes Saved/Eyes/WorldReliefParity/report.txt: every gap, and two
 * diagnostics reported but not asserted -- the file's float build against
 * the GPU, and the C++ against the engine's nodes -- which tell a file that
 * computes the wrong thing from float's own floor (landing task R1's
 * verdict table).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWorldReliefParityTest,
    "Eyes.WorldReliefParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WorldReliefParityLocal
{
    constexpr int32 Side = 256;
    constexpr double Tolerance = 1.0e-3;
    constexpr double StepMarginCells = 2.0e-3;
    constexpr double MaxLeftOut = 0.01;

    /** D units a pixel, times filter_pixels: from continents alone down to
     *  every band coarser than 1/12288 of the radius. */
    const double Footprints[] = { 1.0 / 12.0, 1.0 / 96.0, 1.0 / 768.0, 1.0 / 3072.0, 1.0 / 12288.0 };

    /** A barren world's seed offset until task R4 hands the test Baemsekai
     *  IV's: multiples of 1/256, as every real one is. */
    const FVector3d Offset(12.5, 200.25, 77.0);

    enum class EPass : int32 { Terms = 0, DetailSlope = 1, CraterSlope = 2, Direction = 3 };

    FLinearColor Selecting(EPass Pass)
    {
        FLinearColor Select(0.0f, 0.0f, 0.0f, 0.0f);
        Select.Component(static_cast<int32>(Pass)) = 1.0f;
        return Select;
    }

    TArray<FVector3d> Draw(UWorld* World, UTextureRenderTarget2D* Target, UMaterialInstanceDynamic* Probe,
                           const FLinearColor& Select, const FLinearColor& Bias)
    {
        Probe->SetVectorParameterValue(SkyMaterial::ProbeSelect, Select);
        Probe->SetVectorParameterValue(SkyMaterial::ProbeBias, Bias);
        if (GShaderCompilingManager)
        {
            GShaderCompilingManager->FinishAllCompilation();
        }
        UKismetRenderingLibrary::DrawMaterialToRenderTarget(World, Target, Probe);
        FlushRenderingCommands();
        TArray<FLinearColor> Pixels;
        Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels, FReadSurfaceDataFlags(RCM_MinMax, CubeFace_MAX));
        TArray<FVector3d> Out;
        Out.Reserve(Pixels.Num());
        for (const FLinearColor& Pixel : Pixels)
        {
            Out.Emplace(Pixel.R, Pixel.G, Pixel.B);
        }
        return Out;
    }

    /** One probe at one footprint: its terms, and the D it drew them at. */
    struct FDrawn
    {
        TArray<FVector3d> Terms;
        TArray<FVector3d> DetailSlope;
        TArray<FVector3d> CraterSlope;
        TArray<FVector3d> Direction;

        FFaceTerms At(int32 Index) const
        {
            FFaceTerms Face;
            Face.Continent = Terms[Index].X;
            Face.Detail = Terms[Index].Y;
            Face.CraterAlbedo = Terms[Index].Z;
            Face.DetailSlope = DetailSlope[Index];
            Face.CraterSlope = CraterSlope[Index];
            return Face;
        }
    };

    FDrawn DrawAll(UWorld* World, UTextureRenderTarget2D* Target, UMaterialInstanceDynamic* Probe)
    {
        const FLinearColor NoBias(0.0f, 0.0f, 0.0f, 0.0f);
        FDrawn Drawn;
        Drawn.Terms = Draw(World, Target, Probe, Selecting(EPass::Terms), NoBias);
        Drawn.DetailSlope = Draw(World, Target, Probe, Selecting(EPass::DetailSlope), NoBias);
        Drawn.CraterSlope = Draw(World, Target, Probe, Selecting(EPass::CraterSlope), NoBias);
        Drawn.Direction = Draw(World, Target, Probe, Selecting(EPass::Direction), NoBias);
        return Drawn;
    }

    /** The largest absolute difference seen, term by term. */
    struct FGap
    {
        double Continent = 0.0;
        double Detail = 0.0;
        double CraterAlbedo = 0.0;
        double DetailSlope = 0.0;
        double CraterSlope = 0.0;

        void Widen(const FFaceTerms& A, const FFaceTerms& B)
        {
            Continent = FMath::Max(Continent, FMath::Abs(A.Continent - B.Continent));
            Detail = FMath::Max(Detail, FMath::Abs(A.Detail - B.Detail));
            CraterAlbedo = FMath::Max(CraterAlbedo, FMath::Abs(A.CraterAlbedo - B.CraterAlbedo));
            DetailSlope = FMath::Max(DetailSlope, (A.DetailSlope - B.DetailSlope).GetAbsMax());
            CraterSlope = FMath::Max(CraterSlope, (A.CraterSlope - B.CraterSlope).GetAbsMax());
        }

        double Worst() const
        {
            return FMath::Max(FMath::Max3(Continent, Detail, CraterAlbedo), FMath::Max(DetailSlope, CraterSlope));
        }

        FString Describe() const
        {
            return FString::Printf(TEXT("continent %.2e, detail %.2e, crater albedo %.2e, detail slope %.2e, crater slope %.2e"),
                Continent, Detail, CraterAlbedo, DetailSlope, CraterSlope);
        }
    };
}

bool FWorldReliefParityTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefParityLocal;

    if (GUsingNullRHI)
    {
        AddError(TEXT("Eyes.WorldReliefParity renders: run it with Tools/eyes.sh, never -nullrhi"));
        return false;
    }

    SkyTestWorld::FSkyWorld Test(TEXT("WorldReliefParityWorld"));
    UMaterial* Shared = LoadObject<UMaterial>(nullptr, SkyMaterial::ReliefProbePath);
    UMaterial* Legacy = LoadObject<UMaterial>(nullptr, SkyMaterial::ReliefProbeLegacyPath);
    if (!TestNotNull(TEXT("M_SkyReliefProbe is built (Tools/setup_sky_materials.py)"), Shared)
        || !TestNotNull(TEXT("M_SkyReliefProbeLegacy is built"), Legacy))
    {
        return false;
    }

    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Test.World);
    Target->RenderTargetFormat = RTF_RGBA32f;
    Target->ClearColor = FLinearColor::Transparent;
    Target->bAutoGenerateMips = false;
    Target->InitAutoFormat(Side, Side);
    Target->UpdateResourceImmediate(true);
    UMaterialInstanceDynamic* NewProbe = UMaterialInstanceDynamic::Create(Shared, Test.World);
    UMaterialInstanceDynamic* OldProbe = UMaterialInstanceDynamic::Create(Legacy, Test.World);

    TArray<FString> Report;

    // -- The pipe: a probe that selects nothing draws its bias, exactly -------
    // Signed, above one, and below a float's half-precision: a target that
    // clamped, rounded, tonemapped or drew the grey default fails here.
    {
        const FLinearColor Known(-0.375f, 1234.5f, 3.0e-5f, 0.0f);
        const TArray<FVector3d> Drawn = Draw(Test.World, Target, NewProbe, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), Known);
        int32 Wrong = 0;
        for (const FVector3d& Pixel : Drawn)
        {
            Wrong += (Pixel.X == Known.R && Pixel.Y == Known.G && Pixel.Z == Known.B) ? 0 : 1;
        }
        const bool bWhole = TestEqual(TEXT("the target holds every pixel"), Drawn.Num(), Side * Side);
        if (!TestEqual(TEXT("and hands back what the probe drew, signed and unrounded, at every one"), Wrong, 0) || !bWhole)
        {
            return false;
        }
    }

    double WorstNewOld = 0.0;
    double WorstCppNew = 0.0;
    double WorstFloatNew = 0.0;
    double WorstCppOld = 0.0;
    double MostLeftOut = 0.0;

    // Barren (stretch 1) against both the engine's nodes and the C++; a
    // giant (stretch 6, the belts' streaking) against the engine's nodes,
    // and the C++ reported only: a giant has no ground.
    struct FWorldCase { const TCHAR* Name; float Banding; double Stretch; bool bHoldCpp; };
    const FWorldCase Worlds[] = { { TEXT("barren"), 0.0f, 1.0, true }, { TEXT("giant"), 1.0f, 6.0, false } };
    for (const FWorldCase& World : Worlds)
    {
        for (UMaterialInstanceDynamic* Probe : { NewProbe, OldProbe })
        {
            Probe->SetVectorParameterValue(SkyMaterial::SurfaceSeed,
                FLinearColor(static_cast<float>(Offset.X), static_cast<float>(Offset.Y), static_cast<float>(Offset.Z), 8.0f));
            Probe->SetScalarParameterValue(SkyMaterial::Banding, World.Banding);
        }
        for (const double FootprintD : Footprints)
        {
            const float Footprint = static_cast<float>(FootprintD);
            NewProbe->SetScalarParameterValue(SkyMaterial::ProbeFootprint, Footprint);
            OldProbe->SetScalarParameterValue(SkyMaterial::ProbeFootprint, Footprint);
            const FDrawn New = DrawAll(Test.World, Target, NewProbe);
            const FDrawn Old = DrawAll(Test.World, Target, OldProbe);

            FGap NewVsOld;
            FGap CppVsNew;
            FGap FloatVsNew;
            FGap CppVsOld;
            int32 LeftOut = 0;
            int32 Unmatched = 0;
            for (int32 Index = 0; Index < Side * Side; ++Index)
            {
                const FVector3d& D = New.Direction[Index];
                Unmatched += D == Old.Direction[Index] ? 0 : 1;
                if (WorldReliefNoise::CraterMargin(D, Footprint, Offset) < StepMarginCells)
                {
                    ++LeftOut;
                    continue;
                }
                const FFaceTerms Gpu = New.At(Index);
                const FFaceTerms Engine = Old.At(Index);
                const FFaceTerms Cpp = WorldReliefNoise::FaceF64(D, Footprint, Offset, World.Stretch);
                const FFaceTerms Float = WorldReliefNoise::FaceF32(FVector3f(D), Footprint, FVector3f(Offset), static_cast<float>(World.Stretch));
                NewVsOld.Widen(Gpu, Engine);
                CppVsNew.Widen(Cpp, Gpu);
                FloatVsNew.Widen(Float, Gpu);
                CppVsOld.Widen(Cpp, Engine);
            }
            const double LeftOutShare = static_cast<double>(LeftOut) / (Side * Side);
            const FString At = FString::Printf(TEXT("%s, footprint 1/%.0f"), World.Name, 1.0 / FootprintD);
            TestEqual(At + TEXT(": both probes drew the same directions"), Unmatched, 0);
            TestTrue(FString::Printf(TEXT("%s: at most 1%% of samples lie on a crater's step (%.3f%%)"), *At, 100.0 * LeftOutShare),
                LeftOutShare <= MaxLeftOut);
            TestTrue(FString::Printf(TEXT("%s: the shared file draws what the engine's nodes drew (%s)"), *At, *NewVsOld.Describe()),
                NewVsOld.Worst() <= Tolerance);
            if (World.bHoldCpp)
            {
                TestTrue(FString::Printf(TEXT("%s: the C++ computes what the GPU drew (%s)"), *At, *CppVsNew.Describe()),
                    CppVsNew.Worst() <= Tolerance);
                WorstCppNew = FMath::Max(WorstCppNew, CppVsNew.Worst());
                WorstFloatNew = FMath::Max(WorstFloatNew, FloatVsNew.Worst());
                WorstCppOld = FMath::Max(WorstCppOld, CppVsOld.Worst());
            }
            WorstNewOld = FMath::Max(WorstNewOld, NewVsOld.Worst());
            MostLeftOut = FMath::Max(MostLeftOut, LeftOutShare);
            Report.Add(FString::Printf(TEXT("%s: %d compared, %d left out"), *At, Side * Side - LeftOut, LeftOut));
            Report.Add(TEXT("  shared file vs engine nodes:        ") + NewVsOld.Describe());
            Report.Add(TEXT("  C++ (double) vs shared file:        ") + CppVsNew.Describe());
            Report.Add(TEXT("  C++ (float build) vs shared file:   ") + FloatVsNew.Describe());
            Report.Add(TEXT("  C++ (double) vs engine nodes:       ") + CppVsOld.Describe());
        }
    }

    Report.Add(FString::Printf(TEXT("SUMMARY shared-vs-engine %.2e, C++-vs-shared %.2e, float-C++-vs-shared %.2e, C++-vs-engine %.2e, left out at most %.3f%%"),
        WorstNewOld, WorstCppNew, WorstFloatNew, WorstCppOld, 100.0 * MostLeftOut));
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes") / TEXT("WorldReliefParity");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *(Dir / TEXT("report.txt")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    AddInfo(Report.Last());
    return true;
}

#endif
```

- [ ] **Step 11: The material contract test.** In `SkyMaterialContractTest.cpp`:
  - add includes `#include "Materials/MaterialExpressionCustom.h"` and `#include "Surface/WorldRelief.h"`;
  - in `Roles()`, after the `body_axis_y` row add
    `{ TEXT("probe_footprint"), SkyMaterial::ProbeFootprint, TEXT("scalar") },`,
    `{ TEXT("probe_select"), SkyMaterial::ProbeSelect, TEXT("vector") },`,
    `{ TEXT("probe_bias"), SkyMaterial::ProbeBias, TEXT("vector") },`;
  - in the anonymous-namespace helpers, after `ScaledBy` (so before `CheckSurfaceFace`, which
    task R4 makes call it), add:

```cpp
    /** The shared file as a material reaches it (landing decision 1): one
     *  Custom node that includes WorldRelief.ush and calls its entry point,
     *  its pins the file's, and no engine noise anywhere -- a band left on an
     *  engine node is a band the C++ does not have. Returns the node. */
    const UMaterialExpressionCustom* CheckSharedRelief(FAutomationTestBase& Test, const UMaterial& Material)
    {
        const FString Asset = Material.GetName();
        TArray<const UMaterialExpressionCustom*> Customs;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material.GetExpressions())
        {
            if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression.Get()))
            {
                Customs.Add(Custom);
            }
            const bool bEngineNoise = Cast<UMaterialExpressionNoise>(Expression.Get()) || Cast<UMaterialExpressionVectorNoise>(Expression.Get());
            Test.TestFalse(FString::Printf(TEXT("%s draws no band on an engine noise node (%s)"), *Asset, *Expression->GetName()), bEngineNoise);
        }
        if (!Test.TestEqual(FString::Printf(TEXT("%s reaches the shared file through one Custom node"), *Asset), Customs.Num(), 1))
        {
            return nullptr;
        }
        const UMaterialExpressionCustom* Node = Customs[0];
        Test.TestTrue(FString::Printf(TEXT("%s's Custom node includes %s, and only it"), *Asset, SkyMaterial::WorldReliefInclude),
            Node->IncludeFilePaths.Num() == 1 && Node->IncludeFilePaths[0] == SkyMaterial::WorldReliefInclude);
        Test.TestTrue(FString::Printf(TEXT("and calls %s"), SkyMaterial::WorldReliefEntry),
            Node->Code.Contains(FString(SkyMaterial::WorldReliefEntry) + TEXT("(")));
        TArray<FName> Inputs;
        for (const FCustomInput& Input : Node->Inputs)
        {
            Inputs.Add(Input.InputName);
            Test.TestNotNull(FString::Printf(TEXT("%s's %s is wired"), *Asset, *Input.InputName.ToString()), Input.Input.Expression);
        }
        Test.TestTrue(TEXT("its inputs are the file's, in order"), Inputs == SkyMaterial::WorldReliefInputs());
        TArray<FName> Outputs;
        for (const FCustomOutput& Output : Node->AdditionalOutputs)
        {
            Outputs.Add(Output.OutputName);
        }
        Test.TestTrue(TEXT("and so are its outputs"), Outputs == SkyMaterial::WorldReliefOutputs());
        return Node;
    }

    /** The JSON's shared_relief and band constants against the header and
     *  against the shared file's own tables: the file is the source, the
     *  JSON the list the legacy graph and the docs are built from. */
    void CheckSharedTables(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Contract)
    {
        const TSharedPtr<FJsonObject> Shared = Contract->GetObjectField(TEXT("shared_relief"));
        Test.TestEqual(TEXT("the JSON's include is the header's"), Shared->GetStringField(TEXT("include")), FString(SkyMaterial::WorldReliefInclude));
        Test.TestEqual(TEXT("and its entry point"), Shared->GetStringField(TEXT("entry")), FString(SkyMaterial::WorldReliefEntry));
        TArray<FName> Inputs;
        for (const TSharedPtr<FJsonValue>& Value : Shared->GetArrayField(TEXT("inputs")))
        {
            Inputs.Add(FName(*Value->AsString()));
        }
        Test.TestTrue(TEXT("and its inputs"), Inputs == SkyMaterial::WorldReliefInputs());
        TArray<FName> Outputs;
        for (const TSharedPtr<FJsonValue>& Value : Shared->GetArrayField(TEXT("outputs")))
        {
            Outputs.Add(FName(*Value->AsArray()[0]->AsString()));
        }
        Test.TestTrue(TEXT("and its outputs"), Outputs == SkyMaterial::WorldReliefOutputs());

        const TSharedPtr<FJsonObject> Constants = Contract->GetObjectField(TEXT("constants"));
        const WorldReliefNoise::FBands Bands = WorldReliefNoise::Bands();
        Test.TestEqual(TEXT("the file's coarse band is the contract's"), Bands.ContinentFrequency, Constants->GetNumberField(TEXT("continent_frequency")));
        Test.TestEqual(TEXT("with its octaves"), Bands.ContinentLevels, static_cast<int32>(Constants->GetNumberField(TEXT("continent_levels"))));
        Test.TestEqual(TEXT("at its step"), Bands.LevelScale, Constants->GetNumberField(TEXT("level_scale")));

        const TSharedPtr<FJsonObject> Probe = Constants->GetObjectField(TEXT("probe_bands"));
        TArray<int32> ProbeDetail;
        for (const TSharedPtr<FJsonValue>& Value : Probe->GetArrayField(TEXT("detail")))
        {
            ProbeDetail.Add(static_cast<int32>(Value->AsNumber()));
        }
        TArray<int32> ProbeCrater;
        for (const TSharedPtr<FJsonValue>& Value : Probe->GetArrayField(TEXT("crater")))
        {
            ProbeCrater.Add(static_cast<int32>(Value->AsNumber()));
        }
        Test.TestTrue(TEXT("the shared file carries exactly the probes' detail bands"), Bands.DetailIndices == ProbeDetail);
        Test.TestTrue(TEXT("and exactly their crater bands"), Bands.CraterIndices == ProbeCrater);

        const TArray<TSharedPtr<FJsonValue>>& Frequencies = Constants->GetArrayField(TEXT("detail_frequencies"));
        const TArray<TSharedPtr<FJsonValue>>& Weights = Constants->GetArrayField(TEXT("detail_weights"));
        for (int32 Band = 0; Band < Bands.DetailIndices.Num(); ++Band)
        {
            const int32 Number = Bands.DetailIndices[Band];
            if (Test.TestTrue(FString::Printf(TEXT("detail band %d is one the contract has"), Number), Frequencies.IsValidIndex(Number - 1)))
            {
                Test.TestEqual(FString::Printf(TEXT("detail band %d's frequency"), Number), Bands.DetailFrequencies[Band], Frequencies[Number - 1]->AsNumber());
                Test.TestEqual(FString::Printf(TEXT("detail band %d's weight"), Number), Bands.DetailWeights[Band], Weights[Number - 1]->AsNumber());
            }
        }
        const TArray<TSharedPtr<FJsonValue>>& CraterFrequencies = Constants->GetArrayField(TEXT("crater_frequencies"));
        for (int32 Band = 0; Band < Bands.CraterIndices.Num(); ++Band)
        {
            const int32 Number = Bands.CraterIndices[Band];
            if (Test.TestTrue(FString::Printf(TEXT("crater band %d is one the contract has"), Number), CraterFrequencies.IsValidIndex(Number - 101)))
            {
                Test.TestEqual(FString::Printf(TEXT("crater band %d's frequency"), Number), Bands.CraterFrequencies[Band], CraterFrequencies[Number - 101]->AsNumber());
            }
        }
        const double Radius = Constants->GetNumberField(TEXT("crater_radius"));
        const double Depth = Constants->GetNumberField(TEXT("crater_depth"));
        const double Rim = Constants->GetNumberField(TEXT("crater_rim"));
        Test.TestEqual(TEXT("a crater's radius"), Bands.CraterRadius, Radius);
        Test.TestEqual(TEXT("its reciprocal, as the graph computed it"), Bands.CraterInvRadius, 1.0 / Radius);
        Test.TestEqual(TEXT("its bowl's slope, 2 x depth"), Bands.CraterWall, 2.0 * Depth);
        Test.TestEqual(TEXT("its rim's fall, -4 x depth x rim"), Bands.CraterRimFall, -4.0 * Depth * Rim);
        Test.TestEqual(TEXT("the share of sites kept"), Bands.CraterKeep, Constants->GetNumberField(TEXT("crater_keep")));
        Test.TestEqual(TEXT("the floor's darkening"), Bands.CraterFloorDark, Constants->GetNumberField(TEXT("crater_floor_dark")));
        Test.TestEqual(TEXT("the rim's brightening"), Bands.CraterRimBright, Constants->GetNumberField(TEXT("crater_rim_bright")));
    }
```

  - in `RunTest`, in the `Materials` list after the `M_SkyGlass` row add

```cpp
        { TEXT("M_SkyReliefProbe"), SkyMaterial::ReliefProbePath, SkyMaterial::ProbeScalars(), SkyMaterial::ProbeVectors() },
        { TEXT("M_SkyReliefProbeLegacy"), SkyMaterial::ReliefProbeLegacyPath, SkyMaterial::ProbeScalars(), SkyMaterial::ProbeVectors() },
```

  - in the per-material loop, after the `M_SkyGlass` block add

```cpp
        if (Material->GetFName() == TEXT("M_SkyReliefProbe"))
        {
            CheckSharedRelief(*this, *Material);
        }
```

  - after the per-material loop (before the `// MPC_Sky:` block) add `CheckSharedTables(*this, Contract);`.

- [ ] **Step 12: Build; run the contract test before authoring; expect FAIL.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh && ./test.sh DeepSpace.Sky.MaterialContract
```

Expected: FAIL: `M_SkyReliefProbe is built (Tools/setup_sky_materials.py)` (the assets do not exist yet).

- [ ] **Step 13: Author the materials (editor closed, through the lock).**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && . Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" -unattended -nopause -nosplash -NoLiveCoding; tail -5 Saved/setup_sky_materials.txt
```

Expected: the report ends `ok`, with lines for `M_SkyReliefProbe` and `M_SkyReliefProbeLegacy`
listing scalars `['Banding', 'ProbeFootprint']`, vectors `['ProbeBias', 'ProbeSelect', 'SurfaceSeed']`.
If `could not connect ... -> Custom...` is raised, the Custom node's pins were not rebuilt by
`set_editor_property`: call `custom.post_edit_change()` after setting `inputs` and
`additional_outputs` (it exists on every `UObject` in the editor's Python) and re-run.

- [ ] **Step 14: Run the headless suite; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./test.sh DeepSpace.Sky.MaterialContract && ./test.sh
```

Expected: `DeepSpace.Sky.MaterialContract` passes -- its unchanged `M_SkyBody` graph checks
prove the refactor kept `M_SkyBody` node for node, `M_SkyReliefProbe` translates with its
include (the module's mapping in the commandlet), the tables match. Then the full suite
green.

- [ ] **Step 15: Commit the machinery (before the verdict, so the mutations can run).**

```bash
git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief add Source/DeepSpaceShaders DeepSpace.uproject Source/DeepSpace.Target.cs Source/DeepSpaceEditor.Target.cs launch.sh rebuild.sh Shaders/Private/WorldRelief.ush Source/DeepSpace/Surface/WorldRelief.h Source/DeepSpace/Surface/WorldRelief.cpp Source/DeepSpace/Sky/SkyMaterialContract.h Tools/sky_material_contract.json Tools/setup_sky_materials.py Tools/eyes.sh Tools/mutate.sh Source/DeepSpace/Tests/WorldReliefTest.cpp Source/DeepSpace/Tests/SkyMaterialContractTest.cpp Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp Content/Materials/Sky/M_SkyReliefProbe.uasset Content/Materials/Sky/M_SkyReliefProbeLegacy.uasset Content/Materials/Sky/M_SkyBody.uasset
git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief commit -F - <<'MSG'
spike(relief): the shared WorldRelief.ush, compiled into C++ and a Custom node, and its parity probe

One detail band (8), one crater band (104) and the coarse band, written once
in a subset that is both HLSL and C++ and included twice: into
WorldRelief.cpp (double, and float as the GPU's mirror) and, through the
/Project path the new DeepSpaceShaders module maps at PostConfigInit, into
M_SkyReliefProbe's Custom node. M_SkyReliefProbeLegacy draws the same bands
from the engine's nodes. Eyes.WorldReliefParity (Tools/eyes.sh) renders both
into a float target and holds them, and the C++, to 1e-3.

M_SkyBody is re-authored by a refactored script with the same arithmetic;
DeepSpace.Sky.MaterialContract's graph checks pass unchanged.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 16: Run the rendered test and read the verdict.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/eyes.sh Eyes.WorldReliefParity; cat Saved/Eyes/WorldReliefParity/report.txt
```

  Read the verdict off the table above, from the report's per-footprint lines and its
  `SUMMARY` line, and record it in the test's header, where the next reader will look. Set
  `VERDICT` to the table's row -- `GO`, `NO-GO: module`, `NO-GO: subset` or `FLOAT FLOOR`:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && VERDICT="GO" python3 - <<'PY2'
import os
path = "Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp"
summary = [l.strip() for l in open("Saved/Eyes/WorldReliefParity/report.txt") if l.startswith("SUMMARY")][-1]
text = open(path).read()
anchor = " * verdict table).\n */"
assert text.count(anchor) == 1
text = text.replace(anchor, " * verdict table).\n *\n * Spike verdict (landing R1): %s -- %s\n */" % (os.environ["VERDICT"], summary))
open(path, "w").write(text)
PY2
```

  On anything but GO, stop here: commit that change (`git ... commit -m "spike(relief): <VERDICT>"`
  with the report's lines, as in Step 18), and hand the verdict to the orchestrator. The
  fallbacks below are taken only on its word.

- [ ] **Step 17 (GO): Prove the tests can fail.** Each is killed:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/mutate.sh Shaders/Private/WorldRelief.ush 'WR_UINT X = WR_UINT(PX) * 1664525u + 1013904223u;' 'WR_UINT X = WR_UINT(PX) * 1664525u + 1013904224u;' DeepSpace.Surface.WorldRelief.KnownValues
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/mutate.sh Source/DeepSpaceShaders/Private/DeepSpaceShadersModule.cpp 'AddShaderSourceDirectoryMapping(TEXT("/Project"),' 'AddShaderSourceDirectoryMapping(TEXT("/Projekt"),' DeepSpace.Surface.ShaderMapping
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Shaders/Private/WorldRelief.ush 'NX * Inv * WR_VORONOI_JITTER, NY' 'NX * Inv * WR_REAL(0.2600), NY' Eyes.WorldReliefParity
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Source/DeepSpace/Surface/WorldRelief.cpp 'WorldReliefF64::WR_SurfaceTerms(D.X, D.Y, D.Z, FootprintD,' 'WorldReliefF64::WR_SurfaceTerms(D.X, D.Y, D.Z, FootprintD * 0.5,' Eyes.WorldReliefParity
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh
```

  The third mutation is the one that matters most: it edits only the shared file, which moves
  the C++ and the GPU together, so it is killed only by the shared file against the engine's
  nodes -- and only if the GPU recompiled the Custom node for the edited `.ush`. Killed, it
  proves both. (The ShaderMapping mutation leaves `/Project` unmapped, so every probe
  material fails to translate too; that is expected.)

- [ ] **Step 18 (GO): CLAUDE.md, and commit the verdict.** In CLAUDE.md, *Commands*, in the
  first ```` ```bash ```` block, after `unreal-editor DeepSpace.uproject    # open the project`
  add:

```bash
Tools/eyes.sh Eyes.WorldReliefParity   # a rendered check (Eyes.*): outside ./test.sh, through the lock, never -nullrhi
```

  and in the paragraph beginning **Prove a test can fail with `Tools/mutate.sh`**, after its
  first sentence add: `A rendered check is proven the same way with
  MUTATE_RUNNER=Tools/eyes.sh.`

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && git add CLAUDE.md Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp && {
  echo "spike(relief): GO -- the shared file draws what the engine's nodes drew, and the C++ computes it"
  echo
  grep -E "^SUMMARY|^barren" Saved/Eyes/WorldReliefParity/report.txt
  echo
  echo "Mutations killed: the hash's LCG constant (KnownValues), the /Project path"
  echo "(ShaderMapping), the Voronoi jitter in the shared file (parity: the GPU"
  echo "recompiled the Custom node for the edited .ush), the C++ footprint (parity)."
  echo
  echo "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
} | git commit -F -
```

---

## Task 7 (R1-F1, only on NO-GO: the module): the file's text pasted into the Custom node

Taken only if the orchestrator confirms the module verdict. The C++ side is unchanged: it
still includes the file. The GPU gets the same text, generated into the node by the script.

**Files:** delete `Source/DeepSpaceShaders/`; modify `DeepSpace.uproject`, both
`*.Target.cs`, `launch.sh`, `rebuild.sh` back to one module; modify
`Shaders/Private/WorldRelief.ush`, `Tools/setup_sky_materials.py`,
`Tools/sky_material_contract.json`, `Sky/SkyMaterialContract.h`,
`Tests/SkyMaterialContractTest.cpp`, `Tests/WorldReliefTest.cpp` (delete `FShaderMappingTest`).

- [ ] **Step 1: Make the file embeddable.** A Custom node's body is the inside of one
  function, so the pasted text must be a struct of methods (DXC accepts methods in structs;
  static members it does not). In the `.ush`: move every `struct WR_...` type above a new line
  `WR_LIB_BEGIN`, add `WR_LIB_END` at the end of the file, turn each `static const` table into
  a method returning from a local `const` array (`WR_REAL WR_DetailFrequency(int Band) { const WR_REAL Table[WR_DETAIL_BANDS] = { ... }; return Table[Band]; }`),
  and each scalar constant into a `#define`. In the C++ half, `#define WR_LIB_BEGIN` and
  `#define WR_LIB_END` as empty. In the HLSL half, `#define WR_LIB_BEGIN struct WR_Lib {` and
  `#define WR_LIB_END };`.
- [ ] **Step 2: Paste.** `shared_terms` stops setting `include_file_paths` and sets `code` to
  the file's text (read from `Shaders/Private/WorldRelief.ush` at authoring time, with the
  `#pragma once` line dropped) followed by `WR_Lib L;` and `SHARED_CODE` with every `WR_`
  call prefixed `L.`. The JSON's `shared_relief.include` becomes `"pasted"`; the header's
  `WorldReliefInclude` likewise; `CheckSharedRelief` asserts `IncludeFilePaths` is empty and
  that `Node->Code` contains the SHA-1 of the file (the script appends `// WorldRelief.ush sha1 <hex>`
  as the code's last line, and the test hashes the file with `FSHA1::HashBuffer`), so a
  regeneration that is not re-run is a red test.
- [ ] **Step 3:** re-run Task R1 Steps 13-18 unchanged (the mutation of the `.ush` now needs
  the script re-run to reach the GPU: run Step 13 inside the mutation by hand -- edit, author,
  `Tools/eyes.sh`, restore, author -- and record it).

## Task 8 (R1-F2, only on NO-GO: the subset): a stop-and-replan point, not an executable task

**This is not executed from this plan.** Like the FLOAT FLOOR row, the subset verdict stops track R:
the orchestrator takes R1's report to the developer (sign-off item 1's third alternative), and on
their word this task, R3 (Task 10), R5 (Task 12) and slice (b)'s shader tasks (31, 32, 37) are
re-planned with full code -- the renamed probe's contract entries, the parity test against
`FaceF64`, the include moves -- before any of them runs. What follows is the outline that re-plan
starts from, so the decision can be made knowing what it costs; none of it is a step to take.

Outline: `M_SkyBody` keeps `every_band` (Task R4 is skipped); the Custom-node probe and the module
are deleted; the `.ush` stays, compiled into C++ only, as the port.

- **Outline 1:** delete `Source/DeepSpaceShaders/`, the module's lines in
  `DeepSpace.uproject`, both targets, `launch.sh`, `rebuild.sh`; delete `M_SkyReliefProbe`
  and its contract entries; delete `FShaderMappingTest`; move `WorldRelief.ush` to
  `Source/DeepSpace/Surface/WorldReliefCore.inl` (it is C++ only now) and change the two
  includes.
- **Outline 2:** `Eyes.WorldReliefParity` compares `WorldReliefNoise::FaceF64` with
  `M_SkyReliefProbeLegacy` (renamed `M_SkyReliefProbe`, and kept: it is now the only probe,
  and slice (b) draws `M_SkyGround` against it), same footprints, mask and tolerance.
- **Outline 3:** ADR 0006's amendment (Task R5) says instead: an independent C++ port held to
  the engine's nodes by the rendered test, and why the shared file failed (the report).

---

## Task 9 (R2): Every band in the shared file; parity at full

**Files:**
- Modify: `Shaders/Private/WorldRelief.ush` (the two `#define`s per half, the band tables)
- Modify: `Tools/sky_material_contract.json` (`constants.probe_bands`)
- Modify: `Source/DeepSpace/Tests/WorldReliefTest.cpp` (`KnownValues`: the full terms)
- Modify: `Source/DeepSpace/Tests/SkyMaterialContractTest.cpp` (`CheckSharedTables`: every band)
- Assets: `M_SkyReliefProbeLegacy.uasset` (re-authored with every band); `M_SkyReliefProbe.uasset`
  (its graph is unchanged -- the bands live in the `.ush` -- but `setup_sky_materials.py` rebuilds
  every material it owns, so the authoring run re-saves it and it is committed with the run)

**Interfaces:**
- Consumes: R1's machinery.
- Produces: the `.ush` at full bands (`WR_DETAIL_BANDS` 12, indices 1..12; `WR_CRATER_BANDS` 6, indices 101..106).

- [ ] **Step 1: Write the failing tests.** In `WorldReliefTest.cpp`, `KnownValues`, replace the
  block from `const FFaceTerms Terms = WorldReliefNoise::FaceF64(Known, 0.0, Offset, 1.0);` through
  `TestEqual(TEXT("craters included"), Faded.CraterAlbedo, 0.0);` with:

```cpp
    const FFaceTerms Terms = WorldReliefNoise::FaceF64(Known, 0.0, Offset, 1.0);
    TestTrue(TEXT("the continent at D"), Near(Terms.Continent, -0.051624444675196335));
    TestTrue(TEXT("every detail band at D"), Near(Terms.Detail, -0.37006419577067096));
    TestTrue(TEXT("their slope at D"), Near(Terms.DetailSlope, FVector3d(-1.1190180843380462, -2.0437699869510126, -2.957997344868701)));
    TestTrue(TEXT("every crater band's albedo at D"), Near(Terms.CraterAlbedo, 0.1646827666094558));
    TestTrue(TEXT("and slope"), Near(Terms.CraterSlope, FVector3d(0.10226657986438999, 0.029989050621318493, 0.27960324776032125)));
    const FFaceTerms Faded = WorldReliefNoise::FaceF64(Known, 1.0 / 768.0, Offset, 1.0);
    TestTrue(TEXT("at a footprint of 1/768: the continent"), Near(Faded.Continent, -0.052906497740322966));
    TestTrue(TEXT("the detail"), Near(Faded.Detail, -0.028110410395840155));
    TestTrue(TEXT("its slope"), Near(Faded.DetailSlope, FVector3d(-0.18906468101433271, 0.3914639024500385, -2.955562449468286)));
    TestTrue(TEXT("the craters' albedo"), Near(Faded.CraterAlbedo, 0.015437686430484467));
    TestTrue(TEXT("and slope"), Near(Faded.CraterSlope, FVector3d(0.02537765447267675, -0.04054271510388996, 0.01898122575896894)));
    const FFaceTerms Giant = WorldReliefNoise::FaceF64(Known, 1.0 / 768.0, Offset, 6.0);
    TestTrue(TEXT("a giant's stretched continent"), Near(Giant.Continent, 0.2988758606069588));
    TestTrue(TEXT("its stretched detail"), Near(Giant.Detail, -0.08159535029992765));
    TestTrue(TEXT("and slope"), Near(Giant.DetailSlope, FVector3d(-0.6215217207242866, 0.5170705707138437, 0.4500212151402112)));
    TestTrue(TEXT("craters are never stretched"), Near(Giant.CraterAlbedo, Faded.CraterAlbedo) && Near(Giant.CraterSlope, Faded.CraterSlope));
```

  In `SkyMaterialContractTest.cpp`, `CheckSharedTables`, after the two `probe_bands` equality
  checks add:

```cpp
        TArray<int32> EveryDetail;
        for (int32 Number = 1; Number <= Constants->GetArrayField(TEXT("detail_frequencies")).Num(); ++Number)
        {
            EveryDetail.Add(Number);
        }
        TArray<int32> EveryCrater;
        for (int32 Number = 101; Number < 101 + Constants->GetArrayField(TEXT("crater_frequencies")).Num(); ++Number)
        {
            EveryCrater.Add(Number);
        }
        Test.TestTrue(TEXT("the shared file carries every detail band the contract has, in order"), Bands.DetailIndices == EveryDetail);
        Test.TestTrue(TEXT("and every crater band"), Bands.CraterIndices == EveryCrater);
```

- [ ] **Step 2: Build and run; expect FAIL.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh && ./test.sh DeepSpace.Surface.WorldRelief.KnownValues; ./test.sh DeepSpace.Sky.MaterialContract
```

Expected: both FAIL (`every detail band at D`; `the shared file carries every detail band`).

- [ ] **Step 3: The full tables.** In the `.ush`, in **both** halves replace
  `#define WR_DETAIL_BANDS 1` with `#define WR_DETAIL_BANDS 12` and `#define WR_CRATER_BANDS 1`
  with `#define WR_CRATER_BANDS 6`. Replace the comment line
  `// SPIKE (landing slice (a), task R1): one detail band and one crater band.` with
  `// Every band M_SkyBody draws.` Replace the five table lines

```hlsl
static const WR_REAL WR_DETAIL_FREQUENCY[WR_DETAIL_BANDS] = { WR_REAL(3072.0) };
static const WR_REAL WR_DETAIL_WEIGHT[WR_DETAIL_BANDS] = { WR_REAL(0.5) };
static const int WR_DETAIL_INDEX[WR_DETAIL_BANDS] = { 8 };
static const WR_REAL WR_CRATER_FREQUENCY[WR_CRATER_BANDS] = { WR_REAL(768.0) };
static const int WR_CRATER_INDEX[WR_CRATER_BANDS] = { 104 };
```

  with

```hlsl
static const WR_REAL WR_DETAIL_FREQUENCY[WR_DETAIL_BANDS] = {
    WR_REAL(24.0), WR_REAL(48.0), WR_REAL(96.0), WR_REAL(192.0), WR_REAL(384.0), WR_REAL(768.0),
    WR_REAL(1536.0), WR_REAL(3072.0), WR_REAL(6144.0), WR_REAL(12288.0), WR_REAL(24576.0), WR_REAL(49152.0) };
static const WR_REAL WR_DETAIL_WEIGHT[WR_DETAIL_BANDS] = {
    WR_REAL(0.5), WR_REAL(0.5), WR_REAL(0.5), WR_REAL(0.5), WR_REAL(0.5), WR_REAL(0.5),
    WR_REAL(0.5), WR_REAL(0.5), WR_REAL(0.5), WR_REAL(0.5), WR_REAL(0.5), WR_REAL(0.5) };
static const int WR_DETAIL_INDEX[WR_DETAIL_BANDS] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
static const WR_REAL WR_CRATER_FREQUENCY[WR_CRATER_BANDS] = {
    WR_REAL(12.0), WR_REAL(48.0), WR_REAL(192.0), WR_REAL(768.0), WR_REAL(3072.0), WR_REAL(12288.0) };
static const int WR_CRATER_INDEX[WR_CRATER_BANDS] = { 101, 102, 103, 104, 105, 106 };
```

  In the JSON replace `"probe_bands": { "detail": [8], "crater": [104] }` with
  `"probe_bands": { "detail": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12], "crater": [101, 102, 103, 104, 105, 106] }`.

- [ ] **Step 4: Build, re-author, run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh && ./test.sh DeepSpace.Surface
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && . Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" -unattended -nopause -nosplash -NoLiveCoding; tail -3 Saved/setup_sky_materials.txt
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./test.sh && Tools/eyes.sh Eyes.WorldReliefParity; cat Saved/Eyes/WorldReliefParity/report.txt
```

Expected: `DeepSpace.Surface.*` and the full suite pass; `Eyes.WorldReliefParity` passes at
every footprint with every band. If it passes at the spike's bands but fails here only at the
finest footprints with the float diagnostics as large as the gap, that is the FLOAT FLOOR row of
R1's table: stop and escalate with the report.

- [ ] **Step 5: Commit.**

```bash
git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief add Shaders/Private/WorldRelief.ush Tools/sky_material_contract.json Source/DeepSpace/Tests/WorldReliefTest.cpp Source/DeepSpace/Tests/SkyMaterialContractTest.cpp Content/Materials/Sky/M_SkyReliefProbeLegacy.uasset Content/Materials/Sky/M_SkyReliefProbe.uasset
git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief commit -F - <<'MSG'
feat(relief): every band in the shared file -- twelve detail, six crater -- and parity at full

The legacy probe draws every band too; Eyes.WorldReliefParity holds the
shared file to the engine's nodes and the C++ to the GPU at all five
footprints with every band. KnownValues pins the full terms, a giant's
stretched ones included.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 6: Prove it.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/mutate.sh Shaders/Private/WorldRelief.ush 'WR_REAL(24576.0)' 'WR_REAL(24575.0)' DeepSpace.Surface.WorldRelief.KnownValues
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Shaders/Private/WorldRelief.ush 'WR_REAL(24576.0)' 'WR_REAL(24575.0)' Eyes.WorldReliefParity
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh
```

Expected: `KILLED` twice.

---

## Task 10 (R3): `FWorldRelief`, pure

Needs track P merged (`Surface/WorldReliefParams.h`, `FSkyBody::Relief`). Rebase first:
`git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief rebase main`, then `./build.sh`.

**Files:**
- Modify: `Source/DeepSpace/Surface/WorldRelief.h` (include; bounds; `FWorldRelief`)
- Modify: `Source/DeepSpace/Surface/WorldRelief.cpp` (`FWorldRelief`'s members, at the end)
- Test: `Source/DeepSpace/Tests/WorldReliefTest.cpp` (`KnownValues` extended; `.Deterministic`, `.SeedMatchesSky`, `.Bounds`, `.Gradient`, `.BandLimit`, `.Footprint`, `.SlopeBound`)

**Interfaces:**
- Consumes: `FWorldReliefParams`, `EGround` (P1); `FSkyBody::Relief`, `SkyLook::SurfaceOffset` (P4); `ShipSky::SurfaceSeed`; the `.ush`'s `WR_DetailBand` and tables (R1/R2).
- Produces: `class FWorldRelief` (signatures in *The seam*); `WorldReliefNoise::SimplexValueBound`, `SimplexGradientBound`.

- [ ] **Step 1: Write the failing tests.** In `WorldReliefTest.cpp`:
  - add includes `#include "Math/RandomStream.h"`, `#include "Sky/ShipSky.h"`, `#include "Sky/SkySystem.h"`,
    `#include "Universe/GalaxyGenerator.h"`, `#include "Universe/ProcGenPriorsConfig.h"`, `#include "Universe/StarSystemGenerator.h"`;
  - after the two existing `IMPLEMENT_SIMPLE_AUTOMATION_TEST`s add:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefDeterministicTest, "DeepSpace.Surface.WorldRelief.Deterministic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefSeedMatchesSkyTest, "DeepSpace.Surface.WorldRelief.SeedMatchesSky",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefBoundsTest, "DeepSpace.Surface.WorldRelief.Bounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefGradientTest, "DeepSpace.Surface.WorldRelief.Gradient",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefBandLimitTest, "DeepSpace.Surface.WorldRelief.BandLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefFootprintTest, "DeepSpace.Surface.WorldRelief.Footprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefSlopeBoundTest, "DeepSpace.Surface.WorldRelief.SlopeBound",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

  - inside `namespace WorldReliefTestLocal`, after `Near(const FVector3d&, ...)`, add:

```cpp
    /** An Earth-sized world with a 6 km peak, at the known values' offset. */
    FWorldReliefParams Earthlike()
    {
        FWorldReliefParams Params;
        Params.SeedOffset = Offset;
        Params.RadiusCm = 6.3781e8;
        Params.PeakCm = 6.0e5;
        Params.Cratering = 1.0;
        Params.Ground = EGround::Solid;
        return Params;
    }

    /** Directions over the sphere from a fixed stream: a test's sampling, not
     *  a world's draw, so uniform is the right distribution for once. */
    TArray<FVector3d> Directions(int32 Count, int32 Seed)
    {
        FRandomStream Stream(Seed);
        TArray<FVector3d> Out;
        Out.Reserve(Count);
        for (int32 Sample = 0; Sample < Count; ++Sample)
        {
            Out.Add(FVector3d(Stream.GetUnitVector()));
        }
        return Out;
    }

    /** A unit vector square to D. */
    FVector3d Tangent(const FVector3d& D)
    {
        return FVector3d::CrossProduct(D, FMath::Abs(D.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX()).GetSafeNormal();
    }
```

  - at the end of `FWorldReliefKnownValuesTest::RunTest`, before `return true;`, add:

```cpp

    // -- The height: every detail band the GPU draws and the four finer the
    //    C++ alone carries on an Earth, over the proven bound, at 6 km --------
    const FWorldRelief Relief(Earthlike());
    FVector3d HeightGradient;
    TestTrue(TEXT("the height at D"), FMath::Abs(Relief.HeightAndGradient(Known, HeightGradient) - (-39163.893022613505)) < 1.0e-4);
    TestTrue(TEXT("and its gradient in D"),
        (HeightGradient - FVector3d(-1868268.5494285857, -1201146.9951713625, -10114520.605642153)).GetAbsMax() < 1.0e-2);
    TestTrue(TEXT("the height at D with a footprint of R/768"), FMath::Abs(Relief.Height(Known, 6.3781e8 / 768.0) - (-37499.20596158043)) < 1.0e-4);
```

  - after `FShaderMappingTest::RunTest` (or at its place, if Step 6 of R1 moved it) add:

```cpp
bool FWorldReliefDeterministicTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief One(Earthlike());
    const FWorldRelief Two(Earthlike());
    FWorldReliefParams Elsewhere = Earthlike();
    Elsewhere.SeedOffset += FVector3d(1.0 / 256.0, 0.0, 0.0);
    const FWorldRelief Other(Elsewhere);
    int32 Mismatched = 0;
    int32 Moved = 0;
    for (const FVector3d& D : Directions(1000, 7))
    {
        Mismatched += (One.Height(D) == Two.Height(D) && One.Height(D, 1.0e5) == Two.Height(D, 1.0e5)) ? 0 : 1;
        Moved += One.Height(D) != Other.Height(D) ? 1 : 0;
    }
    TestEqual(TEXT("one world's params make one ground, bit for bit"), Mismatched, 0);
    TestTrue(FString::Printf(TEXT("and the next seed offset another ground (%d of 1,000 differ)"), Moved), Moved >= 990);
    return true;
}

bool FWorldReliefSeedMatchesSkyTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();
    const FGalaxyGenerator Galaxy(20260925, Priors);
    const FStarSystem Home = FStarSystemGenerator::Generate(Galaxy.GenerateSector(FInt64Vector(-1, -1, 0))[0], Priors);
    const FSkySystem Sky = FSkySystem::FromSystem(Home, {});
    for (int32 Index = 1; Index < Sky.Bodies.Num(); ++Index)
    {
        const FSkyBody& Body = Sky.Bodies[Index];
        const FLinearColor Gpu = ShipSky::SurfaceSeed(Body.SurfaceSeed, Body.BeltPairs);
        const FVector3d Drawn(Gpu.R, Gpu.G, Gpu.B);
        const FWorldRelief Relief(Body.Relief);
        TestTrue(FString::Printf(TEXT("%s's ground is taken from the noise its face is: the float M_SkyBody gets, exactly"), *Body.Id.ToString()),
            Relief.GetParams().SeedOffset == Drawn);
        TestEqual(FString::Printf(TEXT("%s's Face is the terms at that offset"), *Body.Id.ToString()),
            Relief.Face(Known, 0.0).Detail, WorldReliefNoise::FaceF64(Known, 0.0, Drawn, 1.0).Detail);
    }
    return true;
}

bool FWorldReliefBoundsTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief Relief(Earthlike());
    TestEqual(TEXT("MaxHeightCm is exactly the peak"), Relief.MaxHeightCm(), 6.0e5);
    TestEqual(TEXT("and MinHeightCm its negative"), Relief.MinHeightCm(), -6.0e5);
    int32 Outside = 0;
    double Highest = 0.0;
    double NoiseHighest = 0.0;
    double NoiseSteepest = 0.0;
    for (const FVector3d& D : Directions(100000, 11))
    {
        const double Height = Relief.Height(D);
        Outside += (Height > Relief.MaxHeightCm() || Height < Relief.MinHeightCm()) ? 1 : 0;
        Highest = FMath::Max(Highest, FMath::Abs(Height));
        FVector3d NoiseGradient;
        NoiseHighest = FMath::Max(NoiseHighest, FMath::Abs(WorldReliefNoise::Simplex(D * 97.0, NoiseGradient)));
        NoiseSteepest = FMath::Max(NoiseSteepest, NoiseGradient.Size());
    }
    TestEqual(TEXT("no sample of 100,000 lies outside [MinHeightCm, MaxHeightCm]"), Outside, 0);
    TestTrue(TEXT("the simplex never exceeds its value bound"), NoiseHighest <= WorldReliefNoise::SimplexValueBound);
    TestTrue(TEXT("nor its gradient bound"), NoiseSteepest <= WorldReliefNoise::SimplexGradientBound);
    AddInfo(FString::Printf(TEXT("highest of 100,000: %.3f of the peak; the simplex reached %.4f of %.4f and %.3f of %.3f (proven bounds, not tight)"),
        Highest / Relief.MaxHeightCm(), NoiseHighest, WorldReliefNoise::SimplexValueBound, NoiseSteepest, WorldReliefNoise::SimplexGradientBound));

    FWorldReliefParams Ocean = Earthlike();
    Ocean.PeakCm = 0.0;
    Ocean.Ground = EGround::None;
    const FWorldRelief Flat(Ocean);
    int32 Raised = 0;
    for (const FVector3d& D : Directions(1000, 13))
    {
        Raised += Flat.Height(D) == 0.0 ? 0 : 1;
    }
    TestEqual(TEXT("an ocean's ground is its datum everywhere"), Raised, 0);
    TestEqual(TEXT("with no height"), Flat.MaxHeightCm(), 0.0);
    TestEqual(TEXT("and no slope"), Flat.MaxSlope(), 0.0);
    return true;
}

bool FWorldReliefGradientTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief Relief(Earthlike());
    constexpr double Step = 1.0e-9;
    int32 Wrong = 0;
    for (const double FootprintCm : { 0.0, 6.3781e8 / 3072.0 })
    {
        for (const FVector3d& D : Directions(1000, 17))
        {
            FVector3d Gradient;
            Relief.HeightAndGradient(D, Gradient, FootprintCm);
            const FVector3d Across = Tangent(D);
            for (const FVector3d& Along : { Across, FVector3d::CrossProduct(D, Across) })
            {
                const double Difference = (Relief.Height(D + Along * Step, FootprintCm) - Relief.Height(D - Along * Step, FootprintCm)) / (2.0 * Step);
                const double Analytic = FVector3d::DotProduct(Gradient, Along);
                Wrong += FMath::Abs(Difference - Analytic) <= 1.0e-4 * Gradient.Size() + 1.0 ? 0 : 1;
            }
        }
    }
    TestEqual(TEXT("the analytic gradient is the height's own, every band and every fade"), Wrong, 0);
    return true;
}

bool FWorldReliefBandLimitTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief Earth(Earthlike());
    const TConstArrayView<double> Bands = Earth.GetDetailFrequencies();
    TestEqual(TEXT("an Earth carries the GPU's twelve bands and four finer"), Bands.Num(), 16);
    TestTrue(FString::Printf(TEXT("nothing finer than 5 m (%.1f cm)"), Earth.FinestWavelengthCm()), Earth.FinestWavelengthCm() >= FWorldRelief::BandLimitCm);
    TestTrue(TEXT("and as fine as it may go: one octave more would be under 5 m"),
        Earth.GetParams().RadiusCm / (2.0 * Bands.Last()) < FWorldRelief::BandLimitCm);
    for (int32 Band = 12; Band < Bands.Num(); ++Band)
    {
        TestEqual(FString::Printf(TEXT("finer band %d is an octave above the last"), Band + 1), Bands[Band], 2.0 * Bands[Band - 1]);
    }
    FWorldReliefParams Small = Earthlike();
    Small.RadiusCm = 6.3781e8 * FMath::Pow(0.05, 0.28);
    const FWorldRelief Light(Small);
    TestEqual(TEXT("the smallest rocky world procgen makes carries three finer"), Light.GetDetailFrequencies().Num(), 15);
    TestTrue(TEXT("and nothing finer than 5 m either"), Light.FinestWavelengthCm() >= FWorldRelief::BandLimitCm);
    return true;
}

bool FWorldReliefFootprintTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief Relief(Earthlike());
    const double Radius = Relief.GetParams().RadiusCm;
    TestEqual(TEXT("a footprint of 0 omits nothing"), Relief.OmittedBoundCm(0.0), 0.0);
    int32 Beyond = 0;
    for (const double FootprintCm : { 1000.0, Radius / 12288.0, Radius / 768.0, Radius / 24.0 })
    {
        for (const FVector3d& D : Directions(10000, 19))
        {
            const double Removed = FMath::Abs(Relief.Height(D) - Relief.Height(D, FootprintCm));
            Beyond += Removed <= Relief.OmittedBoundCm(FootprintCm) + 1.0e-9 ? 0 : 1;
        }
    }
    TestEqual(TEXT("what a footprint removes never exceeds OmittedBoundCm"), Beyond, 0);
    int32 Left = 0;
    for (const FVector3d& D : Directions(1000, 23))
    {
        Left += Relief.Height(D, Radius / 24.0) == 0.0 ? 0 : 1;
    }
    TestEqual(TEXT("a footprint of R/24 fades every band away, to the datum"), Left, 0);
    TestTrue(TEXT("and then OmittedBoundCm is the whole peak"),
        FMath::IsNearlyEqual(Relief.OmittedBoundCm(Radius / 24.0), Relief.MaxHeightCm(), 1.0e-9 * Relief.MaxHeightCm()));
    return true;
}

bool FWorldReliefSlopeBoundTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief Relief(Earthlike());
    int32 Steeper = 0;
    double Steepest = 0.0;
    for (const FVector3d& D : Directions(100000, 29))
    {
        FVector3d Gradient;
        Relief.HeightAndGradient(D, Gradient);
        const FVector3d AlongGround = Gradient - FVector3d::DotProduct(Gradient, D) * D;
        const double Slope = AlongGround.Size() / Relief.GetParams().RadiusCm;
        Steeper += Slope <= Relief.MaxSlope() ? 0 : 1;
        Steepest = FMath::Max(Steepest, Slope);
    }
    TestEqual(TEXT("no sample of 100,000 is steeper than MaxSlope"), Steeper, 0);
    AddInfo(FString::Printf(TEXT("steepest %.4f against the bound %.4f (cm per cm)"), Steepest, Relief.MaxSlope()));
    return true;
}
```

- [ ] **Step 2: Build; expect it not to compile.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh
```

Expected: FAIL: `unknown type name 'FWorldRelief'`, `no member named 'SimplexValueBound'`.

- [ ] **Step 3: The class.** In `WorldRelief.h` add `#include "Surface/WorldReliefParams.h"` after
  `#include "CoreMinimal.h"`. Inside `namespace WorldReliefNoise`, before `Hash16`, add:

```cpp
    /** |value| of one simplex band never exceeds this. Per corner the smoothed
     *  term is scale x (1 - 2 r^2)^3 x (g . f), with g a cube corner, so at
     *  most scale x sqrt(3) x max_r r (1 - 2 r^2)^3 = 0.7960, at r^2 = 1/14;
     *  four corners. A proof, not a sample: 200,000 samples reach 0.9997, so it
     *  is about three times loose, and Height reaches about a third of PeakCm
     *  (planning note 3: the developer rules on that at Task 13 (B0) Step 0). */
    inline constexpr double SimplexValueBound = 3.1840869651792727;

    /** |gradient| of one simplex band never exceeds this: per corner
     *  sqrt(3) x scale x max_x (1 - 2x)^2 (1 + 10x) = 6.054, at x = r^2 = 0.1;
     *  four corners. Samples reach 5.93. */
    inline constexpr double SimplexGradientBound = 24.21582543463124;

```

  After the namespace add:

```cpp

/**
 * A world's height function (landing decision 1). Every consumer -- the
 * flight's ground query, the terrain's tiles, the HUD, the material -- reads
 * this, and the material compiles the same noise, so they agree by
 * construction. Built from FSkyBody::Relief.
 *
 * Height is PeakCm x S(D) / S_max: S is the detail bands' sum, each band's
 * height its value over its frequency so every scale has the same slope,
 * and S_max its proven bound, so MaxHeightCm is exactly PeakCm. The bands
 * are the GPU's twelve and, in C++ only, finer octaves down to BandLimitCm:
 * the GPU's float cannot hold them, and nothing finer than 5 m exists, so
 * the finest tiles' 0.6 m vertices never alias it. Slice (a): the detail
 * bands only; the craters are the material's (Face) until slice (b) makes
 * them continuous (decision 3).
 *
 * Every evaluation takes a footprint, cm: the material's own fade,
 * saturate(1 - footprint x frequency) per band, made explicit. 0 is every
 * band (the flight); a tile passes its vertex spacing; the parity test the
 * probe's. D is the unit direction from the body's centre in universe axes,
 * which are the body's: worlds do not spin.
 */
class DEEPSPACE_API FWorldRelief
{
public:
    /** Nothing in the ground is finer than this, cm. */
    static constexpr double BandLimitCm = 500.0;

    explicit FWorldRelief(const FWorldReliefParams& InParams);

    const FWorldReliefParams& GetParams() const { return Params; }

    /** cm above the datum. */
    double Height(const FVector3d& D, double FootprintCm = 0.0) const;

    /** The same, and Grad = dHeight/dD in cm per unit of D, not projected
     *  onto the ground: its part along D is the caller's to drop. */
    double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm = 0.0) const;

    /** M_SkyBody's raw face terms at D, rocky (stretch 1), at this footprint. */
    FFaceTerms Face(const FVector3d& D, double FootprintCm) const;

    /** At most how far Height(D, 0) and Height(D, FootprintCm) can differ,
     *  from the bound on what each band's fade removed. */
    double OmittedBoundCm(double FootprintCm) const;

    double MaxHeightCm() const { return Params.PeakCm; }
    double MinHeightCm() const { return -Params.PeakCm; }

    /** A Lipschitz bound on the ground's slope, cm of height per cm along
     *  it: for the ray march (slice b). Proven, and loose (the bounds above). */
    double MaxSlope() const;

    /** The finest band's wavelength, cm: RadiusCm over its frequency. */
    double FinestWavelengthCm() const;

    /** Every detail band, cycles per radius, coarsest first. */
    TConstArrayView<double> GetDetailFrequencies() const { return Frequencies; }

    /** The detail bands' sum S at D, radius units (each band its value over
     *  its frequency), before any PeakCm scaling, at a footprint in D units
     *  (radius units); its gradient with respect to D into Grad if given.
     *  Height is PeakCm x S / DetailBound() in this slice; slice (b)'s
     *  craters add to S (Task T1). */
    double DetailSum(const FVector3d& D, double FootprintRadius, FVector3d* Grad = nullptr) const;

    /** S's proven bound, radius units: |DetailSum| never exceeds it. */
    double DetailBound() const { return SumBound; }

    /** S's Lipschitz bound with respect to D: sum of weight x SimplexGradientBound. */
    double DetailSlopeBound() const;

    /** The most a footprint (D units) can have removed from S, radius units. */
    double DetailOmittedBound(double FootprintRadius) const;

private:
    double FootprintOf(double FootprintCm) const;

    FWorldReliefParams Params;
    TArray<double> Frequencies;
    TArray<double> Weights;
    TArray<int32> Indices;
    double SumBound = 0.0;
};
```

  At the end of `WorldRelief.cpp` add:

```cpp

FWorldRelief::FWorldRelief(const FWorldReliefParams& InParams)
    : Params(InParams)
{
    const WorldReliefNoise::FBands Bands = WorldReliefNoise::Bands();
    Frequencies = Bands.DetailFrequencies;
    Weights = Bands.DetailWeights;
    Indices = Bands.DetailIndices;
    // The C++ goes on where the GPU stops: an octave a band, each from its
    // own corner of the noise at the next band number, at the finest band's
    // weight, down to BandLimitCm and never past it.
    while (Params.RadiusCm > 0.0 && Params.RadiusCm / (2.0 * Frequencies.Last()) >= BandLimitCm)
    {
        Frequencies.Add(2.0 * Frequencies.Last());
        Weights.Add(Weights.Last());
        Indices.Add(Indices.Last() + 1);
    }
    for (int32 Band = 0; Band < Frequencies.Num(); ++Band)
    {
        SumBound += Weights[Band] * WorldReliefNoise::SimplexValueBound / Frequencies[Band];
    }
}

double FWorldRelief::FootprintOf(double FootprintCm) const
{
    return Params.RadiusCm > 0.0 ? FMath::Max(FootprintCm, 0.0) / Params.RadiusCm : 0.0;
}

double FWorldRelief::DetailSum(const FVector3d& D, double FootprintD, FVector3d* OutGradient) const
{
    double Total = 0.0;
    FVector3d Gradient = FVector3d::ZeroVector;
    for (int32 Band = 0; Band < Frequencies.Num(); ++Band)
    {
        const int32 Index = Indices[Band];
        const WorldReliefF64::WR_Noise4 Noise = WorldReliefF64::WR_DetailBand(D.X, D.Y, D.Z, Frequencies[Band],
            Params.SeedOffset.X + 37.0 * Index, Params.SeedOffset.Y + 59.0 * Index, Params.SeedOffset.Z + 83.0 * Index,
            FootprintD, Weights[Band]);
        // A band's height is its value over its frequency, so its gradient
        // in D is the noise's own gradient: every band has the same slope.
        Total += Noise.Value / Frequencies[Band];
        Gradient += FVector3d(Noise.GX, Noise.GY, Noise.GZ);
    }
    if (OutGradient)
    {
        *OutGradient = Gradient;
    }
    return Total;
}

double FWorldRelief::DetailSlopeBound() const
{
    double GradientBound = 0.0;
    for (int32 Band = 0; Band < Frequencies.Num(); ++Band)
    {
        GradientBound += Weights[Band] * WorldReliefNoise::SimplexGradientBound;
    }
    return GradientBound;
}

double FWorldRelief::DetailOmittedBound(double FootprintD) const
{
    double Omitted = 0.0;
    for (int32 Band = 0; Band < Frequencies.Num(); ++Band)
    {
        // Faded by saturate(1 - footprint x frequency): the fade took at most
        // that share of the band's bound.
        Omitted += FMath::Min(1.0, FMath::Max(FootprintD, 0.0) * Frequencies[Band]) * Weights[Band] * WorldReliefNoise::SimplexValueBound / Frequencies[Band];
    }
    return Omitted;
}

double FWorldRelief::Height(const FVector3d& D, double FootprintCm) const
{
    FVector3d Unused;
    return HeightAndGradient(D, Unused, FootprintCm);
}

double FWorldRelief::HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const
{
    if (Params.PeakCm <= 0.0 || SumBound <= 0.0)
    {
        Grad = FVector3d::ZeroVector;
        return 0.0;
    }
    const double Scale = Params.PeakCm / SumBound;
    FVector3d Gradient;
    const double Total = DetailSum(D, FootprintOf(FootprintCm), &Gradient);
    Grad = Gradient * Scale;
    return Total * Scale;
}

FFaceTerms FWorldRelief::Face(const FVector3d& D, double FootprintCm) const
{
    return WorldReliefNoise::FaceF64(D, FootprintOf(FootprintCm), Params.SeedOffset, 1.0);
}

double FWorldRelief::OmittedBoundCm(double FootprintCm) const
{
    if (Params.PeakCm <= 0.0 || SumBound <= 0.0)
    {
        return 0.0;
    }
    return Params.PeakCm * DetailOmittedBound(FootprintOf(FootprintCm)) / SumBound;
}

double FWorldRelief::MaxSlope() const
{
    if (Params.PeakCm <= 0.0 || SumBound <= 0.0 || Params.RadiusCm <= 0.0)
    {
        return 0.0;
    }
    // dHeight/dD is bounded by PeakCm / S_max x sum(w G), and an arc of
    // length s along the ground moves D by s / R.
    return Params.PeakCm / SumBound * DetailSlopeBound() / Params.RadiusCm;
}

double FWorldRelief::FinestWavelengthCm() const
{
    return Frequencies.IsEmpty() ? 0.0 : Params.RadiusCm / Frequencies.Last();
}
```

- [ ] **Step 4: Build and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh && ./test.sh DeepSpace.Surface && ./test.sh
```

Expected: `DeepSpace.Surface.*` all pass (9 tests); the `AddInfo` lines report the highest
height as a share of the peak and the steepest slope against `MaxSlope`. The full suite green.

- [ ] **Step 5: Commit.**

```bash
git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief add Source/DeepSpace/Surface/WorldRelief.h Source/DeepSpace/Surface/WorldRelief.cpp Source/DeepSpace/Tests/WorldReliefTest.cpp
git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief commit -F - <<'MSG'
feat(relief): FWorldRelief -- the height function, pure, from the shared file

Height = PeakCm x S / S_max over the GPU's twelve detail bands and the finer
octaves the C++ alone carries, down to 5 m; the analytic gradient; the
footprint fade made explicit, with a proven bound on what it omits; a proven
slope bound. Face is the material's raw terms at the world's own offset.
Slice (a): detail bands only, craters in the material (decision 3).

DeepSpace.Surface.WorldRelief.{KnownValues, Deterministic, SeedMatchesSky,
Bounds, Gradient, BandLimit, Footprint, SlopeBound}.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 6: Prove each test can fail.** Every line is `KILLED`:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/mutate.sh Source/DeepSpace/Surface/WorldRelief.cpp 'SumBound += Weights[Band] * WorldReliefNoise::SimplexValueBound / Frequencies[Band];' 'SumBound += 0.1 * Weights[Band] * WorldReliefNoise::SimplexValueBound / Frequencies[Band];' DeepSpace.Surface.WorldRelief.Bounds
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/mutate.sh Source/DeepSpace/Surface/WorldRelief.cpp 'Gradient += FVector3d(Noise.GX, Noise.GY, Noise.GZ);' 'Gradient += FVector3d(Noise.GX, Noise.GY, -Noise.GZ);' DeepSpace.Surface.WorldRelief.Gradient
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/mutate.sh Source/DeepSpace/Surface/WorldRelief.cpp 'Params.RadiusCm / (2.0 * Frequencies.Last()) >= BandLimitCm)' 'Params.RadiusCm / (2.0 * Frequencies.Last()) >= 2.0 * BandLimitCm)' DeepSpace.Surface.WorldRelief.BandLimit
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/mutate.sh Source/DeepSpace/Surface/WorldRelief.cpp 'return Params.PeakCm * DetailOmittedBound(FootprintOf(FootprintCm)) / SumBound;' 'return 0.01 * Params.PeakCm * DetailOmittedBound(FootprintOf(FootprintCm)) / SumBound;' DeepSpace.Surface.WorldRelief.Footprint
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/mutate.sh Source/DeepSpace/Surface/WorldRelief.cpp 'return Params.PeakCm / SumBound * DetailSlopeBound() / Params.RadiusCm;' 'return 0.01 * Params.PeakCm / SumBound * DetailSlopeBound() / Params.RadiusCm;' DeepSpace.Surface.WorldRelief.SlopeBound
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/mutate.sh Source/DeepSpace/Surface/WorldRelief.cpp 'Params.SeedOffset.X + 37.0 * Index, Params.SeedOffset.Y + 59.0 * Index, Params.SeedOffset.Z + 83.0 * Index,' '37.0 * Index, 59.0 * Index, 83.0 * Index,' DeepSpace.Surface.WorldRelief.Deterministic
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/mutate.sh Source/DeepSpace/Sky/SkySystem.cpp '& 0xFFFFull) / 65536.0 * SkyMaterial::SurfaceOffsetSpan;' '& 0xFFFFull) / 65535.0 * SkyMaterial::SurfaceOffsetSpan;' DeepSpace.Surface.WorldRelief.SeedMatchesSky
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh
```

---

## Task 11 (R4): `M_SkyBody` onto the shared file

**Files:**
- Modify: `Tools/setup_sky_materials.py` (`sky_body`; delete `every_band`)
- Modify: `Source/DeepSpace/Tests/SkyMaterialContractTest.cpp` (`CheckSurfaceFace` rewritten; includes)
- Modify: `Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp` (the barren world is Baemsekai IV, through `FWorldRelief`)
- Modify: `CLAUDE.md` (*The sky*, the paragraph beginning **The material contract.**)
- Assets: `Content/Materials/Sky/M_SkyBody.uasset` (re-authored)

**Interfaces:**
- Consumes: `shared_terms`, `CheckSharedRelief` (R1); `FWorldRelief` (R3); `FSkyBody::Relief` (P4).
- Produces: `M_SkyBody` drawn from `/Project/Private/WorldRelief.ush`; the contract test that holds it there.

- [ ] **Step 1: Write the failing test.** In `SkyMaterialContractTest.cpp` add includes
  `#include "Materials/MaterialExpressionDDX.h"` and `#include "Materials/MaterialExpressionDDY.h"`,
  and replace `CheckSurfaceFace` and its doc comment -- from the line
  `     * M_SkyBody's face and relief, as the developer asked for them: detail` back to its
  opening `/**`, through the function's closing brace -- with:

```cpp
    /**
     * M_SkyBody's face and relief, as the developer asked for them: detail
     * fixed to the body, in several bands, whose finer ones arrive as the
     * world grows, and ground that tilts where the light is low. The names
     * cannot see any of that, so the graph is read:
     *
     * - Object space and nothing else, and no texture (unchanged).
     * - The contract's bands, on their own terms: finer each time, no octave
     *   skipped, weights never falling, craters stepping by four.
     * - One shared file: every band is WorldRelief.ush's, through one Custom
     *   node (landing decision 1), handed D in the body's axes, the pixel's
     *   footprint, the world's seed and the banding's stretch.
     * - The half-float guard: the file's terms reach the pixel only through
     *   the clamp to +/- surface_max_swing and a unit normal.
     * - Every parameter reaches the pixel.
     */
    void CheckSurfaceFace(FAutomationTestBase& Test, UMaterial& Material, const TSharedPtr<FJsonObject>& Constants)
    {
        int32 LocalPositions = 0;
        TMap<FName, const UMaterialExpression*> Parameters;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material.GetExpressions())
        {
            const UMaterialExpression* Node = Expression.Get();
            LocalPositions += Cast<UMaterialExpressionLocalPosition>(Node) ? 1 : 0;
            if (const UMaterialExpressionParameter* Parameter = Cast<UMaterialExpressionParameter>(Node))
            {
                Parameters.Add(Parameter->ParameterName, Parameter);
            }
            const bool bSwims = Cast<UMaterialExpressionWorldPosition>(Node) || Cast<UMaterialExpressionScreenPosition>(Node)
                || Cast<UMaterialExpressionPixelDepth>(Node) || Cast<UMaterialExpressionCameraPositionWS>(Node)
                || Cast<UMaterialExpressionObjectPositionWS>(Node) || Cast<UMaterialExpressionViewSize>(Node);
            Test.TestFalse(FString::Printf(TEXT("M_SkyBody's face reads no world, screen or camera position (%s)"), *Node->GetName()), bSwims);
            Test.TestFalse(FString::Printf(TEXT("M_SkyBody samples no texture (%s)"), *Node->GetName()),
                Cast<UMaterialExpressionTextureBase>(Node) != nullptr);
        }
        Test.TestEqual(TEXT("M_SkyBody's face is taken from the mesh's own position, once"), LocalPositions, 1);

        // -- The contract's bands, on their own terms ---------------------------
        const TArray<TSharedPtr<FJsonValue>>& Frequencies = Constants->GetArrayField(TEXT("detail_frequencies"));
        const TArray<TSharedPtr<FJsonValue>>& Weights = Constants->GetArrayField(TEXT("detail_weights"));
        const TArray<TSharedPtr<FJsonValue>>& CraterFrequencies = Constants->GetArrayField(TEXT("crater_frequencies"));
        const double ContinentFrequency = Constants->GetNumberField(TEXT("continent_frequency"));
        const int32 ContinentLevels = static_cast<int32>(Constants->GetNumberField(TEXT("continent_levels")));
        const int32 DetailLevels = static_cast<int32>(Constants->GetNumberField(TEXT("detail_levels")));
        const double LevelScale = Constants->GetNumberField(TEXT("level_scale"));
        Test.TestEqual(TEXT("a weight for every detail band"), Weights.Num(), Frequencies.Num());
        Test.TestTrue(TEXT("several detail bands, not one"), Frequencies.Num() >= 2);
        Test.TestEqual(TEXT("a detail band is one octave: the vector noise has no others"), DetailLevels, 1);
        double Previous = ContinentFrequency;
        double Reach = ContinentFrequency * FMath::Pow(LevelScale, ContinentLevels);
        for (int32 Index = 0; Index < Frequencies.Num(); ++Index)
        {
            const double Frequency = Frequencies[Index]->AsNumber();
            Test.TestTrue(FString::Printf(TEXT("each detail band finer than the last (%g)"), Frequency), Frequency > Previous);
            Test.TestTrue(FString::Printf(TEXT("and no octave skipped before it (%g after a reach of %g)"), Frequency, Reach),
                Frequency <= Reach * (1.0 + 1e-9));
            Previous = Frequency;
            Reach = Frequency * FMath::Pow(LevelScale, DetailLevels);
            if (Index > 0 && Index < Weights.Num())
            {
                Test.TestTrue(FString::Printf(TEXT("a finer band is never weaker than a coarser (%g)"), Frequency),
                    Weights[Index]->AsNumber() >= Weights[Index - 1]->AsNumber());
            }
        }
        for (int32 Index = 1; Index < CraterFrequencies.Num(); ++Index)
        {
            Test.TestTrue(TEXT("crater bands step by four, so the count wider than D goes as D^-2"),
                FMath::IsNearlyEqual(CraterFrequencies[Index]->AsNumber() / CraterFrequencies[Index - 1]->AsNumber(), 4.0, 1e-9));
        }

        // -- One shared file, handed what the face is made of ---------------------
        const UMaterialExpressionCustom* Shared = CheckSharedRelief(Test, Material);
        if (!Shared)
        {
            return;
        }
        const auto Fed = [Shared](const TCHAR* Pin) -> TSet<const UMaterialExpression*>
        {
            for (const FCustomInput& Input : Shared->Inputs)
            {
                if (Input.InputName == FName(Pin))
                {
                    return Upstream(Input.Input.Expression);
                }
            }
            return {};
        };
        const auto HasA = [](const TSet<const UMaterialExpression*>& Nodes, TFunctionRef<bool(const UMaterialExpression*)> Is)
        {
            for (const UMaterialExpression* Node : Nodes)
            {
                if (Is(Node))
                {
                    return true;
                }
            }
            return false;
        };
        const auto Holds = [&Parameters](const TSet<const UMaterialExpression*>& Nodes, FName Name)
        {
            const UMaterialExpression* const* Found = Parameters.Find(Name);
            return Found && Nodes.Contains(*Found);
        };
        const TSet<const UMaterialExpression*> Direction = Fed(TEXT("Direction"));
        Test.TestTrue(TEXT("the file is handed D from the mesh's own position"),
            HasA(Direction, [](const UMaterialExpression* Node) { return Cast<UMaterialExpressionLocalPosition>(Node) != nullptr; }));
        Test.TestTrue(TEXT("turned into the body's axes by BodyAxisX and BodyAxisY"),
            Holds(Direction, SkyMaterial::BodyAxisX) && Holds(Direction, SkyMaterial::BodyAxisY));
        const TSet<const UMaterialExpression*> Footprint = Fed(TEXT("Footprint"));
        const double FilterPixels = Constants->GetNumberField(TEXT("filter_pixels"));
        Test.TestTrue(TEXT("its footprint is the pixel's: DDX and DDY of that D"),
            HasA(Footprint, [](const UMaterialExpression* Node) { return Cast<UMaterialExpressionDDX>(Node) != nullptr; })
            && HasA(Footprint, [](const UMaterialExpression* Node) { return Cast<UMaterialExpressionDDY>(Node) != nullptr; })
            && HasA(Footprint, [](const UMaterialExpression* Node) { return Cast<UMaterialExpressionLocalPosition>(Node) != nullptr; }));
        Test.TestTrue(TEXT("times filter_pixels"), HasA(Footprint, [FilterPixels](const UMaterialExpression* Node)
        {
            const UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Node);
            return Constant && FMath::IsNearlyEqual(static_cast<double>(Constant->R), FilterPixels, 1e-6);
        }));
        Test.TestTrue(TEXT("its offset is the world's SurfaceSeed"), Holds(Fed(TEXT("SeedOffset")), SkyMaterial::SurfaceSeed));
        Test.TestTrue(TEXT("its stretch is the Banding's"), Holds(Fed(TEXT("Stretch")), SkyMaterial::Banding));

        // -- The half-float guard, and every knob reaching the pixel -------------
        const FExpressionInput* Emissive = Material.GetExpressionInputForProperty(MP_EmissiveColor);
        const UMaterialExpression* Pixel = Emissive ? Emissive->Expression : nullptr;
        if (!Test.TestNotNull(TEXT("M_SkyBody's emissive is wired"), Pixel))
        {
            return;
        }
        const TSet<const UMaterialExpression*> Reached = Upstream(Pixel);
        const double Swing = Constants->GetNumberField(TEXT("surface_max_swing"));
        TArray<const UMaterialExpressionClamp*> Guards;
        for (const UMaterialExpression* Node : Reached)
        {
            const UMaterialExpressionClamp* Clamp = Cast<UMaterialExpressionClamp>(Node);
            if (Clamp && !Clamp->Min.Expression && !Clamp->Max.Expression
                && FMath::IsNearlyEqual(static_cast<double>(Clamp->MinDefault), -Swing, 1e-6)
                && FMath::IsNearlyEqual(static_cast<double>(Clamp->MaxDefault), Swing, 1e-6))
            {
                Guards.Add(Clamp);
            }
        }
        if (!Test.TestEqual(TEXT("the pixel is fed by one clamp to +/- surface_max_swing"), Guards.Num(), 1))
        {
            return;
        }
        const UMaterialExpressionClamp* Guard = Guards[0];

        // The light's dot products: each with a unit normal, so each is in
        // [-1, 1] whatever feeds the normal.
        const UMaterialExpression* const* Light = Parameters.Find(SkyMaterial::LightDirection);
        TSet<const UMaterialExpression*> Turns;
        for (const UMaterialExpression* Node : Reached)
        {
            const UMaterialExpressionDotProduct* Dot = Cast<UMaterialExpressionDotProduct>(Node);
            if (!Dot || !Light || (Dot->A.Expression != *Light && Dot->B.Expression != *Light))
            {
                continue;
            }
            const UMaterialExpression* Normal = Dot->A.Expression == *Light ? Dot->B.Expression : Dot->A.Expression;
            Test.TestTrue(TEXT("every N.L is of a unit normal"),
                Cast<UMaterialExpressionNormalize>(Normal) || Cast<UMaterialExpressionVertexNormalWS>(Normal));
            Turns.Add(Dot);
        }
        Test.TestTrue(TEXT("the light meets the relief's normal"), Turns.Num() >= 1);

        TSet<const UMaterialExpression*> Stops = Turns;
        Stops.Add(Guard);
        const TSet<const UMaterialExpression*> Unguarded = Upstream(Pixel, Stops);
        const TSet<const UMaterialExpression*> Guarded = Upstream(Guard->Input.Expression);
        TSet<const UMaterialExpression*> Turning;
        for (const UMaterialExpression* Dot : Turns)
        {
            Turning.Append(Upstream(Dot));
        }
        Test.TestTrue(TEXT("the shared file lies behind the guard"), Guarded.Contains(Shared));
        Test.TestFalse(TEXT("and reaches the pixel by no way but it and the unit normal"), Unguarded.Contains(Shared));
        Test.TestTrue(TEXT("the shared file tilts the normal: the relief is the same noise as the face"), Turning.Contains(Shared));
        for (const FName Knob : { SkyMaterial::Mottle, SkyMaterial::Detail, SkyMaterial::Banding, SkyMaterial::SurfaceSeed, SkyMaterial::Cratering })
        {
            const UMaterialExpression* const* Parameter = Parameters.Find(Knob);
            Test.TestTrue(FString::Printf(TEXT("%s shapes the face behind the guard"), *Knob.ToString()),
                Parameter && Guarded.Contains(*Parameter));
        }
        for (const FName Knob : { SkyMaterial::Mottle, SkyMaterial::Detail, SkyMaterial::Relief, SkyMaterial::Cratering })
        {
            const UMaterialExpression* const* Parameter = Parameters.Find(Knob);
            Test.TestTrue(FString::Printf(TEXT("and %s reaches the pixel only through the guard or the unit normal"), *Knob.ToString()),
                Parameter && !Unguarded.Contains(*Parameter));
        }
        for (const FName Knob : { SkyMaterial::Relief, SkyMaterial::Cratering })
        {
            const UMaterialExpression* const* Parameter = Parameters.Find(Knob);
            Test.TestTrue(FString::Printf(TEXT("%s tilts the normal"), *Knob.ToString()), Parameter && Turning.Contains(*Parameter));
        }
        for (const TPair<FName, const UMaterialExpression*>& Parameter : Parameters)
        {
            Test.TestTrue(FString::Printf(TEXT("%s reaches the pixel"), *Parameter.Key.ToString()), Reached.Contains(Parameter.Value));
        }
    }
```

  (`CheckSharedRelief`, which R1 placed after `ScaledBy`, is already declared above it.
  `ScaledBy` is now unused: delete it.)

- [ ] **Step 2: Build and run against today's `M_SkyBody`; expect FAIL.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh && ./test.sh DeepSpace.Sky.MaterialContract
```

Expected: FAIL: `M_SkyBody draws no band on an engine noise node (MaterialExpressionNoise_0)`
and `M_SkyBody reaches the shared file through one Custom node`.

- [ ] **Step 3: Move `M_SkyBody`.** In `setup_sky_materials.py`, in `sky_body()`, replace
  `    factor, slope = surface(g, knobs, seed, direction, footprint, every_band)` with
  `    factor, slope = surface(g, knobs, seed, direction, footprint, shared_terms)`, delete the
  `every_band` function, and in `sky_body()`'s docstring, after the line
  `        N        = relief_normal                      (surface)`, add
  `    (the face's every band from Shaders/Private/WorldRelief.ush, through one Custom node: landing decision 1)`.

- [ ] **Step 4: Author and run; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && . Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" -unattended -nopause -nosplash -NoLiveCoding; tail -3 Saved/setup_sky_materials.txt
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./test.sh DeepSpace.Sky && ./test.sh
```

Expected: `ok`; `DeepSpace.Sky.*` pass (`MaterialContract` included; `ShipSky`, `Eclipse`,
`NightSideIsDrawn` and the rest, which set `M_SkyBody`'s parameters by name, unchanged); the
full suite green.

- [ ] **Step 5: The parity test on a real world.** In `WorldReliefParityTest.cpp`:
  - add includes `#include "Sky/ShipSky.h"`, `#include "Sky/SkySystem.h"`, `#include "Universe/UniverseSubsystem.h"`;
  - replace

```cpp
    /** A barren world's seed offset until task R4 hands the test Baemsekai
     *  IV's: multiples of 1/256, as every real one is. */
    const FVector3d Offset(12.5, 200.25, 77.0);
```

  with

```cpp
    /** The giant's seed offset: a made one, multiples of 1/256 as every real
     *  one is. The barren world is Baemsekai IV, with its own. */
    const FVector3d GiantOffset(12.5, 200.25, 77.0);
```

  - replace everything from the comment line `    // Barren (stretch 1) against both the engine's nodes and the C++; a`
    through the closing `}` of `for (const FWorldCase& World : Worlds)` with:

```cpp
    // Barren: Baemsekai IV, the landing fixtures' first world, its C++
    // through FWorldRelief -- what the flight and the terrain read. A giant
    // (stretch 6, the belts' streaking) against the engine's nodes; its C++
    // reported only, since a giant has no ground.
    const TOptional<FStarSystem> Home = Test.Universe->GetSystem(Test.Universe->GetStartSystem());
    if (!TestTrue(TEXT("home generates"), Home.IsSet()))
    {
        return false;
    }
    const FSkySystem HomeSky = FSkySystem::FromSystem(*Home, {});
    if (!TestTrue(TEXT("home has a fourth world"), HomeSky.Bodies.IsValidIndex(4)))
    {
        return false;
    }
    const FSkyBody& Fourth = HomeSky.Bodies[4];
    TestEqual(TEXT("the barren world is Baemsekai IV"), Fourth.Id, FName(TEXT("Baemsekai IV")));
    const FWorldRelief Ground(Fourth.Relief);

    struct FWorldCase
    {
        const TCHAR* Name;
        FLinearColor Seed;
        FVector3d Offset;
        float Banding;
        double Stretch;
        const FWorldRelief* Relief;
    };
    const FWorldCase Worlds[] = {
        { TEXT("Baemsekai IV"), ShipSky::SurfaceSeed(Fourth.SurfaceSeed, Fourth.BeltPairs), Fourth.Relief.SeedOffset, 0.0f, 1.0, &Ground },
        { TEXT("giant"), FLinearColor(static_cast<float>(GiantOffset.X), static_cast<float>(GiantOffset.Y), static_cast<float>(GiantOffset.Z), 8.0f),
          GiantOffset, 1.0f, 6.0, nullptr },
    };
    for (const FWorldCase& World : Worlds)
    {
        for (UMaterialInstanceDynamic* Probe : { NewProbe, OldProbe })
        {
            Probe->SetVectorParameterValue(SkyMaterial::SurfaceSeed, World.Seed);
            Probe->SetScalarParameterValue(SkyMaterial::Banding, World.Banding);
        }
        for (const double FootprintD : Footprints)
        {
            const float Footprint = static_cast<float>(FootprintD);
            NewProbe->SetScalarParameterValue(SkyMaterial::ProbeFootprint, Footprint);
            OldProbe->SetScalarParameterValue(SkyMaterial::ProbeFootprint, Footprint);
            const FDrawn New = DrawAll(Test.World, Target, NewProbe);
            const FDrawn Old = DrawAll(Test.World, Target, OldProbe);

            FGap NewVsOld;
            FGap CppVsNew;
            FGap FloatVsNew;
            FGap CppVsOld;
            int32 LeftOut = 0;
            int32 Unmatched = 0;
            for (int32 Index = 0; Index < Side * Side; ++Index)
            {
                const FVector3d& D = New.Direction[Index];
                Unmatched += D == Old.Direction[Index] ? 0 : 1;
                if (WorldReliefNoise::CraterMargin(D, Footprint, World.Offset) < StepMarginCells)
                {
                    ++LeftOut;
                    continue;
                }
                const FFaceTerms Gpu = New.At(Index);
                const FFaceTerms Engine = Old.At(Index);
                const FFaceTerms Cpp = World.Relief
                    ? World.Relief->Face(D, static_cast<double>(Footprint) * World.Relief->GetParams().RadiusCm)
                    : WorldReliefNoise::FaceF64(D, Footprint, World.Offset, World.Stretch);
                const FFaceTerms Float = WorldReliefNoise::FaceF32(FVector3f(D), Footprint, FVector3f(World.Offset), static_cast<float>(World.Stretch));
                NewVsOld.Widen(Gpu, Engine);
                CppVsNew.Widen(Cpp, Gpu);
                FloatVsNew.Widen(Float, Gpu);
                CppVsOld.Widen(Cpp, Engine);
            }
            const double LeftOutShare = static_cast<double>(LeftOut) / (Side * Side);
            const FString At = FString::Printf(TEXT("%s, footprint 1/%.0f"), World.Name, 1.0 / FootprintD);
            TestEqual(At + TEXT(": both probes drew the same directions"), Unmatched, 0);
            TestTrue(FString::Printf(TEXT("%s: at most 1%% of samples lie on a crater's step (%.3f%%)"), *At, 100.0 * LeftOutShare),
                LeftOutShare <= MaxLeftOut);
            TestTrue(FString::Printf(TEXT("%s: the shared file draws what the engine's nodes drew (%s)"), *At, *NewVsOld.Describe()),
                NewVsOld.Worst() <= Tolerance);
            if (World.Relief)
            {
                TestTrue(FString::Printf(TEXT("%s: FWorldRelief computes what the GPU drew (%s)"), *At, *CppVsNew.Describe()),
                    CppVsNew.Worst() <= Tolerance);
                WorstCppNew = FMath::Max(WorstCppNew, CppVsNew.Worst());
                WorstFloatNew = FMath::Max(WorstFloatNew, FloatVsNew.Worst());
                WorstCppOld = FMath::Max(WorstCppOld, CppVsOld.Worst());
            }
            WorstNewOld = FMath::Max(WorstNewOld, NewVsOld.Worst());
            MostLeftOut = FMath::Max(MostLeftOut, LeftOutShare);
            Report.Add(FString::Printf(TEXT("%s: %d compared, %d left out"), *At, Side * Side - LeftOut, LeftOut));
            Report.Add(TEXT("  shared file vs engine nodes:        ") + NewVsOld.Describe());
            Report.Add(TEXT("  C++ (double) vs shared file:        ") + CppVsNew.Describe());
            Report.Add(TEXT("  C++ (float build) vs shared file:   ") + FloatVsNew.Describe());
            Report.Add(TEXT("  C++ (double) vs engine nodes:       ") + CppVsOld.Describe());
        }
    }
```

  - in the header comment replace the line ` *   - to the C++, at the very D the GPU drew;` with
    ` *   - to the C++ -- FWorldRelief::Face of Baemsekai IV -- at the very D the GPU drew;`.

- [ ] **Step 6: Run the rendered test; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh && Tools/eyes.sh Eyes.WorldReliefParity; tail -1 Saved/Eyes/WorldReliefParity/report.txt
```

- [ ] **Step 7: CLAUDE.md.** In *The sky*, at the end of the paragraph beginning
  **The material contract.** (after its command block's closing paragraph ends), add a
  paragraph:

```text
**`M_SkyBody`'s face is one shared file.** Every band is
`Shaders/Private/WorldRelief.ush` -- the noise the C++ ground compiles too
(`Surface/WorldRelief.*`, landing decision 1) -- reached through one Custom
node as `/Project/Private/WorldRelief.ush`, a path the `DeepSpaceShaders`
module maps at `PostConfigInit`. **A Custom node's HLSL error is invisible
headless**: the translator passes, no shader compiles under `-nullrhi`, and
every world draws grey with every test green. `Tools/eyes.sh
Eyes.WorldReliefParity` is what renders it: run it after any edit to the
`.ush`, and after re-authoring the sky's materials.
```

- [ ] **Step 8: Commit.**

```bash
git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief add Tools/setup_sky_materials.py Source/DeepSpace/Tests/SkyMaterialContractTest.cpp Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp CLAUDE.md Content/Materials/Sky/M_SkyBody.uasset
git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief commit -F - <<'MSG'
feat(sky): M_SkyBody draws its face from the shared WorldRelief.ush -- the orbital look unchanged

One Custom node in place of the engine's noise nodes, handed D in the body's
axes, the pixel's footprint, the world's seed and the banding's stretch.
DeepSpace.Sky.MaterialContract now holds the graph to the shared file and
keeps the half-float guard's checks; Eyes.WorldReliefParity holds the file
to the engine's nodes and FWorldRelief::Face of Baemsekai IV to the GPU.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 9: Prove it.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && Tools/mutate.sh Source/DeepSpace/Sky/SkyMaterialContract.h 'WorldReliefInclude = TEXT("/Project/Private/WorldRelief.ush");' 'WorldReliefInclude = TEXT("/Project/Private/WorldReliefX.ush");' DeepSpace.Sky.MaterialContract
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Source/DeepSpace/Surface/WorldRelief.cpp 'return WorldReliefNoise::FaceF64(D, FootprintOf(FootprintCm), Params.SeedOffset, 1.0);' 'return WorldReliefNoise::FaceF64(D, FootprintOf(FootprintCm), Params.SeedOffset + FVector3d(0.0, 0.0, 1.0), 1.0);' Eyes.WorldReliefParity
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh
```

Expected: `KILLED` twice.

---

## Task 12 (R5): Retire the legacy probe; ADR 0006; the architecture line; done-when

The legacy probe is "deleted when (a) merges" (spec decision 1). Its last run is R4 Step 6.

**Files:**
- Delete: `Content/Materials/Sky/M_SkyReliefProbeLegacy.uasset`
- Modify: `Tools/sky_material_contract.json` (the legacy material; `constants.probe_bands`; `probe_comment`)
- Modify: `Source/DeepSpace/Sky/SkyMaterialContract.h` (`ReliefProbeLegacyPath` and its comment)
- Modify: `Tools/setup_sky_materials.py` (delete `legacy_terms`, `noise_band`, `link_any`, `fade`, `detail_band`, `crater_band`, `EVERY_DETAIL`, `EVERY_CRATER`; `main`)
- Modify: `Source/DeepSpace/Tests/SkyMaterialContractTest.cpp` (the legacy row; `CheckSharedTables`' probe-band lines)
- Modify: `Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp` (C++ against the GPU only)
- Modify: `docs/decisions/0006-generation-is-cpp-and-runs-at-startup.md` (amendment), `CLAUDE.md` (*Architecture*)

**Interfaces:**
- Consumes: everything above.
- Produces: slice (a) as slice (b) inherits it: `Eyes.WorldReliefParity` holding `FWorldRelief` to `M_SkyReliefProbe` at five footprints.

- [ ] **Step 1: Write the failing test.** In `SkyMaterialContractTest.cpp`, delete the
  `{ TEXT("M_SkyReliefProbeLegacy"), ... }` row from `Materials`, and in `CheckSharedTables`
  delete the `probe_bands` block (from `const TSharedPtr<FJsonObject> Probe = Constants->GetObjectField(TEXT("probe_bands"));`
  through `Test.TestTrue(TEXT("and exactly their crater bands"), Bands.CraterIndices == ProbeCrater);`).
  Run:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh && ./test.sh DeepSpace.Sky.MaterialContract
```

Expected: FAIL: `the JSON lists exactly the header's materials` (the JSON still has the legacy probe).

- [ ] **Step 2: Remove it everywhere.**
  - JSON: delete the `"M_SkyReliefProbeLegacy"` entry and the `"probe_bands"` line; in
    `probe_comment` delete `-- and the bands the legacy probe draws, by band number (detail n is detail_frequencies[n - 1], crater n is crater_frequencies[n - 101]); the shared file must carry exactly these`.
  - Header: delete the `ReliefProbeLegacyPath` line, and in the comment above the probe
    names delete the sentence beginning `M_SkyReliefProbeLegacy through the engine's noise nodes`.
  - Script: delete `EVERY_DETAIL`, `EVERY_CRATER`, `noise_band`, `link_any`, `fade`,
    `detail_band`, `crater_band`, `legacy_terms`; in `main()` delete the `bands = ...` line
    and the `relief_probe("M_SkyReliefProbeLegacy", ...)` call; in `surface()`'s docstring
    replace `terms is legacy_terms' engine nodes or shared_terms' file; the
    composition is the same either way, so the face is the terms' alone.` with
    `terms is shared_terms: the shared file.`
  - Asset: `git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief rm Content/Materials/Sky/M_SkyReliefProbeLegacy.uasset`
  - Parity test: delete `Legacy`, `OldProbe`, `Old`, `NewVsOld`, `CppVsOld`, `WorstNewOld`,
    `WorstCppOld` and every line using them; delete the giant case (it had only the engine to
    be held to) and the `GiantOffset` constant; the loop keeps, per footprint, `CppVsNew`
    (asserted) and `FloatVsNew` (reported). The `SUMMARY` line becomes
    `FString::Printf(TEXT("SUMMARY C++-vs-GPU %.2e, float-C++-vs-GPU %.2e, left out at most %.3f%%"), WorstCppNew, WorstFloatNew, 100.0 * MostLeftOut)`.
    In the header comment replace the bullets `- to M_SkyReliefProbeLegacy, ...` and
    `- to the C++ -- FWorldRelief::Face of Baemsekai IV -- at the very D the GPU drew;` with
    `* to FWorldRelief::Face of Baemsekai IV, at the very D the GPU drew. (Until landing slice
    (a) merged it also held the file to the engine's own noise nodes, which is how "the orbital
    look unchanged" was proven; the spike's verdict line below records it.)`

  Check nothing still names it:

```bash
grep -rn "ReliefProbeLegacy\|probe_bands\|legacy_terms" /home/matt/Development/deepspace/.worktrees/landing-a-relief/Source /home/matt/Development/deepspace/.worktrees/landing-a-relief/Tools
```

Expected: no output.

- [ ] **Step 3: Author, run everything; expect PASS.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && . Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" -unattended -nopause -nosplash -NoLiveCoding; tail -3 Saved/setup_sky_materials.txt
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh && ./test.sh && Tools/eyes.sh Eyes.WorldReliefParity
```

- [ ] **Step 4: ADR 0006.** Append to `docs/decisions/0006-generation-is-cpp-and-runs-at-startup.md`:

```markdown

## Amendment, 2026-09-27: one generator compiled twice (landing)

A world's ground -- what the flight stops on, what the terrain meshes, what
`M_SkyBody` shades from orbit -- is `Shaders/Private/WorldRelief.ush`: one
file, in a subset of syntax that is both HLSL and C++. The C++ includes it
(`Source/DeepSpace/Surface/WorldRelief.cpp`, in double for the ground and in
float as the GPU's mirror); `M_SkyBody` includes it through one Custom node,
by the `/Project` shader path the `DeepSpaceShaders` module maps at
`PostConfigInit`. That is still one generator: one text, which a change
reaches in both compilers or fails in one. It is not a second implementation
held to the first by test vectors, which is what this ADR refuses.

What the shared text cannot prove by itself -- what the GPU's float
arithmetic makes of it -- is measured, not assumed: `Eyes.WorldReliefParity`
(`Tools/eyes.sh`, outside `./test.sh`, never `-nullrhi`) renders the file's
terms and holds them to `FWorldRelief` at five footprints. Before landing
slice (a) merged it also held them to the engine's own noise nodes, which the
file replaced; the spike's verdict is in that test's header.

A Custom node's HLSL error is invisible to the headless suite -- the material
ships grey with every test green -- so any change to the `.ush` runs the
rendered test before it merges.
```

- [ ] **Step 5: CLAUDE.md *Architecture*.** After the bullet beginning
  `` - `Source/DeepSpace/Universe/` -- procgen: `` add:

```text
- `Source/DeepSpace/Surface/` -- the ground: `FWorldRelief`, pure, the one
  height function, from `Shaders/Private/WorldRelief.ush`, which `M_SkyBody`
  compiles too (the module `DeepSpaceShaders` maps `/Project` to `Shaders/`);
  `WorldReliefParams.h` is the plain data `FSkyBody::Relief` carries.
```

- [ ] **Step 6: Commit.**

```bash
git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief add -A Tools/sky_material_contract.json Source/DeepSpace/Sky/SkyMaterialContract.h Tools/setup_sky_materials.py Source/DeepSpace/Tests/SkyMaterialContractTest.cpp Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp docs/decisions/0006-generation-is-cpp-and-runs-at-startup.md CLAUDE.md Content/Materials/Sky
git -C /home/matt/Development/deepspace/.worktrees/landing-a-relief commit -F - <<'MSG'
chore(relief): retire the legacy probe -- the orbital look is proven unchanged; ADR 0006 amended

M_SkyReliefProbeLegacy and the engine-node graph code go, as the spec says
they do when slice (a) merges. Eyes.WorldReliefParity now holds
FWorldRelief to the GPU; its header keeps the spike's verdict. ADR 0006:
one generator compiled twice. CLAUDE.md: the Surface architecture line.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
```

- [ ] **Step 7: Prove the guard still fails when it should.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Source/DeepSpace/Surface/WorldRelief.cpp 'return WorldReliefNoise::FaceF64(D, FootprintOf(FootprintCm), Params.SeedOffset, 1.0);' 'return WorldReliefNoise::FaceF64(D, FootprintOf(FootprintCm), Params.SeedOffset + FVector3d(0.0, 0.0, 1.0), 1.0);' Eyes.WorldReliefParity
cd /home/matt/Development/deepspace/.worktrees/landing-a-relief && ./build.sh
```

Expected: `KILLED`.

- [ ] **Step 8: Done-when (spec, *Slice (a)*), checked in order.**
  1. `./build.sh` and `./test.sh` green, with `DeepSpace.Surface.WorldRelief.*` (8),
     `DeepSpace.Surface.ShaderMapping` (unless R1 Step 6 moved it), `DeepSpace.Universe.GroundFacts`,
     `.Relief`, `.Corpus`, `DeepSpace.Sky.WhatCanBeLanded`, `DeepSpace.Sky.MaterialContract`,
     each mutate-proven in its task.
  2. `Eyes.WorldReliefParity` passed with the legacy comparison in R4 Step 6 (the new graph
     against the legacy one, and C++ against the GPU, 1e-3 over 256 x 256 at five
     footprints) and passes now (Step 3).
  3. **The developer flies to Baemsekai III, IV and V and sees no change.** Hand over, through
     the orchestrator: build this branch (`./rebuild.sh --force --launch`), play, and at the
     console `ds.Sky.Goto 3 30`, `ds.Sky.Goto 4 30`, `ds.Sky.Goto 5 30` (30 km above each),
     and `ds.Sky.Goto 4 30 dusk` for raking light; compare with `main` the same way. Merge
     only on the developer's word.
  4. Merge: superpowers:finishing-a-development-branch.

---

# Slice (b): terrain, gravity and the vertical lever -- fly down and hover over real ground

Tracks F, S and T off `feat/landing-b`. No LANDED yet: the ship comes to rest with a foot on the ground and the corner reads `1.5 M ABOVE GROUND · HOVERING`.

## Task 13 (B0): the slice's trees

**Owner:** orchestrator. **Depends on:** the preconditions above, and the developer's ruling in
Step 0.

**Files:** none (git only), unless Step 0's ruling amends the spec
(`docs/superpowers/specs/2026-09-27-landing-design.md`, decision 3).

- [ ] **Step 0: The loose bound goes to the developer (planning note 3)**

Read R3's measured share from the slice (a) tree's last run of `DeepSpace.Surface.WorldRelief.Bounds`:

```bash
cd /home/matt/Development/deepspace && ./test.sh DeepSpace.Surface.WorldRelief.Bounds && \
grep "highest of 100,000" Saved/Logs/DeepSpace.log | tail -1
```

Expected: `passed: 1` and a line `highest of 100,000: 0.3xx of the peak; ...`. Put one question to
the developer (`AskUserQuestion`, per the user's CLAUDE.md), quoting that share and planning note
3's numbers, with two answers:

- **Keep the spec as written** (the drawn relief a third of `PeakCm`, the floor 13-17 km over the
  highest drawn summit, descents 75-140 s from the floor). Then add to the spec, under decision 3, an
  *Amended at the start of slice (b)* paragraph recording the measured share, the drawn heights
  (a 1 g barren world about 2.1 km, the 10 km cap about 3.3 km) and the 75-140 s descent, and to the
  spec's *Decisions needing sign-off* list an item "Relief a third of ruling 6's: kept, <date>". Commit
  it on `main` (`git add docs/superpowers/specs/2026-09-27-landing-design.md && git commit -m "docs:
  landing -- the drawn relief is a third of PeakCm, ruled at the start of slice (b)"` with the
  Co-Authored-By line). Go on to Step 1.
- **Tighten it.** Stop: this is a re-plan point, like R1's FLOAT FLOOR. `Height` would be normalised
  by a measured bound with a margin while `MaxSlope`, the ray march, `FloorFor` and the quadtree's
  height ranges keep a proven one, which moves Tasks 10 (R3), 16 (F2), 23 (S1), 31 (T1) and 33 (T3);
  those are re-planned against the developer's ruling before any tree opens.

- [ ] **Step 1: Create the slice branch and the three track trees**

```bash
cd /home/matt/Development/deepspace && git fetch -q . main && \
git branch feat/landing-b main && \
git worktree add .worktrees/landing-b-f -b feat/landing-b-f feat/landing-b && \
git worktree add .worktrees/landing-b-s -b feat/landing-b-s feat/landing-b && \
git worktree add .worktrees/landing-b-t -b feat/landing-b-t feat/landing-b
```

Expected: three `Preparing worktree` lines, no errors.

- [ ] **Step 2: Prove each tree builds and the suite is green before anything changes**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh
```

Expected: `Result: Succeeded` from the build, then `passed: N` with no `FAILED`. Record N; every
later full-suite run must show at least N plus this slice's new tests. (The other two trees are
the same commit; building them is their tracks' first step.)

---

## Task 14 (B1): the first-day gate -- ProceduralMeshComponent's memory and draw cost

**Owner:** T. **Depends on:** B0. Spec decision 6 (*PMC's two costs, measured as the first task
of (b)*), sign-off item 15.

This is a measurement with a verdict, and the verdict chooses between Task T6 as written (PMC)
and Task T5 (the custom primitive) feeding it. It needs no relief and no quadtree: memory and draw
cost depend on the tile count and the fixed topology, not on the heights.

**Files:**
- Modify: `Source/DeepSpace/DeepSpace.Build.cs` (the `PrivateDependencyModuleNames` block, about
  line 41): add `"ProceduralMeshComponent"`.
- Modify: `DeepSpace.uproject` (`"Plugins"` array, line 13): add the plugin entry. The plugin is
  `EnabledByDefault` in UE 5.8, so this is not what loads it: it is the explicit dependency UBT
  otherwise warns is missing (a module depending on a plugin the project does not list). Track T owns
  the `.uproject` in slice (b).
- Create: `Source/DeepSpace/Tests/Eyes/TerrainBudgetEyesTest.cpp` (`Eyes.TerrainBudget`).
- Modify: `docs/superpowers/specs/2026-09-27-landing-design.md`, decision 6, a *Measured*
  paragraph with the numbers and the verdict.

**Interfaces:** Consumes: `UProceduralMeshComponent::CreateMeshSection`, `UpdateMeshSection`,
`GetProcMeshSection` (engine). Produces: the verdict (`PMC` or `CUSTOM PRIMITIVE`), written into
the spec, which Task T6 reads.

- [ ] **Step 1: Depend on the plugin**

In `Source/DeepSpace/DeepSpace.Build.cs`, after the `MeshDescription` line, add:

```csharp
		// ProceduralMeshComponent: the terrain's tiles (landing decision 6) --
		// pooled runtime mesh sections, rebuilt off the game thread and
		// uploaded with UpdateMeshSection. Gated on the first day of slice (b)
		// by Eyes.TerrainBudget; UTerrainTileComponent is the named fallback.
		PrivateDependencyModuleNames.Add("ProceduralMeshComponent");
```

In `DeepSpace.uproject`, add to the `"Plugins"` array (after `PythonScriptPlugin`'s entry):

```json
		{
			"Name": "ProceduralMeshComponent",
			"Enabled": true
		}
```

- [ ] **Step 2: Write the gate test**

Create `Source/DeepSpace/Tests/Eyes/TerrainBudgetEyesTest.cpp`:

```cpp
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "RenderingThread.h"
#include "TextureResource.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * THE FIRST-DAY GATE of landing slice (b) (decision 6, sign-off item 15): what
 * ProceduralMeshComponent costs at the tile counts the quadtree simulation
 * gave -- 2,200 tiles at 1.5 m, 2,500 at the MaxTiles ceiling -- in the
 * fixed topology every real tile has (33 x 33 and a 4 x 33 skirt, 2,304
 * triangles). Named outside DeepSpace. so ./test.sh never runs it; it must
 * render, so it never runs under -nullrhi:
 *
 *   Tools/eyes.sh Eyes.TerrainBudget
 *
 * It asserts the gate, so a red run is the verdict CUSTOM PRIMITIVE:
 *   - PMC's CPU copies (FProcMeshVertex, the double position and normal
 *     every section keeps) at 2,500 tiles, at most 400 MB;
 *   - a 4K capture of 2,200 tiles, over the empty scene's, at most 6 ms of
 *     render thread and GPU together (PMC draws on the dynamic path, so
 *     every tile's batch is rebuilt every pass);
 *   - moving every tile, as the ship-is-the-origin frame does every frame,
 *     at most 2 ms of game thread, and at most 2 ms more on the capture
 *     after it (GPU Scene updates).
 * The numbers go to Saved/Eyes/TerrainBudget/report.txt and the log.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainBudgetEyesTest, "Eyes.TerrainBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TerrainBudgetLocal
{
    constexpr int32 Cells = 32;
    constexpr int32 Side = Cells + 1;
    constexpr int32 Tiles = 2200;
    constexpr int32 MaxTiles = 2500;
    constexpr double TileCm = 1900.0;
    constexpr int32 Warmups = 3;
    constexpr int32 Captures = 20;

    constexpr double CopyBudgetMB = 400.0;
    constexpr double DrawBudgetMs = 6.0;
    constexpr double MoveBudgetMs = 2.0;
    constexpr double MovedDrawBudgetMs = 2.0;

    struct FSynthetic
    {
        TArray<FVector> Positions;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UV0;
        TArray<FVector2D> UV1;
        TArray<FVector2D> UV2;
    };

    void AddVertex(FSynthetic& Tile, const FVector& Position)
    {
        Tile.Positions.Add(Position);
        Tile.Normals.Add(FVector::UpVector);
        Tile.UV0.Add(FVector2D::ZeroVector);
        Tile.UV1.Add(FVector2D::ZeroVector);
        Tile.UV2.Add(FVector2D(1.0, 0.0));
    }

    /** The real topology, flat: nothing measured here depends on heights. */
    FSynthetic MakeTile()
    {
        FSynthetic Tile;
        for (int32 J = 0; J < Side; ++J)
        {
            for (int32 I = 0; I < Side; ++I)
            {
                AddVertex(Tile, FVector(I * TileCm / Cells, J * TileCm / Cells, 0.0));
            }
        }
        TArray<int32> Ring;
        for (int32 I = 0; I < Side; ++I) { Ring.Add(I); }
        for (int32 J = 0; J < Side; ++J) { Ring.Add(J * Side + Cells); }
        for (int32 I = Cells; I >= 0; --I) { Ring.Add(Cells * Side + I); }
        for (int32 J = Cells; J >= 0; --J) { Ring.Add(J * Side); }
        for (const int32 Grid : Ring)
        {
            AddVertex(Tile, Tile.Positions[Grid] - FVector(0.0, 0.0, 50.0));
        }
        for (int32 J = 0; J < Cells; ++J)
        {
            for (int32 I = 0; I < Cells; ++I)
            {
                const int32 A = J * Side + I;
                const int32 B = A + 1;
                const int32 C = A + Side;
                const int32 D = C + 1;
                Tile.Triangles.Append({ A, C, B, B, C, D });
            }
        }
        const int32 SkirtBase = Side * Side;
        for (int32 Edge = 0; Edge < 4; ++Edge)
        {
            for (int32 K = 0; K < Cells; ++K)
            {
                const int32 R0 = Edge * Side + K;
                Tile.Triangles.Append({ Ring[R0], Ring[R0 + 1], SkirtBase + R0,
                                        Ring[R0 + 1], SkirtBase + R0 + 1, SkirtBase + R0 });
            }
        }
        return Tile;
    }

    /** Render thread and GPU together: capture, flush, then read one pixel
     *  back, which cannot return before the GPU has drawn the frame. */
    double CaptureMs(USceneCaptureComponent2D* Capture, UTextureRenderTarget2D* Target)
    {
        TArray<FColor> Pixel;
        auto One = [&]()
        {
            Capture->CaptureScene();
            FlushRenderingCommands();
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(0, 0, 1, 1));
        };
        for (int32 Warm = 0; Warm < Warmups; ++Warm)
        {
            One();
        }
        const double Start = FPlatformTime::Seconds();
        for (int32 Shot = 0; Shot < Captures; ++Shot)
        {
            One();
        }
        return (FPlatformTime::Seconds() - Start) * 1000.0 / Captures;
    }
}

bool FTerrainBudgetEyesTest::RunTest(const FString& Parameters)
{
    using namespace TerrainBudgetLocal;

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TerrainBudgetWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();

    AActor* Holder = World->SpawnActor<AActor>();
    USceneComponent* Root = NewObject<USceneComponent>(Holder, TEXT("Root"));
    Holder->SetRootComponent(Root);
    Root->RegisterComponent();
    UMaterialInterface* Material = UMaterial::GetDefaultMaterial(MD_Surface);

    const FSynthetic Tile = MakeTile();
    const int32 PerRow = FMath::CeilToInt(FMath::Sqrt(static_cast<double>(Tiles)));
    const uint64 UsedBefore = FPlatformMemory::GetStats().UsedPhysical;
    const double CreateStart = FPlatformTime::Seconds();
    TArray<UProceduralMeshComponent*> Pool;
    for (int32 N = 0; N < Tiles; ++N)
    {
        UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Holder);
        Mesh->SetupAttachment(Root);
        Mesh->bUseAsyncCooking = false;
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false);
        Mesh->bAffectDistanceFieldLighting = false;
        Mesh->bAffectDynamicIndirectLighting = false;
        Mesh->bVisibleInRayTracing = false;
        Mesh->RegisterComponent();
        Mesh->CreateMeshSection(0, Tile.Positions, Tile.Triangles, Tile.Normals, Tile.UV0, Tile.UV1, Tile.UV2,
                                TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);
        Mesh->SetMaterial(0, Material);
        Mesh->SetRelativeLocation(FVector((N % PerRow - PerRow / 2) * TileCm, (N / PerRow - PerRow / 2) * TileCm, 0.0));
        Pool.Add(Mesh);
    }
    FlushRenderingCommands();
    const double CreateMsEach = (FPlatformTime::Seconds() - CreateStart) * 1000.0 / Tiles;
    const double ProcessMB = (static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) - UsedBefore) / 1048576.0;

    int64 CopyBytes = 0;
    for (UProceduralMeshComponent* Mesh : Pool)
    {
        if (const FProcMeshSection* Section = Mesh->GetProcMeshSection(0))
        {
            CopyBytes += Section->ProcVertexBuffer.Num() * static_cast<int64>(sizeof(FProcMeshVertex))
                       + Section->ProcIndexBuffer.Num() * static_cast<int64>(sizeof(uint32));
        }
    }
    const double CopyMBAtMax = CopyBytes / 1048576.0 * MaxTiles / Tiles;

    // What a pooled tile costs to refill: UpdateMeshSection, the per-frame
    // upload the budget allows four of.
    const double UpdateStart = FPlatformTime::Seconds();
    for (int32 N = 0; N < 100; ++N)
    {
        Pool[N]->UpdateMeshSection(0, Tile.Positions, Tile.Normals, Tile.UV0, Tile.UV1, Tile.UV2,
                                   TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>());
    }
    FlushRenderingCommands();
    const double UpdateMsEach = (FPlatformTime::Seconds() - UpdateStart) * 1000.0 / 100.0;

    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Holder);
    Capture->SetupAttachment(Root);
    Capture->RegisterComponent();
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Holder);
    Target->InitCustomFormat(3840, 2160, PF_B8G8R8A8, false);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->FOVAngle = 90.0f;
    Capture->CaptureSource = SCS_FinalColorLDR;
    // 800 m up, straight down: the whole 890 m square of tiles is in view.
    Capture->SetRelativeLocationAndRotation(FVector(0.0, 0.0, 80000.0), FRotator(-90.0, 0.0, 0.0));

    for (UProceduralMeshComponent* Mesh : Pool) { Mesh->SetVisibility(false); }
    const double EmptyMs = CaptureMs(Capture, Target);
    for (UProceduralMeshComponent* Mesh : Pool) { Mesh->SetVisibility(true); }
    const double TilesMs = CaptureMs(Capture, Target);

    const double MoveStart = FPlatformTime::Seconds();
    for (UProceduralMeshComponent* Mesh : Pool)
    {
        Mesh->SetRelativeLocation(Mesh->GetRelativeLocation() + FVector(1.0, 0.0, 0.0));
    }
    const double MoveMs = (FPlatformTime::Seconds() - MoveStart) * 1000.0;
    const double MovedMs = CaptureMs(Capture, Target);

    const double DrawMs = TilesMs - EmptyMs;
    const double MovedDrawMs = MovedMs - TilesMs;
    const FString Report = FString::Printf(
        TEXT("tiles %d, %d triangles each\n")
        TEXT("create %.3f ms/tile, UpdateMeshSection %.3f ms/tile\n")
        TEXT("PMC CPU copies %.1f MB at %d tiles (%.1f MB counted at %d); process grew %.1f MB\n")
        TEXT("4K capture: empty %.2f ms, tiles %.2f ms (+%.2f), after moving every tile %.2f ms (+%.2f)\n")
        TEXT("moving every tile: %.2f ms of game thread\n"),
        Tiles, Tile.Triangles.Num() / 3, CreateMsEach, UpdateMsEach, CopyMBAtMax, MaxTiles,
        CopyBytes / 1048576.0, Tiles, ProcessMB, EmptyMs, TilesMs, DrawMs, MovedMs, MovedDrawMs, MoveMs);
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/TerrainBudget");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(Report, *(Dir / TEXT("report.txt")));
    AddInfo(Report);

    TestTrue(FString::Printf(TEXT("PMC's CPU copies at %d tiles are within %.0f MB (%.1f)"), MaxTiles, CopyBudgetMB, CopyMBAtMax),
             CopyMBAtMax <= CopyBudgetMB);
    TestTrue(FString::Printf(TEXT("2,200 tiles cost at most %.0f ms of a 4K frame (%.2f)"), DrawBudgetMs, DrawMs),
             DrawMs <= DrawBudgetMs);
    TestTrue(FString::Printf(TEXT("moving every tile costs at most %.0f ms of game thread (%.2f)"), MoveBudgetMs, MoveMs),
             MoveMs <= MoveBudgetMs);
    TestTrue(FString::Printf(TEXT("and at most %.0f ms more to draw after (%.2f)"), MovedDrawBudgetMs, MovedDrawMs),
             MovedDrawMs <= MovedDrawBudgetMs);

    World->EndPlay(EEndPlayReason::RemovedFromWorld);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
```

- [ ] **Step 3: Build and run the gate**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && Tools/eyes.sh Eyes.TerrainBudget; \
cat Saved/Eyes/TerrainBudget/report.txt
```

Expected: the build succeeds; the run prints its report. There is no "failing first" for a
measurement: the four `TestTrue`s are the gate. A green run is the verdict **PMC**; a red run,
**CUSTOM PRIMITIVE**, and names which budget failed.

- [ ] **Step 4: Record the verdict in the spec**

In `docs/superpowers/specs/2026-09-27-landing-design.md`, decision 6, after the paragraph that
begins **PMC's two costs, measured as the first task of (b).**, add a paragraph built from the
report (fill in the measured numbers from `report.txt`; the words around them are fixed):

```markdown
**Measured, the first day of (b)** (`Eyes.TerrainBudget`, this machine's RTX 4070 Ti SUPER, 4K):
PMC's CPU copies <copy> MB at 2,500 tiles; 2,200 tiles cost <draw> ms of a 4K capture over the
empty scene; moving every tile <move> ms of game thread and <moved> ms more to draw; one
`UpdateMeshSection` <update> ms. **Verdict: <PMC | CUSTOM PRIMITIVE>.** <If CUSTOM PRIMITIVE:
which budget failed, and that `UTerrainTileComponent` (Task T5) replaces PMC in `AWorldGround`.>
```

- [ ] **Step 5: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
git add Source/DeepSpace/DeepSpace.Build.cs DeepSpace.uproject Source/DeepSpace/Tests/Eyes/TerrainBudgetEyesTest.cpp \
        docs/superpowers/specs/2026-09-27-landing-design.md && \
git commit -m "$(cat <<'EOF'
test(terrain): the first-day gate -- PMC's memory and draw cost at the simulated tile counts

Eyes.TerrainBudget measures ProceduralMeshComponent in the real tile
topology at 2,200 and 2,500 tiles, at 4K, with every tile moved as the
ship-is-the-origin frame moves it, and asserts the gate (decision 6,
sign-off item 15). The verdict is recorded in the spec.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Prove the gate can fail**

`Tools/mutate.sh` runs `./test.sh`, which is headless (`-nullrhi`), and an `Eyes.*` test cannot
render there, so this one mutant is made by hand, through the renderer:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
sed -i 's/constexpr double CopyBudgetMB = 400.0;/constexpr double CopyBudgetMB = 1.0;/' \
  Source/DeepSpace/Tests/Eyes/TerrainBudgetEyesTest.cpp && \
./build.sh && Tools/eyes.sh Eyes.TerrainBudget; echo "exit $?"; \
git checkout -- Source/DeepSpace/Tests/Eyes/TerrainBudgetEyesTest.cpp && ./build.sh
```

Expected: `exit 1`, and the log names `PMC's CPU copies at 2500 tiles are within 1 MB` as failed.
Then the file is restored and rebuilt.

---

## Task 15 (F1): gravity -- the sum from every body, held, never in the velocity

**Owner:** F. **Depends on:** B0 (and slice (a)'s `GMSunCm3PerS2`, `GMEarthCm3PerS2`). Spec
decision 4.

**Files:**
- Create: `Source/DeepSpace/Ship/ShipGravity.h`, `Source/DeepSpace/Ship/ShipGravity.cpp`
- Modify: `Source/DeepSpace/Ship/ShipFlightState.h` (public block after `GetRoom`, about line 198;
  private members after `Surfaces`, about line 377), `Source/DeepSpace/Ship/ShipFlightState.cpp`
  (after `GetRoom`, about line 130)
- Create: `Source/DeepSpace/Tests/ShipGravityTest.cpp` (`DeepSpace.Ship.Gravity`)
- Modify: `docs/decisions/0005-the-ship-is-the-origin.md` (an amendment at the end)

**Interfaces:**
- Consumes: `UniverseUnits::GMEarthCm3PerS2`, `GMSunCm3PerS2`, `CmPerEarthRadius`, `CmPerAU`,
  `CmPerSolarRadius`; `FUniversePosition::operator-`.
- Produces: `struct FGravityWell { FUniversePosition Centre; double Mu = 0.0; double RadiusCm = 0.0; }`;
  `FVector ShipFlight::GravityAt(TConstArrayView<FGravityWell> Wells, const FUniversePosition& Position)`;
  `inline constexpr double ShipFlight::StandardGravityCmS2 = 980.665`;
  `void FShipFlightState::SetWells(TArray<FGravityWell>)`, `TConstArrayView<FGravityWell> GetWells() const`,
  `FVector GetLocalGravity() const`, `FVector GetThrustAcceleration() const`.

- [ ] **Step 1: Write the failing test**

Create `Source/DeepSpace/Tests/ShipGravityTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipGravity.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 4: gravity is real -- inverse-square, from every body, at
 * real masses -- and the boosters hold it, so it never enters the velocity.
 * Known values from the constants actually used, and the proof that a lever
 * flies the same path beside a heavy world as in empty space.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipGravityTest, "DeepSpace.Ship.Gravity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipGravityTestLocal
{
    FUniversePosition Somewhere()
    {
        return FUniversePosition(FInt64Vector(3, -2, 0), FVector(1.0e12, 5.0e12, 0.0));
    }

    FVector Out()
    {
        return FVector(-0.8, 0.45, 0.4).GetSafeNormal();
    }
}

bool FShipGravityTest::RunTest(const FString& Parameters)
{
    using namespace ShipGravityTestLocal;
    const double Re = UniverseUnits::CmPerEarthRadius;

    const FGravityWell Earth{ Somewhere(), UniverseUnits::GMEarthCm3PerS2, Re };
    const FVector AtSurface = ShipFlight::GravityAt({ Earth }, Somewhere() + Out() * Re);
    // GMEarth over CmPerEarthRadius squared is 979.8398 cm/s^2 (the spec's
    // "979.85" rounds it; Task P1 pins the same number).
    TestTrue(FString::Printf(TEXT("an Earth pulls 979.840 cm/s^2 at its equatorial radius (%.4f)"), AtSurface.Size()),
             FMath::IsNearlyEqual(AtSurface.Size(), 979.8398, 1.0e-3));
    TestTrue(TEXT("toward its centre"), FVector::DotProduct(AtSurface.GetSafeNormal(), -Out()) > 1.0 - 1e-12);

    const FVector Halfway = ShipFlight::GravityAt({ Earth }, Somewhere() + Out() * (0.5 * Re));
    TestTrue(TEXT("inside, a uniform sphere: half the pull at half the radius, never a singularity"),
             FMath::IsNearlyEqual(Halfway.Size(), 0.5 * AtSurface.Size(), 1e-9 * AtSurface.Size()));
    TestTrue(TEXT("and none at the centre"), ShipFlight::GravityAt({ Earth }, Somewhere()).IsZero());

    const FGravityWell Sun{ Somewhere() + FVector(0.0, 0.0, 3.0e13), UniverseUnits::GMSunCm3PerS2, UniverseUnits::CmPerSolarRadius };
    const double AtAU = ShipFlight::GravityAt({ Sun }, Sun.Centre + Out() * UniverseUnits::CmPerAU).Size();
    TestTrue(FString::Printf(TEXT("the Sun pulls 0.593 cm/s^2 at 1 AU (%.5f)"), AtAU), FMath::IsNearlyEqual(AtAU, 0.593, 0.0005));

    const FUniversePosition Between = Somewhere() + Out() * (3.0 * Re);
    const FVector Both = ShipFlight::GravityAt({ Earth, Sun }, Between);
    const FVector Summed = ShipFlight::GravityAt({ Earth }, Between) + ShipFlight::GravityAt({ Sun }, Between);
    TestTrue(TEXT("every body's pull, summed (ruling 4)"), (Both - Summed).Size() <= 1e-12 * Both.Size());
    TestTrue(TEXT("no wells, no pull"), ShipFlight::GravityAt({}, Between).IsZero());

    // The velocity is unaffected by gravity (decision 4): a full lever beside
    // a 3 g world flies exactly the path it flies with no wells at all.
    const FGravityWell Heavy{ Somewhere(), 3.0 * ShipFlight::StandardGravityCmS2 * Re * Re, Re };
    FShipFlightState Free;
    FShipFlightState Pulled;
    const FUniversePosition Start = Somewhere() + Out() * (Re + 5.0e6);
    for (FShipFlightState* State : { &Free, &Pulled })
    {
        State->SetUniverseTransform(Start, FQuat::Identity);
        FShipFlightCommand Command;
        Command.Throttle = 1.0;
        State->SetCommand(Command);
    }
    Pulled.SetWells({ Heavy });
    for (int32 Frame = 0; Frame < 600; ++Frame)
    {
        Free.Step(1.0 / 60.0);
        Pulled.Step(1.0 / 60.0);
    }
    TestTrue(TEXT("a full-lever path beside a 3 g world is the empty-space path, exactly"),
             (Pulled.GetUniversePosition() - Free.GetUniversePosition()).IsZero() && Pulled.GetVelocity() == Free.GetVelocity());
    const FVector Expected = ShipFlight::GravityAt({ Heavy }, Pulled.GetUniversePosition());
    TestTrue(TEXT("the flight state reports the pull where the ship is"),
             (Pulled.GetLocalGravity() - Expected).IsNearlyZero(1e-9));
    TestTrue(TEXT("and the boosters' thrust is what the ship did less what gravity would have done"),
             (Pulled.GetThrustAcceleration() - (Pulled.GetLinearAcceleration() - Expected)).IsNearlyZero(1e-9));
    TestTrue(TEXT("a flight state with no wells feels none"), Free.GetLocalGravity().IsZero());
    TestEqual(TEXT("and keeps the wells it was handed"), Pulled.GetWells().Num(), 1);
    return true;
}

#endif
```

- [ ] **Step 2: Run it and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh
```

Expected: the build FAILS: `Ship/ShipGravity.h: No such file or directory` (the test cannot
compile before the header exists; that is this test's red).

- [ ] **Step 3: Write the law**

Create `Source/DeepSpace/Ship/ShipGravity.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Universe/UniversePosition.h"

/**
 * One body's pull, as the flight law needs it (landing decision 4): where it
 * is, its GM, and its radius. Built once a frame by UShipSubsystem from the
 * system here, beside the floor spheres; none in transit.
 */
struct DEEPSPACE_API FGravityWell
{
    FUniversePosition Centre;

    /** GM, cm^3/s^2: FSkyBody::GravParam. */
    double Mu = 0.0;

    /** Mean radius, cm. Inside it the pull falls linearly to nothing at the
     *  centre, a uniform sphere, so a point the ground's hard stop has not
     *  yet lifted never divides by nearly zero. */
    double RadiusCm = 0.0;
};

namespace ShipFlight
{
    /** One Earth gravity, cm/s^2 (the standard, 9.80665 m/s^2): what "g" means
     *  wherever this slice says "per g" -- the hold's watts, the climb top. */
    inline constexpr double StandardGravityCmS2 = 980.665;

    /**
     * The pull at Position, cm/s^2, universe axes: the inverse-square sum over
     * every well (ruling 4 -- the star and every world). Every separation goes
     * through FUniversePosition's operator-, so it keeps its digits at any
     * range. Pure. The flight state reports it and never integrates it: the
     * boosters hold every lever against it (decision 4).
     */
    DEEPSPACE_API FVector GravityAt(TConstArrayView<FGravityWell> Wells, const FUniversePosition& Position);
}
```

Create `Source/DeepSpace/Ship/ShipGravity.cpp`:

```cpp
#include "Ship/ShipGravity.h"

FVector ShipFlight::GravityAt(TConstArrayView<FGravityWell> Wells, const FUniversePosition& Position)
{
    FVector Sum = FVector::ZeroVector;
    for (const FGravityWell& Well : Wells)
    {
        const FVector ToCentre = Well.Centre - Position;
        const double R = ToCentre.Size();
        if (!(R > 0.0) || !(Well.Mu > 0.0))
        {
            continue;
        }
        const double Inside = FMath::Max(Well.RadiusCm, 0.0);
        const double Pull = R >= Inside ? Well.Mu / (R * R) : Well.Mu * R / (Inside * Inside * Inside);
        Sum += ToCentre * (Pull / R);
    }
    return Sum;
}
```

In `Source/DeepSpace/Ship/ShipFlightState.h`, add `#include "Ship/ShipGravity.h"` beside the other
includes, and after `double GetRoom() const;` add:

```cpp
    /**
     * Every body's pull, as wells (landing decision 4): the subsystem hands
     * them over once a frame beside the surfaces, and none in transit. The
     * flight law never adds g dt to the velocity -- the boosters hold every
     * lever against gravity, so the levers mean what they say anywhere --
     * and gravity is felt only as effort, which the subsystem reads here.
     */
    void SetWells(TArray<FGravityWell> NewWells);
    TConstArrayView<FGravityWell> GetWells() const;

    /** The pull where the ship is, cm/s^2, universe axes: the sum over every
     *  well. Asked, never stored. */
    FVector GetLocalGravity() const;

    /** The boosters' proper acceleration, cm/s^2: what the ship did
     *  (GetLinearAcceleration, kinematic, which the hum's "changing" term
     *  keeps reading) less what gravity would have done. */
    FVector GetThrustAcceleration() const;
```

and after `TArray<FFlightSurface> Surfaces;` add `TArray<FGravityWell> Wells;`.

In `Source/DeepSpace/Ship/ShipFlightState.cpp`, after `GetRoom`, add:

```cpp
void FShipFlightState::SetWells(TArray<FGravityWell> NewWells)
{
    Wells = MoveTemp(NewWells);
}

TConstArrayView<FGravityWell> FShipFlightState::GetWells() const
{
    return Wells;
}

FVector FShipFlightState::GetLocalGravity() const
{
    return ShipFlight::GravityAt(Wells, Position);
}

FVector FShipFlightState::GetThrustAcceleration() const
{
    return LastLinearAcceleration - GetLocalGravity();
}
```

- [ ] **Step 4: Build and run: PASS**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh && ./test.sh DeepSpace.Ship.Gravity
```

Expected: `passed: 1`, no `FAILED`.

- [ ] **Step 5: Amend ADR 0005 for gravity**

Append to `docs/decisions/0005-the-ship-is-the-origin.md`:

```markdown
## Amendment, 2026-09-27: gravity is held, never integrated (landing slice b)

Every body pulls, inverse-square at real masses (`ShipFlight::GravityAt`, summed from
`FGravityWell`s the subsystem builds beside the surfaces). The flight state reports the pull
(`GetLocalGravity`) and the boosters' proper acceleration (`GetThrustAcceleration`, the kinematic
acceleration less the pull), and **never adds g dt to the velocity**: the boosters hold every
lever against gravity, so a lever means the same thing beside a 3 g world as between stars, and
at rest the ship stays put anywhere. Gravity is felt as effort -- watts, the hiss, slower climbs,
a starved sink the ground catches -- and only under a solid world's drive floor (landing decision
5). `JumpTo` zeroes the velocity at every arrival; the earlier text of this ADR that says
otherwise predates flight-feel decision 4.
```

- [ ] **Step 6: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
git add Source/DeepSpace/Ship/ShipGravity.h Source/DeepSpace/Ship/ShipGravity.cpp Source/DeepSpace/Ship/ShipFlightState.h \
        Source/DeepSpace/Ship/ShipFlightState.cpp Source/DeepSpace/Tests/ShipGravityTest.cpp docs/decisions/0005-the-ship-is-the-origin.md && \
git commit -m "$(cat <<'EOF'
feat(flight): gravity from every body, held by the boosters, never in the velocity

ShipFlight::GravityAt sums inverse-square pulls from each FGravityWell; the
flight state reports GetLocalGravity and GetThrustAcceleration and flies
exactly the path it would with no wells (landing decision 4). ADR 0005
amended.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7: Prove it can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipGravity.cpp 'Well.Mu / (R * R)' 'Well.Mu / R' DeepSpace.Ship.Gravity && ./build.sh
```

Expected: `KILLED: DeepSpace.Ship.Gravity went red`.

---

## Task 16 (F2): the ground as the flight sees it -- `IGroundField`, the ray march, the approach law, the skim cap, the ETA's law

**Owner:** F. **Depends on:** F1. Spec decisions 10 and 12.

**Files:**
- Create: `Source/DeepSpace/Surface/GroundField.h`, `Source/DeepSpace/Surface/GroundField.cpp`
- Modify: `Source/DeepSpace/Ship/ShipFlightSurface.h` (the struct, lines 15-31; the namespace,
  lines 42-139), `Source/DeepSpace/Ship/ShipFlightSurface.cpp` (append)
- Create: `Source/DeepSpace/Tests/GroundFixtures.h`,
  `Source/DeepSpace/Tests/ShipGroundLawTest.cpp` (`DeepSpace.Ship.Landing.ApproachLaw`,
  `.RayToGround`, `.SkimCap`, `.SecondsToGround`)

**Interfaces:**
- Consumes: `FWorldRelief`, `FWorldReliefParams`, `EGround` (slice a); `ShipFlight::BrakingMargin`.
- Produces: `IGroundField`, `FReliefGround`, `FGroundFieldRef`, `ShipGround::FromRelief`,
  `ShipGround::NormalAt`; `FFlightSurface::bWorld`, `::Ground`, `::HasGround()`;
  `ShipFlight::GroundAt`, `RayToGround`, `GroundApproachSpeed`, `SkimCap`, `FGroundLaw`,
  `SecondsToGround`; the constants listed in *Interfaces at the seams*; test fixtures
  `GroundFixtures::FCrossedSines`, `GroundFixtures::FixtureParams()`, `GroundFixtures::HomeWorld(int32)`,
  `GroundFixtures::SurfaceOver(...)`, `GroundFixtures::Above(...)`, `GroundFixtures::Level(...)`.

- [ ] **Step 1: Write the fixtures**

Create `Source/DeepSpace/Tests/GroundFixtures.h`:

```cpp
#pragma once

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Ship/ShipFlightSurface.h"
#include "Sky/SkySystem.h"
#include "Surface/GroundField.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

/**
 * Grounds for the landing tests: a cheap analytic one with a chosen slope
 * bound, a fixed relief, and the real worlds of home by orbit. Data, not a
 * generator: the real worlds come from the universe as the game builds it.
 */
namespace GroundFixtures
{
    /**
     * Two crossed sines about the +Z pole, h = A (sin(k R D.x) + sin(k R D.y)) / 2:
     * arc lengths R D.x and R D.y, so it is only meaningful near the pole,
     * where every test that uses it flies. Smooth, a few nanoseconds a sample,
     * and with the slope bound set to whatever the test needs -- the real
     * relief's, for GroundAlwaysCatches (spec decision 10).
     */
    class FCrossedSines final : public IGroundField
    {
    public:
        FCrossedSines(double InRadiusCm, double InAmplitudeCm, double InWavelengthCm)
            : R(InRadiusCm), A(InAmplitudeCm), K(2.0 * UE_DOUBLE_PI / InWavelengthCm)
        {
        }

        /** The same field with its steepest slope, rise over run, set to Slope. */
        static FCrossedSines WithSlope(double InRadiusCm, double InWavelengthCm, double Slope)
        {
            const double K = 2.0 * UE_DOUBLE_PI / InWavelengthCm;
            return FCrossedSines(InRadiusCm, 2.0 * Slope / (K * UE_DOUBLE_SQRT_2), InWavelengthCm);
        }

        virtual double RadiusCm() const override { return R; }

        virtual double Height(const FVector3d& D, double) const override
        {
            return 0.5 * A * (FMath::Sin(K * R * D.X) + FMath::Sin(K * R * D.Y));
        }

        virtual double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const override
        {
            Grad = FVector3d(0.5 * A * K * R * FMath::Cos(K * R * D.X), 0.5 * A * K * R * FMath::Cos(K * R * D.Y), 0.0);
            return Height(D, FootprintCm);
        }

        virtual double OmittedBoundCm(double) const override { return 0.0; }
        virtual double MaxHeightCm() const override { return FMath::Abs(A); }
        virtual double MinHeightCm() const override { return -FMath::Abs(A); }
        virtual double MaxSlope() const override { return 0.5 * FMath::Abs(A) * K * UE_DOUBLE_SQRT_2; }

    private:
        double R;
        double A;
        double K;
    };

    /** A barren world of 0.9 Earth radii with a 6.4 km peak, craters kept:
     *  the shape of Baemsekai IV without needing the universe. */
    inline FWorldReliefParams FixtureParams()
    {
        FWorldReliefParams Params;
        Params.SeedOffset = FVector3d(12.5, 40.25, 7.75);
        Params.RadiusCm = 0.9 * UniverseUnits::CmPerEarthRadius;
        Params.PeakCm = 6.4e5;
        Params.Cratering = 1.0;
        Params.Ground = EGround::Solid;
        return Params;
    }

    /** Off the universe origin and across chunks, as the real galaxy is. */
    inline FUniversePosition Somewhere()
    {
        return FUniversePosition(FInt64Vector(3, -2, 0), FVector(1.0e12, 5.0e12, 0.0));
    }

    /** A solid world's surface: the drive floor sphere at DriveFloorCm over
     *  the datum, and the ground. */
    inline FFlightSurface SurfaceOver(const FGroundFieldRef& Ground, double DriveFloorCm,
                                      const FUniversePosition& Centre = Somewhere())
    {
        FFlightSurface Surface;
        Surface.Centre = Centre;
        Surface.Radius = Ground->RadiusCm();
        Surface.Floor = DriveFloorCm;
        Surface.bWorld = true;
        Surface.Ground = Ground;
        return Surface;
    }

    /** AglCm above the ground at direction D (unit, universe axes). */
    inline FUniversePosition Above(const FFlightSurface& Surface, const FVector3d& D, double AglCm)
    {
        return Surface.Centre + FVector(D) * (Surface.Radius + Surface.Ground->Height(D, 0.0) + AglCm);
    }

    /** Level over D: body +Z along the radial up, the nose along Heading
     *  projected onto the horizon. */
    inline FQuat Level(const FVector3d& D, const FVector& Heading = FVector(1.0, 0.0, 0.0))
    {
        const FVector Up(D);
        return FRotationMatrix::MakeFromXZ(Heading - Up * (Heading | Up), Up).ToQuat();
    }

    /** A world of home by orbit (I is 1): seed 20260925's start system,
     *  Baemsekai, as the game builds it from the ini's seed and priors. The
     *  caller checks the name, so a priors change that moves the fixture
     *  fails loudly rather than landing somewhere else (spec, Fixture worlds). */
    struct FHomeWorld
    {
        FSkyBody Body;
        FGroundFieldRef Ground;
        FString Name;
    };

    inline TOptional<FHomeWorld> HomeWorld(int32 Orbit)
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GroundFixtureWorld"));
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        TOptional<FHomeWorld> Out;
        if (const UUniverseSubsystem* Universe = World->GetSubsystem<UUniverseSubsystem>())
        {
            if (const TOptional<FStarSystem> Home = Universe->GetSystem(Universe->GetStartSystem()))
            {
                const FSkySystem Sky = FSkySystem::FromSystem(*Home, {});
                if (Sky.Bodies.IsValidIndex(Orbit) && Sky.Bodies[Orbit].Ground == EGround::Solid)
                {
                    FHomeWorld Found;
                    Found.Body = Sky.Bodies[Orbit];
                    Found.Ground = ShipGround::FromRelief(Found.Body.Relief);
                    Found.Name = Found.Body.Id.ToString();
                    Out = Found;
                }
            }
        }
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return Out;
    }
}
```

- [ ] **Step 2: Write the failing tests**

Create `Source/DeepSpace/Tests/ShipGroundLawTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightSurface.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The ground's laws as pure arithmetic (landing decisions 10 and 12): the
 * approach law with a knee the boosters can follow, the ray march that
 * never says "no hit" when it has not seen, the skim cap, and the ETA that
 * integrates the same laws. Siblings under DeepSpace.Ship.Landing, which is
 * never itself a test.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundApproachLawTest, "DeepSpace.Ship.Landing.ApproachLaw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundRayTest, "DeepSpace.Ship.Landing.RayToGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundSkimCapTest, "DeepSpace.Ship.Landing.SkimCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundSecondsTest, "DeepSpace.Ship.Landing.SecondsToGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GroundLawTestLocal
{
    constexpr double Boost = 2.0e5;   // 2 km/s^2
    constexpr double N = ShipFlight::DefaultApproachSeconds;
    constexpr double Touch = ShipFlight::DefaultTouchdownSpeed;
    constexpr double Step = 1.0 / 120.0;

    ShipFlight::FGroundLaw FullLaw()
    {
        return { Boost, N, Touch, ShipFlight::DefaultSkimSeconds, ShipFlight::DefaultSkimFloor };
    }
}

bool FGroundApproachLawTest::RunTest(const FString& Parameters)
{
    using namespace GroundLawTestLocal;
    const double Margin = ShipFlight::BrakingMargin * Boost;

    // The demanded deceleration, v dv/dD, never exceeds 0.8 A: an inertial
    // ship can follow the law all the way down.
    double Worst = 0.0;
    for (double D = 1.0; D < 3.0e7; D *= 1.002)
    {
        const double V = ShipFlight::GroundApproachSpeed(D, Boost, N, 0.0, 0.0);
        const double Next = ShipFlight::GroundApproachSpeed(D * 1.0001, Boost, N, 0.0, 0.0);
        Worst = FMath::Max(Worst, V * (Next - V) / (D * 1.0e-4) / Margin);
    }
    TestTrue(FString::Printf(TEXT("the law never demands more than 0.8 A (worst %.4f of it)"), Worst), Worst <= 1.0 + 1e-3);

    const double Knee = Margin * N * N;
    TestTrue(FString::Printf(TEXT("the knee is 25.6 km at full thrust (%.1f km)"), Knee / 1.0e5), FMath::IsNearlyEqual(Knee, 2.56e6, 1.0));
    TestTrue(TEXT("where the exponential's D / N meets the braking parabola"),
             FMath::IsNearlyEqual(ShipFlight::GroundApproachSpeed(Knee, Boost, N, 0.0, 0.0), Knee / N, 1e-6)
             && FMath::IsNearlyEqual(ShipFlight::GroundApproachSpeed(Knee * (1.0 + 1e-9), Boost, N, 0.0, 0.0), Knee / N, 1.0));
    TestTrue(TEXT("starved, a quarter thrust, the knee is 6.4 km"),
             FMath::IsNearlyEqual(ShipFlight::BrakingMargin * 0.25 * Boost * N * N, 6.4e5, 1.0));
    TestEqual(TEXT("a metre up, 0.5 m/s: never the 57 m/s the braking curve allows"),
              ShipFlight::GroundApproachSpeed(100.0, Boost, N, Touch, Step), Touch);
    TestTrue(TEXT("which MaySpeed would have allowed there"), ShipFlight::MaySpeed(100.0, Boost, N, Step) > 5.0e3);
    TestEqual(TEXT("under the substep's own reach, D / Step: no substep crosses the ground"),
              ShipFlight::GroundApproachSpeed(0.2, Boost, N, Touch, Step), 0.2 / Step);
    TestEqual(TEXT("at the ground, nothing"), ShipFlight::GroundApproachSpeed(0.0, Boost, N, Touch, Step), 0.0);
    TestEqual(TEXT("ds.Land.ApproachSeconds 0 is clamped to half a second, never a division by zero"),
              ShipFlight::GroundApproachSpeed(1.0e4, Boost, 0.0, Touch, 0.0),
              ShipFlight::GroundApproachSpeed(1.0e4, Boost, ShipFlight::MinApproachSeconds, Touch, 0.0));
    TestEqual(TEXT("800 m up the full 200 m/s sink first binds"), ShipFlight::GroundApproachSpeed(8.0e4, Boost, N, Touch, 0.0), 2.0e4);
    return true;
}

bool FGroundRayTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    const FGroundFieldRef Relief = ShipGround::FromRelief(FixtureParams());
    const FFlightSurface World = SurfaceOver(Relief, 1.0e6 + Relief->MaxHeightCm());
    const FVector3d Pole(0.0, 0.0, 1.0);

    // Under the ground: heading out it may always climb; heading in, 0.
    const FUniversePosition Under = World.Centre + FVector(Pole) * (World.Radius + Relief->Height(Pole, 0.0) - 50.0);
    TestFalse(TEXT("under the ground, a ray heading up meets nothing"),
              ShipFlight::RayToGround(World, Under, FVector(Pole), 0.0, 1.0e6).IsSet());
    const TOptional<double> Down = ShipFlight::RayToGround(World, Under, -FVector(Pole), 0.0, 1.0e6);
    TestTrue(TEXT("and one heading down meets it at once"), Down.IsSet() && *Down == 0.0);

    // An exhausted march is a hit, never "no hit": a grazing ray a metre
    // over ground steeper than any real world.
    const FGroundFieldRef Steep = MakeShared<FCrossedSines, ESPMode::ThreadSafe>(FCrossedSines::WithSlope(World.Radius, 2.0e3, 60.0));
    const FFlightSurface Cliffs = SurfaceOver(Steep, 1.0e6);
    int32 Steps = 0;
    const TOptional<double> Grazing = ShipFlight::RayToGround(Cliffs, Above(Cliffs, Pole, 100.0), FVector(1.0, 0.0, 0.0), 0.0, 1.0e7, &Steps);
    TestTrue(FString::Printf(TEXT("a march that runs out of steps is a hit (%d steps)"), Steps),
             Grazing.IsSet() && Steps == ShipFlight::GroundMarchSteps);

    // On real relief: from every height the skim cap allows, a level ray
    // sees 2.5 x AGL in its 64 steps, and everything it reports is true --
    // nothing crosses the ground before a hit, or before the lookahead when
    // it says clear.
    FRandomStream Random(20260927);
    int32 Exhausted = 0;
    int32 Lies = 0;
    int32 Trials = 0;
    for (const double Agl : { 2.0e3, 5.0e4, 5.0e5, 5.0e6 })
    {
        for (int32 Trial = 0; Trial < 25; ++Trial, ++Trials)
        {
            const FVector3d D = FVector3d(Random.FRandRange(-0.05, 0.05), Random.FRandRange(-0.05, 0.05), 1.0).GetSafeNormal();
            const FUniversePosition From = Above(World, D, Agl);
            const FVector Heading = FVector::CrossProduct(FVector(D), FVector(Random.GetUnitVector())).GetSafeNormal();
            const double Lookahead = 2.5 * Agl;
            int32 Used = 0;
            const TOptional<double> Hit = ShipFlight::RayToGround(World, From, Heading, 0.0, Lookahead, &Used);
            Exhausted += Used >= ShipFlight::GroundMarchSteps ? 1 : 0;
            const double Seen = Hit ? *Hit : Lookahead;
            for (int32 Sample = 0; Sample <= 400; ++Sample)
            {
                const double T = Seen * Sample / 400.0 * 0.999;
                const FVector P = (From + Heading * T) - World.Centre;
                const double R = P.Size();
                if (R - World.Radius - Relief->Height(FVector3d(P / R), 0.0) < -1.0)
                {
                    ++Lies;
                    break;
                }
            }
        }
    }
    TestEqual(FString::Printf(TEXT("no march of %d runs out of steps before 2.5 x AGL"), Trials), Exhausted, 0);
    TestEqual(TEXT("and none reports clear ground that is not"), Lies, 0);
    return true;
}

bool FGroundSkimCapTest::RunTest(const FString& Parameters)
{
    const double Skim = ShipFlight::DefaultSkimSeconds;
    const double Floor = ShipFlight::DefaultSkimFloor;
    TestEqual(TEXT("20 km/s at 50 km: cruise's top, so the cap is slack at the regime's top"), ShipFlight::SkimCap(5.0e6, Skim, Floor), 2.0e6);
    TestEqual(TEXT("2 km/s at 5 km"), ShipFlight::SkimCap(5.0e5, Skim, Floor), 2.0e5);
    TestEqual(TEXT("200 m/s at 500 m"), ShipFlight::SkimCap(5.0e4, Skim, Floor), 2.0e4);
    TestEqual(TEXT("20 m/s at 50 m"), ShipFlight::SkimCap(5.0e3, Skim, Floor), 2.0e3);
    TestEqual(TEXT("and 20 m/s below, however low"), ShipFlight::SkimCap(100.0, Skim, Floor), Floor);
    TestEqual(TEXT("under the ground, still the floor"), ShipFlight::SkimCap(-50.0, Skim, Floor), Floor);
    return true;
}

bool FGroundSecondsTest::RunTest(const FString& Parameters)
{
    using namespace GroundLawTestLocal;
    const ShipFlight::FGroundLaw Law = FullLaw();
    // (H - 800 m) / 200 m/s + N ln(800 m / (N x 0.5 m/s)) + N: 74 s from
    // 10 km, 124 s from 20 km (decision 10's descent time).
    const double Tail = N * FMath::Loge(8.0e4 / (N * Touch)) + N;
    const double From10 = ShipFlight::SecondsToGround(1.0e6, 2.0e4, 1.0, Law);
    const double From20 = ShipFlight::SecondsToGround(2.0e6, 2.0e4, 1.0, Law);
    TestTrue(FString::Printf(TEXT("straight down from 10 km at 200 m/s: %.2f s"), From10),
             FMath::IsNearlyEqual(From10, (1.0e6 - 8.0e4) / 2.0e4 + Tail, 0.05));
    TestTrue(FString::Printf(TEXT("and from 20 km: %.2f s"), From20),
             FMath::IsNearlyEqual(From20, (2.0e6 - 8.0e4) / 2.0e4 + Tail, 0.05));
    TestEqual(TEXT("at the ground, now"), ShipFlight::SecondsToGround(0.0, 2.0e4, 1.0, Law), 0.0);
    TestTrue(TEXT("at rest, never"), !FMath::IsFinite(ShipFlight::SecondsToGround(1.0e5, 0.0, 1.0, Law)));

    // It counts down a second a second: flying dt at the speed the laws
    // allow takes dt off the ETA, at any distance and any path angle.
    FRandomStream Random(7);
    double Worst = 0.0;
    for (int32 Trial = 0; Trial < 60; ++Trial)
    {
        const double L = FMath::Pow(10.0, Random.FRandRange(2.0, 7.0));
        const double Sine = Random.FRandRange(0.05, 1.0);
        const double Speed = FMath::Pow(10.0, Random.FRandRange(2.0, 6.0));
        const double Cosine = FMath::Sqrt(1.0 - Sine * Sine);
        const double Allowed = FMath::Min(Speed, FMath::Min(
            ShipFlight::GroundApproachSpeed(L * Sine, Law.BrakingAccel, Law.ApproachSeconds, Law.TouchdownSpeed, 0.0) / Sine,
            ShipFlight::SkimCap(L * Sine, Law.SkimSeconds, Law.SkimFloor) / Cosine));
        const double Dt = FMath::Min(0.01, 0.001 * L / Allowed);
        const double Rate = (ShipFlight::SecondsToGround(L, Speed, Sine, Law)
                           - ShipFlight::SecondsToGround(L - Allowed * Dt, Speed, Sine, Law)) / Dt;
        Worst = FMath::Max(Worst, FMath::Abs(Rate - 1.0));
    }
    TestTrue(FString::Printf(TEXT("the ETA falls a second a second (worst %.4f off)"), Worst), Worst <= 0.01);
    return true;
}

#endif
```

- [ ] **Step 3: Run them and see them fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh
```

Expected: FAILS to compile: `Surface/GroundField.h: No such file or directory`.

- [ ] **Step 4: Write `IGroundField`**

Create `Source/DeepSpace/Surface/GroundField.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Surface/WorldRelief.h"
#include "Templates/SharedPointer.h"

/**
 * A world's solid ground as the flight and the terrain read it (landing
 * decisions 5 and 10): heights over the datum sphere by unit direction in
 * universe axes, and the analytic bounds the ray march and the LOD boxes rest
 * on. FReliefGround is the real one -- WorldRelief, the one height function --
 * and tests put cheap analytic fields behind the same interface.
 *
 * Const and thread-safe: terrain tiles build on worker threads from the same
 * object the flight reads on the game thread.
 */
class DEEPSPACE_API IGroundField
{
public:
    virtual ~IGroundField() = default;

    /** The datum's radius, cm: heights are measured from it. */
    virtual double RadiusCm() const = 0;

    /** cm above the datum at D (unit), with bands finer than the footprint
     *  faded as the material fades them. 0 footprint is every band. */
    virtual double Height(const FVector3d& D, double FootprintCm) const = 0;

    /** The same, and its gradient in cm per unit of D. */
    virtual double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const = 0;

    /** An analytic bound on what a footprint's fade removed, cm. */
    virtual double OmittedBoundCm(double FootprintCm) const = 0;

    /** Analytic bounds, never sampled. */
    virtual double MaxHeightCm() const = 0;
    virtual double MinHeightCm() const = 0;

    /** Rise over run, the Lipschitz bound the ray march steps by. */
    virtual double MaxSlope() const = 0;
};

using FGroundFieldRef = TSharedPtr<const IGroundField, ESPMode::ThreadSafe>;

/** WorldRelief behind the interface: the ground procgen made. */
class DEEPSPACE_API FReliefGround final : public IGroundField
{
public:
    explicit FReliefGround(const FWorldReliefParams& Params) : Relief(Params) {}

    const FWorldRelief& GetRelief() const { return Relief; }

    virtual double RadiusCm() const override { return Relief.GetParams().RadiusCm; }
    virtual double Height(const FVector3d& D, double FootprintCm) const override { return Relief.Height(D, FootprintCm); }
    virtual double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const override
    {
        return Relief.HeightAndGradient(D, Grad, FootprintCm);
    }
    virtual double OmittedBoundCm(double FootprintCm) const override { return Relief.OmittedBoundCm(FootprintCm); }
    virtual double MaxHeightCm() const override { return Relief.MaxHeightCm(); }
    virtual double MinHeightCm() const override { return Relief.MinHeightCm(); }
    virtual double MaxSlope() const override { return Relief.MaxSlope(); }

private:
    FWorldRelief Relief;
};

namespace ShipGround
{
    /** A shared ground for Params: what the subsystem hands the flight and
     *  the terrain. */
    DEEPSPACE_API FGroundFieldRef FromRelief(const FWorldReliefParams& Params);

    /**
     * The ground's unit normal at D: the sphere's own normal tilted by the
     * gradient's part along the surface over the datum radius,
     * normalize(D - Grad_t / R) -- the composition M_SkyBody's relief_normal
     * uses, so the tile's vertex normal and the orbit's pixel normal agree.
     */
    DEEPSPACE_API FVector3d NormalAt(const IGroundField& Ground, const FVector3d& D, double FootprintCm = 0.0);
}
```

Create `Source/DeepSpace/Surface/GroundField.cpp`:

```cpp
#include "Surface/GroundField.h"

FGroundFieldRef ShipGround::FromRelief(const FWorldReliefParams& Params)
{
    return MakeShared<FReliefGround, ESPMode::ThreadSafe>(Params);
}

FVector3d ShipGround::NormalAt(const IGroundField& Ground, const FVector3d& D, double FootprintCm)
{
    FVector3d Grad = FVector3d::ZeroVector;
    Ground.HeightAndGradient(D, Grad, FootprintCm);
    const FVector3d Along = Grad - D * FVector3d::DotProduct(Grad, D);
    return (D - Along / FMath::Max(Ground.RadiusCm(), 1.0)).GetSafeNormal();
}
```

(`FWorldRelief::GetParams()` is Task R3's.)

- [ ] **Step 5: Give surfaces a ground, and write the laws**

In `Source/DeepSpace/Ship/ShipFlightSurface.h`, add `#include "Surface/GroundField.h"` and, inside
`FFlightSurface` after `bool bInsideOut = false;`:

```cpp
    /** A planet or moon -- never a star, never the edge: what the near regime
     *  and "up" are measured from (landing decision 8). */
    bool bWorld = false;

    /**
     * Solid ground (landing decision 10), set only for EGround::Solid worlds.
     * Two floors, and each substep reads exactly one: Floor is the drive's,
     * the sphere; with a Ground, cruise, the vertical lever, the room and the
     * HUD read the ground, at every altitude, and never the sphere. Fixtures
     * without one behave exactly as before.
     */
    FGroundFieldRef Ground;

    bool HasGround() const { return Ground.IsValid(); }
```

In the `ShipFlight` namespace, after `SecondsToFloor`, add:

```cpp
    /** ds.Land.TouchdownSpeed's default, cm/s: contact at half a metre a second. */
    inline constexpr double DefaultTouchdownSpeed = 50.0;

    /** ds.Land.ApproachSeconds' default: the ground approach's exponential,
     *  its own CVar and never ds.Drive.HoldSeconds (whose "0 or less" means
     *  the braking curve alone). Clamped to MinApproachSeconds where read. */
    inline constexpr double DefaultApproachSeconds = 4.0;
    inline constexpr double MinApproachSeconds = 0.5;

    /** ds.Land.SkimSeconds and ds.Land.SkimFloor's defaults: in the near
     *  regime horizontal speed is at most max(SkimFloor, AGL / SkimSeconds),
     *  so the ground flows past at one apparent rate at every height. */
    inline constexpr double DefaultSkimSeconds = 2.5;
    inline constexpr double DefaultSkimFloor = 2000.0;

    /** The ground march's step budget: an exhausted march is a hit. */
    inline constexpr int32 GroundMarchSteps = 64;

    /** ds.Land.Regime's default, cm: the vertical lever is live within 50 km
     *  of the nearest world's cruise floor, leaves over RegimeExitFactor of
     *  it, and both levers blend across its top RegimeBlendFraction. */
    inline constexpr double DefaultRegimeCm = 5.0e6;
    inline constexpr double RegimeExitFactor = 1.1;
    inline constexpr double RegimeBlendFraction = 0.2;

    /** ds.Land.DriveHandback's default, cm: the drive takes the ship back
     *  only this far above a solid world's drive floor. */
    inline constexpr double DefaultDriveHandbackCm = 5.0e4;

    /** How far From is above Surface's ground, cm, radially: the origin
     *  above the rock, clearance included. Unset without a ground. */
    DEEPSPACE_API TOptional<double> GroundAt(const FFlightSurface& Surface, const FUniversePosition& From);

    /**
     * The distance along a ray from From to Height + ClearanceCm, cm, by
     * sphere tracing: each step is the clearance the slope bound proves free,
     * Above / sqrt(1 + MaxSlope^2). Far steps may be band-limited -- a step
     * that follows one of length s evaluates Height(D, s / 2) and widens the
     * clearance by OmittedBoundCm(s / 2), cheaper and still conservative.
     * Skipped entirely unless the ray enters the sphere of R + MaxHeight +
     * Clearance.
     *
     * - Unset: proven clear to MaxDistanceCm, or out of the shell, or, from
     *   under the ground, heading up (a ship under the ground may always climb).
     * - 0: under the ground heading down.
     * - An exhausted march (GroundMarchSteps) is a hit at its last proven-clear
     *   distance, never "no hit": the cap always brakes for what it could not
     *   see past. OutSteps, if given, gets the steps used.
     *
     * Pure and deterministic, independent of what the mesh has streamed.
     */
    DEEPSPACE_API TOptional<double> RayToGround(const FFlightSurface& Surface, const FUniversePosition& From,
                                                const FVector& Direction, double ClearanceCm, double MaxDistanceCm,
                                                int32* OutSteps = nullptr);

    /**
     * The approach law (decision 10), cm/s, D cm from the ground:
     * D1 = 0.8 A N^2; D / N under D1, sqrt((D1 / N)^2 + 1.6 A (D - D1)) above
     * it; never under TouchdownSpeed; never more than D / Step. Its demanded
     * deceleration never exceeds 0.8 A, so an inertial ship follows it all the
     * way down, and the last metres are an exponential ease to contact at
     * TouchdownSpeed. ApproachSeconds is clamped to MinApproachSeconds. 0 at
     * D of zero or less.
     */
    DEEPSPACE_API double GroundApproachSpeed(double D, double BrakingAccel, double ApproachSeconds,
                                             double TouchdownSpeed, double Step);

    /** The skim cap, cm/s: max(SkimFloor, AGL / SkimSeconds). */
    DEEPSPACE_API double SkimCap(double AglCm, double SkimSeconds, double SkimFloor);

    /** The laws the ETA to the ground integrates. */
    struct FGroundLaw
    {
        double BrakingAccel = 0.0;
        double ApproachSeconds = DefaultApproachSeconds;
        double TouchdownSpeed = DefaultTouchdownSpeed;
        double SkimSeconds = DefaultSkimSeconds;
        double SkimFloor = DefaultSkimFloor;
    };

    /**
     * Seconds to the ground PathCm ahead along a straight path PathSine below
     * the horizon, for a ship at Speed under the laws it is really flying
     * (decision 12): along the path the ship may have min(Speed, the approach
     * law of the height left over PathSine, the skim cap of the height left
     * over the path's cosine). Integrated on a geometric grid, where the
     * exponential parts carry equal time per step, so it is exact to well
     * under a tenth of a percent and counts down a second a second. 0 at
     * PathCm of zero or less; infinite at rest.
     */
    DEEPSPACE_API double SecondsToGround(double PathCm, double Speed, double PathSine, const FGroundLaw& Law);
```

Also change the declaration of `Room` to take the ground's clearance:

```cpp
    /** ... (keep the comment) Over a surface with a ground, the room is the
     *  height above it less GroundClearanceCm: the cruise floor. */
    DEEPSPACE_API double Room(TConstArrayView<FFlightSurface> Surfaces, const FUniversePosition& From,
                              double GroundClearanceCm = 0.0);
```

In `Source/DeepSpace/Ship/ShipFlightSurface.cpp`, change `Room`'s loop body to:

```cpp
    for (const FFlightSurface& Surface : Surfaces)
    {
        const TOptional<double> Ground = GroundAt(Surface, From);
        Least = FMath::Min(Least, Ground ? *Ground - GroundClearanceCm : FloorClearance(Surface, From));
    }
```

(and its signature to match), then append:

```cpp
TOptional<double> ShipFlight::GroundAt(const FFlightSurface& Surface, const FUniversePosition& From)
{
    if (!Surface.HasGround())
    {
        return {};
    }
    const FVector Out = From - Surface.Centre;
    const double R = Out.Size();
    if (!(R > 0.0))
    {
        return -Surface.Radius;
    }
    return R - Surface.Radius - Surface.Ground->Height(FVector3d(Out / R), 0.0);
}

TOptional<double> ShipFlight::RayToGround(const FFlightSurface& Surface, const FUniversePosition& From,
                                          const FVector& Direction, double ClearanceCm, double MaxDistanceCm,
                                          int32* OutSteps)
{
    if (OutSteps)
    {
        *OutSteps = 0;
    }
    const FVector U = Direction.GetSafeNormal();
    if (!Surface.HasGround() || U.IsZero() || !(MaxDistanceCm > 0.0))
    {
        return {};
    }
    const IGroundField& Ground = *Surface.Ground;
    const double Clear = FMath::Max(ClearanceCm, 0.0);

    // Centre-relative, in doubles: at planetary scale this keeps well under
    // a millimetre, and nothing here is astronomical.
    const FVector Start = From - Surface.Centre;
    const double StartR = Start.Size();
    if (StartR > 0.0)
    {
        const FVector3d D(Start / StartR);
        if (StartR - Surface.Radius - Ground.Height(D, 0.0) - Clear <= 0.0)
        {
            // Under already: it may always climb, and may not descend.
            return FVector::DotProduct(U, FVector(D)) > 0.0 ? TOptional<double>() : TOptional<double>(0.0);
        }
    }

    const double Shell = Surface.Radius + Ground.MaxHeightCm() + Clear;
    const double Along = FVector::DotProduct(Start, U);
    const double Miss2 = FMath::Max(Start.SizeSquared() - Along * Along, 0.0);
    const double Half2 = Shell * Shell - Miss2;
    if (Half2 <= 0.0)
    {
        return {};
    }
    const double Half = FMath::Sqrt(Half2);
    const double Exit = Along + Half;
    if (Exit <= 0.0)
    {
        return {};
    }
    const double End = FMath::Min(Exit, MaxDistanceCm);
    const double Lipschitz = FMath::Sqrt(1.0 + FMath::Square(Ground.MaxSlope()));

    double T = FMath::Max(0.0, Along - Half);
    double Step = 0.0;
    for (int32 I = 0; I < GroundMarchSteps; ++I)
    {
        if (OutSteps)
        {
            *OutSteps = I + 1;
        }
        if (T >= End)
        {
            return {};
        }
        const FVector P = Start + U * T;
        const double R = P.Size();
        const FVector3d D(P / R);
        const double Footprint = 0.5 * Step;
        const double Above = R - Surface.Radius - Ground.Height(D, Footprint) - Ground.OmittedBoundCm(Footprint) - Clear;
        if (Above < 1.0)
        {
            return T;
        }
        Step = Above / Lipschitz;
        T += Step;
    }
    return T; // exhausted: a hit at the last proven-clear distance, never "no hit"
}

double ShipFlight::GroundApproachSpeed(double D, double BrakingAccel, double ApproachSeconds,
                                       double TouchdownSpeed, double Step)
{
    if (!(D > 0.0))
    {
        return 0.0;
    }
    const double N = FMath::Max(ApproachSeconds, MinApproachSeconds);
    const double B = BrakingMargin * FMath::Max(BrakingAccel, 0.0);
    const double D1 = B * N * N;
    const double Law = D <= D1 ? D / N : FMath::Sqrt(FMath::Square(D1 / N) + 2.0 * B * (D - D1));
    const double May = FMath::Max(FMath::Max(TouchdownSpeed, 0.0), Law);
    return Step > 0.0 ? FMath::Min(May, D / Step) : May;
}

double ShipFlight::SkimCap(double AglCm, double SkimSeconds, double SkimFloor)
{
    return FMath::Max(SkimFloor, FMath::Max(AglCm, 0.0) / FMath::Max(SkimSeconds, 1.0e-3));
}

double ShipFlight::SecondsToGround(double PathCm, double Speed, double PathSine, const FGroundLaw& Law)
{
    if (!(PathCm > 0.0))
    {
        return 0.0;
    }
    if (!(Speed > 0.0))
    {
        return Never;
    }
    const double S = FMath::Clamp(PathSine, 1.0e-6, 1.0);
    const double C = FMath::Sqrt(FMath::Max(0.0, 1.0 - S * S));
    const auto MayAlong = [&](double L)
    {
        const double Down = GroundApproachSpeed(L * S, Law.BrakingAccel, Law.ApproachSeconds, Law.TouchdownSpeed, 0.0) / S;
        const double Skim = C > 1.0e-9 ? SkimCap(L * S, Law.SkimSeconds, Law.SkimFloor) / C : Never;
        return FMath::Min(Speed, FMath::Min(Down, Skim));
    };
    constexpr int32 Steps = 512;
    const double Last = FMath::Min(1.0, PathCm);
    const double Ratio = FMath::Pow(Last / PathCm, 1.0 / Steps);
    double Seconds = 0.0;
    double L = PathCm;
    for (int32 I = 0; I < Steps; ++I)
    {
        const double Next = L * Ratio;
        Seconds += (L - Next) / MayAlong(FMath::Sqrt(L * Next));
        L = Next;
    }
    return Seconds + Last / MayAlong(0.5 * Last);
}
```

(`Never` is the file's existing anonymous-namespace infinity.) If the last partial step is not
exactly 1 cm when `PathCm` is under a centimetre, `Last = PathCm` and `Ratio` is 1: every grid
step is empty and the whole path is the final term, which is right.

- [ ] **Step 6: Build and run: PASS**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh && ./test.sh DeepSpace.Ship.Landing && \
./test.sh DeepSpace.Ship.FlightSurface && ./test.sh DeepSpace.Ship.FlightCruiseFloor
```

Expected: `passed: 4` for the landing laws; the two existing sphere tests still green (no
fixture of theirs has a ground). If `.RayToGround` reports runs that exhaust their steps, raise
`GroundMarchSteps` before anything else (spec decision 10) and record the new value in its
comment.

- [ ] **Step 7: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
git add Source/DeepSpace/Surface/GroundField.h Source/DeepSpace/Surface/GroundField.cpp Source/DeepSpace/Ship/ShipFlightSurface.h \
        Source/DeepSpace/Ship/ShipFlightSurface.cpp Source/DeepSpace/Tests/GroundFixtures.h Source/DeepSpace/Tests/ShipGroundLawTest.cpp && \
git add -u Source/DeepSpace/Surface/WorldRelief.h && \
git commit -m "$(cat <<'EOF'
feat(flight): the ground as the flight reads it -- ray march, approach law, skim cap, ETA law

IGroundField puts WorldRelief (and cheap test fields) behind one interface;
FFlightSurface carries it as its second floor. RayToGround sphere-traces
by the slope bound and treats an exhausted march as a hit; the approach
law's knee never demands more than 0.8 A; SecondsToGround integrates the
laws the ship flies (landing decisions 10 and 12).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 8: Prove the key tests can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightSurface.cpp 'const double D1 = B * N * N;' 'const double D1 = 2.0 * B * N * N;' DeepSpace.Ship.Landing.ApproachLaw && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightSurface.cpp 'return T; // exhausted' 'return {}; // exhausted' DeepSpace.Ship.Landing.RayToGround && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightSurface.cpp 'Step = Above / Lipschitz;' 'Step = Above;' DeepSpace.Ship.Landing.RayToGround && \
./build.sh
```

Expected: `KILLED` three times (the doubled knee demands 1.6 A; the exhausted march reports "no
hit"; the unbounded step crosses ground, which the dense re-check finds).

---

## Task 17 (F3): the footprint -- eight points, and the least of them above the ground

**Owner:** F. **Depends on:** F2. Spec decision 11 (the footprint is slice (b)'s).

**Files:**
- Create: `Source/DeepSpace/Ship/ShipLanding.h`, `Source/DeepSpace/Ship/ShipLanding.cpp`
- Modify: `Tools/hauler_layout.py` (after the `PILOT_EYE` block, about line 320: `GEAR`, `BELLY`),
  `Tools/test_placement.py` (append four tests)
- Create: `Source/DeepSpace/Tests/ShipFootprintTest.cpp` (`DeepSpace.Ship.Landing.Footprint`)

**Interfaces:**
- Consumes: `FFlightSurface`, `IGroundField`, `ShipGround::NormalAt`.
- Produces: `ShipLanding::DefaultGearClearanceCm`, `BellyZCm`, `GearFeetXY[4][2]`,
  `BellyCorners[4][3]`, `FootprintPoints`, `ReachCm`, `FFootprintClearance`, `FootprintClearance`.

- [ ] **Step 1: Write the failing tests**

Append to `Tools/test_placement.py`:

```python
# -- the footprint (landing decision 11) --------------------------------------

def _landing_table(name, width):
    with open(os.path.join(ROOT, "Source/DeepSpace/Ship/ShipLanding.h")) as f:
        header = f.read()
    found = re.search(name + r"\[4\]\[%d\]\s*=\s*\{(.*?)\};" % width, header, re.S)
    assert found, "no %s in ShipLanding.h" % name
    numbers = [float(v) for v in re.findall(r"-?\d+(?:\.\d+)?", found.group(1))]
    assert len(numbers) == 4 * width, numbers
    return [tuple(numbers[i:i + width]) for i in range(0, len(numbers), width)]


def _hull_bounds():
    ship = L.generate()
    lo = [min(b.centre[i] - b.size[i] / 2.0 for b in ship.boxes) for i in range(3)]
    hi = [max(b.centre[i] + b.size[i] / 2.0 for b in ship.boxes) for i in range(3)]
    return lo, hi


def test_the_gear_feet_are_the_ones_the_cpp_lands_on():
    assert _landing_table("GearFeetXY", 2) == [tuple(float(v) for v in foot) for foot in L.GEAR]


def test_the_belly_corners_are_the_ones_the_cpp_lands_on():
    assert _landing_table("BellyCorners", 3) == [tuple(float(v) for v in corner) for corner in L.BELLY]


def test_the_belly_is_the_hull_s_plan_at_its_underside():
    lo, hi = _hull_bounds()
    xs = sorted({corner[0] for corner in L.BELLY})
    ys = sorted({corner[1] for corner in L.BELLY})
    assert xs == [lo[0], hi[0]] and ys == [lo[1], hi[1]], (xs, ys, lo, hi)
    assert all(corner[2] == lo[2] for corner in L.BELLY), (L.BELLY, lo[2])


def test_the_gear_stands_under_the_hull_wide_enough_to_rest_on():
    lo, hi = _hull_bounds()
    for x, y in L.GEAR:
        assert lo[0] < x < hi[0] and lo[1] < y < hi[1], (x, y, lo, hi)
    xs = [x for x, _ in L.GEAR]
    ys = [y for _, y in L.GEAR]
    assert max(xs) - min(xs) >= 2000 and max(ys) - min(ys) >= 300, L.GEAR
```

Create `Source/DeepSpace/Tests/ShipFootprintTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipLanding.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 11's footprint, built in slice (b) because the descent
 * cap and the ground's hard stop read it from the first hover: measured at
 * the origin alone, a 15-degree slope under a 26 m hull puts a corner 3 m
 * into the ground.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipFootprintTest, "DeepSpace.Ship.Landing.Footprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipFootprintTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    const double Gear = ShipLanding::DefaultGearClearanceCm;
    const FGroundFieldRef Flat = MakeShared<FCrossedSines, ESPMode::ThreadSafe>(UniverseUnits::CmPerEarthRadius, 0.0, 1.0e4);
    const FFlightSurface World = SurfaceOver(Flat, 1.0e6);
    const FVector3d Pole(0.0, 0.0, 1.0);

    TestEqual(TEXT("eight points: four feet and four belly corners"), ShipLanding::FootprintPoints(Gear).Num(), 8);
    TestTrue(FString::Printf(TEXT("the hull reaches 18.4 m from its origin (%.1f cm)"), ShipLanding::ReachCm(Gear)),
             FMath::IsNearlyEqual(ShipLanding::ReachCm(Gear), FMath::Sqrt(1770.0 * 1770.0 + 510.0 * 510.0 + 100.0), 0.01));

    const ShipLanding::FFootprintClearance Resting = ShipLanding::FootprintClearance(World, Above(World, Pole, Gear), Level(Pole), Gear);
    TestTrue(FString::Printf(TEXT("level at the gear's clearance, a foot touches (%.6f cm)"), Resting.Least),
             FMath::Abs(Resting.Least) < 1.0e-3 && Resting.Point >= 0 && Resting.Point < 4);
    TestTrue(TEXT("and the ground's normal there is up"), (FVector(Resting.GroundNormal) - FVector(Pole)).IsNearlyZero(1e-9));

    const ShipLanding::FFootprintClearance Higher = ShipLanding::FootprintClearance(World, Above(World, Pole, Gear + 50.0), Level(Pole), Gear);
    TestTrue(TEXT("half a metre higher, half a metre of clearance"), FMath::IsNearlyEqual(Higher.Least, 50.0, 1.0e-3));

    // Pitched 15 degrees nose up at 20 m: the aft corners are lowest, and
    // the least clearance is the lowest point's height, measured here
    // independently by projecting every point on the up axis.
    const FQuat Pitched = Level(Pole) * FQuat(FVector::RightVector, FMath::DegreesToRadians(-15.0));
    const ShipLanding::FFootprintClearance Tilted = ShipLanding::FootprintClearance(World, Above(World, Pole, 2000.0), Pitched, Gear);
    double Lowest = TNumericLimits<double>::Max();
    for (const FVector& Point : ShipLanding::FootprintPoints(Gear))
    {
        Lowest = FMath::Min(Lowest, 2000.0 + (Pitched.RotateVector(Point) | FVector(Pole)));
    }
    TestTrue(FString::Printf(TEXT("pitched, the lowest point sets it (%.2f vs %.2f cm)"), Tilted.Least, Lowest),
             FMath::IsNearlyEqual(Tilted.Least, Lowest, 0.05));
    TestTrue(TEXT("and it is an aft point"), ShipLanding::FootprintPoints(Gear)[Tilted.Point].X < 0.0);

    FFlightSurface Sphere = World;
    Sphere.Ground.Reset();
    TestFalse(TEXT("a surface with no ground has no footprint clearance"),
              ShipLanding::FootprintClearance(Sphere, Above(World, Pole, 100.0), Level(Pole), Gear).Point >= 0);
    return true;
}

#endif
```

- [ ] **Step 2: Run them and see them fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && python3 Tools/test_placement.py; ./build.sh
```

Expected: the Python run fails with `AttributeError: module 'hauler_layout' has no attribute
'GEAR'` (or `no GearFeetXY in ShipLanding.h`); the build fails on `Ship/ShipLanding.h: No such
file or directory`.

- [ ] **Step 3: Write the data and the query**

In `Tools/hauler_layout.py`, after the `PILOT_EYE` block, add:

```python
# -- Landing: the footprint (landing decision 11) --------------------------
# Ship space, cm: X fore, Y starboard, the deck at Z = 0. The gear's four
# feet, in plan -- their Z is -ds.Land.GearClearance, set in C++ -- and the
# belly's four plan corners at the slab's underside. The flight reads every
# one: the descent cap and the ground's hard stop take the least height of
# any of them above the ground. Mirrored as ShipLanding::GearFeetXY and
# ShipLanding::BellyCorners; test_placement.py holds them equal, and holds
# the belly to the hull generate() builds.
GEAR = [(-700, -300), (-700, 400), (1600, -100), (1600, 200)]
BELLY = [(-820, -410, -10), (-820, 510, -10), (1770, -410, -10), (1770, 510, -10)]
```

Create `Source/DeepSpace/Ship/ShipLanding.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipFlightSurface.h"

/**
 * The ship's footprint on the ground (landing decision 11): eight points in
 * ship space, as layout data -- four gear feet and the belly's four plan
 * corners -- and the least height of any of them above the ground, which
 * the descent cap, the ground's hard stop and (slice c) contact all read.
 *
 * Mirrored from GEAR and BELLY in Tools/hauler_layout.py; test_placement.py
 * reads the two tables below by name and holds them equal. Pure.
 */
namespace ShipLanding
{
    /** ds.Land.GearClearance's default, cm: the origin's height over flat
     *  ground at rest -- 10 cm of slab and 140 cm of notional gear. More than
     *  the drawn ground's error under the ship (GearClearance / 10), so the
     *  drawn and the flown ground never visibly disagree. */
    inline constexpr double DefaultGearClearanceCm = 150.0;

    /** The belly, the slab's underside, cm. */
    inline constexpr double BellyZCm = -10.0;

    /** The gear's feet in plan, cm; their Z is -GearClearance. */
    inline constexpr double GearFeetXY[4][2] = { { -700.0, -300.0 }, { -700.0, 400.0 }, { 1600.0, -100.0 }, { 1600.0, 200.0 } };

    /** The belly's plan corners at its underside, cm. */
    inline constexpr double BellyCorners[4][3] = { { -820.0, -410.0, -10.0 }, { -820.0, 510.0, -10.0 }, { 1770.0, -410.0, -10.0 }, { 1770.0, 510.0, -10.0 } };

    /** The eight points, ship space, cm: the feet first. */
    DEEPSPACE_API TArray<FVector, TFixedAllocator<8>> FootprintPoints(double GearClearanceCm);

    /** The farthest any footprint point is from the origin, cm. */
    DEEPSPACE_API double ReachCm(double GearClearanceCm);

    struct FFootprintClearance
    {
        /** The least height of any point above the ground, cm, radially:
         *  0 when a foot touches, negative when a point is under. */
        double Least = TNumericLimits<double>::Max();

        /** Which point, into FootprintPoints; INDEX_NONE with no ground. */
        int32 Point = INDEX_NONE;

        /** The ground's unit normal under that point, universe axes. */
        FVector GroundNormal = FVector::UpVector;
    };

    /** The footprint's clearance over Surface's ground, the ship at Origin
     *  turned by Orientation. Nothing (Point INDEX_NONE) without a ground. */
    DEEPSPACE_API FFootprintClearance FootprintClearance(const FFlightSurface& Surface, const FUniversePosition& Origin,
                                                         const FQuat& Orientation, double GearClearanceCm);
}
```

Create `Source/DeepSpace/Ship/ShipLanding.cpp`:

```cpp
#include "Ship/ShipLanding.h"

TArray<FVector, TFixedAllocator<8>> ShipLanding::FootprintPoints(double GearClearanceCm)
{
    TArray<FVector, TFixedAllocator<8>> Points;
    for (const auto& Foot : GearFeetXY)
    {
        Points.Add(FVector(Foot[0], Foot[1], -GearClearanceCm));
    }
    for (const auto& Corner : BellyCorners)
    {
        Points.Add(FVector(Corner[0], Corner[1], Corner[2]));
    }
    return Points;
}

double ShipLanding::ReachCm(double GearClearanceCm)
{
    double Reach = 0.0;
    for (const FVector& Point : FootprintPoints(GearClearanceCm))
    {
        Reach = FMath::Max(Reach, Point.Size());
    }
    return Reach;
}

ShipLanding::FFootprintClearance ShipLanding::FootprintClearance(const FFlightSurface& Surface, const FUniversePosition& Origin,
                                                                 const FQuat& Orientation, double GearClearanceCm)
{
    FFootprintClearance Out;
    if (!Surface.HasGround())
    {
        return Out;
    }
    const FVector FromCentre = Origin - Surface.Centre;
    const TArray<FVector, TFixedAllocator<8>> Points = FootprintPoints(GearClearanceCm);
    FVector3d LeastD = FVector3d::UnitZ();
    for (int32 Index = 0; Index < Points.Num(); ++Index)
    {
        const FVector At = FromCentre + Orientation.RotateVector(Points[Index]);
        const double R = At.Size();
        const FVector3d D(At / R);
        const double Above = R - Surface.Radius - Surface.Ground->Height(D, 0.0);
        if (Above < Out.Least)
        {
            Out.Least = Above;
            Out.Point = Index;
            LeastD = D;
        }
    }
    Out.GroundNormal = FVector(ShipGround::NormalAt(*Surface.Ground, LeastD));
    return Out;
}
```

- [ ] **Step 4: Run: PASS**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && python3 Tools/test_placement.py && \
./build.sh && ./test.sh DeepSpace.Ship.Landing.Footprint
```

Expected: the Python file prints no failure (it runs its `test_` functions); `passed: 1`.

- [ ] **Step 5: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
git add Source/DeepSpace/Ship/ShipLanding.h Source/DeepSpace/Ship/ShipLanding.cpp Source/DeepSpace/Tests/ShipFootprintTest.cpp \
        Tools/hauler_layout.py Tools/test_placement.py && \
git commit -m "$(cat <<'EOF'
feat(flight): the footprint -- gear feet and belly corners, and their least height

GEAR and BELLY are layout data in hauler_layout.py, mirrored as
ShipLanding::GearFeetXY and BellyCorners and held equal by
test_placement.py, which also holds the belly to the hull's plan and
underside. FootprintClearance is what the descent cap and the ground's
hard stop read (landing decision 11).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Prove it can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipLanding.cpp 'if (Above < Out.Least)' 'if (Above > Out.Least)' DeepSpace.Ship.Landing.Footprint && \
./build.sh
```

Expected: `KILLED`. And by hand: change `1770.0, 510.0, -10.0` to `1770.0, 520.0, -10.0` in
`ShipLanding.h`, run `python3 Tools/test_placement.py`, see
`test_the_belly_corners_are_the_ones_the_cpp_lands_on` fail, `git checkout -- Source/DeepSpace/Ship/ShipLanding.h`.

---

## Task 18 (F4): the vertical lever as arithmetic

**Owner:** F. **Depends on:** B0. Spec decision 8 (its law), decision 5 (the climb top), the
2026-09-26 ruling (after X a press catches the ship where it is).

**Files:**
- Create: `Source/DeepSpace/Ship/ShipVerticalLever.h`, `Source/DeepSpace/Ship/ShipVerticalLever.cpp`
- Create: `Source/DeepSpace/Tests/ShipVerticalLeverTest.cpp` (`DeepSpace.Ship.VerticalLever`)

**Interfaces:**
- Consumes: `ShipDriveLever::SweepCruise`, `ShipFlight::StandardGravityCmS2`.
- Produces: `ShipVerticalLever::{FloorCmPerSecond, DefaultTopCmPerSecond, DefaultSweep,
  DefaultHeavyFloor, Rate, LeverOf, Sweep, Catch, ClimbTop}`.

- [ ] **Step 1: Write the failing test**

Create `Source/DeepSpace/Tests/ShipVerticalLeverTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipGravity.h"
#include "Ship/ShipVerticalLever.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The third lever (landing decision 8): a climb or sink rate on a log scale,
 * 0.1 m/s just off zero to 200 m/s, the same both ways; zero is a place, the
 * detent; a sweep stops at HOVER; after X a press catches the ship at the
 * rate it is already moving; and the climb top falls on heavy worlds by
 * gravity alone (decision 5).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipVerticalLeverTest, "DeepSpace.Ship.VerticalLever",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipVerticalLeverTest::RunTest(const FString& Parameters)
{
    namespace V = ShipVerticalLever;
    const double Top = V::DefaultTopCmPerSecond;

    TestEqual(TEXT("zero is HOVER, exactly"), V::Rate(0.0, Top), 0.0);
    TestTrue(TEXT("a hair up is 0.1 m/s"), FMath::IsNearlyEqual(V::Rate(1.0e-9, Top), 10.0, 1e-3));
    TestEqual(TEXT("full up is 200 m/s"), V::Rate(1.0, Top), 2.0e4);
    TestEqual(TEXT("full down is 200 m/s sinking"), V::Rate(-1.0, Top), -2.0e4);
    TestTrue(TEXT("half is the geometric middle, 4.47 m/s"), FMath::IsNearlyEqual(V::Rate(0.5, Top), FMath::Sqrt(10.0 * 2.0e4), 1e-6));
    for (const double Rate : { 10.0, 50.0, 300.0, -300.0, 2.0e4, -1234.5 })
    {
        TestTrue(FString::Printf(TEXT("LeverOf inverts Rate at %.1f cm/s"), Rate),
                 FMath::IsNearlyEqual(V::Rate(V::LeverOf(Rate, Top), Top), Rate, 1e-9 * FMath::Abs(Rate)));
    }
    TestEqual(TEXT("a rate well under the floor is no lever at all"), V::LeverOf(2.0, Top), 0.0);

    // The sweep: 0.25 a second, stopping at HOVER from a climb; a sink is a
    // fresh press of C.
    double Lever = 0.3;
    for (int32 Frame = 0; Frame < 120; ++Frame)
    {
        Lever = V::Sweep(Lever, false, true, 0, 0, 1.0 / 60.0, V::DefaultSweep);
    }
    TestEqual(TEXT("C held from a climb stops at HOVER"), Lever, 0.0);
    TestEqual(TEXT("and holding it on moves nothing"), V::Sweep(Lever, false, true, 0, 0, 1.0, V::DefaultSweep), 0.0);
    TestTrue(TEXT("a fresh press of C leaves the detent to sink"), V::Sweep(Lever, false, true, 0, 1, 0.1, V::DefaultSweep) < 0.0);
    TestTrue(TEXT("rest to full in four seconds held"), FMath::IsNearlyEqual(V::Sweep(0.0, true, false, 1, 0, 4.0, V::DefaultSweep), 1.0, 1e-12));

    // After X: the lever at HOVER, the ship still sinking at 3 m/s; one C
    // catches it at the lever nearest 3 m/s.
    const double Caught = V::Catch(0.0, 0, 1, -300.0, Top);
    TestTrue(TEXT("one C catches a sinking ship at its own rate"), FMath::IsNearlyEqual(V::Rate(Caught, Top), -300.0, 1e-9));
    TestEqual(TEXT("a Space while sinking catches nothing; it sweeps from HOVER"), V::Catch(0.0, 1, 0, -300.0, Top), 0.0);
    TestEqual(TEXT("no press, no catch"), V::Catch(0.0, 0, 0, -300.0, Top), 0.0);
    TestEqual(TEXT("a lever already off HOVER is not caught"), V::Catch(0.2, 0, 1, -300.0, Top), 0.2);
    TestEqual(TEXT("a ship all but at rest is not caught"), V::Catch(0.0, 0, 1, -3.0, Top), 0.0);

    // The climb top, by gravity alone: 200 m/s x max(0.25, min(1, g_E / g)).
    const double G = ShipFlight::StandardGravityCmS2;
    TestEqual(TEXT("1 g: 200 m/s"), V::ClimbTop(Top, 1.0 * G, V::DefaultHeavyFloor), 2.0e4);
    TestEqual(TEXT("under 1 g: still 200 m/s"), V::ClimbTop(Top, 0.3 * G, V::DefaultHeavyFloor), 2.0e4);
    TestTrue(TEXT("2 g: 100 m/s"), FMath::IsNearlyEqual(V::ClimbTop(Top, 2.0 * G, V::DefaultHeavyFloor), 1.0e4, 1e-9));
    TestTrue(TEXT("3.3 g: 61 m/s"), FMath::IsNearlyEqual(V::ClimbTop(Top, 3.3 * G, V::DefaultHeavyFloor), 2.0e4 / 3.3, 1e-9));
    TestEqual(TEXT("4 g and more: the floor, 50 m/s"), V::ClimbTop(Top, 5.0 * G, V::DefaultHeavyFloor), 5.0e3);
    TestEqual(TEXT("no gravity: the top"), V::ClimbTop(Top, 0.0, V::DefaultHeavyFloor), 2.0e4);
    return true;
}

#endif
```

- [ ] **Step 2: Run and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh
```

Expected: fails to compile, `Ship/ShipVerticalLever.h: No such file or directory`.

- [ ] **Step 3: Write the lever**

Create `Source/DeepSpace/Ship/ShipVerticalLever.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * The vertical lever as arithmetic (landing decision 8): a climb or sink
 * rate the boosters hold against gravity, ship state like the other two
 * levers. At zero the ship hovers. Pure: the subsystem reads the CVars and
 * passes them in, and registers them with the defaults below.
 */
namespace ShipVerticalLever
{
    /** The slowest rate off zero, cm/s: 0.1 m/s. */
    inline constexpr double FloorCmPerSecond = 10.0;

    /** ds.Vertical.Top's default, cm/s: 200 m/s either way. */
    inline constexpr double DefaultTopCmPerSecond = 2.0e4;

    /** ds.Vertical.Sweep's default, lever a second: rest to full in 4 s, a
     *  decade of rate every 1.3 s. */
    inline constexpr double DefaultSweep = 0.25;

    /** ds.Vertical.HeavyFloor's default: the climb top never falls below a
     *  quarter of Top, however heavy the world. */
    inline constexpr double DefaultHeavyFloor = 0.25;

    /** The lever's rate, cm/s, signed, + climbing: sign(p) x Floor x
     *  (Top / Floor)^|p|, the cruise lever's law mirrored; 0 exactly at 0.
     *  A Top at or under the floor reads linearly. */
    DEEPSPACE_API double Rate(double Lever, double TopCmPerSecond);

    /** The inverse: the lever that reads RateCmPerSecond; 0 for a rate under
     *  half the floor, full for one past the top. */
    DEEPSPACE_API double LeverOf(double RateCmPerSecond, double TopCmPerSecond);

    /** Space and C held: the sweep, -1..1, stopping at HOVER from either
     *  side, and leaving it only in a frame with a fresh press of that key
     *  (ShipDriveLever::SweepCruise with the full travel both ways). */
    DEEPSPACE_API double Sweep(double Lever, bool bUpHeld, bool bDownHeld, int32 UpPresses, int32 DownPresses,
                               double Dt, double SweepRate);

    /** After X (the 2026-09-26 ruling): the lever at HOVER and a fresh press
     *  the way the ship is already moving catches it at the lever nearest
     *  its rate; otherwise the lever as it was. */
    DEEPSPACE_API double Catch(double Lever, int32 UpPresses, int32 DownPresses, double VerticalSpeedCmPerSecond,
                               double TopCmPerSecond);

    /** The climb top on a world pulling GravityCmS2 (decision 5): Top x
     *  max(HeavyFloor, min(1, g_E / g)). By gravity alone, never the booster
     *  share: thrust changes how fast the lever is reached, never its top. */
    DEEPSPACE_API double ClimbTop(double TopCmPerSecond, double GravityCmS2, double HeavyFloor);
}
```

Create `Source/DeepSpace/Ship/ShipVerticalLever.cpp`:

```cpp
#include "Ship/ShipVerticalLever.h"

#include "Ship/ShipDriveLever.h"
#include "Ship/ShipGravity.h"

double ShipVerticalLever::Rate(double Lever, double TopCmPerSecond)
{
    const double P = FMath::Clamp(Lever, -1.0, 1.0);
    if (P == 0.0 || !(TopCmPerSecond > 0.0))
    {
        return 0.0;
    }
    const double Magnitude = TopCmPerSecond <= FloorCmPerSecond
        ? FMath::Abs(P) * TopCmPerSecond
        : FloorCmPerSecond * FMath::Pow(TopCmPerSecond / FloorCmPerSecond, FMath::Abs(P));
    return P > 0.0 ? Magnitude : -Magnitude;
}

double ShipVerticalLever::LeverOf(double RateCmPerSecond, double TopCmPerSecond)
{
    const double Magnitude = FMath::Abs(RateCmPerSecond);
    if (Magnitude < 0.5 * FloorCmPerSecond || !(TopCmPerSecond > FloorCmPerSecond))
    {
        return 0.0;
    }
    const double P = FMath::Clamp(FMath::Loge(FMath::Max(Magnitude, FloorCmPerSecond) / FloorCmPerSecond)
                                  / FMath::Loge(TopCmPerSecond / FloorCmPerSecond), 1.0e-9, 1.0);
    return RateCmPerSecond > 0.0 ? P : -P;
}

double ShipVerticalLever::Sweep(double Lever, bool bUpHeld, bool bDownHeld, int32 UpPresses, int32 DownPresses,
                                double Dt, double SweepRate)
{
    return ShipDriveLever::SweepCruise(Lever, bUpHeld, bDownHeld, UpPresses, DownPresses, Dt, SweepRate, 1.0);
}

double ShipVerticalLever::Catch(double Lever, int32 UpPresses, int32 DownPresses, double VerticalSpeedCmPerSecond,
                                double TopCmPerSecond)
{
    if (Lever != 0.0 || FMath::Abs(VerticalSpeedCmPerSecond) < FloorCmPerSecond)
    {
        return Lever;
    }
    const bool bWithTheShip = VerticalSpeedCmPerSecond < 0.0 ? DownPresses > 0 : UpPresses > 0;
    return bWithTheShip ? LeverOf(VerticalSpeedCmPerSecond, TopCmPerSecond) : Lever;
}

double ShipVerticalLever::ClimbTop(double TopCmPerSecond, double GravityCmS2, double HeavyFloor)
{
    if (!(GravityCmS2 > 0.0))
    {
        return TopCmPerSecond;
    }
    const double Light = FMath::Min(1.0, ShipFlight::StandardGravityCmS2 / GravityCmS2);
    return TopCmPerSecond * FMath::Max(FMath::Clamp(HeavyFloor, 0.0, 1.0), Light);
}
```

- [ ] **Step 4: Run: PASS**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh && ./test.sh DeepSpace.Ship.VerticalLever
```

Expected: `passed: 1`.

- [ ] **Step 5: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
git add Source/DeepSpace/Ship/ShipVerticalLever.h Source/DeepSpace/Ship/ShipVerticalLever.cpp Source/DeepSpace/Tests/ShipVerticalLeverTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(flight): the vertical lever's law -- log rate, detent at HOVER, catch after X, climb top by gravity

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Prove it can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipVerticalLever.cpp 'return TopCmPerSecond * FMath::Max(FMath::Clamp(HeavyFloor, 0.0, 1.0), Light);' 'return TopCmPerSecond * Light;' DeepSpace.Ship.VerticalLever && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipVerticalLever.cpp 'VerticalSpeedCmPerSecond < 0.0 ? DownPresses > 0 : UpPresses > 0' 'true' DeepSpace.Ship.VerticalLever && \
./build.sh
```

Expected: `KILLED` twice.

---

## Task 19 (F5): two floors -- cruise reads the ground at every altitude, and the ground's hard stop lifts

**Owner:** F. **Depends on:** F1, F2, F3. Spec decision 10 (*One surface, two floors*, *The hard
stop, extended -- for the ground only*).

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipFlightState.h` -- `FShipFlightLimits` (after `DriveThrust`,
  about line 69), the public getters (after `GetThrustAcceleration`), the private block (after
  `RecordHold`, about line 353; members after `Wells`)
- Modify: `Source/DeepSpace/Ship/ShipFlightState.cpp` -- `GetRoom` (about line 127),
  `CruiseSubStep` (lines 276-353), new functions after it
- Create: `Source/DeepSpace/Tests/ShipLandingFloorsTest.cpp`
  (`DeepSpace.Ship.Landing.CruiseUnderDriveFloor`, `.HardStopLiftsGroundOnly`)

**Interfaces:**
- Consumes: `FFlightSurface::HasGround`, `ShipFlight::RayToGround`, `GroundAt`, `Room(..., GroundClearanceCm)`,
  `ShipLanding::FootprintClearance`, `ReachCm`, `DefaultGearClearanceCm`.
- Produces: `FShipFlightLimits::GearClearanceCm`;
  `struct FGroundLog { double LeastClearance; double WorstContactSpeed; int32 Contacts; int32 HardStops; }`;
  `TOptional<double> FShipFlightState::GetGroundAltitude() const`,
  `TOptional<double> GetFootprintClearance() const`, `double GetDepthUnderDriveFloor() const`,
  `const FGroundLog& GetGroundLog() const`, `void ResetGroundLog()`.

- [ ] **Step 1: Write the failing tests**

Create `Source/DeepSpace/Tests/ShipLandingFloorsTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipLanding.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 10's two floors: over a solid world the drive's floor is
 * the sphere, and cruise reads the ground and never the sphere, at every
 * altitude -- so a cruising ship descends through the drive floor to the
 * ground. And the hard stop, for the ground alone, lifts: a ship is never
 * left inside drawn rock. Sphere floors keep "never lifted".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingCruiseUnderDriveFloorTest, "DeepSpace.Ship.Landing.CruiseUnderDriveFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingHardStopTest, "DeepSpace.Ship.Landing.HardStopLiftsGroundOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LandingFloorsLocal
{
    using namespace GroundFixtures;

    /** The cruise lever that asks for Speed cm/s ahead. */
    double ThrottleFor(double Speed)
    {
        return FMath::Loge(Speed / ShipDriveLever::CruiseFloorCmPerSecond)
             / FMath::Loge(FShipFlightLimits().MaxSpeed / ShipDriveLever::CruiseFloorCmPerSecond);
    }

    /** Pitched Degrees nose down from Base (+Y pitch puts the nose down). */
    FQuat NoseDown(const FQuat& Base, double Degrees)
    {
        return Base * FQuat(FVector::RightVector, FMath::DegreesToRadians(Degrees));
    }
}

bool FLandingCruiseUnderDriveFloorTest::RunTest(const FString& Parameters)
{
    using namespace LandingFloorsLocal;
    const FGroundFieldRef Relief = ShipGround::FromRelief(FixtureParams());
    const double DriveFloor = 1.02e6 + Relief->MaxHeightCm();
    const FFlightSurface World = SurfaceOver(Relief, DriveFloor);
    const FVector3d D = FVector3d(0.01, -0.02, 1.0).GetSafeNormal();

    FShipFlightState Flight;
    Flight.SetSurfaces({ World });
    Flight.SetUniverseTransform(Above(World, D, 5.0e5), NoseDown(Level(D), 30.0));
    FShipFlightCommand Command;
    Command.Throttle = ThrottleFor(2.0e5);
    Flight.SetCommand(Command);

    bool bWentUnder = false;
    double RestFor = 0.0;
    double Seconds = 0.0;
    while (Seconds < 400.0 && RestFor < 2.0)
    {
        Flight.Step(1.0 / 60.0);
        Seconds += 1.0 / 60.0;
        bWentUnder |= Flight.GetDepthUnderDriveFloor() > 0.0;
        RestFor = Flight.GetSpeed() < 1.0 ? RestFor + 1.0 / 60.0 : 0.0;
    }
    const TOptional<double> Clear = Flight.GetFootprintClearance();
    const TOptional<double> Agl = Flight.GetGroundAltitude();
    AddInfo(FString::Printf(TEXT("at rest %.1f s in: footprint %.2f m, origin %.2f m above the ground"),
                            Seconds, Clear.Get(-1.0) / 100.0, Agl.Get(-1.0) / 100.0));
    TestTrue(TEXT("5 km up is under a solid world's drive floor"), Flight.GetDepthUnderDriveFloor() > 0.0 && bWentUnder);
    TestTrue(TEXT("cruise flies on down through it -- the sphere is not cruise's floor"), Agl.IsSet() && *Agl < 3.0e3);
    TestTrue(TEXT("and comes to rest on the ground"), RestFor >= 2.0);
    TestTrue(TEXT("with no point of the hull more than a centimetre under it"), Flight.GetGroundLog().LeastClearance >= -1.0);
    TestTrue(TEXT("and a point of it within a few metres of it"), Clear.IsSet() && *Clear <= 5.0e2);
    TestTrue(TEXT("the room is the ground's, less the gear's clearance"),
             FMath::IsNearlyEqual(Flight.GetRoom(), FMath::Max(*Agl - ShipLanding::DefaultGearClearanceCm, 0.0), 1.0e-6));
    return true;
}

bool FLandingHardStopTest::RunTest(const FString& Parameters)
{
    using namespace LandingFloorsLocal;
    const double Gear = ShipLanding::DefaultGearClearanceCm;
    const FGroundFieldRef Relief = ShipGround::FromRelief(FixtureParams());
    const FFlightSurface World = SurfaceOver(Relief, 1.02e6 + Relief->MaxHeightCm());
    const FVector3d D = FVector3d(-0.03, 0.02, 1.0).GetSafeNormal();

    // Placed under the ground: one substep lifts every point out of it.
    {
        FShipFlightState Flight;
        Flight.SetSurfaces({ World });
        Flight.SetUniverseTransform(Above(World, D, -300.0), Level(D));
        Flight.Step(FShipFlightState::FixedStep);
        TestTrue(FString::Printf(TEXT("placed 3 m into the ground, lifted out in one substep (%.3f cm)"), Flight.GetFootprintClearance().Get(-1.0e9)),
                 Flight.GetFootprintClearance().Get(-1.0e9) >= -1.0);
        TestEqual(TEXT("which is the ground's hard stop firing"), Flight.GetGroundLog().HardStops, 1);
    }

    // A sphere floor, raised over a ship in play, still teleports nothing.
    {
        FFlightSurface Sphere = World;
        Sphere.Ground.Reset();
        FShipFlightState Flight;
        Flight.SetSurfaces({ Sphere });
        const FUniversePosition Under = Sphere.Centre + FVector(D) * (Sphere.Radius + Sphere.Floor - 1.0e5);
        Flight.SetUniverseTransform(Under, Level(D));
        Flight.Step(1.0);
        TestTrue(TEXT("under a sphere floor the ship is never lifted"),
                 FMath::Abs(Flight.GetUniversePosition().DistanceTo(Sphere.Centre) - Under.DistanceTo(Sphere.Centre)) < 1.0);
    }

    // A priors reload moves the ground under a hovering ship: the next
    // substep re-seats it.
    {
        FShipFlightState Flight;
        Flight.SetSurfaces({ World });
        Flight.SetUniverseTransform(Above(World, D, Gear + 20.0), Level(D));
        Flight.Step(FShipFlightState::FixedStep);
        FWorldReliefParams Taller = FixtureParams();
        Taller.PeakCm *= 2.0;
        const FGroundFieldRef Reloaded = ShipGround::FromRelief(Taller);
        Flight.SetSurfaces({ SurfaceOver(Reloaded, World.Floor + Relief->MaxHeightCm()) });
        Flight.Step(FShipFlightState::FixedStep);
        TestTrue(TEXT("a reload that raises the ground re-seats the ship on it"), Flight.GetFootprintClearance().Get(-1.0e9) >= -1.0);
    }

    // The pilot pitches a corner down at 1.5 m: the ship levers itself up
    // on its own gear, never through it, no faster than the turn lifts it.
    {
        FShipFlightState Flight;
        Flight.SetSurfaces({ World });
        Flight.SetUniverseTransform(Above(World, D, Gear + 1.0), Level(D));
        FShipFlightCommand Command;
        Command.AttitudeRate = FVector(0.0, 1.0, 0.0);
        Flight.SetCommand(Command);
        const double Reach = ShipLanding::ReachCm(Gear);
        double WorstRise = 0.0;
        double Least = TNumericLimits<double>::Max();
        double Previous = Flight.GetUniversePosition().DistanceTo(World.Centre);
        for (int32 Sub = 0; Sub < 3 * 120; ++Sub)
        {
            Flight.Step(FShipFlightState::FixedStep);
            const double Now = Flight.GetUniversePosition().DistanceTo(World.Centre);
            WorstRise = FMath::Max(WorstRise, (Now - Previous) / FShipFlightState::FixedStep);
            Previous = Now;
            Least = FMath::Min(Least, Flight.GetFootprintClearance().Get(-1.0e9));
        }
        const double TurnLift = Flight.GetLimits().MaxAngularRate.Y * Reach;
        TestTrue(FString::Printf(TEXT("pitching down at 1.5 m, no point goes under (least %.3f cm)"), Least), Least >= -1.0);
        TestTrue(FString::Printf(TEXT("and the ship rises no faster than its turn lifts the hull (%.1f of %.1f cm/s)"), WorstRise, TurnLift),
                 WorstRise <= 1.05 * TurnLift + 5.0);
    }
    return true;
}

#endif
```

- [ ] **Step 2: Run and see them fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh
```

Expected: fails to compile: `'GetFootprintClearance': is not a member of 'FShipFlightState'`.

- [ ] **Step 3: Write the two floors**

In `Source/DeepSpace/Ship/ShipFlightState.h`: add `#include "Ship/ShipLanding.h"`; in
`FShipFlightLimits` after `DriveThrust`:

```cpp
    /** ds.Land.GearClearance, cm: the origin's height over flat ground at
     *  rest, and how far below the origin the gear's feet are. */
    double GearClearanceCm = ShipLanding::DefaultGearClearanceCm;
```

Before `struct DEEPSPACE_API FShipFlightState`, add:

```cpp
/** What the ground's laws did, since the log was last reset: for the
 *  invariant's tests (DeepSpace.Ship.Landing.GroundAlwaysCatches*) and the
 *  playtests, never read by the flight itself. */
struct DEEPSPACE_API FGroundLog
{
    /** The least footprint clearance any substep ended with, cm. */
    double LeastClearance = TNumericLimits<double>::Max();

    /** The fastest a lowest point met the ground along its normal, cm/s, at
     *  the substep it first came within a centimetre (decision 10). */
    double WorstContactSpeed = 0.0;

    int32 Contacts = 0;

    /** How often the ground's hard stop fired: the caps missed a contact. */
    int32 HardStops = 0;
};
```

In `FShipFlightState`'s public block, after `GetThrustAcceleration`:

```cpp
    /** The ship's origin above the ground directly below it, cm, radially,
     *  clearance included -- the HUD's number, "1.5 M ABOVE GROUND" at rest
     *  on flat ground -- over the nearest solid world. Unset with none. */
    TOptional<double> GetGroundAltitude() const;

    /** The least height of any footprint point above that ground, cm: 0 when
     *  a foot touches. What the descent cap, the hard stop and contact read;
     *  the HUD never prints it. Unset with no solid world. */
    TOptional<double> GetFootprintClearance() const;

    /** How far under a solid world's drive floor the ship is, cm; 0 when it
     *  is not under one. The scope of effort (decision 5). */
    double GetDepthUnderDriveFloor() const;

    const FGroundLog& GetGroundLog() const;
    void ResetGroundLog();
```

In the private block, after `RecordHold`:

```cpp
    /** The nearest meeting of a ray along Direction with any surface as
     *  cruise sees it -- the ground (plus clearance, less the hull's reach)
     *  over a solid world, the sphere otherwise -- looking far enough to
     *  brake from Speed. */
    TOptional<double> NearestOnCruisePath(const FVector& Direction, double Speed);

    /** The solid world whose ground is nearest, into Surfaces; INDEX_NONE. */
    int32 NearestGround() const;

    /** After the translation: if any footprint point is under the ground,
     *  take the velocity into the ground's normal away and lift the origin
     *  along up by the deepest penetration. The ground only: sphere floors
     *  are never lifted. */
    void GroundHardStop();

    /** Log the footprint after the substep: least clearance, and a contact's
     *  speed the substep a lowest point first comes within a centimetre. */
    void LogGround(const ShipLanding::FFootprintClearance& Foot);
```

and members after `Wells`:

```cpp
    FGroundLog GroundLog;
    double LastFootprintLeast = TNumericLimits<double>::Max();
```

In `Source/DeepSpace/Ship/ShipFlightState.cpp`, change `GetRoom` to
`return ShipFlight::Room(Surfaces, Position, Limits.GearClearanceCm);`.

In `CruiseSubStep`, replace `NearestOnPath(Along)` with `NearestOnCruisePath(Along, TargetSpeed)`;
in the sphere hard-stop loop add as its first line

```cpp
        if (Surface.HasGround())
        {
            continue;   // cruise's floor over a solid world is the ground, below
        }
```

and after `Position = Next;` at the end add `GroundHardStop();`.

Append the new functions:

```cpp
TOptional<double> FShipFlightState::NearestOnCruisePath(const FVector& Direction, double Speed)
{
    const double Braking = 2.0 * ShipFlight::BrakingMargin * FMath::Max(Limits.LinearAcceleration, 1.0);
    const double Reach = ShipLanding::ReachCm(Limits.GearClearanceCm);
    const double Lookahead = 1.5 * (Speed * Speed / Braking + Speed * FixedStep) + Reach + 1.0e5;
    TOptional<double> Nearest;
    for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
    {
        const FFlightSurface& Surface = Surfaces[Index];
        TOptional<double> D;
        if (Surface.HasGround())
        {
            D = ShipFlight::RayToGround(Surface, Position, Direction, Limits.GearClearanceCm, Lookahead);
            if (D)
            {
                D = FMath::Max(0.0, *D - Reach);   // the hull reaches ahead of its origin
            }
        }
        else
        {
            D = ShipFlight::RayToFloor(Surface, Position, Direction);
        }
        if (D && (!Nearest || *D < *Nearest))
        {
            Nearest = D;
        }
    }
    return Nearest;
}

int32 FShipFlightState::NearestGround() const
{
    int32 Best = INDEX_NONE;
    double Least = TNumericLimits<double>::Max();
    for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
    {
        if (const TOptional<double> Agl = ShipFlight::GroundAt(Surfaces[Index], Position))
        {
            if (*Agl < Least)
            {
                Least = *Agl;
                Best = Index;
            }
        }
    }
    return Best;
}

TOptional<double> FShipFlightState::GetGroundAltitude() const
{
    const int32 Index = NearestGround();
    return Index == INDEX_NONE ? TOptional<double>() : ShipFlight::GroundAt(Surfaces[Index], Position);
}

TOptional<double> FShipFlightState::GetFootprintClearance() const
{
    const int32 Index = NearestGround();
    if (Index == INDEX_NONE)
    {
        return {};
    }
    return ShipLanding::FootprintClearance(Surfaces[Index], Position, Orientation, Limits.GearClearanceCm).Least;
}

double FShipFlightState::GetDepthUnderDriveFloor() const
{
    double Depth = 0.0;
    for (const FFlightSurface& Surface : Surfaces)
    {
        if (Surface.HasGround())
        {
            Depth = FMath::Max(Depth, -ShipFlight::FloorClearance(Surface, Position));
        }
    }
    return Depth;
}

const FGroundLog& FShipFlightState::GetGroundLog() const
{
    return GroundLog;
}

void FShipFlightState::ResetGroundLog()
{
    GroundLog = FGroundLog();
    LastFootprintLeast = TNumericLimits<double>::Max();
}

void FShipFlightState::GroundHardStop()
{
    const double Reach = ShipLanding::ReachCm(Limits.GearClearanceCm);
    for (const FFlightSurface& Surface : Surfaces)
    {
        if (!Surface.HasGround())
        {
            continue;
        }
        const FVector Out = Position - Surface.Centre;
        if (Out.Size() > Surface.Radius + Surface.Ground->MaxHeightCm() + Reach)
        {
            continue;   // above every peak by more than the hull reaches
        }
        ShipLanding::FFootprintClearance Foot = ShipLanding::FootprintClearance(Surface, Position, Orientation, Limits.GearClearanceCm);
        if (Foot.Least < -HardStopToleranceCm)
        {
            ++GroundLog.HardStops;
            const double Into = Velocity | Foot.GroundNormal;
            if (Into < 0.0)
            {
                Velocity -= Foot.GroundNormal * Into;
            }
            const FVector Up = Out.GetSafeNormal();
            for (int32 Pass = 0; Pass < 4 && Foot.Least < 0.0; ++Pass)
            {
                Position += Up * -Foot.Least;
                Foot = ShipLanding::FootprintClearance(Surface, Position, Orientation, Limits.GearClearanceCm);
            }
            Position = Position.Normalised();
        }
        LogGround(Foot);
    }
}

void FShipFlightState::LogGround(const ShipLanding::FFootprintClearance& Foot)
{
    GroundLog.LeastClearance = FMath::Min(GroundLog.LeastClearance, Foot.Least);
    if (Foot.Least < 1.0 && LastFootprintLeast >= 1.0 && Foot.Point != INDEX_NONE)
    {
        // The lowest point's own velocity: the ship's, and the turn's lever
        // arm to it (body rates about body axes, turned into universe axes).
        const FVector Arm = ShipLanding::FootprintPoints(Limits.GearClearanceCm)[Foot.Point];
        const FVector PointVelocity = Velocity + Orientation.RotateVector(FVector::CrossProduct(AngularVelocity, Arm));
        GroundLog.WorstContactSpeed = FMath::Max(GroundLog.WorstContactSpeed, -(PointVelocity | Foot.GroundNormal));
        ++GroundLog.Contacts;
    }
    LastFootprintLeast = Foot.Least;
}
```

In the header's public constants (beside `AtFloorCm`), add:

```cpp
    /** Penetration the ground's hard stop ignores, cm: a tenth of a
     *  millimetre, the rounding of a substep that ends exactly on the
     *  ground, which the approach law's D / Step bound already keeps out. */
    static constexpr double HardStopToleranceCm = 0.01;
```

- [ ] **Step 4: Run: PASS, and the sphere tests unchanged**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh && \
./test.sh DeepSpace.Ship.Landing && ./test.sh DeepSpace.Ship.Flight && ./test.sh DeepSpace.Ship.Gravity
```

Expected: every `DeepSpace.Ship.Landing.*` green (the new two with the laws and the footprint);
`DeepSpace.Ship.Flight*` green unchanged -- no fixture of theirs has a ground.

- [ ] **Step 5: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
git add Source/DeepSpace/Ship/ShipFlightState.h Source/DeepSpace/Ship/ShipFlightState.cpp Source/DeepSpace/Tests/ShipLandingFloorsTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(flight): two floors -- cruise reads the ground at every altitude; the ground's hard stop lifts

Over a solid world the drive keeps its sphere and cruise ray-casts,
brakes for and hard-stops at the ground plus the gear's clearance, never
the sphere, so a cruising ship descends through the drive floor. The
ground's hard stop takes the velocity into the ground's normal and lifts
the origin by the deepest footprint penetration; sphere floors keep
"never lifted" (landing decision 10).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Prove them**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'Position += Up * -Foot.Least;' 'Position += Up * 0.0;' DeepSpace.Ship.Landing.HardStopLiftsGroundOnly && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'continue;   // cruise'"'"'s floor over a solid world is the ground, below' 'if (false) continue;' DeepSpace.Ship.Landing.CruiseUnderDriveFloor && \
./build.sh
```

Expected: `KILLED` twice (no lift leaves the ship in the rock; the sphere applied to a solid world
stops the descent at the drive floor).

---

## Task 20 (F6): the vertical lever in the flight -- the near regime, the plan-view cruise, the climb top, the skim and descent caps, the starved sink

**Owner:** F. **Depends on:** F4, F5. Spec decisions 5, 8 and 10.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipFlightState.h` -- `FShipFlightLimits` (after
  `GearClearanceCm`), `FShipFlightCommand` (after `DriveNotch`, about line 109), public getters,
  private block and members
- Modify: `Source/DeepSpace/Ship/ShipFlightState.cpp` -- `SetCommand` (clamp), `SubStep`,
  `CruiseSubStep` (replaced whole), `JumpTo`, new functions
- Create: `Source/DeepSpace/Tests/ShipLandingRegimeTest.cpp`
  (`DeepSpace.Ship.Landing.RegimeTop`, `.HoverAndSink`, `.SkimAndLook`, and review focus 1's
  `.SinkOntoFloorSphere`)

**Interfaces:**
- Consumes: `ShipVerticalLever::Rate`, `ClimbTop`; `ShipFlight::SkimCap`, `GroundApproachSpeed`,
  `DefaultRegimeCm`, `RegimeExitFactor`, `RegimeBlendFraction`; F5's functions.
- Produces: `FShipFlightCommand::Vertical`; `FShipFlightLimits::{TouchdownSpeed, ApproachSeconds,
  SkimSeconds, SkimFloor, RegimeCm, VerticalTop, VerticalHeavyFloor, SinkBias}`;
  `bool FShipFlightState::IsInNearRegime() const`, `double GetRegimeWeight() const`,
  `bool IsVerticalLive() const`, `double GetVerticalLeverRate() const`,
  `double GetVerticalSpeed() const`.

- [ ] **Step 1: Write the failing tests**

Create `Source/DeepSpace/Tests/ShipLandingRegimeTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipGravity.h"
#include "Ship/ShipVerticalLever.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 8 in the flight: within 50 km of a world's cruise floor
 * the vertical lever is live, cruise flies the nose's horizontal projection,
 * and across 40-50 km both blend out; decision 5's climb top and starved
 * sink; decision 10's skim cap. Sign-off item 5 is RegimeTop.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingRegimeTopTest, "DeepSpace.Ship.Landing.RegimeTop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingHoverAndSinkTest, "DeepSpace.Ship.Landing.HoverAndSink",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingSkimAndLookTest, "DeepSpace.Ship.Landing.SkimAndLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LandingRegimeLocal
{
    using namespace GroundFixtures;
    constexpr double Dt = 1.0 / 60.0;

    double ThrottleFor(double Speed)
    {
        return FMath::Loge(Speed / ShipDriveLever::CruiseFloorCmPerSecond)
             / FMath::Loge(FShipFlightLimits().MaxSpeed / ShipDriveLever::CruiseFloorCmPerSecond);
    }

    /** A world near-flat enough that the ground's caps never bind by accident:
     *  metre-high swells a kilometre long. */
    FGroundFieldRef Swells(double Radius)
    {
        return MakeShared<FCrossedSines, ESPMode::ThreadSafe>(Radius, 100.0, 1.0e5);
    }

    FGravityWell WellOf(const FFlightSurface& World, double G)
    {
        return { World.Centre, G * ShipFlight::StandardGravityCmS2 * World.Radius * World.Radius, World.Radius };
    }

    struct FFlight
    {
        FShipFlightState State;
        FFlightSurface World;
        FVector3d D;

        FFlight(const FGroundFieldRef& Ground, double AglCm, double G, const FQuat& Turn = FQuat::Identity)
        {
            World = SurfaceOver(Ground, 1.02e6 + Ground->MaxHeightCm());
            D = FVector3d(0.002, -0.001, 1.0).GetSafeNormal();
            State.SetSurfaces({ World });
            State.SetWells({ WellOf(World, G) });
            State.SetUniverseTransform(Above(World, D, AglCm), Level(D) * Turn);
        }

        void Command(double Throttle, double Vertical)
        {
            FShipFlightCommand Command = State.GetCommand();
            Command.Throttle = Throttle;
            Command.Vertical = Vertical;
            State.SetCommand(Command);
        }

        double Agl() const { return State.GetGroundAltitude().Get(-1.0); }

        FVector Up() const { return (State.GetUniversePosition() - World.Centre).GetSafeNormal(); }

        double Horizontal() const
        {
            const FVector V = State.GetVelocity();
            return (V - Up() * (V | Up())).Size();
        }
    };
}

bool FLandingRegimeTopTest::RunTest(const FString& Parameters)
{
    using namespace LandingRegimeLocal;
    const double R = 0.9 * UniverseUnits::CmPerEarthRadius;
    const double Gear = ShipLanding::DefaultGearClearanceCm;

    // A climb at 200 m/s from 30 km with cruise at STOP comes to rest in the
    // band, under 50 km, and never reverses.
    {
        FFlight Flight(Swells(R), 3.0e6, 0.84);
        Flight.Command(0.0, 1.0);
        double Highest = 0.0;
        double WorstReverse = 0.0;
        for (double T = 0.0; T < 600.0; T += Dt)
        {
            Flight.State.Step(Dt);
            Highest = FMath::Max(Highest, Flight.Agl());
            WorstReverse = FMath::Min(WorstReverse, Flight.State.GetVerticalSpeed());
        }
        AddInfo(FString::Printf(TEXT("the climb came to %.2f km, %.3f m/s"), Highest / 1.0e5, Flight.State.GetVerticalSpeed() / 100.0));
        TestTrue(TEXT("a vertical climb from 30 km never passes the regime's top"), Highest - Gear < ShipFlight::DefaultRegimeCm);
        TestTrue(TEXT("and comes to rest in the band, above 40 km"), Highest - Gear > 0.8 * ShipFlight::DefaultRegimeCm
                 && FMath::Abs(Flight.State.GetVerticalSpeed()) < 50.0);
        TestTrue(TEXT("and never reverses"), WorstReverse > -1.0);
        TestTrue(TEXT("the lever is still asking: the ship is above the ground's reach"), Flight.State.GetRegimeWeight() < 0.05);
    }

    // With cruise set nose-up the ship passes through 50 km with no step in
    // its velocity: nothing changes by more than the boosters can.
    {
        FFlight Flight(Swells(R), 4.5e6, 0.84, FQuat(FVector::RightVector, FMath::DegreesToRadians(-30.0)));
        Flight.Command(ThrottleFor(2.0e5), 1.0);
        FVector Previous = Flight.State.GetVelocity();
        double WorstStep = 0.0;
        bool bPassed = false;
        for (double T = 0.0; T < 120.0; T += Dt)
        {
            Flight.State.Step(Dt);
            WorstStep = FMath::Max(WorstStep, (Flight.State.GetVelocity() - Previous).Size() / (Flight.State.GetLimits().LinearAcceleration * Dt));
            Previous = Flight.State.GetVelocity();
            bPassed |= Flight.Agl() > 1.2 * ShipFlight::DefaultRegimeCm;
        }
        TestTrue(TEXT("cruise nose-up climbs out through the regime's top"), bPassed);
        // The chase can never step the velocity, so what a hard line would
        // show is the boosters saturated for a moment as the target jumps;
        // the blend keeps the demand to a fraction of them.
        TestTrue(FString::Printf(TEXT("with no velocity step on the way (worst %.3f of the boosters)"), WorstStep), WorstStep <= 0.5);
    }

    // A sink asked above the regime with cruise at STOP moves nothing.
    {
        FFlight Flight(Swells(R), 6.0e6, 0.84);
        Flight.Command(0.0, -1.0);
        for (double T = 0.0; T < 10.0; T += Dt)
        {
            Flight.State.Step(Dt);
        }
        TestFalse(TEXT("60 km up is outside the regime"), Flight.State.IsInNearRegime());
        TestTrue(TEXT("and there the vertical lever moves nothing"), Flight.State.GetSpeed() < 1.0 && !Flight.State.IsVerticalLive());
    }

    // Hysteresis: enters under 50 km, leaves over 55 km.
    {
        FFlight Flight(Swells(R), 5.2e6 + Gear, 0.84);
        Flight.State.Step(Dt);
        TestFalse(TEXT("52 km, coming from above: not in"), Flight.State.IsInNearRegime());
        Flight.State.SetUniverseTransform(Above(Flight.World, Flight.D, 4.9e6 + Gear), Level(Flight.D));
        Flight.State.Step(Dt);
        TestTrue(TEXT("49 km: in"), Flight.State.IsInNearRegime());
        Flight.State.SetUniverseTransform(Above(Flight.World, Flight.D, 5.2e6 + Gear), Level(Flight.D));
        Flight.State.Step(Dt);
        TestTrue(TEXT("back to 52 km: still in"), Flight.State.IsInNearRegime());
        Flight.State.SetUniverseTransform(Above(Flight.World, Flight.D, 5.6e6 + Gear), Level(Flight.D));
        Flight.State.Step(Dt);
        TestFalse(TEXT("56 km: out"), Flight.State.IsInNearRegime());
    }
    return true;
}

bool FLandingHoverAndSinkTest::RunTest(const FString& Parameters)
{
    using namespace LandingRegimeLocal;
    const double R = 1.39 * UniverseUnits::CmPerEarthRadius;

    // Hover holds, anywhere, beside a 3 g world: gravity is held, not flown.
    {
        FFlight Flight(Swells(R), 1.0e5, 3.0);
        Flight.Command(0.0, 0.0);
        const FUniversePosition Start = Flight.State.GetUniversePosition();
        for (double T = 0.0; T < 600.0; T += 0.5)
        {
            Flight.State.Step(0.5);
        }
        TestTrue(FString::Printf(TEXT("ten minutes of HOVER beside 3 g moves it %.4f cm"), Flight.State.GetUniversePosition().DistanceTo(Start)),
                 Flight.State.GetUniversePosition().DistanceTo(Start) < 1.0);
    }

    // The climb top falls on heavy worlds, by gravity alone.
    {
        FFlight Flight(Swells(R), 2.0e5, 3.3);
        Flight.Command(0.0, 1.0);
        double Fastest = 0.0;
        for (double T = 0.0; T < 60.0; T += Dt)
        {
            Flight.State.Step(Dt);
            Fastest = FMath::Max(Fastest, Flight.State.GetVerticalSpeed());
        }
        const double Top = 2.0e4 / 3.3;
        TestTrue(FString::Printf(TEXT("on a 3.3 g world the climb tops out at 61 m/s (%.2f)"), Fastest / 100.0),
                 FMath::IsNearlyEqual(Fastest, Top, 0.01 * Top));
        TestTrue(TEXT("and the lever reads it so"), FMath::IsNearlyEqual(Flight.State.GetVerticalLeverRate(), Top, 1e-6));
    }

    // The starved sink: only under the floor, only at HOVER or sinking,
    // eased to rest on the ground.
    {
        FFlight Flight(Swells(R), 3.0e4, 1.0);
        FShipFlightLimits Limits = Flight.State.GetLimits();
        Limits.SinkBias = 200.0;
        Flight.State.SetLimits(Limits);
        Flight.Command(0.0, 0.0);
        double Sinking = 0.0;
        double RestFor = 0.0;
        double T = 0.0;
        for (; T < 600.0 && RestFor < 2.0; T += Dt)
        {
            Flight.State.Step(Dt);
            Sinking = FMath::Min(Sinking, Flight.State.GetVerticalSpeed());
            RestFor = Flight.State.GetSpeed() < 0.5 ? RestFor + Dt : 0.0;
        }
        TestTrue(FString::Printf(TEXT("starved at HOVER under the floor it sinks at 2 m/s (%.2f)"), -Sinking / 100.0),
                 FMath::IsNearlyEqual(-Sinking, 200.0, 2.0));
        TestTrue(TEXT("and the ground catches it: at rest, a foot on it"), RestFor >= 2.0
                 && FMath::Abs(Flight.State.GetFootprintClearance().Get(-1.0e9)) < 1.0);
        TestTrue(TEXT("never more than a centimetre in, never faster than 0.5 m/s at contact"),
                 Flight.State.GetGroundLog().LeastClearance >= -1.0 && Flight.State.GetGroundLog().WorstContactSpeed <= ShipFlight::DefaultTouchdownSpeed + 1e-6);

        Flight.Command(0.0, ShipVerticalLever::LeverOf(100.0, ShipVerticalLever::DefaultTopCmPerSecond));
        for (double Up = 0.0; Up < 5.0; Up += Dt)
        {
            Flight.State.Step(Dt);
        }
        TestTrue(TEXT("a starved ship asked to climb climbs: the sink is never applied to a climb"), Flight.State.GetVerticalSpeed() > 90.0);
    }
    {
        FFlight Flight(Swells(R), 0.0, 1.0);
        Flight.State.SetUniverseTransform(Flight.World.Centre + FVector(Flight.D) * (Flight.World.Radius + Flight.World.Floor + 1.0e5), Level(Flight.D));
        FShipFlightLimits Limits = Flight.State.GetLimits();
        Limits.SinkBias = 200.0;
        Flight.State.SetLimits(Limits);
        Flight.Command(0.0, 0.0);
        const FUniversePosition Start = Flight.State.GetUniversePosition();
        for (double T = 0.0; T < 60.0; T += 0.5)
        {
            Flight.State.Step(0.5);
        }
        TestTrue(TEXT("above the drive floor, starved, nothing sinks"), Flight.State.GetUniversePosition().DistanceTo(Start) < 1.0);
    }
    return true;
}

bool FLandingSkimAndLookTest::RunTest(const FString& Parameters)
{
    using namespace LandingRegimeLocal;
    const double R = 0.9 * UniverseUnits::CmPerEarthRadius;

    // 500 m up, cruise at full: the skim cap holds it to AGL / 2.5 s.
    {
        FFlight Flight(Swells(R), 5.0e4, 0.84);
        Flight.Command(1.0, 0.0);
        for (double T = 0.0; T < 30.0; T += Dt)
        {
            Flight.State.Step(Dt);
        }
        const double Cap = ShipFlight::SkimCap(Flight.Agl(), ShipFlight::DefaultSkimSeconds, ShipFlight::DefaultSkimFloor);
        TestTrue(FString::Printf(TEXT("at 500 m full cruise skims at 200 m/s (%.1f of %.1f m/s)"), Flight.Horizontal() / 100.0, Cap / 100.0),
                 FMath::IsNearlyEqual(Flight.Horizontal(), Cap, 0.02 * Cap));
        TestTrue(TEXT("and says so: HOLDING OFF"), Flight.State.GetHold() == EFlightHold::HoldingOff);
    }

    // Looking down no longer dives: nose 60 degrees down, 50 m/s, HOVER.
    {
        FFlight Flight(Swells(R), 2.0e5, 0.84, FQuat(FVector::RightVector, FMath::DegreesToRadians(60.0)));
        Flight.Command(ThrottleFor(5.0e3), 0.0);
        const double Start = Flight.Agl();
        for (double T = 0.0; T < 20.0; T += Dt)
        {
            Flight.State.Step(Dt);
        }
        TestTrue(FString::Printf(TEXT("nose down at HOVER, the height holds (moved %.2f m)"), (Flight.Agl() - Start) / 100.0),
                 FMath::Abs(Flight.Agl() - Start) < 200.0);
        TestTrue(TEXT("and the ship goes where it points, in plan, at the lever's speed"),
                 FMath::IsNearlyEqual(Flight.Horizontal(), 5.0e3, 50.0));
    }
    return true;
}

#endif
```

(The `.HoverAndSink` hover case at 1 km is under the drive floor with `SinkBias` 0, so it is the
pure hold; its starved half sets the bias the subsystem will compute. Note `FFlight`'s ground is
the swell field: the regime's top at 50 km is measured over it.)

Task F5's `.CruiseUnderDriveFloor` flies nose-down cruise onto the ground. In the regime that no
longer descends -- looking down never dives the ship, which is this task's point -- so amend it
in `ShipLandingFloorsTest.cpp`: right after `Flight.SetSurfaces({ World });` in
`FLandingCruiseUnderDriveFloorTest::RunTest`, add

```cpp
    // Above the regime cruise still flies along the nose; with no regime at
    // all this is the two floors alone -- the sphere is not cruise's floor.
    FShipFlightLimits NoRegime = Flight.GetLimits();
    NoRegime.RegimeCm = 0.0;
    Flight.SetLimits(NoRegime);
```

and before its `return true;` add the spec's own leg, a cruise ship at 5 km descending on C:

```cpp
    FShipFlightState OnC;
    OnC.SetSurfaces({ World });
    OnC.SetUniverseTransform(Above(World, D, 5.0e5), Level(D));
    FShipFlightCommand Sink;
    Sink.Vertical = -1.0;
    OnC.SetCommand(Sink);
    double Rested = 0.0;
    for (double T = 0.0; T < 200.0 && Rested < 2.0; T += 1.0 / 60.0)
    {
        OnC.Step(1.0 / 60.0);
        Rested = OnC.GetSpeed() < 0.5 ? Rested + 1.0 / 60.0 : 0.0;
    }
    TestTrue(TEXT("at 5 km, C brings a cruising ship down through the drive floor to rest on the ground"),
             Rested >= 2.0 && FMath::Abs(OnC.GetFootprintClearance().Get(-1.0e9)) < 1.0 && OnC.GetGroundLog().LeastClearance >= -1.0);
```

- [ ] **Step 2: Run and see them fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh
```

Expected: fails to compile: `'Vertical': is not a member of 'FShipFlightCommand'`.

- [ ] **Step 3: The lever and the limits**

In `FShipFlightLimits`, after `GearClearanceCm`:

```cpp
    /** ds.Land.TouchdownSpeed, cm/s: the approach law's floor, contact speed. */
    double TouchdownSpeed = ShipFlight::DefaultTouchdownSpeed;

    /** ds.Land.ApproachSeconds: the approach law's ease; clamped where read. */
    double ApproachSeconds = ShipFlight::DefaultApproachSeconds;

    /** ds.Land.SkimSeconds and .SkimFloor (cm/s): the skim cap. */
    double SkimSeconds = ShipFlight::DefaultSkimSeconds;
    double SkimFloor = ShipFlight::DefaultSkimFloor;

    /** ds.Land.Regime, cm: the near regime's reach over a world's cruise floor. */
    double RegimeCm = ShipFlight::DefaultRegimeCm;

    /** ds.Vertical.Top (cm/s) and .HeavyFloor: the lever's top and the climb
     *  top's floor on heavy worlds. */
    double VerticalTop = ShipVerticalLever::DefaultTopCmPerSecond;
    double VerticalHeavyFloor = ShipVerticalLever::DefaultHeavyFloor;

    /**
     * The starved sink, cm/s (decision 5): ds.Boosters.StarvedSink x (1 -
     * HoldFed), computed by the subsystem from the split. Added to the
     * vertical lever's asked rate only under a solid world's drive floor and
     * only while the lever asks HOVER or a sink -- a starved ship always
     * lifts. The one sanctioned change with time in the power model, bounded,
     * ending at rest on the ground.
     */
    double SinkBias = 0.0;
```

(add `#include "Ship/ShipVerticalLever.h"`). In `FShipFlightCommand` after `DriveNotch`:

```cpp
    /** The vertical lever, -1..1 (landing decision 8): a climb or sink rate
     *  on ShipVerticalLever's log scale, the boosters holding it against
     *  gravity. At zero the ship hovers, and keeps hovering with nobody at
     *  the helm. Persistent, like both other levers: carry it over from
     *  GetCommand() when building a command. */
    double Vertical = 0.0;
```

In `SetCommand`, after the `DriveNotch` clamp: `Command.Vertical = FMath::Clamp(NewCommand.Vertical, -1.0, 1.0);`.

In the public block:

```cpp
    /** In the near regime (decision 8): within Limits.RegimeCm of the
     *  nearest world's cruise floor -- the ground over a solid world, the
     *  floor sphere otherwise -- entering under it and leaving over 1.1 x it. */
    bool IsInNearRegime() const;

    /** 1 at 40 km and under, 0 at 50 km and over: how far cruise flies the
     *  plan view and the vertical lever counts. 0 outside the regime. */
    double GetRegimeWeight() const;

    /** The vertical lever moves the ship: in the regime with the weight
     *  above 0, and cruise's lever flying (Cruise, or DriveBelowFloor). */
    bool IsVerticalLive() const;

    /** What the vertical lever asks, cm/s, + climbing, after the climb top:
     *  the HUD's CLIMB / SINK / HOVER. The starved sink is not in it. */
    double GetVerticalLeverRate() const;

    /** The ship's radial speed, cm/s, + climbing, over the regime's world
     *  (or the nearest world); 0 with none. */
    double GetVerticalSpeed() const;
```

In the private block:

```cpp
    /** Once a substep: which world is near, the regime with its hysteresis,
     *  and the blend weight. */
    void UpdateRegime();

    /** The cruise floor's clearance over one surface, cm. */
    double CruiseFloorClearance(const FFlightSurface& Surface) const;

    /** What the plan asks radially, cm/s: the lever's rate, the climb top,
     *  and the starved sink under the floor at HOVER or sinking. */
    double AskedVerticalRate() const;

    /** The ground ahead of the ship's origin along a horizontal Heading at
     *  its own height, less the hull's reach, for the along-ground cap. */
    TOptional<double> GroundAhead(int32 SurfaceIndex, const FVector& Heading, double Speed);
```

and members `bool bInRegime = false; double RegimeWeight = 0.0; int32 RegimeSurface = INDEX_NONE;`.

- [ ] **Step 4: The regime, and the new cruise substep**

In `ShipFlightState.cpp`, in `SubStep`, after the attitude integration and before the drive
dispatch, add `UpdateRegime();`. In `JumpTo`, add
`Command.Vertical = 0.0; bInRegime = false; RegimeWeight = 0.0; RegimeSurface = INDEX_NONE;`.

Replace `CruiseSubStep` whole with:

```cpp
void FShipFlightState::CruiseSubStep(double FixedDelta)
{
    const FVector Nose = Orientation.GetForwardVector();
    const double Want = CruiseLeverSpeed();
    const FVector Along = Nose * (Want < 0.0 ? -1.0 : 1.0);
    const double Speed = FMath::Abs(Want);
    const double W = RegimeWeight;
    LastHold = EFlightHold::Free;
    LastHeldFraction = 0.0;

    // Above the regime, and fading out across its top: along the nose, held
    // to the braking curve by whatever the path meets -- the ground over a
    // solid world, the sphere otherwise (decision 5, unchanged far out).
    FVector Classic = FVector::ZeroVector;
    if (W < 1.0 && Speed > 0.0)
    {
        double Held = Speed;
        if (const TOptional<double> D = NearestOnCruisePath(Along, Speed))
        {
            const double May = ShipFlight::MaySpeed(*D, Limits.LinearAcceleration, 0.0, FixedStep);
            if (May < Held)
            {
                Held = May;
                RecordHold(*D, May, Speed);
            }
        }
        Classic = Along * Held;
    }

    // In the regime (decision 8): the nose's horizontal projection at the
    // cruise lever's speed, plus up at the vertical lever's rate, so looking
    // down never dives the ship and zero is hover for any cruise setting.
    // Over solid ground the horizontal part is held to the skim cap and to
    // the ground ahead at the ship's height.
    FVector Plan = FVector::ZeroVector;
    FVector Up = FVector::ZeroVector;
    const FFlightSurface* World = Surfaces.IsValidIndex(RegimeSurface) ? &Surfaces[RegimeSurface] : nullptr;
    if (W > 0.0 && World)
    {
        Up = (Position - World->Centre).GetSafeNormal();
        const FVector Flat = Along - Up * (Along | Up);
        const double FlatSize = Flat.Size();
        const FVector Heading = FlatSize > 1.0e-6 ? Flat / FlatSize : FVector::ZeroVector;
        double Horizontal = FlatSize > 1.0e-6 ? Speed : 0.0;
        if (World->HasGround() && Horizontal > 0.0)
        {
            const double Skim = ShipFlight::SkimCap(ShipFlight::GroundAt(*World, Position).Get(0.0), Limits.SkimSeconds, Limits.SkimFloor);
            if (Skim < Horizontal)
            {
                Horizontal = Skim;
                RecordHold(TNumericLimits<double>::Max(), Skim, Speed);
            }
            if (const TOptional<double> D = GroundAhead(RegimeSurface, Heading, Horizontal))
            {
                const double May = ShipFlight::GroundApproachSpeed(*D, Limits.LinearAcceleration, Limits.ApproachSeconds,
                                                                   Limits.TouchdownSpeed, FixedStep);
                if (May < Horizontal)
                {
                    Horizontal = May;
                    RecordHold(*D, May, Speed);
                }
            }
        }
        Plan = Heading * Horizontal + Up * AskedVerticalRate();
    }
    FVector Target = Classic * (1.0 - W) + Plan * W;

    // Down (decision 10): whatever the levers and the starved sink ask, the
    // descent is held to the approach law over the footprint's least
    // clearance, so the ground always catches, gently.
    if (W > 0.0 && World && World->HasGround())
    {
        const double Down = -(Target | Up);
        const double Clear = ShipLanding::FootprintClearance(*World, Position, Orientation, Limits.GearClearanceCm).Least;
        const double May = ShipFlight::GroundApproachSpeed(FMath::Max(Clear, 0.0), Limits.LinearAcceleration,
                                                           Limits.ApproachSeconds, Limits.TouchdownSpeed, FixedStep);
        if (Down > May)
        {
            Target += Up * (Down - May);
        }
    }

    // Velocity, chasing the target, as a vector (a turn cannot cheat
    // acceleration out of the model by changing direction).
    const FVector VelocityError = Target - Velocity;
    const double MaxVelocityChange = Limits.LinearAcceleration * FixedDelta;
    const FVector VelocityChange = VelocityError.SizeSquared() <= FMath::Square(MaxVelocityChange)
        ? VelocityError
        : VelocityError.GetSafeNormal() * MaxVelocityChange;
    Velocity += VelocityChange;
    LastLinearAcceleration = VelocityChange / FixedDelta;

    // The sphere hard stop, as it always was, for surfaces with no ground.
    FUniversePosition Next = Position + Velocity * FixedDelta;
    for (const FFlightSurface& Surface : Surfaces)
    {
        if (Surface.HasGround())
        {
            continue;   // cruise's floor over a solid world is the ground, below
        }
        const double After = ShipFlight::FloorClearance(Surface, Next);
        if (After >= 0.0)
        {
            continue;
        }
        const FVector FromCentre = Next - Surface.Centre;
        const FVector Out = (Surface.bInsideOut ? -FromCentre : FromCentre).GetSafeNormal();
        const double Inward = Velocity | Out;
        if (Inward < 0.0)
        {
            Velocity -= Out * Inward;
        }
        Next = Position + Velocity * FixedDelta;
        if (ShipFlight::FloorClearance(Surface, Position) >= 0.0)
        {
            Next = Next + Out * (-ShipFlight::FloorClearance(Surface, Next));
        }
    }
    Position = Next;
    GroundHardStop();
}
```

Append:

```cpp
double FShipFlightState::CruiseFloorClearance(const FFlightSurface& Surface) const
{
    const TOptional<double> Ground = ShipFlight::GroundAt(Surface, Position);
    return Ground ? *Ground - Limits.GearClearanceCm : ShipFlight::FloorClearance(Surface, Position);
}

void FShipFlightState::UpdateRegime()
{
    int32 Best = INDEX_NONE;
    double Least = TNumericLimits<double>::Max();
    for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
    {
        if (Surfaces[Index].bWorld && !Surfaces[Index].bInsideOut)
        {
            const double Clear = CruiseFloorClearance(Surfaces[Index]);
            if (Clear < Least)
            {
                Least = Clear;
                Best = Index;
            }
        }
    }
    const double Enter = FMath::Max(Limits.RegimeCm, 1.0);
    if (Best == INDEX_NONE)
    {
        bInRegime = false;
    }
    else if (!bInRegime && Least < Enter)
    {
        bInRegime = true;
    }
    else if (bInRegime && Least > Enter * ShipFlight::RegimeExitFactor)
    {
        bInRegime = false;
    }
    RegimeSurface = Best;
    RegimeWeight = bInRegime ? FMath::Clamp((Enter - Least) / (Enter * ShipFlight::RegimeBlendFraction), 0.0, 1.0) : 0.0;
}

double FShipFlightState::GetVerticalLeverRate() const
{
    const double Rate = ShipVerticalLever::Rate(Command.Vertical, Limits.VerticalTop);
    return FMath::Min(Rate, ShipVerticalLever::ClimbTop(Limits.VerticalTop, GetLocalGravity().Size(), Limits.VerticalHeavyFloor));
}

double FShipFlightState::AskedVerticalRate() const
{
    double Rate = GetVerticalLeverRate();
    if (Rate <= 0.0 && GetDepthUnderDriveFloor() > 0.0)
    {
        Rate -= FMath::Max(Limits.SinkBias, 0.0);
    }
    return Rate;
}

TOptional<double> FShipFlightState::GroundAhead(int32 SurfaceIndex, const FVector& Heading, double Speed)
{
    const double Reach = ShipLanding::ReachCm(Limits.GearClearanceCm);
    const double Braking = 2.0 * ShipFlight::BrakingMargin * FMath::Max(Limits.LinearAcceleration, 1.0);
    const double Lookahead = 1.5 * (Speed * Speed / Braking + Speed * FMath::Max(Limits.ApproachSeconds, ShipFlight::MinApproachSeconds))
                           + Reach + 1.0e3;
    const TOptional<double> D = ShipFlight::RayToGround(Surfaces[SurfaceIndex], Position, Heading, Limits.GearClearanceCm, Lookahead);
    return D ? TOptional<double>(FMath::Max(0.0, *D - Reach)) : TOptional<double>();
}

bool FShipFlightState::IsInNearRegime() const { return bInRegime; }
double FShipFlightState::GetRegimeWeight() const { return RegimeWeight; }

bool FShipFlightState::IsVerticalLive() const
{
    return RegimeWeight > 0.0 && !Command.bDrive && !bSpoolingDown;
}

double FShipFlightState::GetVerticalSpeed() const
{
    int32 Index = RegimeSurface;
    if (!Surfaces.IsValidIndex(Index))
    {
        return 0.0;
    }
    return Velocity | (Position - Surfaces[Index].Centre).GetSafeNormal();
}
```

(`IsVerticalLive` is widened to `DriveBelowFloor` in Task F7.)

- [ ] **Step 5: Run: PASS, and nothing else moved**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh && \
./test.sh DeepSpace.Ship.Landing && ./test.sh DeepSpace.Ship.Flight && ./test.sh DeepSpace.Ship.Gravity
```

Expected: all green. `DeepSpace.Ship.FlightCruiseFloor` does not move: none of its fixtures sets
`bWorld`, so there is no regime and cruise is exactly as before.

- [ ] **Step 6: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
git add Source/DeepSpace/Ship/ShipFlightState.h Source/DeepSpace/Ship/ShipFlightState.cpp Source/DeepSpace/Tests/ShipLandingRegimeTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(flight): the vertical lever flies -- near regime, plan-view cruise, climb top, skim and descent caps, starved sink

Within 50 km of a world's cruise floor (leaving over 55) cruise flies the
nose's horizontal projection and the vertical lever the radial rate, both
blending out across 40-50 km; the climb top falls by gravity alone; the
skim cap holds horizontal speed to AGL / 2.5 s; the descent is held to the
approach law over the footprint; and the starved sink is applied only
under a solid world's drive floor, never to a climb (landing decisions
5, 8, 10).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7: Prove them**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'RegimeWeight = bInRegime ? FMath::Clamp((Enter - Least) / (Enter * ShipFlight::RegimeBlendFraction), 0.0, 1.0) : 0.0;' 'RegimeWeight = bInRegime ? 1.0 : 0.0;' DeepSpace.Ship.Landing.RegimeTop && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'if (Rate <= 0.0 && GetDepthUnderDriveFloor() > 0.0)' 'if (GetDepthUnderDriveFloor() > 0.0)' DeepSpace.Ship.Landing.HoverAndSink && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'if (Skim < Horizontal)' 'if (false)' DeepSpace.Ship.Landing.SkimAndLook && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'if (Down > May)' 'if (false)' DeepSpace.Ship.Landing.HoverAndSink && \
./build.sh
```

Expected: `KILLED` four times.

- [ ] **Step 8: Review focus 1 -- the vertical lever over a world with no ground (write the failing test)**

Oceans and giants keep the floor sphere (ruling 1), but the regime is any world's: the vertical
lever is live over them too, and the descent cap above reads only a ground. A full sink onto an
ocean's floor would meet the sphere hard stop at 200 m/s, a velocity step six times what the
boosters can do in a frame. In `ShipLandingRegimeTest.cpp`, beside the three declarations, add

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingSinkOntoFloorSphereTest, "DeepSpace.Ship.Landing.SinkOntoFloorSphere",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

and before `#endif`:

```cpp
bool FLandingSinkOntoFloorSphereTest::RunTest(const FString& Parameters)
{
    using namespace LandingRegimeLocal;
    // An ocean: a world with no ground, which keeps its floor sphere 10 km up.
    FFlightSurface Ocean;
    Ocean.Centre = Somewhere();
    Ocean.Radius = 0.9 * UniverseUnits::CmPerEarthRadius;
    Ocean.Floor = 1.0e6;
    Ocean.bWorld = true;
    const FVector3d D = FVector3d(0.002, -0.001, 1.0).GetSafeNormal();
    FShipFlightState State;
    State.SetSurfaces({ Ocean });
    State.SetWells({ WellOf(Ocean, 0.84) });
    State.SetUniverseTransform(Ocean.Centre + FVector(D) * (Ocean.Radius + Ocean.Floor + 2.0e5), Level(D));
    FShipFlightCommand Command = State.GetCommand();
    Command.Vertical = -1.0;
    State.SetCommand(Command);

    FVector Previous = State.GetVelocity();
    double WorstStep = 0.0;
    double Least = TNumericLimits<double>::Max();
    bool bLive = false;
    for (double T = 0.0; T < 120.0; T += Dt)
    {
        State.Step(Dt);
        bLive |= State.IsVerticalLive();
        WorstStep = FMath::Max(WorstStep, (State.GetVelocity() - Previous).Size() / (State.GetLimits().LinearAcceleration * Dt));
        Previous = State.GetVelocity();
        Least = FMath::Min(Least, ShipFlight::FloorClearance(Ocean, State.GetUniversePosition()));
    }
    const double Clear = ShipFlight::FloorClearance(Ocean, State.GetUniversePosition());
    TestTrue(TEXT("2 km over an ocean's floor the vertical lever is live"), bLive);
    TestTrue(FString::Printf(TEXT("a full sink comes to rest on the floor sphere (%.1f cm over it, %.2f cm/s)"), Clear, State.GetSpeed()),
             Clear >= -1.0 && Clear < 100.0 && State.GetSpeed() < 1.0);
    TestTrue(FString::Printf(TEXT("never under it (least %.2f cm)"), Least), Least >= -1.0);
    TestTrue(FString::Printf(TEXT("braked by the boosters all the way: no velocity step past them (worst %.3f of them)"), WorstStep),
             WorstStep <= 1.0 + 1.0e-6);
    TestFalse(TEXT("and there is no ground altitude: oceans keep the floor"), State.GetGroundAltitude().IsSet());
    return true;
}
```

Run:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh && ./test.sh DeepSpace.Ship.Landing.SinkOntoFloorSphere
```

Expected: FAIL on `braked by the boosters all the way` (worst about 6: the sphere hard stop takes
200 m/s out in one substep).

- [ ] **Step 9: The floor sphere's braking curve in the regime**

In `CruiseSubStep`, directly after the `// Down (decision 10)` block's closing brace, add:

```cpp
    // Over a world with no ground (an ocean, a giant) the floor sphere is
    // what the descent meets: cruise's own braking curve to it, as cruise has
    // always braked for a floor, so the vertical lever sinks onto it and
    // never into the sphere hard stop at speed.
    if (W > 0.0 && World && !World->HasGround())
    {
        const double Sinking = -(Target | Up);
        const double MaySink = ShipFlight::MaySpeed(FMath::Max(ShipFlight::FloorClearance(*World, Position), 0.0),
                                                    Limits.LinearAcceleration, 0.0, FixedStep);
        if (Sinking > MaySink)
        {
            Target += Up * (Sinking - MaySink);
        }
    }
```

Run `./build.sh && ./test.sh DeepSpace.Ship.Landing && ./test.sh DeepSpace.Ship.Flight`. Expected: all
green, `SinkOntoFloorSphere` included.

- [ ] **Step 10: Commit, then prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
git add Source/DeepSpace/Ship/ShipFlightState.cpp Source/DeepSpace/Tests/ShipLandingRegimeTest.cpp && \
git commit -m "$(cat <<'MSG'
fix(flight): the vertical lever sinks onto a groundless world's floor sphere on the braking curve

Oceans and giants keep the floor, and the regime is any world's, so a full
sink over one met the sphere hard stop at 200 m/s. The descent there is now
held to cruise's own braking curve to the sphere (review focus 1).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
)" && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'if (Sinking > MaySink)' 'if (false)' DeepSpace.Ship.Landing.SinkOntoFloorSphere && \
./build.sh
```

Expected: `KILLED`.

---

## Task 21 (F7): `DriveBelowFloor` -- F under a solid world's drive floor flies cruise, and the drive takes over above it

**Owner:** F. **Depends on:** F6. Spec decision 10 (*Under the drive's floor the drive does not
take the ship: an explicit mode*), sign-off item 4.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipFlightState.h` -- `EFlightMode` (lines 113-128),
  `FShipFlightLimits` (after `RegimeCm`), private members
- Modify: `Source/DeepSpace/Ship/ShipFlightState.cpp` -- `SetCommand` (the leaving branch),
  `SubStep`, `JumpTo`, `GetMode`, `GetLeverSpeed`, `GetOtherLeverSpeed`, `IsVerticalLive`, new
  `UpdateDriveBelowFloor`
- Create: `Source/DeepSpace/Tests/ShipLandingDriveTest.cpp`
  (`DeepSpace.Ship.Landing.DriveUnderFloor`, `.DriveTakesOverAbove`)

**Interfaces:**
- Consumes: F6.
- Produces: `EFlightMode::DriveBelowFloor`; `FShipFlightLimits::DriveHandbackCm`.

- [ ] **Step 1: Write the failing tests**

Create `Source/DeepSpace/Tests/ShipLandingDriveTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipGravity.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 10: F is never refused. Pressed under a solid world's
 * drive floor, the drive does not take the ship -- the ship flies cruise and
 * the vertical lever, Shift and Ctrl move cruise, the drive's notch keeps
 * its setting and its position is held at the ship's own forward speed --
 * and the drive takes over only 500 m above the floor with the nose clear
 * of it. Once the drive flies, its own cap keeps it at or above the floor,
 * so the two modes cannot alternate.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingDriveUnderFloorTest, "DeepSpace.Ship.Landing.DriveUnderFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingDriveTakesOverTest, "DeepSpace.Ship.Landing.DriveTakesOverAbove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LandingDriveLocal
{
    using namespace GroundFixtures;
    constexpr double Dt = 1.0 / 60.0;

    struct FFlight
    {
        FShipFlightState State;
        FFlightSurface World;
        FVector3d D;

        FFlight(double AglCm, const FQuat& Turn = FQuat::Identity)
        {
            const FGroundFieldRef Ground = ShipGround::FromRelief(FixtureParams());
            World = SurfaceOver(Ground, 1.02e6 + Ground->MaxHeightCm());
            D = FVector3d(0.004, 0.003, 1.0).GetSafeNormal();
            State.SetSurfaces({ World });
            State.SetWells({ { World.Centre, 0.84 * ShipFlight::StandardGravityCmS2 * World.Radius * World.Radius, World.Radius } });
            State.SetUniverseTransform(Above(World, D, AglCm), Level(D) * Turn);
        }

        void Engage(int32 Notch, double Throttle, double Vertical)
        {
            FShipFlightCommand Command = State.GetCommand();
            Command.bDrive = true;
            Command.DriveNotch = Notch;
            Command.Throttle = Throttle;
            Command.Vertical = Vertical;
            State.SetCommand(Command);
        }

        double OverFloor() const { return ShipFlight::FloorClearance(World, State.GetUniversePosition()); }
    };
}

bool FLandingDriveUnderFloorTest::RunTest(const FString& Parameters)
{
    using namespace LandingDriveLocal;
    FFlight Flight(5.0e4);
    const int32 Top = Flight.State.GetDriveNotchCount() - 1;
    Flight.Engage(Top, 0.0, 0.0);

    bool bAlwaysBelow = true;
    for (double T = 0.0; T < 10.0; T += Dt)
    {
        Flight.State.Step(Dt);
        bAlwaysBelow &= Flight.State.GetMode() == EFlightMode::DriveBelowFloor;
    }
    TestTrue(TEXT("F at 500 m: the ship says DriveBelowFloor, every substep"), bAlwaysBelow);
    TestTrue(TEXT("and with cruise at STOP it does not move -- never 0.1 c through mountains"), Flight.State.GetSpeed() < 1.0);
    TestEqual(TEXT("the drive's notch keeps its setting"), Flight.State.GetCommand().DriveNotch, Top);
    TestEqual(TEXT("the live lever is cruise's"), Flight.State.GetLeverSpeed(), 0.0);
    TestEqual(TEXT("the dim one the drive's"), Flight.State.GetOtherLeverSpeed(), ShipDriveLever::NotchSpeed(Top));
    TestTrue(TEXT("and the vertical lever is live"), Flight.State.IsVerticalLive());

    FShipFlightCommand Command = Flight.State.GetCommand();
    Command.Throttle = 1.0;
    Flight.State.SetCommand(Command);
    double Fastest = 0.0;
    double WorstOverCap = 0.0;
    double HeldWorst = 0.0;
    for (double T = 0.0; T < 30.0; T += Dt)
    {
        Flight.State.Step(Dt);
        Fastest = FMath::Max(Fastest, Flight.State.GetSpeed());
        const double Cap = ShipFlight::SkimCap(Flight.State.GetGroundAltitude().Get(0.0), ShipFlight::DefaultSkimSeconds, ShipFlight::DefaultSkimFloor);
        WorstOverCap = FMath::Max(WorstOverCap, Flight.State.GetSpeed() / Cap);
        const double Forward = FMath::Max(0.0, Flight.State.GetVelocity() | Flight.State.GetUniverseOrientation().GetForwardVector());
        HeldWorst = FMath::Max(HeldWorst, FMath::Abs(ShipDriveLever::SpeedAt(Flight.State.GetDrivePosition()) - Forward));
    }
    TestTrue(FString::Printf(TEXT("cruise's lever flies it (%.1f m/s)"), Fastest / 100.0), Fastest > 1.0e3);
    TestTrue(FString::Printf(TEXT("held to the skim cap, never the drive's 0.1 c (worst %.3f of the cap)"), WorstOverCap), WorstOverCap <= 1.05);
    TestTrue(FString::Printf(TEXT("the drive's position is held at the ship's forward speed (worst %.3f cm/s)"), HeldWorst), HeldWorst < 1.0);
    TestTrue(TEXT("and no point of the hull went under the ground"), Flight.State.GetGroundLog().LeastClearance >= -1.0);

    // F again, flying under the floor: cruise at once -- there is nothing to
    // spool down from, and no dead stop.
    const double Before = Flight.State.GetSpeed();
    FShipFlightCommand Leave = Flight.State.GetCommand();
    Leave.bDrive = false;
    Flight.State.SetCommand(Leave);
    Flight.State.Step(Dt);
    TestTrue(TEXT("F again under the floor: cruise, with no spool-down"), Flight.State.GetMode() == EFlightMode::Cruise);
    TestTrue(FString::Printf(TEXT("and no dead stop (%.1f -> %.1f m/s)"), Before / 100.0, Flight.State.GetSpeed() / 100.0),
             Flight.State.GetSpeed() >= 0.9 * Before);
    return true;
}

bool FLandingDriveTakesOverTest::RunTest(const FString& Parameters)
{
    using namespace LandingDriveLocal;

    // Under the floor, nose up 20 degrees, climbing on the vertical lever
    // with the drive engaged at its first notch: once 500 m over the floor
    // with the nose clear of it, the drive takes over, once, for good.
    {
        FFlight Flight(0.0, FQuat(FVector::RightVector, FMath::DegreesToRadians(-20.0)));
        Flight.State.SetUniverseTransform(Flight.World.Centre + FVector(Flight.D) * (Flight.World.Radius + Flight.World.Floor - 2.0e5),
                                          Flight.State.GetUniverseOrientation());
        const double Below = -Flight.OverFloor();
        Flight.Engage(1, 0.0, 1.0);
        int32 Changes = 0;
        EFlightMode Was = EFlightMode::DriveBelowFloor;
        double HandedAt = -1.0;
        double SpeedBefore = 0.0;
        double SpeedAfter = 0.0;
        for (double T = 0.0; T < 90.0; T += Dt)
        {
            const double Before = Flight.State.GetSpeed();
            Flight.State.Step(Dt);
            const EFlightMode Now = Flight.State.GetMode();
            if (Now != Was)
            {
                ++Changes;
                HandedAt = Flight.OverFloor();
                SpeedBefore = Before;
                SpeedAfter = Flight.State.GetSpeed();
            }
            Was = Now;
        }
        AddInfo(FString::Printf(TEXT("started %.0f m under; handed over %.0f m above the floor"), Below / 100.0, HandedAt / 100.0));
        TestEqual(TEXT("one change of mode, DriveBelowFloor to Drive"), Changes, 1);
        TestTrue(TEXT("the drive flies it now"), Was == EFlightMode::Drive);
        TestTrue(TEXT("handed over 500 m above the floor, not before"), HandedAt >= ShipFlight::DefaultDriveHandbackCm);
        TestTrue(TEXT("from what the ship was doing: no jump in speed at the handover"),
                 SpeedAfter <= SpeedBefore + ShipDriveLever::NotchSpeed(1) * 0.1);
    }

    // 600 m over the floor with the nose 10 degrees below the horizon: the
    // drive takes the ship and brings it to AT THE FLOOR, never under, and
    // never hands it back.
    {
        FFlight Flight(0.0, FQuat(FVector::RightVector, FMath::DegreesToRadians(10.0)));
        Flight.State.SetUniverseTransform(Flight.World.Centre + FVector(Flight.D) * (Flight.World.Radius + Flight.World.Floor + 6.0e4),
                                          Flight.State.GetUniverseOrientation());
        Flight.Engage(1, 0.0, 0.0);
        bool bNeverBelow = true;
        double Lowest = TNumericLimits<double>::Max();
        for (double T = 0.0; T < 120.0; T += Dt)
        {
            Flight.State.Step(Dt);
            bNeverBelow &= Flight.State.GetMode() == EFlightMode::Drive;
            Lowest = FMath::Min(Lowest, Flight.OverFloor());
        }
        TestTrue(TEXT("above the floor the drive flies, and keeps flying"), bNeverBelow);
        TestTrue(FString::Printf(TEXT("its cap keeps it on or over the floor (lowest %.2f m)"), Lowest / 100.0), Lowest >= -1.0);
        TestTrue(TEXT("and it comes to AT THE FLOOR"), Flight.State.GetHold() == EFlightHold::AtFloor);
    }
    return true;
}

#endif
```

- [ ] **Step 2: Run and see them fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh
```

Expected: fails to compile: `'DriveBelowFloor': is not a member of 'EFlightMode'`.

- [ ] **Step 3: The mode**

In `EFlightMode`, after `SpoolingDown`:

```cpp
    /**
     * F live under a solid world's drive floor (landing decision 10): the
     * drive does not take the ship. It flies cruise and the vertical lever,
     * Shift and Ctrl move cruise, the notch keeps its setting and the drive's
     * position is held at the ship's forward speed, so the drive takes over
     * from what the ship is doing -- once the ship is DriveHandbackCm over the
     * floor with the nose's ray clear of it within the drive's hold. F is
     * never refused.
     */
    DriveBelowFloor,
```

In `FShipFlightLimits`, after `RegimeCm`:

```cpp
    /** ds.Land.DriveHandback, cm: how far over a solid world's drive floor
     *  the drive takes the ship back. */
    double DriveHandbackCm = ShipFlight::DefaultDriveHandbackCm;
```

Private member: `bool bDriveBelowFloor = false;` and declaration `void UpdateDriveBelowFloor();`.

In `ShipFlightState.cpp`:

`SubStep`, after `UpdateRegime();`, replace the dispatch with:

```cpp
    UpdateDriveBelowFloor();
    if (bDriveBelowFloor)
    {
        // Held, never eased while hidden: when the drive takes over it starts
        // from what the ship is doing.
        DrivePosition = ShipDriveLever::PositionOf(FMath::Max(0.0, Velocity | Orientation.GetForwardVector()));
        CruiseSubStep(FixedDelta);
        return;
    }
    if ((Command.bDrive || bSpoolingDown) && DriveSubStep(FixedDelta))
    {
        return;
    }
    CruiseSubStep(FixedDelta);
```

Append:

```cpp
void FShipFlightState::UpdateDriveBelowFloor()
{
    if (!Command.bDrive)
    {
        bDriveBelowFloor = false;
        return;
    }
    const FVector Nose = Orientation.GetForwardVector();
    const double Speed = ShipDriveLever::SpeedAt(DrivePosition);
    const double Braking = 2.0 * ShipFlight::BrakingMargin * FMath::Max(Limits.LinearAcceleration, 1.0);
    const double Hold = FMath::Max(Speed * FMath::Max(Limits.HoldSeconds, 0.0), Speed * Speed / Braking);
    bool bUnder = false;
    bool bKeep = false;
    for (const FFlightSurface& Surface : Surfaces)
    {
        if (!Surface.HasGround())
        {
            continue;
        }
        const double Clear = ShipFlight::FloorClearance(Surface, Position);
        bUnder |= Clear < 0.0;
        if (Clear < Limits.DriveHandbackCm)
        {
            bKeep = true;
            continue;
        }
        const TOptional<double> D = ShipFlight::RayToFloor(Surface, Position, Nose);
        bKeep |= D.IsSet() && *D <= Hold;
    }
    bDriveBelowFloor = bDriveBelowFloor ? bKeep : bUnder;
}
```

In `SetCommand`, capture `const bool bWasBelowFloor = bDriveBelowFloor;` beside `bWasDrive` at
the top, and in the leaving branch (`else if (bWasDrive && !Command.bDrive)`) replace
`bSpoolingDown = !CruiseCanTakeOver();` with:

```cpp
        // From DriveBelowFloor cruise was already flying the ship: there is
        // nothing to spool down from. (CruiseCanTakeOver reads the floor
        // sphere, which a ship under it meets at once; asked here it would
        // start a spool that the drive's cap holds at a dead stop.)
        bSpoolingDown = !bWasBelowFloor && !CruiseCanTakeOver();
        bDriveBelowFloor = false;
```

In `JumpTo`, add `bDriveBelowFloor = false;`.

Replace `GetMode`, `GetLeverSpeed`, `GetOtherLeverSpeed`, `IsVerticalLive`:

```cpp
EFlightMode FShipFlightState::GetMode() const
{
    if (Command.bDrive)
    {
        return bDriveBelowFloor ? EFlightMode::DriveBelowFloor : EFlightMode::Drive;
    }
    return bSpoolingDown ? EFlightMode::SpoolingDown : EFlightMode::Cruise;
}

double FShipFlightState::GetLeverSpeed() const
{
    return GetMode() == EFlightMode::Drive ? ShipDriveLever::NotchSpeed(Command.DriveNotch) : CruiseLeverSpeed();
}

double FShipFlightState::GetOtherLeverSpeed() const
{
    return GetMode() == EFlightMode::Drive ? CruiseLeverSpeed() : ShipDriveLever::NotchSpeed(Command.DriveNotch);
}

bool FShipFlightState::IsVerticalLive() const
{
    const EFlightMode Mode = GetMode();
    return RegimeWeight > 0.0 && (Mode == EFlightMode::Cruise || Mode == EFlightMode::DriveBelowFloor);
}
```

- [ ] **Step 4: Run: PASS, and the drive's own tests**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh && \
./test.sh DeepSpace.Ship.Landing && ./test.sh DeepSpace.Ship.Flight && ./test.sh DeepSpace.Ship.Drive
```

Expected: all green. (No existing drive fixture has a ground, so `DriveBelowFloor` never arises
there.)

- [ ] **Step 5: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
git add Source/DeepSpace/Ship/ShipFlightState.h Source/DeepSpace/Ship/ShipFlightState.cpp Source/DeepSpace/Tests/ShipLandingDriveTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(flight): DriveBelowFloor -- F under a solid world's drive floor flies cruise; the drive takes over above

A drive engaged under the floor would fly tangentially at 0.1 c through
mountains RayToFloor never sees from inside the sphere. Now the ship
flies cruise and the vertical lever, the notch keeps its setting, the
drive's position is held at the ship's forward speed, and the drive
takes over 500 m over the floor with the nose clear; its own cap keeps
it there, so the modes cannot alternate (landing decision 10, sign-off 4).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Prove them**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'bDriveBelowFloor = bDriveBelowFloor ? bKeep : bUnder;' 'bDriveBelowFloor = false;' DeepSpace.Ship.Landing.DriveUnderFloor && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'if (Clear < Limits.DriveHandbackCm)' 'if (Clear < 0.0)' DeepSpace.Ship.Landing.DriveTakesOverAbove && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'bSpoolingDown = !bWasBelowFloor && !CruiseCanTakeOver();' 'bSpoolingDown = !CruiseCanTakeOver();' DeepSpace.Ship.Landing.DriveUnderFloor && \
./build.sh
```

Expected: `KILLED` three times.

---

## Task 22 (F8): the invariant -- the ground always catches -- and the ray marched once a frame

**Owner:** F. **Depends on:** F7. Spec decision 10 (*The invariant*, *Cost*), decision 11
(attitude near the ground).

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipFlightState.h` (private: the ray cache), `.cpp` (`Step`,
  `NearestOnCruisePath`, `GroundAhead`, `JumpTo`, `SetSurfaces`)
- Create: `Source/DeepSpace/Tests/ShipGroundCatchesTest.cpp`
  (`DeepSpace.Ship.Landing.GroundAlwaysCatchesHigh`, `.GroundAlwaysCatchesLow`,
  `.GroundAlwaysCatchesRough`, `.GroundAlwaysCatchesRealRelief`)

The spec's one grid is split over four siblings by start, so each stays near the spec's 30 s
budget; together they are the full product (cruise 6 x vertical 5 x start 8 x chop 5 x thrust 2 x
sink 2 = 4,800 flights), and the real-relief pass covers every cruise-by-vertical pair on
Baemsekai IV and III with the other factors cycled.

**Interfaces:** Consumes F5-F7. Produces nothing new outward; the cache is private.

- [ ] **Step 1: Write the failing tests**

Create `Source/DeepSpace/Tests/ShipGroundCatchesTest.cpp`:

```cpp
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipGravity.h"
#include "Ship/ShipLanding.h"
#include "Ship/ShipVerticalLever.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * DeepSpace.Ship.Landing.GroundAlwaysCatches (decision 10's invariant), in
 * four siblings by where the flights start:
 *   - no footprint point ends a substep more than 1 cm under the ground, at
 *     every frame chop;
 *   - contacts reached by the levers are at or under TouchdownSpeed, along
 *     the ground's normal under the lowest point, the substep it first comes
 *     within 1 cm;
 *   - in the lever sweep the hard stop never fires: the caps foresee every
 *     contact. (Attitude near the ground, in .Rough, may lift; it may never
 *     leave a point under.)
 * The fixture ground is two crossed sines with the real relief's MaxSlope;
 * the real-relief pass flies Baemsekai IV and III.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundCatchesHighTest, "DeepSpace.Ship.Landing.GroundAlwaysCatchesHigh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundCatchesLowTest, "DeepSpace.Ship.Landing.GroundAlwaysCatchesLow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundCatchesRoughTest, "DeepSpace.Ship.Landing.GroundAlwaysCatchesRough",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundCatchesRealTest, "DeepSpace.Ship.Landing.GroundAlwaysCatchesRealRelief",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GroundCatchesLocal
{
    using namespace GroundFixtures;

    const TArray<double> CruiseSpeeds = { 0.0, 1.0e2, 2.0e4, 2.0e5, 2.0e6, -2.0e4 };
    const TArray<double> VerticalRates = { 0.0, -50.0, -500.0, -2.0e4, 2.0e4 };
    const TArray<double> Chops = { 1.0 / 240.0, 1.0 / 60.0, 1.0 / 20.0, 0.5, 2.0 };
    const TArray<double> Thrusts = { 1.0, 0.25 };
    const TArray<double> Sinks = { 0.0, 200.0 };

    double ThrottleFor(double Speed, const FShipFlightLimits& Limits)
    {
        if (Speed == 0.0)
        {
            return 0.0;
        }
        const double Lever = FMath::Loge(FMath::Max(FMath::Abs(Speed), ShipDriveLever::CruiseFloorCmPerSecond) / ShipDriveLever::CruiseFloorCmPerSecond)
                           / FMath::Loge(Limits.MaxSpeed / ShipDriveLever::CruiseFloorCmPerSecond);
        return Speed > 0.0 ? FMath::Max(Lever, 1.0e-9) : -FMath::Max(Lever, 1.0e-9);
    }

    struct FStart
    {
        const TCHAR* Name;
        FVector3d D;
        double Clearance;      // footprint clearance, cm; negative: at the drive floor sphere
    };

    struct FTally
    {
        int32 Flights = 0;
        double LeastClearance = TNumericLimits<double>::Max();
        double WorstContact = 0.0;
        int32 HardStops = 0;
        FString WorstCase;
    };

    /** Places the ship so its footprint clears the ground by Clearance: two
     *  passes, since tilting ground moves which point is lowest. */
    void PlaceOver(FShipFlightState& Flight, const FFlightSurface& World, const FStart& Start)
    {
        const FQuat Turn = Level(Start.D);
        if (Start.Clearance < 0.0)
        {
            Flight.SetUniverseTransform(World.Centre + FVector(Start.D) * (World.Radius + World.Floor), Turn);
            return;
        }
        double Agl = Start.Clearance + ShipLanding::DefaultGearClearanceCm;
        for (int32 Pass = 0; Pass < 3; ++Pass)
        {
            Flight.SetUniverseTransform(Above(World, Start.D, Agl), Turn);
            Agl += Start.Clearance - ShipLanding::FootprintClearance(World, Flight.GetUniversePosition(), Turn,
                                                                     ShipLanding::DefaultGearClearanceCm).Least;
        }
    }

    void Fly(FTally& Tally, const FFlightSurface& World, double G, const FStart& Start, double Cruise, double Vertical,
             double Chop, double Thrust, double Sink, double Budget)
    {
        FShipFlightState Flight;
        FShipFlightLimits Limits = Flight.GetLimits();
        Limits.LinearAcceleration = FShipFlightLimits().LinearAcceleration * Thrust;
        Limits.DriveThrust = Thrust;
        Limits.SinkBias = Sink;
        Flight.SetLimits(Limits);
        Flight.SetSurfaces({ World });
        Flight.SetWells({ { World.Centre, G * ShipFlight::StandardGravityCmS2 * World.Radius * World.Radius, World.Radius } });
        PlaceOver(Flight, World, Start);
        FShipFlightCommand Command;
        Command.Throttle = ThrottleFor(Cruise, Limits);
        Command.Vertical = ShipVerticalLever::LeverOf(Vertical, Limits.VerticalTop);
        Flight.SetCommand(Command);
        Flight.ResetGroundLog();
        double RestFor = 0.0;
        for (double T = 0.0; T < Budget && RestFor < 1.0; T += Chop)
        {
            Flight.Step(Chop);
            RestFor = Flight.GetSpeed() < 1.0 ? RestFor + Chop : 0.0;
        }
        const FGroundLog& Log = Flight.GetGroundLog();
        ++Tally.Flights;
        Tally.HardStops += Log.HardStops;
        if (Log.LeastClearance < Tally.LeastClearance || Log.WorstContactSpeed > Tally.WorstContact || Log.HardStops > 0)
        {
            Tally.WorstCase = FString::Printf(TEXT("%s cruise %.0f vertical %.0f chop %.4f thrust %.2f sink %.0f"),
                                              Start.Name, Cruise, Vertical, Chop, Thrust, Sink);
        }
        Tally.LeastClearance = FMath::Min(Tally.LeastClearance, Log.LeastClearance);
        Tally.WorstContact = FMath::Max(Tally.WorstContact, Log.WorstContactSpeed);
    }

    /** The fixture ground: crossed sines with the real relief's slope bound. */
    FFlightSurface Fixture()
    {
        const FGroundFieldRef Real = ShipGround::FromRelief(FixtureParams());
        const FGroundFieldRef Sines = MakeShared<FCrossedSines, ESPMode::ThreadSafe>(
            FCrossedSines::WithSlope(Real->RadiusCm(), 2.0e5, Real->MaxSlope()));
        return SurfaceOver(Sines, 1.02e6 + Sines->MaxHeightCm());
    }

    /** Directions on the fixture: flat-ish (a trough-to-crest midpoint), a
     *  15-degree slope, a peak (a crater rim's crest, in this field), and a
     *  ridge line. The field's arc coordinates are R D.x and R D.y. */
    FVector3d AtArc(const FFlightSurface& World, double X, double Y)
    {
        return FVector3d(X / World.Radius, Y / World.Radius, 1.0).GetSafeNormal();
    }

    void Sweep(FTally& Tally, const FFlightSurface& World, TConstArrayView<FStart> Starts, double Budget)
    {
        for (const FStart& Start : Starts)
            for (const double Cruise : CruiseSpeeds)
                for (const double Vertical : VerticalRates)
                    for (const double Chop : Chops)
                        for (const double Thrust : Thrusts)
                            for (const double Sink : Sinks)
                            {
                                Fly(Tally, World, 0.84, Start, Cruise, Vertical, Chop, Thrust, Sink, Budget);
                            }
    }

    bool Report(FAutomationTestBase& Test, const FTally& Tally, double Seconds, bool bLeverSweep)
    {
        Test.AddInfo(FString::Printf(TEXT("%d flights in %.1f s: least clearance %.3f cm, worst contact %.3f cm/s, %d hard stops; worst: %s"),
                                     Tally.Flights, Seconds, Tally.LeastClearance, Tally.WorstContact, Tally.HardStops, *Tally.WorstCase));
        Test.TestTrue(TEXT("no footprint point ends a substep more than 1 cm under the ground"), Tally.LeastClearance >= -1.0);
        Test.TestTrue(TEXT("every contact the levers reach is at or under the touchdown speed"),
                      Tally.WorstContact <= ShipFlight::DefaultTouchdownSpeed * (1.0 + 1e-6));
        if (bLeverSweep)
        {
            Test.TestEqual(TEXT("and the hard stop never fires: the caps foresee every contact"), Tally.HardStops, 0);
        }
        return true;
    }
}

bool FGroundCatchesHighTest::RunTest(const FString& Parameters)
{
    using namespace GroundCatchesLocal;
    const FFlightSurface World = Fixture();
    const FVector3d D = AtArc(World, 3.1e4, -1.7e4);
    const FStart Starts[] = { { TEXT("drive floor"), D, -1.0 }, { TEXT("5 km"), D, 5.0e5 }, { TEXT("500 m"), D, 5.0e4 } };
    FTally Tally;
    const double Start = FPlatformTime::Seconds();
    Sweep(Tally, World, Starts, 150.0);
    return Report(*this, Tally, FPlatformTime::Seconds() - Start, true);
}

bool FGroundCatchesLowTest::RunTest(const FString& Parameters)
{
    using namespace GroundCatchesLocal;
    const FFlightSurface World = Fixture();
    const FVector3d D = AtArc(World, 3.1e4, -1.7e4);
    const FStart Starts[] = { { TEXT("20 m"), D, 2.0e3 }, { TEXT("2 m"), D, 2.0e2 } };
    FTally Tally;
    const double Start = FPlatformTime::Seconds();
    Sweep(Tally, World, Starts, 150.0);
    return Report(*this, Tally, FPlatformTime::Seconds() - Start, true);
}

bool FGroundCatchesRoughTest::RunTest(const FString& Parameters)
{
    using namespace GroundCatchesLocal;
    const FFlightSurface World = Fixture();
    const double Wave = 2.0e5;
    const double K = 2.0 * UE_DOUBLE_PI / Wave;
    // Slope: the y crest (cos = 0) and x where the x slope alone is 15 degrees.
    const double Amplitude = World.Ground->MaxHeightCm();
    const double SlopeX = FMath::Acos(FMath::Clamp(FMath::Tan(FMath::DegreesToRadians(15.0)) / (0.5 * Amplitude * K), -1.0, 1.0)) / K;
    const FStart Starts[] = {
        { TEXT("2 m over a 15-degree slope"), AtArc(World, SlopeX, 0.25 * Wave), 2.0e2 },
        { TEXT("2 m over a crest (the rim)"), AtArc(World, 0.25 * Wave, 0.25 * Wave), 2.0e2 },
        { TEXT("2 m over a ridge"), AtArc(World, 0.25 * Wave, 0.0), 2.0e2 } };
    FTally Tally;
    const double Start = FPlatformTime::Seconds();
    Sweep(Tally, World, Starts, 150.0);
    Report(*this, Tally, FPlatformTime::Seconds() - Start, true);

    // Attitude inputs held at hover heights of 0-8 m, on every footprint
    // point: the hard stop may lift; no point may end a substep under.
    FTally Turning;
    for (const FStart& Where : Starts)
    {
        for (const double Clear : { 0.0, 2.0e2, 4.0e2, 8.0e2 })
        {
            for (const FVector& Attitude : { FVector(0, 1, 0), FVector(0, -1, 0), FVector(1, 0, 0), FVector(-1, 0, 0) })
            {
                FShipFlightState Flight;
                Flight.SetSurfaces({ World });
                PlaceOver(Flight, World, { Where.Name, Where.D, Clear });
                FShipFlightCommand Command;
                Command.AttitudeRate = Attitude;
                Flight.SetCommand(Command);
                Flight.ResetGroundLog();
                for (double T = 0.0; T < 5.0; T += 1.0 / 60.0)
                {
                    Flight.Step(1.0 / 60.0);
                }
                ++Turning.Flights;
                Turning.LeastClearance = FMath::Min(Turning.LeastClearance, Flight.GetGroundLog().LeastClearance);
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d turning hovers: least clearance %.3f cm"), Turning.Flights, Turning.LeastClearance));
    TestTrue(TEXT("turning near the ground, the ship levers itself up on its gear, never through it"), Turning.LeastClearance >= -1.0);
    return true;
}

bool FGroundCatchesRealTest::RunTest(const FString& Parameters)
{
    using namespace GroundCatchesLocal;
    FTally Tally;
    const double Start = FPlatformTime::Seconds();
    for (const int32 Orbit : { 4, 3 })
    {
        const TOptional<FHomeWorld> Home = HomeWorld(Orbit);
        const TCHAR* Want = Orbit == 4 ? TEXT("Baemsekai IV") : TEXT("Baemsekai III");
        if (!TestTrue(FString::Printf(TEXT("home's orbit %d is %s, solid"), Orbit, Want), Home.IsSet() && Home->Name == Want))
        {
            return false;
        }
        const FFlightSurface World = SurfaceOver(Home->Ground, 1.02e6 + Home->Ground->MaxHeightCm());
        const double G = Home->Body.GravParam / FMath::Square(World.Radius) / ShipFlight::StandardGravityCmS2;
        int32 Case = 0;
        for (int32 C = 0; C < CruiseSpeeds.Num(); ++C)
        {
            for (int32 V = 0; V < VerticalRates.Num(); ++V, ++Case)
            {
                const FVector3d D = FVector3d(FMath::Sin(Case * 0.7), FMath::Cos(Case * 1.3), 0.5 + 0.1 * (Case % 5)).GetSafeNormal();
                const double Clears[] = { 5.0e4, 2.0e3, 2.0e2 };
                const FStart Where{ TEXT("real"), D, Clears[Case % 3] };
                Fly(Tally, World, G, Where, CruiseSpeeds[C], VerticalRates[V], Chops[Case % Chops.Num()],
                    Thrusts[(Case / 2) % 2], Sinks[Case % 2], 60.0);
            }
        }
    }
    return Report(*this, Tally, FPlatformTime::Seconds() - Start, true);
}

#endif
```

- [ ] **Step 2: Run them before the cache, and read the budget**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh && \
./test.sh DeepSpace.Ship.Landing.GroundAlwaysCatches; grep "flights in" Saved/Logs/DeepSpace.log | tail -4
```

Expected: the four tests run. They may already pass -- F5-F7 built the laws -- and that is the
point of the invariant; if any fails, the failing message and its `worst:` case name the flight,
and the fix is in the caps (F6), never in the test. What must change in this task is the time:
the `flights in N s` lines show each sibling over the spec's 30 s, because every substep marches
the rays afresh.

- [ ] **Step 3: March once a frame**

In `ShipFlightState.h`'s private block:

```cpp
    /**
     * The ground rays, marched once a frame per direction (decision 10's
     * cost): each later substep of the frame takes the distance flown along
     * the ray off the proven-clear distance, and marches again only if the
     * direction turned by more than a degree. Slot 0 is cruise's path along
     * the nose, slot 1 the horizontal heading in the regime.
     */
    struct FGroundRayCache
    {
        int32 Slot = INDEX_NONE;
        int32 Surface = INDEX_NONE;
        FVector Direction = FVector::ZeroVector;
        FUniversePosition From;
        TOptional<double> Hit;
        double SeenTo = 0.0;
        int32 Frame = -1;
    };
    TArray<FGroundRayCache, TInlineAllocator<4>> RayCache;
    int32 FrameCount = 0;

    TOptional<double> CachedRay(int32 Slot, int32 SurfaceIndex, const FVector& Direction, double Clearance, double Lookahead);
```

In `ShipFlightState.cpp`: at the top of `Step`, after the early return, `++FrameCount;`. In
`SetSurfaces` and `JumpTo`, `RayCache.Reset();`. Append:

```cpp
TOptional<double> FShipFlightState::CachedRay(int32 Slot, int32 SurfaceIndex, const FVector& Direction, double Clearance, double Lookahead)
{
    const FVector U = Direction.GetSafeNormal();
    FGroundRayCache* Cache = RayCache.FindByPredicate([&](const FGroundRayCache& Entry)
    {
        return Entry.Slot == Slot && Entry.Surface == SurfaceIndex;
    });
    if (Cache && Cache->Frame == FrameCount && (Cache->Direction | U) >= FMath::Cos(FMath::DegreesToRadians(1.0)))
    {
        const double Flown = FMath::Max(0.0, (Position - Cache->From) | Cache->Direction);
        if (Cache->Hit)
        {
            return FMath::Max(0.0, *Cache->Hit - Flown);
        }
        if (Cache->SeenTo - Flown >= Lookahead)
        {
            return {};
        }
    }
    if (!Cache)
    {
        Cache = &RayCache.AddDefaulted_GetRef();
        Cache->Slot = Slot;
        Cache->Surface = SurfaceIndex;
    }
    Cache->Direction = U;
    Cache->From = Position;
    Cache->Frame = FrameCount;
    Cache->SeenTo = Lookahead;
    Cache->Hit = ShipFlight::RayToGround(Surfaces[SurfaceIndex], Position, U, Clearance, Lookahead);
    return Cache->Hit;
}
```

In `NearestOnCruisePath`, replace the `ShipFlight::RayToGround(Surface, Position, Direction, ...)`
call with `CachedRay(0, Index, Direction, Limits.GearClearanceCm, Lookahead)`; in `GroundAhead`,
replace its `ShipFlight::RayToGround(...)` with
`CachedRay(1, SurfaceIndex, Heading, Limits.GearClearanceCm, Lookahead)`.

- [ ] **Step 4: Run: PASS within the budget**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && ./build.sh && \
./test.sh DeepSpace.Ship.Landing; grep "flights in" Saved/Logs/DeepSpace.log | tail -4
```

Expected: every `DeepSpace.Ship.Landing.*` green; each `flights in N s` under about 30 s. If a
sibling is still over, record its time in the commit message: the spec's 30 s is a budget to
report against, and the grid is not cut to meet it.

- [ ] **Step 5: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
git add Source/DeepSpace/Ship/ShipFlightState.h Source/DeepSpace/Ship/ShipFlightState.cpp Source/DeepSpace/Tests/ShipGroundCatchesTest.cpp && \
git commit -m "$(cat <<'EOF'
test(flight): the ground always catches -- decision 10's full sweep; rays marched once a frame

4,800 lever flights over crossed sines with the real slope bound, and
every cruise-by-vertical pair on Baemsekai IV and III: no point more than
1 cm under, contact at or under 0.5 m/s, the hard stop never needed;
attitude near the ground levers the ship up on its gear. The ground rays
are marched once a frame and shortened by what was flown along them.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Prove them**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-f && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'Limits.ApproachSeconds, Limits.TouchdownSpeed, FixedStep);
        if (Down > May)' 'Limits.ApproachSeconds, 4.0 * Limits.TouchdownSpeed, FixedStep);
        if (Down > May)' DeepSpace.Ship.Landing.GroundAlwaysCatchesLow && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'return FMath::Max(0.0, *Cache->Hit - Flown);' 'return *Cache->Hit;' DeepSpace.Ship.Landing.GroundAlwaysCatchesHigh && \
./build.sh
```

Expected: `KILLED` twice: a descent cap that allows 2 m/s at contact breaks the contact speed;
a cached hit that is never shortened lets a skimming ship reach a ridge the cap thought farther
(the hard stop fires). If the second survives, the lookahead margin hides it: repeat it on
`.GroundAlwaysCatchesRough`, and if it still survives, tighten `NearestOnCruisePath`'s `1.5 x`
lookahead factor to `1.1` and re-run, recording which.

---

## Task 23 (S1): the drive floor over the peaks, the grounds and wells handed to the flight, the landing CVars

**Owner:** S. **Depends on:** F2 (merged into `feat/landing-b`, then `git merge feat/landing-b`
into `feat/landing-b-s`), F6's limits (for the CVar wiring; F1-F7 merged first is simplest).
Spec decisions 4, 10; the CVars table.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` -- `FloorFor`'s comment (lines 210-222), public
  `static double GearClearance();`, private `GroundFor`, `GroundCache`, `FWorldFix` (lines 461-467)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.cpp` -- the CVar block (lines 17-130), `FloorFor`
  (lines 547-557), `UpdateSurfaces` (lines 564-602), `ApplyAllocation` (the limits, lines 471-515),
  `FixWorld` (lines 746-777)
- Create: `Source/DeepSpace/Tests/ShipFloorsOverPeaksTest.cpp` (`DeepSpace.Ship.FloorOverPeaks`)
- Modify: `Source/DeepSpace/Tests/SliceLoopTest.cpp` (the floor assertion, lines 472-473; an include)
- Modify: `Source/DeepSpace/Tests/PlaytestTest.cpp` (`EtaCountsDown`: its cruise leg, lines 1185-1254,
  moves to Task 29 (S7))
- Modify: `Source/DeepSpace/Tests/ShipDriveTest.cpp` (the room's least, lines 193-202; an include)
- Modify: `Source/DeepSpace/Tests/ShipTargetTest.cpp` (the drive's arrival at the floor, lines 255-270)
- Modify: `CLAUDE.md` -- *Where each tunable lives* (the table, about line 913), *The drive and
  the jump*'s floor paragraph (about line 575)

**Interfaces:**
- Consumes: `FSkyBody::{Ground, Relief, GravParam}`; `ShipGround::FromRelief`; `FGravityWell`;
  every `FShipFlightLimits` landing field; `SkyProjection::RenderedFloor`.
- Produces: `FloorFor` over solid worlds = `max(ds.Flight.Floor, RenderedFloor(R)) + MaxHeightCm`;
  `static double UShipSubsystem::GearClearance()`; surfaces with `bWorld` and `Ground`; wells;
  CVars `ds.Land.GearClearance` (cm), `.TouchdownSpeed` (m/s), `.ApproachSeconds`, `.SkimSeconds`,
  `.SkimFloor` (m/s), `.Regime` (km), `.DriveHandback` (m), `ds.Vertical.Top` (m/s), `.Sweep`,
  `.HeavyFloor`, `ds.Boosters.HoldWatts`, `.StarvedSink` (m/s).

- [ ] **Step 1: Write the failing test**

Create `Source/DeepSpace/Tests/ShipFloorsOverPeaksTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkySystem.h"
#include "Surface/WorldRelief.h"
#include "Tests/SkyTestWorld.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 10: over a solid world the drive's floor is 10 km (or the
 * sky's rendered floor) above the world's highest peak, so no summit is ever
 * within 10 km of the drive; oceans, giants and stars are unchanged. And the
 * subsystem hands the flight both floors and every body's pull once a
 * frame, with the landing tunables read at use.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipFloorOverPeaksTest, "DeepSpace.Ship.FloorOverPeaks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipFloorOverPeaksTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    constexpr double Km = UniverseUnits::CmPerKm;
    const FSkyViewParams View;

    FSkyBody Earth;
    Earth.Kind = ESkyBodyKind::Planet;
    Earth.Radius = UniverseUnits::CmPerEarthRadius;
    FSkyBody Rock = Earth;
    Rock.Ground = EGround::Solid;
    Rock.Relief.RadiusCm = Earth.Radius;
    Rock.Relief.PeakCm = 8.0 * Km;
    Rock.Relief.Cratering = 1.0;
    Rock.Relief.Ground = EGround::Solid;
    const double Peak = FWorldRelief(Rock.Relief).MaxHeightCm();
    TestEqual(TEXT("an ocean's floor is the sky's, as before"), UShipSubsystem::FloorFor(Earth), SkyProjection::RenderedFloor(Earth.Radius, View));
    TestEqual(TEXT("a solid world's is that above its highest peak"), UShipSubsystem::FloorFor(Rock),
              SkyProjection::RenderedFloor(Earth.Radius, View) + Peak);
    TestTrue(FString::Printf(TEXT("so an 8 km peak puts it at 18.2 km (%.2f)"), UShipSubsystem::FloorFor(Rock) / Km),
             FMath::IsNearlyEqual(UShipSubsystem::FloorFor(Rock) / Km, 18.2, 0.05));
    {
        FScopedCVar Floor(TEXT("ds.Flight.Floor"), 20.0f);
        TestEqual(TEXT("ds.Flight.Floor 20 moves it with the ocean's"), UShipSubsystem::FloorFor(Rock), 20.0 * Km + Peak);
    }

    FSkyWorld Test(TEXT("FloorOverPeaksWorld"));
    Test.BeginPlay();
    Test.Step(1.0f / 60.0f);
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FShipFlightState& Flight = Test.Ship->GetFlightState();
    TestEqual(TEXT("one surface per body and the edge"), Flight.GetSurfaces().Num(), Here.Bodies.Num() + 1);
    TestEqual(TEXT("one well per body"), Flight.GetWells().Num(), Here.Bodies.Num());
    int32 Solid = 0;
    for (int32 Index = 0; Index < Here.Bodies.Num(); ++Index)
    {
        const FSkyBody& Body = Here.Bodies[Index];
        const FFlightSurface& Surface = Flight.GetSurfaces()[Index];
        const bool bSolid = Body.Ground == EGround::Solid;
        Solid += bSolid ? 1 : 0;
        TestEqual(FString::Printf(TEXT("%s has a ground exactly when it is solid"), *Body.Id.ToString()), Surface.HasGround(), bSolid);
        TestEqual(FString::Printf(TEXT("%s is a world unless it is the star"), *Body.Id.ToString()), Surface.bWorld, Body.Kind != ESkyBodyKind::Star);
        TestEqual(FString::Printf(TEXT("%s pulls with its GM"), *Body.Id.ToString()), Flight.GetWells()[Index].Mu, Body.GravParam);
        if (bSolid)
        {
            TestTrue(FString::Printf(TEXT("%s's drive floor is at most 20.2 km over its datum"), *Body.Id.ToString()),
                     Surface.Floor <= FMath::Max(10.0 * Km, SkyProjection::RenderedFloor(Body.Radius, View)) + 10.0 * Km + 1.0);
        }
    }
    TestTrue(TEXT("home has solid worlds to land on"), Solid > 0);
    TestTrue(TEXT("and the ship feels the pull of home"), Flight.GetLocalGravity().Size() > 0.0);

    {
        FScopedCVar Gear(TEXT("ds.Land.GearClearance"), 200.0f);
        FScopedCVar Regime(TEXT("ds.Land.Regime"), 30.0f);
        FScopedCVar Top(TEXT("ds.Vertical.Top"), 100.0f);
        FScopedCVar Touch(TEXT("ds.Land.TouchdownSpeed"), 1.0f);
        FScopedCVar Approach(TEXT("ds.Land.ApproachSeconds"), 0.0f);
        FScopedCVar Skim(TEXT("ds.Land.SkimSeconds"), 5.0f);
        FScopedCVar SkimFloor(TEXT("ds.Land.SkimFloor"), 10.0f);
        FScopedCVar Handback(TEXT("ds.Land.DriveHandback"), 250.0f);
        FScopedCVar Heavy(TEXT("ds.Vertical.HeavyFloor"), 0.5f);
        Test.Step(1.0f / 60.0f);
        const FShipFlightLimits& Limits = Flight.GetLimits();
        TestEqual(TEXT("ds.Land.GearClearance reaches the flight, cm"), Limits.GearClearanceCm, 200.0);
        TestEqual(TEXT("ds.Land.Regime, km"), Limits.RegimeCm, 30.0 * Km);
        TestEqual(TEXT("ds.Vertical.Top, m/s"), Limits.VerticalTop, 1.0e4);
        TestEqual(TEXT("ds.Land.TouchdownSpeed, m/s"), Limits.TouchdownSpeed, 100.0);
        TestEqual(TEXT("ds.Land.ApproachSeconds 0 is clamped to half a second"), Limits.ApproachSeconds, ShipFlight::MinApproachSeconds);
        TestEqual(TEXT("ds.Land.SkimSeconds"), Limits.SkimSeconds, 5.0);
        TestEqual(TEXT("ds.Land.SkimFloor, m/s"), Limits.SkimFloor, 1.0e3);
        TestEqual(TEXT("ds.Land.DriveHandback, m"), Limits.DriveHandbackCm, 2.5e4);
        TestEqual(TEXT("ds.Vertical.HeavyFloor"), Limits.VerticalHeavyFloor, 0.5);
        TestEqual(TEXT("and GearClearance() asks the same"), UShipSubsystem::GearClearance(), 200.0);
    }
    return true;
}

#endif
```

- [ ] **Step 2: Run and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && git merge -q feat/landing-b && ./build.sh
```

Expected: fails to compile: `'GearClearance': is not a member of 'UShipSubsystem'`.

- [ ] **Step 3: The CVars**

At the end of the anonymous namespace's CVar list in `ShipSubsystem.cpp` (after `CVarCruiseSweep`),
add (and `#include "Ship/ShipLanding.h"`, `"Ship/ShipVerticalLever.h"`, `"Surface/GroundField.h"`,
`"Surface/WorldRelief.h"`):

```cpp
    // Landing (spec 2026-09-27). Each default is the pure layer's constant.

    TAutoConsoleVariable<float> CVarGearClearance(
        TEXT("ds.Land.GearClearance"), static_cast<float>(ShipLanding::DefaultGearClearanceCm),
        TEXT("The ship's origin over flat ground at rest, cm: 10 cm of slab and the gear's feet under it."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarTouchdownSpeed(
        TEXT("ds.Land.TouchdownSpeed"), static_cast<float>(ShipFlight::DefaultTouchdownSpeed / 100.0),
        TEXT("Contact speed, m/s: the ground approach's floor."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarApproachSeconds(
        TEXT("ds.Land.ApproachSeconds"), static_cast<float>(ShipFlight::DefaultApproachSeconds),
        TEXT("The ground approach's ease, seconds: the last hundreds of metres fall by e every this many. Clamped to at least 0.5."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarSkimSeconds(
        TEXT("ds.Land.SkimSeconds"), static_cast<float>(ShipFlight::DefaultSkimSeconds),
        TEXT("The skim cap: near a world, horizontal speed is at most the height above the ground over this many seconds."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarSkimFloor(
        TEXT("ds.Land.SkimFloor"), static_cast<float>(ShipFlight::DefaultSkimFloor / 100.0),
        TEXT("The skim cap's floor, m/s: it never holds the ship slower than this."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarRegime(
        TEXT("ds.Land.Regime"), static_cast<float>(ShipFlight::DefaultRegimeCm / UniverseUnits::CmPerKm),
        TEXT("Within this many km of a world's cruise floor the vertical lever is live (leaving over 1.1 x it)."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarDriveHandback(
        TEXT("ds.Land.DriveHandback"), static_cast<float>(ShipFlight::DefaultDriveHandbackCm / 100.0),
        TEXT("How far over a solid world's drive floor, m, the drive takes the ship back from cruise."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarVerticalTop(
        TEXT("ds.Vertical.Top"), static_cast<float>(ShipVerticalLever::DefaultTopCmPerSecond / 100.0),
        TEXT("The vertical lever's top, m/s, either way."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarVerticalSweep(
        TEXT("ds.Vertical.Sweep"), static_cast<float>(ShipVerticalLever::DefaultSweep),
        TEXT("How fast Space and C held sweep the vertical lever, fraction of its travel a second."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarHeavyFloor(
        TEXT("ds.Vertical.HeavyFloor"), static_cast<float>(ShipVerticalLever::DefaultHeavyFloor),
        TEXT("On heavy worlds the climb top is Top x max(this, g_E / g): never below this fraction of it."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarHoldWatts(
        TEXT("ds.Boosters.HoldWatts"), ShipPower::DefaultHoldWattsPerG,
        TEXT("Watts per g the boosters want to hold the ship, only under a solid world's drive floor, airborne (capped at 3 g)."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarStarvedSink(
        TEXT("ds.Boosters.StarvedSink"), 2.0f,
        TEXT("m/s the ship sinks at when the hold gets nothing, only under a solid world's drive floor, never while climbing."),
        ECVF_Default);
```

(`ShipPower::DefaultHoldWattsPerG` arrives in Task S2; until then write `150.0f` here and S2
replaces it.)

- [ ] **Step 4: The floor, the grounds, the wells, the limits**

`ShipSubsystem.h`: replace `FloorFor`'s comment's last paragraph with:

```cpp
     * Over a solid world (landing decision 10) that floor is taken above the
     * world's highest peak, WorldRelief's MaxHeightCm: 10.2 km over a flat
     * Earth, up to about 20 km over one at the 10 km cap, so no summit is
     * ever within 10 km of the drive. It is the drive's floor only: cruise
     * and the vertical lever read the ground below it (FFlightSurface::Ground).
```

Add public `static double GearClearance();` beside `EdgeFloor`, and private:

```cpp
    /** A solid body's ground for the flight, shared and kept while its relief
     *  is unchanged, so a frame does not allocate one per body. */
    FGroundFieldRef GroundFor(const FSkyBody& Body);

    struct FGroundCacheEntry
    {
        FWorldReliefParams Params;
        FGroundFieldRef Ground;
    };
    TMap<FName, FGroundCacheEntry> GroundCache;
```

and in `FWorldFix` add `EGround Ground = EGround::None; FWorldReliefParams Relief;`.

`ShipSubsystem.cpp`:

```cpp
double UShipSubsystem::FloorFor(const FSkyBody& Body)
{
    if (Body.Kind == ESkyBodyKind::Star)
    {
        return FMath::Max(0.0f, CVarStarFloorRadii.GetValueOnGameThread()) * Body.Radius;
    }
    // The sky's floor is the one the picture is true to, so the flight's is
    // never under it; over solid ground it is taken above the highest peak.
    const double Above = FMath::Max(EdgeFloor(), SkyProjection::RenderedFloor(Body.Radius, FSkyViewParams()));
    return Body.Ground == EGround::Solid ? Above + FWorldRelief(Body.Relief).MaxHeightCm() : Above;
}

double UShipSubsystem::GearClearance()
{
    return FMath::Max(0.0f, CVarGearClearance.GetValueOnGameThread());
}

namespace
{
    bool SameRelief(const FWorldReliefParams& A, const FWorldReliefParams& B)
    {
        return A.SeedOffset == B.SeedOffset && A.RadiusCm == B.RadiusCm && A.PeakCm == B.PeakCm
            && A.Cratering == B.Cratering && A.Ground == B.Ground;
    }
}

FGroundFieldRef UShipSubsystem::GroundFor(const FSkyBody& Body)
{
    if (GroundCache.Num() > 64)
    {
        GroundCache.Reset();   // a few jumps' worth; the next frame refills what is near
    }
    FGroundCacheEntry* Entry = GroundCache.Find(Body.Id);
    if (!Entry || !SameRelief(Entry->Params, Body.Relief))
    {
        Entry = &GroundCache.Add(Body.Id, FGroundCacheEntry{ Body.Relief, ShipGround::FromRelief(Body.Relief) });
    }
    return Entry->Ground;
}
```

Replace `UpdateSurfaces` with:

```cpp
void UShipSubsystem::UpdateSurfaces()
{
    // Between stars there is nothing to be near, and nothing pulls.
    if (NavState.IsInTransit())
    {
        FlightState.SetSurfaces({});
        FlightState.SetWells({});
        return;
    }

    const FSkySystem Here = LocalSystem::Here(GetWorld());
    TArray<FFlightSurface> Surfaces;
    TArray<FGravityWell> Wells;
    Surfaces.Reserve(Here.Bodies.Num() + 1);
    Wells.Reserve(Here.Bodies.Num());
    for (const FSkyBody& Body : Here.Bodies)
    {
        FFlightSurface Surface;
        Surface.Centre = Body.Position;
        Surface.Radius = Body.Radius;
        Surface.Floor = FloorFor(Body);
        Surface.bWorld = Body.Kind != ESkyBodyKind::Star;
        if (Body.Ground == EGround::Solid)
        {
            Surface.Ground = GroundFor(Body);
        }
        Surfaces.Add(Surface);
        // Every body pulls, the star included (ruling 4).
        Wells.Add(FGravityWell{ Body.Position, Body.GravParam, Body.Radius });
    }

    const FSkyBody* Star = Here.Bodies.FindByPredicate([](const FSkyBody& Body) { return Body.Kind == ESkyBodyKind::Star; });
    if (Star && Here.EdgeRadius > 0.0)
    {
        FFlightSurface Edge;
        Edge.Centre = Star->Position;
        Edge.Radius = Here.EdgeRadius;
        Edge.Floor = EdgeFloor();
        Edge.bInsideOut = true;
        Surfaces.Add(Edge);
    }
    FlightState.SetSurfaces(MoveTemp(Surfaces));
    FlightState.SetWells(MoveTemp(Wells));
}
```

In `ApplyAllocation`, just before `FlightState.SetLimits(Limits);`:

```cpp
    // Landing's tunables, read at use and converted from the units they are
    // tuned in.
    Limits.GearClearanceCm = GearClearance();
    Limits.TouchdownSpeed = FMath::Max(0.0f, CVarTouchdownSpeed.GetValueOnGameThread()) * 100.0;
    Limits.ApproachSeconds = FMath::Max(static_cast<double>(CVarApproachSeconds.GetValueOnGameThread()), ShipFlight::MinApproachSeconds);
    Limits.SkimSeconds = FMath::Max(0.01f, CVarSkimSeconds.GetValueOnGameThread());
    Limits.SkimFloor = FMath::Max(0.0f, CVarSkimFloor.GetValueOnGameThread()) * 100.0;
    Limits.RegimeCm = FMath::Max(0.0f, CVarRegime.GetValueOnGameThread()) * UniverseUnits::CmPerKm;
    Limits.DriveHandbackCm = FMath::Max(0.0f, CVarDriveHandback.GetValueOnGameThread()) * 100.0;
    Limits.VerticalTop = FMath::Max(0.0f, CVarVerticalTop.GetValueOnGameThread()) * 100.0;
    Limits.VerticalHeavyFloor = FMath::Clamp(CVarHeavyFloor.GetValueOnGameThread(), 0.0f, 1.0f);
```

In `FixWorld(const FStarSystem&, const FBodyId&)`, after `Fix.Floor = ...`:

```cpp
    Fix.Ground = Sky.Bodies[Index].Ground;
    Fix.Relief = Sky.Bodies[Index].Relief;
```

and in its `Others` loop, after `Surface.Floor = ...`:
`Surface.bWorld = Sky.Bodies[Other].Kind != ESkyBodyKind::Star;` (the in-system jump's guard is
the drive's sphere; `Others` carries no ground, as the drive reads none).

- [ ] **Step 5: The tests that move, each named**

The spec's *Tests that pin today and will move* lists, for this task, `InSystemJumpTest`'s floor
numbers and the parts of `SliceLoopTest`, `PlaytestTest` and `TargetMarkerTest` that fly cruise onto
a rocky world. F5 also changed `GetRoom` (cruise's floor: over a solid world, the height over the
ground less the gear), which reads a ground only from this task on. Read against `202703c`, exactly
four places move here:

1. **`SliceLoopTest.cpp:472-473`** (`DeepSpace.Loop...`'s closing flight onto home's largest world,
   Baemsekai III, which is solid). It pins the floor as the sky's alone:

   ```cpp
       TestTrue(FString::Printf(TEXT("the floor is the sky's rendered floor (%.1f km)"), Floor / UniverseUnits::CmPerKm),
                Floor == FMath::Max(10.0 * UniverseUnits::CmPerKm, SkyProjection::RenderedFloor(Radius, FSkyViewParams())));
   ```

   becomes (with `#include "Surface/WorldRelief.h"` added to the file's includes):

   ```cpp
       TestTrue(FString::Printf(TEXT("the floor is the sky's rendered floor above the world's highest peak (%.1f km)"), Floor / UniverseUnits::CmPerKm),
                Floor == FMath::Max(10.0 * UniverseUnits::CmPerKm, SkyProjection::RenderedFloor(Radius, FSkyViewParams()))
                             + FWorldRelief(Here.Bodies[1 + Largest].Relief).MaxHeightCm());
   ```

   Old value max(10 km, RenderedFloor(R)); new value that plus the body's `Relief.PeakCm`. The rest
   of the flight reads `Floor` from `FloorFor` at run time and does not move (the drive, not cruise,
   flies it, and "at the floor it fills the glass at its true size" holds at any altitude: the
   projection keeps angular size exactly).

2. **`PlaytestTest.cpp`, `EtaCountsDown`'s cruise leg** (lines 1185-1254, from
   `    // -- Cruise, on starved boosters ---...` through
   `    TestTrue(FString::Printf(TEXT("and names when it arrives, to a second (worst %.2f s)"), CruiseArrival), CruiseArrival <= 1.0);`).
   It cruises nose-down onto Baemsekai III from 1,200 km over the drive floor and expects to come to
   rest on the floor sphere, with the target line's ETA naming that moment. Over a solid world cruise
   now flies to the ground, and inside the regime it flies the nose's plan view, so the leg can never
   arrive where it looks; and the ETA it checks counts to the drive floor until Task 29 (S7) makes it
   count to the ground. There is no correct expectation for it between this task and S7, so **cut the
   leg here** (the lines above, inclusive; `Ship->SetConsumerWeight(ShipPower::Boosters, 1.0f);` and
   `return true;` stay) and add one line in its place:
   `    // The cruise leg flies to the ground: Task 29 (S7) restores it with the ETA to the ground.`
   S7 Step 4 restores it, rewritten for the ground. The drive leg above it is unchanged: the drive's
   floor sphere still stops the drive, and `WorldFloor` reads `FloorFor` at run time.

3. **`ShipDriveTest.cpp:193-202`**, "the room is the least distance over every surface less its own
   floor". From the opening placement the nearest surface is home's largest world, which is solid, so
   `GetRoom` is now its ground clearance, not the distance to its drive floor. With
   `#include "Surface/WorldRelief.h"` added, the loop

   ```cpp
               for (const FSkyBody& Body : Here.Bodies)
               {
                   Least = FMath::Min(Least, Where.DistanceTo(Body.Position) - Body.Radius - UShipSubsystem::FloorFor(Body));
               }
   ```

   becomes

   ```cpp
               for (const FSkyBody& Body : Here.Bodies)
               {
                   // Over a solid world the room is cruise's: the height over its
                   // ground less the gear (landing decision 10), not the drive's floor.
                   const double Below = Body.Ground == EGround::Solid
                       ? FWorldRelief(Body.Relief).Height(FVector3d((Where - Body.Position).GetSafeNormal())) + UShipSubsystem::GearClearance()
                       : UShipSubsystem::FloorFor(Body);
                   Least = FMath::Min(Least, Where.DistanceTo(Body.Position) - Body.Radius - Below);
               }
   ```

   and the message's `less its own floor` becomes `less its own floor, or over a solid world its
   ground and the gear`. The 1 cm tolerance stays.

4. **`ShipTargetTest.cpp:255-270`**, "Arrival at the floor: flown down to it on the drive's first
   notch", over home's world II (solid). It loops and asserts on `GetRoom()`, which is now the height
   over the ground, 10-20 km under where the drive stops; the drive's arrival is at `FloorFor`. After
   `const FVector Out = (Where() - Centre).GetSafeNormal();` add

   ```cpp
           // The drive stops at its floor; GetRoom is cruise's, over the ground.
           const double DriveFloor = UShipSubsystem::FloorFor(LocalSystem::Here(Home).Bodies[2]);
           const auto FloorRoom = [&]() { return Where().DistanceTo(Centre) - Radius - DriveFloor; };
   ```

   and replace the three `Ship->GetFlightState().GetRoom()` in that block (the loop's condition, the
   message's argument, the assertion) with `FloorRoom()`. Old: the room to the only floor there was;
   new: the same number, now named, since the flight's room means the ground.

Nothing else moves, and why:
- `InSystemJumpTest.cpp`: every floor it flies against comes from `UShipSubsystem::FloorFor` at run
  time (lines 104, 116, 311, 570); its literal floors (lines 235-283) are `NavStart`'s pure
  arithmetic with hand-typed floors, not `FloorFor`'s. No literal moves.
- `TargetMarkerTest.cpp`: its surfaces are hand-built `FFlightSurface`s with no `Ground` (lines 113,
  488, 616, 677), which the spec says are unaffected.
- `ShipDriveTest.cpp`'s `FloorFor` block: its `FSkyBody` fixtures leave `Ground` at its default,
  `EGround::None`, so their floors do not move.
- `ShipTargetTest.cpp`'s ETA flights and `PlaytestTest.cpp`'s other flights: drive flights against
  their own room, `FloorFor` read at run time, never `GetRoom`.
- `ShipJumpTest.cpp:385` reads `GetRoom` between stars, where there are no surfaces: 0, as before.

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh && ./test.sh DeepSpace.Ship.FloorOverPeaks && \
./test.sh DeepSpace.Ship && ./test.sh DeepSpace.Playtest && ./test.sh DeepSpace.Loop && ./test.sh DeepSpace.UI
```

Expected: all green. Before the four edits above, exactly those fail: the `SliceLoopTest` line
`the floor is the sky's rendered floor`, `EtaCountsDown` at `the cruising ship reaches the floor`,
`ShipDriveTest`'s `the room is the least distance`, and `ShipTargetTest`'s `the drive brings the
ship to the world's floor`.
Any other failure is a bug in this task's code, fixed there -- never by editing a test.

- [ ] **Step 6: Document**

In `CLAUDE.md`, *The drive and the jump*, replace the paragraph beginning **The floor is where the
sky stops being honest** with:

```markdown
**The floor is where the sky stops being honest** (decision 6):
`UShipSubsystem::FloorFor`, the one function that answers it -- over a world
the larger of `ds.Flight.Floor` (10 km) and `SkyProjection::RenderedFloor`,
10.2 km over an Earth and 112 km over a Jupiter; over a **solid** world that
is taken **above its highest peak** (`WorldRelief::MaxHeightCm`, landing
decision 10), so the drive never meets a summit; over a star
`ds.Flight.StarFloorRadii` of its radius. It is **the drive's** floor. Over
a solid world cruise and the vertical lever read the ground instead (*Landing*).
```

In *Where each tunable lives*, add rows (before `ds.HUD.TargetMinPixels`):

```markdown
| `ds.Land.GearClearance` | 150 cm | `ShipSubsystem.cpp`, from `ShipLanding::DefaultGearClearanceCm` (`ShipLanding.h`) |
| `ds.Land.TouchdownSpeed` | 0.5 m/s | `ShipSubsystem.cpp`, from `ShipFlight::DefaultTouchdownSpeed` |
| `ds.Land.ApproachSeconds` | 4 s, clamped to at least 0.5 s | `ShipSubsystem.cpp`, from `ShipFlight::DefaultApproachSeconds` |
| `ds.Land.SkimSeconds`, `.SkimFloor` | 2.5 s, 20 m/s | `ShipSubsystem.cpp`, from `ShipFlight` |
| `ds.Land.Regime` | 50 km (leaves over 55 km) | `ShipSubsystem.cpp`, from `ShipFlight::DefaultRegimeCm` |
| `ds.Land.DriveHandback` | 500 m | `ShipSubsystem.cpp`, from `ShipFlight::DefaultDriveHandbackCm` |
| `ds.Vertical.Top`, `.Sweep`, `.HeavyFloor` | 200 m/s, 0.25/s, 0.25 | `ShipSubsystem.cpp`, from `ShipVerticalLever` |
| `ds.Boosters.HoldWatts`, `.StarvedSink` | 150 W per g (cap 3 g), 2 m/s; both only under a solid world's drive floor | `ShipSubsystem.cpp` |
```

- [ ] **Step 7: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
git add Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/Tests/ShipFloorsOverPeaksTest.cpp CLAUDE.md && \
git add -u Source/DeepSpace/Tests && \
git commit -m "$(cat <<'EOF'
feat(ship): the drive floor above the peaks; grounds and wells to the flight; the landing CVars

FloorFor over a solid world is 10 km (or the sky's floor) above its
highest peak. UpdateSurfaces hands the flight each solid world's ground
and every body's pull; ApplyAllocation reads ds.Land.* and ds.Vertical.*
at use. Tests that pinned a rocky world's floor or flew cruise to rest on
its floor sphere now expect the ground (landing decision 10).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 8: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'return Body.Ground == EGround::Solid ? Above + FWorldRelief(Body.Relief).MaxHeightCm() : Above;' 'return Above;' DeepSpace.Ship.FloorOverPeaks && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'Surface.Ground = GroundFor(Body);' 'Surface.Ground.Reset();' DeepSpace.Ship.FloorOverPeaks && \
./build.sh
```

Expected: `KILLED` twice.

---

## Task 24 (S2): effort -- the hold's watts under the floor, the boosters' split, the starved sink's bias

**Owner:** S. **Depends on:** S1. Spec decision 5; sign-off items 6, 7.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipPowerState.h` (append a namespace), `ShipPowerState.cpp`
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` (public getters; private `HoldWant`,
  `LastSplit`), `ShipSubsystem.cpp` (`ApplyAllocation`, the `CVarHoldWatts` default)
- Create: `Source/DeepSpace/Tests/ShipHoldPowerTest.cpp`
  (`DeepSpace.Ship.Power.SplitBoosters`, `DeepSpace.Ship.Power.ParkedIsWhole`)
- Modify: `CLAUDE.md` -- *Screens, the pointer, and power*, the power paragraph (about line 210)

**Interfaces:**
- Consumes: `FShipFlightState::GetLocalGravity`, `GetDepthUnderDriveFloor`;
  `FShipFlightLimits::SinkBias`.
- Produces: `ShipPower::HoldWant`, `FBoosterSplit`, `SplitBoosters`, `HoldGCap`, `HoldRampCm`,
  `DefaultHoldWattsPerG`; `float UShipSubsystem::GetHoldWant() const`,
  `float UShipSubsystem::GetHoldWatts() const`.

- [ ] **Step 1: Write the failing tests**

Create `Source/DeepSpace/Tests/ShipHoldPowerTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipGravity.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 5: gravity is felt as watts, and only under a solid
 * world's drive floor, airborne -- the one place the pilot chose to go. The
 * hold is one want on the boosters, paid first inside their share; the
 * manoeuvre keeps what is left. Everywhere a ship can be parked, the stock
 * ship stays whole (the 2026-09-26 reactor ruling).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipSplitBoostersTest, "DeepSpace.Ship.Power.SplitBoosters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipParkedIsWholeTest, "DeepSpace.Ship.Power.ParkedIsWhole",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace HoldPowerLocal
{
    constexpr double G = ShipFlight::StandardGravityCmS2;

    /** The stock ship's split, as decision 5's table does it: 1400 W less
     *  620 W of modules, lights 300, boosters 450 + the hold, the engine 380
     *  while the jump winds, every weight 1. */
    ShipPower::FBoosterSplit Stock(double Gs, bool bWinding, float& OutShare)
    {
        FShipPowerState Power;
        Power.SetReactorOutput(1400.0f);
        Power.AddDraw(TEXT("Modules"), 620.0f);
        const float Hold = ShipPower::HoldWant(Gs * G, 1.0e6, ShipPower::DefaultHoldWattsPerG, true);
        Power.SetConsumer(ShipPower::Lights, 300.0f, 1.0f);
        Power.SetConsumer(ShipPower::Boosters, 450.0f + Hold, 1.0f);
        Power.SetConsumer(ShipPower::Engine, bWinding ? 380.0f : 0.0f, 1.0f);
        OutShare = Power.GetShare(ShipPower::Boosters);
        return ShipPower::SplitBoosters(OutShare, Hold, 450.0f);
    }
}

bool FShipSplitBoostersTest::RunTest(const FString& Parameters)
{
    using namespace HoldPowerLocal;
    TestEqual(TEXT("150 W a g"), ShipPower::HoldWant(1.0 * G, 1.0e6, 150.0f, true), 150.0f);
    TestEqual(TEXT("capped at 3 g"), ShipPower::HoldWant(5.0 * G, 1.0e6, 150.0f, true), 450.0f);
    TestEqual(TEXT("ramped in over the first kilometre under the floor"), ShipPower::HoldWant(1.0 * G, 5.0e4, 150.0f, true), 75.0f);
    TestEqual(TEXT("nothing at or above the floor"), ShipPower::HoldWant(3.0 * G, 0.0, 150.0f, true), 0.0f);
    TestEqual(TEXT("nothing landed (slice c's seam)"), ShipPower::HoldWant(3.0 * G, 1.0e6, 150.0f, false), 0.0f);

    struct FRow { const TCHAR* Over; double Gs; bool bWinding; float Share; float Fed; float Feed; };
    const FRow Rows[] = {
        { TEXT("Baemsekai IV, about 0.9 g"), 0.9, false, 480.0f, 1.0f, 345.0f / 450.0f },
        { TEXT("Baemsekai III, 2 g"), 2.0, false, 480.0f, 1.0f, 180.0f / 450.0f },
        { TEXT("a 3 g world"), 3.0, false, 480.0f, 1.0f, 30.0f / 450.0f },
        { TEXT("a 3 g world, the jump winding"), 3.0, true, 260.0f, 260.0f / 450.0f, 0.0f },
    };
    for (const FRow& Row : Rows)
    {
        float Share = 0.0f;
        const ShipPower::FBoosterSplit Split = Stock(Row.Gs, Row.bWinding, Share);
        TestTrue(FString::Printf(TEXT("%s: the boosters get %.0f W (%.1f)"), Row.Over, Row.Share, Share), FMath::IsNearlyEqual(Share, Row.Share, 1.0f));
        TestTrue(FString::Printf(TEXT("%s: the hold is fed %.2f (%.3f)"), Row.Over, Row.Fed, Split.HoldFed), FMath::IsNearlyEqual(Split.HoldFed, Row.Fed, 0.005f));
        TestTrue(FString::Printf(TEXT("%s: the manoeuvre gets %.2f of its want (%.3f)"), Row.Over, Row.Feed, Split.ManoeuvreFeed),
                 FMath::IsNearlyEqual(Split.ManoeuvreFeed, Row.Feed, 0.005f));
    }
    const ShipPower::FBoosterSplit None = ShipPower::SplitBoosters(300.0f, 0.0f, 450.0f);
    TestTrue(TEXT("with no hold, the boosters are exactly as before: feed is share over want"),
             None.HoldFed == 1.0f && None.HoldWatts == 0.0f && FMath::IsNearlyEqual(None.ManoeuvreFeed, 300.0f / 450.0f, 1e-6f));
    return true;
}

bool FShipParkedIsWholeTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("ParkedIsWholeWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const auto Settle = [&]() { for (int32 Frame = 0; Frame < 10; ++Frame) { Test.Step(1.0f / 60.0f); } };
    const auto Whole = [&](const TCHAR* Where)
    {
        TestEqual(FString::Printf(TEXT("%s: no hold is wanted"), Where), Ship->GetHoldWant(), 0.0f);
        TestEqual(FString::Printf(TEXT("%s: the lights are whole"), Where), Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f);
        TestEqual(FString::Printf(TEXT("%s: the boosters are whole"), Where), Ship->GetConsumerSatisfaction(ShipPower::Boosters), 1.0f);
    };
    Settle();
    Whole(TEXT("at the opening placement"));

    const FSkySystem Here = LocalSystem::Here(Test.World);
    const auto ParkOver = [&](int32 Body, double OverFloorCm)
    {
        const FSkyBody& World = Here.Bodies[Body];
        const FVector Out = (Ship->GetFlightState().GetUniversePosition() - World.Position).GetSafeNormal();
        Ship->PlaceShip(World.Position + Out * (World.Radius + UShipSubsystem::FloorFor(World) + OverFloorCm), FQuat::Identity);
        Settle();
    };
    ParkOver(3, 1.0e3);
    Whole(TEXT("at Baemsekai III's drive floor"));
    ParkOver(1, 1.0e3);
    Whole(TEXT("at Baemsekai I's drive floor, the star pulling 3.9 g"));
    // An ocean's floor, where the ship can never land to get relief. Home
    // (Baemsekai) has none -- its five worlds are barren or terrestrial -- so
    // the ship goes to Sova, 5.31 ly out, whose fifth world is an ocean
    // (Saved/procgen_describe.txt). A missing fixture fails here, loudly.
    {
        const FUniversePosition HomeAt = Ship->GetFlightState().GetUniversePosition();
        const TArray<FStarSystemStub> Near = Test.Universe->GetSystemsNear(HomeAt, 12.0 * UniverseUnits::CmPerLightYear);
        const FStarSystemStub* Sova = Near.FindByPredicate([](const FStarSystemStub& Stub) { return Stub.Name == TEXT("Sova"); });
        if (TestNotNull(TEXT("Sova, the nearest system with an ocean, is on the chart"), Sova))
        {
            Ship->PlaceShip(Sova->Position + FVector(UniverseUnits::CmPerAU, 0.0, 0.0), FQuat::Identity);
            Settle();
            const FSkySystem There = LocalSystem::Here(Test.World);
            const FSkyBody* Ocean = There.Bodies.FindByPredicate([](const FSkyBody& Body)
            {
                return Body.Kind != ESkyBodyKind::Star && Body.Ground != EGround::Solid;
            });
            if (TestNotNull(TEXT("Sova has a world with no ground (Sova V, an ocean)"), Ocean))
            {
                const FVector Up = (Ship->GetFlightState().GetUniversePosition() - Ocean->Position).GetSafeNormal();
                Ship->PlaceShip(Ocean->Position + Up * (Ocean->Radius + UShipSubsystem::FloorFor(*Ocean) + 1.0e3), FQuat::Identity);
                Settle();
                Whole(*FString::Printf(TEXT("at %s's floor, which it can never land under"), *Ocean->Id.ToString()));
            }
        }
        // Home again, for the hold's own checks.
        Ship->PlaceShip(HomeAt, FQuat::Identity);
        Settle();
    }

    ParkOver(3, -3.0e5);
    const double Gs = Ship->GetFlightState().GetLocalGravity().Size() / ShipFlight::StandardGravityCmS2;
    TestTrue(FString::Printf(TEXT("3 km under Baemsekai III's floor the hold wants 150 W a g (%.1f W at %.2f g)"), Ship->GetHoldWant(), Gs),
             FMath::IsNearlyEqual(Ship->GetHoldWant(), static_cast<float>(150.0 * FMath::Min(Gs, 3.0)), 1.0f));
    TestTrue(TEXT("the boosters' want shows it: 450 W and the hold"),
             FMath::IsNearlyEqual(Ship->GetConsumerWant(ShipPower::Boosters), 450.0f + Ship->GetHoldWant(), 0.01f));
    TestEqual(TEXT("and the lights stay whole while hovering"), Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f);
    TestTrue(TEXT("fed, the hold sinks nothing"), Ship->GetFlightState().GetLimits().SinkBias < 1.0e-3);
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    Settle();
    TestTrue(FString::Printf(TEXT("starved of it, the sink is 2 m/s (%.1f cm/s)"), Ship->GetFlightState().GetLimits().SinkBias),
             FMath::IsNearlyEqual(Ship->GetFlightState().GetLimits().SinkBias, 200.0, 1.0));
    return true;
}

#endif
```

- [ ] **Step 2: Run and see them fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh
```

Expected: fails to compile: `'HoldWant': is not a member of 'ShipPower'`.

- [ ] **Step 3: The split**

Append to `Source/DeepSpace/Ship/ShipPowerState.h`:

```cpp
/**
 * The boosters' hold against gravity (landing decision 5): a want that
 * exists only under a solid world's drive floor, airborne, so staying put is
 * never taxed anywhere a ship can be parked. It is one consumer's want, not a
 * new consumer -- a new one would be another weight to tune toward an optimum.
 */
namespace ShipPower
{
    /** ds.Boosters.HoldWatts' default: watts per g of total pull. */
    inline constexpr float DefaultHoldWattsPerG = 150.0f;

    /** The hold counts pull up to this many g. */
    inline constexpr double HoldGCap = 3.0;

    /** The want ramps in over this far under the floor, cm, so crossing it is
     *  not a step. */
    inline constexpr double HoldRampCm = 1.0e5;

    /** WattsPerG x min(g / g_E, 3), ramped over the first kilometre under the
     *  floor; 0 at or above the floor, and 0 landed (bAirborne false). */
    DEEPSPACE_API float HoldWant(double GravityCmS2, double DepthUnderFloorCm, float WattsPerG, bool bAirborne);

    struct FBoosterSplit
    {
        /** HoldWatts over the hold's want, 0..1; 1 with no hold wanted. */
        float HoldFed = 1.0f;

        /** Watts reaching the hold: the hum's hold term reads these. */
        float HoldWatts = 0.0f;

        /** What is left for manoeuvring over its want, 0..1: the thrust's feed. */
        float ManoeuvreFeed = 1.0f;
    };

    /** Inside the boosters' Share, the hold is paid first; the manoeuvre keeps
     *  the rest. With no hold it is share over want, exactly as before. */
    DEEPSPACE_API FBoosterSplit SplitBoosters(float Share, float HoldWant, float ManoeuvreWant);
}
```

Append to `ShipPowerState.cpp` (and `#include "Ship/ShipGravity.h"`):

```cpp
float ShipPower::HoldWant(double GravityCmS2, double DepthUnderFloorCm, float WattsPerG, bool bAirborne)
{
    if (!bAirborne || !(DepthUnderFloorCm > 0.0) || !(WattsPerG > 0.0f))
    {
        return 0.0f;
    }
    const double Gs = FMath::Min(FMath::Max(GravityCmS2, 0.0) / ShipFlight::StandardGravityCmS2, HoldGCap);
    const double Ramp = FMath::Clamp(DepthUnderFloorCm / HoldRampCm, 0.0, 1.0);
    return static_cast<float>(WattsPerG * Gs * Ramp);
}

ShipPower::FBoosterSplit ShipPower::SplitBoosters(float Share, float HoldWant, float ManoeuvreWant)
{
    FBoosterSplit Split;
    const float Available = FMath::Max(Share, 0.0f);
    const float Hold = FMath::Max(HoldWant, 0.0f);
    Split.HoldWatts = FMath::Min(Available, Hold);
    Split.HoldFed = Hold > 0.0f ? Split.HoldWatts / Hold : 1.0f;
    const float Left = Available - Split.HoldWatts;
    Split.ManoeuvreFeed = ManoeuvreWant > 0.0f ? FMath::Clamp(Left / ManoeuvreWant, 0.0f, 1.0f) : 1.0f;
    return Split;
}
```

- [ ] **Step 4: The subsystem pays it**

`ShipSubsystem.h`, public (beside `GetLinearAcceleration`):

```cpp
    /** Watts the boosters want to hold the ship against gravity: 150 W a g
     *  under a solid world's drive floor, airborne, and nothing anywhere a
     *  ship can be parked (landing decision 5). */
    float GetHoldWant() const;

    /** Watts actually reaching the hold, paid first inside the boosters'
     *  share: what the hum's hold term reads, never satisfaction. */
    float GetHoldWatts() const;
```

private: `float HoldWant = 0.0f; ShipPower::FBoosterSplit LastSplit;`.

`ShipSubsystem.cpp`: set `CVarHoldWatts`' default to `ShipPower::DefaultHoldWattsPerG`. In
`ApplyAllocation`, replace the three lines from `const float BoosterFeed = ...` to
`const float Thrust = ...` with:

```cpp
    // The hold (landing decision 5): only under a solid world's drive floor,
    // airborne -- landed is slice (c)'s, always airborne here. Set only when
    // it moves by more than a watt, so the split is not re-solved every frame
    // for nothing.
    const float WantNow = ShipPower::HoldWant(FlightState.GetLocalGravity().Size(), FlightState.GetDepthUnderDriveFloor(),
                                              FMath::Max(0.0f, CVarHoldWatts.GetValueOnGameThread()), true);
    if (FMath::Abs(WantNow - HoldWant) > 1.0f || (WantNow == 0.0f && HoldWant != 0.0f))
    {
        HoldWant = WantNow;
        PowerState.SetWant(ShipPower::Boosters, BoostersWant + HoldWant);
    }

    // Asked for fresh every frame and never stored beyond it.
    LastSplit = ShipPower::SplitBoosters(PowerState.GetShare(ShipPower::Boosters), HoldWant, BoostersWant);
    const float EngineFeed = PowerState.GetSatisfaction(ShipPower::Engine);
    const float Thrust = StarvedBoosterThrust + (1.0f - StarvedBoosterThrust) * LastSplit.ManoeuvreFeed;
```

The `const float EngineFeed = ...` line stays: `ChargeJumpDrive` reads it further down
(`ShipSubsystem.cpp:520`). Just before `FlightState.SetLimits(Limits);`:

```cpp
    // The starved sink: a bias the flight applies only under the floor, and
    // only to HOVER or a sink -- a starved ship always lifts.
    Limits.SinkBias = FMath::Max(0.0f, CVarStarvedSink.GetValueOnGameThread()) * 100.0 * (1.0 - LastSplit.HoldFed);
```

and add:

```cpp
float UShipSubsystem::GetHoldWant() const
{
    return HoldWant;
}

float UShipSubsystem::GetHoldWatts() const
{
    return LastSplit.HoldWatts;
}
```

- [ ] **Step 5: Run: PASS, and the power tests unchanged**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh && ./test.sh DeepSpace.Ship.Power && \
./test.sh DeepSpace.Ship.JumpCanWindAtFullSpeed && ./test.sh DeepSpace.Ship.BrownOutRunsWarmer && ./test.sh DeepSpace.Loop.LampsFollowTheSplit
```

Expected: all green: with no hold the split is share over want, exactly as before.

- [ ] **Step 6: Document the one sanctioned change with time**

In `CLAUDE.md`, *Screens, the pointer, and power*, replace the sentence
`**There is deliberately no cutoff, no alarm, no timer and no failure state**, and nothing in the
model changes on its own with time.` with:

```markdown
**There is deliberately no cutoff, no alarm, no timer and no failure state**,
and nothing in the model changes on its own with time -- with one sanctioned,
bounded exception, the **starved sink** (landing decision 5): under a solid
world's drive floor, airborne, the boosters want to hold the ship against
gravity (`ds.Boosters.HoldWatts`, 150 W a g to 3 g, paid first inside their
share, `ShipPower::SplitBoosters`), and a hold short of watts becomes a sink
of at most `ds.Boosters.StarvedSink` (2 m/s), never while the vertical lever
asks a climb, ending always at rest on the ground at no cost. That want
exists only there, so **staying put is never taxed** anywhere a ship can be
parked: at any floor, between worlds, or (slice c) landed.
```

- [ ] **Step 7: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
git add Source/DeepSpace/Ship/ShipPowerState.h Source/DeepSpace/Ship/ShipPowerState.cpp Source/DeepSpace/Ship/ShipSubsystem.h \
        Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/Tests/ShipHoldPowerTest.cpp CLAUDE.md && \
git commit -m "$(cat <<'EOF'
feat(power): gravity felt as watts -- the hold under a solid world's floor, paid first; the starved sink's bias

HoldWant is 150 W a g (to 3 g) under a solid world's drive floor,
airborne, ramped over the first kilometre; SplitBoosters pays it first
inside the boosters' share and leaves the rest to manoeuvre. Parked
anywhere -- the opening, any floor, a giant's, a star's -- the stock
ship is whole. CLAUDE.md's power paragraph gains its one sanctioned
change with time (landing decision 5).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 8: Prove them**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipPowerState.cpp 'Split.HoldWatts = FMath::Min(Available, Hold);' 'Split.HoldWatts = 0.0f;' DeepSpace.Ship.Power.SplitBoosters && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipPowerState.cpp 'if (!bAirborne || !(DepthUnderFloorCm > 0.0) || !(WattsPerG > 0.0f))' 'if (!bAirborne || !(WattsPerG > 0.0f))' DeepSpace.Ship.Power.ParkedIsWhole && \
./build.sh
```

Expected: `KILLED` twice.

---

## Task 25 (S3): the hum's hold term

**Owner:** S. **Depends on:** S2. Spec decision 5 (*The hum*).

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipHumVoice.h` (the `ShipHum` namespace, lines 25-53),
  `ShipHumVoice.cpp` (after `Push`, line 38)
- Modify: `Source/DeepSpace/Ship/ShipHumComponent.cpp` (the CVar block, lines 16-26; `AskShip`,
  lines 87-118)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` (public section, beside `GetHoldWatts`:
  `static float GetHoldWattsPerG();`), `Source/DeepSpace/Ship/ShipSubsystem.cpp` (its definition,
  after `GetHoldWatts`'s)
- Create: `Source/DeepSpace/Tests/HumHoldTest.cpp` (`DeepSpace.Ship.HumHold`)
- Modify: `CLAUDE.md` -- *The hum and the lamps* (one sentence), the tunables table (one row)

**Interfaces:**
- Consumes: `UShipSubsystem::GetHoldWatts`, `ShipPower::DefaultHoldWattsPerG`, `CVarHoldWatts` by
  its value through a new `static float UShipSubsystem::GetHoldWattsPerG()`.
- Produces: `float ShipHum::HoldTerm(float HoldWattsDelivered, float WattsPerG, float HoldHiss, float CruiseHiss)`;
  `float ShipHum::Push(float AccelerationCmS2, float RatedAccelerationCmS2, float Throttle, float ThrustFraction, float CruiseHiss, float Holding)`;
  CVar `ds.Hum.HoldHiss` (0.35).

- [ ] **Step 1: Write the failing test**

Create `Source/DeepSpace/Tests/HumHoldTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipHumComponent.h"
#include "Ship/ShipHumVoice.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 5's hum: a hold term in watts delivered, never
 * satisfaction, that reaches cruise's hiss only at the 3 g cap and never
 * exceeds it, and is silent everywhere a ship can be parked.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHumHoldTest, "DeepSpace.Ship.HumHold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHumHoldTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    TestEqual(TEXT("no watts held, no hold hiss"), ShipHum::HoldTerm(0.0f, 150.0f, 0.35f, 0.35f), 0.0f);
    TestTrue(TEXT("a 1 g hover hisses at a third of cruise"), FMath::IsNearlyEqual(ShipHum::HoldTerm(150.0f, 150.0f, 0.35f, 0.35f), 0.35f / 3.0f, 1e-6f));
    TestTrue(TEXT("the 3 g cap reaches cruise's hiss"), FMath::IsNearlyEqual(ShipHum::HoldTerm(450.0f, 150.0f, 0.35f, 0.35f), 0.35f, 1e-6f));
    TestEqual(TEXT("and a louder ds.Hum.HoldHiss never passes cruise's"), ShipHum::HoldTerm(450.0f, 150.0f, 1.0f, 0.35f), 0.35f);
    TestEqual(TEXT("Push takes the largest way the boosters work"), ShipHum::Push(0.0f, 2.0e5f, 0.0f, 1.0f, 0.35f, 0.2f), 0.2f);
    TestEqual(TEXT("and the old reading is unchanged with no hold"), ShipHum::Push(1.0e5f, 2.0e5f, 0.0f, 1.0f, 0.35f, 0.0f),
              ShipHum::Push(1.0e5f, 2.0e5f, 0.0f, 1.0f, 0.35f));

    FSkyWorld Test(TEXT("HumHoldWorld"));
    Test.BeginPlay();
    StockShip::Install(Test.Ship);
    const auto Settle = [&]() { for (int32 Frame = 0; Frame < 10; ++Frame) { Test.Step(1.0f / 60.0f); } };
    Settle();
    TestEqual(TEXT("parked at the opening, the hold is silent"), UShipHumComponent::AskShip(*Test.Ship).Push, 0.0f);

    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody& Third = Here.Bodies[3];
    const FVector Out = (Test.Ship->GetFlightState().GetUniversePosition() - Third.Position).GetSafeNormal();
    Test.Ship->PlaceShip(Third.Position + Out * (Third.Radius + UShipSubsystem::FloorFor(Third) - 3.0e5), FQuat::Identity);
    Settle();
    const float Expected = ShipHum::HoldTerm(Test.Ship->GetHoldWatts(), UShipSubsystem::GetHoldWattsPerG(), 0.35f, 0.35f);
    TestTrue(FString::Printf(TEXT("hovering under Baemsekai III's floor it hisses with the hold (%.3f)"), Expected), Expected > 0.1f);
    TestTrue(TEXT("and the hum reads it"), UShipHumComponent::AskShip(*Test.Ship).Push >= Expected - 1e-6f);
    return true;
}

#endif
```

- [ ] **Step 2: Run and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh
```

Expected: fails to compile: `'HoldTerm': is not a member of 'ShipHum'`.

- [ ] **Step 3: The term**

In `ShipHumVoice.h`'s `ShipHum` namespace, after `Push`:

```cpp
    /** Push with the hold's hiss beside the other two ways: the largest. */
    DEEPSPACE_API float Push(float AccelerationCmS2, float RatedAccelerationCmS2,
                             float Throttle, float ThrustFraction, float CruiseHiss, float Holding);

    /**
     * The hold's hiss (landing decision 5): HoldHiss x HoldWattsDelivered /
     * (3 x WattsPerG), in watts delivered, never satisfaction (plan conflict
     * 8's trap), so it is silent wherever the hold wants nothing -- every
     * place a ship can be parked -- swells as the ship goes under a solid
     * world's floor, and reaches cruise's hiss only at the 3 g cap, never
     * above it.
     */
    DEEPSPACE_API float HoldTerm(float HoldWattsDelivered, float WattsPerG, float HoldHiss, float CruiseHiss);
```

In `ShipHumVoice.cpp`, after `Push`:

```cpp
float ShipHum::Push(float AccelerationCmS2, float RatedAccelerationCmS2,
                    float Throttle, float ThrustFraction, float CruiseHiss, float Holding)
{
    return FMath::Clamp(FMath::Max(Push(AccelerationCmS2, RatedAccelerationCmS2, Throttle, ThrustFraction, CruiseHiss), Holding), 0.0f, 1.0f);
}

float ShipHum::HoldTerm(float HoldWattsDelivered, float WattsPerG, float HoldHiss, float CruiseHiss)
{
    if (!(WattsPerG > 0.0f) || !(HoldWattsDelivered > 0.0f))
    {
        return 0.0f;
    }
    const float Term = FMath::Max(0.0f, HoldHiss) * HoldWattsDelivered / (3.0f * WattsPerG);
    return FMath::Min(Term, FMath::Max(0.0f, CruiseHiss));
}
```

In `ShipSubsystem.h` public: `static float GetHoldWattsPerG();` and in `.cpp`:

```cpp
float UShipSubsystem::GetHoldWattsPerG()
{
    return FMath::Max(0.0f, CVarHoldWatts.GetValueOnGameThread());
}
```

In `ShipHumComponent.cpp`, after `CVarCruiseHiss`:

```cpp
    TAutoConsoleVariable<float> CVarHoldHiss(
        TEXT("ds.Hum.HoldHiss"), 0.35f,
        TEXT("The hold's hiss at the 3 g cap: under a solid world's drive floor the boosters hiss with the watts holding the ship. Never above ds.Hum.CruiseHiss."),
        ECVF_Default);
```

and in `AskShip` replace the `Inputs.Push = ShipHum::Push(...)` statement's last argument line so
it reads:

```cpp
    const float CruiseHiss = CVarCruiseHiss.GetValueOnGameThread();
    Inputs.Push = ShipHum::Push(static_cast<float>(Flight.GetLinearAcceleration().Size()), Rated,
                                static_cast<float>(Lever),
                                Rated > 0.0f ? Ship.GetLinearAcceleration() / Rated : 0.0f,
                                CruiseHiss,
                                ShipHum::HoldTerm(Ship.GetHoldWatts(), UShipSubsystem::GetHoldWattsPerG(),
                                                  CVarHoldHiss.GetValueOnGameThread(), CruiseHiss));
```

- [ ] **Step 4: Run: PASS, and the hum's old tests**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh && ./test.sh DeepSpace.Ship.HumHold && \
./test.sh DeepSpace.Ship.HumVoice && ./test.sh DeepSpace.Ship.HumComponent
```

Expected: all green (`HumVoice` pins the five-argument `Push`, unchanged).

- [ ] **Step 5: Document**

In `CLAUDE.md`, *The hum and the lamps*, after the sentence ending `(ds.Hum.CruiseHiss)`, add:
`Under a solid world's drive floor the hiss also follows the boosters' **hold**, in watts
delivered (ds.Hum.HoldHiss x watts / (3 x ds.Boosters.HoldWatts), never above cruise's hiss), so
it is silent wherever a ship can be parked.` In the tunables table, change the hum row to
``| `ds.Hum.Volume`, `ds.Hum.CruiseHiss`, `ds.Hum.HoldHiss` | 1.0, 0.35, 0.35 | `ShipHumComponent.cpp` |``.

- [ ] **Step 6: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
git add Source/DeepSpace/Ship/ShipHumVoice.h Source/DeepSpace/Ship/ShipHumVoice.cpp Source/DeepSpace/Ship/ShipHumComponent.cpp \
        Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/Tests/HumHoldTest.cpp CLAUDE.md && \
git commit -m "$(cat <<'EOF'
feat(hum): the hold's hiss -- watts delivered to the hold, never above cruise's, silent when parked

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipHumVoice.cpp 'return FMath::Min(Term, FMath::Max(0.0f, CruiseHiss));' 'return Term;' DeepSpace.Ship.HumHold && \
./build.sh
```

Expected: `KILLED`.

---

## Task 26 (S4): the helm moves the vertical lever -- presses, holds, the catch after X, spent holds, HOVER at every stop

**Owner:** S. **Depends on:** S1, F4, F7. Spec decision 8 (*Spent holds*, *Tools and tests
setter*), sign-off item 17.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` -- `FHelmInput` (lines 23-37), the write-path
  comment (lines 244-248), public `SetVerticalLever`, private spent-hold flags
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.cpp` -- `ApplyHelm` (lines 604-658), the fold's
  stop in `StepNavigation` (about line 691), `SetHelmInput` (lines 934-958), `AllStop`
  (lines 960-978), new `SetVerticalLever`
- Modify: `Source/DeepSpace/Tests/ShipFlightAuthorityTest.cpp` (before `// Standing up stops the
  turn and keeps the cruise.`, line 55)
- Create: `Source/DeepSpace/Tests/HelmVerticalTest.cpp` (`DeepSpace.Ship.HelmVertical`)

**Interfaces:**
- Consumes: `ShipVerticalLever::{Sweep, Catch}`, `FShipFlightState::GetVerticalSpeed`,
  `EFlightMode::DriveBelowFloor`.
- Produces: `FHelmInput::{bVerticalUpHeld, bVerticalDownHeld, VerticalUpPresses, VerticalDownPresses}`;
  `bool UShipSubsystem::SetVerticalLever(APawn* Commander, double Lever)`.

- [ ] **Step 1: Write the failing tests**

In `ShipFlightAuthorityTest.cpp`, before the comment `// Standing up stops the turn and keeps the
cruise.`, insert:

```cpp
        // The vertical lever is a write path too, pilot-gated like the rest.
        TestFalse(TEXT("a passenger may not set the vertical lever"), Ship->SetVerticalLever(Passenger, 0.5));
        TestTrue(TEXT("the pilot may"), Ship->SetVerticalLever(Pilot, 0.5));
        TestEqual(TEXT("and it lands"), Ship->GetFlightState().GetCommand().Vertical, 0.5);
        Ship->SetVerticalLever(Pilot, 0.0);
```

Create `Source/DeepSpace/Tests/HelmVerticalTest.cpp`:

```cpp
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"
#include "Ship/ShipVerticalLever.h"
#include "Sky/LocalSystem.h"
#include "Surface/GroundField.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The helm and the third lever (landing decision 8): Space and C sweep it
 * whatever lever F has live, a sweep stops at HOVER, a key held from before
 * sitting down or through X moves nothing until let go, X and the fold's all
 * stop set HOVER, and after X a press the way the ship is moving catches it.
 * Under a solid world's drive floor, with the drive live, Shift and Ctrl
 * move cruise's lever, which is the one flying the ship.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelmVerticalTest, "DeepSpace.Ship.HelmVertical",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHelmVerticalTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("HelmVerticalWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    const FShipFlightState& Flight = Ship->GetFlightState();
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);

    // 2 km over Baemsekai IV, level: in the near regime, under the drive floor.
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody& Fourth = Here.Bodies[4];
    const FGroundFieldRef Ground = ShipGround::FromRelief(Fourth.Relief);
    const FVector Out = (Flight.GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const FVector3d D(Out);
    Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + Ground->Height(D, 0.0) + 2.0e5),
                    FRotationMatrix::MakeFromXZ(FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal(), Out).ToQuat());
    const float Dt = 1.0f / 60.0f;
    const auto Hand = [&](bool bUp, bool bDown, int32 Ups, int32 Downs)
    {
        FHelmInput Input;
        Input.bVerticalUpHeld = bUp;
        Input.bVerticalDownHeld = bDown;
        Input.VerticalUpPresses = Ups;
        Input.VerticalDownPresses = Downs;
        Ship->SetHelmInput(Pilot, Input);
        Test.Step(Dt);
    };

    // The first hands: a Space held from before sitting down moves nothing.
    Hand(true, false, 0, 0);
    Hand(true, false, 0, 0);
    TestEqual(TEXT("Space held from before sitting down is spent"), Flight.GetCommand().Vertical, 0.0);
    Hand(false, false, 0, 0);

    // A fresh press and a hold: climbing.
    Hand(true, false, 1, 0);
    for (int32 Frame = 0; Frame < 60; ++Frame) { Hand(true, false, 0, 0); }
    TestTrue(FString::Printf(TEXT("a second held sweeps a quarter of the lever up (%.3f)"), Flight.GetCommand().Vertical),
             FMath::IsNearlyEqual(Flight.GetCommand().Vertical, 0.25, 0.02));
    Hand(false, false, 0, 0);
    TestTrue(TEXT("the ship climbs on it"), Flight.GetVerticalSpeed() > 0.0);

    // C held from a climb stops at HOVER.
    for (int32 Frame = 0; Frame < 120; ++Frame) { Hand(false, true, 0, Frame == 0 ? 1 : 0); }
    TestEqual(TEXT("C held from a climb stops at HOVER"), Flight.GetCommand().Vertical, 0.0);
    Hand(false, false, 0, 0);

    // A sink, then X with C still held: HOVER, and the held C moves nothing.
    Hand(false, true, 0, 1);
    for (int32 Frame = 0; Frame < 120; ++Frame) { Hand(false, true, 0, 0); }
    TestTrue(TEXT("a fresh C sinks"), Flight.GetCommand().Vertical < -0.4);
    Ship->AllStop(Pilot);
    TestEqual(TEXT("X sets the vertical lever to HOVER"), Flight.GetCommand().Vertical, 0.0);
    Hand(false, true, 0, 0);
    TestEqual(TEXT("C held through X moves nothing"), Flight.GetCommand().Vertical, 0.0);
    Hand(false, false, 0, 0);

    // Sinking again; X, and C pressed at once while the ship is still
    // sinking (the boosters stop it within a frame or two at 2 km/s^2): the
    // press catches it at the rate it has, the 2026-09-26 ruling.
    Hand(false, true, 0, 1);
    for (int32 Frame = 0; Frame < 120; ++Frame) { Hand(false, true, 0, 0); }
    Hand(false, false, 0, 0);
    const double Sinking = Flight.GetVerticalSpeed();
    TestTrue(FString::Printf(TEXT("sinking before X (%.1f cm/s)"), Sinking), Sinking < -ShipVerticalLever::FloorCmPerSecond);
    Ship->AllStop(Pilot);
    Hand(false, false, 0, 1);
    const double Asked = ShipVerticalLever::Rate(Flight.GetCommand().Vertical, Flight.GetLimits().VerticalTop);
    TestTrue(FString::Printf(TEXT("one C straight after X catches it where it is (asks %.1f, was %.1f cm/s)"), Asked, Sinking),
             Asked < 0.0 && FMath::Abs(Asked - Sinking) <= FMath::Abs(Sinking) * 0.05);

    // F under the floor: Shift moves cruise's lever, the one flying the ship.
    Ship->AllStop(Pilot);
    Hand(false, false, 0, 0);
    Ship->SetDriveEngaged(Pilot, true);
    Test.Step(Dt);
    TestTrue(TEXT("F under the floor: DriveBelowFloor"), Flight.GetMode() == EFlightMode::DriveBelowFloor);
    const int32 Notch = Flight.GetCommand().DriveNotch;
    FHelmInput Shift;
    Shift.bUpHeld = true;
    Shift.UpPresses = 1;
    Ship->SetHelmInput(Pilot, Shift);
    Test.Step(Dt);
    TestTrue(TEXT("Shift moves cruise's lever"), Flight.GetCommand().Throttle > 0.0);
    TestEqual(TEXT("and leaves the drive's notch"), Flight.GetCommand().DriveNotch, Notch);
    return true;
}

#endif
```

- [ ] **Step 2: Run and see them fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && git merge -q feat/landing-b && ./build.sh
```

Expected: fails to compile: `'bVerticalUpHeld': is not a member of 'FHelmInput'`.

- [ ] **Step 3: The helm**

In `FHelmInput`, after `DownPresses`:

```cpp
    /** Space and C held: the vertical lever's up and down (landing decision
     *  8). Always the vertical lever's, never switched by F. */
    bool bVerticalUpHeld = false;
    bool bVerticalDownHeld = false;

    /** Their presses since the last hand-over: the fresh press that leaves
     *  HOVER, and after X the press that catches the ship where it is. */
    int32 VerticalUpPresses = 0;
    int32 VerticalDownPresses = 0;
```

Public, after `SetDriveLever`:

```cpp
    /** The vertical lever to Lever, -1..1, absolute: for tests and tools, as
     *  SetDriveLever is. Pilot-gated; refused in transit. */
    bool SetVerticalLever(APawn* Commander, double Lever);
```

Update the write-path comment on `GetFlightState` to list `SetVerticalLever` after `SetDriveLever`.
Private, after `bDownHoldSpent`: `bool bVerticalUpHoldSpent = false; bool bVerticalDownHoldSpent = false;`.

In `ShipSubsystem.cpp`, `SetHelmInput`: in the `bAwaitingFirstHands` block add
`bVerticalUpHoldSpent = Input.bVerticalUpHeld; bVerticalDownHoldSpent = Input.bVerticalDownHeld;`;
after the `Helm.DownPresses += ...` line add:

```cpp
    Helm.bVerticalUpHeld = Input.bVerticalUpHeld;
    Helm.bVerticalDownHeld = Input.bVerticalDownHeld;
    Helm.VerticalUpPresses += FMath::Max(0, Input.VerticalUpPresses);
    Helm.VerticalDownPresses += FMath::Max(0, Input.VerticalDownPresses);
```

`AllStop`: after `Command.DriveNotch = 0;` add `Command.Vertical = 0.0;`, and after
`bDownHoldSpent = Helm.bDownHeld;` add:

```cpp
    Helm.VerticalUpPresses = 0;
    Helm.VerticalDownPresses = 0;
    bVerticalUpHoldSpent = Helm.bVerticalUpHeld;
    bVerticalDownHoldSpent = Helm.bVerticalDownHeld;
```

The fold's all stop in `StepNavigation`: after `Stopped.DriveNotch = 0;` add
`Stopped.Vertical = 0.0;   // HOVER: all stop means the ship holds where it is (sign-off 17)`.

`ApplyHelm`: after `Helm.DownPresses = 0;` add

```cpp
    const int32 VerticalUps = Helm.VerticalUpPresses;
    const int32 VerticalDowns = Helm.VerticalDownPresses;
    Helm.VerticalUpPresses = 0;
    Helm.VerticalDownPresses = 0;
    bVerticalUpHoldSpent &= Helm.bVerticalUpHeld;
    bVerticalDownHoldSpent &= Helm.bVerticalDownHeld;
```

change `if (Command.bDrive)` to `if (FlightState.GetMode() == EFlightMode::Drive)` (so under the
floor, where the ship flies cruise, Shift and Ctrl move cruise), and before
`FlightState.SetCommand(Command);` add:

```cpp
    // The vertical lever (landing decision 8), whichever lever F has live: a
    // press the way the ship is already moving catches it there when the
    // lever is at HOVER (after X, the 2026-09-26 ruling), then the sweep,
    // which stops at HOVER and leaves it only on a fresh press.
    const bool bVerticalUp = Helm.bVerticalUpHeld && !bVerticalUpHoldSpent;
    const bool bVerticalDown = Helm.bVerticalDownHeld && !bVerticalDownHoldSpent;
    const double Top = FlightState.GetLimits().VerticalTop;
    Command.Vertical = ShipVerticalLever::Catch(Command.Vertical, VerticalUps, VerticalDowns, FlightState.GetVerticalSpeed(), Top);
    Command.Vertical = ShipVerticalLever::Sweep(Command.Vertical, bVerticalUp, bVerticalDown, VerticalUps, VerticalDowns,
                                                DeltaSeconds, FMath::Max(0.0f, CVarVerticalSweep.GetValueOnGameThread()));
```

Add:

```cpp
bool UShipSubsystem::SetVerticalLever(APawn* Commander, double Lever)
{
    if (!MayCommand(Commander))
    {
        return false;
    }
    FShipFlightCommand Command = FlightState.GetCommand();
    Command.Vertical = Lever;
    FlightState.SetCommand(Command);
    return true;
}
```

- [ ] **Step 4: Run: PASS**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh && ./test.sh DeepSpace.Ship.HelmVertical && \
./test.sh DeepSpace.Ship.FlightAuthority && ./test.sh DeepSpace.Ship.Drive && ./test.sh DeepSpace.Playtest.LeverToLightAndBack
```

Expected: all green.

- [ ] **Step 5: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
git add Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp \
        Source/DeepSpace/Tests/ShipFlightAuthorityTest.cpp Source/DeepSpace/Tests/HelmVerticalTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(helm): the vertical lever at the helm -- sweep, detent, catch after X, spent holds, HOVER at every stop

Space and C move the vertical lever whatever F has live; a key held from
before sitting down or through X is spent; X and the fold's all stop set
HOVER; after X a press the way the ship is moving catches it. Under a
solid world's floor with the drive live, Shift and Ctrl move cruise's
lever. SetVerticalLever joins the pilot-gated write paths.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp '    Command.Vertical = 0.0;
    FlightState.SetCommand(Command);' '    FlightState.SetCommand(Command);' DeepSpace.Ship.HelmVertical && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'bVerticalDownHoldSpent = Helm.bVerticalDownHeld;
    return true;' 'return true;' DeepSpace.Ship.HelmVertical && \
./build.sh
```

Expected: `KILLED` twice (X no longer sets HOVER; a C held through X sinks the ship). If the first
pattern is not unique, `mutate.sh` says `MUTANT NOT APPLIED`: use the `AllStop` function's full
three lines `Command.DriveNotch = 0;\n    Command.Vertical = 0.0;\n    FlightState.SetCommand(Command);`.

---

## Task 27 (S5): Space and C at the helm -- the actions, the pawn's hands, jump and crouch gated seated

**Owner:** S. **Depends on:** S4. Spec decision 8 (*Keys*), sign-off item 18.

**Files:**
- Modify: `Source/DeepSpace/Player/DeepSpaceCharacter.h` -- public (after `HoldLever`, about line
  158), the input `UPROPERTY`s (after `LeverDownAction`, line 322), private handlers (after line
  369) and state (after line 499)
- Modify: `Source/DeepSpace/Player/DeepSpaceCharacter.cpp` -- `SetupPlayerInputComponent`'s
  `JumpAction` binding (lines 388-392) and new bindings, `ToggleCrouch` (line 562), `PushHelmInput`
  (lines 515-533), new functions
- Modify: `Tools/setup_flight_input.py` -- the docstring, `PRESS_KEYS`, `PROPERTIES`,
  `SHARES_WITH_WALKING`
- Create: `Source/DeepSpace/Tests/PlaytestLandingTest.cpp` with `DeepSpace.Playtest.KeysLiftTheShip`
  (Task S8 adds its siblings to the same file)
- Modify: `CLAUDE.md` -- *Flying*'s key table

**Interfaces:**
- Consumes: `FHelmInput`'s vertical fields.
- Produces: `IA_VerticalUp` (SpaceBar), `IA_VerticalDown` (C); `ADeepSpaceCharacter::TapVertical(int32 Direction)`,
  `HoldVertical(int32 Direction)`; `UPROPERTY VerticalUpAction`, `VerticalDownAction`.

- [ ] **Step 1: Write the failing playtest**

Create `Source/DeepSpace/Tests/PlaytestLandingTest.cpp`:

```cpp
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipSubsystem.h"
#include "Ship/ShipVerticalLever.h"
#include "Sky/LocalSystem.h"
#include "Surface/GroundField.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"
#include "UI/ShipHUDWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing slice (b) as the next playtest will try it: through the pawn's
 * hands and IMC_Default where keys are involved, on the fixture worlds of
 * home (Baemsekai IV, barren, 0.84 + 0.09 g; Baemsekai III, barren, 1.66 +
 * 0.31 g), read back in the HUD's words. Siblings, never a group.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestKeysLiftTheShipTest, "DeepSpace.Playtest.KeysLiftTheShip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PlaytestLandingLocal
{
    using namespace SkyTestWorld;
    constexpr float Dt = 1.0f / 60.0f;

    /** Home's body by index (0 the star; IV is 4), its name checked. */
    const FSkyBody* HomeBody(FAutomationTestBase& Test, const FSkySystem& Here, int32 Index, const TCHAR* Name)
    {
        const bool bFound = Here.Bodies.IsValidIndex(Index) && Here.Bodies[Index].Id.ToString() == Name;
        return Test.TestTrue(FString::Printf(TEXT("home's body %d is %s"), Index, Name), bFound) ? &Here.Bodies[Index] : nullptr;
    }

    /** The ship AglCm over Body's ground, level, on the side the ship was on. */
    void PlaceOver(UShipSubsystem* Ship, const FSkyBody& Body, double AglCm)
    {
        const FGroundFieldRef Ground = ShipGround::FromRelief(Body.Relief);
        const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Body.Position).GetSafeNormal();
        const FVector Heading = FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal();
        Ship->PlaceShip(Body.Position + Out * (Body.Radius + Ground->Height(FVector3d(Out), 0.0) + AglCm),
                        FRotationMatrix::MakeFromXZ(Heading, Out).ToQuat());
    }

    /** One frame as the game runs it: the pawn hands its keys over, the ship
     *  applies them. */
    void Frame(ADeepSpaceCharacter* Player, UShipSubsystem* Ship, float Seconds = Dt)
    {
        Player->Tick(Seconds);
        Ship->Tick(Seconds);
    }

    /** The key IMC_Default binds Action to, or none. */
    TOptional<FKey> KeyOf(const UInputMappingContext* Context, const TCHAR* ActionPath)
    {
        const UInputAction* Action = LoadObject<UInputAction>(nullptr, ActionPath);
        if (!Context || !Action)
        {
            return {};
        }
        for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
        {
            if (Mapping.Action == Action)
            {
                return Mapping.Key;
            }
        }
        return {};
    }
}

bool FPlaytestKeysLiftTheShipTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    const UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
    const TOptional<FKey> Up = KeyOf(Context, TEXT("/Game/Input/Actions/IA_VerticalUp.IA_VerticalUp"));
    const TOptional<FKey> Down = KeyOf(Context, TEXT("/Game/Input/Actions/IA_VerticalDown.IA_VerticalDown"));
    TestTrue(TEXT("IMC_Default binds Space to the vertical lever's up"), Up.IsSet() && *Up == EKeys::SpaceBar);
    TestTrue(TEXT("and C to its down"), Down.IsSet() && *Down == EKeys::C);
    const UClass* Blueprint = LoadClass<ADeepSpaceCharacter>(nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));
    TestTrue(TEXT("and BP_DeepSpaceCharacter carries both actions"), Blueprint
             && Blueprint->GetDefaultObject<ADeepSpaceCharacter>()->HasVerticalActions());

    FSkyWorld Test(TEXT("PlaytestKeysLiftWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody* Fourth = HomeBody(*this, Here, 4, TEXT("Baemsekai IV"));
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>();
    if (!Fourth || !TestNotNull(TEXT("the pilot spawns"), Player))
    {
        return false;
    }
    PlaceOver(Ship, *Fourth, 2.0e5);
    Ship->SetPilot(Player);
    Frame(Player, Ship, 0.0f);
    const FShipFlightState& Flight = Ship->GetFlightState();

    Player->TapVertical(1);
    Player->HoldVertical(1);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    Player->HoldVertical(0);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    TestTrue(FString::Printf(TEXT("Space pressed and held a second: the ship climbs (%.2f m/s)"), Flight.GetVerticalSpeed() / 100.0),
             Flight.GetVerticalSpeed() > 0.0);
    TestTrue(TEXT("the motion line says CLIMB"), UShipHUDWidget::MotionLineOf(*Ship).Ink.Contains(TEXT("CLIMB")));

    Player->TapVertical(-1);
    Player->HoldVertical(-1);
    for (int32 Tick = 0; Tick < 300; ++Tick) { Frame(Player, Ship); }
    Player->HoldVertical(0);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    TestTrue(TEXT("C held stops at HOVER from a climb"), Flight.GetCommand().Vertical == 0.0);
    Player->TapVertical(-1);
    Player->HoldVertical(-1);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    Player->HoldVertical(0);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    TestTrue(TEXT("and a fresh C sinks"), Flight.GetVerticalSpeed() < 0.0);
    return true;
}

#endif
```

- [ ] **Step 2: Run and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh
```

Expected: fails to compile: `'TapVertical': is not a member of 'ADeepSpaceCharacter'`.

- [ ] **Step 3: The pawn's hands**

`DeepSpaceCharacter.h`, public after `HoldLever`:

```cpp
    /** A vertical key pressed (+1 Space, -1 C), counted as a press: what
     *  IA_VerticalUp and IA_VerticalDown's Started do. Public for tests. */
    void TapVertical(int32 Direction);

    /** Which vertical key is held: +1 Space, -1 C, 0 neither. */
    void HoldVertical(int32 Direction);

    /** Both vertical actions are assigned: what setup_flight_input.py
     *  guarantees on the Blueprint, asked by the playtest. */
    bool HasVerticalActions() const { return VerticalUpAction && VerticalDownAction; }
```

after `LeverDownAction`:

```cpp
    /**
     * The vertical lever's keys (Space up, C down; landing decision 8),
     * Boolean, counted and held like the lever keys. Seated they fly and
     * standing they jump and crouch: movement is disabled seated, and Jump
     * and ToggleCrouch refuse in any seat. See Tools/setup_flight_input.py.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> VerticalUpAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> VerticalDownAction;
```

private handlers after `ReleaseLeverDown`:

```cpp
    void PressVerticalUp() { TapVertical(1); }
    void PressVerticalDown() { TapVertical(-1); }
    void HoldVerticalUp() { bVerticalUpHeld = true; }
    void ReleaseVerticalUp() { bVerticalUpHeld = false; }
    void HoldVerticalDown() { bVerticalDownHeld = true; }
    void ReleaseVerticalDown() { bVerticalDownHeld = false; }

    /** Jump, refused in any seat: Space is the vertical lever there. */
    void TryJump();
```

state after `LeverDownPresses`:

```cpp
    bool bVerticalUpHeld = false;
    bool bVerticalDownHeld = false;
    int32 VerticalUpPresses = 0;
    int32 VerticalDownPresses = 0;
```

`DeepSpaceCharacter.cpp`: change the `JumpAction` Started binding to
`&ADeepSpaceCharacter::TryJump`; after the `LeverDownAction` block add:

```cpp
    if (VerticalUpAction)
    {
        Input->BindAction(VerticalUpAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::PressVerticalUp);
        Input->BindAction(VerticalUpAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::HoldVerticalUp);
        Input->BindAction(VerticalUpAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::ReleaseVerticalUp);
        Input->BindAction(VerticalUpAction, ETriggerEvent::Canceled, this, &ADeepSpaceCharacter::ReleaseVerticalUp);
    }
    if (VerticalDownAction)
    {
        Input->BindAction(VerticalDownAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::PressVerticalDown);
        Input->BindAction(VerticalDownAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::HoldVerticalDown);
        Input->BindAction(VerticalDownAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::ReleaseVerticalDown);
        Input->BindAction(VerticalDownAction, ETriggerEvent::Canceled, this, &ADeepSpaceCharacter::ReleaseVerticalDown);
    }
```

`ToggleCrouch`: change `if (IsSeated())` to `if (IsSeated() || IsInScreenChair())`. Add:

```cpp
void ADeepSpaceCharacter::TryJump()
{
    // Seated, Space is the vertical lever's; the movement component would
    // refuse a jump anyway, but saying so here is what keeps the two keys'
    // meanings from depending on that.
    if (IsSeated() || IsInScreenChair())
    {
        return;
    }
    Jump();
}

void ADeepSpaceCharacter::TapVertical(int32 Direction)
{
    if (Direction > 0)
    {
        ++VerticalUpPresses;
    }
    else if (Direction < 0)
    {
        ++VerticalDownPresses;
    }
}

void ADeepSpaceCharacter::HoldVertical(int32 Direction)
{
    bVerticalUpHeld = Direction > 0;
    bVerticalDownHeld = Direction < 0;
}
```

`PushHelmInput`, after `Input.DownPresses = LeverDownPresses;`:

```cpp
    Input.bVerticalUpHeld = bVerticalUpHeld;
    Input.bVerticalDownHeld = bVerticalDownHeld;
    Input.VerticalUpPresses = VerticalUpPresses;
    Input.VerticalDownPresses = VerticalDownPresses;
    VerticalUpPresses = 0;
    VerticalDownPresses = 0;
```

- [ ] **Step 4: The actions and bindings**

In `Tools/setup_flight_input.py`: in the docstring after the `F` line add

```text
    Space / C   the vertical lever, up and down (IA_VerticalUp,
            IA_VerticalDown; landing decision 8): a climb or sink rate the
            boosters hold, HOVER at zero. Seated they fly; standing they are
            jump and crouch, which refuse in any seat.
```

`PRESS_KEYS` gains `"IA_VerticalUp": "SpaceBar", "IA_VerticalDown": "C",`; `PROPERTIES` gains
`"IA_VerticalUp": "vertical_up_action", "IA_VerticalDown": "vertical_down_action",`; and:

```python
# What a helm key may share, and only on the lever keys: the walking actions,
# which do nothing while seated, as W/A/S/D share with IA_Move. Shift is
# sprint standing and the lever seated; Space and C are jump and crouch
# standing and the vertical lever seated (the character refuses both in any
# seat, so a seated press is only ever the lever).
SHARES_WITH_WALKING = {"LeftShift", "LeftControl", "SpaceBar", "C"}
```

- [ ] **Step 5: Build, run the script, run the playtest**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh && \
. Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/setup_flight_input.py" -unattended -nopause -nosplash -NoLiveCoding; \
tail -5 Saved/setup_flight_input.txt && \
./test.sh DeepSpace.Playtest.KeysLiftTheShip && ./test.sh DeepSpace.Playtest.KeysTurnTheShip && ./test.sh DeepSpace.Player
```

Expected: the script's report ends `DONE`, with `SpaceBar is free in IMC_Default but for walking`
and `C is free ... but for walking`; `KeysLiftTheShip` passes; the attitude keys and every
`DeepSpace.Player.*` test are unchanged. Then run the Blueprint check (a `UPROPERTY` was added and
the Blueprint recompiled):

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && . Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/check_blueprints.py" -unattended -nopause -nosplash -NoLiveCoding; echo "exit $?"
```

Expected: `exit 0`.

- [ ] **Step 6: Document the keys**

In `CLAUDE.md`, *Flying*'s table, after the `X` row:

```markdown
| Space / C | the vertical lever: climb / sink, HOVER at zero (near a world) | `IA_VerticalUp`, `IA_VerticalDown` |
```

- [ ] **Step 7: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
git add Source/DeepSpace/Player/DeepSpaceCharacter.h Source/DeepSpace/Player/DeepSpaceCharacter.cpp Tools/setup_flight_input.py \
        Source/DeepSpace/Tests/PlaytestLandingTest.cpp CLAUDE.md Content/Input Content/Blueprints/BP_DeepSpaceCharacter.uasset && \
git commit -m "$(cat <<'EOF'
feat(input): Space and C fly the vertical lever at the helm; jump and crouch refuse in any seat

IA_VerticalUp (SpaceBar) and IA_VerticalDown (C), built and bound by
setup_flight_input.py, which now lets Space and C share with the walking
actions they replace seated. The pawn counts presses and holds as it does
for the lever keys. DeepSpace.Playtest.KeysLiftTheShip reads IMC_Default
and flies both keys (landing decision 8, sign-off 18).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 8: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
Tools/mutate.sh Source/DeepSpace/Player/DeepSpaceCharacter.cpp 'Input.VerticalUpPresses = VerticalUpPresses;' 'Input.VerticalUpPresses = 0;' DeepSpace.Playtest.KeysLiftTheShip && \
./build.sh
```

Expected: `KILLED` (no fresh press ever leaves HOVER).

---

## Task 28 (S6): the HUD below the floor -- altitude above ground, vertical speed, the vertical lever's words, DRIVE ABOVE THE FLOOR

**Owner:** S. **Depends on:** S4 (F7 merged). Spec decision 12.

**Files:**
- Modify: `Source/DeepSpace/UI/ShipHUDWidget.h` (after `AltitudeLine`, about line 171),
  `ShipHUDWidget.cpp` (`AltitudeWords` lines 462-499, `AltitudeLineText` lines 533-551,
  `MotionLine` lines 679-702, new functions)
- Create: `Source/DeepSpace/Tests/ShipHUDGroundTest.cpp` (`DeepSpace.UI.HUDGround`)
- Modify: `Source/DeepSpace/Tests/ShipHUDAltitudeTest.cpp` lines 38-39 (the two readings under 10 m)

**Interfaces:**
- Consumes: `FShipFlightState::{IsInNearRegime, GetGroundAltitude, GetVerticalSpeed, IsVerticalLive,
  GetVerticalLeverRate, GetRegimeWeight, GetMode}`.
- Produces: `static FString UShipHUDWidget::GroundLine(double GroundAltitudeCm, double VerticalSpeedCmPerSecond, EFlightHold Hold)`,
  `static FString UShipHUDWidget::VerticalWords(double CmPerSecond)`,
  `static FString UShipHUDWidget::VerticalLeverWords(double RateCmPerSecond)`.

- [ ] **Step 1: Write the failing test**

Create `Source/DeepSpace/Tests/ShipHUDGroundTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipGravity.h"
#include "Tests/GroundFixtures.h"
#include "UI/NavText.h"
#include "UI/ShipHUDWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 12: below the floor the corner says how high the ship is
 * over the rock and how it is moving vertically, and the motion line names
 * the third lever -- words from getters, never recomputed in the widget. No
 * colour, nothing that blinks, no percentages, no time to the ground.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipHUDGroundTest, "DeepSpace.UI.HUDGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipHUDGroundTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    const FString Sep = NavText::Separator;
    TestEqual(TEXT("tenths under ten metres"), UShipHUDWidget::AltitudeWords(150.0), FString(TEXT("1.5 M")));
    TestEqual(TEXT("whole metres from ten"), UShipHUDWidget::AltitudeWords(84000.0), FString(TEXT("840 M")));
    TestEqual(TEXT("a sink"), UShipHUDWidget::VerticalWords(-300.0), FString(TEXT("SINKING 3 M/S")));
    TestEqual(TEXT("a slow sink, in tenths"), UShipHUDWidget::VerticalWords(-50.0), FString(TEXT("SINKING 0.5 M/S")));
    TestEqual(TEXT("a climb"), UShipHUDWidget::VerticalWords(1200.0), FString(TEXT("CLIMBING 12 M/S")));
    TestEqual(TEXT("under a tenth either way is hovering"), UShipHUDWidget::VerticalWords(4.0), FString(TEXT("HOVERING")));
    TestEqual(TEXT("the corner hovering"), UShipHUDWidget::GroundLine(150.0, 0.0, EFlightHold::Free),
              FString(TEXT("1.5 M ABOVE GROUND")) + Sep + TEXT("HOVERING"));
    TestEqual(TEXT("the corner sinking"), UShipHUDWidget::GroundLine(84000.0, -300.0, EFlightHold::Free),
              FString(TEXT("840 M ABOVE GROUND")) + Sep + TEXT("SINKING 3 M/S"));
    TestEqual(TEXT("and held off a ridge, or by the skim cap"), UShipHUDWidget::GroundLine(84000.0, 0.0, EFlightHold::HoldingOff),
              FString(TEXT("840 M ABOVE GROUND")) + Sep + TEXT("HOVERING") + Sep + TEXT("HOLDING OFF"));
    TestFalse(TEXT("no time to the ground in the corner"), UShipHUDWidget::GroundLine(84000.0, -300.0, EFlightHold::Free).Contains(TEXT("ETA")));
    TestEqual(TEXT("the lever at zero"), UShipHUDWidget::VerticalLeverWords(0.0), FString(TEXT("HOVER")));
    TestEqual(TEXT("the lever climbing"), UShipHUDWidget::VerticalLeverWords(500.0), FString(TEXT("CLIMB 5 M/S")));
    TestEqual(TEXT("the lever sinking"), UShipHUDWidget::VerticalLeverWords(-300.0), FString(TEXT("SINK 3 M/S")));

    // A flight state, in the regime, hovering: the motion line names HOVER.
    const FGroundFieldRef Swells = MakeShared<FCrossedSines, ESPMode::ThreadSafe>(UniverseUnits::CmPerEarthRadius, 100.0, 1.0e5);
    const FFlightSurface World = SurfaceOver(Swells, 1.02e6 + 100.0);
    const FVector3d D(0.0, 0.0, 1.0);
    FShipFlightState Flight;
    Flight.SetSurfaces({ World });
    Flight.SetWells({ { World.Centre, ShipFlight::StandardGravityCmS2 * World.Radius * World.Radius, World.Radius } });
    Flight.SetUniverseTransform(Above(World, D, 2.0e5), Level(D));
    Flight.Step(1.0 / 60.0);
    TestTrue(TEXT("in the regime the motion line names HOVER, in ink"), UShipHUDWidget::MotionLine(Flight).Ink.EndsWith(Sep + TEXT("HOVER")));

    // F there: cruise in ink, and the corner's words for the mode.
    FShipFlightCommand Command = Flight.GetCommand();
    Command.bDrive = true;
    Command.DriveNotch = 3;
    Flight.SetCommand(Command);
    Flight.Step(1.0 / 60.0);
    const UShipHUDWidget::FMotionWords Words = UShipHUDWidget::MotionLine(Flight);
    TestTrue(FString::Printf(TEXT("DRIVE ABOVE THE FLOOR, with cruise in ink (\"%s\" / \"%s\")"), *Words.Ink, *Words.Dim),
             Words.Ink.Contains(TEXT("CRUISE")) && Words.Ink.Contains(TEXT("DRIVE ABOVE THE FLOOR")) && Words.Dim.Contains(TEXT("DRIVE")));

    // Above the regime the lever is dim, and says why.
    Command.bDrive = false;
    Command.Vertical = 1.0;
    Flight.SetCommand(Command);
    Flight.SetUniverseTransform(Above(World, D, 5.2e6), Level(D));
    Flight.Step(1.0 / 60.0);
    Flight.SetUniverseTransform(Above(World, D, 4.95e6 + 150.0), Level(D));
    Flight.Step(1.0 / 60.0);
    Flight.SetUniverseTransform(Above(World, D, 5.2e6), Level(D));
    Flight.Step(1.0 / 60.0);
    TestTrue(FString::Printf(TEXT("in the regime's hysteresis band, the lever dim and ABOVE THE GROUND'S REACH (\"%s\")"), *UShipHUDWidget::MotionLine(Flight).Dim),
             UShipHUDWidget::MotionLine(Flight).Dim.Contains(TEXT("CLIMB 200 M/S")) && UShipHUDWidget::MotionLine(Flight).Dim.Contains(TEXT("ABOVE THE GROUND'S REACH")));
    return true;
}

#endif
```

- [ ] **Step 2: Run and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh
```

Expected: fails to compile: `'GroundLine': is not a member of 'UShipHUDWidget'`.

- [ ] **Step 3: The words**

`ShipHUDWidget.h`, after `AltitudeLine`:

```cpp
    /**
     * The corner below the regime's top over solid ground (landing decision
     * 12): "840 M ABOVE GROUND · SINKING 3 M/S", "· HOVERING", "· CLIMBING
     * 12 M/S", and HOLDING OFF for the soft cap against a ridge or the skim
     * cap. The altitude is the ship's origin over the ground directly below,
     * clearance included: "1.5 M ABOVE GROUND" at rest over flat ground. No
     * time to the ground: this corner has no destination. Pure.
     */
    static FString GroundLine(double GroundAltitudeCm, double VerticalSpeedCmPerSecond, EFlightHold Hold);

    /** The measured vertical speed: HOVERING under a tenth of a metre a
     *  second, then SINKING or CLIMBING in tenths under ten, whole above. */
    static FString VerticalWords(double CmPerSecond);

    /** What the vertical lever asks: HOVER, CLIMB 5 M/S, SINK 3 M/S. */
    static FString VerticalLeverWords(double RateCmPerSecond);
```

`ShipHUDWidget.cpp`: in `AltitudeWords`, replace its first `if` with:

```cpp
    const int64 TenthsMetres = FMath::RoundToInt64(Metres * 10.0);
    if (TenthsMetres < 100)
    {
        // Tenths under ten metres: the gear's 1.5 m is the number that says
        // the ship is sitting just over the rock.
        return FString::Printf(TEXT("%lld.%lld M"), static_cast<long long>(TenthsMetres / 10), static_cast<long long>(TenthsMetres % 10));
    }
    if (WholeMetres < 1000)
```

(keeping `const int64 WholeMetres = ...` above it). Add, after `ShownHold`:

```cpp
namespace
{
    /** A vertical rate's magnitude as a person says it: tenths under ten
     *  metres a second, zeros dropped, whole above. */
    FString VerticalNumber(double CmPerSecond)
    {
        const double Metres = FMath::Abs(CmPerSecond) * 0.01;
        const int64 Tenths = FMath::RoundToInt64(Metres * 10.0);
        if (Tenths < 100)
        {
            return Tenths % 10 == 0 ? FString::Printf(TEXT("%lld M/S"), static_cast<long long>(Tenths / 10))
                                    : FString::Printf(TEXT("%lld.%lld M/S"), static_cast<long long>(Tenths / 10), static_cast<long long>(Tenths % 10));
        }
        return FString::Printf(TEXT("%lld M/S"), static_cast<long long>(FMath::RoundToInt64(Metres)));
    }
}

FString UShipHUDWidget::VerticalWords(double CmPerSecond)
{
    if (FMath::RoundToInt64(FMath::Abs(CmPerSecond) * 0.1) == 0)
    {
        return TEXT("HOVERING");
    }
    return (CmPerSecond < 0.0 ? FString(TEXT("SINKING ")) : FString(TEXT("CLIMBING "))) + VerticalNumber(CmPerSecond);
}

FString UShipHUDWidget::VerticalLeverWords(double RateCmPerSecond)
{
    if (RateCmPerSecond == 0.0)
    {
        return TEXT("HOVER");
    }
    return (RateCmPerSecond < 0.0 ? FString(TEXT("SINK ")) : FString(TEXT("CLIMB "))) + VerticalNumber(RateCmPerSecond);
}

FString UShipHUDWidget::GroundLine(double GroundAltitudeCm, double VerticalSpeedCmPerSecond, EFlightHold Hold)
{
    FString Line = AltitudeWords(GroundAltitudeCm) + TEXT(" ABOVE GROUND") + NavText::Separator + VerticalWords(VerticalSpeedCmPerSecond);
    if (Hold != EFlightHold::Free)
    {
        // Against the ground there is no floor to be at: held against a
        // ridge or by the skim cap, the ship is holding off.
        Line += NavText::Separator;
        Line += TEXT("HOLDING OFF");
    }
    return Line;
}
```

In `AltitudeLineText(const UShipSubsystem&, const FSkySystem&)`, after the transit check and the
`Flight` line, add:

```cpp
    // Below the regime's top over solid ground the corner reads the rock --
    // unless the drive flies the ship, which the ground never does: its
    // approach ends AT THE FLOOR, and the corner keeps saying so (spec,
    // decision 5: "at rest AT THE FLOOR, where every drive approach ends").
    if (Flight.IsInNearRegime() && Flight.GetMode() != EFlightMode::Drive)
    {
        if (const TOptional<double> Agl = Flight.GetGroundAltitude())
        {
            return FText::FromString(GroundLine(*Agl, Flight.GetVerticalSpeed(), ShownHold(Flight.GetHold(), Flight.GetHeldFraction())));
        }
    }
```

In `MotionLine`, before `return Words;`:

```cpp
    if (Mode == EFlightMode::DriveBelowFloor)
    {
        Words.Ink += NavText::Separator;
        Words.Ink += TEXT("DRIVE ABOVE THE FLOOR");
    }
    // The third lever: in ink where it moves the ship, dim where it does not,
    // and ABOVE THE GROUND'S REACH where the regime's blend has taken it out.
    const FString Vertical = VerticalLeverWords(Flight.GetVerticalLeverRate());
    if (Flight.IsVerticalLive())
    {
        Words.Ink += NavText::Separator + Vertical;
    }
    else if (Flight.GetCommand().Vertical != 0.0)
    {
        Words.Dim += NavText::Separator + Vertical;
        if (Flight.IsInNearRegime() && Mode != EFlightMode::Drive)
        {
            Words.Dim += NavText::Separator;
            Words.Dim += TEXT("ABOVE THE GROUND'S REACH");
        }
    }
```

- [ ] **Step 4: Run: PASS; update what pinned whole metres**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh && ./test.sh DeepSpace.UI.HUDGround && ./test.sh DeepSpace.UI.HUD
```

Expected: `HUDGround` passes, and `DeepSpace.UI.HUDAltitude` fails at exactly two lines, the two
readings under 10 m, which now print tenths. In `ShipHUDAltitudeTest.cpp` change

```cpp
    TestEqual(TEXT("on the surface"), Words(0.0), FString(TEXT("0 M")));
    TestEqual(TEXT("never below it"), Words(-5.0e4), FString(TEXT("0 M")));
```

to

```cpp
    TestEqual(TEXT("on the surface"), Words(0.0), FString(TEXT("0.0 M")));
    TestEqual(TEXT("never below it"), Words(-5.0e4), FString(TEXT("0.0 M")));
```

Nothing else there moves. Its world half flies over Baemsekai I, which is solid: at 250 km the ship
is above the regime; at the drive floor + 1 km, in the regime, the corner reads the ground
(`... ABOVE GROUND · HOVERING`), which ends with neither `AT THE FLOOR` nor `HOLDING OFF`, as the
test asks; and with the drive's lever up, settled onto the floor, the mode is `Drive`, so the corner
keeps `AT THE FLOOR` (line 154), which is what the `GetMode() != EFlightMode::Drive` clause above is
for. Re-run `./test.sh DeepSpace.UI.HUD`: green.

- [ ] **Step 5: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
git add Source/DeepSpace/UI/ShipHUDWidget.h Source/DeepSpace/UI/ShipHUDWidget.cpp Source/DeepSpace/Tests/ShipHUDGroundTest.cpp && \
git add -u Source/DeepSpace/Tests/ShipHUDAltitudeTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(hud): below the floor -- altitude above ground, vertical speed, the vertical lever, DRIVE ABOVE THE FLOOR

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
Tools/mutate.sh Source/DeepSpace/UI/ShipHUDWidget.cpp 'if (TenthsMetres < 100)' 'if (false)' DeepSpace.UI.HUDGround && \
Tools/mutate.sh Source/DeepSpace/UI/ShipHUDWidget.cpp 'if (Flight.IsVerticalLive())' 'if (false)' DeepSpace.UI.HUDGround && \
Tools/mutate.sh Source/DeepSpace/UI/ShipHUDWidget.cpp 'if (Flight.IsInNearRegime() && Flight.GetMode() != EFlightMode::Drive)' 'if (Flight.IsInNearRegime())' DeepSpace.UI.HUDAltitude && \
./build.sh
```

Expected: `KILLED` three times (the third: the drive at a solid world's floor would read the ground,
and lose `AT THE FLOOR`).

---

## Task 29 (S7): the target's ETA counts to the ground

**Owner:** S. **Depends on:** S1, F2. Spec decision 12 (*The target line's ETA*), sign-off 13.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.cpp` (`GetTargetView`, lines 1109-1128)
- Create: `Source/DeepSpace/Tests/TargetGroundEtaTest.cpp` (`DeepSpace.UI.TargetMarker.GroundEta`)
- Modify: `Source/DeepSpace/Tests/PlaytestTest.cpp` (`EtaCountsDown`: the cruise leg S1 cut, restored
  onto the ground)
- Modify: `CLAUDE.md` -- *The system map and the target*, the ETA paragraph (about line 783)

**Interfaces:**
- Consumes: `ShipFlight::RayToGround`, `SecondsToGround`, `FGroundLaw`; `FWorldFix::{Ground, Relief}`.
- Produces: the view's `EtaSeconds` to the ground whenever the lever flying is cruise's and the
  target is solid.

- [ ] **Step 1: Write the failing test**

Create `Source/DeepSpace/Tests/TargetGroundEtaTest.cpp`:

```cpp
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Surface/GroundField.h"
#include "Tests/SkyTestWorld.h"
#include "UI/TargetMarker.h"
#include "Universe/UniverseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Sign-off item 13: in cruise over a solid target the ETA names the moment
 * the ship reaches the ground, at every altitude -- never the drive floor,
 * which a cruising ship now passes through with nothing happening. Under
 * the drive it is still the drive floor's.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTargetGroundEtaTest, "DeepSpace.UI.TargetMarker.GroundEta",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTargetGroundEtaTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("TargetGroundEtaWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    const TOptional<FStarSystem> Home = Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
    if (!TestTrue(TEXT("the ship is home"), Home.IsSet()))
    {
        return false;
    }
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody& Fourth = Here.Bodies[4];
    TestTrue(TEXT("home's IV is solid"), Fourth.Ground == EGround::Solid);
    Ship->SetTarget(FBodyId{ Home->Stub.Id, 3, -1 });

    const FGroundFieldRef Ground = ShipGround::FromRelief(Fourth.Relief);
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const FVector3d D(Out);
    const double Local = Ground->Height(D, 0.0);
    Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + Local + 3.0e6),
                    FRotationMatrix::MakeFromXZ(FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal(), Out).ToQuat());
    Ship->SetVerticalLever(Pilot, -1.0);
    for (int32 Frame = 0; Frame < 120; ++Frame)
    {
        Test.Step(1.0f / 60.0f);
    }
    const TOptional<FTargetView> View = Ship->GetTargetView(*Home);
    if (!TestTrue(TEXT("the target has an ETA, sinking at 200 m/s from 30 km"), View.IsSet() && View->EtaSeconds.IsSet()))
    {
        return false;
    }
    const double Agl = Ship->GetFlightState().GetFootprintClearance().Get(0.0);
    const double Expected = (Agl - 8.0e4) / 2.0e4 + 4.0 * FMath::Loge(8.0e4 / 200.0) + 4.0;
    TestTrue(FString::Printf(TEXT("and it is the time to the ground, (H - 800 m) / 200 m/s + 28 s (%.1f vs %.1f s)"), *View->EtaSeconds, Expected),
             FMath::IsNearlyEqual(*View->EtaSeconds, Expected, 0.05 * Expected));
    TestTrue(TEXT("not the drive floor's, which is sooner"), *View->EtaSeconds > (Agl - (UShipSubsystem::FloorFor(Fourth) - Local)) / 2.0e4 + 5.0);
    return true;
}

#endif
```

- [ ] **Step 2: Run and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh && ./test.sh DeepSpace.UI.TargetMarker.GroundEta
```

Expected: FAIL on `and it is the time to the ground` (the ETA counts to the drive floor sphere).

- [ ] **Step 3: The ground's ETA**

Replace `GetTargetView`'s body after the `Fix` check with:

```cpp
    // Which law depends on the lever flying, not on the mode's name: the
    // drive (and its spool-down) holds 4 s off its floor before it brakes;
    // cruise -- and DriveBelowFloor, which flies cruise -- brakes on the curve
    // alone, and over solid ground flies to the ground itself.
    const EFlightMode Mode = FlightState.GetMode();
    const bool bCruiseFlies = Mode == EFlightMode::Cruise || Mode == EFlightMode::DriveBelowFloor;
    const FShipFlightLimits& Limits = FlightState.GetLimits();
    TOptional<FTargetView> View = TargetMarker::View(Here, *Target, FlightState.GetUniversePosition(), FlightState.GetUniverseOrientation(),
                                                     FlightState.GetVelocity(), Fix->Floor, Limits.LinearAcceleration,
                                                     bCruiseFlies ? 0.0 : Limits.HoldSeconds, NavState.IsInTransit());
    const double Speed = FlightState.GetSpeed();
    if (View && bCruiseFlies && Fix->Ground == EGround::Solid && Speed >= TargetMarker::MinSpeed)
    {
        // Decision 12: to the ground under the laws the ship is flying --
        // the approach law and the skim cap -- measured along the velocity.
        FFlightSurface Surface;
        Surface.Centre = Fix->Centre;
        Surface.Radius = Fix->Radius;
        Surface.Floor = Fix->Floor;
        Surface.bWorld = true;
        Surface.Ground = ShipGround::FromRelief(Fix->Relief);
        const FVector Along = FlightState.GetVelocity() / Speed;
        const double Reach = FlightState.GetUniversePosition().DistanceTo(Fix->Centre);
        if (const TOptional<double> Hit = ShipFlight::RayToGround(Surface, FlightState.GetUniversePosition(), Along, Limits.GearClearanceCm, 2.0 * Reach))
        {
            const FVector Up = (FlightState.GetUniversePosition() - Fix->Centre).GetSafeNormal();
            const ShipFlight::FGroundLaw Law{ Limits.LinearAcceleration, Limits.ApproachSeconds, Limits.TouchdownSpeed,
                                              Limits.SkimSeconds, Limits.SkimFloor };
            View->EtaSeconds = ShipFlight::SecondsToGround(*Hit, Speed, FMath::Max(-(Along | Up), 1.0e-6), Law);
            View->PassingCm.Reset();
        }
    }
    return View;
```

(`TargetMarker::MinSpeed` is the existing 1 m/s threshold.)

- [ ] **Step 4: Run: PASS, and the marker's own tests**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && ./build.sh && ./test.sh DeepSpace.UI.TargetMarker && ./test.sh DeepSpace.Ship.Target
```

Expected: all green, `GroundEta` included. (`DeepSpace.Ship.Target`'s ETA flights fly the drive,
whose ETA is still the drive floor's, so none of them moves; the one cruise flight onto a rocky world
with an ETA is `EtaCountsDown`'s leg, restored in Step 4b.)

- [ ] **Step 4b: Restore `EtaCountsDown`'s cruise leg, onto the ground**

In `PlaytestTest.cpp`, replace the line S1 left,
`    // The cruise leg flies to the ground: Task 29 (S7) restores it with the ETA to the ground.`, with:

```cpp
    // -- Cruise, on starved boosters, to the ground ------------------------------
    // Cruise brakes on the curve alone, with no hold, so its time is that
    // law's. Over a solid world it flies to the ground, and the ETA names
    // the moment it arrives there (sign-off item 13). The regime is off for
    // the leg: inside it cruise flies the nose's plan view and never dives
    // (decision 8), and this leg is about the law, not the lever. At a
    // quarter thrust cruise takes 40 s and 400 km to reach its top, and
    // brakes from it over 500 km, so the flight starts 1,200 km up.
    SkyTestWorld::FScopedCVar NoRegime(TEXT("ds.Land.Regime"), 0.0f);
    Ship->AllStop(Pilot);
    Ship->SetDriveEngaged(Pilot, false);
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    {
        const FVector Out = (Flight.GetUniversePosition() - World.Centre).GetSafeNormal();
        Ship->PlaceShip(World.Centre + Out * (World.Radius + World.Floor + 1.2e8), Facing(-Out));
    }
    Ship->Tick(Dt);
    TestTrue(TEXT("the boosters are starved"), Flight.GetLimits().LinearAcceleration < 0.3 * FShipFlightLimits::Cruise().LinearAcceleration);
    TestTrue(TEXT("and the ship cruises"), Flight.GetMode() == EFlightMode::Cruise);
    Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);

    TArray<FReading> Cruising;
    Seconds = 0.0;
    Arrived = -1.0;
    Tick = 0;
    while (Seconds < 300.0 && Arrived < 0.0)
    {
        Ship->Tick(Dt);
        Seconds += Dt;
        ++Tick;
        const bool bSettled = Flight.GetHold() != EFlightHold::Free || Flight.GetSpeed() >= 0.999 * Flight.GetLimits().MaxSpeed;
        if (Tick % 30 == 0 && bSettled)
        {
            const TOptional<FTargetView> View = Ship->GetTargetView(*Home);
            if (View && View->EtaSeconds)
            {
                Cruising.Add({ Seconds, *View->EtaSeconds, UShipHUDWidget::TargetLineText(*Ship, Home).ToString(),
                               Flight.GetHold() != EFlightHold::Free });
            }
        }
        // On the ground: a foot within a centimetre, and at rest.
        if (Flight.GetFootprintClearance().Get(TNumericLimits<double>::Max()) <= 1.0 && Flight.GetSpeed() < TargetMarker::MinSpeed)
        {
            Arrived = Seconds;
        }
    }
    int32 Braking = 0;
    for (const FReading& Reading : Cruising)
    {
        Braking += Reading.bCapped ? 1 : 0;
    }
    if (!TestTrue(FString::Printf(TEXT("the cruising ship reaches the ground (%.1f s) read at its top (%d) and braking (%d)"),
                                  Arrived, Cruising.Num() - Braking, Braking),
                  Arrived > 0.0 && Cruising.Num() - Braking >= 5 && Braking >= 5))
    {
        return false;
    }
    double CruiseStep = 0.0;
    double CruiseArrival = 0.0;
    for (int32 Index = 0; Index < Cruising.Num(); ++Index)
    {
        CruiseArrival = FMath::Max(CruiseArrival, FMath::Abs(Cruising[Index].At + Cruising[Index].Eta - Arrived));
        if (Index > 0)
        {
            const double Fell = Cruising[Index - 1].Eta - Cruising[Index].Eta;
            CruiseStep = FMath::Max(CruiseStep, FMath::Abs(Fell - (Cruising[Index].At - Cruising[Index - 1].At)));
        }
    }
    AddInfo(FString::Printf(TEXT("cruise: first reading \"%s\" at %.0f s; on the ground at %.1f s"), *Cruising[0].Line, Cruising[0].At, Arrived));
    TestTrue(FString::Printf(TEXT("starved cruise counts a second less every second, to a tenth (worst %.3f s)"), CruiseStep), CruiseStep <= 0.1);
    TestTrue(FString::Printf(TEXT("and names when it reaches the ground, to a second (worst %.2f s)"), CruiseArrival), CruiseArrival <= 1.0);
```

(`FScopedCVar` is `SkyTestWorld`'s, which `PlaytestTest.cpp` already includes through
`Tests/SkyTestWorld.h`.) The leg is S1's cut, with four changes: the regime off, arrival on the
ground (footprint within 1 cm, at rest) instead of the floor sphere, 300 s instead of 200 (the
ground is up to 20 km under the floor), and the words. Its two ETA assertions are unchanged. Run
`./build.sh && ./test.sh DeepSpace.Playtest.EtaCountsDown`: passes; with this task's
`GetTargetView` change reverted it fails at `names when it reaches the ground` (the ETA counts to the
drive floor), which Step 7's second mutation shows. It is committed with this task (Step 6).

- [ ] **Step 5: Document**

In `CLAUDE.md`, *The system map and the target*, the paragraph beginning **The ETA is live**: after
its first sentence add: `In cruise over a solid world -- and in DriveBelowFloor, which flies
cruise -- it counts to **the ground**, at every altitude, under the laws the ship flies
(ShipFlight::SecondsToGround: the approach law and the skim cap), because a cruising ship now
passes through the drive floor with nothing happening there; FloorFor's floor is the ETA's only
under the drive and its spool-down.`

- [ ] **Step 6: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
git add Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/Tests/TargetGroundEtaTest.cpp Source/DeepSpace/Tests/PlaytestTest.cpp CLAUDE.md && \
git commit -m "$(cat <<'EOF'
feat(target): in cruise over a solid world the ETA counts to the ground (sign-off 13)

EtaCountsDown's cruise leg, cut in S1, flies to the ground again and holds
the ETA to the moment it arrives there.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'if (View && bCruiseFlies && Fix->Ground == EGround::Solid && Speed >= TargetMarker::MinSpeed)' 'if (false)' DeepSpace.UI.TargetMarker.GroundEta && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'if (View && bCruiseFlies && Fix->Ground == EGround::Solid && Speed >= TargetMarker::MinSpeed)' 'if (false)' DeepSpace.Playtest.EtaCountsDown && \
./build.sh
```

Expected: `KILLED` twice.

---

## Task 30 (S8): the playtest flights -- hover holds, the starved sink, the heavy climb, parked never drifts, the descent's time, the ETA to the ground

**Owner:** S. **Depends on:** S1-S7, F8 (the flight complete).
Spec: *Tests*, *Playtest flights*; slice (b)'s done-when.

**Files:**
- Modify: `Source/DeepSpace/Tests/PlaytestLandingTest.cpp` (add six siblings; `#include "Ship/PilotSeat.h"`
  and `#include "Ship/ShipGravity.h"` beside the others)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` (public, after `GetHoldWattsPerG`:
  `AddWellForTest`; private, beside `GroundCache`: `TArray<FGravityWell> TestWells;`),
  `Source/DeepSpace/Ship/ShipSubsystem.cpp` (`UpdateSurfaces`: the test wells appended)

**Interfaces:** Consumes everything above, and `ADeepSpaceCharacter::{SitIn, PressInteract, IsSeated,
TapVertical, HoldVertical}`, `APilotSeat`. Produces the tests, and
`void UShipSubsystem::AddWellForTest(const FGravityWell& Well)`: a pull added to every body's in
`UpdateSurfaces`, for tests only -- the playtest's synthetic 3.3 g world, which no procgen world near
home is.

- [ ] **Step 0: The test well**

`ShipSubsystem.h`, public, after `static float GetHoldWattsPerG();`:

```cpp
    /** Tests only: a pull added to every body's, every frame, in
     *  UpdateSurfaces -- a synthetic world's gravity where procgen made none
     *  that heavy (HeavyWorldStillClimbs' 3.3 g). Nothing in the game calls it. */
    void AddWellForTest(const FGravityWell& Well) { TestWells.Add(Well); }
```

and private, after `GroundCache`: `TArray<FGravityWell> TestWells;`. In `UpdateSurfaces`, just
before `FlightState.SetSurfaces(MoveTemp(Surfaces));`: `Wells.Append(TestWells);`.

- [ ] **Step 1: Write the flights**

In `PlaytestLandingTest.cpp`, beside the first `IMPLEMENT_SIMPLE_AUTOMATION_TEST`, add:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestHoverHoldsTest, "DeepSpace.Playtest.HoverHoldsWhenPilotStands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestStarvedSinkTest, "DeepSpace.Playtest.StarvedSinkLandsGently",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestHeavyClimbTest, "DeepSpace.Playtest.HeavyWorldStillClimbs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestParkedTest, "DeepSpace.Playtest.ParkedShipNeverDrifts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestDescendsInTimeTest, "DeepSpace.Playtest.DescendsInTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestEtaToGroundTest, "DeepSpace.Playtest.EtaCountsDownToTheGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

and at the end of the file, before `#endif`:

```cpp
bool FPlaytestHoverHoldsTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestHoverWorld"));
    // The helm, spawned before play as every seat is.
    APilotSeat* Helm = Test.World->SpawnActor<APilotSeat>(FVector::ZeroVector, FRotator::ZeroRotator);
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkyBody* Fourth = HomeBody(*this, LocalSystem::Here(Test.World), 4, TEXT("Baemsekai IV"));
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>(FVector(-150.0, 0.0, 100.0), FRotator::ZeroRotator);
    if (!Fourth || !TestNotNull(TEXT("the helm spawns"), Helm) || !TestNotNull(TEXT("the pilot spawns"), Player))
    {
        return false;
    }
    PlaceOver(Ship, *Fourth, 5.0e4);
    Player->SitIn(Helm);
    if (!TestTrue(TEXT("the pilot is at the helm"), Player->IsSeated()))
    {
        return false;
    }
    Frame(Player, Ship, 0.0f);
    const FShipFlightState& Flight = Ship->GetFlightState();

    // The hover set by hand: C for a second, then Space held back up to the
    // detent -- the lever stops at HOVER from a sink.
    Player->TapVertical(-1);
    Player->HoldVertical(-1);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    Player->HoldVertical(0);
    TestTrue(TEXT("C sank the ship"), Flight.GetVerticalSpeed() < 0.0);
    Player->TapVertical(1);
    Player->HoldVertical(1);
    for (int32 Tick = 0; Tick < 300; ++Tick) { Frame(Player, Ship); }
    Player->HoldVertical(0);
    TestEqual(TEXT("Space held from a sink stops at HOVER"), Flight.GetCommand().Vertical, 0.0);
    for (int32 Tick = 0; Tick < 10 * 60; ++Tick) { Frame(Player, Ship); }

    // E, as the player stands: the character's own interact, out of the
    // helm's seat, and the ship's pilot released.
    Player->PressInteract();
    TestFalse(TEXT("E stands the pilot up"), Player->IsSeated());
    const FUniversePosition Start = Flight.GetUniversePosition();
    for (int32 Tick = 0; Tick < 60 * 60; ++Tick)
    {
        Frame(Player, Ship);
    }
    TestEqual(TEXT("standing up left the lever at HOVER"), Flight.GetCommand().Vertical, 0.0);
    TestTrue(FString::Printf(TEXT("a minute after the pilot stands, the hover has not moved (%.3f cm)"),
                             Flight.GetUniversePosition().DistanceTo(Start)),
             Flight.GetUniversePosition().DistanceTo(Start) < 1.0);
    const FString Corner = UShipHUDWidget::AltitudeLineText(*Ship).ToString();
    TestTrue(FString::Printf(TEXT("and the corner says so (\"%s\")"), *Corner), Corner.Contains(TEXT("ABOVE GROUND")) && Corner.Contains(TEXT("HOVERING")));
    TestTrue(TEXT("the hold is paid while it hovers"), Ship->GetHoldWatts() > 0.0f);
    return true;
}

bool FPlaytestStarvedSinkTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestStarvedSinkWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkyBody* Third = HomeBody(*this, LocalSystem::Here(Test.World), 3, TEXT("Baemsekai III"));
    if (!Third)
    {
        return false;
    }
    PlaceOver(Ship, *Third, 3.0e4);
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    const FShipFlightState& Flight = Ship->GetFlightState();
    bool bSaidSinking = false;
    double RestFor = 0.0;
    double Seconds = 0.0;
    for (; Seconds < 400.0 && RestFor < 2.0; Seconds += Dt)
    {
        Test.Step(Dt);
        bSaidSinking |= UShipHUDWidget::AltitudeLineText(*Ship).ToString().Contains(TEXT("SINKING 2 M/S"));
        RestFor = Flight.GetSpeed() < 0.5 ? RestFor + Dt : 0.0;
    }
    TestTrue(TEXT("starved, the ship sinks, and the corner says SINKING 2 M/S -- a fact, not a warning"), bSaidSinking);
    TestTrue(FString::Printf(TEXT("and the ground catches it, at rest %.0f s in"), Seconds), RestFor >= 2.0);
    TestTrue(TEXT("never more than a centimetre in"), Flight.GetGroundLog().LeastClearance >= -1.0);
    TestTrue(FString::Printf(TEXT("touching at no more than 0.5 m/s (%.3f)"), Flight.GetGroundLog().WorstContactSpeed / 100.0),
             Flight.GetGroundLog().WorstContactSpeed <= Flight.GetLimits().TouchdownSpeed * (1.0 + 1e-6));
    const FString Corner = UShipHUDWidget::AltitudeLineText(*Ship).ToString();
    TestTrue(FString::Printf(TEXT("resting with a foot on the ground, and saying so: \"%s\""), *Corner),
             Corner.Contains(TEXT("ABOVE GROUND")) && Corner.Contains(TEXT("HOVERING")) && FMath::Abs(Flight.GetFootprintClearance().Get(-1.0e9)) < 1.0);
    return true;
}

bool FPlaytestHeavyClimbTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestHeavyClimbWorld"));
    APilotSeat* Helm = Test.World->SpawnActor<APilotSeat>(FVector::ZeroVector, FRotator::ZeroRotator);
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkyBody* Third = HomeBody(*this, LocalSystem::Here(Test.World), 3, TEXT("Baemsekai III"));
    if (!Third)
    {
        return false;
    }
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>(FVector(-150.0, 0.0, 100.0), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("the helm spawns"), Helm) || !TestNotNull(TEXT("the pilot spawns"), Player))
    {
        return false;
    }
    Player->SitIn(Helm);
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    const FShipFlightState& Flight = Ship->GetFlightState();
    // Space pressed and held for thirty seconds: the lever sweeps to its top
    // in four, and the climb is whatever gravity lets it be.
    const auto Climb = [&]()
    {
        Frame(Player, Ship, 0.0f);
        Player->TapVertical(1);
        Player->HoldVertical(1);
        double Fastest = 0.0;
        for (int32 Tick = 0; Tick < 60 * 30; ++Tick)
        {
            Frame(Player, Ship);
            Fastest = FMath::Max(Fastest, Flight.GetVerticalSpeed());
        }
        Player->HoldVertical(0);
        return Fastest;
    };

    PlaceOver(Ship, *Third, 1.0e5);
    const double Fastest = Climb();
    const double Gs = Flight.GetLocalGravity().Size() / ShipFlight::StandardGravityCmS2;
    const double Top = ShipVerticalLever::ClimbTop(2.0e4, Gs * ShipFlight::StandardGravityCmS2, 0.25);
    TestTrue(FString::Printf(TEXT("over Baemsekai III (%.2f g), starved, Space climbs at about 100 m/s (%.1f of %.1f)"), Gs, Fastest / 100.0, Top / 100.0),
             FMath::IsNearlyEqual(Fastest, Top, 0.02 * Top) && Top > 9.0e3 && Top < 1.1e4);

    // A synthetic 3.3 g world, decision 5's heaviest (14.9 M_E): no world
    // near home is that heavy, so Baemsekai III's pull is topped up by a
    // well at its centre until the ship, 1 km over its ground, feels 3.3 g.
    PlaceOver(Ship, *Third, 1.0e5);
    Frame(Player, Ship, 0.0f);
    const double R = Flight.GetUniversePosition().DistanceTo(Third->Position);
    Ship->AddWellForTest(FGravityWell{ Third->Position, (3.3 * ShipFlight::StandardGravityCmS2 - Flight.GetLocalGravity().Size()) * R * R, Third->Radius });
    Player->TapVertical(-1);
    Player->HoldVertical(-1);
    for (int32 Tick = 0; Tick < 5 * 60; ++Tick) { Frame(Player, Ship); }   // back to HOVER at the detent
    Player->HoldVertical(0);
    PlaceOver(Ship, *Third, 1.0e5);
    const double Heavy = Climb();
    const double HeavyGs = Flight.GetLocalGravity().Size() / ShipFlight::StandardGravityCmS2;
    const double HeavyTop = ShipVerticalLever::ClimbTop(2.0e4, HeavyGs * ShipFlight::StandardGravityCmS2, 0.25);
    TestTrue(FString::Printf(TEXT("the synthetic world pulls 3.3 g where the climb is flown (%.3f g)"), HeavyGs),
             FMath::IsNearlyEqual(HeavyGs, 3.3, 0.05));
    TestTrue(FString::Printf(TEXT("at 3.3 g, starved, Space climbs at about 61 m/s (%.1f of %.1f)"), Heavy / 100.0, HeavyTop / 100.0),
             FMath::IsNearlyEqual(Heavy, HeavyTop, 0.02 * HeavyTop) && HeavyTop > 5.9e3 && HeavyTop < 6.2e3);
    return true;
}

bool FPlaytestParkedTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestParkedWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody* Fourth = HomeBody(*this, Here, 4, TEXT("Baemsekai IV"));
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (!Fourth || !TestTrue(TEXT("a star on the chart to wind toward"), Chart.Num() > 0))
    {
        return false;
    }
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    Ship->PlotCourse(Chart[0].Id);
    Ship->SetJumpEngaged(true);
    const auto Park = [&](const FUniversePosition& Where, const TCHAR* Label)
    {
        // Facing away from the course, so the charged jump never finds it in
        // the cone and the fold never opens.
        const FVector Away = -Ship->GetCourseDirection().Get(FVector::ForwardVector);
        Ship->PlaceShip(Where, FRotationMatrix::MakeFromX(Away).ToQuat());
        Test.Step(Dt);
        const FUniversePosition Start = Ship->GetFlightState().GetUniversePosition();
        for (int32 Frame = 0; Frame < 1200; ++Frame)
        {
            Test.Step(0.5f);
        }
        const double Drift = Ship->GetFlightState().GetUniversePosition().DistanceTo(Start);
        TestTrue(FString::Printf(TEXT("%s, starved, the jump winding, ten minutes: it drifts %.4f cm"), Label, Drift), Drift < 1.0);
        TestFalse(TEXT("and it never folded"), Ship->IsInTransit());
    };
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth->Position).GetSafeNormal();
    Park(Fourth->Position + Out * (Fourth->Radius + UShipSubsystem::FloorFor(*Fourth) + 100.0), TEXT("at Baemsekai IV's drive floor"));
    Park(Fourth->Position + Out * 0.25 * UniverseUnits::CmPerAU, TEXT("between worlds"));
    return true;
}

bool FPlaytestDescendsInTimeTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestDescentWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkyBody* Fourth = HomeBody(*this, LocalSystem::Here(Test.World), 4, TEXT("Baemsekai IV"));
    if (!Fourth)
    {
        return false;
    }
    const FGroundFieldRef Ground = ShipGround::FromRelief(Fourth->Relief);
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth->Position).GetSafeNormal();
    const double Local = Ground->Height(FVector3d(Out), 0.0);
    const double DriveFloor = UShipSubsystem::FloorFor(*Fourth);
    PlaceOver(Ship, *Fourth, DriveFloor - Local);
    Test.Step(Dt);
    const FShipFlightState& Flight = Ship->GetFlightState();
    const double H = Flight.GetFootprintClearance().Get(0.0);
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetVerticalLever(Pilot, -1.0);
    double Seconds = 0.0;
    for (; Seconds < 400.0 && Flight.GetFootprintClearance().Get(1.0e9) > 1.0; Seconds += Dt)
    {
        Test.Step(Dt);
    }
    const double Expected = (H - 8.0e4) / 2.0e4 + 4.0 * FMath::Loge(8.0e4 / 200.0) + 4.0;
    AddInfo(FString::Printf(TEXT("from %.1f km over the local ground: %.1f s (expected %.1f)"), H / 1.0e5, Seconds, Expected));
    TestTrue(FString::Printf(TEXT("C from the drive floor to the ground in (H - 800 m) / 200 m/s + 28 s, within 10%% (%.1f vs %.1f)"), Seconds, Expected),
             FMath::Abs(Seconds - Expected) <= 0.1 * Expected);
    TestTrue(TEXT("the vision's a minute or two"), Seconds >= 60.0 && Seconds <= 130.0);
    return true;
}

bool FPlaytestEtaToGroundTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestEtaGroundWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const TOptional<FStarSystem> Home = Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
    const FSkyBody* Fourth = HomeBody(*this, LocalSystem::Here(Test.World), 4, TEXT("Baemsekai IV"));
    if (!Home || !Fourth)
    {
        return false;
    }
    Ship->SetTarget(FBodyId{ Home->Stub.Id, 3, -1 });
    PlaceOver(Ship, *Fourth, 3.0e6);
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetVerticalLever(Pilot, -1.0);
    for (int32 Frame = 0; Frame < 30; ++Frame)
    {
        Test.Step(Dt);
    }
    double Previous = -1.0;
    double Worst = 0.0;
    int32 Samples = 0;
    for (int32 Second = 0; Second < 200; ++Second)
    {
        for (int32 Frame = 0; Frame < 60; ++Frame)
        {
            Test.Step(Dt);
        }
        const TOptional<FTargetView> View = Ship->GetTargetView(*Home);
        if (!View || !View->EtaSeconds || *View->EtaSeconds < 10.0)
        {
            break;
        }
        if (Previous > 0.0)
        {
            Worst = FMath::Max(Worst, FMath::Abs((Previous - *View->EtaSeconds) - 1.0));
            ++Samples;
        }
        Previous = *View->EtaSeconds;
    }
    TestTrue(FString::Printf(TEXT("from above the drive floor to the ground the ETA falls a second a second (%d samples, worst %.3f s off)"), Samples, Worst),
             Samples > 30 && Worst <= 0.1);
    return true;
}
```

- [ ] **Step 2: Run them**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && git merge -q feat/landing-b && ./build.sh && ./test.sh DeepSpace.Playtest
```

Expected: every `DeepSpace.Playtest.*` green (these flights exercise behaviour the earlier tasks
built; the only production code in this task is the test well, which nothing in the game calls).
The hover and the heavy climb go through the character's hands: seated at a real `APilotSeat`,
the lever moved by `TapVertical`/`HoldVertical` (what `IA_VerticalUp`/`IA_VerticalDown` call), and
the stand-up through `PressInteract` (E), so the seat's release of the pilot is what is tested. A red flight is a bug in the laws or the
wiring it names, fixed there with its own failing unit test first, never here.

- [ ] **Step 3: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
git add Source/DeepSpace/Tests/PlaytestLandingTest.cpp Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp && \
git commit -m "$(cat <<'EOF'
test(playtest): slice (b)'s flights -- hover holds standing, starved sink lands gently, heavy climb, parked never drifts, descent time, ETA to the ground

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4: Prove the flights can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'Limits.SinkBias = FMath::Max(0.0f, CVarStarvedSink.GetValueOnGameThread()) * 100.0 * (1.0 - LastSplit.HoldFed);' 'Limits.SinkBias = 0.0;' DeepSpace.Playtest.StarvedSinkLandsGently && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'Limits.VerticalHeavyFloor = FMath::Clamp(CVarHeavyFloor.GetValueOnGameThread(), 0.0f, 1.0f);' 'Limits.VerticalHeavyFloor = 1.0;' DeepSpace.Playtest.HeavyWorldStillClimbs && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp '    Wells.Append(TestWells);' '' DeepSpace.Playtest.HeavyWorldStillClimbs && \
./build.sh
```

Expected: `KILLED` three times (the third: without the well the 3.3 g leg flies at III's 1.97 g).

---

## Task 31 (T1): craters as a height -- summed compact kernels in `WorldRelief.ush` and in `Height`

**Owner:** T (slice (b) owns `Surface/WorldRelief.*` for this, spec *Parallel tracks*). **Depends
on:** B0. Spec decision 3 (*Craters must become continuous to be a height*).

**Files:**
- Modify: `Shaders/Private/WorldRelief.ush` (the kernel section, inserted above the entry point
  `WR_SurfaceTerms`)
- Modify: `Source/DeepSpace/Surface/WorldRelief.h` (public: `SlopeScale`, `SMax`; private:
  `CraterBound`, `CraterSlopeBound`, `CraterOmittedBound`), `Source/DeepSpace/Surface/WorldRelief.cpp`
  (`HeightAndGradient`, `Height`, `MaxHeightCm`, `MinHeightCm`, `MaxSlope`, `OmittedBoundCm`)
- Create: `Source/DeepSpace/Tests/WorldReliefCratersTest.cpp`
  (`DeepSpace.Surface.WorldRelief.CratersContinuous`, `.CraterGradient`, `.CraterBounds`)

**Interfaces:**
- Consumes (Tasks R1-R3): the `.ush` subset -- `WR_REAL`, `WR_Hash3 WR_Rand3DPCG16(int, int, int)`,
  `WR_Vec3 WR_VoronoiJitter(WR_REAL, WR_REAL, WR_REAL)`, the shims `WR_floor`, `WR_sqrt`,
  `WR_saturate`, `WR_max`, and the tables `WR_CRATER_BANDS`, `WR_CRATER_FREQUENCY[]`,
  `WR_CRATER_INDEX[]`; the constants `WR_CRATER_RADIUS`, `WR_CRATER_INV_RADIUS`, `WR_CRATER_DEPTH`,
  `WR_CRATER_RIM`, `WR_CRATER_KEEP`, `WR_CRATER_FLOOR_DARK`, `WR_CRATER_RIM_BRIGHT`;
  `double FWorldRelief::DetailSum(const FVector3d& D, double FootprintRadius, FVector3d* Grad = nullptr) const`,
  `double DetailBound() const`, `double DetailSlopeBound() const`,
  `double DetailOmittedBound(double FootprintRadius) const`, the private `FootprintOf` and `Params`.
- Produces: `struct WR_CraterTerm { WR_REAL H; WR_REAL GX; WR_REAL GY; WR_REAL GZ; WR_REAL Albedo; }`,
  `WR_REAL WR_VertexCarries(WR_REAL VertexBandLimit, WR_REAL Frequency)`,
  `WR_CraterTerm WR_CraterKernelBand(WR_REAL PX, WR_REAL PY, WR_REAL PZ)`,
  `WR_CraterTerm WR_CraterSum(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL FootprintRadius, WR_REAL VertexBandLimit)`,
  `WR_CRATER_BOUND_COUNT`; `double FWorldRelief::SlopeScale() const`, `double FWorldRelief::SMax() const`;
  `Height` including `Cratering x` the summed craters.

- [ ] **Step 1: Write the failing tests**

Create `Source/DeepSpace/Tests/WorldReliefCratersTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Surface/WorldRelief.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 3: a crater band as a sum of compact kernels, one per
 * kept site over the 3 x 3 x 3 cells round the sample, so craters are a
 * continuous height -- no cliff where a kept crater's rim meets the bisector
 * with a dropped neighbour, which Voronoi F1 had and the material hid. The
 * bounds, the slope bound and the gradient hold with craters in.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefCratersContinuousTest, "DeepSpace.Surface.WorldRelief.CratersContinuous",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefCraterGradientTest, "DeepSpace.Surface.WorldRelief.CraterGradient",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefCraterBoundsTest, "DeepSpace.Surface.WorldRelief.CraterBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WorldReliefCratersLocal
{
    /** Bare rock, craters kept whole: every crater band is in the height. */
    FWorldReliefParams Barren(double Cratering = 1.0)
    {
        FWorldReliefParams Params;
        Params.SeedOffset = FVector3d(12.5, 40.25, 7.75);
        Params.RadiusCm = 0.9 * UniverseUnits::CmPerEarthRadius;
        Params.PeakCm = 6.4e5;
        Params.Cratering = Cratering;
        Params.Ground = EGround::Solid;
        return Params;
    }

    FVector3d RandomDirection(FRandomStream& Random)
    {
        return FVector3d(Random.GetUnitVector());
    }

    /** A fixed direction where the crater sum is not zero (if a change of
     *  constants ever makes it zero here, move it and say so). */
    FVector3d From0()
    {
        return FVector3d(0.3, -0.5, 0.81).GetSafeNormal();
    }
}

bool FWorldReliefCratersContinuousTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefCratersLocal;
    const FWorldRelief Relief(Barren());
    const double R = Relief.GetParams().RadiusCm;
    // Along twenty great-circle arcs of 0.2 radian, each crossing a couple
    // of the coarsest crater band's cells and many of the finer ones', in
    // steps of 1e-5 radian (57 m): a cliff shows as a step steeper than the
    // slope bound allows -- the coarse bands' cliffs were tens of metres.
    FRandomStream Random(3);
    const double Step = 1.0e-5;
    double Worst = 0.0;
    for (int32 Arc = 0; Arc < 20; ++Arc)
    {
        const FVector3d From = RandomDirection(Random);
        const FVector3d Across = FVector3d::CrossProduct(From, RandomDirection(Random)).GetSafeNormal();
        double Previous = Relief.Height(From, 0.0);
        for (int32 I = 1; I < 20000; ++I)
        {
            const double Angle = I * Step;
            const FVector3d D = From * FMath::Cos(Angle) + Across * FMath::Sin(Angle);
            const double H = Relief.Height(D, 0.0);
            Worst = FMath::Max(Worst, FMath::Abs(H - Previous) / (R * Step));
            Previous = H;
        }
    }
    TestTrue(FString::Printf(TEXT("no step anywhere steeper than MaxSlope (worst %.4f of %.4f)"), Worst, Relief.MaxSlope()),
             Worst <= Relief.MaxSlope());
    TestTrue(TEXT("and the craters are really in the ground: with none kept, the heights differ"),
             Relief.Height(From0(), 0.0) != FWorldRelief(Barren(0.0)).Height(From0(), 0.0));
    return true;
}
```

```cpp
bool FWorldReliefCraterGradientTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefCratersLocal;
    const FWorldRelief Relief(Barren());
    FRandomStream Random(11);
    const double H = 1.0e-8;
    int32 Agree = 0;
    const int32 Trials = 2000;
    for (int32 Trial = 0; Trial < Trials; ++Trial)
    {
        const FVector3d D = RandomDirection(Random);
        FVector3d Grad;
        Relief.HeightAndGradient(D, Grad, 0.0);
        const FVector3d Along = Grad - D * FVector3d::DotProduct(Grad, D);
        const FVector3d T = FVector3d::CrossProduct(D, RandomDirection(Random)).GetSafeNormal();
        const double Numeric = (Relief.Height((D + T * H).GetSafeNormal(), 0.0) - Relief.Height((D - T * H).GetSafeNormal(), 0.0)) / (2.0 * H);
        const double Analytic = FVector3d::DotProduct(Along, T);
        const double Tolerance = 0.01 * FMath::Abs(Analytic) + 1.0e-4 * Relief.GetParams().RadiusCm * Relief.MaxSlope();
        Agree += FMath::Abs(Numeric - Analytic) <= Tolerance ? 1 : 0;
    }
    // A rim crest is a kink (the profile's slope turns there), a set of
    // measure zero that a random sample can still land beside.
    TestTrue(FString::Printf(TEXT("the analytic gradient, craters in, agrees with finite differences (%d of %d)"), Agree, Trials),
             Agree >= Trials * 995 / 1000);
    return true;
}

bool FWorldReliefCraterBoundsTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefCratersLocal;
    const FWorldRelief Relief(Barren());
    TestEqual(TEXT("the drawn peak is MaxHeightCm, exactly"), Relief.MaxHeightCm(), Relief.GetParams().PeakCm);
    TestTrue(TEXT("SlopeScale is the peak over the radius and the sum's bound"),
             FMath::IsNearlyEqual(Relief.SlopeScale(), Relief.GetParams().PeakCm / (Relief.GetParams().RadiusCm * Relief.SMax()), 1e-15));
    FRandomStream Random(5);
    double Highest = -TNumericLimits<double>::Max();
    double Lowest = TNumericLimits<double>::Max();
    for (int32 Trial = 0; Trial < 100000; ++Trial)
    {
        const double Height = Relief.Height(RandomDirection(Random), 0.0);
        Highest = FMath::Max(Highest, Height);
        Lowest = FMath::Min(Lowest, Height);
    }
    AddInfo(FString::Printf(TEXT("sampled %.0f..%.0f m within %.0f..%.0f m"), Lowest / 100.0, Highest / 100.0, Relief.MinHeightCm() / 100.0, Relief.MaxHeightCm() / 100.0));
    TestTrue(TEXT("no sample of 100,000 above MaxHeightCm or below MinHeightCm, craters in"),
             Highest <= Relief.MaxHeightCm() && Lowest >= Relief.MinHeightCm());
    const double Footprint = 5.0e4;
    TestTrue(TEXT("a footprint's omitted bound covers what the fade removed, craters in"),
             [&]() { FRandomStream Local(9); for (int32 I = 0; I < 5000; ++I) { const FVector3d D = RandomDirection(Local);
                 if (FMath::Abs(Relief.Height(D, 0.0) - Relief.Height(D, Footprint)) > Relief.OmittedBoundCm(Footprint)) { return false; } } return true; }());
    return true;
}

#endif
```

- [ ] **Step 2: Run and see them fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git merge -q feat/landing-b && ./build.sh
```

Expected: fails to compile: `'SlopeScale': is not a member of 'FWorldRelief'`.

- [ ] **Step 3: The kernels, in the shared file**

In `Shaders/Private/WorldRelief.ush`, insert this section directly above the line
`// -- The entry point --------------------------------------------------------` (so
`WR_SurfaceTerms` can call it in Task T2). It is written in the file's subset (Task R1): `WR_`
shims, `WR_REAL(...)` literals, no swizzles, and the file's own `WR_VoronoiJitter` for the sites.

```hlsl
// -- Craters as a height (landing decision 3) --------------------------------
// A crater band is a sum of compact kernels, one per kept site in the
// 3 x 3 x 3 cells round the sample: the profile unchanged, reaching 0 with
// zero slope at q = 1.5; a dropped site contributes 0; overlapping craters
// add. With jitter 0.2588 cells a 3 x 3 x 3 neighbourhood sees every site
// within 0.74 cells, past the profile's 0.525, so the sum is continuous --
// where Voronoi F1, the nearest site only, jumped to 0 at the bisector with
// a dropped neighbour.
//
// Sites are exactly WR_Voronoi's: each lattice corner's jitter is
// WR_VoronoiJitter of that corner, and whether it holds a crater is the
// corner's own hash's first channel against WR_CRATER_KEEP -- what
// WR_CraterBand reads as WR_CellHash(site + 0.5).
static const WR_REAL WR_CRATER_REACH = WR_REAL(1.5);
// At most four lattice corners lie within 0.525 + 0.2588 cells of any point
// (checked by sampling for the landing spec), so at most four kernels overlap.
static const WR_REAL WR_CRATER_BOUND_COUNT = WR_REAL(4.0);

struct WR_CraterTerm
{
    WR_REAL H;       // the band's height in cells (radius units once over the frequency)
    WR_REAL GX;      // its gradient with respect to D, radius units per unit of D
    WR_REAL GY;
    WR_REAL GZ;
    WR_REAL Albedo;  // darker floors and brighter rims, summed
};

WR_CraterTerm WR_CraterZero()
{
    WR_CraterTerm T;
    T.H = WR_REAL(0.0);
    T.GX = WR_REAL(0.0);
    T.GY = WR_REAL(0.0);
    T.GZ = WR_REAL(0.0);
    T.Albedo = WR_REAL(0.0);
    return T;
}

// How much of a band of Frequency a tile's vertices carry at VertexBandLimit
// (radius units): the fade the vertices were built with. A radius (1.0) or
// more is no vertices at all, the orbit: they carry nothing.
WR_REAL WR_VertexCarries(WR_REAL VertexBandLimit, WR_REAL Frequency)
{
    return WR_saturate(WR_REAL(1.0) - VertexBandLimit * Frequency);
}

// One band, P = D x frequency + the band's offset, in cells.
WR_CraterTerm WR_CraterKernelBand(WR_REAL PX, WR_REAL PY, WR_REAL PZ)
{
    WR_CraterTerm Sum = WR_CraterZero();
    const int CX = int(WR_floor(PX + WR_REAL(0.5)));
    const int CY = int(WR_floor(PY + WR_REAL(0.5)));
    const int CZ = int(WR_floor(PZ + WR_REAL(0.5)));
    for (int I = -1; I <= 1; ++I)
    {
        for (int J = -1; J <= 1; ++J)
        {
            for (int K = -1; K <= 1; ++K)
            {
                const int X = CX + I;
                const int Y = CY + J;
                const int Z = CZ + K;
                const WR_REAL Keep = WR_REAL(WR_Rand3DPCG16(X, Y, Z).X) / WR_REAL(65535.0);
                if (Keep > WR_CRATER_KEEP)
                {
                    continue;
                }
                const WR_Vec3 Jitter = WR_VoronoiJitter(WR_REAL(X), WR_REAL(Y), WR_REAL(Z));
                const WR_REAL AX = PX - (WR_REAL(X) + Jitter.X);
                const WR_REAL AY = PY - (WR_REAL(Y) + Jitter.Y);
                const WR_REAL AZ = PZ - (WR_REAL(Z) + Jitter.Z);
                const WR_REAL Dist = WR_sqrt(AX * AX + AY * AY + AZ * AZ);
                const WR_REAL Q = Dist * WR_CRATER_INV_RADIUS;
                if (Q >= WR_CRATER_REACH)
                {
                    continue;
                }
                // The bowl, depth (q^2 - 1 + rim), then the rim falling away,
                // depth rim (3 - 2q)^2: continuous at q = 1, 0 with zero slope
                // at 1.5. Heights in cells: radius x profile; the slope with
                // respect to P is d(profile)/dq along the way out.
                const WR_REAL Fall = WR_REAL(3.0) - WR_REAL(2.0) * Q;
                const WR_REAL Profile = Q < WR_REAL(1.0) ? WR_CRATER_DEPTH * (Q * Q - WR_REAL(1.0) + WR_CRATER_RIM)
                                                : WR_CRATER_DEPTH * WR_CRATER_RIM * Fall * Fall;
                const WR_REAL Wall = Q < WR_REAL(1.0) ? WR_REAL(2.0) * WR_CRATER_DEPTH * Q
                                                      : WR_REAL(-4.0) * WR_CRATER_DEPTH * WR_CRATER_RIM * Fall;
                const WR_REAL Apart = WR_max(Dist, WR_REAL(1.0e-4));
                Sum.H += WR_CRATER_RADIUS * Profile;
                Sum.GX += Wall * AX / Apart;
                Sum.GY += Wall * AY / Apart;
                Sum.GZ += Wall * AZ / Apart;
                Sum.Albedo += Q < WR_REAL(1.0) ? -(WR_REAL(1.0) - Q * Q) * WR_CRATER_FLOOR_DARK
                                               : WR_saturate(Fall) * WR_CRATER_RIM_BRIGHT;
            }
        }
    }
    return Sum;
}

// Every crater band at a footprint (radius units). The height and the albedo
// fade with the footprint as every band does; the slope is also shared out
// with a tile's vertices: it keeps only what the vertices at VertexBandLimit
// do not carry, max(0, fade - carried), which at the orbit (VertexBandLimit
// 1) is the fade alone and on ground finer than its pixels is exactly the
// orbit's total -- nothing counted twice (landing decision 9).
WR_CraterTerm WR_CraterSum(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL OX, WR_REAL OY, WR_REAL OZ,
                           WR_REAL FootprintRadius, WR_REAL VertexBandLimit)
{
    WR_CraterTerm Sum = WR_CraterZero();
    for (int Crater = 0; Crater < WR_CRATER_BANDS; ++Crater)
    {
        const WR_REAL F = WR_CRATER_FREQUENCY[Crater];
        const int Index = WR_CRATER_INDEX[Crater];
        const WR_REAL Fade = WR_saturate(WR_REAL(1.0) - FootprintRadius * F);
        if (Fade > WR_REAL(0.0))
        {
            const WR_CraterTerm T = WR_CraterKernelBand(DX * F + OX + WR_REAL(37 * Index),
                                                        DY * F + OY + WR_REAL(59 * Index),
                                                        DZ * F + OZ + WR_REAL(83 * Index));
            const WR_REAL Slope = WR_max(WR_REAL(0.0), Fade - WR_VertexCarries(VertexBandLimit, F));
            Sum.H += Fade * T.H / F;
            Sum.GX += Slope * T.GX;
            Sum.GY += Slope * T.GY;
            Sum.GZ += Slope * T.GZ;
            Sum.Albedo += Fade * T.Albedo;
        }
    }
    return Sum;
}
```

- [ ] **Step 4: The craters in `Height`**

In `Source/DeepSpace/Surface/WorldRelief.h`, public, after `DetailOmittedBound`:

```cpp
    /** The analytic bound of the band sum S, radius units: the detail bands'
     *  (DetailBound) and Cratering x the crater bands' (WR_CRATER_BOUND_COUNT
     *  kernels at the profile's largest magnitude, per band). Height =
     *  PeakCm x S / SMax, so MaxHeightCm is exactly PeakCm. */
    double SMax() const;

    /** PeakCm / (RadiusCm x SMax): the band sum's gradient times this is the
     *  height's slope in radius units -- what M_SkyBody's ReliefScale is, so
     *  the orbit's relief shading is the ground's own slope (decision 3). */
    double SlopeScale() const;
```

and in its private block, after `FootprintOf`:

```cpp
    /** The crater bands' bound, slope bound and omitted bound, radius units. */
    static double CraterBound();
    static double CraterSlopeBound();
    static double CraterOmittedBound(double FootprintRadius);
```

In the same header, the two inline bodies Task R3 wrote for these become declarations, since their
definitions move to the `.cpp` and read the ground's kind:

```cpp
    double MaxHeightCm() const;
    double MinHeightCm() const;
```

In `WorldRelief.cpp` (which includes the `.ush` twice, into `WorldReliefF64` and
`WorldReliefF32`), after the `#undef WR_CPP` line add

```cpp
namespace WR64 = WorldReliefF64;
```

and replace the definitions of `HeightAndGradient`, `Height`, `OmittedBoundCm` and `MaxSlope`
with the following (and add the new ones):

```cpp
namespace WorldReliefCraterLocal
{
    /** The crater profile's largest magnitude, depth units: the bowl's floor,
     *  depth (1 - rim), or the rim's crest, depth rim. */
    double ProfileMagnitude()
    {
        return FMath::Max(WR64::WR_CRATER_DEPTH * (1.0 - WR64::WR_CRATER_RIM), WR64::WR_CRATER_DEPTH * WR64::WR_CRATER_RIM);
    }

    /** Its slope's largest magnitude: the bowl's 2 depth at the crest, or the
     *  rim's 4 depth rim. */
    double WallMagnitude()
    {
        return FMath::Max(2.0 * WR64::WR_CRATER_DEPTH, 4.0 * WR64::WR_CRATER_DEPTH * WR64::WR_CRATER_RIM);
    }
}

double FWorldRelief::CraterBound()
{
    using namespace WorldReliefCraterLocal;
    double Bound = 0.0;
    for (int32 Crater = 0; Crater < WR_CRATER_BANDS; ++Crater)
    {
        const double F = WR64::WR_CRATER_FREQUENCY[Crater];
        Bound += WR64::WR_CRATER_BOUND_COUNT * WR64::WR_CRATER_RADIUS * ProfileMagnitude() / F;
    }
    return Bound;
}

double FWorldRelief::CraterSlopeBound()
{
    return WR_CRATER_BANDS * WR64::WR_CRATER_BOUND_COUNT * WorldReliefCraterLocal::WallMagnitude();
}

double FWorldRelief::CraterOmittedBound(double FootprintRadius)
{
    using namespace WorldReliefCraterLocal;
    double Bound = 0.0;
    for (int32 Crater = 0; Crater < WR_CRATER_BANDS; ++Crater)
    {
        const double F = WR64::WR_CRATER_FREQUENCY[Crater];
        Bound += (1.0 - FMath::Clamp(1.0 - FootprintRadius * F, 0.0, 1.0)) * WR64::WR_CRATER_BOUND_COUNT * WR64::WR_CRATER_RADIUS * ProfileMagnitude() / F;
    }
    return Bound;
}

double FWorldRelief::SMax() const
{
    return DetailBound() + FMath::Max(Params.Cratering, 0.0) * CraterBound();
}

double FWorldRelief::SlopeScale() const
{
    const double Bound = SMax();
    return Bound > 0.0 && Params.RadiusCm > 0.0 ? Params.PeakCm / (Params.RadiusCm * Bound) : 0.0;
}

double FWorldRelief::HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const
{
    Grad = FVector3d::ZeroVector;
    if (!(Params.PeakCm > 0.0) || !(Params.RadiusCm > 0.0) || Params.Ground != EGround::Solid)
    {
        return 0.0;
    }
    const double Footprint = FootprintOf(FootprintCm);
    FVector3d DetailGrad = FVector3d::ZeroVector;
    const double Detail = DetailSum(D, Footprint, &DetailGrad);
    const WR64::WR_CraterTerm Craters = WR64::WR_CraterSum(D.X, D.Y, D.Z, Params.SeedOffset.X, Params.SeedOffset.Y,
                                                           Params.SeedOffset.Z, Footprint, 1.0);
    const double Kept = FMath::Max(Params.Cratering, 0.0);
    const double Scale = Params.RadiusCm * SlopeScale();   // = PeakCm / SMax
    Grad = (DetailGrad + FVector3d(Craters.GX, Craters.GY, Craters.GZ) * Kept) * Scale;
    return (Detail + Craters.H * Kept) * Scale;
}

double FWorldRelief::Height(const FVector3d& D, double FootprintCm) const
{
    FVector3d Ignored;
    return HeightAndGradient(D, Ignored, FootprintCm);
}

double FWorldRelief::MaxHeightCm() const
{
    return Params.Ground == EGround::Solid ? FMath::Max(Params.PeakCm, 0.0) : 0.0;
}

double FWorldRelief::MinHeightCm() const
{
    return -MaxHeightCm();
}

double FWorldRelief::MaxSlope() const
{
    // The sum's Lipschitz bound with respect to D, times the scale, is the
    // height's slope in radius units, which is rise over run on the ground.
    return SlopeScale() * (DetailSlopeBound() + FMath::Max(Params.Cratering, 0.0) * CraterSlopeBound());
}

double FWorldRelief::OmittedBoundCm(double FootprintCm) const
{
    const double Footprint = FootprintOf(FootprintCm);
    return Params.RadiusCm * SlopeScale()
         * (DetailOmittedBound(Footprint) + FMath::Max(Params.Cratering, 0.0) * CraterOmittedBound(Footprint));
}
```

Task R3's `.KnownValues` pins the hash and the noise, not `Height`, and stays green. Its
`.Bounds`, `.Gradient`, `.SlopeBound` and `.Footprint` now include the craters and must stay
green too. Its `.Bounds` case for an ocean or a giant (`PeakCm` 0) still reads 0, and a world
whose `Ground` is `None` now reads 0 whatever its `PeakCm`.

- [ ] **Step 5: Run: PASS**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Surface.WorldRelief
```

Expected: every `DeepSpace.Surface.WorldRelief.*` green, the three new ones included. (The
material still draws slice (a)'s craters: nothing here changes the look.)

- [ ] **Step 6: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
git add Shaders/Private/WorldRelief.ush Source/DeepSpace/Surface/WorldRelief.h Source/DeepSpace/Surface/WorldRelief.cpp \
        Source/DeepSpace/Tests/WorldReliefCratersTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(relief): craters as a height -- summed compact kernels in the shared file and in Height

One kernel per kept site over the 3 x 3 x 3 cells round the sample, the
sites and profile exactly today's, so a crater is a continuous height and
the bounds, the slope bound and the gradient hold with craters in. The
material is unchanged here; it switches in the next commit (landing
decision 3, sign-off 2).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7: Prove them**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Shaders/Private/WorldRelief.ush ': WR_CRATER_DEPTH * WR_CRATER_RIM * Fall * Fall;' ': WR_CRATER_DEPTH * WR_CRATER_RIM;' DeepSpace.Surface.WorldRelief.CratersContinuous && \
Tools/mutate.sh Source/DeepSpace/Surface/WorldRelief.cpp 'Bound += WR64::WR_CRATER_BOUND_COUNT * WR64::WR_CRATER_RADIUS * ProfileMagnitude() / F;' 'Bound += WR64::WR_CRATER_RADIUS * ProfileMagnitude() / F;' DeepSpace.Surface.WorldRelief.CraterBounds && \
./build.sh
```

Expected: `KILLED` twice (a flat-topped rim ending in a step at q = 1.5 is a cliff; a bound for
one kernel is exceeded where craters overlap). If the second survives the 100,000 samples,
double the sample count and say so; the bound must be shown to matter.

---

## Task 32 (T2): the look switches -- flattened relief from the ground's own slope, summed craters, `ds.Sky.Relief` and `ds.Sky.Craters` retired

**Owner:** T. **Depends on:** T1. Spec decisions 3 and 9 (*retired as live knobs*), sign-off
items 2 and 21.

**Files:**
- Create: `Source/DeepSpace/Tests/Eyes/ReliefLookEyesTest.cpp` (`Eyes.ReliefLook`, frames only)
- Modify: `Shaders/Private/WorldRelief.ush` -- `WR_SurfaceTerms` (Task R1's entry point)
- Modify: `Source/DeepSpace/Surface/WorldRelief.h`, `.cpp` (`FaceF64`, `FaceF32` gain
  `VertexBandLimit`)
- Modify: `Tools/sky_material_contract.json` (`relief` -> `relief_scale`; `M_SkyBody`'s list;
  the `relief_comment`; `shared_relief.inputs`), `Source/DeepSpace/Sky/SkyMaterialContract.h`
  (`Relief` -> `ReliefScale`; `WorldReliefInputs()`), `Tools/setup_sky_materials.py`
  (`SHARED_CODE`, `shared_terms`, `surface`, `sky_body`),
  `Source/DeepSpace/Tests/SkyMaterialContractTest.cpp` (`CheckSurfaceFace`'s two knob lists)
- Modify: `Source/DeepSpace/Sky/ShipSky.cpp` (delete `CVarRelief`, `CVarCraters`, lines 150-168;
  the writes in `DrawBodies`, lines 440-473), `Source/DeepSpace/Tests/ShipSkyTest.cpp` (lines
  390-392 and 433-435)
- Modify: `Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp` (the parameter it sets)
- Modify: `CLAUDE.md` -- *The sky*'s *A world's face and relief are one noise* paragraph, the
  tunables row `ds.Sky.SurfaceDetail`, `.Relief`, `.Craters`

**Interfaces:**
- Consumes: `FWorldRelief::SlopeScale`, `WR_CraterSum`, `WR_VertexCarries` (Task T1); Task R1's
  `WR_Terms WR_SurfaceTerms(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL Footprint, WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL Stretch)`,
  the Python `Terms`, `shared_terms(g, direction, footprint, seed, stretch)` and
  `surface(g, knobs, seed, direction, footprint, terms)` with `knobs = (mottle, detail, banding, relief, cratering)`.
- Produces: `WR_SurfaceTerms(..., WR_REAL Stretch, WR_REAL VertexBandLimit)` (the crater terms from
  `WR_CraterSum`; every slope term only what the vertices at `VertexBandLimit` do not carry);
  `WorldReliefNoise::FaceF64(const FVector3d& D, double FootprintD, const FVector3d& Offset, double Stretch, double VertexBandLimit = 1.0)`
  and `FaceF32(..., float Stretch, float VertexBandLimit = 1.0f)`;
  `shared_terms(g, direction, footprint, seed, stretch, vertex_band_limit=None)` (`None` is the
  orbit's constant 1.0); `surface()`'s knobs `(mottle, detail, banding, relief_scale, cratering)`;
  `SkyMaterial::ReliefScale` (`"ReliefScale"`); `SkyMaterial::WorldReliefInputs()` gaining
  `VertexBandLimit`; `ShipSky::GiantReliefScale` (0.066), `ShipSky::ReliefScaleOf(const FSkyBody&)`.

- [ ] **Step 1: A frames-only eyes test, and the before frames**

Create `Source/DeepSpace/Tests/Eyes/ReliefLookEyesTest.cpp`:

```cpp
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * FRAMES, NOT A GUARD (sign-off item 2): Baemsekai III, IV and V from 200 km
 * under a low sun, from the helm's eye, for the developer to see the orbit's
 * relief flatten when (b) switches it to the ground's own slope. Run once
 * before the switch and once after, each into its own folder:
 *
 *   EYES_TAG=before Tools/eyes.sh Eyes.ReliefLook
 *   EYES_TAG=after  Tools/eyes.sh Eyes.ReliefLook
 *
 * Writes Saved/Eyes/ReliefLook/<tag>/<world>.png.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReliefLookEyesTest, "Eyes.ReliefLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReliefLookEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    const FString Tag = FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_TAG")).IsEmpty()
        ? FString(TEXT("untagged")) : FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_TAG"));
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/ReliefLook") / Tag;
    IFileManager::Get().MakeDirectory(*Dir, true);

    FSkyWorld Test(TEXT("ReliefLookWorld"));
    Test.BeginPlay();
    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Test.Sky);
    Capture->RegisterComponent();
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Test.Sky);
    Target->InitCustomFormat(1920, 1080, PF_B8G8R8A8, false);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->FOVAngle = 60.0f;
    Capture->CaptureSource = SCS_FinalColorLDR;
    Capture->SetWorldLocationAndRotation(PilotEye, FRotator::ZeroRotator);

    for (const TCHAR* Body : { TEXT("3"), TEXT("4"), TEXT("5") })
    {
        FOutputDeviceNull Out;
        const TArray<FString> Args = { Body, TEXT("200"), TEXT("dusk") };
        AShipSky::Goto(*Test.Ship, LocalSystem::Current(Test.World), false, Args, Out);
        for (int32 Frame = 0; Frame < 3; ++Frame)
        {
            Test.Step(1.0f / 60.0f);
        }
        Capture->CaptureScene();
        FlushRenderingCommands();
        TArray<FColor> Pixels;
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(1920, 1080, Pixels, Png);
        FFileHelper::SaveArrayToFile(Png, *(Dir / FString::Printf(TEXT("world_%s.png"), Body)));
        TestTrue(FString::Printf(TEXT("world %s framed"), Body), Pixels.Num() == 1920 * 1080);
    }
    return true;
}

#endif
```

Build and take the before frames:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && EYES_TAG=before Tools/eyes.sh Eyes.ReliefLook && \
ls Saved/Eyes/ReliefLook/before/
```

Expected: `world_3.png world_4.png world_5.png`.

- [ ] **Step 2: Change the failing test first: the proxy's relief is the ground's slope, no knob**

In `ShipSkyTest.cpp`, replace

```cpp
    const FScopedCVar FaceRelief(TEXT("ds.Sky.Relief"), 0.321f);
    const FScopedCVar FaceCraters(TEXT("ds.Sky.Craters"), 0.5f);
    Fixture.Bodies[SkyTestFixtures::HomeIndex].Cratering = 0.25;
```

with

```cpp
    FSkyBody& HomeBody = Fixture.Bodies[SkyTestFixtures::HomeIndex];
    HomeBody.Cratering = 0.25;
    HomeBody.Ground = EGround::Solid;
    HomeBody.Relief = FWorldReliefParams{ FVector3d(3.0, 5.0, 7.0), HomeBody.Radius, 5.0e5, 0.25, EGround::Solid };
```

and the two assertions that read `SkyMaterial::Relief` (`"its relief, as the knob says"`, 0.321)
and `SkyMaterial::Cratering` (0.125) with:

```cpp
                TestTrue(TEXT("its relief is its ground's own slope scale, from its peak (decision 3)"),
                         FMath::IsNearlyEqual(Instance->K2_GetScalarParameterValue(SkyMaterial::ReliefScale),
                                              static_cast<float>(FWorldRelief(HomeBody.Relief).SlopeScale()), 1e-7f));
                TestEqual(TEXT("and its craters its own Cratering, with no knob over it"),
                          Instance->K2_GetScalarParameterValue(SkyMaterial::Cratering), 0.25f);
```

(add `#include "Surface/WorldRelief.h"`), and after them:

```cpp
                TestNull(TEXT("ds.Sky.Relief is retired: a knob that moves the ground under a landed ship"),
                         IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Relief")));
                TestNull(TEXT("and ds.Sky.Craters"), IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Craters")));
```

Run: `./build.sh` fails: `'ReliefScale': is not a member of 'SkyMaterial'`.

- [ ] **Step 3: The contract, three sides**

`Tools/sky_material_contract.json`: rename the `"relief"` parameter entry to
`"relief_scale": { "name": "ReliefScale", "type": "scalar" }`; in `M_SkyBody`'s `parameters`
replace `"relief"` with `"relief_scale"`; in `"shared_relief"`, `"inputs"` becomes
`["Direction", "Footprint", "SeedOffset", "Stretch", "VertexBandLimit"]`; replace
`relief_comment`'s first sentence with: `"M_SkyBody's relief: the ground's own slope (landing
decision 3) -- ReliefScale is FWorldRelief::SlopeScale, PeakCm / (R x SMax), so the orbit shades
exactly the heights a ship lands on; giants, which have no ground, keep a fixed cloud-top billow
(ShipSky::GiantReliefScale), oceans none. Craters are summed compact kernels, one per kept site
(WR_CraterSum), their albedo still fewer in the basins (maria_cratering), their height not."` and
keep the rest.

`Source/DeepSpace/Sky/SkyMaterialContract.h`: rename the `Relief` name constant to `ReliefScale`
with the value `TEXT("ReliefScale")`, and make `WorldReliefInputs()` return
`{ TEXT("Direction"), TEXT("Footprint"), TEXT("SeedOffset"), TEXT("Stretch"), TEXT("VertexBandLimit") }`.

`Source/DeepSpace/Tests/SkyMaterialContractTest.cpp`, in `CheckSurfaceFace`: in the two knob lists
`{ SkyMaterial::Mottle, SkyMaterial::Detail, SkyMaterial::Relief, SkyMaterial::Cratering }` and
`{ SkyMaterial::Relief, SkyMaterial::Cratering }`, `SkyMaterial::Relief` becomes
`SkyMaterial::ReliefScale`.

`Tools/setup_sky_materials.py`:

- `SHARED_CODE`'s call gains the last argument: `... SeedOffset.z, Stretch, VertexBandLimit);`.
- `shared_terms` becomes:

```python
def shared_terms(g, direction, footprint, seed, stretch, vertex_band_limit=None):
    """The same raw terms from Shaders/Private/WorldRelief.ush -- the file the
    C++ compiles too (landing decision 1) -- through one Custom node that
    includes it and calls its entry point. Every band the file carries.
    vertex_band_limit is what a tile's vertices carry (radius units); the
    slopes keep only the rest. None is the orbit: no vertices, 1.0."""
    custom = g.node(unreal.MaterialExpressionCustom)
    custom.set_editor_property("description", "WorldRelief")
    custom.set_editor_property("code", SHARED_CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("include_file_paths", [SHARED["include"]])
    pins = []
    for input_name in SHARED["inputs"]:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", input_name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    outputs = []
    for output_name, kind in SHARED["outputs"]:
        pin = unreal.CustomOutput()
        pin.set_editor_property("output_name", output_name)
        pin.set_editor_property("output_type", getattr(unreal.CustomMaterialOutputType, kind))
        outputs.append(pin)
    custom.set_editor_property("additional_outputs", outputs)
    limit = vertex_band_limit if vertex_band_limit is not None else g.constant(1.0)
    for input_name, source in zip(SHARED["inputs"], (direction, footprint, seed_offset(g, seed), stretch, limit)):
        g.link(source, custom, input_name)
    return Terms(mask(g, custom, "r", output_name="Continent"), custom,
                 mask(g, custom, "r", output_name="CraterAlbedo"),
                 mask(g, custom, "rgb", output_name="CraterSlope"))
```

- In `surface()`: the knobs line becomes `mottle, detail, banding, relief_scale, cratering = knobs`;
  delete the two `cloud` lines; the `slope = ...` statement becomes

```python
    # The ground's own slope (landing decision 3): ReliefScale x the detail
    # bands' gradient and x Cratering the craters', everywhere -- the maria
    # still thin the craters' albedo, never their height.
    slope = g.add(g.mul(g.mul(mask(g, t.fine, "rgb"), stretch_axes(g, stretch)), relief_scale),
                  g.mul(t.crater_slope, g.mul(cratering, relief_scale)))
```

  and in its docstring the `slope = Relief * lerp(1, relief_giant,
  Banding) * fine.rgb * (1, 1, stretch) + craters.slope` lines become
  `slope = ReliefScale * (fine.rgb * (1, 1, stretch) + Cratering * craters.slope)`.
- In `sky_body()`: the knob `g.scalar("relief", 0.2)` becomes `g.scalar("relief_scale", 0.0)`,
  still passed as the knobs tuple's fourth member.

- [ ] **Step 4: The entry point, in the shared file, and its C++ wrappers**

In `Shaders/Private/WorldRelief.ush`, replace `WR_SurfaceTerms` whole with:

```hlsl
// -- The entry point --------------------------------------------------------
// Every raw term of a world's face at D, for footprint Footprint (D units a
// pixel, times filter_pixels), seed offset O, and Stretch (1 for ground,
// belt_stretch for a giant's detail). The slopes keep only what a tile's
// vertices at VertexBandLimit (radius units) do not carry: 1.0 is the orbit,
// with no vertices, and every slope whole (landing decision 9). The craters
// are the summed kernels (decision 3).
WR_Terms WR_SurfaceTerms(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL Footprint,
                         WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL Stretch, WR_REAL VertexBandLimit)
{
    const WR_REAL PX = DX;
    const WR_REAL PY = DY;
    const WR_REAL PZ = DZ * Stretch;
    WR_Terms T;
    T.Continent = WR_Continent(PX * WR_CONTINENT_FREQUENCY + OX, PY * WR_CONTINENT_FREQUENCY + OY,
                               PZ * WR_CONTINENT_FREQUENCY + OZ, Footprint * WR_CONTINENT_FREQUENCY * Stretch);
    const WR_REAL Width = Footprint * Stretch;
    T.Detail = WR_REAL(0.0);
    T.DetailSlopeX = WR_REAL(0.0);
    T.DetailSlopeY = WR_REAL(0.0);
    T.DetailSlopeZ = WR_REAL(0.0);
    for (int Band = 0; Band < WR_DETAIL_BANDS; ++Band)
    {
        const int Index = WR_DETAIL_INDEX[Band];
        const WR_REAL F = WR_DETAIL_FREQUENCY[Band];
        const WR_Noise4 N = WR_DetailBand(PX, PY, PZ, F,
            OX + WR_REAL(37 * Index), OY + WR_REAL(59 * Index), OZ + WR_REAL(83 * Index), Width, WR_DETAIL_WEIGHT[Band]);
        // N is already faded by Fade; its slope keeps max(0, Fade - carried) of the whole.
        const WR_REAL Fade = WR_saturate(WR_REAL(1.0) - Width * F);
        const WR_REAL Share = Fade > WR_REAL(0.0)
            ? WR_max(WR_REAL(0.0), Fade - WR_VertexCarries(VertexBandLimit, F)) / Fade
            : WR_REAL(0.0);
        T.DetailSlopeX += N.GX * Share;
        T.DetailSlopeY += N.GY * Share;
        T.DetailSlopeZ += N.GZ * Share;
        T.Detail += N.Value;
    }
    const WR_CraterTerm Craters = WR_CraterSum(DX, DY, DZ, OX, OY, OZ, Footprint, VertexBandLimit);
    T.CraterAlbedo = Craters.Albedo;
    T.CraterSlopeX = Craters.GX;
    T.CraterSlopeY = Craters.GY;
    T.CraterSlopeZ = Craters.GZ;
    return T;
}
```

`WR_CraterBand` and `WorldReliefNoise::CraterMargin` stay (the parity test's step mask reads the
Voronoi sites), though no term uses `WR_CraterBand` now.

In `Surface/WorldRelief.h`, `FaceF64` and `FaceF32` gain a trailing parameter:

```cpp
    DEEPSPACE_API FFaceTerms FaceF64(const FVector3d& D, double FootprintD, const FVector3d& Offset, double Stretch,
                                     double VertexBandLimit = 1.0);
    DEEPSPACE_API FFaceTerms FaceF32(const FVector3f& D, float FootprintD, const FVector3f& Offset, float Stretch,
                                     float VertexBandLimit = 1.0f);
```

and in `WorldRelief.cpp` their definitions take it and pass it last to `WR_SurfaceTerms`
(`..., Offset.Z, Stretch, VertexBandLimit)`). `FWorldRelief::Face` keeps calling `FaceF64` with
four arguments: the orbit's 1.0.

- [ ] **Step 5: The sky writes it, and the knobs go**

In `ShipSky.cpp`: delete the `CVarRelief` and `CVarCraters` definitions and the two `const float
Relief/Craters = ...` lines in `DrawBodies`. Add in the `ShipSky` namespace of `ShipSky.h`:

```cpp
    /** A giant's cloud tops, which have no ground: the fixed billow the orbit
     *  had before landing (ds.Sky.Relief's 0.2 x relief_giant's 0.33), kept
     *  as a look, not a height. Oceans draw none: water is flat. */
    inline constexpr double GiantReliefScale = 0.066;

    /** The relief a body's face is shaded with: its ground's own slope scale
     *  (FWorldRelief::SlopeScale) over a solid world, the giant billow over a
     *  banded one, none otherwise. */
    DEEPSPACE_API double ReliefScaleOf(const FSkyBody& Body);
```

and in `ShipSky.cpp`:

```cpp
double ShipSky::ReliefScaleOf(const FSkyBody& Body)
{
    if (Body.Ground == EGround::Solid)
    {
        return FWorldRelief(Body.Relief).SlopeScale();
    }
    return Body.Surface == ESkySurface::Banded ? GiantReliefScale : 0.0;
}
```

(`#include "Surface/WorldRelief.h"`). In `DrawBodies`, replace the `Relief` and `Cratering` writes:

```cpp
            Instance->SetScalarParameterValue(SkyMaterial::ReliefScale, static_cast<float>(ShipSky::ReliefScaleOf(System.Bodies[Index])));
            Instance->SetScalarParameterValue(SkyMaterial::Cratering, static_cast<float>(System.Bodies[Index].Cratering));
```

`Tests/Eyes/WorldReliefParityTest.cpp` needs no change: its probe draws the raw terms through
`shared_terms` at the orbit's `VertexBandLimit` of 1.0, and `FWorldRelief::Face` computes the same;
the craters it compares are now the summed kernels on both sides.

- [ ] **Step 6: Author, test, and take the after frames**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && \
rm -f Saved/setup_sky_materials.txt && \
. Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" -unattended -nopause -nosplash -NoLiveCoding && \
tail -3 Saved/setup_sky_materials.txt && tail -n 1 Saved/setup_sky_materials.txt | grep -qx ok && \
./test.sh DeepSpace.Sky && Tools/eyes.sh Eyes.WorldReliefParity && EYES_TAG=after Tools/eyes.sh Eyes.ReliefLook
```

The report is removed first so a stale one cannot pass, and the chain stops unless the authoring
run's last line is `ok` (`setup_sky_materials.py` writes it only when `main()` finished).

Expected: the authoring report ends with `ok`; every `DeepSpace.Sky.*` green (the
contract test holds header, JSON and asset to `ReliefScale`); `Eyes.WorldReliefParity` passes
(the GPU compiled the new Custom node: a grey material would fail it); after frames written.

- [ ] **Step 7: Document**

In `CLAUDE.md`, *The sky*, in the paragraph **A world's face and relief are one noise**, replace
`Each band's height goes with its wavelength, so every scale the screen holds has the same slope.`
through the end of the crater sentence with:

```markdown
Each band's height goes with its wavelength, so every scale the screen holds
has the same slope, and **the amplitude is the ground's** (landing decision
3): `ReliefScale` is `FWorldRelief::SlopeScale`, the world's drawn peak over
its radius and the sum's bound, so the orbit shades exactly the heights a ship
lands on -- Earth-like, 1/g, capped at 10 km -- and relief is data (the
priors), not a knob: `ds.Sky.Relief` and `ds.Sky.Craters` are retired, because
a knob that moved the height would move the ground under a landed ship.
Craters are summed compact kernels (`WR_CraterSum`), one per kept site, in
bands stepping by four (the count wider than D goes as D^-2); their albedo is
fewer in the basins, their height the same everywhere. Giants keep a fixed
cloud billow (`ShipSky::GiantReliefScale`); oceans are flat.
```

and the tunables row to ``| `ds.Sky.SurfaceDetail` | 0.3 | `ShipSky.cpp` |``.

- [ ] **Step 8: Commit, with the frames named for the developer**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
git add Shaders/Private/WorldRelief.ush Source/DeepSpace/Surface/WorldRelief.h Source/DeepSpace/Surface/WorldRelief.cpp \
        Tools/sky_material_contract.json Source/DeepSpace/Sky/SkyMaterialContract.h \
        Tools/setup_sky_materials.py Source/DeepSpace/Sky/ShipSky.h Source/DeepSpace/Sky/ShipSky.cpp \
        Source/DeepSpace/Tests/ShipSkyTest.cpp Source/DeepSpace/Tests/SkyMaterialContractTest.cpp \
        Source/DeepSpace/Tests/Eyes/ReliefLookEyesTest.cpp Content/Materials/Sky CLAUDE.md && \
git commit -m "$(cat <<'EOF'
feat(sky): the orbit shades the ground's own slope -- relief flattens, craters summed, knobs retired

M_SkyBody's ReliefScale is FWorldRelief::SlopeScale, so what the orbit
shows is what is there (ruling 5): Earth-like, a third of the old
slopes. Craters are summed kernels in the look as in the height. ds.Sky.Relief
and ds.Sky.Craters are retired (sign-off 21). Before and after frames of
Baemsekai III, IV and V are in Saved/Eyes/ReliefLook/{before,after} for
the developer (sign-off 2).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

Then send the six frames to the developer (the orchestrator does this; the plan's rule is that
they are seen before (b) merges).

- [ ] **Step 9: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'return FWorldRelief(Body.Relief).SlopeScale();' 'return 0.2;' DeepSpace.Sky.ShipSky && \
./build.sh
```

Expected: `KILLED`.

---

## Task 33 (T3): the terrain's quadtree -- cube faces, CDLOD from the ship, 2:1, the horizon, the tile cap

**Owner:** T. **Depends on:** B0. Spec decision 6 (*The quadtree*, *The LOD rule*, *What that costs*,
*`MaxTiles`*).

**Files:**
- Create: `Source/DeepSpace/Surface/TerrainQuadtree.h`, `Source/DeepSpace/Surface/TerrainQuadtree.cpp`
- Create: `Source/DeepSpace/Tests/TerrainQuadtreeTest.cpp` (`DeepSpace.Surface.Quadtree`, and review
  focus 2's `DeepSpace.Surface.QuadtreeAtCubeSeams`)

**Interfaces:**
- Consumes: nothing but `CoreMinimal`.
- Produces: `struct FTileKey { uint8 Face; uint8 Level; uint32 X; uint32 Y; Parent(); Child(int32); QuadrantInParent(); Contains(const FTileKey&); }`,
  `uint32 GetTypeHash(const FTileKey&)`; `TerrainQuadtree::{CellsPerTile (32), TargetSpacingCm (100),
  DefaultSplitFactor (2), DefaultMaxTiles (2500), PrefetchLevel (8), PrefetchAltitudeCm (1e8),
  ChainFullAltitudeCm (1e5), MinSplitFactor (1), SplitFactorStep (0.25)}`;
  `int32 MaxLevel(double RadiusCm)`; `double FaceCoordinate(int64 TwiceIndexMinusCount, int64 Count)`;
  `FVector3d GridDirection(const FTileKey&, int32 I, int32 J)`; `FVector3d CentreDirection(const FTileKey&)`;
  `FTileKey KeyAt(const FVector3d& D, int32 Level)`; `double EdgeLengthCm(int32 Level, double RadiusCm)`;
  `double SpacingCm(int32 Level, double RadiusCm)`; `double HalfDiagonalRadians(const FTileKey&)`;
  `struct FHeightRange { double MinCm; double MaxCm; }`; `struct FCutParams { double RadiusCm; int32 MaxLevel; double SplitFactor; int32 MaxTiles; double OccluderRadiusCm; double GroundAltitudeCm; }`;
  `struct FCut { TArray<FTileKey> Leaves; TArray<FTileKey> Prefetch; TArray<double> SplitFactorByLevel; bool bCapBinding; }`;
  `using FBoundsOf = TFunctionRef<TOptional<FHeightRange>(const FTileKey&)>`;
  `FCut SelectCut(const FVector3d& ShipFromCentre, const FCutParams&, FBoundsOf)`;
  `bool Visible(const FTileKey&, const FVector3d& Ship, const FHeightRange&, const FCutParams&)`;
  `TArray<FTileKey, TFixedAllocator<4>> EdgeNeighbours(const FTileKey&)`;
  `bool Balanced(TConstArrayView<FTileKey> Leaves)`.

- [ ] **Step 1: Write the failing test**

Create `Source/DeepSpace/Tests/TerrainQuadtreeTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Surface/TerrainQuadtree.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 6's quadtree, pure: six equal-angle cube faces (the
 * SM_SkyBody mapping), shared edges exact, and CDLOD from the ship's origin
 * with boxes from each node's own height range, the 2:1 rule, the horizon,
 * the ship's chain to MaxLevel near the ground, and MaxTiles coarsening the
 * far levels first. Reports the tile and triangle counts that set the
 * budgets (the simulation said 860 at 50 km to 2,200 at 1.5 m).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainQuadtreeTest, "DeepSpace.Surface.Quadtree",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TerrainQuadtreeLocal
{
    using namespace TerrainQuadtree;

    /** The leaf holding D, searching every level; none if D is culled. */
    TOptional<FTileKey> LeafAt(const TSet<FTileKey>& Leaves, const FVector3d& D, int32 MaxLevelIn)
    {
        for (int32 Level = 0; Level <= MaxLevelIn; ++Level)
        {
            const FTileKey Key = KeyAt(D, Level);
            if (Leaves.Contains(Key))
            {
                return Key;
            }
        }
        return {};
    }
}

bool FTerrainQuadtreeTest::RunTest(const FString& Parameters)
{
    using namespace TerrainQuadtreeLocal;
    const double Earth = UniverseUnits::CmPerEarthRadius;
    TestEqual(TEXT("an Earth's finest level is 19: 19 m tiles, 0.6 m between vertices"), MaxLevel(Earth), 19);
    TestEqual(TEXT("a small world has fewer"), MaxLevel(1.0e7), 13);
    TestTrue(FString::Printf(TEXT("level 19's spacing on an Earth is 0.6 m (%.3f)"), SpacingCm(19, Earth) / 100.0),
             FMath::IsNearlyEqual(SpacingCm(19, Earth), 59.7, 0.5));

    // Keys and directions agree, and shared edges are the same directions exactly.
    FRandomStream Random(19);
    int32 RoundTrips = 0;
    int32 EdgesExact = 0;
    int32 Edges = 0;
    for (int32 Trial = 0; Trial < 400; ++Trial)
    {
        FTileKey Key;
        Key.Face = static_cast<uint8>(Random.RandRange(0, 5));
        Key.Level = static_cast<uint8>(Random.RandRange(0, 19));
        Key.X = static_cast<uint32>(Random.RandRange(0, (1 << Key.Level) - 1));
        Key.Y = static_cast<uint32>(Random.RandRange(0, (1 << Key.Level) - 1));
        RoundTrips += KeyAt(CentreDirection(Key), Key.Level) == Key ? 1 : 0;
        TSet<FVector3d> Boundary;
        for (int32 K = 0; K <= CellsPerTile; ++K)
        {
            Boundary.Add(GridDirection(Key, K, 0));
            Boundary.Add(GridDirection(Key, K, CellsPerTile));
            Boundary.Add(GridDirection(Key, 0, K));
            Boundary.Add(GridDirection(Key, CellsPerTile, K));
        }
        for (const FTileKey& Neighbour : EdgeNeighbours(Key))
        {
            ++Edges;
            int32 Shared = 0;
            for (int32 K = 0; K <= CellsPerTile; ++K)
            {
                for (const FVector3d& D : { GridDirection(Neighbour, K, 0), GridDirection(Neighbour, K, CellsPerTile),
                                            GridDirection(Neighbour, 0, K), GridDirection(Neighbour, CellsPerTile, K) })
                {
                    Shared += Boundary.Contains(D) ? 1 : 0;
                }
            }
            // 33 shared, corners counted twice on each side's own list.
            EdgesExact += Shared >= CellsPerTile + 1 ? 1 : 0;
        }
    }
    TestEqual(TEXT("every key's centre maps back to it"), RoundTrips, 400);
    TestEqual(TEXT("every shared edge is the same 33 directions, to the last bit, across faces too"), EdgesExact, Edges);

    // The cut, over a sweep of altitudes, with 2 km local peaks for every
    // node's box (the simulation's horizon), no cap.
    const FHeightRange Peaks{ 0.0, 2.0e5 };
    struct FCount { double Altitude; int32 Tiles; };
    TArray<FCount> Counts;
    for (const double Altitude : { 150.0, 2.0e3, 1.0e5, 1.0e6, 5.0e6, 1.0e8 })
    {
        const FVector3d Nadir = FVector3d(0.31, -0.62, 0.72).GetSafeNormal();
        const FVector3d Ship = Nadir * (Earth + Altitude);
        FCutParams Params;
        Params.RadiusCm = Earth;
        Params.MaxLevel = MaxLevel(Earth);
        Params.MaxTiles = 1000000;
        Params.OccluderRadiusCm = Earth;
        Params.GroundAltitudeCm = Altitude;
        const FCut Cut = SelectCut(Ship, Params, [&](const FTileKey&) { return TOptional<FHeightRange>(Peaks); });
        Counts.Add({ Altitude, Cut.Leaves.Num() });
        const TSet<FTileKey> Leaves(Cut.Leaves);

        bool bNoOverlap = true;
        for (const FTileKey& Leaf : Cut.Leaves)
        {
            for (FTileKey Up = Leaf; Up.Level > 0;)
            {
                Up = Up.Parent();
                bNoOverlap &= !Leaves.Contains(Up);
            }
        }
        TestTrue(FString::Printf(TEXT("%.0f m: no leaf overlaps another"), Altitude / 100.0), bNoOverlap);
        TestTrue(FString::Printf(TEXT("%.0f m: 2:1 between neighbours"), Altitude / 100.0), Balanced(Cut.Leaves));

        // Everything the ship can see of a bare sphere is covered.
        const double Horizon = FMath::Acos(Earth / (Earth + Altitude));
        int32 Uncovered = 0;
        for (int32 Sample = 0; Sample < 2000; ++Sample)
        {
            const FVector3d Tangent = FVector3d::CrossProduct(Nadir, FVector3d(Random.GetUnitVector())).GetSafeNormal();
            const double Angle = Horizon * FMath::Sqrt(Random.FRand()) * 0.999;
            const FVector3d D = Nadir * FMath::Cos(Angle) + Tangent * FMath::Sin(Angle);
            Uncovered += LeafAt(Leaves, D, Params.MaxLevel).IsSet() ? 0 : 1;
        }
        TestEqual(FString::Printf(TEXT("%.0f m: every direction inside the horizon is drawn"), Altitude / 100.0), Uncovered, 0);
        if (Altitude < ChainFullAltitudeCm)
        {
            TestTrue(FString::Printf(TEXT("%.0f m: the ship's own chain reaches MaxLevel"), Altitude / 100.0),
                     Leaves.Contains(KeyAt(Nadir, Params.MaxLevel)));
        }
        if (Altitude < PrefetchAltitudeCm + 1.0)
        {
            TestEqual(TEXT("the chain to level 8 is prefetched"), Cut.Prefetch.Num(), PrefetchLevel + 1);
        }
    }
    for (const FCount& Count : Counts)
    {
        AddInfo(FString::Printf(TEXT("%.1f km: %d tiles, %.2f M triangles"), Count.Altitude / 1.0e5, Count.Tiles,
                                Count.Tiles * 2304.0 / 1.0e6));
    }

    // The cap: the far levels coarsen first; the ship's chain never does.
    {
        const FVector3d Nadir(0.0, 0.0, 1.0);
        FCutParams Params;
        Params.RadiusCm = Earth;
        Params.MaxLevel = MaxLevel(Earth);
        Params.MaxTiles = 600;
        Params.OccluderRadiusCm = Earth;
        Params.GroundAltitudeCm = 150.0;
        const FCut Cut = SelectCut(Nadir * (Earth + 150.0), Params, [&](const FTileKey&) { return TOptional<FHeightRange>(Peaks); });
        TestTrue(TEXT("over the cap it says it is binding"), Cut.bCapBinding);
        TestTrue(FString::Printf(TEXT("and gets under it (%d)"), Cut.Leaves.Num()), Cut.Leaves.Num() <= 600);
        TestTrue(TEXT("with the ship's chain still at MaxLevel"), Cut.Leaves.Contains(KeyAt(Nadir, Params.MaxLevel)));
        bool bCoarseFirst = true;
        for (int32 Level = 1; Level < Cut.SplitFactorByLevel.Num(); ++Level)
        {
            bCoarseFirst &= Cut.SplitFactorByLevel[Level] >= Cut.SplitFactorByLevel[Level - 1];
        }
        TestTrue(TEXT("lowering the coarsest levels' split factor first"), bCoarseFirst);
    }
    return true;
}

#endif
```

- [ ] **Step 2: Run and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh
```

Expected: fails to compile: `Surface/TerrainQuadtree.h: No such file or directory`.

- [ ] **Step 3: Write the quadtree**

Create `Source/DeepSpace/Surface/TerrainQuadtree.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/** One node of a world's cube-sphere quadtree (landing decision 6). */
struct DEEPSPACE_API FTileKey
{
    /** 0..5, SkySphereMesh's face table: +X, -X, +Y, -Y, +Z, -Z. */
    uint8 Face = 0;

    /** 0 is the whole face; each level halves the angle. */
    uint8 Level = 0;

    /** 0 .. 2^Level - 1 along the face's U and V. */
    uint32 X = 0;
    uint32 Y = 0;

    bool operator==(const FTileKey& Other) const
    {
        return Face == Other.Face && Level == Other.Level && X == Other.X && Y == Other.Y;
    }
    bool operator!=(const FTileKey& Other) const { return !(*this == Other); }

    FTileKey Parent() const { return FTileKey{ Face, static_cast<uint8>(Level - 1), X >> 1, Y >> 1 }; }

    /** Quadrant 0: (2X, 2Y); 1: (2X + 1, 2Y); 2: (2X, 2Y + 1); 3: (2X + 1, 2Y + 1). */
    FTileKey Child(int32 Quadrant) const
    {
        return FTileKey{ Face, static_cast<uint8>(Level + 1), (X << 1) | static_cast<uint32>(Quadrant & 1), (Y << 1) | static_cast<uint32>(Quadrant >> 1) };
    }

    int32 QuadrantInParent() const { return static_cast<int32>((X & 1) | ((Y & 1) << 1)); }

    /** Other is this node or lies under it. */
    bool Contains(const FTileKey& Other) const
    {
        return Other.Face == Face && Other.Level >= Level
            && (Other.X >> (Other.Level - Level)) == X && (Other.Y >> (Other.Level - Level)) == Y;
    }
};

FORCEINLINE uint32 GetTypeHash(const FTileKey& Key)
{
    return HashCombine(HashCombine(::GetTypeHash(static_cast<uint32>(Key.Face) | (static_cast<uint32>(Key.Level) << 8)), ::GetTypeHash(Key.X)), ::GetTypeHash(Key.Y));
}

/**
 * The terrain's quadtree as pure arithmetic (landing decision 6): six cube
 * faces on SM_SkyBody's equal-angle mapping, split into quadrants; CDLOD
 * measured from the ship's origin, never the camera, so it is the same in
 * tests and in play; each node's box its own height range, taken from its
 * parent's built vertices (the caller's BoundsOf); an explicit 2:1 rule; the
 * horizon; the ship's own chain to MaxLevel near the ground; and MaxTiles,
 * a ceiling that coarsens the farthest levels first. No UObject, no world.
 */
namespace TerrainQuadtree
{
    inline constexpr int32 CellsPerTile = 32;
    inline constexpr double TargetSpacingCm = 100.0;
    inline constexpr double DefaultSplitFactor = 2.0;
    inline constexpr int32 DefaultMaxTiles = 2500;

    /** The chain under the ship to this level is wanted from PrefetchAltitudeCm
     *  down, so the coarse cap is resident well before the handover. */
    inline constexpr int32 PrefetchLevel = 8;
    inline constexpr double PrefetchAltitudeCm = 1.0e8;

    /** Under this height above the ground the ship's own chain goes to
     *  MaxLevel, whatever the distance rule says: the drawn ground under the
     *  ship is then within GearClearance / 10 of the analytic ground. */
    inline constexpr double ChainFullAltitudeCm = 1.0e5;

    inline constexpr double MinSplitFactor = 1.0;
    inline constexpr double SplitFactorStep = 0.25;

    struct FFace
    {
        FVector3d Normal;
        FVector3d U;
        FVector3d V;
    };

    /** SkySphereMesh's face table, (U x V) = Normal. */
    DEEPSPACE_API const FFace& Face(int32 Index);

    /** ceil(log2(R x pi/2 / (32 x TargetSpacing))), at least 0. */
    DEEPSPACE_API int32 MaxLevel(double RadiusCm);

    /** The cube coordinate tan(angle) of grid line Index of Count across a
     *  face, as 2 Index - Count: exactly -1, 0 and 1 at the ends and middle,
     *  and odd, tan of |angle| signed, so a line shared by two faces or two
     *  levels is the same number to the last bit. */
    DEEPSPACE_API double FaceCoordinate(int64 TwiceIndexMinusCount, int64 Count);

    /** Unit direction of grid vertex (I, J), 0..CellsPerTile, of Key: the cube
     *  point normalised with a fixed order of operations, so shared vertices
     *  of neighbouring nodes are identical. */
    DEEPSPACE_API FVector3d GridDirection(const FTileKey& Key, int32 I, int32 J);

    /** The node's centre direction: its grid vertex (16, 16). */
    DEEPSPACE_API FVector3d CentreDirection(const FTileKey& Key);

    /** The node at Level holding direction D. */
    DEEPSPACE_API FTileKey KeyAt(const FVector3d& D, int32 Level);

    /** A node's edge at Level, R x (pi/2) / 2^Level, cm; and its vertex
     *  spacing, that over CellsPerTile. */
    DEEPSPACE_API double EdgeLengthCm(int32 Level, double RadiusCm);
    DEEPSPACE_API double SpacingCm(int32 Level, double RadiusCm);

    /** The largest angle from the node's centre to a corner. */
    DEEPSPACE_API double HalfDiagonalRadians(const FTileKey& Key);

    struct FHeightRange
    {
        double MinCm = 0.0;
        double MaxCm = 0.0;
    };

    struct FCutParams
    {
        double RadiusCm = 0.0;
        int32 MaxLevel = 0;
        double SplitFactor = DefaultSplitFactor;
        int32 MaxTiles = DefaultMaxTiles;

        /** The horizon's sphere: R plus the world's lowest height. */
        double OccluderRadiusCm = 0.0;

        /** The ship's origin above the ground under it, for the chain rule. */
        double GroundAltitudeCm = TNumericLimits<double>::Max();
    };

    struct FCut
    {
        /** The nodes to draw: the cut's leaves, culled nodes left out. */
        TArray<FTileKey> Leaves;

        /** The chain under the ship to PrefetchLevel, within PrefetchAltitudeCm. */
        TArray<FTileKey> Prefetch;

        /** The split factor each level was cut with: lowered from the
         *  coarsest level up while the cap binds. */
        TArray<double> SplitFactorByLevel;

        bool bCapBinding = false;
    };

    /** A node's height range, or unset when it cannot be known yet (its
     *  parent is not built): the cut then stops at the parent. */
    using FBoundsOf = TFunctionRef<TOptional<FHeightRange>(const FTileKey&)>;

    /** The cut for a ship at ShipFromCentre (cm, universe axes). BoundsOf of a
     *  root face must be set (the world's analytic bounds). */
    DEEPSPACE_API FCut SelectCut(const FVector3d& ShipFromCentre, const FCutParams& Params, FBoundsOf BoundsOf);

    /** False when the node, at its own top height, lies wholly below the
     *  horizon of the occluder sphere as seen from Ship. */
    DEEPSPACE_API bool Visible(const FTileKey& Key, const FVector3d& Ship, const FHeightRange& Range, const FCutParams& Params);

    /** The same-level nodes across each of Key's four edges. */
    DEEPSPACE_API TArray<FTileKey, TFixedAllocator<4>> EdgeNeighbours(const FTileKey& Key);

    /** No leaf has an edge neighbour more than one level coarser. */
    DEEPSPACE_API bool Balanced(TConstArrayView<FTileKey> Leaves);
}
```

Create `Source/DeepSpace/Surface/TerrainQuadtree.cpp`:

```cpp
#include "Surface/TerrainQuadtree.h"

namespace
{
    using namespace TerrainQuadtree;

    const FFace Faces[6] = {
        { FVector3d(1, 0, 0), FVector3d(0, 1, 0), FVector3d(0, 0, 1) },
        { FVector3d(-1, 0, 0), FVector3d(0, 0, 1), FVector3d(0, 1, 0) },
        { FVector3d(0, 1, 0), FVector3d(0, 0, 1), FVector3d(1, 0, 0) },
        { FVector3d(0, -1, 0), FVector3d(1, 0, 0), FVector3d(0, 0, 1) },
        { FVector3d(0, 0, 1), FVector3d(1, 0, 0), FVector3d(0, 1, 0) },
        { FVector3d(0, 0, -1), FVector3d(0, 1, 0), FVector3d(1, 0, 0) },
    };

    /** Normalised with one fixed order of operations: the same cube point
     *  gives the same direction whichever face built it. */
    FVector3d Normalised(const FVector3d& C)
    {
        const double L = FMath::Sqrt(C.X * C.X + C.Y * C.Y + C.Z * C.Z);
        return FVector3d(C.X / L, C.Y / L, C.Z / L);
    }

    FVector3d CubePoint(int32 FaceIndex, double CU, double CV)
    {
        const FFace& F = Faces[FaceIndex];
        return F.Normal + F.U * CU + F.V * CV;
    }

    /** A point a hair outside the middle of one of Key's edges: 0 bottom
     *  (J = 0), 1 right (I = 32), 2 top (J = 32), 3 left (I = 0). */
    FVector3d EdgeProbe(const FTileKey& Key, int32 Edge)
    {
        const double Count = static_cast<double>(1ll << Key.Level);
        const double U = Edge == 1 ? Key.X + 1.0 + 1.0e-3 : Edge == 3 ? Key.X - 1.0e-3 : Key.X + 0.5;
        const double V = Edge == 2 ? Key.Y + 1.0 + 1.0e-3 : Edge == 0 ? Key.Y - 1.0e-3 : Key.Y + 0.5;
        const double AU = 0.5 * UE_DOUBLE_PI * (U / Count - 0.5);
        const double AV = 0.5 * UE_DOUBLE_PI * (V / Count - 0.5);
        return Normalised(CubePoint(Key.Face, FMath::Tan(AU), FMath::Tan(AV)));
    }

    double DistanceToNode(const FVector3d& Ship, const FTileKey& Key, const FHeightRange& Range, double RadiusCm)
    {
        const FVector3d Centre = CentreDirection(Key) * (RadiusCm + 0.5 * (Range.MinCm + Range.MaxCm));
        const double Reach = (RadiusCm + Range.MaxCm) * HalfDiagonalRadians(Key) + 0.5 * (Range.MaxCm - Range.MinCm);
        return FMath::Max(0.0, (Ship - Centre).Size() - Reach);
    }

    struct FCutter
    {
        const FVector3d& Ship;
        const FVector3d Nadir;
        const FCutParams& Params;
        FBoundsOf BoundsOf;
        const TArray<double>& Factors;
        TArray<FTileKey>& Leaves;

        void Visit(const FTileKey& Key, const FHeightRange& Range)
        {
            if (!Visible(Key, Ship, Range, Params))
            {
                return;
            }
            if (Key.Level < Params.MaxLevel)
            {
                const bool bChain = KeyAt(Nadir, Key.Level) == Key;
                const double Factor = bChain ? Params.SplitFactor : Factors[Key.Level];
                const bool bSplit = DistanceToNode(Ship, Key, Range, Params.RadiusCm) < Factor * EdgeLengthCm(Key.Level, Params.RadiusCm)
                                 || (bChain && Params.GroundAltitudeCm < ChainFullAltitudeCm);
                if (bSplit)
                {
                    TOptional<FHeightRange> Children[4];
                    bool bKnown = true;
                    for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
                    {
                        Children[Quadrant] = BoundsOf(Key.Child(Quadrant));
                        bKnown &= Children[Quadrant].IsSet();
                    }
                    if (bKnown)
                    {
                        for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
                        {
                            Visit(Key.Child(Quadrant), *Children[Quadrant]);
                        }
                        return;
                    }
                }
            }
            Leaves.Add(Key);
        }
    };

    /** Split any leaf more than one level coarser than an edge neighbour of
     *  another, until none is. */
    void Balance(TArray<FTileKey>& Leaves)
    {
        TSet<FTileKey> Set(Leaves);
        for (bool bChanged = true; bChanged;)
        {
            bChanged = false;
            for (const FTileKey& Leaf : Set.Array())
            {
                if (!Set.Contains(Leaf))
                {
                    continue;
                }
                for (int32 Edge = 0; Edge < 4; ++Edge)
                {
                    const FVector3d Outside = EdgeProbe(Leaf, Edge);
                    for (int32 Level = Leaf.Level - 2; Level >= 0; --Level)
                    {
                        const FTileKey Coarse = KeyAt(Outside, Level);
                        if (Set.Contains(Coarse))
                        {
                            Set.Remove(Coarse);
                            for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
                            {
                                Set.Add(Coarse.Child(Quadrant));
                            }
                            bChanged = true;
                            break;
                        }
                    }
                }
            }
        }
        Leaves = Set.Array();
        Leaves.Sort([](const FTileKey& A, const FTileKey& B)
        {
            return A.Level != B.Level ? A.Level < B.Level : A.Face != B.Face ? A.Face < B.Face : A.Y != B.Y ? A.Y < B.Y : A.X < B.X;
        });
    }
}

const TerrainQuadtree::FFace& TerrainQuadtree::Face(int32 Index)
{
    return Faces[FMath::Clamp(Index, 0, 5)];
}

int32 TerrainQuadtree::MaxLevel(double RadiusCm)
{
    const double Tiles = RadiusCm * 0.5 * UE_DOUBLE_PI / (CellsPerTile * TargetSpacingCm);
    return Tiles <= 1.0 ? 0 : FMath::Min(24, FMath::CeilToInt(FMath::Log2(Tiles)));
}

double TerrainQuadtree::FaceCoordinate(int64 TwiceIndexMinusCount, int64 Count)
{
    if (TwiceIndexMinusCount >= Count)
    {
        return 1.0;
    }
    if (TwiceIndexMinusCount <= -Count)
    {
        return -1.0;
    }
    if (TwiceIndexMinusCount == 0)
    {
        return 0.0;
    }
    const double Magnitude = FMath::Tan(static_cast<double>(FMath::Abs(TwiceIndexMinusCount)) * (0.25 * UE_DOUBLE_PI / static_cast<double>(Count)));
    return TwiceIndexMinusCount < 0 ? -Magnitude : Magnitude;
}

FVector3d TerrainQuadtree::GridDirection(const FTileKey& Key, int32 I, int32 J)
{
    const int64 Count = static_cast<int64>(CellsPerTile) << Key.Level;
    const int64 GU = static_cast<int64>(Key.X) * CellsPerTile + I;
    const int64 GV = static_cast<int64>(Key.Y) * CellsPerTile + J;
    return Normalised(CubePoint(Key.Face, FaceCoordinate(2 * GU - Count, Count), FaceCoordinate(2 * GV - Count, Count)));
}

FVector3d TerrainQuadtree::CentreDirection(const FTileKey& Key)
{
    return GridDirection(Key, CellsPerTile / 2, CellsPerTile / 2);
}

FTileKey TerrainQuadtree::KeyAt(const FVector3d& D, int32 Level)
{
    const FVector3d A(FMath::Abs(D.X), FMath::Abs(D.Y), FMath::Abs(D.Z));
    int32 FaceIndex = A.X >= A.Y && A.X >= A.Z ? (D.X >= 0.0 ? 0 : 1) : A.Y >= A.Z ? (D.Y >= 0.0 ? 2 : 3) : (D.Z >= 0.0 ? 4 : 5);
    const FFace& F = Faces[FaceIndex];
    const double Along = FVector3d::DotProduct(D, F.Normal);
    const double AU = FMath::Atan(FVector3d::DotProduct(D, F.U) / Along);
    const double AV = FMath::Atan(FVector3d::DotProduct(D, F.V) / Along);
    const int64 Count = 1ll << Level;
    FTileKey Key;
    Key.Face = static_cast<uint8>(FaceIndex);
    Key.Level = static_cast<uint8>(Level);
    Key.X = static_cast<uint32>(FMath::Clamp<int64>(FMath::FloorToInt64((AU / (0.5 * UE_DOUBLE_PI) + 0.5) * Count), 0, Count - 1));
    Key.Y = static_cast<uint32>(FMath::Clamp<int64>(FMath::FloorToInt64((AV / (0.5 * UE_DOUBLE_PI) + 0.5) * Count), 0, Count - 1));
    return Key;
}

double TerrainQuadtree::EdgeLengthCm(int32 Level, double RadiusCm)
{
    return RadiusCm * 0.5 * UE_DOUBLE_PI / static_cast<double>(1ll << Level);
}

double TerrainQuadtree::SpacingCm(int32 Level, double RadiusCm)
{
    return EdgeLengthCm(Level, RadiusCm) / CellsPerTile;
}

double TerrainQuadtree::HalfDiagonalRadians(const FTileKey& Key)
{
    const FVector3d Centre = CentreDirection(Key);
    double Widest = 0.0;
    for (const FVector3d& Corner : { GridDirection(Key, 0, 0), GridDirection(Key, CellsPerTile, 0),
                                     GridDirection(Key, 0, CellsPerTile), GridDirection(Key, CellsPerTile, CellsPerTile) })
    {
        Widest = FMath::Max(Widest, FMath::Atan2(FVector3d::CrossProduct(Centre, Corner).Size(), FVector3d::DotProduct(Centre, Corner)));
    }
    return Widest;
}

bool TerrainQuadtree::Visible(const FTileKey& Key, const FVector3d& Ship, const FHeightRange& Range, const FCutParams& Params)
{
    const double Distance = Ship.Size();
    const double Occluder = FMath::Max(Params.OccluderRadiusCm, 1.0);
    if (Distance <= Occluder)
    {
        return true;
    }
    const double Top = FMath::Max(Params.RadiusCm + Range.MaxCm, Occluder);
    const double Horizon = FMath::Acos(Occluder / Distance) + FMath::Acos(FMath::Min(1.0, Occluder / Top));
    const FVector3d Centre = CentreDirection(Key);
    const double Apart = FMath::Atan2(FVector3d::CrossProduct(Ship, Centre).Size(), FVector3d::DotProduct(Ship, Centre));
    return Apart - HalfDiagonalRadians(Key) <= Horizon;
}

TArray<FTileKey, TFixedAllocator<4>> TerrainQuadtree::EdgeNeighbours(const FTileKey& Key)
{
    TArray<FTileKey, TFixedAllocator<4>> Out;
    for (int32 Edge = 0; Edge < 4; ++Edge)
    {
        Out.Add(KeyAt(EdgeProbe(Key, Edge), Key.Level));
    }
    return Out;
}

bool TerrainQuadtree::Balanced(TConstArrayView<FTileKey> Leaves)
{
    const TSet<FTileKey> Set(Leaves);
    for (const FTileKey& Leaf : Leaves)
    {
        for (int32 Edge = 0; Edge < 4; ++Edge)
        {
            const FVector3d Outside = EdgeProbe(Leaf, Edge);
            for (int32 Level = Leaf.Level - 2; Level >= 0; --Level)
            {
                if (Set.Contains(KeyAt(Outside, Level)))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

TerrainQuadtree::FCut TerrainQuadtree::SelectCut(const FVector3d& ShipFromCentre, const FCutParams& Params, FBoundsOf BoundsOf)
{
    FCut Cut;
    Cut.SplitFactorByLevel.Init(Params.SplitFactor, Params.MaxLevel + 1);
    const FVector3d Nadir = ShipFromCentre.GetSafeNormal();
    if (ShipFromCentre.Size() - Params.RadiusCm < PrefetchAltitudeCm)
    {
        for (int32 Level = 0; Level <= FMath::Min(PrefetchLevel, Params.MaxLevel); ++Level)
        {
            Cut.Prefetch.Add(KeyAt(Nadir, Level));
        }
    }
    for (;;)
    {
        Cut.Leaves.Reset();
        FCutter Cutter{ ShipFromCentre, Nadir, Params, BoundsOf, Cut.SplitFactorByLevel, Cut.Leaves };
        for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
        {
            const FTileKey Root{ static_cast<uint8>(FaceIndex), 0, 0, 0 };
            if (const TOptional<FHeightRange> Range = BoundsOf(Root))
            {
                Cutter.Visit(Root, *Range);
            }
        }
        Balance(Cut.Leaves);
        if (Cut.Leaves.Num() <= Params.MaxTiles)
        {
            break;
        }
        // Over the ceiling: the coarsest level that can still be lowered is
        // lowered a step -- the farthest nodes first. The chain under the
        // ship always cuts at the full factor, so it is never coarsened.
        Cut.bCapBinding = true;
        int32 Lower = INDEX_NONE;
        for (int32 Level = 0; Level < Params.MaxLevel && Lower == INDEX_NONE; ++Level)
        {
            if (Cut.SplitFactorByLevel[Level] > MinSplitFactor + 1.0e-9)
            {
                Lower = Level;
            }
        }
        if (Lower == INDEX_NONE)
        {
            break;
        }
        Cut.SplitFactorByLevel[Lower] = FMath::Max(MinSplitFactor, Cut.SplitFactorByLevel[Lower] - SplitFactorStep);
    }
    return Cut;
}
```

- [ ] **Step 4: Run: PASS, and record the counts**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Surface.Quadtree; \
grep "M triangles" Saved/Logs/DeepSpace.log | tail -6
```

Expected: `passed: 1`; six lines of counts. Compare them with decision 6's (860 at 50 km, 1,100
at 10 km, 1,600 at 1 km, 2,200 at 1.5 m); if they differ by more than a factor of 1.5 either way,
write the measured row into decision 6's *What that costs* paragraph in the spec, and if 1.5 m
exceeds 2,500 raise `DefaultMaxTiles` only with the gate's memory number (Task B1) in hand.

- [ ] **Step 5: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
git add Source/DeepSpace/Surface/TerrainQuadtree.h Source/DeepSpace/Surface/TerrainQuadtree.cpp Source/DeepSpace/Tests/TerrainQuadtreeTest.cpp && \
git add -u docs/superpowers/specs/2026-09-27-landing-design.md && \
git commit -m "$(cat <<'EOF'
feat(terrain): the quadtree -- equal-angle cube faces, CDLOD from the ship, 2:1, horizon, chain, tile cap

Shared edges are the same directions to the last bit across levels and
faces (odd tan of exact angles, one normalisation order); boxes come from
each node's own height range; the ship's chain goes to MaxLevel under
1 km; MaxTiles coarsens the coarsest levels first and never the chain.
DeepSpace.Surface.Quadtree reports the counts per altitude.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Source/DeepSpace/Surface/TerrainQuadtree.cpp '                            bChanged = true;
                            break;' '                            break;' DeepSpace.Surface.Quadtree && \
Tools/mutate.sh Source/DeepSpace/Surface/TerrainQuadtree.cpp '|| (bChain && Params.GroundAltitudeCm < ChainFullAltitudeCm);' ';' DeepSpace.Surface.Quadtree && \
Tools/mutate.sh Source/DeepSpace/Surface/TerrainQuadtree.cpp 'return TwiceIndexMinusCount < 0 ? -Magnitude : Magnitude;' 'return FMath::Tan(static_cast<double>(TwiceIndexMinusCount) * (0.25 * UE_DOUBLE_PI / static_cast<double>(Count)));' DeepSpace.Surface.Quadtree && \
./build.sh
```

Expected: `KILLED` three times (the balance stops after one pass; the chain stops short near the
ground; a tangent that is not computed odd breaks bit-identical edges across a face's mirror).
If the third survives on this machine's libm (whose `tan` may be exactly odd), record that in the
comment on `FaceCoordinate`: the explicit sign is kept as a guarantee the platform does not give.

- [ ] **Step 7: Review focus 2 -- the ship over a cube corner and a cube edge**

`DeepSpace.Surface.Quadtree` sweeps the cut from one nadir in the middle of a face. The
equal-angle cube's seams -- an edge, where two faces meet, and a corner, where three do -- are
where a cube-sphere quadtree's neighbour finding, 2:1 balance and coverage break, and a ship can
land on any of them. In `TerrainQuadtreeTest.cpp`, beside the first declaration, add

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainQuadtreeSeamsTest, "DeepSpace.Surface.QuadtreeAtCubeSeams",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

and before `#endif`:

```cpp
bool FTerrainQuadtreeSeamsTest::RunTest(const FString& Parameters)
{
    using namespace TerrainQuadtreeLocal;
    const double Earth = UniverseUnits::CmPerEarthRadius;
    const FHeightRange Peaks{ 0.0, 2.0e5 };
    FRandomStream Random(23);
    // Two corners (three faces meet) and the middle of an edge (two do).
    const FVector3d Nadirs[] = { FVector3d(1.0, 1.0, 1.0).GetSafeNormal(), FVector3d(-1.0, 1.0, -1.0).GetSafeNormal(),
                                 FVector3d(1.0, 0.0, 1.0).GetSafeNormal() };
    for (const FVector3d& Nadir : Nadirs)
    {
        for (const double Altitude : { 150.0, 1.0e5, 5.0e6 })
        {
            FCutParams Params;
            Params.RadiusCm = Earth;
            Params.MaxLevel = MaxLevel(Earth);
            Params.MaxTiles = 1000000;
            Params.OccluderRadiusCm = Earth;
            Params.GroundAltitudeCm = Altitude;
            const FCut Cut = SelectCut(Nadir * (Earth + Altitude), Params, [&](const FTileKey&) { return TOptional<FHeightRange>(Peaks); });
            const TSet<FTileKey> Leaves(Cut.Leaves);
            const FString At = FString::Printf(TEXT("(%.2f, %.2f, %.2f) at %.0f m"), Nadir.X, Nadir.Y, Nadir.Z, Altitude / 100.0);

            bool bNoOverlap = true;
            for (const FTileKey& Leaf : Cut.Leaves)
            {
                for (FTileKey Up = Leaf; Up.Level > 0;)
                {
                    Up = Up.Parent();
                    bNoOverlap &= !Leaves.Contains(Up);
                }
            }
            TestTrue(At + TEXT(": no leaf overlaps another"), bNoOverlap);
            TestTrue(At + TEXT(": 2:1 between neighbours, across the faces"), Balanced(Cut.Leaves));

            const double Horizon = FMath::Acos(Earth / (Earth + Altitude));
            int32 Uncovered = 0;
            for (int32 Sample = 0; Sample < 2000; ++Sample)
            {
                const FVector3d Tangent = FVector3d::CrossProduct(Nadir, FVector3d(Random.GetUnitVector())).GetSafeNormal();
                const double Angle = Horizon * FMath::Sqrt(Random.FRand()) * 0.999;
                const FVector3d D = Nadir * FMath::Cos(Angle) + Tangent * FMath::Sin(Angle);
                Uncovered += LeafAt(Leaves, D, Params.MaxLevel).IsSet() ? 0 : 1;
            }
            TestEqual(At + TEXT(": every direction inside the horizon is drawn"), Uncovered, 0);
            if (Altitude < ChainFullAltitudeCm)
            {
                TestTrue(At + TEXT(": the ship's own chain reaches MaxLevel"), Leaves.Contains(KeyAt(Nadir, Params.MaxLevel)));
            }

            // An oracle that shares nothing with KeyAt or EdgeProbe: only the
            // leaves and GridDirection. Every leaf edge on its face's border,
            // well inside the horizon, must meet the leaves of the other face
            // vertex for vertex -- all 33 directions (a neighbour as fine or
            // finer) or exactly the 17 even ones (a neighbour one level
            // coarser). Fewer is a crack; every fourth is a 2:1 break.
            TMap<uint8, TSet<FVector3d>> BorderOf;
            const auto BorderEdges = [](const FTileKey& Leaf, TArray<TArray<FVector3d>>& Out)
            {
                const uint32 Last = (1u << Leaf.Level) - 1u;
                for (int32 Side = 0; Side < 4; ++Side)
                {
                    const bool bOnBorder = Side == 0 ? Leaf.Y == 0 : Side == 1 ? Leaf.X == Last : Side == 2 ? Leaf.Y == Last : Leaf.X == 0;
                    if (!bOnBorder)
                    {
                        continue;
                    }
                    TArray<FVector3d>& Edge = Out.AddDefaulted_GetRef();
                    for (int32 K = 0; K <= CellsPerTile; ++K)
                    {
                        Edge.Add(Side == 0 ? GridDirection(Leaf, K, 0) : Side == 1 ? GridDirection(Leaf, CellsPerTile, K)
                                 : Side == 2 ? GridDirection(Leaf, K, CellsPerTile) : GridDirection(Leaf, 0, K));
                    }
                }
            };
            for (const FTileKey& Leaf : Cut.Leaves)
            {
                TArray<TArray<FVector3d>> Edges;
                BorderEdges(Leaf, Edges);
                for (const TArray<FVector3d>& Edge : Edges)
                {
                    BorderOf.FindOrAdd(Leaf.Face).Append(Edge);
                }
            }
            int32 SeamEdges = 0;
            int32 Broken = 0;
            for (const FTileKey& Leaf : Cut.Leaves)
            {
                TArray<TArray<FVector3d>> Edges;
                BorderEdges(Leaf, Edges);
                for (const TArray<FVector3d>& Edge : Edges)
                {
                    bool bInside = true;
                    for (const FVector3d& D : Edge)
                    {
                        bInside &= FMath::Acos(FMath::Clamp(FVector3d::DotProduct(D, Nadir), -1.0, 1.0)) < 0.9 * Horizon;
                    }
                    if (!bInside)
                    {
                        continue;
                    }
                    ++SeamEdges;
                    int32 Met = 0;
                    int32 EvenMet = 0;
                    for (int32 K = 0; K <= CellsPerTile; ++K)
                    {
                        bool bFound = false;
                        for (const TPair<uint8, TSet<FVector3d>>& Other : BorderOf)
                        {
                            bFound |= Other.Key != Leaf.Face && Other.Value.Contains(Edge[K]);
                        }
                        Met += bFound ? 1 : 0;
                        EvenMet += bFound && K % 2 == 0 ? 1 : 0;
                    }
                    const bool bWhole = Met == CellsPerTile + 1;
                    const bool bHalf = EvenMet == CellsPerTile / 2 + 1 && Met == EvenMet;
                    Broken += bWhole || bHalf ? 0 : 1;
                }
            }
            TestTrue(At + FString::Printf(TEXT(": the cut has seam edges inside the horizon to check (%d)"), SeamEdges), SeamEdges > 0);
            TestEqual(At + TEXT(": across every seam the other face's leaves meet each border edge vertex for vertex, at most one level apart"),
                      Broken, 0);
        }
    }
    return true;
}
```

Run `./build.sh && ./test.sh DeepSpace.Surface.QuadtreeAtCubeSeams`. Expected: `passed: 1`. If it
fails, the fault is in `EdgeNeighbours` or `KeyAt` across a face: fix the quadtree, never the
test, and re-run `DeepSpace.Surface.Quadtree` too.

- [ ] **Step 8: Commit, then prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
git add Source/DeepSpace/Surface/TerrainQuadtree.h Source/DeepSpace/Surface/TerrainQuadtree.cpp Source/DeepSpace/Tests/TerrainQuadtreeTest.cpp && \
git commit -m "$(cat <<'MSG'
test(terrain): the cut at the cube's seams -- an edge and two corners (review focus 2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
MSG
)" && \
Tools/mutate.sh Source/DeepSpace/Surface/TerrainQuadtree.cpp '                            bChanged = true;
                            break;' '                            break;' DeepSpace.Surface.QuadtreeAtCubeSeams && \
Tools/mutate.sh Source/DeepSpace/Surface/TerrainQuadtree.cpp 'return Normalised(CubePoint(Key.Face, FMath::Tan(AU), FMath::Tan(AV)));' 'return Normalised(CubePoint(Key.Face, FMath::Clamp(FMath::Tan(AU), -1.0, 1.0), FMath::Clamp(FMath::Tan(AV), -1.0, 1.0)));' DeepSpace.Surface.QuadtreeAtCubeSeams && \
Tools/mutate.sh Source/DeepSpace/Surface/TerrainQuadtree.cpp 'return Normalised(CubePoint(Key.Face, FMath::Tan(AU), FMath::Tan(AV)));' 'return Normalised(CubePoint(Key.Face, FMath::Clamp(FMath::Tan(AU), -1.0, 1.0), FMath::Clamp(FMath::Tan(AV), -1.0, 1.0)));' DeepSpace.Surface.Quadtree; \
./build.sh
```

Expected: the first two `KILLED` by `QuadtreeAtCubeSeams`; the third `SURVIVED` by
`DeepSpace.Surface.Quadtree` (the `;` before `./build.sh` is deliberate: `mutate.sh` exits non-zero
on a survivor). The first is the balance stopping after one pass. The second is seam-specific: a
probe past a face's edge clamped back onto its own face, so neither `EdgeNeighbours` nor the balance
ever looks across a seam. Within a face nothing changes -- which is why the mid-face sweep survives
it, and why this test exists -- while the seams oracle, which never calls `EdgeProbe` or `KeyAt`,
finds the cross-face edges no longer meet vertex for vertex. If the second survives
`QuadtreeAtCubeSeams`, the sweep is not refining across a seam: add the altitude 2,000 cm, where the
chain is deepest there, and re-run until it is killed; never weaken the oracle.

---

## Task 34 (T4): the tile -- 33 x 33 and a skirt, local to its pivot, normals in UV channels, heights morphed

**Owner:** T. **Depends on:** T1, T3, F2 (`IGroundField`). Spec decisions 6 (*What each tile
carries*, *Async build*, *Seams and pops*), 7 (*The relief grows in*).

**Files:**
- Create: `Source/DeepSpace/Surface/TerrainTile.h`, `Source/DeepSpace/Surface/TerrainTile.cpp`
- Create: `Source/DeepSpace/Tests/TerrainTileTest.cpp` (`DeepSpace.Surface.Tile`)

**Interfaces:**
- Consumes: `IGroundField`, `ShipGround::FromRelief`, `ShipGround::NormalAt`'s composition,
  `TerrainQuadtree::*`, `GroundFixtures::*` (tests).
- Produces:
  `struct FTileBuild { FTileKey Key; FVector3d Pivot; double RadiusCm; double SpacingCm; TArray<FVector3f> Positions; TArray<FVector3f> Normals; TArray<double> Heights; TArray<FVector3d> Directions; TerrainQuadtree::FHeightRange Range; TerrainQuadtree::FHeightRange QuadrantRange[4]; double MaxEdgeErrorCm; double SkirtDepthCm; double BuildSeconds; }`;
  `TerrainTile::{Cells, GridVerts, SkirtVerts, VertexCount, TriangleCount, HandoverAltitudeCm (5e6), HeightUVScaleCm (1e5)}`;
  `const TArray<int32>& Indices()`; `FTileBuild Build(const IGroundField&, const FTileKey&)`;
  `double MorphFraction(double AltitudeCm, double DriveFloorAltitudeCm, double HandoverCm = HandoverAltitudeCm)`;
  `FVector3d MorphedPosition(const FTileBuild&, int32 Vertex, double Morph)`;
  `TOptional<double> SampleHeight(const FTileBuild&, const FVector3d& D)`;
  `TerrainQuadtree::FHeightRange ChildRange(const FTileBuild& Parent, int32 Quadrant, const IGroundField&)`;
  `FVector2f UV1Of(const FTileBuild&, int32 Vertex)`, `FVector2f UV2Of(const FTileBuild&, int32 Vertex)`.

A finding for the record: `ProceduralMeshComponent` uploads UVs through `FStaticMeshVertexBuffer`
at its default **half** precision (`bUseFullPrecisionUVs` false, `StaticMesh.cpp`'s
`InitFromDynamicVertex`), not the float the spec's decision 6 assumed. Normals in half precision
step about 0.03 degrees, still fifteen times finer than the 8-bit packed normal the spec rejected;
the height cannot ride in centimetres (half's largest value is 65,504), so UV2.y carries it in
**kilometres** (`HeightUVScaleCm`). A half in [8, 16) steps by 2^-7, so near a 10 km peak the height
is quantised to about 7.8 m (3.9 m worst rounding), and it drives only the morph, which is 1 -- no
offset at all -- under the drive floor. Between the handover and the drive floor a vertex's drawn
height is off by at most 3.9 m x (1 - M), largest at the handover itself: 390 cm seen from 50 km is
0.19 of a 4K pixel. The tile test reports that term beside the split pop and holds it under a pixel. `UTerrainTileComponent` (Task T5)
sets full precision.

- [ ] **Step 1: Write the failing test**

Create `Source/DeepSpace/Tests/TerrainTileTest.cpp`:

```cpp
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipLanding.h"
#include "Surface/TerrainTile.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decisions 6 and 7's tile, pure: the fixed topology wound as the
 * engine's meshes are, shared edges of same-level neighbours identical,
 * vertices local to a pivot with sub-millimetre precision near the ship, a
 * skirt deep enough for the edge error and the omitted bands, the mesh under
 * the ship within GearClearance / 10 of the analytic ground, and heights
 * that are the sphere at the handover and the full relief at the drive
 * floor. Reports a tile's build time and the largest split pop in 4K pixels.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainTileTest, "DeepSpace.Surface.Tile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainTileTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    using namespace TerrainTile;
    const FGroundFieldRef Ground = ShipGround::FromRelief(FixtureParams());
    const double R = Ground->RadiusCm();
    const int32 Finest = TerrainQuadtree::MaxLevel(R);

    TestEqual(TEXT("2,304 triangles, three indices each"), Indices().Num(), 3 * TriangleCount);
    bool bInRange = true;
    for (const int32 Index : Indices())
    {
        bInRange &= Index >= 0 && Index < VertexCount;
    }
    TestTrue(TEXT("every index names a vertex"), bInRange);

    FRandomStream Random(8);
    const auto RandomKey = [&](int32 Level)
    {
        FTileKey Key = TerrainQuadtree::KeyAt(FVector3d(Random.GetUnitVector()), Level);
        return Key;
    };

    // Winding: seen from outside, (B - A) x (C - A) points into the world.
    {
        const FTileBuild Tile = Build(*Ground, RandomKey(12));
        int32 Inward = 0;
        for (int32 T = 0; T < 2 * Cells * Cells; ++T)
        {
            const FVector3d A = Tile.Pivot + FVector3d(Tile.Positions[Indices()[3 * T]]);
            const FVector3d B = Tile.Pivot + FVector3d(Tile.Positions[Indices()[3 * T + 1]]);
            const FVector3d C = Tile.Pivot + FVector3d(Tile.Positions[Indices()[3 * T + 2]]);
            Inward += FVector3d::DotProduct(FVector3d::CrossProduct(B - A, C - A), A.GetSafeNormal()) < 0.0 ? 1 : 0;
        }
        TestEqual(TEXT("every grid triangle is wound as the engine's own meshes"), Inward, 2 * Cells * Cells);
    }

    // Shared edges: same directions and heights, to the last bit; positions
    // agree to float precision across the two pivots.
    int32 Identical = 0;
    int32 Compared = 0;
    double WorstGap = 0.0;
    for (int32 Trial = 0; Trial < 12; ++Trial)
    {
        const FTileKey Key = RandomKey(Random.RandRange(8, Finest));
        const FTileBuild Tile = Build(*Ground, Key);
        for (const FTileKey& Neighbour : TerrainQuadtree::EdgeNeighbours(Key))
        {
            const FTileBuild Other = Build(*Ground, Neighbour);
            for (int32 V = 0; V < GridVerts; ++V)
            {
                for (int32 W = 0; W < GridVerts; ++W)
                {
                    if (Tile.Directions[V] == Other.Directions[W])
                    {
                        ++Compared;
                        Identical += Tile.Heights[V] == Other.Heights[W] ? 1 : 0;
                        WorstGap = FMath::Max(WorstGap, ((Tile.Pivot + FVector3d(Tile.Positions[V])) - (Other.Pivot + FVector3d(Other.Positions[W]))).Size());
                    }
                }
            }
        }
    }
    TestTrue(TEXT("neighbours share their edge vertices"), Compared >= 12 * 4 * (Cells + 1));
    TestEqual(TEXT("and the heights there are identical"), Identical, Compared);
    TestTrue(FString::Printf(TEXT("and the positions agree across pivots (worst %.4f cm)"), WorstGap), WorstGap < 1.0);

    // Precision near the ship: a finest tile's vertices hold sub-millimetre.
    {
        const FTileBuild Tile = Build(*Ground, RandomKey(Finest));
        double Worst = 0.0;
        for (int32 V = 0; V < GridVerts; ++V)
        {
            const FVector3d Exact = Tile.Directions[V] * (R + Tile.Heights[V]);
            Worst = FMath::Max(Worst, ((Tile.Pivot + FVector3d(Tile.Positions[V])) - Exact).Size());
        }
        TestTrue(FString::Printf(TEXT("a finest tile's float vertices are within 0.1 mm (%.5f cm)"), Worst), Worst < 0.01);
        TestTrue(TEXT("its skirt covers its edge error and the bands it omits"),
                 Tile.SkirtDepthCm >= Tile.MaxEdgeErrorCm + Ground->OmittedBoundCm(Tile.SpacingCm));
    }

    // The mesh under the ship is the ground, to GearClearance / 10.
    {
        double Worst = 0.0;
        for (int32 Trial = 0; Trial < 50; ++Trial)
        {
            const FVector3d D(Random.GetUnitVector());
            const FTileBuild Tile = Build(*Ground, TerrainQuadtree::KeyAt(D, Finest));
            const TOptional<double> Drawn = SampleHeight(Tile, D);
            Worst = FMath::Max(Worst, Drawn ? FMath::Abs(*Drawn - Ground->Height(D, 0.0)) : 1.0e9);
        }
        TestTrue(FString::Printf(TEXT("the finest mesh is within 15 cm of the analytic ground (worst %.2f cm)"), Worst),
                 Worst <= ShipLanding::DefaultGearClearanceCm / 10.0);
    }

    // The morph: the sphere at the handover, the full relief at the drive floor.
    {
        const double FloorAlt = 1.02e6 + Ground->MaxHeightCm();
        TestEqual(TEXT("M is 0 at the handover"), MorphFraction(HandoverAltitudeCm, FloorAlt), 0.0);
        TestEqual(TEXT("and 1 at the drive floor"), MorphFraction(FloorAlt, FloorAlt), 1.0);
        TestEqual(TEXT("and under it"), MorphFraction(1.0e3, FloorAlt), 1.0);
        TestTrue(TEXT("and between, between"), MorphFraction(3.0e6, FloorAlt) > 0.0 && MorphFraction(3.0e6, FloorAlt) < 1.0);
        const FTileBuild Tile = Build(*Ground, RandomKey(14));
        double OffSphere = 0.0;
        double OffRelief = 0.0;
        for (int32 V = 0; V < GridVerts; ++V)
        {
            OffSphere = FMath::Max(OffSphere, FMath::Abs((Tile.Pivot + MorphedPosition(Tile, V, 0.0)).Size() - R));
            OffRelief = FMath::Max(OffRelief, (MorphedPosition(Tile, V, 1.0) - FVector3d(Tile.Positions[V])).Size());
        }
        TestTrue(FString::Printf(TEXT("tile heights are the sphere at M = 0 (%.4f cm)"), OffSphere), OffSphere < 0.05);
        TestTrue(TEXT("and the full relief at M = 1"), OffRelief < 1.0e-6);
        TestTrue(TEXT("the height rides UV2.y in kilometres"),
                 FMath::IsNearlyEqual(UV2Of(Tile, 7).Y * HeightUVScaleCm, Tile.Heights[7], 1.0));
    }

    // Reported: build cost, and the largest vertex pop at a split in 4K pixels.
    {
        const double Start = FPlatformTime::Seconds();
        double WorstPixels = 0.0;
        constexpr int32 Builds = 20;
        for (int32 Trial = 0; Trial < Builds / 2; ++Trial)
        {
            const FTileKey Child = RandomKey(Random.RandRange(10, Finest));
            const FTileBuild Coarse = Build(*Ground, Child.Parent());
            const FTileBuild Fine = Build(*Ground, Child);
            const double SplitDistance = TerrainQuadtree::DefaultSplitFactor * TerrainQuadtree::EdgeLengthCm(Coarse.Key.Level, R);
            for (int32 V = 0; V < GridVerts; ++V)
            {
                if (const TOptional<double> Before = SampleHeight(Coarse, Fine.Directions[V]))
                {
                    WorstPixels = FMath::Max(WorstPixels, FMath::Atan(FMath::Abs(Fine.Heights[V] - *Before) / SplitDistance) / (2.0 / 3840.0));
                }
            }
        }
        const double MsEach = (FPlatformTime::Seconds() - Start) * 1000.0 / Builds;
        AddInfo(FString::Printf(TEXT("a tile builds in %.2f ms; the largest split pop is %.2f 4K pixels"), MsEach, WorstPixels));
        TestTrue(TEXT("both are numbers"), FMath::IsFinite(MsEach) && FMath::IsFinite(WorstPixels));
        // PMC uploads UV2.y (the height, km) as a half: 10 mantissa bits, so
        // a height in [2^e, 2^(e+1)) km rounds by at most 2^(e-11) km. The
        // morph scales that by (1 - M), which is 1 only at the handover.
        const double PeakKm = FMath::Max(Ground->MaxHeightCm(), 1.0) / HeightUVScaleCm;
        const double HalfRoundingCm = FMath::Pow(2.0, FMath::FloorToDouble(FMath::Log2(PeakKm)) - 11.0) * HeightUVScaleCm;
        const double HalfPixels = FMath::Atan(HalfRoundingCm / HandoverAltitudeCm) / (2.0 / 3840.0);
        AddInfo(FString::Printf(TEXT("the half-float height rounds by at most %.0f cm: %.2f 4K pixels at the handover"), HalfRoundingCm, HalfPixels));
        TestTrue(TEXT("the half-float height's rounding stays under a pixel at the handover"), HalfPixels < 1.0);
    }
    return true;
}

#endif
```

- [ ] **Step 2: Run and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git merge -q feat/landing-b && ./build.sh
```

Expected: fails to compile: `Surface/TerrainTile.h: No such file or directory`.

- [ ] **Step 3: Write the tile**

Create `Source/DeepSpace/Surface/TerrainTile.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Surface/GroundField.h"
#include "Surface/TerrainQuadtree.h"

/** One built tile (landing decision 6): what the mesh gets and what the
 *  LOD, the tests and the drawn-height check read. Built off the game
 *  thread; plain data. */
struct DEEPSPACE_API FTileBuild
{
    FTileKey Key;

    /** The ground under the node's centre, from the world's centre, cm,
     *  universe axes, in doubles: the tile's component is placed here every
     *  frame and its vertices are local to it. The ground's point rather than
     *  the datum's, so a tile's local coordinates span its own few metres of
     *  relief and not the kilometres below it: that is what keeps a 17 m tile
     *  sub-millimetre in float. */
    FVector3d Pivot = FVector3d::ZeroVector;

    double RadiusCm = 0.0;

    /** Vertex spacing, cm: the tile's band limit, the footprint its heights
     *  were evaluated at. */
    double SpacingCm = 0.0;

    /** Pivot-relative positions, cm, the grid then the skirt: scale 1, so a
     *  19 m tile keeps sub-millimetre precision in float. */
    TArray<FVector3f> Positions;

    /** Unit normals, universe axes: the band-limited gradient composed as
     *  the orbit composes it, normalize(D - Grad_t / R). */
    TArray<FVector3f> Normals;

    /** The grid's heights above the datum, cm, and unit directions: unrounded,
     *  for the morph, the LOD boxes and the tests. */
    TArray<double> Heights;
    TArray<FVector3d> Directions;

    TerrainQuadtree::FHeightRange Range;

    /** The same over each child's quarter (quadrants as FTileKey::Child). */
    TerrainQuadtree::FHeightRange QuadrantRange[4];

    /** The largest miss of an edge's straight segment against the band-limited
     *  ground at its midpoint, cm. */
    double MaxEdgeErrorCm = 0.0;

    /** How far the skirt hangs: 1.5 x (edge error + omitted bands) + 1 cm. */
    double SkirtDepthCm = 0.0;

    double BuildSeconds = 0.0;
};

namespace TerrainTile
{
    inline constexpr int32 Cells = TerrainQuadtree::CellsPerTile;
    inline constexpr int32 GridVerts = (Cells + 1) * (Cells + 1);
    inline constexpr int32 SkirtVerts = 4 * (Cells + 1);
    inline constexpr int32 VertexCount = GridVerts + SkirtVerts;
    inline constexpr int32 TriangleCount = 2 * Cells * Cells + 4 * 2 * Cells;

    /** The orbit-to-ground handover, cm over the datum (decision 7): the
     *  projection's magnification is exactly 1 there. */
    inline constexpr double HandoverAltitudeCm = 5.0e6;

    /** UV2.y carries the height in kilometres: ProceduralMeshComponent's UVs
     *  are half precision, which cannot hold centimetres to 10 km. */
    inline constexpr double HeightUVScaleCm = 1.0e5;

    /** The fixed topology every tile shares: grid triangles (a, c, b) and
     *  (b, c, d) per cell -- inward, as the engine's meshes are wound -- then
     *  the skirt, each edge traversed with the tile's outside on its right. */
    DEEPSPACE_API const TArray<int32>& Indices();

    /** The tile for Key: heights at the tile's own spacing as the footprint,
     *  so a coarse tile never samples fine bands into vertex noise; normals
     *  from the analytic gradient of the same band-limited height. Pure and
     *  thread-safe given a thread-safe Ground. */
    DEEPSPACE_API FTileBuild Build(const IGroundField& Ground, const FTileKey& Key);

    /** M (decision 7): a smoothstep of the altitude over the datum, 0 at the
     *  handover and above, 1 at the drive floor and under. */
    DEEPSPACE_API double MorphFraction(double AltitudeCm, double DriveFloorAltitudeCm, double HandoverCm = HandoverAltitudeCm);

    /** Vertex V (grid or skirt) at morph M, pivot-relative: the built position
     *  less (1 - M) x its height along its own direction -- what M_SkyGround's
     *  World Position Offset does, (M - 1) x h x D. */
    DEEPSPACE_API FVector3d MorphedPosition(const FTileBuild& Tile, int32 Vertex, double Morph);

    /** The mesh's height above the datum at D, by its own triangles (the
     *  split along b-c), cm; unset if D is not on the tile. */
    DEEPSPACE_API TOptional<double> SampleHeight(const FTileBuild& Tile, const FVector3d& D);

    /** A child's height range from its parent's quarter, widened by what the
     *  parent's spacing omitted and its edge error: the child's LOD box. */
    DEEPSPACE_API TerrainQuadtree::FHeightRange ChildRange(const FTileBuild& Parent, int32 Quadrant, const IGroundField& Ground);

    /** The UV channels a mesh gets: UV1 = the normal's x and y; UV2 = its z
     *  and the height in km. */
    DEEPSPACE_API FVector2f UV1Of(const FTileBuild& Tile, int32 Vertex);
    DEEPSPACE_API FVector2f UV2Of(const FTileBuild& Tile, int32 Vertex);

    /** The grid vertex a skirt vertex hangs from; V itself for a grid vertex. */
    DEEPSPACE_API int32 GridOf(int32 Vertex);
}
```

Create `Source/DeepSpace/Surface/TerrainTile.cpp`:

```cpp
#include "Surface/TerrainTile.h"

#include "HAL/PlatformTime.h"

namespace
{
    using namespace TerrainTile;
    constexpr int32 Side = Cells + 1;

    /** The grid vertices round the edge, each edge traversed with the tile's
     *  outside on its right: bottom +I, right +J, top -I, left -J. */
    const TArray<int32>& Ring()
    {
        static const TArray<int32> Ring = []()
        {
            TArray<int32> Out;
            for (int32 I = 0; I < Side; ++I) { Out.Add(I); }
            for (int32 J = 0; J < Side; ++J) { Out.Add(J * Side + Cells); }
            for (int32 I = Cells; I >= 0; --I) { Out.Add(Cells * Side + I); }
            for (int32 J = Cells; J >= 0; --J) { Out.Add(J * Side); }
            return Out;
        }();
        return Ring;
    }
}

const TArray<int32>& TerrainTile::Indices()
{
    static const TArray<int32> Indices = []()
    {
        TArray<int32> Out;
        Out.Reserve(3 * TriangleCount);
        for (int32 J = 0; J < Cells; ++J)
        {
            for (int32 I = 0; I < Cells; ++I)
            {
                const int32 A = J * Side + I;
                const int32 B = A + 1;
                const int32 C = A + Side;
                const int32 D = C + 1;
                Out.Append({ A, C, B, B, C, D });
            }
        }
        for (int32 Edge = 0; Edge < 4; ++Edge)
        {
            for (int32 K = 0; K < Cells; ++K)
            {
                const int32 R0 = Edge * Side + K;
                const int32 G0 = Ring()[R0];
                const int32 G1 = Ring()[R0 + 1];
                const int32 S0 = GridVerts + R0;
                const int32 S1 = S0 + 1;
                Out.Append({ G0, G1, S0, G1, S1, S0 });
            }
        }
        return Out;
    }();
    return Indices;
}

int32 TerrainTile::GridOf(int32 Vertex)
{
    return Vertex < GridVerts ? Vertex : Ring()[Vertex - GridVerts];
}

FTileBuild TerrainTile::Build(const IGroundField& Ground, const FTileKey& Key)
{
    const double Start = FPlatformTime::Seconds();
    FTileBuild Tile;
    Tile.Key = Key;
    Tile.RadiusCm = Ground.RadiusCm();
    const double R = Tile.RadiusCm;
    Tile.SpacingCm = TerrainQuadtree::SpacingCm(Key.Level, R);
    const FVector3d CentreD = TerrainQuadtree::CentreDirection(Key);
    Tile.Pivot = CentreD * (R + Ground.Height(CentreD, Tile.SpacingCm));
    Tile.Positions.SetNumUninitialized(VertexCount);
    Tile.Normals.SetNumUninitialized(VertexCount);
    Tile.Heights.SetNumUninitialized(GridVerts);
    Tile.Directions.SetNumUninitialized(GridVerts);
    Tile.Range = { TNumericLimits<double>::Max(), -TNumericLimits<double>::Max() };
    for (TerrainQuadtree::FHeightRange& Quadrant : Tile.QuadrantRange)
    {
        Quadrant = Tile.Range;
    }

    for (int32 J = 0; J < Side; ++J)
    {
        for (int32 I = 0; I < Side; ++I)
        {
            const int32 V = J * Side + I;
            const FVector3d D = TerrainQuadtree::GridDirection(Key, I, J);
            FVector3d Grad = FVector3d::ZeroVector;
            const double H = Ground.HeightAndGradient(D, Grad, Tile.SpacingCm);
            const FVector3d Along = Grad - D * FVector3d::DotProduct(Grad, D);
            Tile.Directions[V] = D;
            Tile.Heights[V] = H;
            Tile.Positions[V] = FVector3f(D * (R + H) - Tile.Pivot);
            Tile.Normals[V] = FVector3f((D - Along / R).GetSafeNormal());
            Tile.Range.MinCm = FMath::Min(Tile.Range.MinCm, H);
            Tile.Range.MaxCm = FMath::Max(Tile.Range.MaxCm, H);
            for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
            {
                const bool bInI = (Quadrant & 1) ? I >= Cells / 2 : I <= Cells / 2;
                const bool bInJ = (Quadrant >> 1) ? J >= Cells / 2 : J <= Cells / 2;
                if (bInI && bInJ)
                {
                    Tile.QuadrantRange[Quadrant].MinCm = FMath::Min(Tile.QuadrantRange[Quadrant].MinCm, H);
                    Tile.QuadrantRange[Quadrant].MaxCm = FMath::Max(Tile.QuadrantRange[Quadrant].MaxCm, H);
                }
            }
        }
    }

    // The edges' own interpolation error, measured at every segment's middle.
    for (int32 R0 = 0; R0 + 1 < Ring().Num(); ++R0)
    {
        if ((R0 + 1) % Side == 0)
        {
            continue;   // between edges, not along one
        }
        const int32 A = Ring()[R0];
        const int32 B = Ring()[R0 + 1];
        const FVector3d Middle = (Tile.Directions[A] + Tile.Directions[B]).GetSafeNormal();
        Tile.MaxEdgeErrorCm = FMath::Max(Tile.MaxEdgeErrorCm,
            FMath::Abs(Ground.Height(Middle, Tile.SpacingCm) - 0.5 * (Tile.Heights[A] + Tile.Heights[B])));
    }
    Tile.SkirtDepthCm = 1.5 * (Tile.MaxEdgeErrorCm + Ground.OmittedBoundCm(Tile.SpacingCm)) + 1.0;

    for (int32 S = 0; S < SkirtVerts; ++S)
    {
        const int32 G = Ring()[S];
        const FVector3d& D = Tile.Directions[G];
        Tile.Positions[GridVerts + S] = FVector3f(D * (R + Tile.Heights[G] - Tile.SkirtDepthCm) - Tile.Pivot);
        Tile.Normals[GridVerts + S] = Tile.Normals[G];
    }
    Tile.BuildSeconds = FPlatformTime::Seconds() - Start;
    return Tile;
}

double TerrainTile::MorphFraction(double AltitudeCm, double DriveFloorAltitudeCm, double HandoverCm)
{
    if (DriveFloorAltitudeCm >= HandoverCm)
    {
        return AltitudeCm < HandoverCm ? 1.0 : 0.0;
    }
    const double T = FMath::Clamp((HandoverCm - AltitudeCm) / (HandoverCm - DriveFloorAltitudeCm), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

FVector3d TerrainTile::MorphedPosition(const FTileBuild& Tile, int32 Vertex, double Morph)
{
    const int32 G = GridOf(Vertex);
    return FVector3d(Tile.Positions[Vertex]) - Tile.Directions[G] * ((1.0 - Morph) * Tile.Heights[G]);
}

TOptional<double> TerrainTile::SampleHeight(const FTileBuild& Tile, const FVector3d& D)
{
    const TerrainQuadtree::FFace& Face = TerrainQuadtree::Face(Tile.Key.Face);
    const double Along = FVector3d::DotProduct(D, Face.Normal);
    if (!(Along > 0.0))
    {
        return {};
    }
    const double Count = static_cast<double>(static_cast<int64>(Cells) << Tile.Key.Level);
    const double GU = (FMath::Atan(FVector3d::DotProduct(D, Face.U) / Along) / (0.5 * UE_DOUBLE_PI) + 0.5) * Count - Tile.Key.X * static_cast<double>(Cells);
    const double GV = (FMath::Atan(FVector3d::DotProduct(D, Face.V) / Along) / (0.5 * UE_DOUBLE_PI) + 0.5) * Count - Tile.Key.Y * static_cast<double>(Cells);
    if (GU < -1.0e-9 || GV < -1.0e-9 || GU > Cells + 1.0e-9 || GV > Cells + 1.0e-9)
    {
        return {};
    }
    const int32 I = FMath::Clamp(FMath::FloorToInt32(GU), 0, Cells - 1);
    const int32 J = FMath::Clamp(FMath::FloorToInt32(GV), 0, Cells - 1);
    const double FX = GU - I;
    const double FY = GV - J;
    const double A = Tile.Heights[J * Side + I];
    const double B = Tile.Heights[J * Side + I + 1];
    const double C = Tile.Heights[(J + 1) * Side + I];
    const double Dd = Tile.Heights[(J + 1) * Side + I + 1];
    return FX + FY <= 1.0 ? A + FX * (B - A) + FY * (C - A)
                          : Dd + (1.0 - FX) * (C - Dd) + (1.0 - FY) * (B - Dd);
}

TerrainQuadtree::FHeightRange TerrainTile::ChildRange(const FTileBuild& Parent, int32 Quadrant, const IGroundField& Ground)
{
    const double Widen = Ground.OmittedBoundCm(Parent.SpacingCm) + Parent.MaxEdgeErrorCm;
    const TerrainQuadtree::FHeightRange& Quarter = Parent.QuadrantRange[Quadrant & 3];
    return { Quarter.MinCm - Widen, Quarter.MaxCm + Widen };
}

FVector2f TerrainTile::UV1Of(const FTileBuild& Tile, int32 Vertex)
{
    return FVector2f(Tile.Normals[Vertex].X, Tile.Normals[Vertex].Y);
}

FVector2f TerrainTile::UV2Of(const FTileBuild& Tile, int32 Vertex)
{
    return FVector2f(Tile.Normals[Vertex].Z, static_cast<float>(Tile.Heights[GridOf(Vertex)] / HeightUVScaleCm));
}
```

- [ ] **Step 4: Run: PASS; record the build time and the pop**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Surface.Tile; \
grep "split pop" Saved/Logs/DeepSpace.log | tail -1
```

Expected: `passed: 1`, and the line `a tile builds in N ms; the largest split pop is P 4K pixels`.
Write both into the spec's decision 6: N beside *Estimated cost: ... 5-10 ms a tile*, P beside
*Seams and pops* (sign-off item 16 is judged on P). If N exceeds 30 ms, stop and report: the
spec's *Risks* says the skim cap then rises until two workers keep up, which is the developer's
call.

- [ ] **Step 5: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
git add Source/DeepSpace/Surface/TerrainTile.h Source/DeepSpace/Surface/TerrainTile.cpp Source/DeepSpace/Tests/TerrainTileTest.cpp && \
git add -u docs/superpowers/specs/2026-09-27-landing-design.md && \
git commit -m "$(cat <<'EOF'
feat(terrain): the tile -- 33 x 33 and a skirt, pivot-local, band-limited, morphable

Heights at the tile's own spacing, normals from the same band-limited
gradient in the orbit's composition, shared edges identical, the skirt
deep enough for the edge error and the omitted bands, the finest mesh
within 15 cm of the analytic ground, and heights that are the sphere at
the handover and the full relief at the drive floor (landing decisions
6, 7). The height rides UV2.y in km: PMC's UVs are half precision.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Source/DeepSpace/Surface/TerrainTile.cpp 'const double H = Ground.HeightAndGradient(D, Grad, Tile.SpacingCm);' 'const double H = Ground.HeightAndGradient(D, Grad, 20.0 * Tile.SpacingCm);' DeepSpace.Surface.Tile && \
Tools/mutate.sh Source/DeepSpace/Surface/TerrainTile.cpp 'Out.Append({ A, C, B, B, C, D });' 'Out.Append({ A, B, C, B, D, C });' DeepSpace.Surface.Tile && \
./build.sh
```

Expected: `KILLED` twice (an over-smoothed finest tile misses the ground by more than 15 cm; the
reversed winding faces away).

---

## Task 35 (T5, only if Task B1's verdict is CUSTOM PRIMITIVE): `UTerrainTileComponent`

**Owner:** T. **Depends on:** T4 and T6 (it replaces T6's two component functions). Spec
decision 6's fallback; sign-off item 15. **Skip this task when the gate said PMC.**

**Files:**
- Create: `Source/DeepSpace/Surface/TerrainTileComponent.h`, `TerrainTileComponent.cpp`
- (No `DeepSpace.Build.cs` change: `RenderCore` is already a private dependency, `DeepSpace.Build.cs:46`,
  added for `DeepSpace.Sky.ProxyOnTheGpu`; `RHI` is at line 41. The proxy needs no other module.)
- Modify: `Source/DeepSpace/Surface/WorldGround.cpp` (`NewTileComponent`, `UploadTo`)
- Create: `Source/DeepSpace/Tests/TerrainTileComponentTest.cpp` (`DeepSpace.Surface.TileComponent`)

**Interfaces:** Produces `class UTerrainTileComponent : public UMeshComponent` with
`void SetTile(const FTileBuild& Tile)`, `const FTileBuild* GetTileForTest() const`,
`bool bKeepForTest`; a static-path scene proxy with full-precision UVs.

- [ ] **Step 1: Write the failing test**

Create `Source/DeepSpace/Tests/TerrainTileComponentTest.cpp`:

```cpp
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Surface/TerrainTileComponent.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/* Decision 6's fallback: a tile primitive on the static draw path, no CPU
 * copy beyond a test hook, never casting a shadow. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainTileComponentTest, "DeepSpace.Surface.TileComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainTileComponentTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TileComponentWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    AActor* Holder = World->SpawnActor<AActor>();
    UTerrainTileComponent* Tile = NewObject<UTerrainTileComponent>(Holder);
    Tile->bKeepForTest = true;
    Holder->SetRootComponent(Tile);
    Tile->RegisterComponent();
    const FGroundFieldRef Ground = ShipGround::FromRelief(FixtureParams());
    const FTileBuild Build = TerrainTile::Build(*Ground, TerrainQuadtree::KeyAt(FVector3d(0, 0, 1), 12));
    Tile->SetTile(Build);
    TestNotNull(TEXT("the tile is kept for the test"), Tile->GetTileForTest());
    const FBox Bounds = Tile->CalcBounds(FTransform::Identity).GetBox();
    bool bInside = true;
    for (const FVector3f& P : Build.Positions)
    {
        bInside &= Bounds.ExpandBy(1.0).IsInside(FVector(P));
    }
    TestTrue(TEXT("its bounds hold every vertex"), bInside);
    TestFalse(TEXT("it casts no shadow"), Tile->CastShadow);
    TestFalse(TEXT("nor lights by distance field"), Tile->bAffectDistanceFieldLighting);
    TestEqual(TEXT("and has no collision"), Tile->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
```

Run `./build.sh`: fails, no such header.

- [ ] **Step 2: The component**

Create `Source/DeepSpace/Surface/TerrainTileComponent.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "Surface/TerrainTile.h"
#include "TerrainTileComponent.generated.h"

/**
 * One terrain tile on the static draw path (landing decision 6's fallback,
 * taken because Eyes.TerrainBudget failed PMC): its scene proxy owns the GPU
 * buffers and is rebuilt when the tile is, with full-precision UVs; the
 * component keeps no copy of the arrays but a test's. Moving it is a
 * transform update, which the static path handles through GPU Scene.
 */
UCLASS(ClassGroup = Rendering)
class DEEPSPACE_API UTerrainTileComponent : public UMeshComponent
{
    GENERATED_BODY()

public:
    UTerrainTileComponent();

    /** The tile to draw; rebuilds the render proxy. */
    void SetTile(const FTileBuild& Tile);

    /** Only when bKeepForTest: the last tile set. */
    const FTileBuild* GetTileForTest() const { return bKeepForTest && bHasTile ? &Kept : nullptr; }
    bool bKeepForTest = false;

    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    virtual int32 GetNumMaterials() const override { return 1; }

private:
    /** Handed to the next proxy, then dropped. */
    TSharedPtr<FTileBuild, ESPMode::ThreadSafe> Pending;
    FTileBuild Kept;
    bool bHasTile = false;
    FBox LocalBounds = FBox(ForceInit);
};
```

Create `Source/DeepSpace/Surface/TerrainTileComponent.cpp`:

```cpp
#include "Surface/TerrainTileComponent.h"

#include "DynamicMeshBuilder.h"
#include "Engine/Engine.h"
#include "LocalVertexFactory.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "PrimitiveSceneProxy.h"
#include "SceneInterface.h"
#include "StaticMeshResources.h"

namespace
{
    class FTerrainTileSceneProxy final : public FPrimitiveSceneProxy
    {
    public:
        FTerrainTileSceneProxy(UTerrainTileComponent* Component, const FTileBuild& Tile)
            : FPrimitiveSceneProxy(Component)
            , VertexFactory(GetScene().GetFeatureLevel(), "FTerrainTileSceneProxy")
            , MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetShaderPlatform()))
        {
            Material = Component->GetMaterial(0);
            if (!Material)
            {
                Material = UMaterial::GetDefaultMaterial(MD_Surface);
            }
            const int32 Count = Tile.Positions.Num();
            Buffers.PositionVertexBuffer.Init(Count);
            Buffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
            Buffers.StaticMeshVertexBuffer.Init(Count, 3);
            Buffers.ColorVertexBuffer.Init(Count);
            for (int32 V = 0; V < Count; ++V)
            {
                const FVector3f Normal = Tile.Normals[V];
                const FVector3f Any = FMath::Abs(Normal.Z) < 0.9f ? FVector3f(0, 0, 1) : FVector3f(1, 0, 0);
                const FVector3f TangentX = FVector3f::CrossProduct(Any, Normal).GetSafeNormal();
                const FVector3f TangentY = FVector3f::CrossProduct(Normal, TangentX);
                Buffers.PositionVertexBuffer.VertexPosition(V) = Tile.Positions[V];
                Buffers.StaticMeshVertexBuffer.SetVertexTangents(V, TangentX, TangentY, Normal);
                Buffers.StaticMeshVertexBuffer.SetVertexUV(V, 0, FVector2f::ZeroVector);
                Buffers.StaticMeshVertexBuffer.SetVertexUV(V, 1, TerrainTile::UV1Of(Tile, V));
                Buffers.StaticMeshVertexBuffer.SetVertexUV(V, 2, TerrainTile::UV2Of(Tile, V));
                Buffers.ColorVertexBuffer.VertexColor(V) = FColor::White;
            }
            for (const int32 Index : TerrainTile::Indices())
            {
                IndexBuffer.Indices.Add(static_cast<uint32>(Index));
            }
            ENQUEUE_RENDER_COMMAND(InitTerrainTile)([this](FRHICommandListImmediate& RHICmdList)
            {
                Buffers.PositionVertexBuffer.InitResource(RHICmdList);
                Buffers.StaticMeshVertexBuffer.InitResource(RHICmdList);
                Buffers.ColorVertexBuffer.InitResource(RHICmdList);
                FLocalVertexFactory::FDataType Data;
                Buffers.PositionVertexBuffer.BindPositionVertexBuffer(&VertexFactory, Data);
                Buffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&VertexFactory, Data);
                Buffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&VertexFactory, Data);
                Buffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(&VertexFactory, Data, 0);
                Buffers.ColorVertexBuffer.BindColorVertexBuffer(&VertexFactory, Data);
                VertexFactory.SetData(RHICmdList, Data);
                VertexFactory.InitResource(RHICmdList);
                IndexBuffer.InitResource(RHICmdList);
            });
        }

        virtual ~FTerrainTileSceneProxy() override
        {
            Buffers.PositionVertexBuffer.ReleaseResource();
            Buffers.StaticMeshVertexBuffer.ReleaseResource();
            Buffers.ColorVertexBuffer.ReleaseResource();
            IndexBuffer.ReleaseResource();
            VertexFactory.ReleaseResource();
        }

        virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override
        {
            FMeshBatch Mesh;
            Mesh.VertexFactory = &VertexFactory;
            Mesh.MaterialRenderProxy = Material->GetRenderProxy();
            Mesh.Type = PT_TriangleList;
            Mesh.DepthPriorityGroup = SDPG_World;
            Mesh.CastShadow = false;
            Mesh.bCanApplyViewModeOverrides = false;
            FMeshBatchElement& Element = Mesh.Elements[0];
            Element.IndexBuffer = &IndexBuffer;
            Element.FirstIndex = 0;
            Element.NumPrimitives = IndexBuffer.Indices.Num() / 3;
            Element.MinVertexIndex = 0;
            Element.MaxVertexIndex = Buffers.PositionVertexBuffer.GetNumVertices() - 1;
            PDI->DrawMesh(Mesh, FLT_MAX);
        }

        virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
        {
            FPrimitiveViewRelevance Result;
            Result.bDrawRelevance = IsShown(View);
            Result.bShadowRelevance = false;
            Result.bStaticRelevance = true;
            Result.bDynamicRelevance = false;
            Result.bRenderInMainPass = ShouldRenderInMainPass();
            Result.bVelocityRelevance = false;
            MaterialRelevance.SetPrimitiveViewRelevance(Result);
            return Result;
        }

        virtual bool CanBeOccluded() const override { return !MaterialRelevance.bDisableDepthTest; }
        virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }
        virtual SIZE_T GetTypeHash() const override
        {
            static size_t UniquePointer;
            return reinterpret_cast<size_t>(&UniquePointer);
        }

    private:
        UMaterialInterface* Material = nullptr;
        FStaticMeshVertexBuffers Buffers;
        FDynamicMeshIndexBuffer32 IndexBuffer;
        FLocalVertexFactory VertexFactory;
        FMaterialRelevance MaterialRelevance;
    };
}

UTerrainTileComponent::UTerrainTileComponent()
{
    SetCastShadow(false);
    bAffectDistanceFieldLighting = false;
    bAffectDynamicIndirectLighting = false;
    bVisibleInRayTracing = false;
    bNeverDistanceCull = true;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    SetMobility(EComponentMobility::Movable);
}

void UTerrainTileComponent::SetTile(const FTileBuild& Tile)
{
    LocalBounds = FBox(ForceInit);
    for (const FVector3f& Position : Tile.Positions)
    {
        LocalBounds += FVector(Position);
    }
    // The morph lowers vertices by up to their height along their direction.
    LocalBounds = LocalBounds.ExpandBy(FMath::Max(FMath::Abs(Tile.Range.MinCm), FMath::Abs(Tile.Range.MaxCm)));
    Pending = MakeShared<FTileBuild, ESPMode::ThreadSafe>(Tile);
    if (bKeepForTest)
    {
        Kept = Tile;
    }
    bHasTile = true;
    UpdateBounds();
    MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UTerrainTileComponent::CreateSceneProxy()
{
    if (!Pending.IsValid() && !bHasTile)
    {
        return nullptr;
    }
    const TSharedPtr<FTileBuild, ESPMode::ThreadSafe> Tile = Pending.IsValid() ? Pending : MakeShared<FTileBuild, ESPMode::ThreadSafe>(Kept);
    Pending.Reset();
    return Tile->Positions.Num() > 0 ? new FTerrainTileSceneProxy(this, *Tile) : nullptr;
}

FBoxSphereBounds UTerrainTileComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    return LocalBounds.IsValid ? FBoxSphereBounds(LocalBounds).TransformBy(LocalToWorld) : FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0);
}
```

(A proxy recreated without a pending tile -- the renderer may recreate proxies on its own -- is
rebuilt from `Kept`, so `AWorldGround` sets `bKeepForTest = true` on every tile component when
this fallback is taken; the memory is then one `FTileBuild` per tile, about a quarter of PMC's.)

`DeepSpace.Build.cs` is not touched: `RenderCore` (line 46) and `RHI` (line 41) are already private
dependencies, and nothing here needs `Renderer`.

- [ ] **Step 3: Swap it into the ground**

In `Source/DeepSpace/Surface/WorldGround.cpp`, replace `NewTileComponent` and `UploadTo` (Task
T6) with:

```cpp
UPrimitiveComponent* AWorldGround::NewTileComponent()
{
    UTerrainTileComponent* Tile = NewObject<UTerrainTileComponent>(this);
    Tile->bKeepForTest = true;
    Tile->SetupAttachment(Root);
    Tile->SetVisibility(false);
    Tile->RegisterComponent();
    Tile->SetMaterial(0, Material);
    Pool.Add(Tile);
    return Tile;
}

void AWorldGround::UploadTo(UPrimitiveComponent* Component, const FTileBuild& Tile, bool bFirst)
{
    CastChecked<UTerrainTileComponent>(Component)->SetTile(Tile);
}
```

(`#include "Surface/TerrainTileComponent.h"`; drop the PMC include if nothing else uses it.)

- [ ] **Step 4: Run, gate again, commit, prove**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Surface && \
git add Source/DeepSpace/Surface/TerrainTileComponent.h Source/DeepSpace/Surface/TerrainTileComponent.cpp \
        Source/DeepSpace/Surface/WorldGround.cpp Source/DeepSpace/Tests/TerrainTileComponentTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(terrain): UTerrainTileComponent -- the static-path tile primitive, taken because PMC failed the gate

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)" && \
Tools/mutate.sh Source/DeepSpace/Surface/TerrainTileComponent.cpp '    SetCastShadow(false);' '    SetCastShadow(true);' DeepSpace.Surface.TileComponent && ./build.sh
```

Expected: `DeepSpace.Surface.*` green; `KILLED`. Then re-run `Eyes.TerrainBudget` with its
`Pool` built from `UTerrainTileComponent` (swap the `NewObject<UProceduralMeshComponent>` and
`CreateMeshSection` lines for `NewObject<UTerrainTileComponent>` and `SetTile(TerrainTile::Build(...))`
of one fixture tile) and record the new numbers beside the verdict in the spec.

---

## Task 36 (T6): `AWorldGround` -- the cut streamed off the game thread into pooled tiles on the counter-frame

**Owner:** T. **Depends on:** B1 (the verdict), T3, T4, S1 (`FloorFor` over peaks; merge
`feat/landing-b` first). Spec decision 6 (*The frame*, *Async build*, *Never a hole*, *Budgets*),
decision 7 (the switch, the morph).

**Files:**
- Create: `Source/DeepSpace/Surface/WorldGround.h`, `Source/DeepSpace/Surface/WorldGround.cpp`
- Create: `Source/DeepSpace/Tests/WorldGroundTest.cpp` (`DeepSpace.Surface.GroundActor`)
- Modify: `Source/DeepSpace/Tests/SkyTestWorld.h` (an optional ground in `FSkyWorld`)
- Modify: `CLAUDE.md` -- *Architecture* (a `Surface/` line), the tunables table (the
  `ds.Terrain.*` row)

**Interfaces:**
- Consumes: `TerrainQuadtree::*`, `TerrainTile::*`, `FGroundFieldRef`, `ShipGround::FromRelief`,
  `UShipSubsystem::{Get, GetFlightState, IsInTransit, FloorFor}`, `LocalSystem::Here`,
  `AShipCounterFrame`, `SkyMaterial::Morph` (Task T7 adds the name; until then this task writes
  `TEXT("Morph")` through a local `const FName MorphName`, replaced in T7).
- Produces: `class AWorldGround` with `static const FName GroundTag` (`Sky.Ground`),
  `UPROPERTY GroundMaterial`, `void SyncToShip()`, `void SyncTo(const FSkySystem&, bool bInTransit)`,
  `void FlushBuildsForTest()`, `FName GetDrawnBody() const`, `bool IsDrawingBody() const`,
  `double GetMorph() const`, `TArray<FTileKey> GetDrawnKeys() const`,
  `UPrimitiveComponent* GetTileComponent(const FTileKey&) const`,
  `const FTileBuild* GetResidentTile(const FTileKey&) const`, `int32 GetUploadsLastFrame() const`,
  `int32 GetResidentCount() const`, `TOptional<double> DrawnHeightUnderShip() const`,
  `UMaterialInstanceDynamic* GetGroundMaterialInstance() const`, `FString Describe() const`;
  CVars `ds.Terrain.SplitFactor`, `.MaxTiles`, `.BuildTasks`, `.UploadsPerFrame`, `.Show`;
  command `ds.Terrain.Describe`.

- [ ] **Step 1: A ground in the sky's test world**

In `Source/DeepSpace/Tests/SkyTestWorld.h`, add `#include "Surface/WorldGround.h"` and
`#include "Materials/Material.h"`; in `FSkyWorld`, a member `AWorldGround* Ground = nullptr;`
and, at the end of the constructor:

```cpp
            // What place_ground assigns, and all it assigns: the ground's
            // material. Before M_SkyGround exists (Task T7), the engine's.
            Ground = World->SpawnActor<AWorldGround>();
            if (Ground)
            {
                UMaterialInterface* GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/Sky/M_SkyGround.M_SkyGround"));
                Ground->GroundMaterial = GroundMaterial ? GroundMaterial : UMaterial::GetDefaultMaterial(MD_Surface);
            }
```

and in `Step`, after `Frame->SyncToShip();`: `if (Ground) { Ground->SyncToShip(); }` (the ground
before the sky, as its tick prerequisite orders them in play).

- [ ] **Step 2: Write the failing test**

Create `Source/DeepSpace/Tests/WorldGroundTest.cpp`:

```cpp
#include "Components/PrimitiveComponent.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipLanding.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Surface/WorldGround.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 6's actor: the cut built off the game thread and drawn
 * as pooled tiles on the counter-frame, each at UniverseToWorld of its
 * pivot, never casting a shadow or reaching distance fields, indirect light
 * or ray tracing; a capped number of uploads a frame; and under 1 km the
 * drawn ground under the ship within GearClearance / 10 of the analytic
 * ground. Spawned before play, builds flushed inline.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldGroundActorTest, "DeepSpace.Surface.GroundActor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldGroundActorTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("GroundActorWorld"));
    if (!TestNotNull(TEXT("the ground spawns before play"), Test.Ground))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    AWorldGround* Ground = Test.Ground;
    TestTrue(TEXT("the ground is tagged for the level's check"), Ground->ActorHasTag(AWorldGround::GroundTag));
    TestTrue(TEXT("and rides the counter-frame"), Ground->GetAttachParentActor() == Test.Frame);

    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody& Fourth = Here.Bodies[4];
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const auto PlaceAt = [&](const FVector& Up, double Agl)
    {
        Ship->PlaceShip(Fourth.Position + Up * (Fourth.Radius + Field->Height(FVector3d(Up), 0.0) + Agl),
                        FRotationMatrix::MakeFromXZ(FVector::CrossProduct(Up, FVector(0.3, 0.9, 0.1)).GetSafeNormal(), Up).ToQuat());
        Test.Step(1.0f / 60.0f);
    };

    PlaceAt(Out, 5.0e4);
    Ground->FlushBuildsForTest();
    Test.Step(1.0f / 60.0f);
    const TArray<FTileKey> Drawn = Ground->GetDrawnKeys();
    AddInfo(FString::Printf(TEXT("500 m over Baemsekai IV: %d tiles drawn, %d resident\n%s"), Drawn.Num(), Ground->GetResidentCount(), *Ground->Describe()));
    TestTrue(TEXT("500 m up the ground draws a cut"), Drawn.Num() > 100);
    TestTrue(TEXT("and claims the body"), Ground->IsDrawingBody() && Ground->GetDrawnBody() == Fourth.Id);

    int32 Misplaced = 0;
    int32 Lit = 0;
    for (const FTileKey& Key : Drawn)
    {
        const UPrimitiveComponent* Tile = Ground->GetTileComponent(Key);
        const FTileBuild* Built = Ground->GetResidentTile(Key);
        if (!Tile || !Built)
        {
            ++Misplaced;
            continue;
        }
        const FVector Expected = Ship->UniverseToWorld(Fourth.Position + FVector(Built->Pivot));
        Misplaced += (Tile->GetComponentLocation() - Expected).Size() > 1.0 ? 1 : 0;
        Lit += (Tile->CastShadow || Tile->bAffectDistanceFieldLighting || Tile->bAffectDynamicIndirectLighting
                || Tile->bVisibleInRayTracing || Tile->GetCollisionEnabled() != ECollisionEnabled::NoCollision || !Tile->IsVisible()) ? 1 : 0;
    }
    TestEqual(TEXT("every drawn tile sits at UniverseToWorld of its pivot"), Misplaced, 0);
    TestEqual(TEXT("and none casts a shadow, reaches distance fields, indirect light or ray tracing, or collides"), Lit, 0);

    const double Nadir = Field->Height(FVector3d((Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal()), 0.0);
    const TOptional<double> DrawnHeight = Ground->DrawnHeightUnderShip();
    TestTrue(FString::Printf(TEXT("under 1 km the drawn ground under the ship is within 15 cm of the ground (%.2f cm)"),
                             DrawnHeight ? FMath::Abs(*DrawnHeight - Nadir) : -1.0),
             DrawnHeight.IsSet() && FMath::Abs(*DrawnHeight - Nadir) <= ShipLanding::DefaultGearClearanceCm / 10.0);
    TestEqual(TEXT("under the drive floor the relief is whole: the morph is 1"), Ground->GetMorph(), 1.0);

    // A few kilometres over, frame by frame: never more than
    // ds.Terrain.UploadsPerFrame tiles a frame.
    const FVector Aside = (Out + FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal() * 5.0e-4).GetSafeNormal();
    PlaceAt(Aside, 5.0e4);
    int32 Most = 0;
    int32 Frames = 0;
    for (int32 Frame = 0; Frame < 300; ++Frame)
    {
        FPlatformProcess::Sleep(0.005f);
        Test.Step(1.0f / 60.0f);
        Most = FMath::Max(Most, Ground->GetUploadsLastFrame());
        Frames += Ground->GetUploadsLastFrame() > 0 ? 1 : 0;
    }
    TestTrue(TEXT("tiles arrive as the ship moves"), Frames > 0);
    TestTrue(FString::Printf(TEXT("never more than ds.Terrain.UploadsPerFrame a frame (%d)"), Most),
             Most <= SkyTestWorld::CVarFloat(TEXT("ds.Terrain.UploadsPerFrame")));

    // ds.Terrain.Show 0 gives the body back.
    {
        FScopedCVar Show(TEXT("ds.Terrain.Show"), 0.0f);
        Test.Step(1.0f / 60.0f);
        TestFalse(TEXT("ds.Terrain.Show 0 hides the ground"), Ground->IsDrawingBody());
    }
    return true;
}

#endif
```

- [ ] **Step 3: Run and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git merge -q feat/landing-b && ./build.sh
```

Expected: fails to compile: `Surface/WorldGround.h: No such file or directory`.

- [ ] **Step 4: Write the actor**

Create `Source/DeepSpace/Surface/WorldGround.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Surface/GroundField.h"
#include "Surface/TerrainQuadtree.h"
#include "Surface/TerrainTile.h"
#include "Tasks/Task.h"
#include "WorldGround.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class USceneComponent;
struct FSkySystem;

/**
 * The ground under the ship (landing decisions 6 and 7): the nearest solid
 * world's cube-sphere quadtree, cut by CDLOD from the ship's origin, built on
 * worker threads (UE::Tasks, at most ds.Terrain.BuildTasks at once -- the
 * machine's cap, never the core count), uploaded into pooled mesh tiles at
 * most ds.Terrain.UploadsPerFrame a frame, and drawn attached to the
 * counter-frame: each tile at its double-precision pivot relative to the
 * ship, scale 1, turned by the counter-frame's rotation, its vertices local.
 *
 * It takes the body from AShipSky below 50 km once the coarse cut is
 * resident (and gives it back over 55 km), and always under the drive floor,
 * where the proxy is never drawn; the relief grows in by one morph fraction
 * between the handover and the drive floor. Residency never gates the
 * ship's motion. Polls the ship and the universe every frame and stores
 * nothing about either beyond its own drawing (the sky's rule).
 *
 * Placed by Tools/build_hauler.py as hauler_ground, tagged Sky.Ground, with
 * its material assigned there (ADR 0002).
 */
UCLASS()
class DEEPSPACE_API AWorldGround : public AActor
{
    GENERATED_BODY()

public:
    AWorldGround();

    /** Sky.Ground: how the level check finds it. */
    static const FName GroundTag;

    /** M_SkyGround, assigned by build_hauler.py. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground")
    TObjectPtr<UMaterialInterface> GroundMaterial;

    /** The frame's work: which world, the cut, builds, uploads, what to draw,
     *  the handover and the morph, and every drawn tile placed. */
    void SyncToShip();
    void SyncTo(const FSkySystem& System, bool bInTransit);

    /** Tests: build everything the cut wants, round after round, uploading
     *  without the per-frame cap, until the cut is resident. */
    void FlushBuildsForTest();

    /** The body the ground draws instead of the sky's proxy; none when it
     *  does not. */
    FName GetDrawnBody() const { return bDrawsBody ? Body : NAME_None; }
    bool IsDrawingBody() const { return bDrawsBody; }

    /** M, 0 at the handover to 1 at the drive floor. */
    double GetMorph() const { return Morph; }

    TArray<FTileKey> GetDrawnKeys() const { return Drawn; }
    UPrimitiveComponent* GetTileComponent(const FTileKey& Key) const;
    const FTileBuild* GetResidentTile(const FTileKey& Key) const;
    int32 GetUploadsLastFrame() const { return UploadsLastFrame; }
    int32 GetResidentCount() const { return Resident.Num(); }

    /** The drawn (morphed) ground's height over the datum at the ship's
     *  nadir, cm; unset if no drawn tile holds it. */
    TOptional<double> DrawnHeightUnderShip() const;

    /** The one material instance every tile shares: AShipSky writes the
     *  body's look into it, this actor writes Morph. */
    UMaterialInstanceDynamic* GetGroundMaterialInstance() const { return Material; }

    /** The cut per level -- drawn, resident, building -- the cap, the morph. */
    FString Describe() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY(VisibleAnywhere, Category = "Ground")
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UPrimitiveComponent>> Pool;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> Material;

    struct FResident
    {
        FTileBuild Tile;
        int32 Component = INDEX_NONE;
    };
    TMap<FTileKey, FResident> Resident;

    struct FPending
    {
        FTileKey Key;
        UE::Tasks::TTask<FTileBuild> Task;
    };
    TArray<FPending> InFlight;
    TArray<FTileBuild> Finished;

    TArray<FTileKey> Wanted;
    TArray<FTileKey> Prefetch;
    TArray<FTileKey> Drawn;
    TArray<int32> FreeComponents;

    FGroundFieldRef Ground;
    FWorldReliefParams GroundParams;
    FName Body;
    FUniversePosition Centre;
    double Radius = 0.0;
    double DriveFloorAltitude = 0.0;
    FVector3d ShipFromCentre = FVector3d::ZeroVector;
    FVector3d LastCutFrom = FVector3d::ZeroVector;
    double LastCutTime = -1.0;
    bool bResidencyChanged = true;
    bool bCapBinding = false;
    double Morph = 0.0;
    bool bDrawsBody = false;
    int32 UploadsLastFrame = 0;

    void Release();
    void Select(double GroundAltitudeCm);
    void Launch();
    void Collect(int32 Budget);
    void Upload(const FTileBuild& Tile);
    void Free(int32 Component);
    void Resolve();
    void Place();
    bool CoarseResident() const;
    TSet<FTileKey> NeededKeys() const;
    TOptional<TerrainQuadtree::FHeightRange> BoundsOf(const FTileKey& Key) const;

    /** The mesh component a tile is drawn with, and how a tile gets into it:
     *  ProceduralMeshComponent, or UTerrainTileComponent if the first-day gate
     *  failed PMC (Task T5 replaces these two, nothing else). */
    UPrimitiveComponent* NewTileComponent();
    void UploadTo(UPrimitiveComponent* Component, const FTileBuild& Tile, bool bFirst);
};
```

Create `Source/DeepSpace/Surface/WorldGround.cpp`:

```cpp
#include "Surface/WorldGround.h"

#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/OutputDevice.h"
#include "ProceduralMeshComponent.h"
#include "Ship/ShipCounterFrame.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkySystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldGround, Log, All);

const FName AWorldGround::GroundTag(TEXT("Sky.Ground"));

namespace
{
    TAutoConsoleVariable<float> CVarSplitFactor(
        TEXT("ds.Terrain.SplitFactor"), static_cast<float>(TerrainQuadtree::DefaultSplitFactor),
        TEXT("A terrain node splits when the ship is closer than this many of its edges."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarMaxTiles(
        TEXT("ds.Terrain.MaxTiles"), TerrainQuadtree::DefaultMaxTiles,
        TEXT("A ceiling on drawn terrain tiles: over it the farthest levels coarsen first, never the ship's own chain."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarBuildTasks(
        TEXT("ds.Terrain.BuildTasks"), 2,
        TEXT("Terrain tile builds in flight at once, on worker threads: the machine's cap, never sized to the core count."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarUploadsPerFrame(
        TEXT("ds.Terrain.UploadsPerFrame"), 4,
        TEXT("Finished terrain tiles uploaded to the GPU a frame."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShow(
        TEXT("ds.Terrain.Show"), 1,
        TEXT("0 hides the ground and gives the world back to the sky's proxy, for comparison."),
        ECVF_Default);

    /** The handover's hysteresis: taken under 50 km, given back over 55. */
    constexpr double HandbackFactor = 1.1;

    const FName MorphName(TEXT("Morph"));

    bool SameRelief(const FWorldReliefParams& A, const FWorldReliefParams& B)
    {
        return A.SeedOffset == B.SeedOffset && A.RadiusCm == B.RadiusCm && A.PeakCm == B.PeakCm
            && A.Cratering == B.Cratering && A.Ground == B.Ground;
    }

    void DescribeGround(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        int32 Count = 0;
        for (TActorIterator<AWorldGround> It(World); It; ++It, ++Count)
        {
            Out.Log(It->Describe());
        }
        if (Count == 0)
        {
            Out.Log(TEXT("ds.Terrain.Describe: no ground in this world."));
        }
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice DescribeCommand(
        TEXT("ds.Terrain.Describe"),
        TEXT("The terrain's cut per level: drawn, resident and building tiles, whether the tile cap binds, and the morph."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DescribeGround));
}

AWorldGround::AWorldGround()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Movable);
    SetRootComponent(Root);
    Tags.AddUnique(GroundTag);
}

void AWorldGround::BeginPlay()
{
    Super::BeginPlay();
    TActorIterator<AShipCounterFrame> Frame(GetWorld());
    if (Frame)
    {
        AttachToActor(*Frame, FAttachmentTransformRules::KeepRelativeTransform);
        SetActorRelativeTransform(FTransform::Identity);
    }
    else
    {
        UE_LOG(LogWorldGround, Warning, TEXT("No AShipCounterFrame in the level; the ground will not turn with the ship."));
    }
    Material = UMaterialInstanceDynamic::Create(GroundMaterial ? GroundMaterial.Get() : UMaterial::GetDefaultMaterial(MD_Surface), this);
    // The sky writes the body's look into this ground's material and asks
    // whether it has the body: this frame's answer, so the ground goes first.
    for (TActorIterator<AShipSky> Sky(GetWorld()); Sky; ++Sky)
    {
        Sky->AddTickPrerequisiteActor(this);
    }
}

void AWorldGround::EndPlay(const EEndPlayReason::Type Reason)
{
    for (FPending& Pending : InFlight)
    {
        Pending.Task.Wait();
    }
    InFlight.Reset();
    Super::EndPlay(Reason);
}

void AWorldGround::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    SyncToShip();
}

void AWorldGround::SyncToShip()
{
    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship)
    {
        return;
    }
    SyncTo(Ship->IsInTransit() ? FSkySystem() : LocalSystem::Here(GetWorld()), Ship->IsInTransit());
}

void AWorldGround::SyncTo(const FSkySystem& System, bool bInTransit)
{
    UploadsLastFrame = 0;
    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship || bInTransit)
    {
        Release();
        return;
    }
    const FUniversePosition ShipAt = Ship->GetFlightState().GetUniversePosition();

    // The nearest solid world within the prefetch range: the ground there.
    const FSkyBody* Near = nullptr;
    double NearAltitude = TNumericLimits<double>::Max();
    for (const FSkyBody& Candidate : System.Bodies)
    {
        if (Candidate.Ground == EGround::Solid)
        {
            const double Altitude = ShipAt.DistanceTo(Candidate.Position) - Candidate.Radius;
            if (Altitude < NearAltitude)
            {
                NearAltitude = Altitude;
                Near = &Candidate;
            }
        }
    }
    if (!Near || NearAltitude > TerrainQuadtree::PrefetchAltitudeCm)
    {
        Release();
        return;
    }
    // A new world, or this one reloaded with new priors: start again.
    if (Near->Id != Body || !Ground.IsValid() || !SameRelief(Near->Relief, GroundParams))
    {
        Release();
        Body = Near->Id;
        GroundParams = Near->Relief;
        Ground = ShipGround::FromRelief(Near->Relief);
    }
    Centre = Near->Position;
    Radius = Near->Radius;
    DriveFloorAltitude = UShipSubsystem::FloorFor(*Near);
    ShipFromCentre = FVector3d(ShipAt - Centre);
    const double Agl = ShipFromCentre.Size() - Radius - Ground->Height(ShipFromCentre.GetSafeNormal(), 0.0);

    Select(Agl);
    Collect(FMath::Max(0, CVarUploadsPerFrame.GetValueOnGameThread()));
    Launch();
    Resolve();

    // The handover (decision 7): always under the drive floor, where the
    // proxy is never drawn; above it, under 50 km once the coarse cut is
    // resident, and given back over 55 km.
    if (CVarShow.GetValueOnGameThread() == 0)
    {
        bDrawsBody = false;
    }
    else if (NearAltitude < DriveFloorAltitude)
    {
        bDrawsBody = true;
    }
    else if (!bDrawsBody && NearAltitude < TerrainTile::HandoverAltitudeCm && CoarseResident())
    {
        bDrawsBody = true;
    }
    else if (bDrawsBody && NearAltitude > TerrainTile::HandoverAltitudeCm * HandbackFactor)
    {
        bDrawsBody = false;
    }
    Morph = TerrainTile::MorphFraction(NearAltitude, DriveFloorAltitude);
    if (Material)
    {
        Material->SetScalarParameterValue(MorphName, static_cast<float>(Morph));
    }
    Place();
}

void AWorldGround::Release()
{
    for (FPending& Pending : InFlight)
    {
        Pending.Task.Wait();
    }
    InFlight.Reset();
    Finished.Reset();
    for (const TPair<FTileKey, FResident>& Pair : Resident)
    {
        Free(Pair.Value.Component);
    }
    Resident.Reset();
    Wanted.Reset();
    Prefetch.Reset();
    Drawn.Reset();
    Ground.Reset();
    Body = NAME_None;
    bDrawsBody = false;
    bResidencyChanged = true;
}

TOptional<TerrainQuadtree::FHeightRange> AWorldGround::BoundsOf(const FTileKey& Key) const
{
    if (Key.Level == 0)
    {
        return TerrainQuadtree::FHeightRange{ Ground->MinHeightCm(), Ground->MaxHeightCm() };
    }
    if (const FResident* Parent = Resident.Find(Key.Parent()))
    {
        return TerrainTile::ChildRange(Parent->Tile, Key.QuadrantInParent(), *Ground);
    }
    return {};
}

void AWorldGround::Select(double GroundAltitudeCm)
{
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const double Moved = (ShipFromCentre - LastCutFrom).Size();
    if (!Wanted.IsEmpty() && !bResidencyChanged && Moved < 0.05 * FMath::Max(GroundAltitudeCm, 100.0) && Now - LastCutTime < 0.25)
    {
        return;
    }
    TerrainQuadtree::FCutParams Params;
    Params.RadiusCm = Radius;
    Params.MaxLevel = TerrainQuadtree::MaxLevel(Radius);
    Params.SplitFactor = FMath::Max(1.0f, CVarSplitFactor.GetValueOnGameThread());
    Params.MaxTiles = FMath::Max(64, CVarMaxTiles.GetValueOnGameThread());
    Params.OccluderRadiusCm = Radius + Ground->MinHeightCm();
    Params.GroundAltitudeCm = GroundAltitudeCm;
    const TerrainQuadtree::FCut Cut = TerrainQuadtree::SelectCut(ShipFromCentre, Params,
        [this](const FTileKey& Key) { return BoundsOf(Key); });
    Wanted = Cut.Leaves;
    Prefetch = Cut.Prefetch;
    bCapBinding = Cut.bCapBinding;
    LastCutFrom = ShipFromCentre;
    LastCutTime = Now;
    bResidencyChanged = false;
}

TSet<FTileKey> AWorldGround::NeededKeys() const
{
    TSet<FTileKey> Needed(Prefetch);
    for (const FTileKey& Leaf : Wanted)
    {
        for (FTileKey Key = Leaf;; Key = Key.Parent())
        {
            bool bAlready = false;
            Needed.Add(Key, &bAlready);
            if (bAlready || Key.Level == 0)
            {
                break;
            }
        }
    }
    return Needed;
}

void AWorldGround::Launch()
{
    const int32 Slots = FMath::Max(1, CVarBuildTasks.GetValueOnGameThread()) - InFlight.Num();
    if (Slots <= 0 || !Ground.IsValid())
    {
        return;
    }
    TArray<FTileKey> Candidates;
    const auto Consider = [&](const FTileKey& Key)
    {
        if (!Resident.Contains(Key)
            && !InFlight.ContainsByPredicate([&](const FPending& Pending) { return Pending.Key == Key; })
            && !Finished.ContainsByPredicate([&](const FTileBuild& Built) { return Built.Key == Key; }))
        {
            Candidates.AddUnique(Key);
        }
    };
    for (const FTileKey& Key : Prefetch)
    {
        Consider(Key);
    }
    for (const FTileKey& Key : Wanted)
    {
        Consider(Key);
    }
    // Coarse first -- the chain and the cap under the ship -- then nearest.
    const FVector3d Nadir = ShipFromCentre.GetSafeNormal();
    Candidates.Sort([&](const FTileKey& A, const FTileKey& B)
    {
        if (A.Level != B.Level)
        {
            return A.Level < B.Level;
        }
        return FVector3d::DotProduct(TerrainQuadtree::CentreDirection(A), Nadir) > FVector3d::DotProduct(TerrainQuadtree::CentreDirection(B), Nadir);
    });
    for (int32 Index = 0; Index < FMath::Min(Slots, Candidates.Num()); ++Index)
    {
        const FTileKey Key = Candidates[Index];
        const FGroundFieldRef Field = Ground;
        InFlight.Add(FPending{ Key, UE::Tasks::Launch(UE_SOURCE_LOCATION,
            [Field, Key]() { return TerrainTile::Build(*Field, Key); }, UE::Tasks::ETaskPriority::BackgroundNormal) });
    }
}

void AWorldGround::Collect(int32 Budget)
{
    for (int32 Index = InFlight.Num() - 1; Index >= 0; --Index)
    {
        if (InFlight[Index].Task.IsCompleted())
        {
            Finished.Add(MoveTemp(InFlight[Index].Task.GetResult()));
            InFlight.RemoveAtSwap(Index);
        }
    }
    const TSet<FTileKey> Needed = NeededKeys();
    Finished.RemoveAll([&](const FTileBuild& Built) { return !Needed.Contains(Built.Key); });
    Finished.Sort([](const FTileBuild& A, const FTileBuild& B) { return A.Key.Level < B.Key.Level; });
    const int32 Uploads = FMath::Min(Budget, Finished.Num());
    for (int32 Index = 0; Index < Uploads; ++Index)
    {
        Upload(Finished[Index]);
    }
    Finished.RemoveAt(0, Uploads);

    // What nothing needs goes back to the pool.
    for (auto It = Resident.CreateIterator(); It; ++It)
    {
        if (!Needed.Contains(It.Key()))
        {
            Free(It.Value().Component);
            It.RemoveCurrent();
            bResidencyChanged = true;
        }
    }
}

void AWorldGround::Upload(const FTileBuild& Tile)
{
    int32 Index = INDEX_NONE;
    bool bFirst = false;
    if (FreeComponents.Num() > 0)
    {
        Index = FreeComponents.Pop();
    }
    else
    {
        NewTileComponent();
        Index = Pool.Num() - 1;
        bFirst = true;
    }
    UPrimitiveComponent* Component = Pool[Index];
    UploadTo(Component, Tile, bFirst);
    // The band limit and the pivot, per tile, as custom primitive data: one
    // material instance serves every tile (decision 6).
    Component->SetCustomPrimitiveDataFloat(0, static_cast<float>(Tile.SpacingCm / Radius));
    Component->SetCustomPrimitiveDataVector3(1, FVector(Tile.Pivot));
    Resident.Add(Tile.Key, FResident{ Tile, Index });
    ++UploadsLastFrame;
    bResidencyChanged = true;
}

void AWorldGround::Free(int32 Component)
{
    if (Pool.IsValidIndex(Component) && Pool[Component])
    {
        Pool[Component]->SetVisibility(false);
        FreeComponents.Push(Component);
    }
}

void AWorldGround::Resolve()
{
    Drawn.Reset();
    const TSet<FTileKey> Leaves(Wanted);
    TSet<FTileKey> Interior;
    for (const FTileKey& Leaf : Wanted)
    {
        for (FTileKey Up = Leaf; Up.Level > 0;)
        {
            Up = Up.Parent();
            bool bAlready = false;
            Interior.Add(Up, &bAlready);
            if (bAlready)
            {
                break;
            }
        }
    }
    // A node is drawn only when resident; a parent stays drawn until all four
    // of its children can be -- never a hole, never two levels over one spot.
    TFunction<bool(const FTileKey&, TArray<FTileKey>&)> Draw = [&](const FTileKey& Key, TArray<FTileKey>& Out) -> bool
    {
        if (Leaves.Contains(Key))
        {
            if (Resident.Contains(Key))
            {
                Out.Add(Key);
                return true;
            }
            return false;
        }
        if (!Interior.Contains(Key))
        {
            return true;   // culled: nothing to draw, nothing missing
        }
        TArray<FTileKey> Children;
        bool bAll = true;
        for (int32 Quadrant = 0; Quadrant < 4 && bAll; ++Quadrant)
        {
            bAll &= Draw(Key.Child(Quadrant), Children);
        }
        if (bAll)
        {
            Out.Append(Children);
            return true;
        }
        if (Resident.Contains(Key))
        {
            Out.Add(Key);
            return true;
        }
        return false;
    };
    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        Draw(FTileKey{ static_cast<uint8>(FaceIndex), 0, 0, 0 }, Drawn);
    }
}

bool AWorldGround::CoarseResident() const
{
    for (const FTileKey& Key : Prefetch)
    {
        if (!Resident.Contains(Key))
        {
            return false;
        }
    }
    for (const FTileKey& Leaf : Wanted)
    {
        FTileKey Coarse = Leaf;
        while (Coarse.Level > TerrainQuadtree::PrefetchLevel)
        {
            Coarse = Coarse.Parent();
        }
        if (!Resident.Contains(Coarse))
        {
            return false;
        }
    }
    return !Wanted.IsEmpty();
}

void AWorldGround::Place()
{
    const TSet<FTileKey> Shown(Drawn);
    for (const TPair<FTileKey, FResident>& Pair : Resident)
    {
        UPrimitiveComponent* Component = Pool[Pair.Value.Component];
        const bool bVisible = bDrawsBody && Shown.Contains(Pair.Key);
        if (bVisible)
        {
            // The pivot relative to the ship, subtracted in doubles, in
            // universe axes: the counter-frame's rotation turns it.
            Component->SetRelativeLocation(FVector(Pair.Value.Tile.Pivot - ShipFromCentre));
        }
        if (Component->IsVisible() != bVisible)
        {
            Component->SetVisibility(bVisible);
        }
    }
}

UPrimitiveComponent* AWorldGround::GetTileComponent(const FTileKey& Key) const
{
    const FResident* Found = Resident.Find(Key);
    return Found && Pool.IsValidIndex(Found->Component) ? Pool[Found->Component].Get() : nullptr;
}

const FTileBuild* AWorldGround::GetResidentTile(const FTileKey& Key) const
{
    const FResident* Found = Resident.Find(Key);
    return Found ? &Found->Tile : nullptr;
}

TOptional<double> AWorldGround::DrawnHeightUnderShip() const
{
    const FVector3d Nadir = ShipFromCentre.GetSafeNormal();
    for (const FTileKey& Key : Drawn)
    {
        if (TerrainQuadtree::KeyAt(Nadir, Key.Level) == Key)
        {
            if (const FResident* Found = Resident.Find(Key))
            {
                if (const TOptional<double> Height = TerrainTile::SampleHeight(Found->Tile, Nadir))
                {
                    return *Height * Morph;
                }
            }
        }
    }
    return {};
}

void AWorldGround::FlushBuildsForTest()
{
    for (int32 Round = 0; Round < 4096; ++Round)
    {
        bResidencyChanged = true;
        SyncToShip();
        if (InFlight.IsEmpty() && Finished.IsEmpty())
        {
            break;   // nothing left to build: the cut is resident
        }
        while (!InFlight.IsEmpty() || !Finished.IsEmpty())
        {
            if (!InFlight.IsEmpty())
            {
                InFlight[0].Task.Wait();
            }
            Collect(TNumericLimits<int32>::Max());
            Launch();
        }
    }
    bResidencyChanged = true;
    SyncToShip();
}

FString AWorldGround::Describe() const
{
    if (!Ground.IsValid())
    {
        return TEXT("ds.Terrain: no solid world within 1,000 km.");
    }
    TMap<int32, FIntVector> PerLevel;   // drawn, resident, building
    for (const FTileKey& Key : Drawn) { ++PerLevel.FindOrAdd(Key.Level).X; }
    for (const TPair<FTileKey, FResident>& Pair : Resident) { ++PerLevel.FindOrAdd(Pair.Key.Level).Y; }
    for (const FPending& Pending : InFlight) { ++PerLevel.FindOrAdd(Pending.Key.Level).Z; }
    PerLevel.KeySort(TLess<int32>());
    FString Out = FString::Printf(TEXT("ds.Terrain: %s, %s the body, morph %.3f, %d drawn, %d resident, %d building%s\n"),
        *Body.ToString(), bDrawsBody ? TEXT("drawing") : TEXT("not drawing"), Morph, Drawn.Num(), Resident.Num(), InFlight.Num(),
        bCapBinding ? TEXT(", the tile cap BINDING") : TEXT(""));
    for (const TPair<int32, FIntVector>& Level : PerLevel)
    {
        Out += FString::Printf(TEXT("  level %2d: %4d drawn, %4d resident, %2d building\n"), Level.Key, Level.Value.X, Level.Value.Y, Level.Value.Z);
    }
    return Out;
}

UPrimitiveComponent* AWorldGround::NewTileComponent()
{
    UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
    Mesh->SetupAttachment(Root);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->bUseAsyncCooking = false;
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    // A thousand tiles moving every frame would invalidate the Sun's cached
    // virtual shadow pages every frame and shadow the deck (decision 6).
    Mesh->SetCastShadow(false);
    Mesh->bAffectDistanceFieldLighting = false;
    Mesh->bAffectDynamicIndirectLighting = false;
    Mesh->bVisibleInRayTracing = false;
    Mesh->bVisibleInReflectionCaptures = false;
    Mesh->bVisibleInRealTimeSkyCaptures = false;
    Mesh->bReceivesDecals = false;
    Mesh->bNeverDistanceCull = true;
    Mesh->SetVisibility(false);
    Mesh->RegisterComponent();
    Mesh->SetMaterial(0, Material);
    Pool.Add(Mesh);
    return Mesh;
}

void AWorldGround::UploadTo(UPrimitiveComponent* Component, const FTileBuild& Tile, bool bFirst)
{
    UProceduralMeshComponent* Mesh = CastChecked<UProceduralMeshComponent>(Component);
    TArray<FVector> Positions;
    TArray<FVector> Normals;
    TArray<FVector2D> UV0;
    TArray<FVector2D> UV1;
    TArray<FVector2D> UV2;
    Positions.Reserve(TerrainTile::VertexCount);
    Normals.Reserve(TerrainTile::VertexCount);
    UV0.Init(FVector2D::ZeroVector, TerrainTile::VertexCount);
    UV1.Reserve(TerrainTile::VertexCount);
    UV2.Reserve(TerrainTile::VertexCount);
    for (int32 V = 0; V < TerrainTile::VertexCount; ++V)
    {
        Positions.Add(FVector(Tile.Positions[V]));
        Normals.Add(FVector(Tile.Normals[V]));
        UV1.Add(FVector2D(TerrainTile::UV1Of(Tile, V)));
        UV2.Add(FVector2D(TerrainTile::UV2Of(Tile, V)));
    }
    if (bFirst || Mesh->GetNumSections() == 0)
    {
        Mesh->CreateMeshSection(0, Positions, TerrainTile::Indices(), Normals, UV0, UV1, UV2, TArray<FVector2D>(),
                                TArray<FColor>(), TArray<FProcMeshTangent>(), false);
    }
    else
    {
        Mesh->UpdateMeshSection(0, Positions, Normals, UV0, UV1, UV2, TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>());
    }
    // The morph lowers a vertex by up to its height along its direction, far
    // outside a small tile's box above the drive floor: widen the bounds by
    // the tile's relief so it is never culled while it is drawn.
    const double HalfExtent = FMath::Max(1.0, 0.5 * Mesh->GetLocalBounds().GetBox().GetSize().GetMin());
    Mesh->SetBoundsScale(static_cast<float>(1.0 + FMath::Max(FMath::Abs(Tile.Range.MinCm), FMath::Abs(Tile.Range.MaxCm)) / HalfExtent));
}
```

- [ ] **Step 5: Run: PASS**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Surface.GroundActor && ./test.sh DeepSpace.Sky
```

Expected: `GroundActor` passes, its info line naming the drawn and resident counts; the sky's
tests are unchanged (the ground draws nothing the sky does not yet hand it).

- [ ] **Step 6: Document**

`CLAUDE.md`, *Architecture*, after the `Sky/` line:

```markdown
- `Source/DeepSpace/Surface/` -- the ground (landing slice b): `IGroundField`
  (`GroundField.*`, WorldRelief behind the flight's interface), the pure
  quadtree and tile builder (`TerrainQuadtree.*`, `TerrainTile.*`), and
  `AWorldGround`, which streams the nearest solid world's tiles off the game
  thread into pooled meshes on the counter-frame (*The ground*).
```

and in the tunables table:

```markdown
| `ds.Terrain.SplitFactor`, `.MaxTiles`, `.BuildTasks`, `.UploadsPerFrame`, `.Show` | 2.0, 2,500, 2, 4, 1 | `WorldGround.cpp`, from `TerrainQuadtree` |
```

- [ ] **Step 7: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
git add Source/DeepSpace/Surface/WorldGround.h Source/DeepSpace/Surface/WorldGround.cpp Source/DeepSpace/Tests/WorldGroundTest.cpp \
        Source/DeepSpace/Tests/SkyTestWorld.h CLAUDE.md && \
git commit -m "$(cat <<'EOF'
feat(terrain): AWorldGround -- the cut streamed on two workers into pooled tiles on the counter-frame

The nearest solid world's quadtree, cut from the ship's origin, built with
UE::Tasks at most ds.Terrain.BuildTasks at once, uploaded at most
ds.Terrain.UploadsPerFrame a frame, each tile at UniverseToWorld of its
pivot, no shadow, no distance fields, no ray tracing; a parent drawn until
all four children are; the body claimed under the drive floor always and
under 50 km once the coarse cut is resident (landing decisions 6, 7).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 8: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Source/DeepSpace/Surface/WorldGround.cpp '    Mesh->SetCastShadow(false);' '    Mesh->SetCastShadow(true);' DeepSpace.Surface.GroundActor && \
Tools/mutate.sh Source/DeepSpace/Surface/WorldGround.cpp 'Component->SetRelativeLocation(FVector(Pair.Value.Tile.Pivot - ShipFromCentre));' 'Component->SetRelativeLocation(FVector(Pair.Value.Tile.Pivot));' DeepSpace.Surface.GroundActor && \
Tools/mutate.sh Source/DeepSpace/Surface/WorldGround.cpp 'const int32 Uploads = FMath::Min(Budget, Finished.Num());' 'const int32 Uploads = Finished.Num();' DeepSpace.Surface.GroundActor && \
./build.sh
```

Expected: `KILLED` three times.

---

## Task 37 (T7): `M_SkyGround` -- the ground shades as the orbit does; the contract on three sides; `hauler_ground` in the level

**Owner:** T. **Depends on:** T2, T6. Spec decision 9; decision 1's parity at 50 km.

**Files:**
- Modify: `Shaders/Private/WorldRelief.ush` (append `WR_GroundNormal`)
- Modify: `Source/DeepSpace/Surface/WorldRelief.h`, `.cpp` (the `WorldReliefShading` functions)
- Modify: `Tools/sky_material_contract.json`, `Source/DeepSpace/Sky/SkyMaterialContract.h`,
  `Tools/setup_sky_materials.py` (`custom_node`, `primitive_parameter`, `sky_ground()`,
  `sky_ground_probe()`, `main`)
- Modify: `Source/DeepSpace/Surface/WorldGround.cpp` (`MorphName` -> `SkyMaterial::Morph`)
- Modify: `Tools/build_hauler.py` (`place_ground`, called after `place_sky`),
  `Tools/verify_level.py` (`check_ground`, called beside `check_sky`),
  `Tools/placement.py` only if `sky_asset` lives there (it does: `PL.sky_asset`)
- Create: `Source/DeepSpace/Tests/GroundShadingTest.cpp` (`DeepSpace.Surface.GroundShadesAsOrbit`)
- Modify: `Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp` (the ground probe's case)
- Modify: `Source/DeepSpace/Tests/SkyMaterialContractTest.cpp` (`Materials` rows: `M_SkyGround`,
  `M_SkyGroundProbe`; the custom-primitive-data index check)
- Modify: `Tools/placement.py` (`GROUND_TAG`), `Tools/test_placement.py`
  (`test_the_ground_tag_is_the_one_the_cpp_sets`)
- Assets: `M_SkyGround.uasset`, `M_SkyGroundProbe.uasset` (new), every other material the authoring
  run re-saves under `Content/Materials/Sky`, and `Content/Maps/L_Hauler.umap` (rebuilt)

**Interfaces:**
- Consumes: `WR_SurfaceTerms(..., Stretch, VertexBandLimit)` and `WorldReliefNoise::FaceF64(..., VertexBandLimit)`
  (Task T2); the Python `shared_terms(g, direction, footprint, seed, stretch, vertex_band_limit)`,
  `surface(g, knobs, seed, direction, footprint, terms)`, `probe_direction(g)`, `mask`,
  `fresh_material`, `finish`, `Graph`, `SHARED` (Tasks R1, R4, T2); `FWorldRelief::SlopeScale` (T1);
  `ShipGround::NormalAt` (F2); `TerrainQuadtree::SpacingCm` (T3).
- Produces: `struct WR_GroundNormalOut { WR_REAL NX; WR_REAL NY; WR_REAL NZ; }`,
  `WR_GroundNormalOut WR_GroundNormal(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL VX, WR_REAL VY, WR_REAL VZ, WR_REAL SX, WR_REAL SY, WR_REAL SZ)`;
  `struct WorldReliefShading::FSurface { FFaceTerms Terms; FVector3d Slope; FVector3d Normal; }`,
  `FSurface WorldReliefShading::Orbit(const FWorldReliefParams&, const FVector3d& D, double FootprintRadius)`,
  `FSurface WorldReliefShading::Ground(const FWorldReliefParams&, const FVector3d& D, const FVector3d& VertexNormal, double FootprintRadius, double VertexBandLimit)`;
  `SkyMaterial::GroundPath`, `SkyMaterial::GroundProbePath`, `SkyMaterial::Morph`,
  `SkyMaterial::BandLimit`, `SkyMaterial::TilePivot`, `SkyMaterial::VertexBandLimit`;
  `M_SkyGround`, `M_SkyGroundProbe`; the level's `hauler_ground`.

The face needs no new proof: `M_SkyGround` composes its face with the very `surface()` graph
`M_SkyBody` uses, from the same raw terms, and `VertexBandLimit` moves only the slopes (Task T2).
What differs is the normal, which is what this task holds equal.

- [ ] **Step 1: Write the failing headless test**

Create `Source/DeepSpace/Tests/GroundShadingTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Surface/GroundField.h"
#include "Surface/TerrainQuadtree.h"
#include "Surface/WorldRelief.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 9: the ground shades exactly as the orbit does. A tile's
 * vertex normal carries the bands its spacing resolves; the pixel adds only
 * what the vertices do not, max(0, fade - carried); on ground finer than its
 * pixels -- every tile at the handover, CDLOD keeping spacing in proportion
 * to distance -- the sum is the orbit's, band for band. So at 50 km there is
 * no brightness step and no blur: the same numbers, not nearly the same.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundShadesAsOrbitTest, "DeepSpace.Surface.GroundShadesAsOrbit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroundShadesAsOrbitTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    const FWorldReliefParams Params = FixtureParams();
    const FGroundFieldRef Ground = ShipGround::FromRelief(Params);
    const double R = Params.RadiusCm;
    FRandomStream Random(50);
    double WorstNormal = 0.0;
    double WorstFace = 0.0;
    for (int32 Level = 6; Level <= 12; ++Level)
    {
        const double Spacing = TerrainQuadtree::SpacingCm(Level, R);
        const double Footprint = 0.1 * Spacing / R;
        for (int32 Trial = 0; Trial < 100; ++Trial)
        {
            const FVector3d D(Random.GetUnitVector());
            const FVector3d Vertex = ShipGround::NormalAt(*Ground, D, Spacing);
            const WorldReliefShading::FSurface Orbit = WorldReliefShading::Orbit(Params, D, Footprint);
            const WorldReliefShading::FSurface Tile = WorldReliefShading::Ground(Params, D, Vertex, Footprint, Spacing / R);
            WorstNormal = FMath::Max(WorstNormal, (Orbit.Normal - Tile.Normal).Size());
            WorstFace = FMath::Max3(WorstFace, FMath::Abs(Orbit.Terms.Continent - Tile.Terms.Continent),
                                    FMath::Max(FMath::Abs(Orbit.Terms.Detail - Tile.Terms.Detail),
                                               FMath::Abs(Orbit.Terms.CraterAlbedo - Tile.Terms.CraterAlbedo)));
        }
    }
    TestTrue(FString::Printf(TEXT("the ground's normal is the orbit's (worst %.3g)"), WorstNormal), WorstNormal <= 1.0e-9);
    TestTrue(FString::Printf(TEXT("and its face terms the orbit's, exactly (worst %.3g)"), WorstFace), WorstFace == 0.0);

    // And the split is real: with the vertices carrying nothing, the pixel
    // carries everything -- the orbit, again.
    const FVector3d D = FVector3d(0.2, 0.7, -0.3).GetSafeNormal();
    const WorldReliefShading::FSurface Orbit = WorldReliefShading::Orbit(Params, D, 1.0e-6);
    const WorldReliefShading::FSurface Bare = WorldReliefShading::Ground(Params, D, D, 1.0e-6, 1.0);
    TestTrue(TEXT("a flat vertex with no bands carried shades as the orbit"), (Orbit.Normal - Bare.Normal).Size() <= 1.0e-12);
    TestTrue(TEXT("and the orbit's relief is really tilting it"), (Orbit.Normal - D).Size() > 1.0e-6);
    return true;
}

#endif
```

Run `./build.sh`: fails, `'WorldReliefShading': is not a namespace`.

- [ ] **Step 2: The ground's normal, in the shared file**

Append to `Shaders/Private/WorldRelief.ush`:

```hlsl
// -- The ground's per-pixel normal (landing decision 9) ----------------------
// Composed exactly as the orbit's is, normalize(D - s_t), from two parts: the
// slope a tile's vertex normal carries, recovered from that normal (it was
// built as normalize(D - s_v,t), so s_v,t = D - Nv / (Nv . D)), and the
// pixel's own slope S -- which the material formed from only the bands the
// vertices do not carry -- taken along the ground.
struct WR_GroundNormalOut
{
    WR_REAL NX;
    WR_REAL NY;
    WR_REAL NZ;
};

WR_GroundNormalOut WR_GroundNormal(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL VX, WR_REAL VY, WR_REAL VZ,
                                   WR_REAL SX, WR_REAL SY, WR_REAL SZ)
{
    const WR_REAL VD = WR_max(VX * DX + VY * DY + VZ * DZ, WR_REAL(1.0e-3));
    const WR_REAL CarriedX = DX - VX / VD;
    const WR_REAL CarriedY = DY - VY / VD;
    const WR_REAL CarriedZ = DZ - VZ / VD;
    const WR_REAL PD = SX * DX + SY * DY + SZ * DZ;
    const WR_REAL NX = DX - CarriedX - (SX - PD * DX);
    const WR_REAL NY = DY - CarriedY - (SY - PD * DY);
    const WR_REAL NZ = DZ - CarriedZ - (SZ - PD * DZ);
    const WR_REAL NL = WR_sqrt(NX * NX + NY * NY + NZ * NZ);
    WR_GroundNormalOut Out;
    Out.NX = NX / NL;
    Out.NY = NY / NL;
    Out.NZ = NZ / NL;
    return Out;
}
```

- [ ] **Step 3: The C++ side**

In `Surface/WorldRelief.h`, after the class:

```cpp
/** What M_SkyBody and M_SkyGround shade a rocky world with, computed by the
 *  same shared file on the CPU, for the tests that hold them equal: the raw
 *  face terms (both materials compose the face from them with one graph,
 *  surface() in setup_sky_materials.py, so equal terms are an equal face),
 *  the slope that graph forms, ReliefScale x (detail + Cratering x craters),
 *  and the unit normal. */
namespace WorldReliefShading
{
    struct FSurface
    {
        FFaceTerms Terms;
        FVector3d Slope = FVector3d::ZeroVector;
        FVector3d Normal = FVector3d::UnitZ();
    };

    /** M_SkyBody's: every slope the footprint keeps. */
    DEEPSPACE_API FSurface Orbit(const FWorldReliefParams& Params, const FVector3d& D, double FootprintRadius);

    /** M_SkyGround's: the tile's VertexNormal, and the pixel's slope of the
     *  bands the vertices at VertexBandLimit (radius units) do not carry. */
    DEEPSPACE_API FSurface Ground(const FWorldReliefParams& Params, const FVector3d& D, const FVector3d& VertexNormal,
                                  double FootprintRadius, double VertexBandLimit);
}
```

In `WorldRelief.cpp`:

```cpp
namespace WorldReliefShadingLocal
{
    /** surface()'s slope for a rocky world (stretch 1). */
    FVector3d SlopeOf(const FWorldReliefParams& Params, const FFaceTerms& Terms)
    {
        const double ReliefScale = FWorldRelief(Params).SlopeScale();
        return (Terms.DetailSlope + Terms.CraterSlope * FMath::Max(Params.Cratering, 0.0)) * ReliefScale;
    }
}

WorldReliefShading::FSurface WorldReliefShading::Orbit(const FWorldReliefParams& Params, const FVector3d& D, double FootprintRadius)
{
    FSurface Out;
    Out.Terms = WorldReliefNoise::FaceF64(D, FootprintRadius, Params.SeedOffset, 1.0, 1.0);
    Out.Slope = WorldReliefShadingLocal::SlopeOf(Params, Out.Terms);
    Out.Normal = (D - (Out.Slope - D * FVector3d::DotProduct(Out.Slope, D))).GetSafeNormal();
    return Out;
}

WorldReliefShading::FSurface WorldReliefShading::Ground(const FWorldReliefParams& Params, const FVector3d& D, const FVector3d& VertexNormal,
                                                        double FootprintRadius, double VertexBandLimit)
{
    FSurface Out;
    Out.Terms = WorldReliefNoise::FaceF64(D, FootprintRadius, Params.SeedOffset, 1.0, VertexBandLimit);
    Out.Slope = WorldReliefShadingLocal::SlopeOf(Params, Out.Terms);
    const WR64::WR_GroundNormalOut N = WR64::WR_GroundNormal(D.X, D.Y, D.Z, VertexNormal.X, VertexNormal.Y, VertexNormal.Z,
                                                             Out.Slope.X, Out.Slope.Y, Out.Slope.Z);
    Out.Normal = FVector3d(N.NX, N.NY, N.NZ);
    return Out;
}
```

Run: `./build.sh && ./test.sh DeepSpace.Surface.GroundShadesAsOrbit` -- PASS. If the normal
differs by more than rounding, the vertex normal's and the pixel's band weights are not
complementary: check that `FWorldRelief::HeightAndGradient` fades each band at the footprint by
`saturate(1 - footprint x f)` exactly as `WR_VertexCarries` does, and fix the side that differs.

- [ ] **Step 4: The contract, three sides**

`Tools/sky_material_contract.json`: add parameters

```json
    "morph":             { "name": "Morph",           "type": "scalar" },
    "band_limit":        { "name": "BandLimit",       "type": "scalar", "custom_primitive_data": 0 },
    "tile_pivot":        { "name": "TilePivot",       "type": "vector", "custom_primitive_data": 1 },
    "vertex_band_limit": { "name": "VertexBandLimit", "type": "scalar" },
```

and materials

```json
    "M_SkyGround":      { "parameters": ["colour", "light_direction", "surface_seed", "brightness", "mottle", "detail", "cratering", "relief_scale", "morph", "band_limit", "tile_pivot"] },
    "M_SkyGroundProbe": { "parameters": ["surface_seed", "cratering", "relief_scale", "vertex_band_limit", "probe_footprint", "probe_bias"] },
```

plus a `"ground_comment"`: `"M_SkyGround: unlit, M_SkyBody's own law (the light, the colour, the
brightness, the 1.5 disc gain, the terminator's smoothstep, the face clamp) and M_SkyBody's own
face, composed by the same surface() graph from the shared file's terms; its normal is the
tile's vertex normal (UV1.xy, UV2.x, universe axes) plus the pixel's slope of only the bands the
vertices do not carry (WR_GroundNormal). BandLimit (the tile's spacing over R) and TilePivot (the
tile's pivot from the world's centre, cm) are custom primitive data, so one instance serves every
tile. World Position Offset lowers each vertex by (1 - Morph) x its height (UV2.y, km) along its
direction: the relief grows in between the handover and the drive floor. No rim: the sky is
black."`

`SkyMaterialContract.h`, in `SkyMaterial`:

```cpp
    inline const TCHAR* const GroundPath = TEXT("/Game/Materials/Sky/M_SkyGround.M_SkyGround");
    inline const TCHAR* const GroundProbePath = TEXT("/Game/Materials/Sky/M_SkyGroundProbe.M_SkyGroundProbe");
    inline const FName Morph = TEXT("Morph");                     // scalar: the relief's growth, 0 at 50 km to 1 at the drive floor
    inline const FName BandLimit = TEXT("BandLimit");             // scalar, custom primitive data: the tile's spacing over R
    inline const FName TilePivot = TEXT("TilePivot");             // vector, custom primitive data: the tile's pivot, cm
    inline const FName VertexBandLimit = TEXT("VertexBandLimit"); // scalar, the probe's: what the vertices carry

    /** Where BandLimit and TilePivot sit in a tile's custom primitive data. A
     *  wrong index fails as silently as a misspelt name -- every tile reads 0
     *  -- so the index is contract too: here, in the JSON's
     *  custom_primitive_data, and on M_SkyGround's parameter nodes. */
    inline constexpr int32 BandLimitPrimitiveIndex = 0;
    inline constexpr int32 TilePivotPrimitiveIndex = 1;               // 1..3
```

In `WorldGround.cpp`, replace the two custom-primitive-data lines with

```cpp
    Component->SetCustomPrimitiveDataFloat(SkyMaterial::BandLimitPrimitiveIndex, static_cast<float>(Tile.SpacingCm / Radius));
    Component->SetCustomPrimitiveDataVector3(SkyMaterial::TilePivotPrimitiveIndex, FVector(Tile.Pivot));
```

In `SkyMaterialContractTest.cpp` (`#include "Materials/MaterialExpressionScalarParameter.h"` beside
the vector parameter's include), inside the `Materials` loop after the one-node-per-parameter check:

```cpp
        // Custom primitive data: the index is contract as much as the name.
        for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
        {
            bool bPrimitive = false;
            int32 Index = -1;
            FName Name;
            if (const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
            {
                bPrimitive = Scalar->bUseCustomPrimitiveData;
                Index = Scalar->PrimitiveDataIndex;
                Name = Scalar->ParameterName;
            }
            else if (const UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
            {
                bPrimitive = Vector->bUseCustomPrimitiveData;
                Index = Vector->PrimitiveDataIndex;
                Name = Vector->ParameterName;
            }
            if (!bPrimitive)
            {
                continue;
            }
            int32 JsonIndex = -1;
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Role : JsonParameters->Values)
            {
                const TSharedPtr<FJsonObject> Entry = Role.Value->AsObject();
                if (FName(*Entry->GetStringField(TEXT("name"))) == Name && Entry->HasField(TEXT("custom_primitive_data")))
                {
                    JsonIndex = static_cast<int32>(Entry->GetNumberField(TEXT("custom_primitive_data")));
                }
            }
            TestEqual(FString::Printf(TEXT("%s: %s reads the custom primitive data the JSON says"), Expected.Asset, *Name.ToString()), Index, JsonIndex);
            if (Name == SkyMaterial::BandLimit)
            {
                TestEqual(TEXT("BandLimit's index is the header's, which WorldGround writes"), Index, SkyMaterial::BandLimitPrimitiveIndex);
            }
            else if (Name == SkyMaterial::TilePivot)
            {
                TestEqual(TEXT("TilePivot's index is the header's, which WorldGround writes"), Index, SkyMaterial::TilePivotPrimitiveIndex);
            }
            else
            {
                AddError(FString::Printf(TEXT("%s: %s reads custom primitive data the header names no index for"), Expected.Asset, *Name.ToString()));
            }
        }
```

(`JsonParameters` is the test's `Contract->GetObjectField(TEXT("parameters"))`, declared above the
loop.) `SkyMaterialContract.h` gains, beside `BodyScalars()` and `BodyVectors()`, the two new
materials' name lists:

```cpp
    inline TArray<FName> GroundScalars() { return { Brightness, Mottle, Detail, Cratering, ReliefScale, Morph, BandLimit }; }
    inline TArray<FName> GroundVectors() { return { Colour, LightDirection, SurfaceSeed, TilePivot }; }
    inline TArray<FName> GroundProbeScalars() { return { Cratering, ReliefScale, VertexBandLimit, ProbeFootprint }; }
    inline TArray<FName> GroundProbeVectors() { return { SurfaceSeed, ProbeBias }; }
```

and the test's `Materials` rows gain:

```cpp
        { TEXT("M_SkyGround"), SkyMaterial::GroundPath, SkyMaterial::GroundScalars(), SkyMaterial::GroundVectors() },
        { TEXT("M_SkyGroundProbe"), SkyMaterial::GroundProbePath, SkyMaterial::GroundProbeScalars(), SkyMaterial::GroundProbeVectors() },
```

In `WorldGround.cpp`, delete `MorphName` and write `SkyMaterial::Morph` (`#include "Sky/SkyMaterialContract.h"`).
The two `Materials` rows above go into `SkyMaterialContractTest.cpp` beside `M_SkyReliefProbe`'s, so
the test holds header, JSON and asset to the same names and the same primitive-data indices.

`Tools/setup_sky_materials.py`, after `relief_probe`:

```python
def custom_node(g, code, inputs, output_type):
    """A Custom node calling the shared file: one body of HLSL text, its
    inputs by name. The include path is the DeepSpaceShaders module's
    mapping, so the text here is only the call."""
    node = g.node(unreal.MaterialExpressionCustom)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", output_type)
    node.set_editor_property("include_file_paths", [SHARED["include"]])
    entries = []
    for input_name, _ in inputs:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        entries.append(entry)
    node.set_editor_property("inputs", entries)
    for input_name, source in inputs:
        g.link(source, node, input_name)
    return node


def primitive_parameter(node, index):
    """A parameter read from the component's custom primitive data: per
    tile, with one material instance for all of them."""
    node.set_editor_property("use_custom_primitive_data", True)
    node.set_editor_property("primitive_data_index", index)
    return node


# The ground's normal: the tile's vertex normal (UV1.xy, UV2.x) and the
# pixel's slope, composed by the shared file.
GROUND_NORMAL_CODE = (
    "WR_GroundNormalOut N = WR_GroundNormal(D.x, D.y, D.z, NormalXY.x, NormalXY.y, NormalZH.x, Slope.x, Slope.y, Slope.z);\n"
    "return float3(N.NX, N.NY, N.NZ);\n")

# The probe's: a flat vertex, its normal D itself.
GROUND_PROBE_CODE = (
    "WR_GroundNormalOut N = WR_GroundNormal(D.x, D.y, D.z, D.x, D.y, D.z, Slope.x, Slope.y, Slope.z);\n"
    "return float3(N.NX, N.NY, N.NZ);\n")


def ground_direction(g, pivot):
    """D, the unit direction from the world's centre to the pixel in
    universe axes (worlds do not spin): normalize(TilePivot + LocalPosition),
    and its footprint, max(|ddx D|, |ddy D|) * filter_pixels, as the orbit's."""
    local = g.node(unreal.MaterialExpressionLocalPosition)
    direction = g.unary(unreal.MaterialExpressionNormalize, g.add(mask(g, pivot, "rgb"), local))
    ddx = g.unary(unreal.MaterialExpressionLength, g.unary(unreal.MaterialExpressionDDX, direction))
    ddy = g.unary(unreal.MaterialExpressionLength, g.unary(unreal.MaterialExpressionDDY, direction))
    footprint = g.mul(g.binary(unreal.MaterialExpressionMax, ddx, ddy), g.constant(CONSTANTS["filter_pixels"]))
    return local, direction, footprint


def sky_ground():
    """The ground, drawn by AWorldGround's tiles (landing decision 9).

        D, footprint = ground_direction(TilePivot)            (universe axes)
        face, slope  = surface(the look, D, footprint,
                               shared_terms(..., BandLimit))  (M_SkyBody's own graph)
        N            = WR_GroundNormal(D, the vertex normal, slope)
        shaded       = gain * saturate(N.L) * smoothstep(-w, w, N.L), N in world space
        emissive     = Colour * Brightness * shaded * face
        WPO          = (Morph - 1) * h * D, carried to world space

    M_SkyBody's own law, so the handover at 50 km has no brightness step: the
    same light direction, colour and brightness (AShipSky copies the body's
    look into this material's one instance), the same face, and a normal that
    is the orbit's band for band wherever the tiles are finer than the pixels.
    """
    asset = "M_SkyGround"
    material = fresh_material(asset)
    g = Graph(material)

    colour = g.vector("colour", (1.0, 1.0, 1.0, 1.0))
    light = g.vector("light_direction", (0.0, 0.0, 1.0, 0.0))
    seed = g.vector("surface_seed", (0.0, 0.0, 0.0, 0.0))
    brightness = g.scalar("brightness", 1.0)
    mottle = g.scalar("mottle", 0.35)
    detail = g.scalar("detail", 0.3)
    cratering = g.scalar("cratering", 0.0)
    relief_scale = g.scalar("relief_scale", 0.0)
    morph = g.scalar("morph", 1.0)
    band_limit = primitive_parameter(g.scalar("band_limit", 1.0), 0)
    pivot = primitive_parameter(g.vector("tile_pivot", (0.0, 0.0, 0.0, 0.0)), 1)

    local, direction, footprint = ground_direction(g, pivot)
    # Ground is never banded: the knobs' banding is 0, so the stretch is 1.
    knobs = (mottle, detail, g.constant(0.0), relief_scale, cratering)
    factor, slope = surface(g, knobs, seed, direction, footprint,
                            lambda gg, d, fp, s, st: shared_terms(gg, d, fp, s, st, band_limit))
    uv1 = g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=1)
    uv2 = g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=2)
    normal_local = custom_node(g, GROUND_NORMAL_CODE,
                               [("D", direction), ("NormalXY", uv1), ("NormalZH", uv2), ("Slope", slope)],
                               unreal.CustomMaterialOutputType.CMOT_FLOAT3)

    to_world = g.node(unreal.MaterialExpressionTransform,
                      transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                      transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    g.link(normal_local, to_world)
    normal = g.unary(unreal.MaterialExpressionNormalize, to_world)
    n_dot_l = g.binary(unreal.MaterialExpressionDotProduct, normal, light)
    lambert = g.unary(unreal.MaterialExpressionSaturate, n_dot_l)
    width = float(CONSTANTS["terminator_width"])
    soft = g.node(unreal.MaterialExpressionSmoothStep, const_min=-width, const_max=width)
    g.link(n_dot_l, soft, "Value")
    shaded = g.mul(g.mul(lambert, soft), g.constant(CONSTANTS["lambert_disc_gain"]))
    g.emissive(g.mul(g.mul(colour, brightness), g.mul(shaded, factor)))

    # The morph: every vertex lowered by (1 - Morph) x its height along its
    # own direction -- the sphere at the handover, the whole relief at the
    # drive floor. The height rides UV2.y in km (PMC's UVs are half floats).
    height = g.mul(mask(g, uv2, "g"), g.constant(1.0e5))
    offset = g.mul(direction, g.mul(g.add(morph, g.constant(-1.0)), height))
    wpo = g.node(unreal.MaterialExpressionTransform,
                 transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                 transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    g.link(offset, wpo)
    unreal.MaterialEditingLibrary.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    finish(material, asset)


def sky_ground_probe():
    """Eyes.WorldReliefParity's ground case: M_SkyGround's per-pixel normal
    over the probe patch (probe_direction), a flat vertex carrying
    VertexBandLimit's bands, at ProbeFootprint, untonemapped. The face is not
    drawn: it is surface()'s composition of the terms M_SkyReliefProbe
    already holds to the C++, and VertexBandLimit moves only the slopes.

        pixel = ProbeBias.rgb + WR_GroundNormal(D, D, slope)"""
    asset = "M_SkyGroundProbe"
    material = fresh_material(asset)
    g = Graph(material)
    seed = g.vector("surface_seed", (0.0, 0.0, 0.0, 0.0))
    cratering = g.scalar("cratering", 0.0)
    relief_scale = g.scalar("relief_scale", 0.0)
    limit = g.scalar("vertex_band_limit", 1.0)
    footprint = g.scalar("probe_footprint", 0.0)
    bias = g.vector("probe_bias", (0.0, 0.0, 0.0, 0.0))
    direction = probe_direction(g)
    knobs = (g.constant(0.35), g.constant(0.3), g.constant(0.0), relief_scale, cratering)
    _, slope = surface(g, knobs, seed, direction, footprint,
                       lambda gg, d, fp, s, st: shared_terms(gg, d, fp, s, st, limit))
    normal = custom_node(g, GROUND_PROBE_CODE, [("D", direction), ("Slope", slope)],
                         unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    g.emissive(g.add(normal, mask(g, bias, "rgb")))
    finish(material, asset)
```

In `main()`, after `sky_body(...)`'s call, add `sky_ground()` and `sky_ground_probe()`.

- [ ] **Step 5: The level**

`Tools/build_hauler.py`, after `place_sky`:

```python
def place_ground(actor_sub):
    """The ground under the ship (landing decision 6): AWorldGround streams
    the nearest solid world's tiles at runtime and draws them on the
    counter-frame. The level holds one, at the origin, with its material --
    asset assignment only (ADR 0002). It attaches itself to the
    counter-frame at BeginPlay."""
    ground = actor_sub.spawn_actor_from_class(
        unreal.WorldGround, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    ground.set_actor_label(TAG + "ground")
    ground.set_editor_property("ground_material", sky_asset("M_SkyGround"))
    # AWorldGround tags itself in its constructor; the level carries the tag
    # too, so verify_level finds it in the saved level without C++ running.
    ground.set_editor_property("tags", [unreal.Name(PL.GROUND_TAG)])
    return ground
```

and in `main`'s placement block, after `place_sky(actor_sub)`: `place_ground(actor_sub)`.

`Tools/placement.py`, after `GLASS_TAG = "Sky.Glass"`:

```python
# AWorldGround::GroundTag: how verify_level finds hauler_ground.
GROUND_TAG = "Sky.Ground"
```

`Tools/test_placement.py`, after `test_the_glass_tag_is_the_one_the_cpp_traces_for` (and
`GROUND_TAG` added to its `from placement import ...` line beside `GLASS_TAG`):

```python
def test_the_ground_tag_is_the_one_the_cpp_sets():
    with open(os.path.join(ROOT, "Source/DeepSpace/Surface/WorldGround.cpp")) as f:
        cpp = f.read()
    assert 'GroundTag(TEXT("%s"))' % GROUND_TAG in cpp, GROUND_TAG
```

Run `python3 Tools/test_placement.py`: passes (`WorldGround.cpp` has
`const FName AWorldGround::GroundTag(TEXT("Sky.Ground"));` from Task T6).

`Tools/verify_level.py`, after `check_sky`:

```python
def check_ground(every):
    """One AWorldGround, on M_SkyGround, tagged PL.GROUND_TAG (Sky.Ground):
    the tiles' material, and the tag the level's own checks find it by."""
    failures = []
    grounds = of_class(every, unreal.WorldGround)
    if len(grounds) != 1:
        failures.append("%d grounds, want exactly one" % len(grounds))
    for ground in grounds[:1]:
        got = path_of(ground.get_editor_property("ground_material"))
        if got != PL.sky_asset("M_SkyGround"):
            failures.append("ground ground_material is %s, not %s" % (got, PL.sky_asset("M_SkyGround")))
        if PL.GROUND_TAG not in [str(tag) for tag in ground.tags]:
            failures.append("the ground is not tagged %s" % PL.GROUND_TAG)
    return failures
```

and add `check_ground(every)`'s failures wherever `check_sky(every)`'s are collected.

- [ ] **Step 6: The rendered parity case**

In `Tests/Eyes/WorldReliefParityTest.cpp`, after the orbit's loop, add a case: load
`SkyMaterial::GroundProbePath` into a `UMaterialInstanceDynamic` as the relief probe is loaded;
set `SurfaceSeed` to Baemsekai IV's (`ShipSky::SurfaceSeed(Fourth.SurfaceSeed, Fourth.BeltPairs)`),
`Cratering` to `Fourth.Relief.Cratering`, `ReliefScale` to `FWorldRelief(Fourth.Relief).SlopeScale()`,
`VertexBandLimit` to the spacing of the level the cut uses at 50 km over it
(`TerrainQuadtree::SpacingCm(9, Fourth.Relief.RadiusCm) / Fourth.Relief.RadiusCm`), and, at each of
the test's five footprints, `ProbeFootprint`; draw it once with the file's own helper,
`const TArray<FVector3d> GroundNormals = Draw(Test.World, Target, GroundProbe, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));`
(the probe has no `ProbeSelect`; its rgb is the normal), take each sample's D from the relief
probe's `Direction` pass at the same footprint (both probes draw `probe_direction`'s patch), and
compare `GroundNormals[Index]` with
`WorldReliefShading::Ground(Fourth.Relief, D, D, Footprint, VertexBandLimit).Normal` (the flat
vertex the probe uses): max absolute difference 1e-3 in each component, the same step mask and 1%
allowance as the orbit's case, and a `Report` line per footprint
(`TEXT("  ground normal C++ vs GPU:          ")` and the largest gap). With
`DeepSpace.Surface.GroundShadesAsOrbit` (the ground's normal equals the orbit's on the CPU,
exactly, and its face terms are the orbit's) this holds the ground's normal on the GPU to the C++.
It does not draw the shipped `M_SkyGround` graph; Task 38 (T8)'s `Eyes.HandoverParity` does, against
`M_SkyBody` over the same patch at 50 km, once the sky copies the body's look across.

- [ ] **Step 7: Author, rebuild the level, verify, test, render**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && \
python3 Tools/test_placement.py && \
rm -f Saved/setup_sky_materials.txt Saved/hauler_build.txt Saved/verify_level.txt && \
. Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" -unattended -nopause -nosplash -NoLiveCoding && \
tail -3 Saved/setup_sky_materials.txt && tail -n 1 Saved/setup_sky_materials.txt | grep -qx ok && \
ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/build_hauler.py" -unattended -nopause -nosplash -NoLiveCoding && \
tail -3 Saved/hauler_build.txt && grep -q "^L_Hauler built:" Saved/hauler_build.txt && \
ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/verify_level.py" -unattended -nopause -nosplash -NoLiveCoding && \
tail -5 Saved/verify_level.txt && grep -q "^PASS:" Saved/verify_level.txt && \
./test.sh DeepSpace.Sky && ./test.sh DeepSpace.Surface && Tools/eyes.sh Eyes.WorldReliefParity
```

Every step is joined with `&&`, the three reports are removed first so a stale one cannot pass, and
each commandlet's own report must say it finished: `ok` as the authoring report's last line,
`L_Hauler built:` (written only after `save_current_level`), and `PASS:` (`verify_level.py` also
exits 1 on any failure). A failed authoring or build therefore never reaches the tests, and Step 8
never commits a stale `L_Hauler.umap`.

Expected: all three reports say they finished; `verify_level.txt` shows no `ground` failure and
still no extra light, sky light, atmosphere or fog; `DeepSpace.Sky.*` and `DeepSpace.Surface.*`
green (the ground actor test now loads `M_SkyGround`); the parity run passes, the ground probe
case included -- a grey material would fail it.

- [ ] **Step 8: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
git add Shaders/Private/WorldRelief.ush Source/DeepSpace/Surface/WorldRelief.h Source/DeepSpace/Surface/WorldRelief.cpp \
        Source/DeepSpace/Surface/WorldGround.cpp Source/DeepSpace/Sky/SkyMaterialContract.h Tools/sky_material_contract.json \
        Tools/setup_sky_materials.py Tools/build_hauler.py Tools/verify_level.py Tools/placement.py Tools/test_placement.py \
        Source/DeepSpace/Tests/GroundShadingTest.cpp Source/DeepSpace/Tests/SkyMaterialContractTest.cpp \
        Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp Content/Materials/Sky Content/Maps/L_Hauler.umap && \
git commit -m "$(cat <<'EOF'
feat(terrain): M_SkyGround -- the ground shades as the orbit does; hauler_ground in the level

The tile's vertex normal carries the bands its spacing resolves and the
pixel adds only the rest, so on tiles finer than their pixels the normal
is the orbit's band for band (DeepSpace.Surface.GroundShadesAsOrbit) and
the 50 km handover has no brightness step. World Position Offset grows
the relief in by the morph. Contract on three sides; the level rebuilt
and verified; the GPU's ground probe held to the C++ (landing decision 9).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 9: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Shaders/Private/WorldRelief.ush 'const WR_REAL NX = DX - CarriedX - (Pixel.SX - PD * DX);' 'const WR_REAL NX = DX - (Pixel.SX - PD * DX);' DeepSpace.Surface.GroundShadesAsOrbit && \
./build.sh
```

Expected: `KILLED` (a normal that drops what the vertices carry is not the orbit's). Then the
index, a silent failure in play:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Source/DeepSpace/Sky/SkyMaterialContract.h 'inline constexpr int32 TilePivotPrimitiveIndex = 1;' 'inline constexpr int32 TilePivotPrimitiveIndex = 2;' DeepSpace.Sky.MaterialContract && \
./build.sh
```

Expected: `KILLED` (the header's index no longer matches the asset's, which the JSON authored).

---

## Task 38 (T8): the handover -- the sky gives the body to the ground, the look copied across, the depth stack kept

**Owner:** T. **Depends on:** T6, T7, and S4 (`SetVerticalLever`, for `GroundKeepsUp`'s flight:
`git merge -q feat/landing-b` after S4 is merged into it). Spec decision 7 (*The switch waits for
the ground*, *The depth stack is unchanged*, *The flight's floor and the drawn floor split*),
decision 6's residency under motion, decision 9's rendered handover. No generated actor's
components change here (`hauler_sky` keeps its components; only `ShipSky`'s code moves), so the
level is not rebuilt.

**Files:**
- Modify: `Source/DeepSpace/Sky/ShipSky.h` (`ShipSky::CopyBodyLook`), `ShipSky.cpp` (`DrawBodies`)
- Create: `Source/DeepSpace/Tests/HandoverTest.cpp` (`DeepSpace.Sky.Handover`,
  `DeepSpace.Sky.ProjectionAtGround`)
- Create: `Source/DeepSpace/Tests/GroundKeepsUpTest.cpp` (`DeepSpace.Surface.GroundKeepsUp`: the
  done-when's residency with the ship moving and the builds asynchronous)
- Create: `Source/DeepSpace/Tests/Eyes/HandoverParityEyesTest.cpp` (`Eyes.HandoverParity`: the real
  `M_SkyGround` and `M_SkyBody` rendered over the same patch at 50 km)
- Modify: `Source/DeepSpace/Tests/SkyBodyMeshTest.cpp` (its premise's comment only),
  `Tools/sky_probe.py` (the floor it tables over solid worlds)
- Modify: `CLAUDE.md` -- *The sky* (the handover paragraph), `docs/superpowers/specs/2026-09-26-sky-design.md`
  is not in this repo's specs list under that name: add the note to whichever file
  `ls docs/superpowers/specs | grep -i sky` names

**Interfaces:**
- Consumes: `AWorldGround::{GetDrawnBody, GetGroundMaterialInstance, IsDrawingBody, GetMorph,
  DrawnHeightUnderShip, GetDrawnKeys, GetTileComponent, FlushBuildsForTest}`;
  `bool UShipSubsystem::SetVerticalLever(APawn*, double)` (S4), `bool SetFlightCommand(APawn*, float, const FVector&)`,
  `static double UShipSubsystem::GearClearance()` (S1), `static double FloorFor(const FSkyBody&)`;
  `FShipFlightState::{GetGroundAltitude, GetVelocity, GetHold}`; `ShipFlight::SkimCap` (F2);
  `SkyTestWorld::{FSkyWorld, FScopedCVar}`; `SkyMaterial::GroundPath`.
- Produces: `static void ShipSky::CopyBodyLook(UMaterialInstanceDynamic& From, UMaterialInstanceDynamic& To)`;
  the tests `DeepSpace.Surface.GroundKeepsUp` and `Eyes.HandoverParity`.

- [ ] **Step 1: Write the failing tests**

Create `Source/DeepSpace/Tests/HandoverTest.cpp`:

```cpp
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Surface/WorldGround.h"
#include "Tests/SkyTestFixtures.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 7: at 50 km the projection's magnification is exactly 1,
 * so the proxy is the true sphere; below it over a solid world the ground
 * draws the body and the proxy is hidden -- once the coarse cut is resident
 * above the drive floor, always under it -- and the body's look is the
 * ground's too. The depth stack is unchanged: the hidden proxy keeps its
 * rendered floor, so every other body still starts beyond it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkyHandoverTest, "DeepSpace.Sky.Handover",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkyProjectionAtGroundTest, "DeepSpace.Sky.ProjectionAtGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkyHandoverTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("HandoverWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const int32 Index = 4;
    const FSkyBody& Fourth = Here.Bodies[Index];
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const auto Over = [&](double DatumAltitude)
    {
        Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + DatumAltitude), FRotationMatrix::MakeFromX(-Out).ToQuat());
        Test.Step(1.0f / 60.0f);
    };
    const auto ProxyShown = [&]() { return Test.Sky->GetProxy(Index) && Test.Sky->GetProxy(Index)->IsVisible(); };

    Over(6.0e6);
    TestTrue(TEXT("60 km up the proxy draws Baemsekai IV"), ProxyShown() && !Test.Ground->IsDrawingBody());

    Over(4.5e6);
    TestTrue(TEXT("at 45 km, before the coarse cut is resident, the proxy still stands in"), ProxyShown());
    Test.Ground->FlushBuildsForTest();
    Test.Step(1.0f / 60.0f);
    TestTrue(TEXT("once it is, the ground takes the body and the proxy is hidden"), Test.Ground->IsDrawingBody() && !ProxyShown());
    TestTrue(TEXT("the morph has begun, and is not whole"), Test.Ground->GetMorph() > 0.0 && Test.Ground->GetMorph() < 1.0);

    UMaterialInstanceDynamic* Proxy = Cast<UMaterialInstanceDynamic>(Test.Sky->GetProxy(Index)->GetMaterial(0));
    UMaterialInstanceDynamic* Ground = Test.Ground->GetGroundMaterialInstance();
    TestTrue(TEXT("the ground wears the body's own look: the same light, colour, seed, brightness and relief"),
             Proxy && Ground
             && Proxy->K2_GetVectorParameterValue(SkyMaterial::LightDirection) == Ground->K2_GetVectorParameterValue(SkyMaterial::LightDirection)
             && Proxy->K2_GetVectorParameterValue(SkyMaterial::SurfaceSeed) == Ground->K2_GetVectorParameterValue(SkyMaterial::SurfaceSeed)
             && Proxy->K2_GetScalarParameterValue(SkyMaterial::Brightness) == Ground->K2_GetScalarParameterValue(SkyMaterial::Brightness)
             && Proxy->K2_GetScalarParameterValue(SkyMaterial::ReliefScale) == Ground->K2_GetScalarParameterValue(SkyMaterial::ReliefScale));

    Over(5.3e6);
    TestTrue(TEXT("at 53 km, inside the hysteresis, the ground keeps it"), Test.Ground->IsDrawingBody() && !ProxyShown());
    Over(5.6e6);
    TestTrue(TEXT("over 55 km it gives it back"), !Test.Ground->IsDrawingBody() && ProxyShown());

    // Under the drive floor the proxy is never drawn, resident or not.
    Over(UShipSubsystem::FloorFor(Fourth) - 5.0e5);
    TestTrue(TEXT("under the drive floor, the proxy is never drawn over a solid world"), Test.Ground->IsDrawingBody() && !ProxyShown());
    TestEqual(TEXT("and the relief is whole"), Test.Ground->GetMorph(), 1.0);

    // ds.Sky.Goto low: a placement under the ground is lifted by the ground's hard stop.
    {
        FOutputDeviceNull Log;
        const TArray<FString> Args = { TEXT("4"), TEXT("0.001") };
        AShipSky::Goto(*Ship, LocalSystem::Current(Test.World), false, Args, Log);
        for (int32 Frame = 0; Frame < 2; ++Frame)
        {
            Test.Step(1.0f / 60.0f);
        }
        TestTrue(TEXT("ds.Sky.Goto 4 0.001 ends above the ground, lifted by the hard stop"),
                 Ship->GetFlightState().GetFootprintClearance().Get(-1.0e9) >= -1.0);
    }
    return true;
}

bool FSkyProjectionAtGroundTest::RunTest(const FString& Parameters)
{
    // 1.5 m over the fixture home world: the hidden proxy keeps its clamp, so
    // its far side, and every other body beyond it, stays inside FarProxy --
    // lifting the clamp would put its centre at NearProxy / NearFactor, 2e8 km.
    const FSkySystem System = SkyTestFixtures::System();
    const FSkyBody& Home = System.Bodies[SkyTestFixtures::HomeIndex];
    const FVector Up = FVector(0.3, -0.2, 0.93).GetSafeNormal();
    const FSkyViewParams Params;
    const FSkyFrame Frame = SkyProjection::Project(System, Home.Position + Up * (Home.Radius + 150.0), Params);
    bool bInside = true;
    for (const FSkyBodyView& View : Frame.Bodies)
    {
        bInside &= View.ProxyLocation.Size() + View.ProxyRadius <= Params.FarProxy * (1.0 + 1e-9);
    }
    TestTrue(TEXT("at 1.5 m every body's proxy, the star's included, stays within FarProxy"), bInside);
    const FSkyBodyView& Near = Frame.Bodies[SkyTestFixtures::HomeIndex];
    TestTrue(TEXT("and the hidden proxy's near side is the rendered floor's, not the ship's height"),
             Near.ProxyLocation.Size() - Near.ProxyRadius >= Params.NearProxy * (1.0 - 1e-9));
    return true;
}

#endif
```

(add `#include "Misc/OutputDeviceNull.h"`.) Run `./build.sh && ./test.sh DeepSpace.Sky.Handover`:
FAIL on `the ground takes the body and the proxy is hidden` (the sky still draws its proxy).
`ProjectionAtGround` passes already -- the projection is not changed, and this test holds it so.

- [ ] **Step 1b: The residency test, with the ship moving**

Every other ground test places the ship and flushes the builds. The done-when asks for the
opposite: under the drive floor the proxy is never drawn, and under 1 km the mesh under the ship
stays within 15 cm of the analytic ground, *at the full sink and at the skim cap's top*, with the
workers building as they do in play. Create `Source/DeepSpace/Tests/GroundKeepsUpTest.cpp`:

```cpp
#include "GameFramework/Pawn.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Surface/GroundField.h"
#include "Surface/WorldGround.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Slice (b)'s done-when with the ship moving (landing decision 6: residency
 * never gates motion; the skim cap and the prefetch keep the ground ahead
 * of the ship). Over Baemsekai IV: from the drive floor at the full sink,
 * then cruising at the skim cap's top at 500 m and at 50 m. The builds run
 * on ds.Terrain.BuildTasks workers, uploaded ds.Terrain.UploadsPerFrame a
 * frame, never flushed; each frame is paced to the wall clock, as play is,
 * so the workers get the time they get in play -- which makes this the
 * slowest test in the suite, about two minutes, by design. Every frame
 * under the drive floor the ground draws the body and the proxy is hidden;
 * every frame under 1 km the drawn ground under the ship is within
 * GearClearance / 10 of the analytic ground.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundKeepsUpTest, "DeepSpace.Surface.GroundKeepsUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroundKeepsUpTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FScopedCVar Tasks(TEXT("ds.Terrain.BuildTasks"), 2.0f);
    FScopedCVar Uploads(TEXT("ds.Terrain.UploadsPerFrame"), 4.0f);
    FSkyWorld Test(TEXT("GroundKeepsUpWorld"));
    if (!TestNotNull(TEXT("the ground spawns before play"), Test.Ground))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    const FShipFlightState& Flight = Ship->GetFlightState();
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const int32 Index = 4;
    const FSkyBody& Fourth = Here.Bodies[Index];
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    const FVector Out = (Flight.GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const FVector Heading = FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal();
    const double DriveFloor = UShipSubsystem::FloorFor(Fourth);
    const double Tolerance = UShipSubsystem::GearClearance() / 10.0;
    constexpr float Dt = 1.0f / 60.0f;

    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    // At the drive floor, level, over the ship's own side of the world.
    Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + DriveFloor - 100.0), FRotationMatrix::MakeFromXZ(Heading, Out).ToQuat());

    int32 UnderFloor = 0;
    int32 ProxyShown = 0;
    int32 NotDrawing = 0;
    int32 Low = 0;
    int32 Missing = 0;
    double Worst = 0.0;
    // One frame as play has it: the wall clock paced to Dt, so the workers
    // build what they would in a real frame.
    const auto Frame = [&]()
    {
        const double Began = FPlatformTime::Seconds();
        Test.Step(Dt);
        while (FPlatformTime::Seconds() - Began < Dt)
        {
            FPlatformProcess::Sleep(0.001f);
        }
        const FVector Up = (Flight.GetUniversePosition() - Fourth.Position).GetSafeNormal();
        if (Flight.GetUniversePosition().DistanceTo(Fourth.Position) - Fourth.Radius < DriveFloor)
        {
            ++UnderFloor;
            const UStaticMeshComponent* Proxy = Test.Sky->GetProxy(Index);
            ProxyShown += Proxy && Proxy->IsVisible() ? 1 : 0;
            NotDrawing += Test.Ground->IsDrawingBody() ? 0 : 1;
        }
        const TOptional<double> Agl = Flight.GetGroundAltitude();
        if (Agl && *Agl < 1.0e5)
        {
            ++Low;
            const TOptional<double> Drawn = Test.Ground->DrawnHeightUnderShip();
            if (!Drawn)
            {
                ++Missing;
            }
            else
            {
                Worst = FMath::Max(Worst, FMath::Abs(*Drawn - Field->Height(FVector3d(Up), 0.0)));
            }
        }
    };
    const auto Agl = [&]() { return Flight.GetGroundAltitude().Get(TNumericLimits<double>::Max()); };

    // Two seconds' hover at the floor: a placement is a teleport, not
    // motion, and the coarse cut comes first.
    for (int32 Tick = 0; Tick < 120; ++Tick)
    {
        Frame();
    }
    // The full sink, from the drive floor to 500 m.
    Ship->SetVerticalLever(Pilot, -1.0);
    double Fastest = 0.0;
    for (double Seconds = 0.0; Seconds < 200.0 && Agl() > 5.0e4; Seconds += Dt)
    {
        Frame();
        Fastest = FMath::Max(Fastest, -Flight.GetVerticalSpeed());
    }
    TestTrue(FString::Printf(TEXT("the descent reached the full sink, 200 m/s (%.1f m/s)"), Fastest / 100.0), Fastest >= 0.99 * 2.0e4);

    // The skim cap's top at 500 m, then at 50 m: HOVER, cruise full ahead.
    for (const double Height : { 5.0e4, 5.0e3 })
    {
        Ship->SetVerticalLever(Pilot, -1.0);
        for (double Seconds = 0.0; Seconds < 60.0 && Agl() > Height; Seconds += Dt)
        {
            Frame();
        }
        Ship->SetVerticalLever(Pilot, 0.0);
        for (int32 Tick = 0; Tick < 5 * 60; ++Tick)
        {
            Frame();
        }
        Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
        double Top = 0.0;
        bool bHeldOff = false;
        for (int32 Tick = 0; Tick < 10 * 60; ++Tick)
        {
            Frame();
            const FVector Up = (Flight.GetUniversePosition() - Fourth.Position).GetSafeNormal();
            const FVector Along = Flight.GetVelocity() - Up * FVector::DotProduct(Flight.GetVelocity(), Up);
            Top = FMath::Max(Top, Along.Size() / ShipFlight::SkimCap(Agl(), ShipFlight::DefaultSkimSeconds, ShipFlight::DefaultSkimFloor));
            bHeldOff |= Flight.GetHold() != EFlightHold::Free && Top < 0.9;
        }
        Ship->SetFlightCommand(Pilot, 0.0f, FVector::ZeroVector);
        AddInfo(FString::Printf(TEXT("at %.0f m: cruise reached %.2f of the skim cap%s"), Height / 100.0, Top, bHeldOff ? TEXT(", held off a ridge") : TEXT("")));
        TestTrue(FString::Printf(TEXT("at %.0f m the ship flew at the skim cap's top, or a ridge held it off (%.2f)"), Height / 100.0, Top),
                 Top >= 0.9 || bHeldOff);
    }

    AddInfo(FString::Printf(TEXT("%d frames under the drive floor, %d under 1 km; worst drawn gap %.2f cm"), UnderFloor, Low, Worst));
    TestTrue(TEXT("the flight spent frames under the floor and under 1 km"), UnderFloor > 1000 && Low > 1000);
    TestEqual(TEXT("under the drive floor the proxy is never drawn"), ProxyShown, 0);
    TestEqual(TEXT("and the ground draws the body every frame"), NotDrawing, 0);
    TestEqual(TEXT("under 1 km there is always drawn ground under the ship"), Missing, 0);
    TestTrue(FString::Printf(TEXT("and it is within GearClearance / 10 of the analytic ground, moving (worst %.2f cm)"), Worst),
             Worst <= Tolerance);
    return true;
}

#endif
```

Run `./build.sh && ./test.sh DeepSpace.Surface.GroundKeepsUp`. Expected: FAIL at `under the drive
floor the proxy is never drawn` (the sky still draws its proxy until Step 2); the residency
assertions report their numbers already.

- [ ] **Step 1c: The rendered handover, the real materials**

`Eyes.WorldReliefParity`'s ground case holds `M_SkyGroundProbe`'s normal to the C++, and
`GroundShadesAsOrbit` holds the two sides' numbers equal on the CPU; neither draws the shipped
`M_SkyGround` graph -- its face composition, `Brightness`, the 1.5 disc gain, the terminator -- so
a Custom-node error or a miswired parameter there would ship the engine's grey material with every
guard green. This case draws both real materials over the same patch from just under 50 km and
compares them (decision 9). Create `Source/DeepSpace/Tests/Eyes/HandoverParityEyesTest.cpp`:

```cpp
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"
#include "Surface/WorldGround.h"
#include "TextureResource.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Landing decision 9's rendered case: from 49.9 km over Baemsekai IV, looking
 * straight down, the frame the ground draws (M_SkyGround on the tiles, the
 * proxy hidden) against the same frame the sky draws (M_SkyBody on the
 * proxy, the ground hidden), untonemapped. Both are unlit, so the scene
 * colour is each material's emissive. The mean over the central quarter
 * must agree to 1e-3 of itself -- the handover's "no brightness step" -- and
 * the ground must not be the engine's default material. The per-pixel
 * 99th percentile is reported: a sub-pixel difference in where the two
 * meshes put a pixel moves single pixels, never the mean.
 *
 *   Tools/eyes.sh Eyes.HandoverParity
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandoverParityEyesTest, "Eyes.HandoverParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHandoverParityEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("HandoverParityWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const int32 Index = 4;
    const FSkyBody& Fourth = Here.Bodies[Index];
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    // Just under the handover: the morph has barely begun, so both sides
    // draw the same sphere, and the sky has copied the body's look across.
    Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + 4.99e6), FRotationMatrix::MakeFromX(-Out).ToQuat());
    Test.Step(1.0f / 60.0f);
    Test.Ground->FlushBuildsForTest();
    Test.Step(1.0f / 60.0f);
    UStaticMeshComponent* Proxy = Test.Sky->GetProxy(Index);
    if (!TestTrue(TEXT("the ground has the body, the proxy hidden"), Test.Ground->IsDrawingBody() && Proxy && !Proxy->IsVisible()))
    {
        return false;
    }
    const TArray<FTileKey> Keys = Test.Ground->GetDrawnKeys();
    const UPrimitiveComponent* AnyTile = Keys.Num() > 0 ? Test.Ground->GetTileComponent(Keys[0]) : nullptr;
    const UMaterialInterface* Worn = AnyTile ? AnyTile->GetMaterial(0) : nullptr;
    TestTrue(TEXT("the tiles wear M_SkyGround, not the engine's default"),
             Worn && Worn->GetMaterial() && Worn->GetMaterial()->GetPathName() == FString(SkyMaterial::GroundPath));

    constexpr int32 Size = 512;
    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Test.Sky);
    Capture->RegisterComponent();
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Test.Sky);
    Target->InitCustomFormat(Size, Size, PF_FloatRGBA, true);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->FOVAngle = 20.0f;
    Capture->CaptureSource = SCS_SceneColorHDR;
    // One exposure for both frames, whatever the sky's volume says.
    Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Capture->PostProcessSettings.AutoExposureMethod = AEM_Manual;
    Capture->PostProcessBlendWeight = 1.0f;
    // At the ship, looking at the world's centre: straight down.
    const FVector Down = Ship->UniverseToWorld(Fourth.Position).GetSafeNormal();
    Capture->SetWorldLocationAndRotation(FVector::ZeroVector, Down.Rotation());

    const auto Shoot = [&](TArray<FLinearColor>& Pixels)
    {
        for (int32 Warm = 0; Warm < 3; ++Warm)
        {
            Capture->CaptureScene();
            FlushRenderingCommands();
        }
        Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
    };
    TArray<FLinearColor> Ground;
    Shoot(Ground);
    // The same scene with the sky's proxy instead. Nothing steps between
    // the two, so nothing else moves.
    Test.Ground->SetActorHiddenInGame(true);
    Proxy->SetVisibility(true);
    TArray<FLinearColor> Orbit;
    Shoot(Orbit);

    double SumGround = 0.0;
    double SumOrbit = 0.0;
    TArray<double> Gaps;
    for (int32 Y = Size / 4; Y < 3 * Size / 4; ++Y)
    {
        for (int32 X = Size / 4; X < 3 * Size / 4; ++X)
        {
            const FLinearColor& A = Ground[Y * Size + X];
            const FLinearColor& B = Orbit[Y * Size + X];
            const double LumA = (A.R + A.G + A.B) / 3.0;
            const double LumB = (B.R + B.G + B.B) / 3.0;
            SumGround += LumA;
            SumOrbit += LumB;
            Gaps.Add(FMath::Abs(LumA - LumB));
        }
    }
    const double MeanGround = SumGround / Gaps.Num();
    const double MeanOrbit = SumOrbit / Gaps.Num();
    Gaps.Sort();
    const double P99 = Gaps[FMath::FloorToInt32(0.99 * (Gaps.Num() - 1))];
    const FString Line = FString::Printf(TEXT("49.9 km over Baemsekai IV: ground mean %.6f, orbit mean %.6f, relative gap %.2e; per-pixel p99 %.2e"),
                                         MeanGround, MeanOrbit, FMath::Abs(MeanGround - MeanOrbit) / FMath::Max(MeanOrbit, 1e-12), P99);
    AddInfo(Line);
    TestTrue(TEXT("the orbit's frame is lit"), MeanOrbit > 0.0);
    TestTrue(FString::Printf(TEXT("the ground's frame is the orbit's to 1e-3 of it: no brightness step at the handover (%s)"), *Line),
             FMath::Abs(MeanGround - MeanOrbit) <= 1.0e-3 * MeanOrbit);
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/HandoverParity");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(Line + TEXT("\n"), *(Dir / TEXT("report.txt")));
    return true;
}

#endif
```

Run `./build.sh && Tools/eyes.sh Eyes.HandoverParity`. Expected: FAIL at `the ground has the body,
the proxy hidden` (the sky still draws the proxy until Step 2).

- [ ] **Step 2: The sky hands the body over**

`ShipSky.h`, in the `ShipSky` namespace:

```cpp
    /** The body's look, copied from its proxy's instance into the ground's:
     *  the light, colour and seed, and the brightness, mottle, detail, relief
     *  and craters -- one writer of the look, so the handover can have no
     *  step in it (landing decision 9). */
    DEEPSPACE_API void CopyBodyLook(UMaterialInstanceDynamic& From, UMaterialInstanceDynamic& To);
```

`ShipSky.cpp` (`#include "Surface/WorldGround.h"`, `"EngineUtils.h"`):

```cpp
void ShipSky::CopyBodyLook(UMaterialInstanceDynamic& From, UMaterialInstanceDynamic& To)
{
    for (const FName Name : { SkyMaterial::Colour, SkyMaterial::LightDirection, SkyMaterial::SurfaceSeed })
    {
        To.SetVectorParameterValue(Name, From.K2_GetVectorParameterValue(Name));
    }
    for (const FName Name : { SkyMaterial::Brightness, SkyMaterial::Mottle, SkyMaterial::Detail, SkyMaterial::ReliefScale, SkyMaterial::Cratering })
    {
        To.SetScalarParameterValue(Name, From.K2_GetScalarParameterValue(Name));
    }
}
```

In `DrawBodies`, before the loop:

```cpp
    // The ground below 50 km over a solid world (landing decision 7): it
    // says which body it draws; that proxy is hidden, and its look copied
    // into the ground's material. The projection still computes the hidden
    // proxy, with its rendered floor, so the depth stack is unchanged.
    AWorldGround* Ground = nullptr;
    for (TActorIterator<AWorldGround> It(GetWorld()); It && !Ground; ++It)
    {
        Ground = *It;
    }
    const FName GroundBody = Ground ? Ground->GetDrawnBody() : NAME_None;
```

and in the loop, after the proxy's transform is set and before the instance writes:

```cpp
        const bool bGroundHasIt = GroundBody != NAME_None && System.Bodies[Index].Id == GroundBody;
        if (Proxy->IsVisible() == bGroundHasIt)
        {
            Proxy->SetVisibility(!bGroundHasIt);
        }
```

and at the end of the loop body, after the proxy's own parameter writes:

```cpp
        if (bGroundHasIt && Instance && Ground->GetGroundMaterialInstance())
        {
            ShipSky::CopyBodyLook(*Instance, *Ground->GetGroundMaterialInstance());
        }
```

(`SyncTo` returns before `DrawFrom` in transit, and the ground releases the body in transit, so
this never fights `SetSkyVisible(false)`.)

- [ ] **Step 3: The premises that move**

`SkyBodyMeshTest.cpp`: on the comment that sizes LOD 0 at the rendered floor, 1.6e-3 R, add:
`// Solid worlds are drawn by the ground under 50 km (landing decision 7); LOD 0 at the rendered
floor is still what oceans, giants and every world above 50 km need.` (no number changes).
`Tools/sky_probe.py`: where it tables the floor as `max(10 km, 1.6e-3 R)`, add a note line in its
docstring: `-- the drive's floor over a solid world is that above its highest peak (landing
decision 10); this table flies the drive to it over a flat datum, which is where the rendered
floor still sets the proxy.` Run `python3 Tools/sky_probe.py | head -5` to see it still runs.

- [ ] **Step 4: Run: PASS**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git merge -q feat/landing-b && ./build.sh && \
./test.sh DeepSpace.Sky && ./test.sh DeepSpace.Surface && Tools/eyes.sh Eyes.HandoverParity; cat Saved/Eyes/HandoverParity/report.txt
```

Expected: all green, `Handover`, `ProjectionAtGround` and `GroundKeepsUp` included (its `AddInfo`
lines give the frames under the floor and under 1 km, the worst drawn gap, and each skim leg's
share of the cap); `Eyes.HandoverParity` passes with the relative gap under 1e-3 and reports the
per-pixel 99th percentile. A `GroundKeepsUp` failure on the drawn gap or on missing ground is a
residency bug in `AWorldGround` (the prefetch, the cut's refresh, the upload cap), fixed there, never
by loosening the test; if it is the workers' speed, that is the spec's *Tile build cost* risk --
`ds.Land.SkimSeconds` rises, a feel change for the developer, never a gate on motion.

- [ ] **Step 5: Document**

`CLAUDE.md`, *The sky*, after **Distant bodies are projected, never placed.**'s paragraph, add:

```markdown
**Below 50 km over a solid world the ground draws the body** (landing
decision 7). At 50 km the projection's magnification is exactly 1 -- the
proxy is the true sphere at its true place -- so the ground (`AWorldGround`,
*The ground*) takes the body there once its coarse cut is resident, gives it
back over 55 km, and always has it under the drive floor, where the proxy is
never drawn. The relief grows in: every tile's heights are scaled by one
morph fraction, 0 at 50 km to 1 at the drive floor, while the flight always
has the whole relief. The sky still projects the hidden proxy with its
rendered floor, so the depth stack is unchanged, and it copies that body's
look into the ground's material (`ShipSky::CopyBodyLook`), so the handover
has no brightness step.
```

In the sky spec (`ls docs/superpowers/specs | grep -i sky`), at its non-goal that says landing
"replaces the proxy with real terrain in the ship's frame", append: `-- as built in landing slice
(b): below 50 km over a solid world, with the relief grown in between 50 km and the drive floor.`

- [ ] **Step 6: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
git add Source/DeepSpace/Sky/ShipSky.h Source/DeepSpace/Sky/ShipSky.cpp Source/DeepSpace/Tests/HandoverTest.cpp \
        Source/DeepSpace/Tests/GroundKeepsUpTest.cpp Source/DeepSpace/Tests/Eyes/HandoverParityEyesTest.cpp \
        Source/DeepSpace/Tests/SkyBodyMeshTest.cpp Tools/sky_probe.py CLAUDE.md docs/superpowers/specs && \
git commit -m "$(cat <<'EOF'
feat(sky): the handover -- below 50 km over a solid world the ground draws the body, in its look

The sky hides the proxy of the body the ground claims -- under 50 km once
the coarse cut is resident, over 55 km given back, always under the
drive floor -- and copies that body's look into the ground's instance.
The projection is unchanged: the hidden proxy keeps its rendered floor
and the stack behind it (landing decision 7).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7: Prove it**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'Proxy->SetVisibility(!bGroundHasIt);' 'Proxy->SetVisibility(true);' DeepSpace.Sky.Handover && \
Tools/mutate.sh Source/DeepSpace/Surface/WorldGround.cpp '    else if (NearAltitude < DriveFloorAltitude)
    {
        bDrawsBody = true;
    }' '' DeepSpace.Sky.Handover && \
Tools/mutate.sh Source/DeepSpace/Surface/WorldGround.cpp 'Moved < 0.05 * FMath::Max(GroundAltitudeCm, 100.0)' 'Moved < 1.0e12' DeepSpace.Surface.GroundKeepsUp && \
MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp '{ SkyMaterial::Brightness, SkyMaterial::Mottle,' '{ SkyMaterial::Mottle,' Eyes.HandoverParity && \
./build.sh
```

Expected: `KILLED` four times (the third: a cut never refreshed while the ship moves leaves coarse
tiles under it, off by metres; the fourth: a ground not given the body's brightness draws a step
at the handover).

---

## Task 39 (Z): slice (b) done -- the frame at 4K, the flight the developer makes, the documentation, the merge

**Owner:** orchestrator, with the developer for the eyes. **Depends on:** every task above
merged into `feat/landing-b` (F, then T and S; conflicts in `CLAUDE.md` are resolved by keeping
every task's paragraph).

**Files:**
- Create: `Source/DeepSpace/Tests/Eyes/LandingFrameEyesTest.cpp` (`Eyes.LandingFrame`)
- Modify: `CLAUDE.md` (a *Landing* section), `docs/superpowers/specs/2026-09-27-landing-design.md`
  (a *Built, slice (b)* note under *The three slices*)

- [ ] **Step 1: The frame at 4K, measured**

Create `Source/DeepSpace/Tests/Eyes/LandingFrameEyesTest.cpp`:

```cpp
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Surface/WorldGround.h"
#include "TextureResource.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Slice (b)'s done-when, the part a test can measure: a 4K capture from the
 * helm's eye with the real sky and the real ground, at 50 km (the handover)
 * and 1.5 m over Baemsekai IV, render thread and GPU together, and the
 * tile counts at each. Not the game's frame -- `stat unit` in play is, which
 * the developer reads -- but the same scene, measured the same way every
 * time. Asserts the 16.6 ms budget.
 *
 *   Tools/eyes.sh Eyes.LandingFrame
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingFrameEyesTest, "Eyes.LandingFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandingFrameEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("LandingFrameWorld"), 3000);
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Test.Sky);
    Capture->RegisterComponent();
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Test.Sky);
    Target->InitCustomFormat(3840, 2160, PF_B8G8R8A8, false);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->FOVAngle = 90.0f;
    Capture->CaptureSource = SCS_FinalColorLDR;

    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody& Fourth = Here.Bodies[4];
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    const FVector Up = (Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const FVector Heading = FVector::CrossProduct(Up, FVector(0.3, 0.9, 0.1)).GetSafeNormal();
    FString Report;
    for (const double Agl : { 5.0e6, 150.0 })
    {
        Ship->PlaceShip(Fourth.Position + Up * (Fourth.Radius + Field->Height(FVector3d(Up), 0.0) + Agl),
                        FRotationMatrix::MakeFromXZ(Heading, Up).ToQuat());
        Test.Step(1.0f / 60.0f);
        Test.Ground->FlushBuildsForTest();
        Test.Step(1.0f / 60.0f);
        // Looking out and a little down through the cockpit glass, as a pilot would.
        Capture->SetWorldLocationAndRotation(PilotEye, FRotator(-15.0, 0.0, 0.0));
        TArray<FColor> Pixel;
        for (int32 Warm = 0; Warm < 3; ++Warm)
        {
            Capture->CaptureScene();
            FlushRenderingCommands();
        }
        const double Start = FPlatformTime::Seconds();
        for (int32 Shot = 0; Shot < 20; ++Shot)
        {
            Test.Step(1.0f / 60.0f);
            Capture->CaptureScene();
            FlushRenderingCommands();
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(0, 0, 1, 1));
        }
        const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0 / 20.0;
        const FString Line = FString::Printf(TEXT("%s: %.2f ms a frame (game step, render and GPU), %d tiles drawn\n%s"),
            Agl > 1.0e5 ? TEXT("50 km") : TEXT("1.5 m"), Ms, Test.Ground->GetDrawnKeys().Num(), *Test.Ground->Describe());
        Report += Line;
        AddInfo(Line);
        TestTrue(FString::Printf(TEXT("the frame fits 16.6 ms at %s (%.2f)"), Agl > 1.0e5 ? TEXT("50 km") : TEXT("1.5 m"), Ms), Ms <= 16.6);
    }
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/LandingFrame");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(Report, *(Dir / TEXT("report.txt")));
    return true;
}

#endif
```

Run it on the merged tree:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git merge -q feat/landing-b && ./build.sh && \
Tools/eyes.sh Eyes.LandingFrame; cat Saved/Eyes/LandingFrame/report.txt
```

Expected: two lines within 16.6 ms. If either is over, the budgets are revised before anything
else (spec, *Done when*): lower `ds.Terrain.MaxTiles` or `ds.Terrain.SplitFactor` in play until
it fits, write the settled value back as the default in `WorldGround.cpp` with the measured
numbers in the commit, and re-run.

- [ ] **Step 2: Everything green, every guard run**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./test.sh && \
Tools/eyes.sh Eyes.WorldReliefParity && Tools/eyes.sh Eyes.HandoverParity && Tools/eyes.sh Eyes.TerrainBudget && \
python3 Tools/validate_hauler.py && \
. Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/check_blueprints.py" -unattended -nopause -nosplash -NoLiveCoding; echo "blueprints exit $?"
```

Expected: the whole suite green (at least B0's N plus this slice's tests, `GroundKeepsUp` among
them), the three rendered guards (`WorldReliefParity`, `HandoverParity`, the gate) green, the layout valid, `blueprints exit 0`.

- [ ] **Step 3: The developer flies it (the done-when that is not a test)**

Hand the developer, in one message, the checklist from the spec's *Done when* for slice (b), with
the console lines to get there. The spec says "from its drive floor ... through the handover", but
the handover is at 50 km and Baemsekai IV's drive floor is under 20 km, so a leg that starts at the
floor never crosses it. The handover leg therefore starts above the 55 km hand-back:

1. `ds.Sky.Goto 4 60` (60 km over Baemsekai IV, facing it), sit at the helm, one Shift (the drive
   at 1 km/s; the vertical lever is inert up here): the soft cap brings the ship down through the
   handover at 50 km to the drive floor. No hole, no flicker, no brightness step, no jump at 50 km;
   the mountains rise under the ship as it descends. At the floor, F (cruise's lever live) and C
   held from the floor to the ground.
2. Under 1 km, at the full sink and then cruising at the skim cap's top (full cruise at 500 m and
   at 50 m): the ground under the ship never goes coarse or pops, and never shows sky
   (`DeepSpace.Surface.GroundKeepsUp` measures the same flight headless).
3. It comes to hover over real ground in about `(H - 800 m) / 200 m/s + 28 s`; the corner reads
   `1.5 M ABOVE GROUND · HOVERING` over flat ground with a foot down. (For this item and the next,
   `ds.Sky.Goto 4 30` is a shorter start: 30 km is under the handover, above the floor.)
4. Standing up leaves it hovering; Space climbs, at about 100 m/s over Baemsekai III
   (`ds.Sky.Goto 3 30`); a booster weight of 0 sinks it gently to the ground.
5. `stat unit` at 50 km and at the ground within 16.6 ms at 4K with the project's
   anti-aliasing and upscaler; game, render-thread and GPU times read separately.
6. The before/after frames of the orbit (Task T2) and the largest LOD pop in 4K pixels (Task T4's
   report) for sign-off items 2 and 16.

Record the developer's words against each item in the spec's *Built, slice (b)* note (Step 5).

- [ ] **Step 4: The Landing section of CLAUDE.md**

Add a section after *The drive and the jump*:

```markdown
## Landing (slice b: fly down, hover over real ground)

**Two floors.** Over a solid world (`EGround::Solid`: barren, ice,
terrestrial; oceans and giants keep the floor sphere) the **drive's** floor
is 10 km above the highest peak (`FloorFor`), and **cruise and the vertical
lever read the ground** -- `FFlightSurface::Ground`, an `IGroundField` over
WorldRelief, the one height function -- at every altitude, never the sphere.
`ShipFlight::RayToGround` marches the ground by its slope bound, once a frame
per direction; an exhausted march is a hit.

**Gravity is held, never flown** (ADR 0005, amended): `ShipFlight::GravityAt`
sums every body; the velocity never gets g dt. It is felt only under a solid
world's drive floor, airborne: the boosters' hold (`ds.Boosters.HoldWatts`,
paid first in their share), the hold's hiss, slower climbs on heavy worlds
(`ds.Vertical.HeavyFloor`, by gravity alone), and the starved sink
(`ds.Boosters.StarvedSink`), which the ground always catches.

**The vertical lever** (Space up, C down) is ship state like the other two:
a log rate 0.1-200 m/s, HOVER at zero, a detent, X and every fold set HOVER,
and after X a press the way the ship is moving catches it. It is live within
`ds.Land.Regime` (50 km, leaving over 55) of a world's cruise floor, where
cruise flies the nose's horizontal projection -- looking down never dives the
ship -- and across 40-50 km both blend out. Under a solid world's drive floor
F gives **DriveBelowFloor**: the ship flies cruise, Shift/Ctrl move cruise,
and the drive takes over 500 m above the floor with the nose clear of it.

**The ground always catches.** In the regime horizontal speed is held to the
**skim cap**, max(20 m/s, AGL / 2.5 s); a ridge ahead slows the ship to rest
against it; and the descent is held to the **approach law**, an exponential
ease (`ds.Land.ApproachSeconds`, 4 s) with a knee the boosters can follow, to
contact at `ds.Land.TouchdownSpeed` (0.5 m/s), measured on the **footprint**
-- four gear feet 1.5 m under the origin and the belly's four corners
(`GEAR`, `BELLY` in `hauler_layout.py`, `ShipLanding` in C++). If a point is
ever under the ground the **ground's hard stop lifts** the ship (sphere
floors still never lift). `DeepSpace.Ship.Landing.GroundAlwaysCatches*` is
the invariant. LANDED is slice (c).

**The ground** (`AWorldGround`, `hauler_ground`): the nearest solid world's
cube-sphere quadtree, CDLOD from the ship, 2:1, tiles 33 x 33 with skirts,
built on two workers, uploaded four a frame, on the counter-frame, shaded by
`M_SkyGround` exactly as the orbit shades (the vertex normal carries what the
tile resolves, the pixel the rest). It takes the body from the sky under 50
km and grows the relief in to the drive floor. Residency never gates motion.
`ds.Terrain.Describe` prints the cut.

**The HUD below the floor:** `840 M ABOVE GROUND · SINKING 3 M/S`, the
vertical lever in the motion line (`HOVER`, `CLIMB 5 M/S`, `SINK 3 M/S`, `ABOVE
THE GROUND'S REACH`), `DRIVE ABOVE THE FLOOR`; the target's ETA in cruise over
a solid world counts to the ground.
```

- [ ] **Step 5: The spec's note, and the merge**

In the landing spec, under *Slice (b)*'s **Done when**, add:

```markdown
**Built** (feat/landing-b, <date>): the gate's verdict <PMC | CUSTOM PRIMITIVE>; tiles <n> at 50 km,
<n> at 1.5 m; a tile <ms> ms; the largest split pop <px> 4K pixels; the frame <ms> ms at 50 km and
<ms> ms at the ground (Eyes.LandingFrame); the handover's rendered brightness gap <value>
(Eyes.HandoverParity); the developer's flight: <their words per item>. The done-when's "from its
drive floor ... through the handover" is flown as two legs: from 60 km down through the 50 km
handover to the drive floor on the drive, then C from the floor to the ground (the floor is under
the handover, so no leg from the floor crosses it).
```

filled from Tasks B1, T3, T4 and Z's reports and the developer's reply. Then:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
git add Source/DeepSpace/Tests/Eyes/LandingFrameEyesTest.cpp CLAUDE.md docs/superpowers/specs/2026-09-27-landing-design.md && \
git commit -m "$(cat <<'EOF'
docs: landing slice (b) as built -- the Landing section, the frame at 4K, the developer's flight

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

The merge of `feat/landing-b` into `main` is the developer's decision once the Step 3 checklist
is answered (spec, *Workflow*); it is not taken by an agent.

---

# Slice (c): touchdown and LANDED

One tree, `.worktrees/landing-c`, cut from `main` after slice (b) merges:

```bash
git -C /home/matt/Development/deepspace worktree add .worktrees/landing-c -b feat/landing-c main
```

Every command in Tasks 40-47 runs from `/home/matt/Development/deepspace/.worktrees/landing-c`: the steps write `./build.sh`, `./test.sh`, `Tools/mutate.sh` and `git` relative to it, so prefix each command with `cd /home/matt/Development/deepspace/.worktrees/landing-c &&` (an agent's working directory is reset between calls).

## Task 40 (C1): the rest plane, the settle's turn and the contact rules, pure

The three pieces of decision 11 that need no flight state: which three feet a
rigid hull rests on, how fast the settle turns it and about which axis, and
when the contact changes.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipLanding.h` (append after slice (b)'s last declaration, before the file ends)
- Modify: `Source/DeepSpace/Ship/ShipLanding.cpp` (append at the end)
- Create: `Source/DeepSpace/Tests/ShipLandingRestTest.cpp`

**Interfaces:**
- Consumes: `ShipFlight::MinApproachSeconds` (`Ship/ShipFlightSurface.h`, slice b).
- Produces: `enum class EGroundContact : uint8 { Airborne, Settling, Landed }`; the constants `ShipLanding::DefaultSettleBandCm` (800), `DefaultSettleDegPerSec` (4), `MinSettleBandCm` (100), `LandedPenetrationCm` (1), `TripodToleranceCm` (2), `AtRestCmPerSecond` (1); `struct ShipLanding::FRestPlane { int32 Tripod[3]; int32 FreeFoot; FVector Normal; FVector Point; bool bOriginInside; }`; `FRestPlane ShipLanding::RestPlane(const FVector (&Ground)[4], const FVector& Up)`; `FVector ShipLanding::SettleRate(const FQuat& Orientation, const FVector& RestNormal, double FootprintClearanceCm, double SettleBandCm, double MaxRadPerSecond, double Step)`; `double ShipLanding::SettleSkimCap(double FootprintClearanceCm, double ApproachSeconds)`; `struct ShipLanding::FContactInputs { bool bHasGround; double FootprintClearanceCm; double VerticalSpeedCmPerS; bool bLeverAsksClimb; double DeepestFootUnderCm; double TripodGapCm; bool bOriginInsideTripod; double SettleBandCm; }`; `EGroundContact ShipLanding::NextContact(EGroundContact Now, const FContactInputs& In)`.

- [ ] **Step 1: Write the failing tests.** Create `Source/DeepSpace/Tests/ShipLandingRestTest.cpp`:

```cpp
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipLanding.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 11's pure pieces: the rest plane a rigid hull meets over
 * four ground points, the settle's turn, and the contact rules. Siblings, no
 * test path with children.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingRestPlaneTest, "DeepSpace.Ship.Landing.RestPlane",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingContactRulesTest, "DeepSpace.Ship.Landing.ContactRules",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingTurnToRestPlaneTest, "DeepSpace.Ship.Landing.TurnToRestPlane",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LandingRestTestLocal
{
    /** The hauler's gear in plan, ship space, cm (hauler_layout's GEAR), over
     *  ground at the given heights: the ground point under each foot,
     *  relative to the ship's origin. */
    void Quad(FVector (&Out)[4], double H0, double H1, double H2, double H3)
    {
        Out[0] = FVector(-700.0, -300.0, H0);
        Out[1] = FVector(-700.0, 400.0, H1);
        Out[2] = FVector(1600.0, -100.0, H2);
        Out[3] = FVector(1600.0, 200.0, H3);
    }

    /** Point's height over Plane along its normal, cm. */
    double Above(const ShipLanding::FRestPlane& Plane, const FVector& Point)
    {
        return (Point - Plane.Point) | Plane.Normal;
    }

    bool OnTripod(const ShipLanding::FRestPlane& Plane, int32 Foot)
    {
        return Plane.Tripod[0] == Foot || Plane.Tripod[1] == Foot || Plane.Tripod[2] == Foot;
    }
}

bool FLandingRestPlaneTest::RunTest(const FString& Parameters)
{
    using namespace LandingRestTestLocal;
    const FVector Up = FVector::UpVector;
    FVector Ground[4];

    // Flat: the plane is the ground, and the origin is over it.
    Quad(Ground, -150.0, -150.0, -150.0, -150.0);
    {
        const ShipLanding::FRestPlane Plane = ShipLanding::RestPlane(Ground, Up);
        TestTrue(TEXT("over flat ground the rest plane is level"), Plane.Normal.Equals(Up, 1e-12));
        TestTrue(TEXT("and the origin is over its tripod"), Plane.bOriginInside);
        TestTrue(TEXT("and every foot is on it"), FMath::Abs(Above(Plane, Ground[Plane.FreeFoot])) < 1e-9);
    }

    // A true plane at 15 degrees: either split gives that plane.
    const double Tan15 = FMath::Tan(FMath::DegreesToRadians(15.0));
    Quad(Ground, 0.0, 0.0, 0.0, 0.0);
    for (FVector& Point : Ground)
    {
        Point.Z = -150.0 + Point.X * Tan15;
    }
    {
        const ShipLanding::FRestPlane Plane = ShipLanding::RestPlane(Ground, Up);
        TestTrue(TEXT("over a 15-degree plane the rest plane is that plane"),
                 Plane.Normal.Equals(FVector(-Tan15, 0.0, 1.0).GetSafeNormal(), 1e-9));
    }

    // A ridge along the 0-3 diagonal: the hull rests on the ridge, and the
    // fourth foot hangs over ground that falls away.
    Quad(Ground, 0.0, -100.0, -100.0, 0.0);
    {
        const ShipLanding::FRestPlane Plane = ShipLanding::RestPlane(Ground, Up);
        TestTrue(TEXT("on a ridge the tripod holds both ends of it"), OnTripod(Plane, 0) && OnTripod(Plane, 3));
        TestTrue(FString::Printf(TEXT("and the free foot's ground is below the plane (%.3f cm)"), Above(Plane, Ground[Plane.FreeFoot])),
                 Above(Plane, Ground[Plane.FreeFoot]) < -1.0);
    }

    // A valley along 0-3 is a ridge along 1-2.
    Quad(Ground, -100.0, 0.0, 0.0, -100.0);
    {
        const ShipLanding::FRestPlane Plane = ShipLanding::RestPlane(Ground, Up);
        TestTrue(TEXT("across a valley the tripod holds the other diagonal"), OnTripod(Plane, 1) && OnTripod(Plane, 2));
        TestTrue(TEXT("and the free foot is below the plane"), Above(Plane, Ground[Plane.FreeFoot]) < -1.0);
    }

    // The order the feet come in changes nothing.
    Quad(Ground, 12.0, -40.0, -95.0, 30.0);
    {
        const ShipLanding::FRestPlane Plane = ShipLanding::RestPlane(Ground, Up);
        const int32 Shuffle[4] = { 2, 0, 3, 1 };
        FVector Shuffled[4];
        for (int32 Index = 0; Index < 4; ++Index)
        {
            Shuffled[Index] = Ground[Shuffle[Index]];
        }
        const ShipLanding::FRestPlane Again = ShipLanding::RestPlane(Shuffled, Up);
        TestTrue(TEXT("the same plane whatever order the feet are given in"), Again.Normal.Equals(Plane.Normal, 1e-12));
        TestTrue(TEXT("and the same free foot"), Shuffled[Again.FreeFoot].Equals(Ground[Plane.FreeFoot], 1e-12));
    }

    // A thousand quads, turned every way: the free foot is never above the
    // plane, and the origin is always over the tripod.
    FRandomStream Rng(8);
    int32 Above0 = 0;
    int32 Outside = 0;
    for (int32 Trial = 0; Trial < 1000; ++Trial)
    {
        // Test data, not a world: heights uniform over +-3 m on purpose.
        Quad(Ground, Rng.FRandRange(-300.0f, 300.0f), Rng.FRandRange(-300.0f, 300.0f),
             Rng.FRandRange(-300.0f, 300.0f), Rng.FRandRange(-300.0f, 300.0f));
        const FQuat Turn(Rng.GetUnitVector(), Rng.FRandRange(0.0f, UE_PI));
        for (FVector& Point : Ground)
        {
            Point = Turn.RotateVector(Point);
        }
        const ShipLanding::FRestPlane Plane = ShipLanding::RestPlane(Ground, Turn.RotateVector(Up));
        Above0 += Above(Plane, Ground[Plane.FreeFoot]) > 1e-6 ? 1 : 0;
        Outside += Plane.bOriginInside ? 0 : 1;
    }
    TestEqual(TEXT("the free foot is never above the rest plane"), Above0, 0);
    TestEqual(TEXT("the gear always surrounds the origin"), Outside, 0);

    // A quad that does not surround the origin says so.
    Quad(Ground, -150.0, -150.0, -150.0, -150.0);
    for (FVector& Point : Ground)
    {
        Point.X += 5000.0;
    }
    TestFalse(TEXT("gear wholly to one side does not hold the origin"), ShipLanding::RestPlane(Ground, Up).bOriginInside);
    return true;
}

bool FLandingContactRulesTest::RunTest(const FString& Parameters)
{
    using ShipLanding::NextContact;
    ShipLanding::FContactInputs Descending;
    Descending.bHasGround = true;
    Descending.FootprintClearanceCm = 500.0;
    Descending.VerticalSpeedCmPerS = -100.0;
    Descending.SettleBandCm = ShipLanding::DefaultSettleBandCm;

    ShipLanding::FContactInputs Resting = Descending;
    Resting.FootprintClearanceCm = 0.0;
    Resting.VerticalSpeedCmPerS = 0.0;
    Resting.DeepestFootUnderCm = 0.5;
    Resting.TripodGapCm = 1.5;
    Resting.bOriginInsideTripod = true;

    TestTrue(TEXT("coming down into the band, it settles"),
             NextContact(EGroundContact::Airborne, Descending) == EGroundContact::Settling);
    {
        ShipLanding::FContactInputs Hover = Descending;
        Hover.VerticalSpeedCmPerS = 0.0;
        TestTrue(TEXT("hovering in the band is not settling"), NextContact(EGroundContact::Airborne, Hover) == EGroundContact::Airborne);
        ShipLanding::FContactInputs High = Descending;
        High.FootprintClearanceCm = 800.0;
        TestTrue(TEXT("above the band it is airborne"), NextContact(EGroundContact::Airborne, High) == EGroundContact::Airborne);
        ShipLanding::FContactInputs Climb = Descending;
        Climb.bLeverAsksClimb = true;
        TestTrue(TEXT("asking a climb it never settles"), NextContact(EGroundContact::Airborne, Climb) == EGroundContact::Airborne);
    }
    TestTrue(TEXT("never straight from the air to landed"), NextContact(EGroundContact::Airborne, Resting) == EGroundContact::Airborne);
    TestTrue(TEXT("resting on its tripod, settling becomes landed"), NextContact(EGroundContact::Settling, Resting) == EGroundContact::Landed);
    {
        ShipLanding::FContactInputs Deep = Resting;
        Deep.DeepestFootUnderCm = 1.5;
        ShipLanding::FContactInputs Rocking = Resting;
        Rocking.TripodGapCm = 2.5;
        ShipLanding::FContactInputs Overhang = Resting;
        Overhang.bOriginInsideTripod = false;
        ShipLanding::FContactInputs Moving = Resting;
        Moving.VerticalSpeedCmPerS = -2.0;
        ShipLanding::FContactInputs Lifting = Resting;
        Lifting.bLeverAsksClimb = true;
        TestTrue(TEXT("a foot 1.5 cm under is not landed"), NextContact(EGroundContact::Settling, Deep) == EGroundContact::Settling);
        TestTrue(TEXT("a tripod 2.5 cm off is not landed"), NextContact(EGroundContact::Settling, Rocking) == EGroundContact::Settling);
        TestTrue(TEXT("the origin off the tripod is not landed"), NextContact(EGroundContact::Settling, Overhang) == EGroundContact::Settling);
        TestTrue(TEXT("still sinking is not landed"), NextContact(EGroundContact::Settling, Moving) == EGroundContact::Settling);
        TestTrue(TEXT("asking a climb while settling lifts it"), NextContact(EGroundContact::Settling, Lifting) == EGroundContact::Airborne);
    }
    {
        ShipLanding::FContactInputs High = Resting;
        High.FootprintClearanceCm = 900.0;
        TestTrue(TEXT("out of the band, settling ends"), NextContact(EGroundContact::Settling, High) == EGroundContact::Airborne);
        TestTrue(TEXT("landed latches, whatever the clearance says"), NextContact(EGroundContact::Landed, High) == EGroundContact::Landed);
        ShipLanding::FContactInputs Lift = Resting;
        Lift.bLeverAsksClimb = true;
        TestTrue(TEXT("landed lifts off only on a climb"), NextContact(EGroundContact::Landed, Lift) == EGroundContact::Airborne);
        ShipLanding::FContactInputs Gone = Resting;
        Gone.bHasGround = false;
        TestTrue(TEXT("with no ground, landed is airborne"), NextContact(EGroundContact::Landed, Gone) == EGroundContact::Airborne);
    }
    return true;
}

bool FLandingTurnToRestPlaneTest::RunTest(const FString& Parameters)
{
    const double Max = FMath::DegreesToRadians(ShipLanding::DefaultSettleDegPerSec);
    const double Band = ShipLanding::DefaultSettleBandCm;
    const double Step = 1.0 / 120.0;
    const FQuat Rolled(FVector::ForwardVector, FMath::DegreesToRadians(10.0));
    const FVector Normal = FVector::UpVector;

    const FVector AtTouch = ShipLanding::SettleRate(Rolled, Normal, 0.0, Band, Max, Step);
    TestEqual(TEXT("at a foot's touch it turns at the full rate"), AtTouch.Size(), Max, 1e-12);
    TestEqual(TEXT("and never about the body's own Z: yaw is the pilot's"), AtTouch.Z, 0.0);
    TestEqual(TEXT("halfway down the band, half the rate"), ShipLanding::SettleRate(Rolled, Normal, 0.5 * Band, Band, Max, Step).Size(), 0.5 * Max, 1e-12);
    TestTrue(TEXT("at the band and above, nothing"), ShipLanding::SettleRate(Rolled, Normal, Band, Band, Max, Step).IsZero());

    // One step of it closes the angle.
    const FQuat After = Rolled * FQuat(AtTouch.GetSafeNormal(), AtTouch.Size() * Step);
    TestTrue(TEXT("and turns body +Z toward the plane"),
             (After.GetUpVector() | Normal) > (Rolled.GetUpVector() | Normal));

    // Nearly there: it lands on the plane and does not overshoot.
    const double Tiny = FMath::DegreesToRadians(0.01);
    const FVector Last = ShipLanding::SettleRate(FQuat(FVector::ForwardVector, Tiny), Normal, 0.0, Band, Max, Step);
    TestEqual(TEXT("the last step closes exactly what is left"), Last.Size() * Step, Tiny, 1e-12);

    // Yawed first: still no body-Z part.
    const FQuat Yawed = FQuat(FVector::UpVector, 1.0) * Rolled;
    TestEqual(TEXT("a yawed ship's settle has no yaw either"), ShipLanding::SettleRate(Yawed, Normal, 0.0, Band, Max, Step).Z, 0.0, 1e-15);

    TestEqual(TEXT("the sideways cap is the clearance over the approach"), ShipLanding::SettleSkimCap(400.0, 4.0), 100.0);
    TestEqual(TEXT("with the approach's own 0.5 s floor"), ShipLanding::SettleSkimCap(400.0, 0.0), 800.0);
    TestEqual(TEXT("and never negative under the ground"), ShipLanding::SettleSkimCap(-3.0, 4.0), 0.0);
    return true;
}

#endif
```

- [ ] **Step 2: Build to prove the tests fail.** Run `./build.sh`. Expected: FAIL to compile, `'RestPlane' is not a member of 'ShipLanding'` (and `EGroundContact` undeclared). A test that cannot compile is this project's failing test for a new interface.

- [ ] **Step 3: Write the declarations.** Append to `Source/DeepSpace/Ship/ShipLanding.h`:

```cpp
/** Where the ship is against the ground (landing decision 11). Held by
 *  FShipFlightState and changed only inside its substep: no write path. */
enum class EGroundContact : uint8
{
    /** Flying: the levers move the ship. */
    Airborne,

    /** Under ds.Land.SettleBand, having come down into it: the ship turns
     *  onto the ground's rest plane, the pilot's input added, and slows
     *  sideways with its clearance. */
    Settling,

    /** At rest on three feet, exactly: velocity and turn zero and the
     *  orientation held, until the vertical lever asks a climb. */
    Landed,
};

namespace ShipLanding
{
    /** ds.Land.SettleBand's default, cm: more than the 3.4 m a corner of the
     *  23 m gear drops on a 15-degree slope, so the settle has begun before
     *  any foot can touch. */
    inline constexpr double DefaultSettleBandCm = 800.0;

    /** ds.Land.SettleDegPerSec's default: the fastest the settle turns, at a
     *  foot's touch. Slow enough to read as the ship sitting down. */
    inline constexpr double DefaultSettleDegPerSec = 4.0;

    /** The least band ds.Land.SettleBand may set, cm (review focus 5). At 0
     *  nothing could come down into the band, so nothing would settle and
     *  nothing would ever land; a metre still lets a descent settle before
     *  its feet rest. */
    inline constexpr double MinSettleBandCm = 100.0;

    /** Landed means no foot more than this under the ground, cm. */
    inline constexpr double LandedPenetrationCm = 1.0;

    /** ...and the tripod's three feet within this of it, cm: 2 cm over the
     *  gear's 23 m is 0.05 degrees, under any pixel the glass shows. */
    inline constexpr double TripodToleranceCm = 2.0;

    /** A descent at rest: the radial speed within this, cm/s. */
    inline constexpr double AtRestCmPerSecond = 1.0;

    /** The plane a rigid hull rests on over four ground points. */
    struct FRestPlane
    {
        /** The three feet it rests on, indices into the ground points. */
        int32 Tripod[3] = { 0, 1, 2 };

        /** The fourth, over wherever the ground falls away under it. */
        int32 FreeFoot = 3;

        /** Unit, on Up's side. */
        FVector Normal = FVector::UpVector;

        /** On the plane: the first tripod foot's ground point, in the frame
         *  the ground points were given in. */
        FVector Point = FVector::ZeroVector;

        /** Whether the origin's projection along Up lies in the tripod's
         *  triangle. Always, for the hauler's gear over any ground: the
         *  origin is inside the gear's plan. */
        bool bOriginInside = false;
    };

    /**
     * The rest plane, defined rather than fitted (decision 11). Of the two
     * ways to split four ground points into triangles, the upper -- the
     * diagonal that is higher where the two cross in plan -- is the surface a
     * rigid hull lowered onto them meets; of its two facets, the one whose
     * triangle holds the origin's projection is the plane and its three feet
     * the tripod. The fourth point is then on or below the plane.
     *
     * Ground: the ground point under each gear foot, relative to the ship's
     * origin, in any one frame (the flight state gives universe axes). Up:
     * the local vertical in that frame. The feet may come in any order.
     */
    DEEPSPACE_API FRestPlane RestPlane(const FVector (&Ground)[4], const FVector& Up);

    /**
     * The settle's turn this substep, rad/s, as a rotation vector in body
     * axes: X roll and Y pitch, never Z -- yaw is the pilot's alone. It swings
     * body +Z toward RestNormal (universe axes) at MaxRadPerSecond x (1 -
     * FootprintClearance / SettleBand), and never by more than closes the
     * angle in Step, so it comes onto the plane and does not overshoot. Zero
     * at or above the band.
     */
    DEEPSPACE_API FVector SettleRate(const FQuat& Orientation, const FVector& RestNormal, double FootprintClearanceCm,
                                     double SettleBandCm, double MaxRadPerSecond, double Step);

    /** The sideways cap while Settling and descending, cm/s: the clearance
     *  over the approach's time constant (clamped as ds.Land.ApproachSeconds
     *  is), so the ship stops skidding as it stops sinking. */
    DEEPSPACE_API double SettleSkimCap(double FootprintClearanceCm, double ApproachSeconds);

    /** What the contact rules read, measured at the end of a substep. */
    struct FContactInputs
    {
        bool bHasGround = false;
        double FootprintClearanceCm = 0.0;
        /** Radial, + up, cm/s. */
        double VerticalSpeedCmPerS = 0.0;
        bool bLeverAsksClimb = false;
        /** How far the deepest gear foot is under the ground, cm; never negative. */
        double DeepestFootUnderCm = 0.0;
        /** The tripod's three feet: the largest |height over the ground|, cm. */
        double TripodGapCm = 0.0;
        bool bOriginInsideTripod = false;
        double SettleBandCm = DefaultSettleBandCm;
    };

    /**
     * The contact's next state (decision 11). No ground in reach is Airborne
     * from anything. Landed latches until the lever asks a climb. Airborne
     * becomes Settling on coming down into the band with the lever not
     * climbing. Settling becomes Airborne above the band or on a climb, and
     * Landed with no foot more than LandedPenetrationCm under, the tripod
     * within TripodToleranceCm, the origin over it and the descent at rest.
     * Landed is entered only from Settling.
     */
    DEEPSPACE_API EGroundContact NextContact(EGroundContact Now, const FContactInputs& In);
}
```

- [ ] **Step 4: Write the implementation.** Append to `Source/DeepSpace/Ship/ShipLanding.cpp` (add `#include "Algo/Sort.h"` and, if not already there, `#include "Ship/ShipFlightSurface.h"` at the top):

```cpp
namespace
{
    double Cross2(const FVector2D& L, const FVector2D& R)
    {
        return L.X * R.Y - L.Y * R.X;
    }

    /** Q in triangle (P0, P1, P2), edges included. */
    bool InTriangle(const FVector2D& Q, const FVector2D& P0, const FVector2D& P1, const FVector2D& P2)
    {
        constexpr double Eps = 1e-9;
        const double D0 = Cross2(P1 - P0, Q - P0);
        const double D1 = Cross2(P2 - P1, Q - P1);
        const double D2 = Cross2(P0 - P2, Q - P2);
        const bool bNegative = D0 < -Eps || D1 < -Eps || D2 < -Eps;
        const bool bPositive = D0 > Eps || D1 > Eps || D2 > Eps;
        return !(bNegative && bPositive);
    }
}

ShipLanding::FRestPlane ShipLanding::RestPlane(const FVector (&Ground)[4], const FVector& Up)
{
    const FVector U = Up.GetSafeNormal();
    FVector AxisA;
    FVector AxisB;
    U.FindBestAxisVectors(AxisA, AxisB);

    // The four points seen from above, and how high each is.
    FVector2D Plan[4];
    double Height[4];
    FVector2D Centroid(0.0, 0.0);
    for (int32 Foot = 0; Foot < 4; ++Foot)
    {
        Plan[Foot] = FVector2D(Ground[Foot] | AxisA, Ground[Foot] | AxisB);
        Height[Foot] = Ground[Foot] | U;
        Centroid += Plan[Foot] * 0.25;
    }

    // Round the quad, so the diagonals are known whatever order the feet
    // came in: Ring[0]-Ring[2] and Ring[1]-Ring[3].
    int32 Ring[4] = { 0, 1, 2, 3 };
    const auto Bearing = [&Plan, &Centroid](int32 Foot)
    {
        return FMath::Atan2(Plan[Foot].Y - Centroid.Y, Plan[Foot].X - Centroid.X);
    };
    Algo::Sort(Ring, [&Bearing](int32 L, int32 R) { return Bearing(L) < Bearing(R); });

    // Where the diagonals cross in plan, and how high each is there: the
    // higher one is the ridge the hull meets, the upper envelope of the four.
    const FVector2D Across = Plan[Ring[2]] - Plan[Ring[0]];
    const FVector2D Other = Plan[Ring[3]] - Plan[Ring[1]];
    const double Den = Cross2(Across, Other);
    double T = 0.5;
    double S = 0.5;
    if (FMath::Abs(Den) > UE_DOUBLE_SMALL_NUMBER)
    {
        const FVector2D Gap = Plan[Ring[1]] - Plan[Ring[0]];
        T = Cross2(Gap, Other) / Den;
        S = Cross2(Gap, Across) / Den;
    }
    const double HeightA = FMath::Lerp(Height[Ring[0]], Height[Ring[2]], T);
    const double HeightB = FMath::Lerp(Height[Ring[1]], Height[Ring[3]], S);
    const bool bFirstDiagonal = HeightA >= HeightB;
    const int32 D0 = bFirstDiagonal ? Ring[0] : Ring[1];
    const int32 D1 = bFirstDiagonal ? Ring[2] : Ring[3];
    const int32 E0 = bFirstDiagonal ? Ring[1] : Ring[0];
    const int32 E1 = bFirstDiagonal ? Ring[3] : Ring[2];

    // Of the upper split's two facets, the one over the origin. On the
    // diagonal both are; the first is taken.
    const FVector2D Origin(0.0, 0.0);
    const bool bInFirst = InTriangle(Origin, Plan[D0], Plan[D1], Plan[E0]);
    const bool bInSecond = InTriangle(Origin, Plan[D0], Plan[D1], Plan[E1]);
    bool bUseFirst = bInFirst;
    if (!bInFirst && !bInSecond)
    {
        const FVector2D First = (Plan[D0] + Plan[D1] + Plan[E0]) / 3.0;
        const FVector2D Second = (Plan[D0] + Plan[D1] + Plan[E1]) / 3.0;
        bUseFirst = First.SizeSquared() <= Second.SizeSquared();
    }

    FRestPlane Plane;
    Plane.Tripod[0] = D0;
    Plane.Tripod[1] = D1;
    Plane.Tripod[2] = bUseFirst ? E0 : E1;
    Plane.FreeFoot = bUseFirst ? E1 : E0;
    Plane.bOriginInside = bInFirst || bInSecond;
    FVector Normal = FVector::CrossProduct(Ground[Plane.Tripod[1]] - Ground[Plane.Tripod[0]],
                                           Ground[Plane.Tripod[2]] - Ground[Plane.Tripod[0]]).GetSafeNormal();
    if ((Normal | U) < 0.0)
    {
        Normal = -Normal;
    }
    Plane.Normal = Normal.IsNearlyZero() ? U : Normal;
    Plane.Point = Ground[Plane.Tripod[0]];
    return Plane;
}

FVector ShipLanding::SettleRate(const FQuat& Orientation, const FVector& RestNormal, double FootprintClearanceCm,
                                double SettleBandCm, double MaxRadPerSecond, double Step)
{
    if (!(SettleBandCm > 0.0) || FootprintClearanceCm >= SettleBandCm || !(MaxRadPerSecond > 0.0) || !(Step > 0.0))
    {
        return FVector::ZeroVector;
    }
    // The normal in body axes, and Z x Normal: the axis that swings body +Z
    // onto it, which has no Z part, so the settle never yaws.
    const FVector Normal = Orientation.UnrotateVector(RestNormal.GetSafeNormal());
    const FVector Axis(-Normal.Y, Normal.X, 0.0);
    const double Sin = Axis.Size();
    if (Sin < UE_DOUBLE_SMALL_NUMBER)
    {
        // On the plane already, or upside down over it, which nothing flies.
        return FVector::ZeroVector;
    }
    const double Angle = FMath::Atan2(Sin, Normal.Z);
    const double Weight = FMath::Clamp(1.0 - FMath::Max(0.0, FootprintClearanceCm) / SettleBandCm, 0.0, 1.0);
    const double Rate = FMath::Min(MaxRadPerSecond * Weight, Angle / Step);
    return Axis / Sin * Rate;
}

double ShipLanding::SettleSkimCap(double FootprintClearanceCm, double ApproachSeconds)
{
    return FMath::Max(0.0, FootprintClearanceCm) / FMath::Max(ApproachSeconds, ShipFlight::MinApproachSeconds);
}

EGroundContact ShipLanding::NextContact(EGroundContact Now, const FContactInputs& In)
{
    if (!In.bHasGround)
    {
        return EGroundContact::Airborne;
    }
    switch (Now)
    {
    case EGroundContact::Landed:
        // The latch: a refed ship does not lift, a starved one does not
        // bounce. Only the pilot's climb ends it.
        return In.bLeverAsksClimb ? EGroundContact::Airborne : EGroundContact::Landed;

    case EGroundContact::Airborne:
        return !In.bLeverAsksClimb && In.FootprintClearanceCm < In.SettleBandCm
                && In.VerticalSpeedCmPerS < -AtRestCmPerSecond
            ? EGroundContact::Settling
            : EGroundContact::Airborne;

    case EGroundContact::Settling:
    {
        if (In.bLeverAsksClimb || In.FootprintClearanceCm >= In.SettleBandCm)
        {
            return EGroundContact::Airborne;
        }
        const bool bRests = In.DeepestFootUnderCm <= LandedPenetrationCm && In.TripodGapCm <= TripodToleranceCm
            && In.bOriginInsideTripod && FMath::Abs(In.VerticalSpeedCmPerS) <= AtRestCmPerSecond;
        return bRests ? EGroundContact::Landed : EGroundContact::Settling;
    }
    }
    return Now;
}
```

- [ ] **Step 5: Build and run.** `./build.sh` (expected: `Result: Succeeded`), then
  `./test.sh DeepSpace.Ship.Landing.RestPlane`, `./test.sh DeepSpace.Ship.Landing.ContactRules`,
  `./test.sh DeepSpace.Ship.Landing.TurnToRestPlane`. Expected: each `passed: 1`, no `FAILED`.

- [ ] **Step 6: Commit.**

```bash
git add Source/DeepSpace/Ship/ShipLanding.h Source/DeepSpace/Ship/ShipLanding.cpp Source/DeepSpace/Tests/ShipLandingRestTest.cpp
git commit -F - <<'EOF'
feat(landing): the rest plane, the settle's turn and the contact rules, pure

Decision 11's pieces that need no flight state. A rigid hull rests on the
upper split of its four ground points, on the facet over its origin; the
settle turns body +Z onto that plane in roll and pitch only, at 4 deg/s at a
foot's touch and never past it; Landed is entered only from Settling and
latches until the lever asks a climb.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

- [ ] **Step 7: Prove the rest-plane test can fail.**

```bash
Tools/mutate.sh Source/DeepSpace/Ship/ShipLanding.cpp 'const bool bFirstDiagonal = HeightA >= HeightB;' 'const bool bFirstDiagonal = HeightA <= HeightB;' DeepSpace.Ship.Landing.RestPlane
```

Expected: `KILLED: DeepSpace.Ship.Landing.RestPlane went red` (the ridge's free
foot ends up above the plane), then `restored clean`. And the contact rules:

```bash
Tools/mutate.sh Source/DeepSpace/Ship/ShipLanding.cpp 'return In.bLeverAsksClimb ? EGroundContact::Airborne : EGroundContact::Landed;' 'return EGroundContact::Landed;' DeepSpace.Ship.Landing.ContactRules
```

Expected: `KILLED`. Then `./build.sh` (the mutant's last build held the mutant;
mutate.sh rebuilds clean, and this confirms it).

---

## Task 41 (C2): settling and touchdown in the flight state

`FShipFlightState` holds the contact, finds the rest plane under its gear,
turns onto it while Settling, slows sideways in the band, and at touchdown
comes to rest exactly with cruise at STOP and the vertical lever at HOVER.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipFlightState.h` (`FShipFlightLimits`, after `DriveThrust`; `FShipFlightState` public getters after `GetDriveNotchCount`; private helpers and fields after `LastHeldFraction`)
- Modify: `Source/DeepSpace/Ship/ShipFlightState.cpp` (`SubStep`, `CruiseSubStep` inside Task F6's regime block, new functions at the end)
- Create: `Source/DeepSpace/Tests/LandingTestFixtures.h`, `Source/DeepSpace/Tests/ShipTouchdownTest.cpp`

**Interfaces:**
- Consumes: Task C1's `ShipLanding` functions; from slice (b): `FShipFlightCommand::Vertical`, `FShipFlightLimits::{ApproachSeconds, TouchdownSpeed, GearClearanceCm}` (F5, F6), `TOptional<double> FShipFlightState::GetFootprintClearance() const`, `TOptional<double> GetGroundAltitude() const` (F5), `double GetVerticalSpeed() const` (F6), the private `int32 NearestGround() const` (F5), `TOptional<double> ShipFlight::GroundAt(const FFlightSurface&, const FUniversePosition&)`, `FFlightSurface::{Ground, bWorld, HasGround()}` (F2), `ShipLanding::GearFeetXY[4][2]` (F3), the `double Horizontal` local in `CruiseSubStep`'s regime block (F6), `IGroundField`, `FGroundFieldRef` (F2), `GroundFixtures::{FCrossedSines, Somewhere}` (F2), `UShipSubsystem::FloorFor(const FSkyBody&)` (S1).
- Produces: `FShipFlightLimits::SettleBandCm`, `SettleRadPerSecond`; `EGroundContact FShipFlightState::GetContact() const`, `const ShipLanding::FRestPlane& GetRestPlane() const`, `int32 GetTouchdownCount() const`, `const FFlightSurface* GetGroundSurface() const`; `Tests/LandingTestFixtures.h`: `enum class LandingFixtures::EGroundShape : uint8 { Flat, Slope15, CraterRim, Ridge, CrossedSines }`, `class LandingFixtures::FShapedGround final : public IGroundField`, `FFlightSurface LandingFixtures::World(EGroundShape Shape, double RadiusCm = 6.0e8, const FUniversePosition& Centre = FUniversePosition())`, `FFlightSurface LandingFixtures::RealWorld(const FSystemId& System, int32 PlanetIndex)`.

- [ ] **Step 1: Write the fixtures and the failing tests.** Create `Source/DeepSpace/Tests/LandingTestFixtures.h`:

```cpp
#pragma once

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkySystem.h"
#include "Surface/GroundField.h"
#include "Tests/GroundFixtures.h"
#include "Universe/UniverseSubsystem.h"

/**
 * Grounds for the touchdown tests (landing decision 11): analytic shapes a
 * rigid hull rests on in known ways, around the +Z pole of a world, and the
 * real worlds by system and planet. Test data, not a generator: nothing
 * outside Tests/ may include this.
 */
namespace LandingFixtures
{
    enum class EGroundShape : uint8
    {
        Flat,
        Slope15,       // a true plane at 15 degrees, rising toward +X
        CraterRim,     // a crater's bowl and rim, the rim running under the gear
        Ridge,         // a ridge along X, falling away at 10 degrees each side
        CrossedSines,  // GroundFixtures::FCrossedSines, 40 m waves, slope 0.2
    };

    /**
     * Heights as functions of the arc coordinates x = R D.x, y = R D.y about
     * the +Z pole: meaningful near it, where every test flies. The planes are
     * bounded at 5 km from the pole, so MaxHeightCm is finite.
     */
    class FShapedGround final : public IGroundField
    {
    public:
        FShapedGround(EGroundShape InShape, double InRadiusCm)
            : Shape(InShape), R(InRadiusCm), Sines(GroundFixtures::FCrossedSines::WithSlope(InRadiusCm, 4000.0, 0.2))
        {
        }

        static constexpr double PlaneReachCm = 5.0e5;
        static constexpr double RidgeDeg = 10.0;
        static constexpr double CraterCentreXCm = -1500.0;
        static constexpr double CraterRadiusCm = 2500.0;
        static constexpr double CraterDepthCm = 300.0;
        static constexpr double CraterRim = 0.25;

        virtual double RadiusCm() const override { return R; }

        virtual double Height(const FVector3d& D, double FootprintCm) const override
        {
            FVector3d Unused;
            return HeightAndGradient(D, Unused, FootprintCm);
        }

        virtual double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const override
        {
            if (Shape == EGroundShape::CrossedSines)
            {
                return Sines.HeightAndGradient(D, Grad, FootprintCm);
            }
            const double X = R * D.X;
            const double Y = R * D.Y;
            double H = 0.0;
            double DHDX = 0.0;
            double DHDY = 0.0;
            if (Shape == EGroundShape::Slope15)
            {
                const double Tan = FMath::Tan(FMath::DegreesToRadians(15.0));
                H = Tan * FMath::Clamp(X, -PlaneReachCm, PlaneReachCm);
                DHDX = FMath::Abs(X) < PlaneReachCm ? Tan : 0.0;
            }
            else if (Shape == EGroundShape::Ridge)
            {
                const double Tan = FMath::Tan(FMath::DegreesToRadians(RidgeDeg));
                H = -Tan * FMath::Min(FMath::Abs(Y), PlaneReachCm);
                DHDY = FMath::Abs(Y) < PlaneReachCm ? -Tan * FMath::Sign(Y) : 0.0;
            }
            else if (Shape == EGroundShape::CraterRim)
            {
                // Today's crater profile: depth (q^2 - 1 + rim) inside, depth
                // rim (3 - 2q)^2 out to q = 1.5.
                const double RX = X - CraterCentreXCm;
                const double RY = Y;
                const double Dist = FMath::Sqrt(RX * RX + RY * RY);
                const double Q = Dist / CraterRadiusCm;
                if (Q < 1.5)
                {
                    const double Fall = 3.0 - 2.0 * Q;
                    H = Q < 1.0 ? CraterDepthCm * (Q * Q - 1.0 + CraterRim) : CraterDepthCm * CraterRim * Fall * Fall;
                    const double Wall = Q < 1.0 ? 2.0 * CraterDepthCm * Q : -4.0 * CraterDepthCm * CraterRim * Fall;
                    const double Apart = FMath::Max(Dist, 1.0e-6);
                    DHDX = Wall / CraterRadiusCm * RX / Apart;
                    DHDY = Wall / CraterRadiusCm * RY / Apart;
                }
            }
            // Height per unit of D is R times height per cm of arc.
            Grad = FVector3d(DHDX * R, DHDY * R, 0.0);
            return H;
        }

        virtual double OmittedBoundCm(double) const override { return 0.0; }

        virtual double MaxHeightCm() const override
        {
            switch (Shape)
            {
            case EGroundShape::Slope15: return FMath::Tan(FMath::DegreesToRadians(15.0)) * PlaneReachCm;
            case EGroundShape::CraterRim: return CraterDepthCm * CraterRim;
            case EGroundShape::CrossedSines: return Sines.MaxHeightCm();
            default: return 0.0;
            }
        }

        virtual double MinHeightCm() const override
        {
            switch (Shape)
            {
            case EGroundShape::Slope15: return -FMath::Tan(FMath::DegreesToRadians(15.0)) * PlaneReachCm;
            case EGroundShape::Ridge: return -FMath::Tan(FMath::DegreesToRadians(RidgeDeg)) * PlaneReachCm;
            case EGroundShape::CraterRim: return -CraterDepthCm * (1.0 - CraterRim);
            case EGroundShape::CrossedSines: return Sines.MinHeightCm();
            default: return 0.0;
            }
        }

        virtual double MaxSlope() const override
        {
            switch (Shape)
            {
            case EGroundShape::Slope15: return FMath::Tan(FMath::DegreesToRadians(15.0));
            case EGroundShape::Ridge: return FMath::Tan(FMath::DegreesToRadians(RidgeDeg));
            case EGroundShape::CraterRim: return FMath::Max(2.0 * CraterDepthCm, 4.0 * CraterDepthCm * CraterRim) / CraterRadiusCm;
            case EGroundShape::CrossedSines: return Sines.MaxSlope();
            default: return 0.0;
            }
        }

    private:
        EGroundShape Shape;
        double R;
        GroundFixtures::FCrossedSines Sines;
    };

    /** A solid world of RadiusCm centred at Centre, its ground shaped round
     *  +Z, its drive floor 10 km over the datum. */
    inline FFlightSurface World(EGroundShape Shape, double RadiusCm = 6.0e8, const FUniversePosition& Centre = FUniversePosition())
    {
        FFlightSurface Surface;
        Surface.Centre = Centre;
        Surface.Radius = RadiusCm;
        Surface.Floor = 1.0e6;
        Surface.bWorld = true;
        Surface.Ground = MakeShared<FShapedGround, ESPMode::ThreadSafe>(Shape, RadiusCm);
        return Surface;
    }

    /** A real world as UpdateSurfaces builds it: PlanetIndex from 0 (IV is
     *  3), its sky body PlanetIndex + 1 (0 is the star), its drive floor
     *  FloorFor's, its ground WorldRelief's when solid. No ground and a zero
     *  radius if the system or the planet is not there. */
    inline FFlightSurface RealWorld(const FSystemId& System, int32 PlanetIndex)
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("LandingFixtureWorld"));
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        FFlightSurface Surface;
        if (const UUniverseSubsystem* Universe = World->GetSubsystem<UUniverseSubsystem>())
        {
            if (const TOptional<FStarSystem> Found = Universe->GetSystem(System))
            {
                const FSkySystem Sky = LocalSystem::Here(Found);
                if (Sky.Bodies.IsValidIndex(PlanetIndex + 1))
                {
                    const FSkyBody& Body = Sky.Bodies[PlanetIndex + 1];
                    Surface.Centre = Body.Position;
                    Surface.Radius = Body.Radius;
                    Surface.Floor = UShipSubsystem::FloorFor(Body);
                    Surface.bWorld = true;
                    if (Body.Ground == EGround::Solid)
                    {
                        Surface.Ground = ShipGround::FromRelief(Body.Relief);
                    }
                }
            }
        }
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return Surface;
    }
}
```

Then create `Source/DeepSpace/Tests/ShipTouchdownTest.cpp`:

```cpp
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipLanding.h"
#include "Tests/LandingTestFixtures.h"
#include "Universe/StarSystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Touchdown in the pure flight state (landing decision 11): the settle onto
 * the rest plane, contact once, the latch, take-off. Pure: no world. Siblings,
 * no test path with children.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingSettleTest, "DeepSpace.Ship.Landing.Settle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingContactOnceTest, "DeepSpace.Ship.Landing.ContactOnce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TouchdownTestLocal
{
    using LandingFixtures::EGroundShape;

    /** A frame at 30 Hz: four substeps and the ground rays marched once, as
     *  at the slowest frame the game is expected to run. */
    constexpr double Frame = 1.0 / 30.0;

    /** Where LandingFixtures shapes its patch. */
    const FVector PatchUp(0.0, 0.0, 1.0);

    /** Upright over Up (body +Z along it), the nose YawRadians round it. */
    FQuat Level(const FVector& Up, double YawRadians)
    {
        FVector A;
        FVector B;
        Up.FindBestAxisVectors(A, B);
        const FVector Nose = A * FMath::Cos(YawRadians) + B * FMath::Sin(YawRadians);
        return FRotationMatrix::MakeFromXZ(Nose, Up).ToQuat();
    }

    /** A flight state over Surface: upright over the ground at Dir, its gear
     *  feet FootCm above the ground there, then turned by Tilt in body axes. */
    FShipFlightState Over(const FFlightSurface& Surface, const FVector& Dir, double FootCm, double YawRadians = 0.0,
                          const FQuat& Tilt = FQuat::Identity)
    {
        FShipFlightState Flight;
        const FVector Up = Dir.GetSafeNormal();
        const FUniversePosition Far = Surface.Centre + Up * (Surface.Radius + 1.0e6);
        const double Clearance = Flight.GetLimits().GearClearanceCm;
        const FUniversePosition At = Far + Up * (FootCm + Clearance - ShipFlight::GroundAt(Surface, Far).Get(0.0));
        Flight.SetUniverseTransform(At, Level(Up, YawRadians) * Tilt);
        Flight.SetSurfaces({ Surface });
        return Flight;
    }

    void Levers(FShipFlightState& Flight, double Throttle, double Vertical, const FVector& Attitude = FVector::ZeroVector)
    {
        FShipFlightCommand Command = Flight.GetCommand();
        Command.Throttle = Throttle;
        Command.Vertical = Vertical;
        Command.AttitudeRate = Attitude;
        Flight.SetCommand(Command);
    }

    double AngleDeg(const FVector& A, const FVector& B)
    {
        return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(A.GetSafeNormal() | B.GetSafeNormal(), -1.0, 1.0)));
    }

    FVector BodyUp(const FShipFlightState& Flight)
    {
        return Flight.GetUniverseOrientation().GetUpVector();
    }

    FVector RadialUp(const FShipFlightState& Flight)
    {
        const FFlightSurface* Surface = Flight.GetGroundSurface();
        return Surface ? (Flight.GetUniversePosition() - Surface->Centre).GetSafeNormal() : FVector::UpVector;
    }

    struct FFlown
    {
        TArray<EGroundContact> Contacts;
        double Seconds = 0.0;
        /** The deepest any footprint point went under the ground, cm. */
        double DeepestCm = 0.0;
        /** Downward speed at the frame the footprint first came within 1 cm. */
        double ContactSpeed = -1.0;
        /** Settling and descending: the most the sideways speed exceeded the
         *  settle's cap, cm/s (one substep's chase allowed). */
        double WorstSideways = 0.0;
    };

    /** Frames until Landed or MaxSeconds; Each runs before every frame. */
    FFlown Fly(FShipFlightState& Flight, double MaxSeconds, TFunctionRef<void(FShipFlightState&, double)> Each)
    {
        FFlown Out;
        Out.Contacts.Add(Flight.GetContact());
        while (Out.Seconds < MaxSeconds && Flight.GetContact() != EGroundContact::Landed)
        {
            Each(Flight, Out.Seconds);
            Flight.Step(Frame);
            Out.Seconds += Frame;
            if (Flight.GetContact() != Out.Contacts.Last())
            {
                Out.Contacts.Add(Flight.GetContact());
            }
            const double Clearance = Flight.GetFootprintClearance().Get(TNumericLimits<double>::Max());
            Out.DeepestCm = FMath::Max(Out.DeepestCm, -Clearance);
            if (Out.ContactSpeed < 0.0 && Clearance <= 1.0)
            {
                Out.ContactSpeed = FMath::Max(0.0, -Flight.GetVerticalSpeed());
            }
            if (Flight.GetContact() == EGroundContact::Settling && Flight.GetVerticalSpeed() < 0.0)
            {
                const FVector Up = RadialUp(Flight);
                const FVector Sideways = Flight.GetVelocity() - Up * (Flight.GetVelocity() | Up);
                const double Cap = ShipLanding::SettleSkimCap(Clearance, Flight.GetLimits().ApproachSeconds)
                    + Flight.GetLimits().LinearAcceleration * FShipFlightState::FixedStep + 1.0;
                Out.WorstSideways = FMath::Max(Out.WorstSideways, Sideways.Size() - Cap);
            }
        }
        return Out;
    }

    /** Full sink, cruise at Throttle, every frame. */
    void Sink(FShipFlightState& Flight, double Throttle)
    {
        Levers(Flight, Throttle, -1.0);
    }

    /** The steepest rise over a 10 m run at Dir, either way across it. */
    double SlopeAt(const FFlightSurface& World, const FVector& Dir)
    {
        const FVector Up = Dir.GetSafeNormal();
        FVector A;
        FVector B;
        Up.FindBestAxisVectors(A, B);
        const double Run = 1000.0;
        const double Lift = World.Radius + 2.0e6;
        const auto GroundRadius = [&World, Lift](const FVector& D)
        {
            return Lift - ShipFlight::GroundAt(World, World.Centre + D.GetSafeNormal() * Lift).Get(0.0);
        };
        const double Here = GroundRadius(Up);
        const double Angle = Run / World.Radius;
        return FMath::Max(FMath::Abs(GroundRadius(Up + A * Angle) - Here), FMath::Abs(GroundRadius(Up + B * Angle) - Here)) / Run;
    }

    /** Baemsekai, home of seed 20260925 (the landing spec's fixture worlds). */
    const FSystemId Baemsekai{ FInt64Vector(-1, -1, 0), 0 };
}

bool FLandingSettleTest::RunTest(const FString& Parameters)
{
    using namespace TouchdownTestLocal;
    const FFlightSurface Flat = LandingFixtures::World(EGroundShape::Flat);
    const FFlightSurface Slope = LandingFixtures::World(EGroundShape::Slope15);

    // Rolled five degrees over flat ground: it settles level onto it.
    {
        FShipFlightState Flight = Over(Flat, PatchUp, 1000.0, 0.0, FQuat(FVector::ForwardVector, FMath::DegreesToRadians(5.0)));
        Fly(Flight, 60.0, [](FShipFlightState& F, double) { Sink(F, 0.0); });
        TestTrue(TEXT("rolled 5 degrees over flat ground, it lands"), Flight.GetContact() == EGroundContact::Landed);
        const double Off = AngleDeg(BodyUp(Flight), Flight.GetRestPlane().Normal);
        TestTrue(FString::Printf(TEXT("level on the ground (%.3f deg off the rest plane)"), Off), Off <= 0.1);
    }

    // Upright over a 15-degree slope: it tilts onto it, all the way.
    {
        FShipFlightState Flight = Over(Slope, PatchUp, 1000.0);
        Fly(Flight, 60.0, [](FShipFlightState& F, double) { Sink(F, 0.0); });
        TestTrue(TEXT("over a 15-degree slope, it lands"), Flight.GetContact() == EGroundContact::Landed);
        const double Tilt = AngleDeg(BodyUp(Flight), RadialUp(Flight));
        TestTrue(FString::Printf(TEXT("sitting on the slope (%.2f deg off the vertical)"), Tilt), Tilt >= 13.0 && Tilt <= 17.0);
        const double Off = AngleDeg(BodyUp(Flight), Flight.GetRestPlane().Normal);
        TestTrue(FString::Printf(TEXT("on its rest plane (%.3f deg off)"), Off), Off <= 0.1);
    }

    // Cruise full ahead all the way down: it still lands, and never skids in.
    {
        FShipFlightState Flight = Over(Flat, PatchUp, 1000.0);
        const FFlown Flown = Fly(Flight, 90.0, [](FShipFlightState& F, double) { Sink(F, 1.0); });
        TestTrue(TEXT("cruising full ahead as it comes down, it lands"), Flight.GetContact() == EGroundContact::Landed);
        TestTrue(FString::Printf(TEXT("and slows sideways with its clearance (worst %.2f cm/s over)"), Flown.WorstSideways),
                 Flown.WorstSideways <= 0.0);
    }

    // The pilot's yaw is theirs through the settle: two seconds of it,
    // hovering in the band, turn the nose.
    {
        FShipFlightState Flight = Over(Slope, PatchUp, 1000.0);
        const FVector NoseBefore = Flight.GetUniverseOrientation().GetForwardVector();
        double SettlingFor = 0.0;
        Fly(Flight, 90.0, [&SettlingFor](FShipFlightState& F, double)
        {
            if (F.GetContact() == EGroundContact::Settling)
            {
                SettlingFor += Frame;
            }
            const bool bYawing = SettlingFor > 0.0 && SettlingFor <= 2.0;
            Levers(F, 0.0, bYawing ? 0.0 : -1.0, bYawing ? FVector(0.0, 0.0, 1.0) : FVector::ZeroVector);
        });
        const FVector Up = RadialUp(Flight);
        const FVector Before = (NoseBefore - Up * (NoseBefore | Up)).GetSafeNormal();
        const FVector NoseAfter = Flight.GetUniverseOrientation().GetForwardVector();
        const FVector After = (NoseAfter - Up * (NoseAfter | Up)).GetSafeNormal();
        const double Turned = AngleDeg(Before, After);
        TestTrue(TEXT("yawing through the settle, it still lands"), Flight.GetContact() == EGroundContact::Landed);
        TestTrue(FString::Printf(TEXT("and the pilot's yaw turned it (%.1f deg)"), Turned), Turned >= 15.0);
    }

    // The pilot's pitch adds to the settle: held against it, the ship does
    // not land; let go, it does.
    {
        FShipFlightState Flight = Over(Slope, PatchUp, 1000.0);
        double SettlingFor = 0.0;
        bool bLandedWhileHeld = false;
        const FFlown Flown = Fly(Flight, 90.0, [&SettlingFor, &bLandedWhileHeld](FShipFlightState& F, double)
        {
            if (F.GetContact() == EGroundContact::Settling)
            {
                SettlingFor += Frame;
            }
            const bool bHolding = SettlingFor > 0.0 && SettlingFor <= 3.0;
            bLandedWhileHeld |= bHolding && F.GetContact() == EGroundContact::Landed;
            Levers(F, 0.0, -1.0, bHolding ? FVector(0.0, 1.0, 0.0) : FVector::ZeroVector);
        });
        TestFalse(TEXT("while the pilot holds the nose down against it, it does not land"), bLandedWhileHeld);
        TestTrue(TEXT("let go, it lands"), Flight.GetContact() == EGroundContact::Landed);
        TestTrue(FString::Printf(TEXT("and no corner went into the ground (%.2f cm)"), Flown.DeepestCm),
                 Flown.DeepestCm <= ShipLanding::LandedPenetrationCm);
    }
    return true;
}

bool FLandingContactOnceTest::RunTest(const FString& Parameters)
{
    using namespace TouchdownTestLocal;
    int32 Flights = 0;
    int32 Wrong = 0;
    const auto LandOnce = [this, &Flights, &Wrong](const FFlightSurface& Surface, const FVector& Dir, double Yaw, const FString& Where)
    {
        FShipFlightState Flight = Over(Surface, Dir, 850.0, Yaw);
        const FFlown Flown = Fly(Flight, 60.0, [](FShipFlightState& F, double) { Sink(F, 0.0); });
        bool bOnce = Flown.Contacts.Num() == 3 && Flown.Contacts[0] == EGroundContact::Airborne
            && Flown.Contacts[1] == EGroundContact::Settling && Flown.Contacts[2] == EGroundContact::Landed;
        // And it stays: two more seconds, the levers as touchdown left them.
        for (int32 More = 0; More < 60 && bOnce; ++More)
        {
            Flight.Step(Frame);
            bOnce &= Flight.GetContact() == EGroundContact::Landed;
        }
        const bool bGentle = Flown.DeepestCm <= ShipLanding::LandedPenetrationCm && Flown.ContactSpeed >= 0.0
            && Flown.ContactSpeed <= Flight.GetLimits().TouchdownSpeed + 1e-6;
        ++Flights;
        if (!(bOnce && bGentle))
        {
            ++Wrong;
            if (Wrong <= 5)
            {
                AddError(FString::Printf(TEXT("%s: %d contact states, deepest %.2f cm, contact at %.2f cm/s, %.1f s"),
                                         *Where, Flown.Contacts.Num(), Flown.DeepestCm, Flown.ContactSpeed, Flown.Seconds));
            }
        }
    };

    for (const EGroundShape Shape : { EGroundShape::Flat, EGroundShape::Slope15, EGroundShape::CraterRim,
                                      EGroundShape::Ridge, EGroundShape::CrossedSines })
    {
        LandOnce(LandingFixtures::World(Shape), PatchUp, 0.3, FString::Printf(TEXT("fixture %d"), static_cast<int32>(Shape)));
    }

    // Real ground: Baemsekai IV and III, a hundred patches each -- the twenty
    // steepest of a thousand sampled, then eighty anywhere.
    FRandomStream Rng(20260927);
    for (const int32 Orbit : { 3, 2 })
    {
        const FFlightSurface World = LandingFixtures::RealWorld(Baemsekai, Orbit);
        if (!TestTrue(FString::Printf(TEXT("Baemsekai orbit %d has ground"), Orbit), World.HasGround()))
        {
            continue;
        }
        struct FPatch
        {
            FVector Dir;
            double Slope;
        };
        TArray<FPatch> Sampled;
        for (int32 Sample = 0; Sample < 1000; ++Sample)
        {
            const FVector Dir = Rng.GetUnitVector();
            Sampled.Add({ Dir, SlopeAt(World, Dir) });
        }
        Sampled.Sort([](const FPatch& L, const FPatch& R) { return L.Slope > R.Slope; });
        AddInfo(FString::Printf(TEXT("orbit %d: steepest sampled slope %.3f"), Orbit, Sampled[0].Slope));
        for (int32 Steep = 0; Steep < 20; ++Steep)
        {
            LandOnce(World, Sampled[Steep].Dir, Rng.FRandRange(0.0f, 2.0f * UE_PI),
                     FString::Printf(TEXT("orbit %d steep patch %d"), Orbit, Steep));
        }
        for (int32 Any = 0; Any < 80; ++Any)
        {
            LandOnce(World, Rng.GetUnitVector(), Rng.FRandRange(0.0f, 2.0f * UE_PI),
                     FString::Printf(TEXT("orbit %d patch %d"), Orbit, Any));
        }
    }
    AddInfo(FString::Printf(TEXT("%d flights"), Flights));
    TestEqual(TEXT("every flight settles, lands once, gently, and stays"), Wrong, 0);
    return true;
}

#endif
```

- [ ] **Step 2: Build to prove the tests fail.** `./build.sh`. Expected: FAIL, `'GetContact': is not a member of 'FShipFlightState'`.

- [ ] **Step 3: Write the declarations.** In `Source/DeepSpace/Ship/ShipFlightState.h`, make sure `#include "Ship/ShipLanding.h"` is among the includes, then in `FShipFlightLimits` after `DriveThrust`:

```cpp
    /** The settle's band, cm (landing decision 11): under this much footprint
     *  clearance, having come down into it, the ship is Settling and turns
     *  onto the ground's rest plane. ds.Land.SettleBand, set by the
     *  subsystem. */
    double SettleBandCm = ShipLanding::DefaultSettleBandCm;

    /** The settle's fastest turn, rad/s, at a foot's touch, in proportion to
     *  1 - clearance / band above it. ds.Land.SettleDegPerSec. */
    double SettleRadPerSecond = FMath::DegreesToRadians(ShipLanding::DefaultSettleDegPerSec);
```

In `FShipFlightState`'s public section, after `GetDriveNotchCount()`:

```cpp
    /** Airborne, Settling or Landed (landing decision 11). Changed only inside
     *  the substep, so it is no new write path; SetUniverseTransform and
     *  JumpTo put it back to Airborne. */
    EGroundContact GetContact() const;

    /** The rest plane the settle turns toward, as last found under the gear,
     *  universe axes relative to the ship's origin at the time. Meaningful
     *  while Settling or Landed. */
    const ShipLanding::FRestPlane& GetRestPlane() const;

    /** Bumps at every touchdown (Settling to Landed). */
    int32 GetTouchdownCount() const;

    /** The solid world whose ground is nearest, a pointer into the surfaces
     *  (NearestGround's); null with none. */
    const FFlightSurface* GetGroundSurface() const;
```

In the private section, after `double LastHeldFraction = 0.0;`:

```cpp
    /** The ground point under each gear foot, relative to the ship's origin
     *  in universe axes, each foot's height above it (cm, negative under),
     *  and the local vertical. False with no solid ground in reach. */
    bool FeetOverGround(FVector (&Ground)[4], double (&FootGap)[4], FVector& Up) const;

    /** The settle's turn this substep, added to the pilot's: roll and pitch
     *  onto the rest plane. Only while Settling. */
    void ApplySettle(double FixedDelta);

    /** The contact rules, at the end of every substep. */
    void UpdateContact();

    /** Settling to Landed: at rest exactly, cruise to STOP, the vertical
     *  lever to HOVER. */
    void Touchdown();

    EGroundContact Contact = EGroundContact::Airborne;
    ShipLanding::FRestPlane RestPlaneFound;
    int32 TouchdownCount = 0;
```

- [ ] **Step 4: Write the implementation.** In `Source/DeepSpace/Ship/ShipFlightState.cpp`:

(a) In `SubStep`, directly after the attitude block's closing `}` (the `Orientation.Normalize();` block) and before the drive/cruise dispatch, insert:

```cpp
    // The settle (landing decision 11): added to the pilot's turn, never in
    // place of it, and never in yaw.
    if (Contact == EGroundContact::Settling)
    {
        ApplySettle(FixedDelta);
    }
```

(b) Replace the dispatch at the end of `SubStep` so every substep, whichever lever flew it, ends in the contact rules. Keep slice (b)'s condition in front of `DriveSubStep` exactly as it is; in speed-bands' form that is:

```cpp
    const bool bDriveFlew = (Command.bDrive || bSpoolingDown) && DriveSubStep(FixedDelta);
    if (!bDriveFlew)
    {
        CruiseSubStep(FixedDelta);
    }
    UpdateContact();
}
```

(c) In `CruiseSubStep`'s regime block (Task F6), after the `if (World->HasGround() && Horizontal > 0.0)` block's closing brace and before `Plan = Heading * Horizontal + Up * AskedVerticalRate();`, insert:

```cpp
        // Settling and descending, the sideways speed falls with the
        // clearance, so the ship does not skid in (decision 11); hovering in
        // the band keeps the skim cap's.
        if (Contact == EGroundContact::Settling && GetVerticalSpeed() < 0.0)
        {
            Horizontal = FMath::Min(Horizontal, ShipLanding::SettleSkimCap(GetFootprintClearance().Get(0.0), Limits.ApproachSeconds));
        }
```

(d) Append at the end of the file:

```cpp
const FFlightSurface* FShipFlightState::GetGroundSurface() const
{
    const int32 Index = NearestGround();
    return Index == INDEX_NONE ? nullptr : &Surfaces[Index];
}

bool FShipFlightState::FeetOverGround(FVector (&Ground)[4], double (&FootGap)[4], FVector& Up) const
{
    const FFlightSurface* Surface = GetGroundSurface();
    if (!Surface || !Surface->HasGround())
    {
        return false;
    }
    // One vertical for the four feet: across the gear's 23 m the radial turns
    // by 4e-6 rad over an Earth, and the rest plane is defined against one Up.
    Up = (Position - Surface->Centre).GetSafeNormal();
    const double Clearance = Limits.GearClearanceCm;
    for (int32 Foot = 0; Foot < 4; ++Foot)
    {
        const FVector Offset = Orientation.RotateVector(
            FVector(ShipLanding::GearFeetXY[Foot][0], ShipLanding::GearFeetXY[Foot][1], -Clearance));
        FootGap[Foot] = ShipFlight::GroundAt(*Surface, Position + Offset).Get(0.0);
        Ground[Foot] = Offset - Up * FootGap[Foot];
    }
    return true;
}

void FShipFlightState::ApplySettle(double FixedDelta)
{
    const FVector Rate = ShipLanding::SettleRate(Orientation, RestPlaneFound.Normal,
                                                 GetFootprintClearance().Get(TNumericLimits<double>::Max()),
                                                 Limits.SettleBandCm, Limits.SettleRadPerSecond, FixedDelta);
    const double Speed = Rate.Size();
    if (Speed * FixedDelta > UE_DOUBLE_SMALL_NUMBER)
    {
        // Body axes, as the pilot's turn is: right-multiplied.
        Orientation = Orientation * FQuat(Rate / Speed, Speed * FixedDelta);
        Orientation.Normalize();
    }
}

void FShipFlightState::UpdateContact()
{
    const FFlightSurface* Surface = GetGroundSurface();
    if (!Surface || !Surface->HasGround())
    {
        Contact = EGroundContact::Airborne;
        return;
    }
    const double Clearance = GetFootprintClearance().Get(TNumericLimits<double>::Max());
    if (Contact == EGroundContact::Airborne && Clearance >= Limits.SettleBandCm)
    {
        // Nothing above the band can change the contact: four ground samples
        // a substep saved on every flight over a world.
        return;
    }
    FVector Ground[4];
    double Gap[4];
    FVector Up;
    FeetOverGround(Ground, Gap, Up);
    RestPlaneFound = ShipLanding::RestPlane(Ground, Up);

    ShipLanding::FContactInputs In;
    In.bHasGround = true;
    In.FootprintClearanceCm = Clearance;
    In.VerticalSpeedCmPerS = GetVerticalSpeed();
    In.bLeverAsksClimb = Command.Vertical > 0.0;
    In.SettleBandCm = Limits.SettleBandCm;
    for (const double FootGap : Gap)
    {
        In.DeepestFootUnderCm = FMath::Max(In.DeepestFootUnderCm, -FootGap);
    }
    for (const int32 Foot : RestPlaneFound.Tripod)
    {
        In.TripodGapCm = FMath::Max(In.TripodGapCm, FMath::Abs(Gap[Foot]));
    }
    In.bOriginInsideTripod = RestPlaneFound.bOriginInside;

    const EGroundContact Was = Contact;
    Contact = ShipLanding::NextContact(Contact, In);
    if (Was != EGroundContact::Landed && Contact == EGroundContact::Landed)
    {
        Touchdown();
    }
}

void FShipFlightState::Touchdown()
{
    // At rest, exactly, and landing is a stop (decision 11): cruise to STOP
    // and the vertical lever to HOVER. The drive notch keeps its setting, as
    // it does across F.
    Velocity = FVector::ZeroVector;
    AngularVelocity = FVector::ZeroVector;
    LastLinearAcceleration = FVector::ZeroVector;
    LastAngularAcceleration = FVector::ZeroVector;
    Command.Throttle = 0.0;
    Command.Vertical = 0.0;
    ++TouchdownCount;
}

EGroundContact FShipFlightState::GetContact() const { return Contact; }
const ShipLanding::FRestPlane& FShipFlightState::GetRestPlane() const { return RestPlaneFound; }
int32 FShipFlightState::GetTouchdownCount() const { return TouchdownCount; }
```

- [ ] **Step 5: Build and run.** `./build.sh` (a header change: expected `Result: Succeeded` after a longer compile). Then
  `./test.sh DeepSpace.Ship.Landing.Settle` and `./test.sh DeepSpace.Ship.Landing.ContactOnce`. Expected: `passed: 1` each. Then `./test.sh DeepSpace.Ship.Landing` for the whole group, slice (b)'s `GroundAlwaysCatches` included. Expected: every test passes.

- [ ] **Step 6: Move the tests that pinned "no LANDED yet".** Run `./test.sh DeepSpace`. Slice (b)'s done-when had the ship come to rest with a foot on the ground reading `· HOVERING`; after a descent that now reads `LANDED`, and the levers go to STOP/HOVER. For every test that fails on exactly that, change the expectation in its file to the landed one (`GetContact() == EGroundContact::Landed`, the corner containing `LANDED`, `GetCommand().Vertical == 0.0`) and nothing else. Expected afterwards: the whole suite green.

- [ ] **Step 7: Commit.**

```bash
git add Source/DeepSpace/Ship/ShipFlightState.h Source/DeepSpace/Ship/ShipFlightState.cpp Source/DeepSpace/Tests/LandingTestFixtures.h Source/DeepSpace/Tests/ShipTouchdownTest.cpp
git add -u Source/DeepSpace/Tests
git commit -F - <<'EOF'
feat(landing): the ship settles onto the ground's slope and touches down

The flight state holds EGroundContact and changes it only inside the
substep. Coming down into ds.Land.SettleBand it turns body +Z onto the rest
plane under its gear, the pilot's input added and yaw left alone, and slows
sideways with its clearance; on its tripod and at rest it is Landed, with
velocity and turn zero, cruise at STOP and the vertical lever at HOVER.

Tests that read HOVERING at rest on the ground after a descent now read
LANDED: that is the slice.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

- [ ] **Step 8: Prove the settle and the contact can fail.**

```bash
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'Limits.SettleBandCm, Limits.SettleRadPerSecond, FixedDelta);' 'Limits.SettleBandCm, 0.0, FixedDelta);' DeepSpace.Ship.Landing.Settle
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'In.VerticalSpeedCmPerS = GetVerticalSpeed();' 'In.VerticalSpeedCmPerS = -GetVerticalSpeed();' DeepSpace.Ship.Landing.ContactOnce
```

Expected: both `KILLED` (with no turn the 15-degree slope never rests on its
tripod; read upside down, a descending ship never settles). Then `./build.sh`.

---

## Task 42 (C3): the LANDED latch, no taxiing, take-off, and the re-seat

Landed, the ship is held exactly at rest; the cruise lever cannot leave STOP
and the vertical lever cannot ask a sink; a climb lifts it at once, starved or
not; ground that moves under it (a priors reload) re-seats it; ground that
goes (the world became an ocean) makes it fly; `PlaceShip` and `JumpTo` put it
in the air.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipFlightState.h` (private: `HoldLanded`)
- Modify: `Source/DeepSpace/Ship/ShipFlightState.cpp` (`SetCommand` after its clamps; `SubStep` first lines; `JumpTo`; `SetUniverseTransform`; `HoldLanded` at the end)
- Modify: `Source/DeepSpace/Tests/ShipTouchdownTest.cpp` (four tests: `TakeOff`, `LandedLatch`,
  `LandedFarAway` (review focus 3), `LandedGear` (review focus 4))

**Interfaces:**
- Consumes: Task C2's contact and `LandingTestFixtures.h`; slice (b)'s `TOptional<double> GetFootprintClearance() const`, `TOptional<double> GetGroundAltitude() const`, `FShipFlightLimits::GearClearanceCm`, `GroundFixtures::Somewhere()`.
- Produces: the latch's behaviour behind `GetContact()`; nothing new in any header's public section.

- [ ] **Step 1: Write the failing tests.** In `ShipTouchdownTest.cpp`, add beside the other two declarations:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingTakeOffTest, "DeepSpace.Ship.Landing.TakeOff",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingLandedLatchTest, "DeepSpace.Ship.Landing.LandedLatch",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingLandedFarAwayTest, "DeepSpace.Ship.Landing.LandedFarAway",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingLandedGearTest, "DeepSpace.Ship.Landing.LandedGear",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

(`LandedLatch`, `LandedFarAway` and `LandedGear` are siblings -- no path is a prefix of another -- so
each review focus's mutant is attributed to its own test.)

Inside `namespace TouchdownTestLocal`, after `Sink`:

```cpp
    /** Down on full sink from 8.5 m, cruise at Throttle all the way. */
    FShipFlightState LandOn(const FFlightSurface& Surface, const FVector& Dir, double Throttle = 0.0)
    {
        FShipFlightState Flight = Over(Surface, Dir, 850.0, 0.3);
        Fly(Flight, 90.0, [Throttle](FShipFlightState& F, double) { Sink(F, Throttle); });
        return Flight;
    }

    /** Frames, the levers as they are. */
    void Frames(FShipFlightState& Flight, int32 Count)
    {
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Flight.Step(Frame);
        }
    }
```

And after `FLandingContactOnceTest::RunTest`:

```cpp
bool FLandingTakeOffTest::RunTest(const FString& Parameters)
{
    using namespace TouchdownTestLocal;
    for (const EGroundShape Shape : { EGroundShape::Flat, EGroundShape::Slope15 })
    {
        FShipFlightState Flight = LandOn(LandingFixtures::World(Shape), PatchUp);
        if (!TestTrue(FString::Printf(TEXT("fixture %d: landed"), static_cast<int32>(Shape)), Flight.GetContact() == EGroundContact::Landed))
        {
            continue;
        }
        const FUniversePosition Where = Flight.GetUniversePosition();
        const FQuat Facing = Flight.GetUniverseOrientation();
        Frames(Flight, 300);
        TestTrue(TEXT("ten seconds at HOVER: still landed, not a centimetre moved, not a hair turned"),
                 Flight.GetContact() == EGroundContact::Landed && Where.DistanceTo(Flight.GetUniversePosition()) == 0.0
                     && Facing.Equals(Flight.GetUniverseOrientation(), 0.0));

        // A climb asked lifts it at once, and it cannot land again while it asks.
        Levers(Flight, 0.0, 0.5);
        Flight.Step(Frame);
        TestTrue(TEXT("asked to climb, it is airborne at once"), Flight.GetContact() == EGroundContact::Airborne);
        const double Start = Flight.GetGroundAltitude().Get(0.0);
        bool bStayedUp = true;
        for (int32 Index = 0; Index < 300; ++Index)
        {
            Levers(Flight, 0.0, 0.5);
            Flight.Step(Frame);
            bStayedUp &= Flight.GetContact() == EGroundContact::Airborne;
        }
        TestTrue(TEXT("and never settles while it climbs"), bStayedUp);
        TestTrue(FString::Printf(TEXT("it climbed (%.1f m)"), (Flight.GetGroundAltitude().Get(0.0) - Start) / 100.0),
                 Flight.GetGroundAltitude().Get(0.0) - Start >= 300.0);
    }

    // Starved to a quarter thrust, it lifts just the same: thrust scales the
    // lever's acceleration, never whether it climbs.
    {
        FShipFlightState Flight = LandOn(LandingFixtures::World(EGroundShape::Flat), PatchUp);
        FShipFlightLimits Starved = Flight.GetLimits();
        Starved.LinearAcceleration *= 0.25;
        Flight.SetLimits(Starved);
        const double Start = Flight.GetGroundAltitude().Get(0.0);
        Levers(Flight, 0.0, 0.5);
        Frames(Flight, 300);
        TestTrue(TEXT("a quarter thrust, it lifts off and climbs"),
                 Flight.GetContact() == EGroundContact::Airborne && Flight.GetGroundAltitude().Get(0.0) - Start >= 300.0);
    }
    return true;
}

bool FLandingLandedLatchTest::RunTest(const FString& Parameters)
{
    using namespace TouchdownTestLocal;
    const FFlightSurface Flat = LandingFixtures::World(EGroundShape::Flat);
    FShipFlightState Flight = LandOn(Flat, PatchUp, 1.0);
    if (!TestTrue(TEXT("cruising full ahead on the way down, it lands"), Flight.GetContact() == EGroundContact::Landed))
    {
        return false;
    }
    TestEqual(TEXT("touchdown put cruise to STOP"), Flight.GetCommand().Throttle, 0.0);
    TestEqual(TEXT("and the vertical lever to HOVER"), Flight.GetCommand().Vertical, 0.0);
    TestEqual(TEXT("one touchdown counted"), Flight.GetTouchdownCount(), 1);

    // No taxiing: cruise, attitude and a sink all asked, and nothing moves.
    const FUniversePosition Where = Flight.GetUniversePosition();
    const FQuat Facing = Flight.GetUniverseOrientation();
    for (int32 Index = 0; Index < 150; ++Index)
    {
        Levers(Flight, 1.0, -1.0, FVector(1.0, 1.0, 1.0));
        Flight.Step(Frame);
    }
    TestEqual(TEXT("landed, the cruise lever stays at STOP"), Flight.GetCommand().Throttle, 0.0);
    TestEqual(TEXT("and the vertical lever never asks a sink"), Flight.GetCommand().Vertical, 0.0);
    TestTrue(TEXT("and nothing moved or turned"),
             Where.DistanceTo(Flight.GetUniversePosition()) == 0.0 && Facing.Equals(Flight.GetUniverseOrientation(), 0.0)
                 && Flight.GetVelocity().IsZero() && Flight.GetAngularVelocity().IsZero());
    TestEqual(TEXT("still landed"), Flight.GetContact(), EGroundContact::Landed);

    // The ground moves under it, as a priors reload moves it: it re-seats,
    // both ways, along the radial.
    for (const double Shift : { 300.0, -300.0 })
    {
        const double Before = Flight.GetUniversePosition().DistanceTo(Flat.Centre);
        Flight.SetSurfaces({ LandingFixtures::World(EGroundShape::Flat, Flat.Radius + Shift) });
        Flight.Step(Frame);
        const double Moved = Flight.GetUniversePosition().DistanceTo(Flat.Centre) - Before;
        TestEqual(FString::Printf(TEXT("ground %+.0f cm: still landed"), Shift), Flight.GetContact(), EGroundContact::Landed);
        const double Clear = Flight.GetFootprintClearance().Get(TNumericLimits<double>::Max());
        TestTrue(FString::Printf(TEXT("ground %+.0f cm: re-seated on it (moved %.2f cm, clearance %.2f cm)"), Shift, Moved, Clear),
                 FMath::Abs(Moved - Shift) <= 1.0 && FMath::Abs(Clear) <= ShipLanding::LandedPenetrationCm);
        Flight.SetSurfaces({ Flat });
        Flight.Step(Frame);
    }

    // The world becomes an ocean: no ground, and the ship flies again from
    // rest, never lifted by the floor sphere it is now under.
    {
        FFlightSurface Ocean = Flat;
        Ocean.Ground.Reset();
        const FUniversePosition Before = Flight.GetUniversePosition();
        Flight.SetSurfaces({ Ocean });
        Flight.Step(Frame);
        TestEqual(TEXT("with its ground gone it is airborne"), Flight.GetContact(), EGroundContact::Airborne);
        TestTrue(TEXT("and has not jumped"), Before.DistanceTo(Flight.GetUniversePosition()) < 1.0);
    }

    // PlaceShip and JumpTo each put it in the air.
    {
        FShipFlightState Placed = LandOn(Flat, PatchUp);
        Placed.SetUniverseTransform(Placed.GetUniversePosition(), Placed.GetUniverseOrientation());
        TestEqual(TEXT("placed, it is airborne"), Placed.GetContact(), EGroundContact::Airborne);
        FShipFlightState Jumped = LandOn(Flat, PatchUp);
        Jumped.JumpTo(Jumped.GetUniversePosition() + FVector(0.0, 0.0, 1.0e9));
        TestEqual(TEXT("jumped, it is airborne"), Jumped.GetContact(), EGroundContact::Airborne);
    }

    // Review focus 3, far from the origin, is its own test,
    // DeepSpace.Ship.Landing.LandedFarAway, so its mutant is attributed to it.
    return true;
}

bool FLandingLandedFarAwayTest::RunTest(const FString& Parameters)
{
    using namespace TouchdownTestLocal;
    // Review focus 3: far from the universe's origin, across chunks, as the
    // galaxy is -- every fixture in LandedLatch sits at the origin. Landed is
    // still exactly still, and the re-seat still exact.
    {
        const FFlightSurface Far = LandingFixtures::World(EGroundShape::Slope15, 6.0e8, GroundFixtures::Somewhere());
        FShipFlightState Away = LandOn(Far, PatchUp);
        TestEqual(TEXT("far from the origin, it lands"), Away.GetContact(), EGroundContact::Landed);
        const FUniversePosition Where = Away.GetUniversePosition();
        const FQuat Facing = Away.GetUniverseOrientation();
        Frames(Away, 300);
        TestTrue(TEXT("and ten seconds later has not moved or turned at all"),
                 Where.DistanceTo(Away.GetUniversePosition()) == 0.0 && Facing.Equals(Away.GetUniverseOrientation(), 0.0));
        const double Before = Away.GetUniversePosition().DistanceTo(Far.Centre);
        Away.SetSurfaces({ LandingFixtures::World(EGroundShape::Slope15, 6.0e8 + 300.0, GroundFixtures::Somewhere()) });
        Away.Step(Frame);
        TestTrue(FString::Printf(TEXT("far away, a ground 3 m higher re-seats it 3 m higher (%.2f cm)"),
                                 Away.GetUniversePosition().DistanceTo(Far.Centre) - Before),
                 FMath::Abs(Away.GetUniversePosition().DistanceTo(Far.Centre) - Before - 300.0) <= 1.0
                     && Away.GetContact() == EGroundContact::Landed);
    }
    return true;
}

bool FLandingLandedGearTest::RunTest(const FString& Parameters)
{
    using namespace TouchdownTestLocal;
    const FFlightSurface Flat = LandingFixtures::World(EGroundShape::Flat);
    // Review focus 4: the gear changed at the console while landed
    // (ds.Land.GearClearance): the ship re-seats on its new legs, still landed.
    for (const double Change : { 100.0, -100.0 })
    {
        FShipFlightState Legs = LandOn(Flat, PatchUp);
        const double Before = Legs.GetUniversePosition().DistanceTo(Flat.Centre);
        FShipFlightLimits Changed = Legs.GetLimits();
        Changed.GearClearanceCm += Change;
        Legs.SetLimits(Changed);
        Legs.Step(Frame);
        const double Rose = Legs.GetUniversePosition().DistanceTo(Flat.Centre) - Before;
        const double Clear = Legs.GetFootprintClearance().Get(TNumericLimits<double>::Max());
        TestEqual(FString::Printf(TEXT("gear %+.0f cm: still landed"), Change), Legs.GetContact(), EGroundContact::Landed);
        TestTrue(FString::Printf(TEXT("gear %+.0f cm: re-seated by as much (%.2f cm, clearance %.2f cm)"), Change, Rose, Clear),
                 FMath::Abs(Rose - Change) <= 1.0 && FMath::Abs(Clear) <= ShipLanding::LandedPenetrationCm);
    }
    return true;
}
```

- [ ] **Step 2: Run to prove they fail.** `./build.sh` (expected: Succeeded), then `./test.sh DeepSpace.Ship.Landing.LandedLatch`. Expected: FAIL at "landed, the cruise lever stays at STOP" (nothing clamps it yet) and at "placed, it is airborne". `./test.sh DeepSpace.Ship.Landing.LandedFarAway` and `./test.sh DeepSpace.Ship.Landing.LandedGear`: FAIL at "has not moved or turned at all" and "re-seated by as much" (nothing holds or re-seats a landed ship yet). `./test.sh DeepSpace.Ship.Landing.TakeOff`. Expected: FAIL at "not a centimetre moved" or "asked to climb, it is airborne at once" (Landed latches, but nothing yet holds the ship or releases it on a climb).

- [ ] **Step 3: Write the implementation.** In `ShipFlightState.h`, private, after `void Touchdown();`:

```cpp
    /** The landed substep: at rest exactly, re-seated on ground that moved
     *  under it. False when the ship flies this substep instead -- a climb
     *  asked, or no ground in reach -- having set it Airborne. */
    bool HoldLanded();
```

In `ShipFlightState.cpp`:

(a) `SetCommand`, after the line that clamps `Command.bDrive` (and slice (b)'s clamp of `Command.Vertical`), before the drive-toggle logic:

```cpp
    if (Contact == EGroundContact::Landed)
    {
        // No taxiing (decision 11): landed, the cruise lever stays at STOP and
        // the vertical lever never asks a sink. Only a climb moves anything.
        Command.Throttle = 0.0;
        Command.Vertical = FMath::Max(0.0, Command.Vertical);
    }
```

(b) First lines of `SubStep`, before the attitude block:

```cpp
    // Landed: at rest, exactly, until the pilot asks to climb (decision 11).
    if (Contact == EGroundContact::Landed && HoldLanded())
    {
        return;
    }
```

(c) In `JumpTo`, after `LastHeldFraction = 0.0;`, and in `SetUniverseTransform`, after `Orientation = NewOrientation.GetNormalized();`:

```cpp
    Contact = EGroundContact::Airborne;
```

(d) Append:

```cpp
bool FShipFlightState::HoldLanded()
{
    FVector Ground[4];
    double Gap[4];
    FVector Up;
    if (Command.Vertical > 0.0 || !FeetOverGround(Ground, Gap, Up))
    {
        // Take-off -- a fresh climb, starved or not, since the starved sink is
        // never applied to a climb -- or the ground has gone from under the
        // ship (a priors reload made the world an ocean): it flies again,
        // from rest, this substep.
        Contact = EGroundContact::Airborne;
        return false;
    }
    Velocity = FVector::ZeroVector;
    AngularVelocity = FVector::ZeroVector;
    LastLinearAcceleration = FVector::ZeroVector;
    LastAngularAcceleration = FVector::ZeroVector;
    DrivePosition = 0.0;
    LastHold = EFlightHold::Free;
    LastHeldFraction = 0.0;

    // The ground under a landed ship changes only when the world does (or
    // the gear's length, at the console). The ground is drawn, so the ship
    // sits on it: along the radial, up or down. If the new ground no longer
    // carries its tripod, it settles again.
    const double Least = GetFootprintClearance().Get(0.0);
    if (FMath::Abs(Least) > ShipLanding::LandedPenetrationCm)
    {
        Position = Position + Up * (-Least);
        FeetOverGround(Ground, Gap, Up);
        RestPlaneFound = ShipLanding::RestPlane(Ground, Up);
        double TripodGap = 0.0;
        for (const int32 Foot : RestPlaneFound.Tripod)
        {
            TripodGap = FMath::Max(TripodGap, FMath::Abs(Gap[Foot]));
        }
        if (TripodGap > ShipLanding::TripodToleranceCm)
        {
            Contact = EGroundContact::Settling;
        }
    }
    return true;
}
```

- [ ] **Step 4: Build and run.** `./build.sh`; `./test.sh DeepSpace.Ship.Landing.TakeOff`; `./test.sh DeepSpace.Ship.Landing.LandedLatch`; then `./test.sh DeepSpace.Ship.Landing`. Expected: all pass.

- [ ] **Step 5: Commit.**

```bash
git add Source/DeepSpace/Ship/ShipFlightState.h Source/DeepSpace/Ship/ShipFlightState.cpp Source/DeepSpace/Tests/ShipTouchdownTest.cpp
git commit -F - <<'EOF'
feat(landing): LANDED holds the ship until a fresh climb, and never taxis

Landed, the ship is held exactly at rest and its cruise lever cannot leave
STOP nor the vertical lever ask a sink. A climb lifts it the same substep,
at a quarter thrust too. Ground that moves under it re-seats it along the
radial; ground that goes lets it fly; PlaceShip and JumpTo put it in the air.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

- [ ] **Step 6: Prove both tests can fail.**

```bash
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'if (Command.Vertical > 0.0 || !FeetOverGround(Ground, Gap, Up))' 'if (Command.Vertical > 2.0 || !FeetOverGround(Ground, Gap, Up))' DeepSpace.Ship.Landing.TakeOff
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'Command.Vertical = FMath::Max(0.0, Command.Vertical);' 'Command.Vertical = Command.Vertical;' DeepSpace.Ship.Landing.LandedLatch
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'Position = Position + Up * (-Least);' 'Position = Position;' DeepSpace.Ship.Landing.LandedLatch
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'Position = Position + Up * (-Least);' 'Position = Position;' DeepSpace.Ship.Landing.LandedGear
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'Position = Position + Up * (-Least);' 'Position = FUniversePosition(FVector(FVector3f(Position.ToVector() + Up * (-Least))));' DeepSpace.Ship.Landing.LandedFarAway
```

Expected: all five `KILLED`. The third: no re-seat for a moved ground near the origin; the fourth:
none for a changed gear (review focus 4); the fifth is focus 3's own precision failure -- the re-seat
taken through a float absolute position. Near the origin a float would hold a planet's
position to a metre, but at `GroundFixtures::Somewhere()`, chunks (3, -2, 0) out, a float's step is
tens of kilometres, so only the far-away test can see it. Then `./build.sh`.

---

## Task 43 (C4): the subsystem -- the settle's tunables, a landed ship's hold is free, the fold held while settling

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.cpp` (CVars beside the other `ds.Land.*` ones; `ApplyAllocation` before `FlightState.SetLimits(Limits);` and where `HoldWant`'s scope is read; `StepNavigation`'s `OffBoresight`)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` (the write-path comment on `GetFlightState`)
- Create: `Source/DeepSpace/Tests/LandingPlaytest.h`
- Create: `Source/DeepSpace/Tests/LandingSubsystemTest.cpp`
- Modify: `Source/DeepSpace/Tests/JumpWindsTest.cpp` (a landed block before `Fresh->EndPlay(EEndPlayReason::Quit);`)

**Interfaces:**
- Consumes: `FShipFlightState::GetContact()`, `FShipFlightLimits::SettleBandCm`, `SettleRadPerSecond` (Tasks C2-C3); `ShipLanding::MinSettleBandCm` (C1); slice (b)'s `float ShipPower::HoldWant(double GravityCmS2, double DepthUnderFloorCm, float WattsPerG, bool bAirborne)` as `ApplyAllocation` calls it (S2), `bool UShipSubsystem::SetVerticalLever(APawn*, double)` (S4), `float UShipSubsystem::GetHoldWatts() const` (S2), `static double UShipSubsystem::FloorFor(const FSkyBody&)` (S1); slice (a)'s `FWorldRelief`, `FSkyBody::Relief`.
- Produces: CVars `ds.Land.SettleBand`, `ds.Land.SettleDegPerSec`; `Tests/LandingPlaytest.h`.

- [ ] **Step 1: Write the shared test scaffolding.** Create `Source/DeepSpace/Tests/LandingPlaytest.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/ShipLanding.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkySystem.h"
#include "Surface/WorldRelief.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"

/**
 * The landing spec's fixture worlds, and placing the ship over their real
 * ground, for slice (c)'s subsystem and Playtest tests. Each world is named by
 * seed (the ini's 20260925), system and orbit, and the tests check its name
 * and kind, so priors that move it fail loudly rather than landing somewhere
 * else. Test scaffolding: nothing outside Tests/ may include this.
 */
namespace LandingPlaytest
{
    /** Home: sector (-1, -1, 0), slot 0. */
    inline const FSystemId Baemsekai{ FInt64Vector(-1, -1, 0), 0 };

    /** 6.69 ly out: sector (0, 0, 1), slot 0. */
    inline const FSystemId Gasfe{ FInt64Vector(0, 0, 1), 0 };

    inline constexpr int32 OrbitIII = 2;
    inline constexpr int32 OrbitIV = 3;
    inline constexpr int32 OrbitV = 4;
    inline constexpr int32 OrbitVII = 6;

    /** A frame at 30 Hz, the slowest the game is expected to run. */
    inline constexpr float Dt = 1.0f / 30.0f;

    struct FWorldAt
    {
        FStarSystem System;
        FPlanet Planet;
        /** As the sky and the flight have it: body Orbit + 1. */
        FSkyBody Body;
        FSkyBody Star;
    };

    inline TOptional<FWorldAt> WorldAt(const UUniverseSubsystem& Universe, const FSystemId& Id, int32 Orbit)
    {
        const TOptional<FStarSystem> System = Universe.GetSystem(Id);
        if (!System || !System->Planets.IsValidIndex(Orbit))
        {
            return {};
        }
        const FSkySystem Sky = LocalSystem::Here(TOptional<FStarSystem>(*System));
        if (!Sky.Bodies.IsValidIndex(Orbit + 1))
        {
            return {};
        }
        return FWorldAt{ *System, System->Planets[Orbit], Sky.Bodies[Orbit + 1], Sky.Bodies[0] };
    }

    /** Unit, from the world's centre toward its star: the day side. */
    inline FVector DaySide(const FWorldAt& World)
    {
        return (World.Star.Position - World.Body.Position).GetSafeNormal();
    }

    /** The ground's distance from the world's centre at Up, cm, as the
     *  flight has it: the datum plus WorldRelief's height. */
    inline double GroundRadius(const FWorldAt& World, const FVector& Up)
    {
        return World.Body.Relief.RadiusCm + FWorldRelief(World.Body.Relief).Height(Up.GetSafeNormal());
    }

    /** The steepest rise over a 10 m run at Up, either way across it. */
    inline double SlopeAt(const FWorldAt& World, const FVector& Up)
    {
        const FVector U = Up.GetSafeNormal();
        FVector A;
        FVector B;
        U.FindBestAxisVectors(A, B);
        const double Run = 1000.0;
        const double Angle = Run / World.Body.Relief.RadiusCm;
        const double Here = GroundRadius(World, U);
        return FMath::Max(FMath::Abs(GroundRadius(World, U + A * Angle) - Here),
                          FMath::Abs(GroundRadius(World, U + B * Angle) - Here)) / Run;
    }

    /** Of 24 ways round Course, the Up at right angles to it with the
     *  gentlest ground: where the course is level, so the nose can be on it
     *  with the ship upright. */
    inline FVector FlattestUpAcross(const FWorldAt& World, const FVector& Course)
    {
        const FVector C = Course.GetSafeNormal();
        FVector A;
        FVector B;
        C.FindBestAxisVectors(A, B);
        FVector Best = A;
        double BestSlope = TNumericLimits<double>::Max();
        for (int32 Step = 0; Step < 24; ++Step)
        {
            const double Theta = Step * UE_DOUBLE_TWO_PI / 24.0;
            const FVector Up = A * FMath::Cos(Theta) + B * FMath::Sin(Theta);
            const double Slope = SlopeAt(World, Up);
            if (Slope < BestSlope)
            {
                BestSlope = Slope;
                Best = Up;
            }
        }
        return Best;
    }

    /** The ship OriginAglCm above the ground at Up, upright, the nose along
     *  NoseHint laid flat (any level heading if NoseHint is zero or along
     *  Up). Through PlaceShip, the tools' path. */
    inline void PlaceOver(UShipSubsystem& Ship, const FWorldAt& World, const FVector& Up, double OriginAglCm, const FVector& NoseHint)
    {
        const FVector U = Up.GetSafeNormal();
        FVector Nose = NoseHint - U * (NoseHint | U);
        if (Nose.SizeSquared() < 1e-12)
        {
            FVector Unused;
            U.FindBestAxisVectors(Nose, Unused);
        }
        Ship.PlaceShip(World.Body.Position + U * (GroundRadius(World, U) + OriginAglCm),
                       FRotationMatrix::MakeFromXZ(Nose.GetSafeNormal(), U).ToQuat());
    }

    inline void Console(UWorld* World, const TCHAR* Name, const TArray<FString>& Args = {})
    {
        FOutputDeviceNull Quiet;
        if (IConsoleObject* Command = IConsoleManager::Get().FindConsoleObject(Name))
        {
            if (Command->AsCommand())
            {
                Command->AsCommand()->Execute(Args, World, Quiet);
            }
        }
    }

    /** Ticks the ship until Done or MaxSeconds; whether Done came. */
    inline bool TickUntil(UShipSubsystem& Ship, double MaxSeconds, TFunctionRef<bool()> Done, float Step = Dt)
    {
        for (double Seconds = 0.0; Seconds < MaxSeconds; Seconds += Step)
        {
            if (Done())
            {
                return true;
            }
            Ship.Tick(Step);
        }
        return Done();
    }

    inline bool TickUntilLanded(UShipSubsystem& Ship, double MaxSeconds)
    {
        return TickUntil(Ship, MaxSeconds, [&Ship] { return Ship.GetFlightState().GetContact() == EGroundContact::Landed; });
    }
}
```

- [ ] **Step 2: Write the failing tests.** Create `Source/DeepSpace/Tests/LandingSubsystemTest.cpp`:

```cpp
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipSubsystem.h"
#include "Tests/LandingPlaytest.h"
#include "Tests/SkyTestWorld.h"
#include "UI/NavText.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Slice (c) through the subsystem: the fold from the ground (decision 11,
 * sign-off item 11) and the settle's tunables. Siblings, no children.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingFoldHeldWhileSettlingTest, "DeepSpace.Ship.Landing.FoldHeldWhileSettling",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingSettleTunablesTest, "DeepSpace.Ship.Landing.SettleTunables",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandingFoldHeldWhileSettlingTest::RunTest(const FString& Parameters)
{
    using namespace LandingPlaytest;
    SkyTestWorld::FSkyWorld Test(TEXT("FoldHeldWhileSettlingWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    const FShipFlightState& Flight = Ship->GetFlightState();
    const TOptional<FWorldAt> IV = WorldAt(*Test.Universe, Baemsekai, OrbitIV);
    if (!TestTrue(TEXT("Baemsekai IV, barren, is there"),
                  IV.IsSet() && IV->Planet.Kind == EPlanetKind::Barren && NavText::WorldName(IV->Planet) == TEXT("Baemsekai IV")))
    {
        return false;
    }

    // A star course, and the patch of IV where it is level, the nose on it.
    Ship->Tick(Dt);
    Console(Test.World, TEXT("ds.Nav.Plot"), { TEXT("0") });
    const TOptional<FVector> Course = Ship->GetCourseDirection();
    if (!TestTrue(TEXT("a star is plotted"), Course.IsSet()))
    {
        return false;
    }
    PlaceOver(*Ship, *IV, FlattestUpAcross(*IV, *Course), 1200.0, *Course);

    // Down into the band.
    Ship->SetVerticalLever(Pilot, -1.0);
    TestTrue(TEXT("coming down, it settles"),
             TickUntil(*Ship, 20.0, [&Flight] { return Flight.GetContact() == EGroundContact::Settling; }));

    // Everything the fold waits for -- engaged, charged, the nose on the
    // course -- while the settle is turning the ship.
    TestTrue(TEXT("the jump engages"), Ship->SetJumpEngaged(true));
    Console(Test.World, TEXT("ds.Nav.Charge"));
    Ship->Tick(Dt);
    const TOptional<FVector> Local = Ship->GetCourseDirectionShipLocal();
    TestTrue(TEXT("charged, with the nose inside the cone: only the settle holds it"),
             Ship->GetJumpCharge() >= 1.0f && Local.IsSet() && ShipNav::OffBoresight(*Local) < Ship->GetJumpConeRadians());
    bool bHeld = true;
    TickUntil(*Ship, 60.0, [&Flight, &bHeld, Ship]
    {
        bHeld &= !(Flight.GetContact() == EGroundContact::Settling && Ship->IsInTransit());
        return Flight.GetContact() == EGroundContact::Landed || Ship->IsInTransit();
    });
    TestTrue(TEXT("while settling, the fold never opens"), bHeld && !Ship->IsInTransit());
    TestEqual(TEXT("and the ship lands"), Flight.GetContact(), EGroundContact::Landed);

    // Landed with the nose still on the course: the pilot aimed, so it opens.
    TestTrue(TEXT("landed with the nose on the course, it folds from the ground"),
             TickUntil(*Ship, 2.0, [Ship] { return Ship->IsInTransit(); }));
    TickUntil(*Ship, 30.0, [Ship] { return !Ship->IsInTransit(); });
    TestEqual(TEXT("and arrives in the air"), Flight.GetContact(), EGroundContact::Airborne);

    // Landed with the nose off the course, nothing opens. The pilot lifts off
    // and aims, and it opens in flight.
    Ship->Tick(Dt);
    Console(Test.World, TEXT("ds.Nav.Plot"), { TEXT("0") });
    PlaceOver(*Ship, *IV, DaySide(*IV), 1200.0, FVector::ZeroVector);
    Ship->Tick(Dt);
    Console(Test.World, TEXT("ds.Nav.Plot"), { TEXT("0") });
    const TOptional<FVector> Again = Ship->GetCourseDirection();
    if (!TestTrue(TEXT("a star is plotted again, from home"), Again.IsSet()))
    {
        return false;
    }
    const FVector Up = FlattestUpAcross(*IV, *Again);
    PlaceOver(*Ship, *IV, Up, 1200.0, FVector::CrossProduct(Up, *Again));
    Ship->SetVerticalLever(Pilot, -1.0);
    TestTrue(TEXT("nose off the course, it lands"), TickUntilLanded(*Ship, 60.0));
    Ship->SetJumpEngaged(true);
    Console(Test.World, TEXT("ds.Nav.Charge"));
    TickUntil(*Ship, 5.0, [] { return false; });
    TestFalse(TEXT("landed with the nose off the course, nothing opens"), Ship->IsInTransit());
    Ship->SetVerticalLever(Pilot, 0.5);
    TickUntil(*Ship, 3.0, [] { return false; });
    TestEqual(TEXT("a climb lifts it"), Flight.GetContact(), EGroundContact::Airborne);
    EGroundContact AtFold = EGroundContact::Landed;
    const bool bFolded = TickUntil(*Ship, 30.0, [Ship, Pilot, &Flight, &AtFold]
    {
        if (Ship->IsInTransit())
        {
            return true;
        }
        AtFold = Flight.GetContact();
        const TOptional<FVector> Dir = Ship->GetCourseDirectionShipLocal();
        const float Yaw = Dir && Dir->Y < 0.0 ? -1.0f : 1.0f;
        Ship->SetFlightCommand(Pilot, 0.0f, FVector(0.0, 0.0, Yaw));
        return false;
    });
    TestTrue(TEXT("aimed in flight, it folds"), bFolded);
    TestEqual(TEXT("from the air"), AtFold, EGroundContact::Airborne);
    return true;
}

bool FLandingSettleTunablesTest::RunTest(const FString& Parameters)
{
    SkyTestWorld::FSkyWorld Test(TEXT("SettleTunablesWorld"));
    Test.BeginPlay();
    {
        SkyTestWorld::FScopedCVar Band(TEXT("ds.Land.SettleBand"), 3.0f);
        SkyTestWorld::FScopedCVar Rate(TEXT("ds.Land.SettleDegPerSec"), 2.0f);
        Test.Ship->Tick(LandingPlaytest::Dt);
        TestEqual(TEXT("ds.Land.SettleBand is metres, handed over in cm"), Test.Ship->GetFlightState().GetLimits().SettleBandCm, 300.0, 1e-9);
        TestEqual(TEXT("ds.Land.SettleDegPerSec is degrees, handed over in radians"),
                  Test.Ship->GetFlightState().GetLimits().SettleRadPerSecond, FMath::DegreesToRadians(2.0), 1e-9);
    }
    Test.Ship->Tick(LandingPlaytest::Dt);
    TestEqual(TEXT("and the defaults are the header's"), Test.Ship->GetFlightState().GetLimits().SettleBandCm, ShipLanding::DefaultSettleBandCm, 1e-9);

    // Review focus 5: a band of 0 at the console would mean nothing ever
    // settles, so nothing ever lands. It is held to the least band instead,
    // and the ship still lands under it: the behaviour, not only the number.
    {
        SkyTestWorld::FScopedCVar Zero(TEXT("ds.Land.SettleBand"), 0.0f);
        Test.Ship->Tick(LandingPlaytest::Dt);
        TestEqual(TEXT("ds.Land.SettleBand 0 is held to the least band"), Test.Ship->GetFlightState().GetLimits().SettleBandCm,
                  ShipLanding::MinSettleBandCm, 1e-9);
        const TOptional<LandingPlaytest::FWorldAt> IV = LandingPlaytest::WorldAt(*Test.Universe, LandingPlaytest::Baemsekai, LandingPlaytest::OrbitIV);
        APawn* Pilot = Test.World->SpawnActor<APawn>();
        if (TestTrue(TEXT("Baemsekai IV is there to land on"), IV.IsSet()) && TestNotNull(TEXT("a pilot"), Pilot))
        {
            Test.Ship->SetPilot(Pilot);
            LandingPlaytest::PlaceOver(*Test.Ship, *IV, LandingPlaytest::DaySide(*IV), 3000.0, FVector::ZeroVector);
            Test.Ship->SetVerticalLever(Pilot, -1.0);
            TestTrue(TEXT("with ds.Land.SettleBand 0 at the console, a sink from 30 m still lands"),
                     LandingPlaytest::TickUntilLanded(*Test.Ship, 120.0));
        }
    }
    return true;
}

#endif
```

In `Source/DeepSpace/Tests/JumpWindsTest.cpp` add `#include "GameFramework/Pawn.h"`, `#include "Tests/LandingPlaytest.h"` and `#include "Universe/UniverseSubsystem.h"` to the includes, and insert directly before `Fresh->EndPlay(EEndPlayReason::Quit);`:

```cpp
    // Landed, the hold is off the split (landing decision 5): the boosters
    // want exactly their 450 W again and the stock ship is whole, though the
    // same ship hovering there pays for its hold.
    {
        Default->SetJumpEngaged(false);
        APawn* Pilot = Fresh->SpawnActor<APawn>();
        Default->SetPilot(Pilot);
        const UUniverseSubsystem* Cosmos = Fresh->GetSubsystem<UUniverseSubsystem>();
        const TOptional<LandingPlaytest::FWorldAt> IV = Cosmos
            ? LandingPlaytest::WorldAt(*Cosmos, LandingPlaytest::Baemsekai, LandingPlaytest::OrbitIV)
            : TOptional<LandingPlaytest::FWorldAt>();
        if (TestTrue(TEXT("Baemsekai IV is there to land on"), IV.IsSet()))
        {
            LandingPlaytest::PlaceOver(*Default, *IV, LandingPlaytest::DaySide(*IV), 3000.0, FVector::ZeroVector);
            Default->Tick(0.01f);
            Default->Tick(0.01f);
            TestTrue(FString::Printf(TEXT("hovering under the floor, the boosters want their hold as well (%.0f W)"),
                                     Default->GetConsumerWant(ShipPower::Boosters)),
                     Default->GetConsumerWant(ShipPower::Boosters) > 450.0f);
            Default->SetVerticalLever(Pilot, -1.0);
            TestTrue(TEXT("it lands"), LandingPlaytest::TickUntilLanded(*Default, 120.0));
            Default->Tick(0.01f);
            TestEqual(TEXT("landed, the boosters want exactly 450 W"), Default->GetConsumerWant(ShipPower::Boosters), 450.0f);
            TestEqual(TEXT("and the stock ship is whole"), Default->GetConsumerSatisfaction(ShipPower::Lights), 1.0f, 1e-4f);
            TestEqual(TEXT("the hold's watts are gone"), Default->GetHoldWatts(), 0.0f);
        }
    }
```

- [ ] **Step 3: Run to prove they fail.** `./build.sh` (expected: Succeeded; the CVar lookups are by name at run time). `./test.sh DeepSpace.Ship.Landing.SettleTunables`: expected FAIL -- `FScopedCVar`'s `check(Variable)` names the missing `ds.Land.SettleBand` (a fatal check: the run reports no verdict and `./test.sh` exits non-zero, "no tests ran"). `./test.sh DeepSpace.Ship.Landing.FoldHeldWhileSettling`: expected FAIL at "while settling, the fold never opens". `./test.sh DeepSpace.Ship.JumpCanWindAtFullSpeed`: expected FAIL at "landed, the boosters want exactly 450 W".

- [ ] **Step 4: Write the implementation.** In `Source/DeepSpace/Ship/ShipSubsystem.cpp`:

(a) Beside slice (b)'s `ds.Land.*` CVars (add `#include "Ship/ShipLanding.h"` if absent):

```cpp
static TAutoConsoleVariable<float> CVarSettleBand(
    TEXT("ds.Land.SettleBand"), static_cast<float>(ShipLanding::DefaultSettleBandCm / 100.0),
    TEXT("Metres of footprint clearance under which a descending ship settles onto the ground's rest plane (landing decision 11)."));

static TAutoConsoleVariable<float> CVarSettleDegPerSec(
    TEXT("ds.Land.SettleDegPerSec"), static_cast<float>(ShipLanding::DefaultSettleDegPerSec),
    TEXT("The settle's fastest turn, degrees a second, at a foot's touch (landing decision 11)."));
```

(b) In `ApplyAllocation`, directly before `FlightState.SetLimits(Limits);`:

```cpp
    // The settle's band and rate (decision 11), read at use like every
    // tunable here: metres and degrees at the console, cm and radians inside.
    Limits.SettleBandCm = FMath::Max(static_cast<double>(CVarSettleBand.GetValueOnGameThread()) * 100.0, ShipLanding::MinSettleBandCm);
    Limits.SettleRadPerSecond = FMath::DegreesToRadians(FMath::Max(0.0f, CVarSettleDegPerSec.GetValueOnGameThread()));
```

(c) In `ApplyAllocation`, the `ShipPower::HoldWant(...)` call Task S2 wrote passes `true` for `bAirborne` ("landed is slice (c)'s"). Replace that statement with:

```cpp
    // Landed, the ground holds the ship: no hold is paid and the hum's hold
    // term, which reads the hold's watts delivered, falls silent (decision 5).
    const float WantNow = ShipPower::HoldWant(FlightState.GetLocalGravity().Size(), FlightState.GetDepthUnderDriveFloor(),
                                              FMath::Max(0.0f, CVarHoldWatts.GetValueOnGameThread()),
                                              FlightState.GetContact() != EGroundContact::Landed);
```

and in the comment above it delete "-- landed is slice (c)'s, always airborne here".

(d) In `StepNavigation`, replace the `OffBoresight` line with:

```cpp
    // The fold opens by itself only on the pilot's own aim: while the settle
    // turns the ship it is held, so the game's turn can never be what brings
    // the nose into the cone (decision 11). Landed, the orientation is fixed,
    // and a nose already on the course opens it from the ground.
    const double OffBoresight = Course && FlightState.GetContact() != EGroundContact::Settling
        ? ShipNav::OffBoresight(*Course)
        : UE_DOUBLE_PI;
```

In `Source/DeepSpace/Ship/ShipSubsystem.h`, the comment on `GetFlightState` becomes:

```cpp
    /** Read-only. There is no non-const accessor: the only write paths are
     *  SetFlightCommand, SetHelmInput, AllStop, SetDriveLever,
     *  SetVerticalLever, SetDriveEngaged, ClearPilot and this subsystem's own
     *  tick -- which is where the jump's JumpTo, the settle's turn onto the
     *  ground and a landed ship's re-seat happen (ADR 0005, amended) -- and
     *  that is what makes the state trustworthy. */
```

- [ ] **Step 5: Build and run.** `./build.sh`; then `./test.sh DeepSpace.Ship.Landing.SettleTunables`, `./test.sh DeepSpace.Ship.Landing.FoldHeldWhileSettling`, `./test.sh DeepSpace.Ship.JumpCanWindAtFullSpeed`. Expected: `passed: 1` each.

- [ ] **Step 6: Commit.**

```bash
git add Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/Tests/LandingPlaytest.h Source/DeepSpace/Tests/LandingSubsystemTest.cpp Source/DeepSpace/Tests/JumpWindsTest.cpp
git commit -F - <<'EOF'
feat(landing): a landed ship's hold is free, and the fold waits out the settle

ds.Land.SettleBand and ds.Land.SettleDegPerSec reach the flight state. The
boosters' hold is paid only airborne under a solid world's floor, so landed
they want their 450 W and the stock ship is whole. The fold is held while the
settle turns the ship; landed, it opens only on a nose already on the course.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

- [ ] **Step 7: Prove both guards can fail.**

```bash
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'const double OffBoresight = Course && FlightState.GetContact() != EGroundContact::Settling' 'const double OffBoresight = Course' DeepSpace.Ship.Landing.FoldHeldWhileSettling
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'FlightState.GetContact() != EGroundContact::Landed);' 'true);' DeepSpace.Ship.JumpCanWindAtFullSpeed
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'CVarSettleBand.GetValueOnGameThread()) * 100.0, ShipLanding::MinSettleBandCm);' 'CVarSettleBand.GetValueOnGameThread()) * 100.0, 0.0);' DeepSpace.Ship.Landing.SettleTunables
```

Expected: all three `KILLED` (the third twice over: the band's number, and a sink that never lands). Then `./build.sh`.

---

## Task 44 (C5): the corner says LANDED

**Files:**
- Modify: `Source/DeepSpace/UI/ShipHUDWidget.h` (`GroundLine`'s declaration), `ShipHUDWidget.cpp` (`GroundLine`'s first lines; its call in `AltitudeLineText`, Task S6)
- Create: `Source/DeepSpace/Tests/ShipHUDLandedTest.cpp`

**Interfaces:**
- Consumes: slice (b)'s `static FString UShipHUDWidget::GroundLine(double GroundAltitudeCm, double VerticalSpeedCmPerSecond, EFlightHold Hold)`, `AltitudeWords` (S6); `NavText::Separator`; `FShipFlightState::GetContact()`.
- Produces: `static FString UShipHUDWidget::GroundLine(double GroundAltitudeCm, double VerticalSpeedCmPerSecond, EFlightHold Hold, EGroundContact Contact = EGroundContact::Airborne);`

- [ ] **Step 1: Write the failing test.** Create `Source/DeepSpace/Tests/ShipHUDLandedTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipLanding.h"
#include "UI/ShipHUDWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipHUDLandedTest, "DeepSpace.UI.HUDLanded",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipHUDLandedTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("landed, the corner is the deck's height above the rock, and LANDED"),
              UShipHUDWidget::GroundLine(150.0, 0.0, EFlightHold::Free, EGroundContact::Landed),
              FString(TEXT("1.5 M ABOVE GROUND · LANDED")));
    TestEqual(TEXT("with no hold beside it: nothing is moving"),
              UShipHUDWidget::GroundLine(150.0, 0.0, EFlightHold::HoldingOff, EGroundContact::Landed),
              FString(TEXT("1.5 M ABOVE GROUND · LANDED")));
    TestEqual(TEXT("settling reads as flying does"),
              UShipHUDWidget::GroundLine(420.0, -50.0, EFlightHold::Free, EGroundContact::Settling),
              UShipHUDWidget::GroundLine(420.0, -50.0, EFlightHold::Free));
    TestFalse(TEXT("and never says LANDED in the air"),
              UShipHUDWidget::GroundLine(84000.0, -300.0, EFlightHold::Free, EGroundContact::Airborne).Contains(TEXT("LANDED")));
    return true;
}

#endif
```

- [ ] **Step 2: Build to prove it fails.** `./build.sh`. Expected: FAIL, `GroundLine` does not take 4 arguments.

- [ ] **Step 3: Write it.** In `ShipHUDWidget.h`, `GroundLine` becomes (add `#include "Ship/ShipLanding.h"` if absent):

```cpp
    static FString GroundLine(double GroundAltitudeCm, double VerticalSpeedCmPerSecond, EFlightHold Hold,
                              EGroundContact Contact = EGroundContact::Airborne);
```

In `ShipHUDWidget.cpp`, the definition gains the parameter (without the default) and begins:

```cpp
    if (Contact == EGroundContact::Landed)
    {
        // At rest on three feet (landing decision 11): the deck's height above
        // the rock and the one word. No hold and no rate: nothing is moving.
        return AltitudeWords(GroundAltitudeCm) + TEXT(" ABOVE GROUND") + NavText::Separator + TEXT("LANDED");
    }
```

and in `AltitudeLineText` the call Task S6 wrote,
`GroundLine(*Agl, Flight.GetVerticalSpeed(), ShownHold(Flight.GetHold(), Flight.GetHeldFraction()))`,
becomes `GroundLine(*Agl, Flight.GetVerticalSpeed(), ShownHold(Flight.GetHold(), Flight.GetHeldFraction()), Flight.GetContact())`.

- [ ] **Step 4: Build and run.** `./build.sh`; `./test.sh DeepSpace.UI.HUDLanded` and `./test.sh DeepSpace.UI.HUDAltitude`. Expected: both pass.

- [ ] **Step 5: Commit.**

```bash
git add Source/DeepSpace/UI/ShipHUDWidget.h Source/DeepSpace/UI/ShipHUDWidget.cpp Source/DeepSpace/Tests/ShipHUDLandedTest.cpp
git commit -F - <<'EOF'
feat(landing): the altitude corner says LANDED

At rest on the ground the corner reads "1.5 M ABOVE GROUND · LANDED": the
deck's height above the rock and the one word, with no hold and no rate.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

- [ ] **Step 6: Prove it can fail.**

```bash
Tools/mutate.sh Source/DeepSpace/UI/ShipHUDWidget.cpp 'if (Contact == EGroundContact::Landed)' 'if (Contact == EGroundContact::Settling)' DeepSpace.UI.HUDLanded
```

Expected: `KILLED`. Then `./build.sh`.

---

## Task 45 (C6): Playtest flights on the fixture worlds

What the slice (c) playtest will try, done here first through the pawn's hands
and read back in the words the HUD prints.

**Files:**
- Create: `Source/DeepSpace/Tests/PlaytestTouchdownTest.cpp` (beside Task S8's `PlaytestLandingTest.cpp`, which flies slice (b)'s hover)

**Interfaces:**
- Consumes: Tasks C1-C5; `LandingPlaytest.h`; slice (b)'s `void ADeepSpaceCharacter::TapVertical(int32 Direction)`, `void HoldVertical(int32 Direction)` (S5), `SetVerticalLever`, `float GetHoldWatts() const`; existing `ADeepSpaceCharacter::TapLever`, `HoldLever`, `SetFlightInput`, `PressStop`, `StockShip::Install`, `SkyTestWorld::FSkyWorld`.
- Produces: `DeepSpace.Playtest.LandsGently`, `.StarvedShipTakesOff`, `.LandedShipStandsStill`, `.LandsOnEveryKind`.

- [ ] **Step 1: Write the tests.** Create `Source/DeepSpace/Tests/PlaytestTouchdownTest.cpp`:

```cpp
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipLanding.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Tests/LandingPlaytest.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"
#include "UI/NavText.h"
#include "UI/ShipHUDWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing slice (c), end to end: what the playtest will try, through the
 * pawn's hands on the stock ship, over the spec's fixture worlds. Siblings,
 * and no test path with children.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestLandsGentlyTest, "DeepSpace.Playtest.LandsGently",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestStarvedShipTakesOffTest, "DeepSpace.Playtest.StarvedShipTakesOff",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestLandedShipStandsStillTest, "DeepSpace.Playtest.LandedShipStandsStill",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestLandsOnEveryKindTest, "DeepSpace.Playtest.LandsOnEveryKind",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PlaytestTouchdownLocal
{
    using namespace LandingPlaytest;

    void Frame(ADeepSpaceCharacter* Player, UShipSubsystem* Ship, float Seconds = Dt)
    {
        Player->Tick(Seconds);
        Ship->Tick(Seconds);
    }

    /** What a landing did, measured every frame. */
    struct FLanding
    {
        bool bLanded = false;
        double Seconds = 0.0;
        double DeepestCm = 0.0;
        double ContactSpeed = -1.0;
    };

    /** C pressed and held until LANDED or MaxSeconds. */
    FLanding HoldC(ADeepSpaceCharacter* Player, UShipSubsystem* Ship, double MaxSeconds)
    {
        const FShipFlightState& Flight = Ship->GetFlightState();
        FLanding Out;
        Player->TapVertical(-1);
        Player->HoldVertical(-1);
        while (Out.Seconds < MaxSeconds && Flight.GetContact() != EGroundContact::Landed)
        {
            Frame(Player, Ship);
            Out.Seconds += Dt;
            const double Clearance = Flight.GetFootprintClearance().Get(TNumericLimits<double>::Max());
            Out.DeepestCm = FMath::Max(Out.DeepestCm, -Clearance);
            if (Out.ContactSpeed < 0.0 && Clearance <= 1.0)
            {
                Out.ContactSpeed = FMath::Max(0.0, -Flight.GetVerticalSpeed());
            }
        }
        Out.bLanded = Flight.GetContact() == EGroundContact::Landed;
        return Out;
    }

    /** The world, checked by name and kind: priors that move it fail here. */
    TOptional<FWorldAt> Named(FAutomationTestBase& Test, const UUniverseSubsystem& Universe, const FSystemId& Id, int32 Orbit,
                              const TCHAR* Name, EPlanetKind Kind)
    {
        const TOptional<FWorldAt> World = WorldAt(Universe, Id, Orbit);
        const bool bRight = World && NavText::WorldName(World->Planet) == Name && World->Planet.Kind == Kind;
        Test.TestTrue(FString::Printf(TEXT("%s is where the fixture table says, of the kind it says"), Name), bRight);
        return bRight ? World : TOptional<FWorldAt>();
    }
}

bool FPlaytestLandsGentlyTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTouchdownLocal;
    SkyTestWorld::FSkyWorld Test(TEXT("PlaytestLandsGentlyWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>();
    const TOptional<FWorldAt> IV = Named(*this, *Test.Universe, Baemsekai, OrbitIV, TEXT("Baemsekai IV"), EPlanetKind::Barren);
    if (!TestNotNull(TEXT("the pilot spawns"), Player) || !IV)
    {
        return false;
    }
    Ship->SetPilot(Player);
    const FShipFlightState& Flight = Ship->GetFlightState();

    // At its drive floor, where every landing begins.
    const FVector Up = DaySide(*IV);
    const double FloorAgl = IV->Body.Radius + UShipSubsystem::FloorFor(IV->Body) - GroundRadius(*IV, Up);
    PlaceOver(*Ship, *IV, Up, FloorAgl, FVector::ZeroVector);
    Frame(Player, Ship, 0.0f);

    // Thirty seconds of whatever the hands do, from a seeded stream so a
    // failure replays: both levers, taps and holds, yaw, and X. Pitch and
    // roll near the ground are GroundAlwaysCatches' and .Settle's.
    FRandomStream Hands(20260927);
    double Deepest = 0.0;
    for (int32 Tick = 0; Tick < 900; ++Tick)
    {
        switch (Hands.RandRange(0, 7))
        {
        case 0: Player->TapLever(Hands.RandRange(0, 1) ? 1 : -1); break;
        case 1: Player->HoldLever(Hands.RandRange(-1, 1)); break;
        case 2: Player->TapVertical(Hands.RandRange(0, 1) ? 1 : -1); break;
        case 3: Player->HoldVertical(Hands.RandRange(-1, 1)); break;
        case 4: Player->SetFlightInput(FVector(0.0, 0.0, Hands.FRandRange(-1.0f, 1.0f))); break;
        case 5: Player->SetFlightInput(FVector::ZeroVector); break;
        case 6:
            if (Hands.RandRange(0, 9) == 0)
            {
                Player->PressStop();
            }
            break;
        default: break;
        }
        Frame(Player, Ship);
        Deepest = FMath::Max(Deepest, -Flight.GetFootprintClearance().Get(TNumericLimits<double>::Max()));
    }

    // Let go, all stop, and come down on C.
    Player->HoldLever(0);
    Player->HoldVertical(0);
    Player->SetFlightInput(FVector::ZeroVector);
    Player->PressStop();
    Frame(Player, Ship);
    Frame(Player, Ship);
    const FLanding Landing = HoldC(Player, Ship, 400.0);
    Deepest = FMath::Max(Deepest, Landing.DeepestCm);
    TestTrue(FString::Printf(TEXT("it lands (%.0f s)"), Landing.Seconds), Landing.bLanded);
    TestTrue(FString::Printf(TEXT("no foot or corner ever a centimetre into the ground (%.2f cm)"), Deepest),
             Deepest <= ShipLanding::LandedPenetrationCm);
    TestTrue(FString::Printf(TEXT("contact at touchdown speed or under (%.2f cm/s)"), Landing.ContactSpeed),
             Landing.ContactSpeed >= 0.0 && Landing.ContactSpeed <= Flight.GetLimits().TouchdownSpeed + 1e-3);
    const FString Corner = UShipHUDWidget::AltitudeLineText(*Ship).ToString();
    TestTrue(FString::Printf(TEXT("the corner reads LANDED: \"%s\""), *Corner), Corner.Contains(TEXT("LANDED")));
    TestEqual(TEXT("cruise is at STOP"), Flight.GetCommand().Throttle, 0.0);
    TestEqual(TEXT("and the vertical lever at HOVER"), Flight.GetCommand().Vertical, 0.0);

    // Still holding C, and now yaw and Shift too: nothing moves.
    const FUniversePosition Where = Flight.GetUniversePosition();
    const FQuat Facing = Flight.GetUniverseOrientation();
    Player->HoldLever(1);
    Player->SetFlightInput(FVector(0.0, 0.0, 1.0));
    for (int32 Tick = 0; Tick < 60; ++Tick)
    {
        Frame(Player, Ship);
    }
    TestTrue(TEXT("landed, no key moves it"),
             Flight.GetContact() == EGroundContact::Landed && Where.DistanceTo(Flight.GetUniversePosition()) == 0.0
                 && Facing.Equals(Flight.GetUniverseOrientation(), 0.0));

    // The hiss falls silent and the boosters' watts go back to the split.
    TestEqual(TEXT("no hold is paid landed"), Ship->GetHoldWatts(), 0.0f);
    TestEqual(TEXT("the boosters want their 450 W"), Ship->GetConsumerWant(ShipPower::Boosters), 450.0f);
    return true;
}

bool FPlaytestStarvedShipTakesOffTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTouchdownLocal;
    SkyTestWorld::FSkyWorld Test(TEXT("PlaytestStarvedTakeOffWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>();
    const TOptional<FWorldAt> IV = Named(*this, *Test.Universe, Baemsekai, OrbitIV, TEXT("Baemsekai IV"), EPlanetKind::Barren);
    if (!TestNotNull(TEXT("the pilot spawns"), Player) || !IV)
    {
        return false;
    }
    Ship->SetPilot(Player);
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    const FShipFlightState& Flight = Ship->GetFlightState();
    PlaceOver(*Ship, *IV, DaySide(*IV), 3000.0, FVector::ZeroVector);
    Frame(Player, Ship, 0.0f);
    const FLanding Landing = HoldC(Player, Ship, 120.0);
    TestTrue(TEXT("starved, it lands"), Landing.bLanded);
    Player->HoldVertical(0);
    Frame(Player, Ship);

    // A fresh Space, held.
    const double Start = Flight.GetGroundAltitude().Get(0.0);
    Player->TapVertical(1);
    Player->HoldVertical(1);
    Frame(Player, Ship);
    bool bUp = true;
    for (int32 Tick = 0; Tick < 300; ++Tick)
    {
        Frame(Player, Ship);
        bUp &= Flight.GetContact() == EGroundContact::Airborne;
    }
    TestTrue(TEXT("starved, a fresh Space lifts it and it stays up"), bUp);
    TestTrue(FString::Printf(TEXT("and climbs (%.1f m in ten seconds)"), (Flight.GetGroundAltitude().Get(0.0) - Start) / 100.0),
             Flight.GetGroundAltitude().Get(0.0) - Start >= 2000.0);
    return true;
}

bool FPlaytestLandedShipStandsStillTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTouchdownLocal;
    SkyTestWorld::FSkyWorld Test(TEXT("PlaytestLandedStandsStillWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>();
    const TOptional<FWorldAt> IV = Named(*this, *Test.Universe, Baemsekai, OrbitIV, TEXT("Baemsekai IV"), EPlanetKind::Barren);
    if (!TestNotNull(TEXT("the pilot spawns"), Player) || !IV)
    {
        return false;
    }
    Ship->SetPilot(Player);
    const FShipFlightState& Flight = Ship->GetFlightState();
    PlaceOver(*Ship, *IV, DaySide(*IV), 3000.0, FVector::ZeroVector);
    Frame(Player, Ship, 0.0f);
    TestTrue(TEXT("it lands"), HoldC(Player, Ship, 120.0).bLanded);

    // The pilot stands up, the boosters are starved, and two minutes pass.
    Player->HoldVertical(0);
    Ship->ClearPilot();
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    const FUniversePosition Where = Flight.GetUniversePosition();
    const FQuat Facing = Flight.GetUniverseOrientation();
    bool bStill = true;
    for (int32 Tick = 0; Tick < 3600; ++Tick)
    {
        Ship->Tick(Dt);
        bStill &= Flight.GetContact() == EGroundContact::Landed;
    }
    TestTrue(TEXT("nobody at the helm and starved, it stays landed"), bStill);
    TestTrue(TEXT("not a centimetre moved, not a hair turned"),
             Where.DistanceTo(Flight.GetUniversePosition()) == 0.0 && Facing.Equals(Flight.GetUniverseOrientation(), 0.0));
    TestEqual(TEXT("and it pays nothing to stay"), Ship->GetConsumerWant(ShipPower::Boosters), 450.0f);
    TestEqual(TEXT("so the hold is silent"), Ship->GetHoldWatts(), 0.0f);
    return true;
}

bool FPlaytestLandsOnEveryKindTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTouchdownLocal;
    SkyTestWorld::FSkyWorld Test(TEXT("PlaytestLandsOnEveryKindWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>();
    if (!TestNotNull(TEXT("the pilot spawns"), Player))
    {
        return false;
    }
    Ship->SetPilot(Player);
    const FShipFlightState& Flight = Ship->GetFlightState();
    struct FWhere
    {
        FSystemId System;
        int32 Orbit;
        const TCHAR* Name;
        EPlanetKind Kind;
    };
    const FWhere Worlds[] = {
        { Baemsekai, OrbitIV, TEXT("Baemsekai IV"), EPlanetKind::Barren },
        { Baemsekai, OrbitV, TEXT("Baemsekai V"), EPlanetKind::Terrestrial },
        { Gasfe, OrbitVII, TEXT("Gasfe VII"), EPlanetKind::Ice },
    };
    for (const FWhere& Where : Worlds)
    {
        const TOptional<FWorldAt> World = Named(*this, *Test.Universe, Where.System, Where.Orbit, Where.Name, Where.Kind);
        if (!World)
        {
            continue;
        }
        Player->HoldVertical(0);
        PlaceOver(*Ship, *World, DaySide(*World), 3000.0, FVector::ZeroVector);
        Frame(Player, Ship, 0.0f);
        Frame(Player, Ship);
        const FLanding Landing = HoldC(Player, Ship, 120.0);
        TestTrue(FString::Printf(TEXT("%s: lands (%.0f s)"), Where.Name, Landing.Seconds), Landing.bLanded);
        TestTrue(FString::Printf(TEXT("%s: gently (deepest %.2f cm, contact %.2f cm/s)"), Where.Name, Landing.DeepestCm, Landing.ContactSpeed),
                 Landing.DeepestCm <= ShipLanding::LandedPenetrationCm && Landing.ContactSpeed <= Flight.GetLimits().TouchdownSpeed + 1e-3);
        const double Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
            Flight.GetUniverseOrientation().GetUpVector() | Flight.GetRestPlane().Normal, -1.0, 1.0)));
        TestTrue(FString::Printf(TEXT("%s: on its rest plane (%.3f deg)"), Where.Name, Off), Off <= 0.1);
        TestTrue(FString::Printf(TEXT("%s: the corner reads LANDED"), Where.Name),
                 UShipHUDWidget::AltitudeLineText(*Ship).ToString().Contains(TEXT("LANDED")));
    }
    return true;
}

#endif
```

- [ ] **Step 2: Build and run.** `./build.sh`; then each of `./test.sh DeepSpace.Playtest.LandsGently`, `./test.sh DeepSpace.Playtest.StarvedShipTakesOff`, `./test.sh DeepSpace.Playtest.LandedShipStandsStill`, `./test.sh DeepSpace.Playtest.LandsOnEveryKind`. Expected: `passed: 1` each. These are written after the code they fly, so their failing half is Step 4's mutations: every one must be killed, or the flight proves nothing.

- [ ] **Step 3: Commit.**

```bash
git add Source/DeepSpace/Tests/PlaytestTouchdownTest.cpp
git commit -F - <<'EOF'
test(landing): the slice (c) playtest, flown first

Through the pawn's hands on the stock ship: from Baemsekai IV's drive floor
with the levers mashed, down to LANDED, gently, and nothing moves it; starved,
a fresh Space lifts it; nobody at the helm and starved, it stays landed and
pays nothing; and it lands on barren, terrestrial and ice.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

- [ ] **Step 4: Prove every flight can fail.**

```bash
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'Command.Vertical = FMath::Max(0.0, Command.Vertical);' 'Command.Vertical = Command.Vertical;' DeepSpace.Playtest.LandsGently
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'if (Command.Vertical > 0.0 || !FeetOverGround(Ground, Gap, Up))' 'if (Command.Vertical > 2.0 || !FeetOverGround(Ground, Gap, Up))' DeepSpace.Playtest.StarvedShipTakesOff
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'FlightState.GetContact() != EGroundContact::Landed);' 'true);' DeepSpace.Playtest.LandedShipStandsStill
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'In.VerticalSpeedCmPerS = GetVerticalSpeed();' 'In.VerticalSpeedCmPerS = -GetVerticalSpeed();' DeepSpace.Playtest.LandsOnEveryKind
```

Expected: all four `KILLED` (held C sinks the lever while landed and the HUD
words or the stillness go; take-off never comes; a landed ship pays for its
hold; nothing ever settles). Then `./build.sh`.

---

## Task 46 (C7): the settle as a writer -- FlightAuthority, ADR 0005, CLAUDE.md

**Files:**
- Modify: `Source/DeepSpace/Tests/ShipFlightAuthorityTest.cpp` (a block at the end of the `if (TestNotNull(...Ship))` body)
- Modify: `docs/decisions/0005-the-ship-is-the-origin.md` (append an amendment)
- Modify: `CLAUDE.md` (the *Landing* section slice (b) wrote; the tunables table; *The system map and the target*; *The drive and the jump*; *Flying*'s key table)

**Interfaces:**
- Consumes: everything above.
- Produces: the writer accounting in `DeepSpace.Ship.FlightAuthority`; the documentation.

- [ ] **Step 1: Write the failing test.** In `ShipFlightAuthorityTest.cpp`, add includes `#include "Ship/ShipLanding.h"`, `#include "Ship/ShipPowerState.h"`, `#include "Tests/LandingPlaytest.h"`, `#include "Universe/UniverseSubsystem.h"`, and append inside the `if (TestNotNull(TEXT("the world has a ship subsystem"), Ship))` block, after the last `TestFalse`:

```cpp
        // The settle is the one writer of orientation besides the pilot (ADR
        // 0005, the landing amendment): from the tick, and only while
        // Settling. Starved over Baemsekai IV with nobody at the helm, the
        // ship sinks, settles and lands, and no frame turns it that neither
        // began nor ended Settling.
        const UUniverseSubsystem* Cosmos = World->GetSubsystem<UUniverseSubsystem>();
        const TOptional<LandingPlaytest::FWorldAt> IV = Cosmos
            ? LandingPlaytest::WorldAt(*Cosmos, LandingPlaytest::Baemsekai, LandingPlaytest::OrbitIV)
            : TOptional<LandingPlaytest::FWorldAt>();
        if (TestTrue(TEXT("Baemsekai IV is there"), IV.IsSet()))
        {
            Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
            LandingPlaytest::PlaceOver(*Ship, *IV, LandingPlaytest::DaySide(*IV), 1200.0, FVector::ZeroVector);
            const FShipFlightState& Flight = Ship->GetFlightState();
            bool bOnlySettling = true;
            int32 Turned = 0;
            for (double Seconds = 0.0; Seconds < 120.0 && Flight.GetContact() != EGroundContact::Landed; Seconds += LandingPlaytest::Dt)
            {
                const EGroundContact Before = Flight.GetContact();
                const FQuat Facing = Flight.GetUniverseOrientation();
                Ship->Tick(LandingPlaytest::Dt);
                const EGroundContact After = Flight.GetContact();
                if (!Flight.GetUniverseOrientation().Equals(Facing, 0.0))
                {
                    ++Turned;
                    bOnlySettling &= Before == EGroundContact::Settling || After == EGroundContact::Settling
                        || After == EGroundContact::Landed;
                }
            }
            TestEqual(TEXT("starved with nobody at the helm, it comes down and lands"), Flight.GetContact(), EGroundContact::Landed);
            TestTrue(FString::Printf(TEXT("the settle turned it (%d frames)"), Turned), Turned > 0);
            TestTrue(TEXT("and nothing but the settle, only while settling"), bOnlySettling);
        }
```

The existing test's world is created without `BeginPlay`; add after `Context.SetCurrentWorld(World);`:

```cpp
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
```

and before `GEngine->DestroyWorldContext(World);`:

```cpp
    World->EndPlay(EEndPlayReason::Quit);
```

(the universe subsystem answers from `Initialize`, and a world torn down without `EndPlay` fails `./test.sh`'s log check).

- [ ] **Step 2: Run to prove it is live.** `./build.sh`; `./test.sh DeepSpace.Ship.FlightAuthority`. Expected: PASS -- the behaviour exists since Task C2; this test's failing half is Step 5's mutation, which must be killed.

- [ ] **Step 3: Amend ADR 0005.** Append to `docs/decisions/0005-the-ship-is-the-origin.md`:

```markdown
## Amendment, 2026-09-27: the settle and the landed re-seat (landing slice c)

The landing spec (`docs/superpowers/specs/2026-09-27-landing-design.md`,
decision 11) changes what the flight state writes from its own tick.
Recorded here because this ADR counts the writers. (Gravity, held and never
integrated, is the amendment above, from slice (b).)

**The settle is a new writer of orientation, from the tick.** Until landing,
only the pilot's command turned the ship, and `JumpTo` deliberately never
does, because turning the ship turns the dome. Under `ds.Land.SettleBand`,
coming down, the ship is `Settling`, and `FShipFlightState::SubStep` turns
body +Z onto the rest plane under its gear at most `ds.Land.SettleDegPerSec`,
added to the pilot's turn, in roll and pitch only. It is the first thing but
the pilot to turn the ship. It is slow, a few degrees at the end of a descent
the pilot is flying, and it reads as the ship sitting down. The fold is held
while Settling, so the settle can never be what brings the nose onto a
course. `DeepSpace.Ship.FlightAuthority` holds that it turns the ship only
while Settling.

**A landed ship is re-seated from the tick.** Landed, the flight state holds
the ship exactly at rest; if the ground under it moves (a priors reload), it
moves the ship along the radial onto it, up or down, and if the ground goes
(the world is no longer solid), the ship flies from where it is. The ground
hard stop lifts a ship out of the ground in flight the same way (landing
decision 10). Both are the tick's. Sphere floors keep their rule: a ship under
one is never lifted.

**`JumpTo` puts the contact back to `Airborne`**, as `PlaceShip` does, beside
zeroing the velocity (the gravity amendment above). Orientation and angular
velocity are still untouched.

**The frame handoff stays deferred.** Landed on a world that does not spin,
the ship is at rest in universe space and the ship stays the origin. Walking
gravity stays ship −Z, so landed on a slope the deck is level and the
landscape tilted, and `PlaceCamera`'s assumptions hold. The handoff to the
planet's frame arrives with going outside, or with worlds that spin.
```

- [ ] **Step 4: Update CLAUDE.md.** Three edits, each in the section named (the ETA to the ground is Task S7's paragraph already):

(a) At the end of the *Landing* section slice (b) wrote, append:

```markdown
**Touchdown is a tripod.** Four gear feet almost never meet real ground at
once, so the ship rests on three: of the two ways to split the four ground
points under the feet into triangles, the upper one's facet over the origin
is the rest plane (`ShipLanding::RestPlane`), and the fourth foot hangs over
wherever the ground falls away. Coming down under `ds.Land.SettleBand` (8 m of
footprint clearance) the ship is `Settling`: it turns onto that plane at up to
`ds.Land.SettleDegPerSec` (4 deg/s), the pilot's input added, never yaw, and
slows sideways with its clearance. On its tripod (feet within 2 cm, none more
than 1 cm under) and at rest it is `Landed` (`EGroundContact`, changed only in
the substep): velocity and turn exactly zero, cruise to STOP and the vertical
lever to HOVER, and nothing moves it -- no taxiing, the attitude and cruise
keys do nothing, and C cannot ask a sink. **LANDED latches** until the
vertical lever asks a climb: a fresh Space lifts it, starved too, and a held
key does not (the lever's detent at HOVER). Landed, no hold is paid, the hiss
is silent and the boosters want their 450 W. A priors reload re-seats a landed
ship on its new ground; `PlaceShip` and `JumpTo` put it in the air. **The fold
is held while Settling**; landed, an engaged, charged jump opens from the
ground only if the landed nose is already on the course -- otherwise lift
off, aim, and it opens in flight.
```

(b) In *Where each tunable lives*, after slice (b)'s `ds.Land.*` rows:

```markdown
| `ds.Land.SettleBand`, `.SettleDegPerSec` | 8 m (never under 1 m), 4 deg/s | `ShipSubsystem.cpp`, from `ShipLanding::DefaultSettleBandCm`, `MinSettleBandCm`, `DefaultSettleDegPerSec` (`ShipLanding.h`) |
```

and in its closing list of named constants with tests, add: "the landed
tolerances `ShipLanding::LandedPenetrationCm` (1 cm), `TripodToleranceCm` (2
cm) and `AtRestCmPerSecond` (1 cm/s)".

(c) In *The drive and the jump*, after the sentence ending "**the fold opens
by itself.**", add: "Except while the ship is settling onto the ground: the
settle turns the nose on the game's initiative, so the fold waits for it
(landing decision 11)." In *Flying*'s key table, the Space row's *Does* cell
gains "; landed, a fresh press lifts off".

- [ ] **Step 5: Commit, then prove the accounting can fail.**

```bash
git add Source/DeepSpace/Tests/ShipFlightAuthorityTest.cpp docs/decisions/0005-the-ship-is-the-origin.md CLAUDE.md
git commit -F - <<'EOF'
docs(landing): the settle is a writer; ADR 0005 amended, CLAUDE.md's landing

FlightAuthority now holds that the settle is the only thing but the pilot
that turns the ship, and only while Settling. ADR 0005 records gravity held
and never integrated, the settle and the landed re-seat as the tick's
writes, JumpTo's arrival at rest (its old amendment said otherwise), and the
frame handoff still deferred. CLAUDE.md gains the tripod, LANDED and its
latch, the fold from the ground and the two tunables.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
Tools/mutate.sh Source/DeepSpace/Ship/ShipFlightState.cpp 'if (Contact == EGroundContact::Settling)' 'if (Contact != EGroundContact::Landed)' DeepSpace.Ship.FlightAuthority
```

Expected: `KILLED` (airborne, the ship turns toward a stale rest plane). Then
`./build.sh`.

---

## Task 47 (C8): done when -- the suite, then the developer lands at 4K

- [ ] **Step 1: The whole suite.** `./build.sh`, then `./test.sh` (every DeepSpace test and the Python tests). Expected: `python: N files passed`, a `passed:` count, no `FAILED`, exit 0. Compare the count against the tests defined (`grep -c IMPLEMENT_SIMPLE_AUTOMATION_TEST Source/DeepSpace/Tests/*.cpp | awk -F: '{s+=$2} END {print s}'` less the `Eyes.` tests): equal, or a test path has become a group.

- [ ] **Step 2: What need not run, and why.** No `UPROPERTY`, component or `BlueprintImplementableEvent` was removed or renamed, so `Tools/check_blueprints.py` is not required; no generated actor's components changed, so neither `build_hauler.py` nor `verify_level.py` is; no key was bound, so `setup_flight_input.py` is not re-run. State these three in the merge message.

- [ ] **Step 3: Open the editor.** `./rebuild.sh --force --launch` (clears stray hot-reload libraries, verifies the manifest, opens the project through `unreal-editor`). Play in a new window at 3840 x 2160, with the project's anti-aliasing and upscaler as configured -- change nothing. Backtick opens the console. `stat unit` on.

- [ ] **Step 4: Baemsekai IV, barren.** `ds.Sky.Goto 4 60` (60 km, above the 55 km hand-back; the drive floor is under 20 km, so a start at 30 km would never cross the handover). Sit at the helm, facing the world. One Shift (the drive at 1 km/s): the soft cap brings the ship down through the handover at 50 km to the drive floor -- no hole, flicker, brightness step or jump there. At the floor, F (cruise's lever live), then C held: down to the ground. Confirm, and note:
  - the corner goes `… ABOVE GROUND · SINKING …`, then `1.5 M ABOVE GROUND · LANDED` (or the local deck height) at rest, with no bounce;
  - the landscape through the windows is level with the tilt the ship took (on a slope, the deck stays level and the horizon tilts);
  - the hiss falls silent at touchdown; the engineering console's allocation shows the boosters back at 450 W;
  - `stat unit`'s Game, Draw (render thread) and GPU times hovering at 1 km and landed, each under 16.6 ms, and `ds.Terrain.Describe`'s tile count landed;
  - stand up (E), walk to the galley and back, look out of each window; sit again;
  - Shift, Ctrl, W/A/S/D/Q/Z and C do nothing landed; one fresh Space lifts it.

- [ ] **Step 5: Starved, and the fold from the ground.** On IV, landed: engineering console, boosters' weight to 0; one fresh Space lifts it, and C lands it again. Then `ds.Nav.Near`, `ds.Nav.Plot 0`, `ds.Nav.ChargeSeconds 5`, `ds.Nav.Engage`: with the landed nose off the course nothing opens; lift off, aim at the course with the helm, and the fold opens in flight.

- [ ] **Step 6: Baemsekai V, terrestrial.** `ds.Sky.Goto 5 30`; land as in Step 4.

- [ ] **Step 7: Gasfe VII, ice.** `ds.Nav.Near`, find Gasfe's row, `ds.Nav.Plot <row>`, `ds.Nav.ChargeSeconds 5`, `ds.Nav.Engage`, aim; after arrival, `ds.Sky.Goto 7 30`; land as in Step 4.

- [ ] **Step 8: Report.** The done-when holds when every check in Steps 4-7 held and the frame stayed under 16.6 ms at 4K. Put the `stat unit` numbers (game, render thread, GPU, at 1 km and landed, on each world) and the tile counts in the merge description, beside the playtest items for the developer: sign-off items 10-13 are what the slice (c) playtest judges -- the 1.5 m clearance, the 0.5 m/s contact, the 4 s ease, the 8 m settle at 4 deg/s, levers to STOP/HOVER at touchdown, the fold from the ground, and the ETA to the ground.
